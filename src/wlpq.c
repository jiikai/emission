/*  @file           wlpq.c
    @version        0.2.0
    @brief          @implements wlpq.h, a PostgreSQL C API (libpq) wrapper.
    @details        See ../README and ../doc/wlpq_api.md.
    @copyright:     (c) Joa Käis (github.com/jiikai) 2017-2018 (MIT). See ../LICENSE.
*/

/*
**  INCLUDES
*/

#include "wlpq.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/poll.h>
#include <sys/time.h>
#include "utlist.h"
#include "dbg.h"

/*
**  MACROS
*/

/*  Define some shorthands. */
#define MAX_NTHRD WLPQ_MAX_NCONNTHREADS
#define MAX_NCONN_THRD WLPQ_MAX_NCONN_PER_THREAD

/*  Define I/O states for database connections. */
#define PGCONN_IOSTATE_IDLE 0
#define PGCONN_IOSTATE_SEND 1
#define PGCONN_IOSTATE_WAIT 2
#define PGCONN_IOSTATE_FLUSH 3
#define PGCONN_IOSTATE_EXIT 4
#define PGCONN_IOSTATE_ERROR 0xF

#define PFDS_INIT(fd_val, events_val)\
    (struct pollfd){.fd = fd_val, .events = events_val}

#define TIMESPEC_INIT_S_MS(s_val, ms_val)\
    (struct timespec) {.tv_sec = s_val, .tv_nsec = ms_val * 1000000L}

#define PRINT_STR_ARRAY(stream, arr, len, prepend)\
    fprintf(stream, "%s{", prepend);\
    for (uint8_t i = 0; i < len - 1; i++) {\
        fprintf(stream, "%s, ", arr[i]);\
    }\
    fprintf(stream, "%s}\n", arr[len - 1])

/*
**  STRUCTURES & TYPES
*/

/*  Defines a function type for polling PG database connections. */
typedef PostgresPollingStatusType connpoll_func_t(PGconn *conn);

/*  Definition of type wlpq_query_data_st. */
struct wlpq_query_data {
    uint8_t                         lock_until_complete;
    unsigned                        nparams;
    union {
        char                       *cmd;
        struct wlpq_prep_stmt {
            char                   *stmt;
            char                   *param_val[WLPQ_MAX_NPARAMS];
            int                     param_len[WLPQ_MAX_NPARAMS];
        }                          *prep_stmt;
    };
    wlpq_res_handler_ft            *res_callback;
    void                           *cb_arg;
};

/*  Declaration & definition of query queue list item structure. */
struct queue_elem {
    unsigned                        conn_id;
    PGconn                        **conn;
    wlpq_query_data_st             *data;
    struct queue_elem              *next;
};

/*  Main context structure declared and typedef'd in header. */
struct wlpq_conn_ctx {
    char                           *db_url;
    wlpq_notify_handler_ft         *notify_cb;
    void                           *notify_cb_arg;
    struct queue_elem              *qqueue_head;
    struct queue_elem              *qqueue_tail;
    volatile atomic_bool            qqueue_empty;
    volatile atomic_flag            qqueue_lock;
    volatile atomic_bool            thread_continue;
    volatile wlpq_thread_state_et   thread_state[MAX_NTHRD];
    pthread_t                       thread_pt_id[MAX_NTHRD];
    unsigned                        thread_nconn;
};

/*  Declaration & definition of connection query thread context structure. */
struct query_thread_ctx {
    wlpq_conn_ctx_st               *conn_ctx;
    PGconn                        **pgconn;
    wlpq_query_data_st            **pgconn_qr_dt;
    struct pollfd                   pgconn_sockfds[MAX_NCONN_THRD];
    volatile uint8_t                pgconn_iostate[MAX_NCONN_THRD];
    unsigned                        nthread;
};

/*
**  STATIC FUNCTIONS
*/

static inline int
inl_init_thread_attr(pthread_attr_t *attr, int detach_state)
{
    int pthrdret = pthread_attr_init(attr);
    check(pthrdret == 0, ERR_FAIL, WLPQ, "initializing thread attributes");
    pthrdret = pthread_attr_setdetachstate(attr, detach_state);
    check(pthrdret == 0, ERR_FAIL, WLPQ, "setting thread detach state");
    pthrdret = pthread_attr_setstacksize(attr, WLPQ_STACK_SIZE);
    check(pthrdret == 0, ERR_FAIL, WLPQ, "setting thread stack state");
    return 1;
error:
    return 0;
}

static inline void
inl_free_query_data(wlpq_query_data_st *data)
{
    uint8_t nparams = data->nparams;
    if (nparams) {
        struct wlpq_prep_stmt *prep_stmt = data->prep_stmt;
        free(prep_stmt->stmt);
        for (uint8_t i = 0; i < nparams; i++)
            if (prep_stmt->param_val[i])
                free(prep_stmt->param_val[i]);
        free(prep_stmt);
    } else
        free(data->cmd);
    free(data);
}

static inline void
inl_free_query_thread_ctx(struct query_thread_ctx *thrd_ctx)
{
    volatile uint8_t *pgconn_iostate = thrd_ctx->pgconn_iostate;
    unsigned nconn = thrd_ctx->conn_ctx->thread_nconn;
    struct timespec timer = TIMESPEC_INIT_S_MS(0, 50);
    for (size_t i = 0; i < nconn; i++) {
        while (pgconn_iostate[i] != PGCONN_IOSTATE_IDLE) {
            if (pgconn_iostate[i] == PGCONN_IOSTATE_ERROR)
                break;
            nanosleep(&timer, NULL);
        }
        if (thrd_ctx->pgconn[i])
            PQfinish(thrd_ctx->pgconn[i]);
    }
    free(thrd_ctx->pgconn_qr_dt);
    free(thrd_ctx->pgconn);
    free(thrd_ctx);
}

static inline PGconn *
inl_open_noblock_conn_start(char *conn_info)
{
    struct timespec timer = TIMESPEC_INIT_S_MS(0, 10);
    while (PQping(conn_info) != PQPING_OK)
        nanosleep(&timer, NULL);
    return PQconnectStart(conn_info);
}

static inline int
inl_open_noblock_conn_poll(PGconn *conn, connpoll_func_t *connpollf)
{
    struct pollfd pfds;
    int ret = PGRES_POLLING_WRITING;
    do {
        pfds.events = ret == PGRES_POLLING_READING ? (0 ^ POLLIN) : (0 ^ POLLOUT);
        pfds.fd = PQsocket(conn);
        ret = poll(&pfds, 1, 5000);
        check(ret != -1, ERR_FAIL, WLPQ, "polling socket");
        check(ret, ERR_FAIL, WLPQ, "poll timeout");
        ret = connpollf(conn);
        check(ret != PGRES_POLLING_FAILED, ERR_EXTERN, "libpq",
            PQerrorMessage(conn));
    } while (ret != PGRES_POLLING_OK);

    if (!PQisnonblocking(conn))
        check(PQsetnonblocking(conn, 1) != -1, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    return 1;
error:
    if (conn)
        PQfinish(conn);
    return 0;
}

static inline void
inl_print_query_data(wlpq_query_data_st *qr_dt, FILE *stream)
{
    uint8_t nparams = qr_dt->nparams;
    fprintf(stream, "Query or prepared statement name:\n%s\n",
        nparams ? qr_dt->prep_stmt->stmt : qr_dt->cmd);
    if (nparams) {
        PRINT_STR_ARRAY(stream,
            qr_dt->prep_stmt->param_val,
            nparams, "PARAMETERS: ");
    }
}

static PGconn *
open_noblock_conn(char *conn_info)
{
    PGconn *conn = inl_open_noblock_conn_start(conn_info);
    check(conn, ERR_MEM, WLPQ);
    int ret = inl_open_noblock_conn_poll(conn, PQconnectPoll);
    check(ret, ERR_FAIL, WLPQ, "opening a non-blocking connection");
    return conn;
error:
    if (conn)
        PQfinish(conn);
    return NULL;
}

static inline int
inl_try_fix_noblock_conn(PGconn *conn, int oldfd, connpoll_func_t *connpollf, char *db_url)
{
    /*  If the connection still exists, try calling PQsocket() again. */
    if (conn) {
        int newfd = PQsocket(conn);
        /*  If a nonidentical fd was returned, return and try that. */
        if (oldfd != newfd)
            return newfd;
    } else
        conn = open_noblock_conn(db_url);

    /*  Otherwise, try to reset the corresponding connection. If that fails,
        or conn[i] was a NULL pointer, try to open a new connection at its address. */
    if (PQresetStart(conn))
        if (inl_open_noblock_conn_poll(conn, connpollf))
            return PQsocket(conn);
        else
            conn = open_noblock_conn(db_url);
    else
        conn = open_noblock_conn(db_url);
    if (!conn)
        return -1; /* Give up on this connection. */
    else
        return PQsocket(conn); /* Return the new file descriptor. */
}

static inline void
inl_flush_noblock_conn(PGconn *conn,
    wlpq_notify_handler_ft notify_cb, void *notify_cb_arg)
{
    struct pollfd pfds = PFDS_INIT(PQsocket(conn), 0 ^ POLLIN ^ POLLOUT);
    int do_flush = 1;
    do {
        int ret = poll(&pfds, 1, 10);
        while (ret == -1) {
            log_err(ERR_FAIL, WLPQ, "polling");
            ret = poll(&pfds, 1, 10);
        }
        if (ret) {
            ret = pfds.revents;
            if (ret & POLLIN) {
                ret = PQconsumeInput(conn);
                if (ret) {
                    PGnotify *notify = PQnotifies(conn);
                    if (notify)
                        if (notify_cb)
                            notify_cb(notify, notify_cb_arg);
                } else
                    log_err(ERR_FAIL, WLPQ, "reading from connection");

            }
            do_flush = PQflush(conn);
        }
    } while (do_flush);
}

static int
query_concurrent(PGconn *conn, const char *query_or_stmt, char **param_val,
    const int *param_len, uint8_t nparams, wlpq_res_handler_ft *res_callback,
    void *res_cb_arg, wlpq_notify_handler_ft *notify_cb, void *notify_cb_arg,
    uint8_t blocking)
{
    PGresult *res = NULL;
    int ret;
    if (blocking)
        check(PQsetnonblocking(conn, 0) != -1, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    if (!nparams)   /*  Interpret query_or_stmt as a ready query. */
        ret = PQsendQuery(conn, query_or_stmt);
    else            /*  Interpret query_or_stmt as a prepared statement name. */
        ret = PQsendQueryPrepared(conn, query_or_stmt, nparams,
            (const char * const *) param_val, param_len, NULL, 0);

    check(ret, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    /*  Flush queued data to server. If set to blocking, returns when complete. */
    ret = PQflush(conn);
    check(ret != -1, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    if (blocking) {
        /* Block & wait for the query to return a result. */
        do {
            res = PQgetResult(conn);
            ExecStatusType desired = res_callback ? PGRES_TUPLES_OK : PGRES_COMMAND_OK;
            check(PQresultStatus(res) == desired, ERR_EXTERN, "libpq", PQerrorMessage(conn));
            if (res_callback) /*  Pass the result set to a callback if one was provided. */
                res_callback(res, res_cb_arg);

            /*  According to libpq documentation, PQgetResult() should
            always be called repeatedly until it returns NULL, while
            taking care to PQclear each non-NULL result. */
            PQclear(res);
            res = PQgetResult(conn);
        } while (res);
        check(PQsetnonblocking(conn, 1) != -1, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    } else if (ret)
        /*  In nonblocking mode PQflush() returns 1 if it was not able to send all queued output.
        According to libpq documentation, we should wait for the connection socket to become
        READ | WRITE ready, PQconsumeInput() on a READ (checking for NOTIFY messages), and
        call PQflush() again, repeating this until flush returns 0.
            The function called below implements this procedure. */
        inl_flush_noblock_conn(conn, notify_cb, notify_cb_arg);
    return 1;
error:
    /*  As above, call PQclear() & PQgetResult() until the latter returns NULL. */
    while (res) {
        PQclear(res);
        res = PQgetResult(conn);
    }
    return 0;
}

static void
queue_enqueue_item(struct queue_elem *item, struct queue_elem **head,
    struct queue_elem **tail, volatile atomic_flag *lock, volatile atomic_bool *empty)
{
    struct timespec timer = TIMESPEC_INIT_S_MS(0, 5);
    while (atomic_flag_test_and_set(lock))
        nanosleep(&timer, NULL);
    if (*head != NULL)
        LL_APPEND_ELEM(*head, *tail, item);
    else
        LL_PREPEND(*head, item);
    atomic_store_explicit(empty, false, memory_order_release);
    atomic_flag_clear(lock);
    *tail = item;
}

static struct queue_elem *
queue_dequeue_item(struct queue_elem **head, volatile atomic_flag *lock,
    volatile atomic_bool *empty, volatile atomic_bool *thrd_continue)
{
    struct timespec timer = TIMESPEC_INIT_S_MS(0, 10);
    do {
        while (atomic_flag_test_and_set(lock)) {
            nanosleep(&timer, NULL);
            while (atomic_load_explicit(empty, memory_order_acquire)) {
                /*  Inner loop, continue until either
                    a)  the queue is non-empty, or
                    b)  do_continue evaluates to false AND the queue is empty.
                    If the latter, return NULL. */
                if (!atomic_load_explicit(thrd_continue, memory_order_relaxed))
                    return NULL;
                nanosleep(&timer, NULL);
            }
        }
        struct queue_elem *item = *head;
        if (item) {
            /*  Dequeue head element. */
            LL_DELETE(*head, item);
            /*  Set status to empty if head now points to NULL. */
            if (!(*head))
                atomic_store_explicit(empty, true, memory_order_release);
            /*  Release lock if not instructed otherwise. */
            if (!(item->data->lock_until_complete))
                atomic_flag_clear(lock);
            return item;
        } else {
            atomic_flag_clear(lock);
            if (!atomic_load_explicit(thrd_continue, memory_order_relaxed))
                return NULL;
        }
        nanosleep(&timer, NULL);
    } while (1);
}

static void *
send_poll_loop(void *arg)
{
    struct query_thread_ctx *thrd_ctx           = (struct query_thread_ctx *)arg;
    wlpq_conn_ctx_st *conn_ctx                  = thrd_ctx->conn_ctx;
    volatile atomic_bool *qqueue_empty          = &conn_ctx->qqueue_empty;
    volatile atomic_bool *thrd_continue         = &conn_ctx->thread_continue;
    volatile atomic_flag *qqueue_lock           = &conn_ctx->qqueue_lock;
    volatile wlpq_thread_state_et *thrd_state   = &conn_ctx->thread_state[thrd_ctx->nthread];
    wlpq_query_data_st **pgconn_qr_dt           = thrd_ctx->pgconn_qr_dt;
    wlpq_query_data_st **qr_dt                  = thrd_ctx->pgconn_qr_dt;
    PGconn **pgconn                             = thrd_ctx->pgconn;
    struct pollfd *pgconn_sockfds               = thrd_ctx->pgconn_sockfds;
    volatile uint8_t *pgconn_iostate            = thrd_ctx->pgconn_iostate;
    unsigned nconn                              = conn_ctx->thread_nconn;
    wlpq_notify_handler_ft *notify_cb           = conn_ctx->notify_cb;
    void *notify_cb_arg                         = conn_ctx->notify_cb_arg;
    unsigned topoll = 0, conn_err                   = 0;

    /*  Poll connections requested in the master thread, checking they are ready. */
    for (unsigned i = 0; i < nconn; ++i)
        if (!inl_open_noblock_conn_poll(pgconn[i], PQconnectPoll)) {
            log_err(ERR_FAIL_N, WLPQ, "opening connection", i);
            ++conn_err;
        }
    /*  If all connections failed to open, exit. */
    if (conn_err == nconn) {
        log_err(ERR_FAIL, WLPQ, "opening connections: all failed, closing");
        goto EXIT;
    }

    /*  Init connection socket file descriptors. */
    for (size_t i = 0; i < nconn; ++i)
        pgconn_sockfds[i] = PFDS_INIT(PQsocket(pgconn[i]), 0);

    /*  The total error count will be the return value on a join. */
    unsigned *err_total = calloc(1, sizeof(unsigned));
    if (!err_total) {
        log_err(ERR_MEM, WLPQ);
        goto EXIT;
    }

    /*  Set an interval for waiting on
        1) a busy connection, as per PQisbusy(),
        2) an ENOMEM error from poll(). */
    struct timespec timer_enomem = TIMESPEC_INIT_S_MS(0, 500);
    //struct timespec timer_conn = TIMESPEC_INIT_S_MS(0, 20);
    bool empty = atomic_load_explicit(qqueue_empty, memory_order_relaxed);
    /*  Begin main loop. */
    while (atomic_load_explicit(thrd_continue, memory_order_acquire) || !empty || topoll) {
        *thrd_state = (topoll || !empty) ? BUSY : IDLE;
        unsigned err_query = 0, err_poll = 0;
        int ret = poll(pgconn_sockfds, nconn, WLPQ_POLL_TIMEOUT_MS);
        while (ret == -1) {
            ++err_poll;
            log_err(ERR_FAIL, WLPQ, "polling pending connections");
            if (errno == ENOMEM)
                nanosleep(&timer_enomem, NULL);
            else if (errno != EINTR)
                goto EXIT;
            ret = poll(pgconn_sockfds, nconn, WLPQ_POLL_TIMEOUT_MS);
        }
        topoll = 0;
        size_t i = 0;
        int j = ret; /*  j == the number of poll'd events */
        for (i = 0; i < nconn; ++i) {
            ret = pgconn_sockfds[i].revents;
            if (ret & POLLIN) {
                --j;
                ret = PQconsumeInput(pgconn[i]);
                if (!ret) {
                    log_err(ERR_EXTERN, "libpq", PQerrorMessage(pgconn[i]));
                    goto EXIT;
                }
                PGnotify *notify = PQnotifies(pgconn[i]);
                if (notify) {
                    if (notify_cb)
                        notify_cb(notify, notify_cb_arg);
                    PQfreemem(notify);
                }
                wlpq_res_handler_ft *callback = qr_dt[i] ? qr_dt[i]->res_callback : 0;
                PGresult *res = PQgetResult(pgconn[i]);
                while (res) {
                    if (PQresultStatus(res)
                            != (callback ? PGRES_TUPLES_OK : PGRES_COMMAND_OK)) {
                        ++err_query;
                        log_err(ERR_EXTERN, "libpq", PQresStatus(PQresultStatus(res)));
                    } else if (callback) {
                    /*  Pass the result set to a callback if one was provided. */
                        callback(res, qr_dt[i]->cb_arg);
                    }
                    PQclear(res);
                    res = PQgetResult(pgconn[i]);
                }
                if (err_query) {
                    log_err(ERR_FAIL_N, WLPQ,
                        "sending query to database on conn", (int) i);
                    if (qr_dt[i])
                        inl_print_query_data(qr_dt[i], stderr);
                }
                if (qr_dt[i]) {
                    wlpq_query_free(qr_dt[i]);
                    qr_dt[i] = NULL;
                }
                pgconn_iostate[i] = PGCONN_IOSTATE_IDLE;
                pgconn_sockfds[i].events = 0;
            } else if (ret) {
                --j;
                ++err_poll;
                pgconn_iostate[i] = PGCONN_IOSTATE_ERROR;
                if (ret & POLLNVAL) {
                    /*  Invalid file descriptor. Try a reset/renew. */
                    log_err(ERR_FAIL_N, WLPQ, "invalid file descriptor on conn", (int) i);
                    log_info("[%s]: Will try to reset/restart conn %d", WLPQ, (int) i);
                    ret = inl_try_fix_noblock_conn(pgconn[i], pgconn_sockfds[i].fd,
                                PQresetPoll, conn_ctx->db_url);
                    if (ret == -1) { /* Didn't work out. */
                        log_err(ERR_FAIL_N, WLPQ, "resetting conn", (int) i);
                        pgconn_sockfds[i].events = 0;
                    } else
                        pgconn_iostate[i] = PGCONN_IOSTATE_IDLE;
                    /*  Save a new file descriptor. On error,
                        ret (from inl_try_fix_noblock_conn()) is negative
                        and thus ignored in poll(). */
                    pgconn_sockfds[i].fd = ret;
                    if (qr_dt[i]) {
                        log_err(ERR_FAIL_N, WLPQ, "sending query to database on conn", (int) i);
                        inl_print_query_data(qr_dt[i], stderr);
                        wlpq_query_free(qr_dt[i]);
                        qr_dt[i] = NULL;
                    }
                }
            } else if (pgconn_iostate[i] == PGCONN_IOSTATE_WAIT) {
                pgconn_sockfds[i].events = 0 ^ POLLIN;
                ++topoll;
            }
            if (pgconn_iostate[i] == PGCONN_IOSTATE_IDLE && !empty) {

                struct queue_elem *item = queue_dequeue_item(&conn_ctx->qqueue_head,
                                            qqueue_lock, qqueue_empty, thrd_continue);

                /*  Break loop when thrd_continue was set in qqueue_dequeue(). */
                if (!item)
                    goto EXIT;

                wlpq_query_data_st *data = item->data;
                free(item);
                if (data->nparams) {
                    struct wlpq_prep_stmt *prep_stmt = data->prep_stmt;
                    ret = query_concurrent(pgconn[i], prep_stmt->stmt,
                            prep_stmt->param_val, prep_stmt->param_len,
                            data->nparams, data->res_callback, data->cb_arg,
                            conn_ctx->notify_cb, conn_ctx->notify_cb_arg,
                            data->lock_until_complete);
                } else {
                    ret = query_concurrent(pgconn[i], data->cmd, 0, 0, 0,
                            data->res_callback, data->cb_arg, conn_ctx->notify_cb,
                            conn_ctx->notify_cb_arg, data->lock_until_complete);
                            printf("sent %s\n", data->cmd);
                }
                if (data->lock_until_complete)
                    atomic_flag_clear_explicit(qqueue_lock, memory_order_release);
                if (!ret) {
                    /*  An error occured trying to send the query. */
                    log_err(ERR_FAIL, WLPQ, "sending below query to database:");
                    inl_print_query_data(data, stderr);
                    wlpq_query_free(data);
                    pgconn_iostate[i] = PGCONN_IOSTATE_ERROR;
                    goto EXIT;
                }
                if (data->lock_until_complete) {
                    wlpq_query_free(data);
                    data = NULL;
                    pgconn_iostate[i] = PGCONN_IOSTATE_IDLE;
                    pgconn_sockfds[i].events = 0;
                } else {
                    pgconn_qr_dt[i] = data;
                    pgconn_iostate[i] = PGCONN_IOSTATE_WAIT;
                    pgconn_sockfds[i].events = 0 ^ POLLIN;
                    ++topoll;
                }
            }
            empty = atomic_load_explicit(qqueue_empty, memory_order_relaxed);
        }
        *err_total += err_query + err_poll;
    }
EXIT:
    printf("thread exits\n");
    *thrd_state = *err_total ? FAIL : SUCC;
    inl_free_query_thread_ctx(thrd_ctx);
    return err_total;
}

static struct query_thread_ctx *
init_query_thread_ctx(wlpq_conn_ctx_st *conn_ctx, uint8_t nthread)
{
    struct query_thread_ctx *thrd_ctx = malloc(sizeof(struct query_thread_ctx));
    check(thrd_ctx, ERR_MEM, WLPQ);
    thrd_ctx->nthread = nthread;
    thrd_ctx->conn_ctx = conn_ctx;
    unsigned nconn = conn_ctx->thread_nconn;
    thrd_ctx->pgconn = calloc(nconn, sizeof(PGconn *));
    check(thrd_ctx->pgconn, ERR_MEM, WLPQ);
    thrd_ctx->pgconn_qr_dt = calloc(nconn, sizeof(wlpq_query_data_st **));
    check(thrd_ctx->pgconn_qr_dt, ERR_MEM, WLPQ);
    for (size_t i = 0; i < nconn; i++) {
        thrd_ctx->pgconn[i] = inl_open_noblock_conn_start(conn_ctx->db_url);
        check(thrd_ctx->pgconn[i], ERR_FAIL, WLPQ,
            "sending request for a non-blocking connection");
        thrd_ctx->pgconn_iostate[i] = PGCONN_IOSTATE_IDLE;
    }
    return thrd_ctx;
error:
    return 0;
}

static void *
threads_launch_async_start(void *arg)
{
    if (!wlpq_threads_launch((wlpq_conn_ctx_st *)arg))
        log_err(ERR_FAIL, WLPQ, "launching threads");
    return 0;
}

/*  FUNCTION PROTOTYPE IMPLEMENTATIONS  */

void
wlpq_conn_ctx_free(wlpq_conn_ctx_st *conn_ctx)
{
    if (conn_ctx) {
        if (atomic_load_explicit(&conn_ctx->thread_continue, memory_order_relaxed))
            wlpq_threads_stop_and_join(conn_ctx);
        if (conn_ctx->db_url)
            free(conn_ctx->db_url);
        /*  The if-block below will only run in case of error;
            if all went well, job queue ought to be empty
            by the time the conn context will be freed. */
        if (!atomic_load(&conn_ctx->qqueue_empty)) {
            struct queue_elem *el, *tmp;
            struct queue_elem *head = conn_ctx->qqueue_head;
            LL_FOREACH_SAFE(head, el, tmp) {
                LL_DELETE(head, el);
                if (el->data)
                    wlpq_query_free(el->data);
                free(el);
            }
        }
        free(conn_ctx);
    }
}

wlpq_conn_ctx_st *
wlpq_conn_ctx_init(char *db_url)
{
    /* Allocate memory for connection context structure. */
    wlpq_conn_ctx_st *conn_ctx = calloc(1, sizeof(wlpq_conn_ctx_st));
    check(conn_ctx, ERR_MEM, WLPQ);

    /* Set up db connection information, either provided as a parameter
    or obtained from an environment variable. */
    char *conn_info;
    size_t len;
    char buffer[0x800];
    if (!db_url) {
        db_url = buffer;
        snprintf(db_url, 0x7FF, "%s?%s", getenv(WLPQ_DATABASE_URL_ENV), "sslmode=require");
    }
    len = strlen(db_url) + 1;
    conn_info = malloc(sizeof(char) * len);
    check(conn_info, ERR_MEM, WLPQ);
    memcpy(conn_info, db_url, len);
    conn_ctx->db_url = conn_info;
    conn_ctx->thread_nconn = MAX_NCONN_THRD;
    /* Set up queue lock and initialize head to NULL. */
    atomic_flag_clear_explicit(&conn_ctx->qqueue_lock, memory_order_relaxed);
    atomic_init(&conn_ctx->thread_continue, false);
    atomic_init(&conn_ctx->qqueue_empty, true);
    struct queue_elem *qqueue_head = NULL;
    conn_ctx->qqueue_head = qqueue_head;
    conn_ctx->qqueue_tail = qqueue_head;
    conn_ctx->notify_cb = NULL;
    conn_ctx->notify_cb_arg = NULL;
    return conn_ctx;
error:
    return 0;
}

void
wlpq_conn_ctx_notify_handler_set(wlpq_conn_ctx_st *ctx,
    wlpq_notify_handler_ft *notify_cb, void *notify_arg)
{
    if (ctx) {
        ctx->notify_cb = notify_cb;
        ctx->notify_cb_arg = notify_arg;
    }
}

void
wlpq_query_free(wlpq_query_data_st *data)
{
    if (data)
        inl_free_query_data(data);
    data = NULL;
}

wlpq_query_data_st *
wlpq_query_init(char *stmt_or_cmd, char **param_val,
    int *param_len, unsigned nparams, wlpq_res_handler_ft *callback,
    void *cb_arg, uint8_t lock_until_complete)
{
    wlpq_query_data_st *qr_data = calloc(1, sizeof(wlpq_query_data_st));
    check(qr_data, ERR_MEM, WLPQ);
    size_t stmt_or_cmd_len = strlen(stmt_or_cmd);
    if (nparams) {
        struct wlpq_prep_stmt *prep_stmt = calloc(1, sizeof(struct wlpq_prep_stmt));
        check(prep_stmt, ERR_MEM, WLPQ);
        prep_stmt->stmt = calloc(stmt_or_cmd_len + 1, sizeof(char));
        check(prep_stmt->stmt, ERR_MEM, WLPQ);
        memcpy(prep_stmt->stmt, stmt_or_cmd, stmt_or_cmd_len + 1);
        for (uint8_t i = 0; i < nparams; i++) {
            prep_stmt->param_val[i] = calloc(param_len[i] + 1, sizeof(char));
            check(prep_stmt->param_val[i], ERR_MEM, WLPQ);
            memcpy(prep_stmt->param_val[i], param_val[i], param_len[i]);
            prep_stmt->param_len[i] = param_len[i];
        }
    } else {
        qr_data->cmd = calloc(stmt_or_cmd_len + 1, sizeof(char));
        check(qr_data->cmd, ERR_MEM, WLPQ);
        memcpy(qr_data->cmd, stmt_or_cmd, stmt_or_cmd_len + 1);
    }
    qr_data->nparams = nparams;
    qr_data->res_callback = callback ? callback : NULL;
    qr_data->cb_arg = callback && cb_arg ? cb_arg : NULL;
    qr_data->lock_until_complete = lock_until_complete;
    return qr_data;
error:
    return NULL;
}

uint8_t
wlpq_query_queue_empty(wlpq_conn_ctx_st *conn_ctx)
{
    return atomic_load(&conn_ctx->qqueue_empty);
}

int
wlpq_query_queue_enqueue(wlpq_conn_ctx_st *conn_ctx, wlpq_query_data_st *qr_dt)
{
    struct queue_elem *el = malloc(sizeof(struct queue_elem));
    check(el, ERR_MEM, WLPQ);
    el->data = qr_dt;
    el->conn = NULL;
    el->conn_id = 0;
    queue_enqueue_item(el, &conn_ctx->qqueue_head, &conn_ctx->qqueue_tail,
        &conn_ctx->qqueue_lock, &conn_ctx->qqueue_empty);
    return 1;
error:
    return 0;
}

int
wlpq_query_run_blocking(wlpq_conn_ctx_st *ctx, char *stmt_or_cmd,
    char **param_val, int *param_len, uint8_t nparams,
    wlpq_res_handler_ft *callback, void *cb_arg)
{
    PGconn *conn = open_noblock_conn(ctx->db_url);
    check(conn, ERR_FAIL, WLPQ, "obtaining a connection");
    PGresult *res;
    if (nparams) /* Prepared statement. */
        res = PQexecParams(conn,
                stmt_or_cmd, nparams, NULL,
                (const char * const *)param_val,
                param_len, NULL, 0);
    else
        res = PQexec(conn, stmt_or_cmd);

    if (PQresultStatus(res) != (callback ? PGRES_TUPLES_OK : PGRES_COMMAND_OK)) {
        log_err(ERR_EXTERN, "libpq", PQerrorMessage(conn));
        return 0;
    } else if (callback) /*  Pass the result set to a callback if one was provided. */
        callback(res, cb_arg);

    /*  According to libpq documentation, PQgetResult should
        always be called repeatedly until it returns NULL while
        taking care to PQclear() each non-NULL result. */
    while (res) {
        PQclear(res);
        res = PQgetResult(conn);
    }
    PQfinish(conn);
    return 1;
error:
    return -1;
}

int
wlpq_threads_launch(wlpq_conn_ctx_st *conn_ctx)
{
    check(conn_ctx, ERR_NALLOW, WLPQ, "NULL conn_ctx argument");

    /*  Set thread continue flag to true. */
    atomic_store(&conn_ctx->thread_continue, true);
    /*  Initialize thread attributes. */
    pthread_attr_t attr;
    int ret = inl_init_thread_attr(&attr, PTHREAD_CREATE_JOINABLE);
    check(ret, ERR_FAIL, WLPQ, "initializing thread attribute object");

    /*  Allocate memory for and initialize thread context structs,
        start the threads with the specified attributes and store the
        unique thread identifiers in the connection conn_ctx structure. */
    for (uint8_t i = 0; i < MAX_NTHRD; i++) {
        struct query_thread_ctx *thrd_ctx;
        thrd_ctx = init_query_thread_ctx(conn_ctx, i);
        check(thrd_ctx, ERR_FAIL, WLPQ, "creating thread context data");
        ret = pthread_create(&conn_ctx->thread_pt_id[i],
                &attr, send_poll_loop, thrd_ctx);
    	check(ret == 0, ERR_FAIL, WLPQ, "creating thread");
        conn_ctx->thread_state[i] = BUSY;
    }
    pthread_attr_destroy(&attr);
    return 1;
error:
    atomic_store(&conn_ctx->thread_continue, false);
    wlpq_threads_stop_and_join(conn_ctx);
    pthread_attr_destroy(&attr);
    return 0;
}

int
wlpq_threads_launch_async(wlpq_conn_ctx_st *conn_ctx)
{
    pthread_attr_t attr;
	int ret = inl_init_thread_attr(&attr, PTHREAD_CREATE_JOINABLE);
    check(ret, ERR_FAIL, WLPQ, "initializing thread attribute object");
    pthread_t thrd_id = 0;
    ret = pthread_create(&thrd_id, &attr, threads_launch_async_start, conn_ctx);
    pthread_attr_destroy(&attr);
    check(ret == 0, ERR_FAIL, WLPQ, "creating thread");
    return 1;
error:
    return 0;
}

void
wlpq_threads_nconn_set(wlpq_conn_ctx_st *conn_ctx, unsigned nconn)
{
    if (nconn <= (unsigned) (MAX_NCONN_THRD))
        conn_ctx->thread_nconn = nconn;
}

int
wlpq_threads_stop_and_join(wlpq_conn_ctx_st *conn_ctx)
{
    check(conn_ctx, ERR_NALLOW, WLPQ, "NULL conn_ctx argument");
    atomic_store(&conn_ctx->thread_continue, false);
    int ret = 0, nerrors = 0;
    for (uint8_t i = 0; i < WLPQ_MAX_NCONNTHREADS; ++i) {
        void *retval;
        ret = pthread_join(conn_ctx->thread_pt_id[i], &retval);
        if (ret) {
            ++nerrors;
            log_err(ERR_FAIL_N, WLPQ, "joining thread: pthread_join() returned", ret);
        } else if (conn_ctx->thread_state[i] == FAIL) {
            int thrd_errs = (int) *((uint8_t *)retval);
            log_err(ERR_FAIL_N, WLPQ, "joining thread: FAILURE reported from thread", (int) i);
            log_err(ERR_FAIL_N, WLPQ, "joining thread: total number of errors was", thrd_errs);
            nerrors += thrd_errs;
        }
        free(retval);
        conn_ctx->thread_pt_id[i] = 0;
    }
    return nerrors; /* Return the amount of errors: 0 on success. */
error:
    return -1; /* Context argument was NULL. */
}

void
wlpq_threads_wait_until(wlpq_conn_ctx_st *ctx, wlpq_thread_state_et state)
{
    if (ctx && state != NONE) {
        struct timespec timer = TIMESPEC_INIT_S_MS(0, 100);
    	for (size_t i = 0; i < MAX_NTHRD; ++i)
            while (ctx->thread_state[i] != state)
    			nanosleep(&timer, NULL);
    }
}
