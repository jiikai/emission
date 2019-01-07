/*
    FILE:       psql-db.c
    IMPLEMENTS: psql-db.h
*/

/*
** INCLUDES
*/

#include <stdatomic.h>
#include "psql-db.h"
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <unistd.h>

#include "utlist.h"
#include "dbg.h"

/*
**  MACROS
*/

#define FREE_2D_ARRAY(arr, count)\
    for (int i = 0; i < count; i++) {\
        if (arr[i]) free(arr[i]);\
    }\
    free(arr)

#define SET_TIMER_S_MS(timer, _s_timer_val, _ms_timer_val)\
    timer.tv_sec = _s_timer_val;\
    timer.tv_nsec = (_ms_timer_val * 1000000L)

#define PRINT_STR_ARRAY(stream, arr, len, prepend)\
    fprintf(stream, "%s{", prepend);\
    for (uint8_t i = 0; i < len - 1; i++) {\
        fprintf(stream, "%s, ", arr[i]);\
    }\
    fprintf(stream, "%s}\n", arr[len - 1])

/*
**  STRUCTURES & TYPES
*/

/*  Definition of query data structure declared and typedef'd in header. */
struct psqldb_query_data {
    char                       *stmt_or_cmd;
    char                       *param_values[PSQLDB_MAX_NPARAMS];
    int                         param_lengths[PSQLDB_MAX_NPARAMS];
    uint8_t                     nparams;
    uint8_t                     lock_until_complete;
    psqldb_res_handler_ft      *res_callback;
    void                       *cb_arg;
};

/*  Declaration & definition of query queue list item structure. */
struct queue_elem {
    psqldb_query_data_st       *data;
    struct queue_elem          *next;
};

/*  Main context structure declared and typedef'd in header. */
struct psqldb_conn_ctx {
    char                       *db_url;
    struct queue_elem          *queue_head;
    struct queue_elem          *queue_tail;
    volatile atomic_bool        queue_empty;
    volatile atomic_flag        queue_lock;
    volatile atomic_bool        thread_continue;
    volatile uint8_t            thread_state[PSQLDB_NTHREADS][PSQLDB_NCONN_PER_THREAD];
    pthread_t                   thread_ids[PSQLDB_NTHREADS];
    uint8_t                     thread_retval[PSQLDB_NTHREADS];
};

/*  Declaration & definition of connection query thread context structure. */
struct async_query_thread_ctx {
    PGconn                    **pgconn;
    volatile atomic_flag        pgconn_lock[PSQLDB_NCONN_PER_THREAD];
    struct psqldb_conn_ctx     *conn_ctx;
    uint8_t                     nthread;
};

/*  Declaration & definition of connection poller thread context structure. */
struct async_poll_thread_ctx {
    PGconn                    **pgconn_ptr;
    volatile atomic_flag       *pgconn_lock_ptr;
    volatile uint8_t           *pgconn_thread_state_ptr;
    struct psqldb_query_data   *query_data;
};

/*
**  FUNCTIONS
*/

/*  STATIC INLINE  */

static inline struct async_poll_thread_ctx *
inl_init_poll_thread_ctx(PGconn **conn_ptr, volatile atomic_flag *lock_ptr,
    volatile uint8_t *thread_state_ptr, psqldb_query_data_st *data)
{
    struct async_poll_thread_ctx *poll_ctx;
    poll_ctx = calloc(1, sizeof(struct async_poll_thread_ctx));
    check(poll_ctx, ERR_MEM, PSQLDB);
    poll_ctx->pgconn_ptr = conn_ptr;
    poll_ctx->pgconn_lock_ptr = lock_ptr;
    poll_ctx->query_data = data;
    poll_ctx->pgconn_thread_state_ptr = thread_state_ptr;
    return poll_ctx;
error:
    return NULL;
}

static inline void
inl_free_query_thread_ctx(struct async_query_thread_ctx *thrd_ctx)
{
    if (thrd_ctx) {
        volatile atomic_flag *pgconn_lock = thrd_ctx->pgconn_lock;
        for (int i = 0; i < PSQLDB_NCONN_PER_THREAD; i++) {
            do {
            } while (atomic_flag_test_and_set(&pgconn_lock[i]));
            if (thrd_ctx->pgconn[i] != NULL) {
                PQfinish(thrd_ctx->pgconn[i]);
            }
        }
        free(thrd_ctx->pgconn);
        free(thrd_ctx);
    }
}

/*  STATIC  */

static PGconn *
open_nonblocking_conn(char *conn_info)
{
    PGconn *conn;
    int ret;
    struct timespec timer;
    SET_TIMER_S_MS(timer, 0, 100);
    while (PQping(conn_info) != PQPING_OK) {
        nanosleep(&timer, NULL);
    }
    conn = PQconnectStart(conn_info);
    check(conn, ERR_MEM, PSQLDB);

    struct pollfd pfds;
    ret = PGRES_POLLING_WRITING;
    do {
        pfds.events = ret == PGRES_POLLING_READING
                            ? (0 ^ POLLIN)
                            : (0 ^ POLLOUT);
        pfds.fd = PQsocket(conn);
        poll(&pfds, 1, 5000);
        check(ret != -1, ERR_FAIL, PSQLDB, "polling socket");
        check(ret, ERR_FAIL, PSQLDB, "poll timeout");
        ret = PQconnectPoll(conn);
        check(ret != PGRES_POLLING_FAILED, ERR_EXTERN, "libpq",
            PQerrorMessage(conn));
    } while (ret != PGRES_POLLING_OK);
    if (!PQisnonblocking(conn)) {
        ret = PQsetnonblocking(conn, 1);
        check(ret != -1, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    }
    return conn;
error:
    if (conn) PQfinish(conn);
    return NULL;
}

static int
create_prepared_stmt(PGconn *conn, const char *stmt)
{
    PGresult *res = NULL;
    int ret;
    ret = PQsendQuery(conn, stmt);
    check(ret, ERR_FAIL, PSQLDB, "sending request for a prepared statement");
    res = PQgetResult(conn);
    while (res != NULL) {
        check(PQresultStatus(res) == PGRES_COMMAND_OK, ERR_EXTERN, "libpq",
            PQerrorMessage(conn));
        PQclear(res);
        res = PQgetResult(conn);
    }
    return 1;
error:
    printf("%s\n", PQresStatus(PQresultStatus(res)));
    if (res) PQclear(res);
    return 0;
}

static PGresult *
poll_and_consume(PGconn *conn)
{
    struct pollfd pfds;
    struct timespec timer;
    SET_TIMER_S_MS(timer, 0, 100);
    pfds.events =  0 ^ POLLIN;
    uint8_t attempts = PSQLDB_POLL_ATTEMPTS;
    int ret;
    do {
        pfds.fd = PQsocket(conn);
        ret = poll(&pfds, 1, PSQLDB_POLL_TIMEOUT_MS);
        attempts--;
        check(ret != -1, ERR_FAIL, PSQLDB, "to poll socket");
        if (!ret) {
            log_warn("poll timeout occured, %d attempts left", attempts);
        } else {
            ret = pfds.revents;
            ret &= POLLIN;
            check(ret, ERR_FAIL, PSQLDB, "polling; data not found");
            ret = PQconsumeInput(conn);
            check(ret, ERR_EXTERN, "libpq", PQerrorMessage(conn));
            ret = PQisBusy(conn);
            while (ret) {
                nanosleep(&timer, NULL);
                ret = PQisBusy(conn);
            }
            break;
        }
    } while (attempts);
    check(attempts, ERR_FAIL, PSQLDB, "too many timeouts polling");
    return PQgetResult(conn);
error:
    return NULL;
}

static int
query_concurrent(PGconn *conn, const char *query_or_stmt, char **param_values,
    const int *param_lengths, uint8_t nparams, psqldb_res_handler_ft *callback,
    void *cb_arg, uint8_t blocking)
{
    PGresult *res = NULL;
    int ret;
    if (nparams == 0) {
        /* Interpret query_or_stmt as a ready query */
        ret = PQsendQuery(conn, query_or_stmt);
    } else {
        /* Interpret query_or_stmt as a prepared statement name */
        ret = PQsendQueryPrepared(conn, query_or_stmt, nparams,
            (const char * const *) param_values, param_lengths, NULL, 0);
    }
    check(ret, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    if (blocking) {
        /* Block & wait for the query to return a result. */
        res = poll_and_consume(conn);
        ExecStatusType desired = callback ? PGRES_TUPLES_OK : PGRES_COMMAND_OK;
        if (PQresultStatus(res) != desired) {
            log_err(ERR_EXTERN, "libpq", PQerrorMessage(conn));
        } else if (callback) {
        /* Pass the result set to a callback if one was provided. */
            callback(res, cb_arg);
        }
        /* According to libpq documentation, PQgetResult should
           always be called repeatedly until it returns NULL while
           taking care to PQclear each non-NULL result. */
        while (res) {
            PQclear(res);
            res = PQgetResult(conn);
        }
    }
    return 1;
error:
    /* As above, call PQclear-PQgetResult
       until the latter returns NULL. */
    while (res) {
        PQclear(res);
        res = PQgetResult(conn);
    }
    return 0;
}

static void
enqueue_item(struct queue_elem *item, psqldb_conn_ctx_st *conn_ctx)
{
    do {
    } while (atomic_flag_test_and_set(&conn_ctx->queue_lock));
    if (conn_ctx->queue_head) {
        LL_APPEND_ELEM(conn_ctx->queue_head, conn_ctx->queue_tail, item);
    } else {
        LL_PREPEND(conn_ctx->queue_head, item);
    }
    conn_ctx->queue_tail = item;
    atomic_store_explicit(&conn_ctx->queue_empty, false, memory_order_release);
    atomic_flag_clear(&conn_ctx->queue_lock);
}

static struct queue_elem *
dequeue_item(psqldb_conn_ctx_st *conn_ctx)
{
    volatile atomic_flag *lock = &conn_ctx->queue_lock;
    volatile atomic_bool *empty = &conn_ctx->queue_empty;
    volatile atomic_bool *thrd_continue = &conn_ctx->thread_continue;
    do {
        do {
            bool do_continue, is_empty;
            do {
                /*  Inner loop, continue until either
                a) the queue is non-empty, or
                b) do_continue evaluates to false
                AND the queue is empty. If the latter, return NULL. */
                do_continue = atomic_load_explicit(thrd_continue,
                                memory_order_relaxed);
                is_empty = atomic_load_explicit(empty, memory_order_acquire);
                if (!do_continue && is_empty) {
                    return NULL;
                }
            } while (is_empty);
        } while (atomic_flag_test_and_set(lock));
        struct queue_elem *item = conn_ctx->queue_head;
        if (item != NULL) {
            /* Dequeue head element. */
            LL_DELETE(conn_ctx->queue_head, item);
            /* Set status to empty if head now points to NULL. */
            if (conn_ctx->queue_head == NULL) {
                atomic_store_explicit(empty, true, memory_order_release);
            }
            /* Release lock if not instructed otherwise. */
            if (!(item->data->lock_until_complete)) {
                atomic_flag_clear(lock);
            }
            /* Return dequeued item. */
            return item;
        } else {
            atomic_flag_clear(lock);
        }
    } while (1);
}

static void *
poll_thread_cycle(void *arg)
{
    struct async_poll_thread_ctx *poll_ctx;
    poll_ctx = (struct async_poll_thread_ctx *)arg;
    PGconn *conn = *(poll_ctx->pgconn_ptr);
    PGresult *res = poll_and_consume(conn);
    uint8_t err = 0;
    psqldb_query_data_st *data = poll_ctx->query_data;
    psqldb_res_handler_ft *callback = data->res_callback;
    ExecStatusType desired = callback ? PGRES_TUPLES_OK : PGRES_COMMAND_OK;
    if (PQresultStatus(res) != desired) {
        err = 1;
        log_err(ERR_EXTERN, "libpq", PQresStatus(PQresultStatus(res)));
    }
    while (res != NULL) {
        if (callback) {
           callback(res, data->cb_arg);
        }
        PQclear(res);
        res = PQgetResult(conn);
    }
    if (err) {
        log_err(ERR_FAIL, PSQLDB, "sending below query to database:");
        fprintf(stderr, "%s\n", data->stmt_or_cmd);
        if (data->nparams) {
            PRINT_STR_ARRAY(stderr, data->param_values,
                data->nparams, "PARAMETERS: ");
        }
    }
    psqldb_free_query_data(data);
    atomic_flag_clear(poll_ctx->pgconn_lock_ptr);
    *poll_ctx->pgconn_thread_state_ptr = PSQLDB_THRD_IDLE;
    free(poll_ctx);
    return NULL;
}

static void *
query_thread_cycle(void *arg)
{
    /* Store some stuff in local variables & pointers for performance. */
    struct async_query_thread_ctx *thrd_ctx;
    thrd_ctx = (struct async_query_thread_ctx *)arg;
    psqldb_conn_ctx_st *conn_ctx = thrd_ctx->conn_ctx;
    volatile atomic_bool *queue_empty = &conn_ctx->queue_empty;
    volatile atomic_bool *thrd_continue = &conn_ctx->thread_continue;
    volatile atomic_flag *pgconn_lock = thrd_ctx->pgconn_lock;
    volatile atomic_flag *queue_lock = &conn_ctx->queue_lock;
    uint8_t nthread = thrd_ctx->nthread;
    volatile uint8_t *thrd_state = conn_ctx->thread_state[nthread];
    conn_ctx->thread_retval[nthread] = 0;
    void *retval = (void *)&conn_ctx->thread_retval[nthread];
    PGconn **pgconn = thrd_ctx->pgconn;
    uint8_t nconn = 0;
    int ret = 0;

    pthread_attr_t attr;
    int pthrdret = pthread_attr_init(&attr);
    check(pthrdret == 0, ERR_FAIL, PSQLDB, "initializing thread attributes");
    pthrdret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    check(pthrdret == 0, ERR_FAIL, PSQLDB, "setting thread detach state");
    pthrdret = pthread_attr_setstacksize(&attr, PSQLDB_STACK_SIZE);

    /* Dequeue & send query -loop, continue until both
        a) the thread_continue boolean in the connection context struct is false; and
        b) the pending query queue is empty.
    */

    struct queue_elem *item;
    psqldb_query_data_st *data;
    while (atomic_load(thrd_continue) || !(atomic_load(queue_empty))) {
        item = dequeue_item(conn_ctx);
        /* Break loop when thrd_continue was set in dequeue_item(). */
        if (item == NULL) break;
        /* Find an idle connection. */
        while (atomic_flag_test_and_set(&pgconn_lock[nconn])) {
            nconn = nconn == PSQLDB_NCONN_PER_THREAD - 1 ? 0 : nconn + 1;
        }
        thrd_state[nconn] = PSQLDB_THRD_BUSY;
        data = item->data;
        if (data->lock_until_complete) {
            /* Request for a blocking query. */
            ret = query_concurrent(pgconn[nconn],
                    data->stmt_or_cmd, data->param_values,
                    data->param_lengths, data->nparams,
                    data->res_callback, data->cb_arg,
                    1);
            atomic_flag_clear(&pgconn_lock[nconn]);
            check(ret, ERR_FAIL_A, PSQLDB,
                "sending a prioritized command",
                data->stmt_or_cmd);
            atomic_flag_clear(queue_lock);
            thrd_state[nconn] = PSQLDB_THRD_IDLE;
            free(item);
            item = NULL;
            psqldb_free_query_data(data);
            data = NULL;
        } else {
            /* A nonblocking query. */
            ret = query_concurrent(pgconn[nconn],
                    data->stmt_or_cmd, data->param_values,
                    data->param_lengths, data->nparams,
                    data->res_callback, data->cb_arg,
                    0);
            if (!ret) {
                log_err(ERR_FAIL, PSQLDB, "sending below query to database:");
                fprintf(stderr, "%s\n", data->stmt_or_cmd);
                if (data->nparams) {
                    PRINT_STR_ARRAY(stderr,
                        data->param_values, data->nparams,
                        "PARAMETERS: ");
                }
                atomic_flag_clear(&pgconn_lock[nconn]);
                psqldb_free_query_data(data);
                data = NULL;
            } else {
                /* Let a separate poller thread handle the results. */
                struct async_poll_thread_ctx *poll_ctx;
                poll_ctx = inl_init_poll_thread_ctx(&pgconn[nconn],
                            &pgconn_lock[nconn], &thrd_state[nconn],
                            data);
                pthread_t poll_thrd_id = 0;
                pthrdret = pthread_create(&poll_thrd_id,
                            &attr, poll_thread_cycle,
                            poll_ctx);
                check(pthrdret == 0 && poll_thrd_id,
                    ERR_FAIL, PSQLDB, "creating thread");
                data = NULL;
            }
            free(item);
            item = NULL;
        }
    }
    /* Set return value for success. */
    conn_ctx->thread_retval[nthread] = 1;
error:
    /* Clean up. Return value is zero on error
    (as was set when this function was entered). */
    inl_free_query_thread_ctx(thrd_ctx);
    pthread_attr_destroy(&attr);
    if (item) {
        free(item);
    }
    if (data) {
        psqldb_free_query_data(data);
    }
    thrd_state[nthread] = PSQLDB_THRD_IDLE;
    return retval;
}

static struct async_query_thread_ctx *
init_query_thread_ctx(psqldb_conn_ctx_st *conn_ctx, uint8_t nthread)
{
    struct async_query_thread_ctx *thrd_ctx;
    thrd_ctx = malloc(sizeof(struct async_query_thread_ctx));
    check(thrd_ctx, ERR_MEM, PSQLDB);
    thrd_ctx->nthread = nthread;
    thrd_ctx->conn_ctx = conn_ctx;
    thrd_ctx->pgconn = calloc(PSQLDB_NCONN_PER_THREAD, sizeof(PGconn *));
    check(thrd_ctx->pgconn, ERR_MEM, PSQLDB);
    for (uint8_t i = 0; i < PSQLDB_NCONN_PER_THREAD; i++) {
        thrd_ctx->pgconn[i] = open_nonblocking_conn(conn_ctx->db_url);
        check(thrd_ctx->pgconn[i], ERR_FAIL, PSQLDB,
            "opening connection for thread");
        atomic_flag_clear_explicit(&thrd_ctx->pgconn_lock[i],
            memory_order_relaxed);
    }
    return thrd_ctx;
error:
    return 0;
}

/*  PROTOTYPE IMPLEMENTATIONS  */

int
psqldb_blocking_query(psqldb_conn_ctx_st *ctx, char *stmt_or_cmd,
    char **param_values, int *param_lengths, uint8_t nparams,
    psqldb_res_handler_ft *callback, void *cb_arg)
{
    PGconn *conn = open_nonblocking_conn(ctx->db_url);
    check(conn, ERR_FAIL, PSQLDB, "obtaining a connection");
    PGresult *res;
    /* Run a blocking query. */
    if (nparams) {
        /* Prepared statement. */
        res = PQexecParams(conn,
                stmt_or_cmd, nparams, NULL,
                (const char * const *)param_values,
                param_lengths, NULL, 0);
    } else {
        /* Straight SQL. */
        res = PQexec(conn, stmt_or_cmd);
    }
    ExecStatusType desired = callback ? PGRES_TUPLES_OK : PGRES_COMMAND_OK;
    if (PQresultStatus(res) != desired) {
        log_err(ERR_EXTERN, "libpq", PQerrorMessage(conn));
    } else if (callback) {
    /* Pass the result set to a callback if one was provided. */
        callback(res, cb_arg);
    }
    /* According to libpq documentation, PQgetResult should
       always be called repeatedly until it returns NULL while
       taking care to PQclear() each non-NULL result.
    */
    while (res) {
        PQclear(res);
        res = PQgetResult(conn);
    }
    PQfinish(conn);
    return 1;
error:
    return 0;
}

int
psqldb_enqueue_query(psqldb_conn_ctx_st *conn_ctx, psqldb_query_data_st *qr_dt)
{
    struct queue_elem *el = malloc(sizeof(struct queue_elem));
    check(el, ERR_MEM, PSQLDB);
    el->data = qr_dt;
    enqueue_item(el, conn_ctx);
    return 1;
error:
    return 0;
}

/* Context and thread initializers and deallocators. */

psqldb_conn_ctx_st *
psqldb_init_conn_ctx(char *db_url)
{
    /* Allocate memory for connection context structure. */
    psqldb_conn_ctx_st *conn_ctx = calloc(1, sizeof(psqldb_conn_ctx_st));
    check(conn_ctx, ERR_MEM, PSQLDB);

    /* Set up db connection information, either provided as a parameter
    or obtained from an environment variable. */
    char *conn_info;
    size_t len;
    char buffer[0x800];
    if (!db_url) {
        db_url = buffer;
        sprintf(db_url, "%s?%s",
            getenv(PSQLDB_DATABASE_URL_ENV), "sslmode=require");
    }
    len = strlen(db_url) + 1;
    conn_info = malloc(sizeof(char) * len);
    check(conn_info, ERR_MEM, PSQLDB);
    memcpy(conn_info, db_url, len);
    conn_ctx->db_url = conn_info;

    /* Set up queue lock and initialize head to NULL. */
    atomic_flag_clear_explicit(&conn_ctx->queue_lock, memory_order_relaxed);
    atomic_init(&conn_ctx->thread_continue, false);
    atomic_init(&conn_ctx->queue_empty, true);
    struct queue_elem *queue_head = NULL;
    conn_ctx->queue_head = queue_head;
    conn_ctx->queue_tail = queue_head;
    return conn_ctx;
error:
    return 0;
}

int psqldb_launch_conn_threads(psqldb_conn_ctx_st *conn_ctx)
{
    int ret = 0;
    check(conn_ctx, ERR_NALLOW, PSQLDB, "NULL conn_ctx argument");

    /* Set thread continue flag to true. */
    atomic_store(&conn_ctx->thread_continue, true);

    /* Initialize thread attributes. */
    pthread_attr_t attr;
	ret = pthread_attr_init(&attr);
    check(ret == 0, ERR_FAIL, PSQLDB, "initializing thread attributes");
    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    check(ret == 0, ERR_FAIL, PSQLDB, "setting thread detach state");
	ret = pthread_attr_setstacksize(&attr, PSQLDB_STACK_SIZE);
    check(ret == 0, ERR_FAIL, PSQLDB, "setting thread stack size");

    /*  Allocate memory for and initialize thread context structs,
    start the threads with the specified attributes and store the
    unique thread identifiers in the connection conn_ctx structure. */
    for (uint8_t i = 0; i < PSQLDB_NTHREADS; i++) {
        struct async_query_thread_ctx *thrd_ctx;
        thrd_ctx = init_query_thread_ctx(conn_ctx, i);
        check(thrd_ctx, ERR_FAIL, PSQLDB, "creating thread context data");
        ret = pthread_create(&conn_ctx->thread_ids[i],
                &attr, query_thread_cycle,
                thrd_ctx);
    	check(ret == 0, ERR_FAIL, PSQLDB, "creating thread");
    }
    ret = 1;
    /* Return 1 on success, 0 on failure. */
error:
    pthread_attr_destroy(&attr);
    return ret;
}

void
psqldb_free_query_data(psqldb_query_data_st *data)
{
    if (data) {
        uint8_t nparams = data->nparams;
        for (uint8_t i = 0; i < nparams; i++) {
            if (data->param_values[i]) {
                free(data->param_values[i]);
            }
        }
        if (data->stmt_or_cmd) {
            free(data->stmt_or_cmd);
        }
        free(data);
    }
}

psqldb_query_data_st *
psqldb_init_query_data(char *stmt_or_cmd, char **param_values,
    int *param_lengths, uint8_t nparams, psqldb_res_handler_ft *callback,
    void *cb_arg, uint8_t lock_until_complete)
{
    psqldb_query_data_st *qr_data = calloc(1, sizeof(psqldb_query_data_st));
    check(qr_data, ERR_MEM, PSQLDB);
    size_t stmt_or_cmd_len = strlen(stmt_or_cmd);
    qr_data->stmt_or_cmd = calloc(stmt_or_cmd_len + 1, sizeof(char));
    check(qr_data->stmt_or_cmd, ERR_MEM, PSQLDB);
    memcpy(qr_data->stmt_or_cmd, stmt_or_cmd, stmt_or_cmd_len + 1);
    if (nparams) {
        for (uint8_t i = 0; i < nparams; i++) {
            qr_data->param_values[i] = calloc(param_lengths[i] + 1, sizeof(char));
            check(qr_data->param_values[i], ERR_MEM, PSQLDB);
            memcpy(qr_data->param_values[i], param_values[i], param_lengths[i]);
            qr_data->param_lengths[i] = param_lengths[i];
        }
    }
    qr_data->res_callback = callback ? callback : NULL;
    qr_data->cb_arg = callback && cb_arg ? cb_arg : NULL;
    qr_data->nparams = nparams;
    qr_data->lock_until_complete = lock_until_complete;
    return qr_data;
error:
    return NULL;
}

int
psqldb_queue_is_empty(psqldb_conn_ctx_st *conn_ctx)
{
    return atomic_load(&conn_ctx->queue_empty);
}

void
psqldb_wait_on_threads_until_idle(psqldb_conn_ctx_st *ctx)
{
	struct timespec timer;
	SET_TIMER_S_MS(timer, 0, 10000000);
	for (uint8_t i = 0; i < PSQLDB_NTHREADS; i++) {
        uint8_t j = 0;
		while (ctx->thread_state[i][j]) {
			nanosleep(&timer, NULL);
		}
	}
}

int
psqldb_stop_and_join_threads(psqldb_conn_ctx_st *conn_ctx)
{
    check(conn_ctx, ERR_NALLOW, PSQLDB, "NULL conn_ctx argument");
    atomic_store(&conn_ctx->thread_continue, false);
    int ret = 0;
    int nerrors = 0;
    for (uint8_t i = 0; i < PSQLDB_NTHREADS; i++) {
        void *retval;
        ret = pthread_join(conn_ctx->thread_ids[i], &retval);
        if (ret) {
            nerrors++;
            log_err(ERR_FAIL, PSQLDB, "joining thread with master");
        }
        if (*(uint8_t *) retval == 0) {
            nerrors++;
            log_err(ERR_FAIL, PSQLDB, "thread return value invalid");
        }
        conn_ctx->thread_ids[i] = 0;
    }
    return nerrors; /* Return the amount of errors: 0 on success. */
error:
    return -1; /* Context argument was NULL. */
}

void
psqldb_free_conn_ctx(psqldb_conn_ctx_st *conn_ctx)
{
    if (conn_ctx) {
        if (conn_ctx->thread_ids[0] != 0) {
            psqldb_stop_and_join_threads(conn_ctx);
        }
        if (conn_ctx->db_url) {
            free(conn_ctx->db_url);
        }
        /*  The if-block below will only run in case of error;
            if all went well, job queue ought to be empty
            by the time the conn context will be freed.
        */
        if (!atomic_load(&conn_ctx->queue_empty)) {
            struct queue_elem *el, *tmp;
            struct queue_elem *head = conn_ctx->queue_head;
            LL_FOREACH_SAFE(head, el, tmp) {
                LL_DELETE(head, el);
                if (el->data) {
                    psqldb_free_query_data(el->data);
                }
                free(el);
            }
        }
        free(conn_ctx);
    }
}
