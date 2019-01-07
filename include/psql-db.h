#ifndef _psql_db_
#define _psql_db_

#ifndef _XOPEN_SOURCE
    #define _XOPEN_SOURCE 700
#endif

/*
**  INCLUDES
*/

#include <stdint.h>
#include <libpq-fe.h>

/*
**  MACRO DEFINITIONS
*/

/*  MACRO CONSTANTS */

/*  Error message provider name. */
#define PSQLDB "PSQL-DB"

/*  Define some per-compile constants */
#ifndef USE_STACK_SIZE
    #define PSQLDB_STACK_SIZE 0x200000
#endif
#ifndef PSQLDB_MAX_NCONN
    #define PSQLDB_MAX_NCONN 19
#endif
#ifndef PSQLDB_NTHREADS
    #define PSQLDB_NTHREADS 1
#endif
#ifndef PSQLDB_NCONN_PER_THREAD
    #define PSQLDB_NCONN_PER_THREAD (int) (PSQLDB_MAX_NCONN / PSQLDB_NTHREADS)
#endif
#ifndef PSQLDB_CONN_TIMEOUT
    #define PSQLDB_CONN_TIMEOUT 10
#endif
#ifndef PSQLDB_DATABASE_URL_ENV
    #define PSQLDB_DATABASE_URL_ENV "DATABASE_URL"
#endif
#ifndef PSQLDB_POLL_TIMEOUT_MS
    #define PSQLDB_POLL_TIMEOUT_MS 5000
#endif
#ifndef PSQLDB_POLL_ATTEMPTS
    #define PSQLDB_POLL_ATTEMPTS 12
#endif
#ifndef PSQLDB_MAX_NPARAMS
    #define PSQLDB_MAX_NPARAMS 8
#endif

#define PSQLDB_THRD_IDLE 0
#define PSQLDB_THRD_BUSY 1

/*
**  TYPE & STRUCTURE DEFINITIONS/DECLARATIONS
*/

/*  A callback function type for result sets from database queries. */
typedef void psqldb_res_handler_ft(PGresult *res, void *arg);

/*  Pending database queries are enqueued in a singly linked list (utlist.h) with a separate
    tail-pointer housed in the connection context structure. This enables O(1) append, crucial
    to an efficient queue-implementation, without the need for a doubly-linked list's in this
    context useless backpointers.
*/

/*  Database query data opaque structure declaration & typedef, embedded in queue elements. */

typedef struct psqldb_query_data psqldb_query_data_st;

/*  Main context opaque structure delaration. */
typedef struct psqldb_conn_ctx psqldb_conn_ctx_st;

/*
**  FUNCTION PROTOTYPES
*/

psqldb_conn_ctx_st *
psqldb_init_conn_ctx(char *db_url);

void
psqldb_free_conn_ctx(psqldb_conn_ctx_st *ctx);

int
psqldb_launch_conn_threads(psqldb_conn_ctx_st *ctx);

void
psqldb_wait_on_threads_until_idle(psqldb_conn_ctx_st *ctx);

int
psqldb_stop_and_join_threads(psqldb_conn_ctx_st *ctx);

psqldb_query_data_st *
psqldb_init_query_data(char *stmt_or_cmd, char **param_values,
    int *param_lengths, uint8_t nparams, psqldb_res_handler_ft *callback,
    void *cb_arg, uint8_t lock_until_complete);

void
psqldb_free_query_data(psqldb_query_data_st *data);

int
psqldb_enqueue_query(psqldb_conn_ctx_st *ctx, psqldb_query_data_st *qr_dt);

int
psqldb_blocking_query(psqldb_conn_ctx_st *ctx, char *stmt_or_cmd,
    char **param_values, int *param_lengths, uint8_t nparams,
    psqldb_res_handler_ft *callback, void *cb_arg);

int
psqldb_queue_is_empty(psqldb_conn_ctx_st *ctx);

#endif /* _psql_db_ */
