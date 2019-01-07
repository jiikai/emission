#ifndef _PSQL_DB_H_
#define _PSQL_DB_H_

#ifndef _XOPEN_SOURCE
    #define _XOPEN_SOURCE 700
#endif
#ifndef MAX_STACK_SIZE
    #define MAX_STACK_SIZE 0x100000
#endif

#include <libpq-fe.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>
#include "dependencies/utstack.h"

#define PSQLDB "PSQL-DB"

#define PSQLDB_MAX_NCONN 10

#ifndef PSQLDB_CONN_TIMEOUT
    #define PSQLDB_CONN_TIMEOUT 10
#endif

typedef void (*psqldb_res_callback)(PGresult *res);

/* A stack element struct, used for housing free thread "slot" information. */

struct conn_thread_stack_el {
    uint8_t thread_num;
    struct conn_thread_stack_el *next;
};

/*  Helper structs for threaded MO. */

struct conn_thread_data {
    /* pointer to this thread's stack struct; thread_num can also be obtained from here */
    struct conn_thread_stack_el *thread_stack_el;
    /* pointer to the context struct buffer containing db connection information */
    char **db_conn_info;
    /* data to be passed to start routine of this thread */
    char *data;
    /* a callback function pointer; if != NULL, use psqldb_select_concurrent() and pass this as a parameter */
    psqldb_res_callback callback;
    /* storage for the return value of this thread */
    uint8_t retval;
};

struct psqldb_conn_thread_ctx {
    /* Struct containing the data that will be passed to the next created thread. */
    struct conn_thread_data *new_thread_data;
    /* A LIFO stack containing thread_numbers indicating free thread "slots". */
    struct conn_thread_stack_el *free_thread_stack;
    /* Database connection information buffer. */
    char *db_conn_info;
    /* A fixed-size array storing the IDs of created threads. */
    pthread_t thread_ids[PSQLDB_MAX_NCONN];
    /* For setting thread attributes. */
    pthread_attr_t default_attr;
    uint8_t ready;
};

struct psqldb_conn_thread_ctx *psqldb_init_conn_thread_ctx();
int psqldb_set_new_thread_data(struct psqldb_conn_thread_ctx *ctx, char* data, psqldb_res_callback callback);
void psqldb_free_conn_thread_ctx(struct psqldb_conn_thread_ctx *ctx);
void psqldb_concurrency_manager(struct psqldb_conn_thread_ctx *ctx);
int psqldb_select_concurrent(char *db_url, char* query, psqldb_res_callback res_function);
int psqldb_insert_concurrent(char *db_url, char* query);

#endif
