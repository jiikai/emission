#undef NDEBUG
#ifndef _minunit_h
#define _minunit_h

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>
#ifdef _WIN32
    #include <windows.h>
    #define msleeper(x) Sleep(x)
#else
    #include <unistd.h>
    #define msleeper(x) struct timespec ts1, ts2;\
                        ts1.tv_sec = 0; ts1.tv_nsec = 0;\
                        ts2.tv_sec = 0; ts2.tv_nsec = 1000000L;\
                        nanosleep(&ts1, &ts2)
#endif

#include "dbg.h"
#include "illist.h"

/* BEGIN MOD */

typedef char* (*mu_test_function) ();

typedef struct mu_wrapper {
    void *arg;
    struct timespec time_a;
    struct timespec time_b;
    mu_test_function *test;
    struct list_head list;
} muWrapper;

muWrapper *muWrapper_init(mu_test_function *test, struct list_head *head) {
    muWrapper *muwr = malloc(sizeof(muWrapper));
    check(muwr, ERR_MEM, "minunit");
    muwr->arg = NULL;
    muwr->test = test;
    INIT_LIST_HEAD(&muwr->list);
    list_add(&muwr->list, head);
    return muwr;
error:
    return NULL;
}

#pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
    inline void* __attribute__((always_inline))
    functobj_ptr(char* (*fptr)())
    {
        void *optr = &fptr;
        return container_of(optr, muWrapper, test);
    }
#pragma GCC diagnostic pop

#define mu_suite_start() char *message = NULL

#define mu_assert(test, message) if (!(test)) {\
    log_err("[mu]: %s", message); return message; }
#define mu_run_test(test) debug("\n-----%s", " " #test);\
    message = test(); tests_run++; if (message) return message;

#define RUN_TESTS(name) int main(int argc, char *argv[]) {\
    argc = 1;\
    debug("----- RUNNING: %s", argv[0]);\
    printf("----\nRUNNING: %s\n", argv[0]);\
    char *result = name();\
    if (result != 0) {\
        printf("FAILED: %s\n", result);\
    }\
    else {\
        printf("ALL TESTS PASSED\n");\
    }\
    printf("Tests run: %d\n", tests_run);\
    exit(result != 0);\
}

int tests_run;

#endif
