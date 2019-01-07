/*
    This header is almost identical to the "dbg.h" from Zed Shaw's "Learn C The Hard Way",
    expect for:

    1)  Substitution of the GCC extension ##__VA_ARGS__ with the plain standard-compliant
        __VA_ARGS__ macro.
    2)  Removal of check_mem(A), a specific memory-checking version of check(A, M, ...).
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

#define clean_errno() (errno == 0 ? "None" : strerror(errno))

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
#define ERR_FAIL_N "[%s]: Failed %s # %d"
#define ERR_FAIL_A "[%s]: Failed %s %s."
#define ERR_IMPRO "[%s]: Improper %s (got %d)."
#define ERR_INVAL "[%s]: Invalid %s (got %d)."
#define ERR_UNDEF "[%s]: Undefined %s (got %d)."
#define ERR_NALLOW "[%s] %s not allowed."
#define ERR_NALLOW_A "[%s] %s not allowed, use %s instead."
#define ERR_EXTERN "[%s]: %s."
#define ERR_EXTERN_AT "[%s]: %s (%d)."
/*

#define ALEN0(\
        _00, _01, _02, _03, _04, _05, _06, _07,\
        _08, _09, _0A, _0B, _0C, _0D, _0E, _0F,\
        _10, _11, _12, _13, _14, _15, _16, _17,\
        _18, _19, _1A, _1B, _1C, _1D, _1E, _1F,\
        ...)\
    _1F

#define ALEN(...)\
    ALEN0(__VA_ARGS__,\
        0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18,\
        0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10,\
        0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,\
        0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00)

#define TRACE_VALUES0(NARGS, EXPR, FMRT, ...)\
    do {\
        if (NARGS > 1) {\
            trace_values(stderr,\
                __FILE__, TOSTRING(__LINE__),\
                "" EXPR "", "" FRMT "", NARGS,\
                (const long double[NARGS]){__VA_ARGS__});\
        } else {\
            fprintf(stderr, "%s:" __FILE__ "" TOSTRING(__LINE__) " %s\n", FRMT);\
        }\
    } while(0)

#define TRACE_VALUES(...)\
    TRACE_VALUES0(ALEN(__VA_ARGS__)),\
        #__VA_ARGS__,\
        __VA_ARGS__,\
        0)

inline void
trace_skip(const char *expr)
{

}

inline void
trace_values(FILE *stream,
    const char *file, const char *line,
    const char *expr, const char *frmt,
    size_t len, const long double arr[len])
{
    fprintf(stream, "(%s:%s:[%s] %s %Lg\n",
        file, line, trace_skip(expr), frmt, arr[0]);
    for (size_t i = 1; i < len - 1; ++i) {
        fprintf(stream, ", %Lg", arr[i]);
    }
    fputc('\n', stream);
}
*/
#endif
