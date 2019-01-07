#include "dbg.h"
#include "psql-db_threaded.h"

/* Database operation functions */

static PGconn *
_open_nonblocking_conn(char *conn_info)
{
    PGconn *conn;
    int ret;
    conn = PQconnectStart(conn_info);
    check(conn, ERR_MEM, PSQLDB);
    check(PQstatus(conn) != CONNECTION_BAD, ERR_FAIL, PSQLDB, "establishing connection")
    struct pollfd pfds;
    ret = PGRES_POLLING_WRITING;
    do {
        pfds.events = ret == PGRES_POLLING_READING ? (0 ^ POLLIN) : (0 ^ POLLOUT);
        pfds.fd = PQsocket(conn);
        poll(&pfds, 1, 5000);
        check(ret != -1, ERR_FAIL, PSQLDB, "polling socket");
        check(ret, ERR_FAIL, PSQLDB, "poll timeout");
        ret = PQconnectPoll(conn);
        check(ret != PGRES_POLLING_FAILED, ERR_EXTERN, "libpq", PQerrorMessage(conn));
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

int
psqldb_select_concurrent(char *db_url, char* query, psqldb_res_callback res_function)
{
    PGconn *conn = NULL;
    PGresult *res = NULL;
    int ret;
    if (!db_url) db_url = getenv("DATABASE_URL");
    conn = _open_nonblocking_conn(db_url);
    check(conn, ERR_FAIL, PSQLDB, "sending request for nonblocking connection.");
    ret = PQsendQuery(conn, query);
    check(ret, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    struct pollfd pfds;
    pfds.events =  0 ^ POLLIN;
    uint8_t i = 3;
    do {
        pfds.fd = PQsocket(conn);
        ret = poll(&pfds, 1, 5000);
        i--;
        check(ret != -1, ERR_FAIL, PSQLDB, "to poll socket");
        if (!ret) {
            log_warn("poll timeout occured, %d attempts left", i);
        } else {
            ret = pfds.revents;
            ret &= POLLIN;
            check(ret, ERR_FAIL, PSQLDB, "reading; data indicated by poll was not found");
            ret = PQconsumeInput(conn);
            check(ret, ERR_EXTERN, "libpq", PQerrorMessage(conn));
            ret = PQisBusy(conn);
            while (ret) {
                sleep(1);
                ret = PQisBusy(conn);
            }
            break;
        }
    } while (i);
    check(i, ERR_FAIL, PSQLDB, "too many timeouts polling");
    res = PQgetResult(conn);
    check(PQresultStatus(res) == PGRES_TUPLES_OK, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    if (conn) PQfinish(conn);
    res_function(res);
    return 1;
error:
    if (res) PQclear(res);
    if (conn) PQfinish(conn);
    return 0;
}

int
psqldb_insert_concurrent(char *db_url, char* query)
{
    PGconn *conn;
    PGnotify *notify;
    PGresult *res = NULL;
    int ret;
    if (!db_url) db_url = getenv("DATABASE_URL");
    conn = _open_nonblocking_conn(db_url);
    check(conn, ERR_FAIL, PSQLDB, "sending request for nonblocking connection");
    res = PQexec(conn, "LISTEN Notifier;");
    check(PQresultStatus(res) == PGRES_COMMAND_OK, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    PQclear(res);
    res = NULL;
    ret = PQsendQuery(conn, query);
    check(ret, ERR_EXTERN, "libpq", PQerrorMessage(conn));
    int sock;
    fd_set input_mask;
    do {
        sock = PQsocket(conn);
        FD_ZERO(&input_mask);
        FD_SET(sock, &input_mask);
        check(sock >= 0, ERR_FAIL, PSQLDB, "fd_setting socket");
        ret = select(sock + 1, &input_mask, NULL, NULL, NULL);
        check(ret >= 0, ERR_FAIL, PSQLDB, "selecting socket");
        PQconsumeInput(conn);
        notify = PQnotifies(conn);
    } while (!notify);
    PQfreemem(notify);
    if (conn) PQfinish(conn);
    return ret;
error:
    if (res) PQclear(res);
    if (notify) PQfreemem(notify);
    if (conn) PQfinish(conn);
    return 0;
}

/* Thread context management */

static void *
_conn_thread_start(void* data)
{
    struct conn_thread_data *thread_data = (struct conn_thread_data *) data;
    int ret;
    printf("QUERY: %s\n", thread_data->data);
    if (thread_data->callback) {
        ret = psqldb_select_concurrent(*thread_data->db_conn_info, thread_data->data, thread_data->callback);
    } else {
        ret = psqldb_insert_concurrent(*thread_data->db_conn_info, thread_data->data);
    }
    if (!ret) {
        log_err(ERR_FAIL_N, PSQLDB, "querying db in thread", thread_data->thread_stack_el->thread_num);
    }
    struct conn_thread_stack_el *free_thread_stack = thread_data->thread_stack_el->next;
    free_thread_stack->next = NULL;
    STACK_PUSH(free_thread_stack, thread_data->thread_stack_el);
    thread_data->retval = ret;
    return &thread_data->retval;
}

void
psqldb_concurrency_manager(struct psqldb_conn_thread_ctx *ctx)
{
    if (!ctx) return;

    if (ctx->new_thread_data != NULL) {
        struct timespec timer;
        timer.tv_sec = 0;
        timer.tv_nsec = 300000000;
        while (STACK_EMPTY(ctx->free_thread_stack)) {
            printf("NO FREE THREADS, WAIT\n");
            nanosleep(&timer, NULL);
        }
        struct conn_thread_stack_el *tmp;
        int count;
        STACK_COUNT(ctx->free_thread_stack, tmp, count);
        printf("FREE THREADS: %d\n", count);
        struct conn_thread_stack_el *el;
        STACK_POP(ctx->free_thread_stack, el);
        uint8_t thread_num = el->thread_num;
        /* Set the just popped element's next pointer to the address of the stack head */
        el->next = ctx->free_thread_stack;
        /* And copy the address of the pointer to the data struct for the new thread */
        ctx->new_thread_data->thread_stack_el = el;
        /* This way stack element structs can be recycled;
        thread pushes its element to the stack just before returning from start routine */
        int ret = pthread_create(&ctx->thread_ids[thread_num], &ctx->default_attr,
             _conn_thread_start, (void*) ctx->new_thread_data);
        if (ret != 0) {
            log_err(ERR_FAIL_N, PSQLDB, "creating thread", thread_num);
        }
    } else {
        /* NULL new_thread_data is taken as a signal to wait for each thread to terminate. */
        /* Block until each thread has terminated */
        for (uint8_t i = 0; i < PSQLDB_MAX_NCONN; i++) {
            pthread_t thread_id = ctx->thread_ids[i];
            if (thread_id) {
                void *ret;
                pthread_join(thread_id, &ret);
                if (!(*(uint8_t *)ret)) {
                    log_err(ERR_FAIL_N, PSQLDB, "joining: exit_failure from thread", i);
                }
            }
        }
        ctx->ready = 1;
    }
}

struct psqldb_conn_thread_ctx *
psqldb_init_conn_thread_ctx()
{
    struct psqldb_conn_thread_ctx *conn_ctx = calloc(1, sizeof(struct psqldb_conn_thread_ctx));
    check(conn_ctx, ERR_MEM, PSQLDB);
    char buffer[0x800];
    sprintf(buffer, "%s?%s", getenv("DATABASE_URL"), "sslmode=require");
    size_t len = strlen(buffer) + 1;
    char *conn_info = malloc(sizeof(char) * len);
    check(conn_info, ERR_MEM, PSQLDB);
    memcpy(conn_info, buffer, len);
    conn_ctx->db_conn_info = conn_info;
    int res;
    res = pthread_attr_init(&conn_ctx->default_attr);
    check(res == 0, ERR_FAIL, PSQLDB, "initializing pthread attributes");
    res = pthread_attr_setstacksize(&conn_ctx->default_attr, MAX_STACK_SIZE);
    check(res == 0, ERR_FAIL, PSQLDB, "setting pthread stack size attribute");
    struct conn_thread_stack_el *free_thread_stack = NULL;
    for (uint8_t i = 0; i < PSQLDB_MAX_NCONN; i++) {
        struct conn_thread_stack_el *el = calloc(1, sizeof(struct conn_thread_stack_el));
        check(el, ERR_MEM, PSQLDB);
        el->thread_num = i;
        STACK_PUSH(free_thread_stack, el);
    }
    conn_ctx->free_thread_stack = free_thread_stack;
    return conn_ctx;
error:
    return NULL;
}

int
psqldb_set_new_thread_data(struct psqldb_conn_thread_ctx *ctx, char* data, psqldb_res_callback callback)
{
    check(ctx, ERR_NALLOW, PSQLDB, "either ctx or data was NULL");
    struct conn_thread_data *new_thread_data = calloc(1, sizeof(struct conn_thread_data));
    check(new_thread_data, ERR_MEM, PSQLDB);
    new_thread_data->data = data;
    new_thread_data->callback = callback;
    new_thread_data->db_conn_info = &ctx->db_conn_info;
    ctx->new_thread_data = new_thread_data;
    return 1;
error:
    return 0;
}

void
psqldb_free_conn_thread_ctx(struct psqldb_conn_thread_ctx *conn_ctx)
{
    int res;
    if (conn_ctx) {
        if (conn_ctx->new_thread_data) {
            if (conn_ctx->new_thread_data->data) {
                free(conn_ctx->new_thread_data->data);
            }
            free(conn_ctx->new_thread_data);
        }
        while (!STACK_EMPTY(conn_ctx->free_thread_stack)) {
            struct conn_thread_stack_el *el;
            STACK_POP(conn_ctx->free_thread_stack, el);
            free(el);
        }
        if (conn_ctx->db_conn_info) {
            free(conn_ctx->db_conn_info);
        }
        res = pthread_attr_destroy(&conn_ctx->default_attr);
        if (res != 0) {
            log_err(ERR_FAIL, PSQLDB, "problem in pthread_attr_destroy()");
        }
        free(conn_ctx);
    }
}
