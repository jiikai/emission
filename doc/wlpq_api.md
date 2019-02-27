# ‪wlpq.h - API documentation

## Synopsis

>`#include "wlpq.h"`

> Compile/link with `-lpq`.

This file documents __`wlpq`__, a wrapper around the PostgreSQL C library `libpq`.

The intention is to facilitate non-blocking, asynchronous database operation.

It is a component of the [Emission API](emiss_api.md).

### License

(c) Joa Käis (github.com/jiikai) 2018-2019 under the [MIT license](../LICENSE.md).

===============================================================================

## Includes

The header `wlpq.h` includes the following headers:

```c
#include <stdint.h>
#include <libpq-fe.h>
```

===============================================================================

## Macros

### Nonmodifiable

- Defines the name of this interface (for use in error messages).
```c
#define WLPQ "wlpq"
```

- Define lower bounds for the number of simultaneous connections and connection query/poll threads.
```c
#define WLPQ_MIN_NCONN 1
#define WLPQ_MIN_NQUERY_THREADS 1
#define WLPQ_MIN_NPOLL_THREADS 1
```

- Define the derived maximum number of connections per query/poll thread.
```c
#define WLPQ_MAX_NCONN_PER_THREAD (unsigned) (WLPQ_MAX_NCONN / WLPQ_MAX_NCONNTHREADS)
```


### User definable

All of the following are enclosed in a `#ifndef X\ #define X Y\ #endif` block.

Redefine at compile time by passing `-D<MACRO_NAME>=<value>` to the compiler.

- Timeout in seconds when establishing a database connection.
```c
#define WLPQ_CONN_TIMEOUT 10
```
- Name of the environment variable to look for if no db URL is provided explicitly at runtime .
```c
#define WLPQ_DATABASE_URL_ENV "DATABASE_URL"
```
- Define default higher bounds for
   - the number of simultaneous database connections,
   - active connection query/poll threads.
```c
#define WLPQ_MAX_NCONN 19
#define WLPQ_MAX_NCONNTHREADS WLPQ_MIN_NCONNTHREADS
```
- Maximum number of parameters in a prepared statement.
```c
#define WLPQ_MAX_NPARAMS 8
```
- ‪Poll timeout in milliseconds.
```c
#define WLPQ_POLL_TIMEOUT_MS 500
```
The stack size in bytes available to threads launched by functions of this interface.
```c
#define WLPQ_STACK_SIZE 0x200000
```

===============================================================================

## Types

### Structure types

#### `wlpq_conn_ctx_st`

‪An opaque handle to the main context structure.

```c
typedef struct wlpq_conn_ctx wlpq_conn_ctx_st;
```


#### `wlpq_query_data_st`
An opaque handle to a data structure for a single query.

```c
typedef struct wlpq_query_data wlpq_query_data_st;
```

- Pending database queries are enqueued in a singly linked list ([`utlist.h`](utlist.h)) with a separate tail-pointer housed in the connection context structure. This enables O(1) append, crucial to an efficient queue-implementation, without the need for a doubly-linked list's in this context useless backpointers.


### Function types

#### `wlpq_notify_handler_ft`

A callback function type for handling NOTIFY messages sent by the server.

```c
typedef void wlpq_notify_handler_ft(PGnotify *notify, void *arg);
```


#### `wlpq_notify_res_ft`

A callback function type for handling result sets returned by queries.

```c
typedef void wlpq_res_handler_ft(PGresult *res, void *arg);
```


### Enum types

#### `wlpq_notify_thread_state_et`

An enum type of possible thread states.

```c
typedef enum wlpq_thread_state {
    NONE, IDLE, BUSY, SUCC, FAIL
} wlpq_thread_state_et;
```

===============================================================================

## Functions

#### `wlpq_conn_ctx_free()`

Deallocate a previously allocated connection context structure.

```c
void wlpq_conn_ctx_free(wlpq_conn_ctx_st *ctx);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | The connection context structure.

- This function will silently fail if `ctx` is a `NULL` pointer.


#### `wlpq_conn_ctx_init()` ‪

Allocate and initialize a connection context structure.

```c
wlpq_conn_ctx_st * wlpq_conn_ctx_init(char *db_url);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`db_url`     | An URL to a Postgres database.

- If `db_url == NULL`, checks the environment variable `DATABASE_URL`.
- Function will fail if both `DATABASE_URL` is empty and no valid Postgres database URL is provided as a parameter.

__Returns:__ A valid pointer to the initialized context structure or `NULL` on failure.



#### `wlpq_conn_ctx_notify_handler_set()`

Set a callback for `NOTIFY` messages from the database server.

```c
void wlpq_conn_ctx_notify_handler_set(wlpq_conn_ctx_st *ctx,
        wlpq_notify_handler_ft *notify_callback, void *notify_arg);
```

|__Parameter__    |__Description__
|:----------------|:-----------------------------------------------------------
|`ctx`            | The connection context structure.
|`notify_callback`| The handler callback that will be called when a NOTIFY arrives. If `NULL`, resets the handling of notifications to the default ignore mode.
|`notify_arg`     |Optional argument of user data to be passed to `notify_callback` function.

- The default behaviour is to ignore `NOTIFY` messages. You may want to issue `LISTEN/NOTIFY` commands too, since no notifications will arrive unless explicitly requested.
- This function will silently fail if `ctx` is a `NULL` pointer.


#### `wlpq_query_free()`

‪Deallocate a previously allocated connection context structure.

```c
void wlpq_query_free(wlpq_query_data_st *data);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`data`       | A pointer to a query data structure.

- This function will silently fail if `data == NULL`.


#### `wlpq_query_init()`

‪Allocate and initialize a structure holding SQL query data.

```c
wlpq_query_data_st * wlpq_query_init(char *stmt_or_cmd,
        char **param_values, int *param_lengths,
        unsigned nparams, wlpq_res_handler_ft *callback,
        void *cb_arg, unsigned lock_until_done);
```
|__Parameter__       |__Description__
|:-------------------|:--------------------------------------------------------
|`stmt_or_cmd`       | Either the name of a prepared statement or a valid SQL query string.
|`param_values`      | If using a prepared statement, should point to a string array with the desired parameters to be inserted into the prepared statement; else `NULL`.
|`param_lengths`     | If using a prepared statement, should point to an integer array with the lengths of the strings in `param_values`; else `NULL`.
|`nparams`           | If using a prepared statement, the number of parameters; else `0`.
|`callback`          | If the query may return data, should be a pointer to a function handling the processing of that data. Otherwise all results apart from the query's success status will be discarded.
|`cb_arg`            |  If a callback was specified, `cb_arg` can be used to pass a pointer to any user-specified data to the callback.
|`lock_until_done`   | If greater than zero, this function blocks all processing of further queries until complete.

__Returns:__ A pointer to the allocated and initialized query data structure on success or `NULL` on error.


#### `wlpq_query_queue_empty()`

‪Atomically check whether the query queue is currently empty.

```c
uint8_t wlpq_query_queue_empty(wlpq_conn_ctx_st *ctx);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | A pointer to the connection context structure.

- The function will silently fail if `ctx` is a `NULL` pointer.

__Returns:__ `1` if the queue is empty, `0` if not.


#### `wlpq_query_queue_enqueue()`

‪Atomically enqueue a query data object created with [`wlpq_query_init()`]().

```c
int wlpq_query_queue_enqueue(wlpq_conn_ctx_st *ctx, wlpq_query_data_st *qr_dt);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | A pointer to the connection context structure.
|`qr_dt`      | A pointer to a query data object.

__Returns:__  `1` on success, `0` on error.


####`wlpq_query_run_blocking()`

‪Run a query that will block the calling thread until complete.

```c
int wlpq_query_run_blocking(wlpq_conn_ctx_st *ctx, char *stmt_or_cmd,
    char **param_values, int *param_lengths, uint8_t nparams,
    wlpq_res_handler_ft *callback, void *cb_arg);
```

- Parameters are identical to those of [wlpq_query_init()]().

__Returns:__  `1` on success, `0` on error.


#### `wlpq_threads_launch()`

Launch the query sender and connection poller threads.

```c
int wlpq_threads_launch(wlpq_conn_ctx_st *ctx);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | A pointer to the connection context structure.

- Blocks until complete.

__Returns:__ `1` on success, `0` on error.


#### `wlpq_threads_launch_async()`

‪Request the launch of the query sender and connection poller threads.

```c
int wlpq_threads_launch_async(wlpq_conn_ctx_st *ctx);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | A pointer to the connection context structure.

- Uses a temporary, separate thread to handle the process asynchronously.

__Returns:__ `1` on success, `0` on error.


#### `wlpq_threads_nconn_set()`

Set the number of database connections to send queries over.

```c
void wlpq_threads_nconn_set(wlpq_conn_ctx_st *ctx, unsigned nconn);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | A pointer to the connection context structure.
|`nconn`      | The number of connections.

- The function will silently fail if `nconn < 1`.
- Make sure your database doesn't exceed its credentials!


#### `wlpq_threads_stop_and_join()`

‪Stop all query sender and connection poller threads, blocking until complete.

```c
int wlpq_threads_stop_and_join(wlpq_conn_ctx_st *ctx);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | A pointer to the connection context structure.

__Returns:__ `0` on success, on error a positive integer (the number of errors), or `-1` if `ctx == NULL`.


#### `wlpq_threads_wait_until()`

Block control flow of the calling thread until all send/poll threads are in a certain state.

```c
void wlpq_threads_wait_until_idle(wlpq_conn_ctx_st *ctx, wlpq_thread_state_et state);
```

|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | A pointer to the connection context structure.
|`state`      | A desired state value of type [wlpq_thread_state_et](#wlpq_thread_state_et).

- The function will silently fail if `ctx == NULL`.
