/*  @file       dbg.h
    @brief      A set of debug macros.

    This header is almost identical to the "dbg.h" from Zed Shaw's "Learn C The
    Hard Way", expect for:

    1)  Substitution of the GCC extension `##__VA_ARGS__` with the standard `__VA_ARGS__` macro.
    2)  Removal of check_mem(A), a convenience version of check(A, M, ...) for memory errors.
    3)  Addition of error message templates.
    4)  Some code reformatting.
    5)  Allowing the user to #define DBG_STREAM, making it possible to direct output to a stream
        other than stderr (the default).

        Change 1) results in the variadic macros not accepting zero arguments for __VA_ARGS__.
        This is not an issue if a format string with 1 or more arguments is used with them.
*/

#ifndef _dbg_h_
#define _dbg_h_

#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifndef DBG_STREAM
    #define DBG_STREAM stderr
#endif

#define STRINGIFY(A) #A
#define TOSTRING(A) STRINGIFY(A)

#ifdef NDEBUG
    #define debug(M, ...)
#else
    #define debug(M, ...) fprintf(stderr,\
                "DEBUG %s:%s: " M "\n",\
                __FILE__, TOSTRING(__LINE__), __VA_ARGS__)
#endif

#define clean_errno() (!errno ? "None" : strerror(errno))

#define log_err(M, ...) fprintf(stderr,\
            "[ERROR] (%s:%s: errno: %s) " M "\n",\
            __FILE__, TOSTRING(__LINE__),\
            clean_errno(), __VA_ARGS__)

#define log_warn(M, ...) fprintf(stderr,\
            "[WARN] (%s:%s: errno: %s) " M "\n",\
            __FILE__, TOSTRING(__LINE__),\
            clean_errno(), __VA_ARGS__)

#define log_info(M, ...) fprintf(stderr,\
            "[INFO] (%s:%s) " M "\n",\
            __FILE__, TOSTRING(__LINE__), __VA_ARGS__)

#define check(A, M, ...) if(!(A)) {\
            log_err(M, __VA_ARGS__);\
            errno=0;\
            goto error;\
        }

#define sentinel(M, ...) {\
            log_err(M, __VA_ARGS__);\
            errno=0;\
            goto error;\
        }

#define check_debug(A, M, ...) if(!(A)) {\
            debug(M, __VA_ARGS__);\
            errno=0;\
            goto error;\
        }
#define CHECK_ELSE(A, E, M, ...) if(!(A)) {\
        debug(M, __VA_ARGS__);\
        errno = 0;\
        E;\
    }

#define ERR_MEM "[%s]: Out of memory."
#define ERR_FAIL "[%s]: Failed %s."
#define ERR_FAIL_N "[%s]: Failed %s # %u"
#define ERR_FAIL_A "[%s]: Failed %s %s."
#define ERR_IMPRO "[%s]: Improper %s (got %d)."
#define ERR_INVAL "[%s]: Invalid %s (got %d)."
#define ERR_UNDEF "[%s]: Undefined %s (got %d)."
#define ERR_NALLOW "[%s] %s not allowed."
#define ERR_NALLOW_A "[%s] %s not allowed, use %s instead."
#define ERR_EXTERN "[%s]: %s."
#define ERR_EXTERN_AT "[%s]: %s (%d)."
#endif
