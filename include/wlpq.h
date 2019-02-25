/*! @file           wlpq.h
    @version        0.2.0
    @brief          An interface to wlpq, a PostgreSQL C API (libpq) wrapper.
    @details        See [documentation](../doc/wlcsv_api.md).
    @copyright      (c) Joa Käis [github.com/jiikai] 2018-2019, [MIT](../LICENSE).
*/

#ifndef _wlpq_h_
#define _wlpq_h_

/*
**  INCLUDES
*/

#include <stddef.h>
#include <stdint.h>
#include <libpq-fe.h>

/*
**  MACRO CONSTANTS
*/

/*! Error message provider name. */
#define WLPQ "wlpq"

#define WLPQ_VERSION_MAJOR 0
#define WLPQ_VERSION_MINOR 2
#define WLPQ_VERSION_PATCH 0

/*! Defines an expression for calculating the total number of threads spawned by wlpq. */
#define WLPQ_NTOTAL_THREADS(nquery_threads, npoll_threads)\
    (nquery_threads + npoll_threads * nquery_threads)

/*! Define lower bounds for the number of simultaneous database connections, query threads,
    poller threads opened per query thread and a derived total thread count thereof. These are not
    redefinable by the user at compile-time.
*////@{
#define WLPQ_MIN_NCONN 1
#define WLPQ_MIN_NQUERY_THREADS 1
#define WLPQ_MIN_NPOLL_THREADS 1
#define WLPQ_MIN_NTOTAL_THREADS WLPQ_NTOTAL_THREADS(WLPQ_MIN_NQUERY_THREADS, WLPQ_MIN_NPOLL_THREADS)
///@}

/*! Define default higher bounds for the number of simultaneous database connections, query
    threads, poller threads opened per query thread and a derived total thread count thereof.
    These defaults are used if the user does not pass the relevant define at compile time, i.e.
    an option of the form -D[NAME]=[VALUE].

    The following rule is also enforced:

    Either @def WLPQ_MAX_NQUERY_THREADS or @def WLPQ_MAX_NPOLL_THREADS may not be greater than
    @def WLPQ_MAX_NCONN.
*////@{
#ifndef WLPQ_MAX_NCONN
    #define WLPQ_MAX_NCONN 20
#endif
#ifndef WLPQ_MAX_NQUERY_THREADS
    #define WLPQ_MAX_NQUERY_THREADS 1
#endif
#ifndef WLPQ_MAX_NPOLL_THREADS
    #define WLPQ_MAX_NPOLL_THREADS 1
#elif WLPQ_MAX_NPOLL_THREADS > WLPQ_MAX_NCONN
    #define
#endif
///@}
#define WLPQ_MAX_NTOTAL_THREADS WLPQ_NTOTAL_THREADS(WLPQ_MAX_NQUERY_THREADS, WLPQ_MAX_NPOLL_THREADS)
/*! Define the derived maximum number of connections per query and per poller thread. *////@{
#define WLPQ_MAX_NCONN_PER_QUERY_THREAD\
    (unsigned) (WLPQ_MAX_NCONN / WLPQ_MAX_NQUERY_THREADS)
#define WLPQ_MAX_NCONN_PER_POLL_THREAD\
    (unsigned) (WLPQ_MAX_NCONN_PER_QUERY_THREAD / WLPQ_MAX_NPOLL_THREADS)
///@}

/*! The stack size in bytes available to threads launched by functions of this interface.
    Change at compile-time by passing a -DWLPQ_STACK_SIZE=value to the compiler. */
#ifndef WLPQ_STACK_SIZE
    #define WLPQ_STACK_SIZE 0x200000
#endif
/*! Timeout in seconds when establishing a database connection.
    Change at compile-time by passing -DWLPQ_CONN_TIMEOUT=value to the compiler. */
#ifndef WLPQ_CONN_TIMEOUT
    #define WLPQ_CONN_TIMEOUT 10
#endif
/*! Name of the environment variable to look for if no db URL is provided explicitly at runtime.
    Change at compile-time by passing -DWLPQ_DATABASE_URL_ENV=value to the compiler. */
#ifndef WLPQ_DATABASE_URL_ENV
    #define WLPQ_DATABASE_URL_ENV "DATABASE_URL"
#endif
/*! Poll timeout in milliseconds.
    Change at compile-time by passing -DWLPQ_POLL_TIMEOUT_MS=value to the compiler. */
#ifndef WLPQ_POLL_TIMEOUT_MS
    #define WLPQ_POLL_TIMEOUT_MS 500
#endif
/*! Maximum number of parameters in a prepared statement.
    Change at compile-time by passing -WLPQ_MAX_NPARAMS=value to the compiler. */
#ifndef WLPQ_MAX_NPARAMS
    #define WLPQ_MAX_NPARAMS 8
#endif

/*
**  TYPEDEFS
*/

/*! An enum type of possible thread states. */
typedef enum wlpq_thread_state {
    NONE, IDLE, BUSY, SUCC, FAIL
} wlpq_thread_state_et;

/*! An opaque handle to the main context structure. */
typedef struct wlpq_conn_ctx wlpq_conn_ctx_st;

/*! An opaque handle to a data structure for a single query.

    Pending database queries are enqueued in a singly linked list (utlist.h) with a separate
    tail-pointer housed in the connection context structure. This enables O(1) append, crucial
    to an efficient queue-implementation, without the need for a doubly-linked list's in this
    context useless backpointers.
*/
typedef struct wlpq_query_data wlpq_query_data_st;

/*! A callback function type for handling result sets returned by queries. */
typedef void wlpq_res_handler_ft(PGresult *res, void *arg);

/*! A callback function type for handling NOTIFY messages sent by the server. */
typedef void wlpq_notify_handler_ft(PGnotify *notify, void *arg);

/*
**  FUNCTION PROTOTYPES
*/

/*! Deallocate a previously allocated connection context structure.

    This function will silently fail if @a ctx is a NULL pointer.

    @param ctx The connection context structure.
    @see wlpq_conn_ctx_init()
*/
void
wlpq_conn_ctx_free(wlpq_conn_ctx_st *ctx);

/*! Allocate and initialize a connection context structure.

    Will fail if the environment variable DATABASE_URL is empty AND no valid Postgres database URL
    is provided as a parameter.

    @param db_url A valid URL to a Postgres database. If null, depends on DATABASE_URL.
    @return A pointer to the alloc'd and initialized context struct on success, NULL on failure.
    @see wlpq_conn_ctx_free()
*/
wlpq_conn_ctx_st *
wlpq_conn_ctx_init(char *db_url);

/*! Set a callback for NOTIFY messages from the database server.

    The default behaviour is to ignore NOTIFYs. You may want to issue a LISTEN/UNLISTEN/NOTIFY
    command too, since no NOTIFYs will arrive unless explicitly requested. This function will
    silently fail if @a ctx is a NULL pointer.

    @param ctx              The connection context structure.
    @param notify_callback  The handler callback that will be called when a NOTIFY arrives. Can be
                            NULL, resetting the handling of NOTIFYs to the default ignore mode.
    @param notify_arg       Optional argument of user data to be passed to @notify_callback.

    @see wlpq_query_init(), wlpq_query_queue_enqueue(), wlpq_query_run_blocking()
*/
void
wlpq_conn_ctx_notify_handler_set(wlpq_conn_ctx_st *ctx,
    wlpq_notify_handler_ft *notify_callback, void *notify_arg);

/*! Deallocate a previously allocated connection context structure.

    This function will silently fail if @a ctx is a NULL pointer.

    @param data A pointer to a query data structure.
    @see wlpq_query_init(), wlpq_query_queue_empty(), wlpq_query_queue_enqueue()
*/
void
wlpq_query_free(wlpq_query_data_st *data);

/*! Allocate and initialize a structure holding SQL query data.

    @param stmt_or_cmd      Either the name of a prepared statement or a valid SQL query string.
    @param param_values,
           param_lengths,
           nparams          If using a prepared statement, these should point to a string array with
                            the desired parameters to be inserted into the prepared statement, an
                            int array with their lengths and the number of parameters. Else they
                            should all be NULL.
    @param callback         If the query may return data, this should be a pointer to a callback
                            function handling the processing of that data. Otherwise all results
                            apart from the query's success status will be discarded.
    @param cb_arg           If a callback was specified, this parameter can be used to pass a
                            pointer to any user-specified data to the callback.
    @param lock_until_done  If 1, blocks all processing of further queries until complete.

    @return A pointer to the alloc'd and initialized query data struct on success, NULL on error.
    @see wlpq_query_free(), wlpq_query_queue_enqueue()

*/
wlpq_query_data_st *
wlpq_query_init(char *stmt_or_cmd, char **param_values, int *param_lengths,
    unsigned nparams, wlpq_res_handler_ft *callback, void *cb_arg,
    uint8_t lock_until_done);

/*! Atomically check whether the query queue is currently empty.

    This function never fails.

    @param ctx  A pointer to the connection context structure.
    @return 1 if the queue is empty, 0 if not.
    @see wlpq_query_free(), wlpq_query_init(), wlpq_query_queue_enqueue()
*/
uint8_t
wlpq_query_queue_empty(wlpq_conn_ctx_st *ctx);

/*! Atomically enqueue a query data object created with wlpq_query_init().

    @param ctx      pointer to the connection context structure.
    @param qr_dt    a pointer to a query data object.

    @return 1 on success, 0 on error.
    @see wlpq_query_free(), wlpq_query_init(), wlpq_query_queue_empty()
*/
int
wlpq_query_queue_enqueue(wlpq_conn_ctx_st *ctx, wlpq_query_data_st *qr_dt);

/*! Run a query that will block the calling thread until complete.

    @param ctx              A pointer to the connection context structure.
    @param stmt_or_cmd      Either the name of a prepared statement or a valid SQL query string.
    @param param_values,
           param_lengths,
           nparams          If using a prepared statement, these should point to a string array with
                            the desired parameters to be inserted into the prepared statement, an
                            int array with their lengths and the number of parameters. Else they
                            should all be NULL.
    @param callback         If the query may return data, this should be a pointer to a callback
                            function handling the processing of that data. Otherwise all results
                            apart from the query's success status will be discarded.
    @param cb_arg           If a callback was specified, this parameter can be used to pass a
                            pointer to any user-specified data to the callback.

    @return 1 on success, 0 on error.
    @see wlpq_query_free(), wlpq_conn_ctx_init()
*/
int
wlpq_query_run_blocking(wlpq_conn_ctx_st *ctx, char *stmt_or_cmd,
    char **param_values, int *param_lengths, uint8_t nparams,
    wlpq_res_handler_ft *callback, void *cb_arg);

/*! Launch the query sender and connection poller threads.

    Blocks until complete.

    @param ctx A pointer to the connection context structure.

    @return 1 on success, 0 on error.
    @see wlpq_threads_launch_async()
*/
int
wlpq_threads_launch(wlpq_conn_ctx_st *ctx);

/*! Request the launch of the query sender and connection poller threads.

    Uses a temporary separate pthread to handle the process asynchronously.

    @param ctx A pointer to the connection context structure.

    @return 1 on success, 0 on error.
    @see wlpq_threads_launch()
*/
int
wlpq_threads_launch_async(wlpq_conn_ctx_st *ctx);

/*! Set the number of database connections to send queries over.

    The function will silently fail if nconn < 1. Make sure your database doesn't exceed its
    credentials!

    @param ctx      A pointer to the connection context structure.
    @param nconn    The number of connections.

    @see wlpq_threads_launch(), wlpq_threads_launch_async(), wlpq_threads_npoll_set()
*/
void
wlpq_threads_nconn_set(wlpq_conn_ctx_st *ctx, unsigned nconn);

/*! Set the number of poller threads per query sender thread.

    @param ctx      A pointer to the connection context structure.
    @param npoll    The number of pollers, function will silently fail if npoll < 1.

    @see wlpq_threads_launch(), wlpq_threads_launch_async(), wlpq_threads_nconn_set()
*/
void
wlpq_threads_npoll_set(wlpq_conn_ctx_st *ctx, unsigned npoll);

/*! Stop all query sender and connection poller threads, blocks until complete.

    @param ctx A pointer to the connection context structure.

    @return 0 on success, on error a positive integer indicating the number of errors, or -1 if the
    'ctx' parameter was NULL.

    @see wlpq_threads_launch(), wlpq_threads_launch_async(), wlpq_threads_nconn_set(),
    wlpq_threads_npoll_set(), wlpq_threads_wait_until_idle()
*/
int
wlpq_threads_stop_and_join(wlpq_conn_ctx_st *ctx);

/*! Block control flow of the calling thread until all send/poll threads are in an idle state.

    "Idle state" means not processing a query or polling a busy connection.

    @param ctx A pointer to the connection context structure.
    @see wlpq_threads_launch(), wlpq_threads_launch_async(), wlpq_threads_nconn_set(),
    wlpq_threads_npoll_set(), wlpq_threads_stop_and_join()
*/
void
wlpq_threads_wait_until(wlpq_conn_ctx_st *ctx, wlpq_thread_state_et state);

#endif /* _wlpq_h_ */
