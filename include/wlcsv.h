/*! @file           wlcsv.h
    @version        0.1.0
    @brief          Interface to wlcsv, a libcsv wrapper API.
    @details        See (../README) and [API documentation](../doc/wlcsv_api.md).
    @copyright      (c) Joa Käis (github.com/jiikai) 2018-2019, [MIT](../LICENSE).
*/

#ifndef _wlcsv_h_
#define _wlcsv_h_

/*
**  INCLUDES
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pcre.h>
#include "csv.h"

/*
**  MACROS
*/

/*  CONSTANTS */

/*!  Error message provider name. */
#define WLCSV "wlcsv"

#define WLCSV_VERSION_MAJOR 0
#define WLCSV_VERSION_MINOR 1
#define WLCSV_VERSION_PATCH 0

/*! Currently supports matching by a keyword (strcmp(), a PCRE regex (pcre_exec(),
    and the current row and column numbers. */
#define WLCSV_NCALLBACK_MATCH_TYPES 4

/*!  Option flag for ignoring empty fields completely when parsing. */
#define WLCSV_IGNORE_EMPTY_FIELDS 1

/*! Maximun number of enlisted callbacks, including the default
    but excluding the end-of-row callback. */
#ifndef WLCSV_NCALLBACKS_MAX
    #define WLCSV_NCALLBACKS_MAX 0x10
#elif ((WLCSV_NCALLBACKS_MAX - 1) % WLCSV_NCALLBACK_MATCH_TYPES)
    #define  WLCSV_NCALLBACKS_MAX 0x10
#endif

/*  FUNCTION-LIKE MACROS */

#define WLCSV_MATCH_STR(expr)\
    &(wlcsv_callback_match_to_ut){.key_or_rgx = expr}
#define WLCSV_MATCH_NUM(expr)\
    &(wlcsv_callback_match_to_ut){.row_or_col = expr}


/*  A getter expression for the state structure (type wlcsv_state_st, see below). */
#define WLCSV_STATE_MEMB_GET(stt, member)\
    (stt ? stt->member : UINT_MAX)

/*
**  TYPES
*/

/*  STRUCTURE TYPES */

/*! An opaque handle for the main context structure. */
typedef struct wlcsv_ctx wlcsv_ctx_st;

/*! A transparent structure with fields holding data regarding the current state of parsing.

    The fields eor_terminator, col, and row are updated by wlcsv during csv parsing. They are
    intended as read-only values; however, they hold no significance to wlcsv and are not relied
    upon during any operation.

    The latter two fields, lineskip and options, are intended as modifiable entities and
    accordingly, setters implemented as inline functions are provided for them. Seeing as
    wlcsv_state is a transparent structure, there is of course nothing stopping one from
    directly manipulating the fields themselves. Use caution.

    The convenience getter macro expression WLCSV_STATE_MEMBER_GET(stt, member) can be used
    with all field names to provide their value in a NULL-safe manner.

    @member eor_terminator: The code of the last character on the previous row.
    @member col:            The index of the current column.
    @member row:            The index of the current row.
    @member lineskip:       The user-set offset in lines from the start of the csv file to ignore.
    @member options:        A bit mask of currently active wlcsv options.
*/
typedef struct wlcsv_state {
    int                         eor_terminator;
    unsigned                    col;
    unsigned                    row;
    unsigned                    lineskip;
    unsigned                    options;
} wlcsv_state_st;

typedef struct wlcsv_callback_entry wlcsv_callback_entry_st;

/*  FUNCTION TYPES */

typedef unsigned wlcsv_callback_match_ft(const char *, size_t, wlcsv_state_st *);

/*! A function type for user-provided callbacks that handle parsed csv field data.

    Callback selection proceeds as follows:
    1.  If the pointer to the current field is NULL (or the field is empty in case
        ignore_empty_fields is set), return with no callback.
    2.  If ignore_regex is not NULL, check field text for match. Return with no callback if so.
    3.  If h_kcallbacks has entries, check if a string key matching the field text is found. If so,
        select the associated callback function and return.
    4.  If h_rcallbacks has entries, check if a row key (int) matching the current row number
        exists. If so, select the associated callback function and return.
    5.  If h_ccallbacks has entries, check if a column key (int) matching the current column number
        exists. If so, select the associated callback function and return.
    6.  If a default callback has been set, call it and return.
*/
typedef void wlcsv_callback_ft(void *, size_t, void *);

/*! A function type for optional callbacks that are called as the end of any row is reached. */
typedef void wlcsv_eor_callback_ft(void *);

/* ENUM & UNION TYPES */

/*! Match types for callbacks.

    @see wlcsv_callback_match_to_ut, wlcsv_callback_ft, wlcsv_callbacks_set()
*/
typedef enum wlcsv_callback_match_by {
    KEYWORD, REGEX, ROW, COLUMN
} wlcsv_callback_match_by_et;

/*! A union type for passing criteria to match a callback against.

    @member row_or_col  Corresponds to wlcsv_callback_match_by_et type ROW or COLUMN.
    @member key_or_rgx  Corresponds to wlcsv_callback_match_by_et type KEYWORD or REGEX.

    @see wlcsv_callback_match_by_et, wlcsv_callback_ft, wlcsv_callbacks_set()
*/
typedef union {
    unsigned                    row_or_col;
    char                       *key_or_rgx;
} wlcsv_callback_match_to_ut;

/*
**  FUNCTIONS
*/

/*! Frees the wrapper struct and all memory that was allocated by the wrapper.

    @param ctx A previously initialized instance of the context structure.

    @remark Does nothing if @a ctx is NULL.
    @see wlcsv_init()
*/
void
wlcsv_free(wlcsv_ctx_st *ctx);

/*! Initializes the libcsv wrapper context structure.

    @param  ignore_rgx              A PCRE regex, a match causes a field to be ignored.
    @param  default_callback        A function to call if no other condition holds for a field.
    @param  default_callback_data   A default data pointer to be passed to callbacks.
    @param  nkeycallbacks,
            nrgxcallbacks,
            nrowcallbacks,
            ncolcallbacks,          Total count of callbacks by keyword, regex, row or column match.
    @param lineskip                 Offset in lines from the beginning of the file to be skipped
                                    over. Default 0, can be set later.
    @param options                  [TODO]

    @remark Parameters ignore_rgx, default_callback, default_callback_data, lineskip and options can
    be set or changed later.

    @return The initialized struct or NULL on error.
    @see wlcsv_free(), wlcsv_callbacks_default_set(), wlcsv_ignore_regex_set(),
    wlcsv_state_lineskip_set(), wlcsv_state_options_set()
*/
wlcsv_ctx_st *
wlcsv_init(char *ignore_rgx, wlcsv_callback_ft default_callback,
        void *default_callback_data, uint8_t nkeycallbacks,
        uint8_t nrgxcallbacks, uint8_t nrowcallbacks,
        uint8_t ncolcallbacks, unsigned lineskip,
        unsigned options);

/*! Set a new target csv file path.

    @param ctx      An initialized context structure handle.
    @param path,
           len      The path to the target file and its string length.

    @return 1 on success, 0 if any of the parameters were NULL/0 and -1 on memory error.
    @see wlcsv_free(), wlcsv_init(), wlcsv_read(), wlcsv_preview()
*/
int
wlcsv_file_path(wlcsv_ctx_st *ctx, const char *path, size_t len);

/*! Sends the first `nrows` from the csv file set via wlcsv_set_target_path() to `callback`.

    @param ctx          An initialized context structure handle.
    @param nrows        The number of rows to preview.
    @param buf_size     An expected byte size of the data in memory (optional).
    @param callback     A callback function.

    @remark Can be used for e.g. determining a lineskip, header field names and other structural
    features of the csv file.

    @remark Contrary to callbacks used when processing files with wlcsv_read(), `callback` is passed
    directly to the underlying libcsv parser. As such filtering rules have no effect.

    @return The number of parsed bytes on success or -1 on NULL @a ctx or memory/file stream error.
    @see wlcsv_free(), wlcsv_init(), wlcsv_read()
*/
int
wlcsv_file_preview(wlcsv_ctx_st *ctx, unsigned nrows, size_t buf_size,
    wlcsv_callback_ft *callback);

/*! Processes a csv file, the path to which was set via wlcsv_set_target_path().

    @param ctx          An initialized context structure handle.
    @param buf_size     Byte size of the data.

    @return Count of parsed bytes, or 0 if no path had previously been set, or -1 on NULL @a ctx or
            a memory/file stream error.
    @see wlcsv_free(), wlcsv_init(), wlcsv_preview()
*/
int
wlcsv_file_read(wlcsv_ctx_st *ctx, size_t buf_size);

/*!*/
int
wlcsv_callbacks_active(wlcsv_ctx_st *ctx, uint8_t id);

/*!  */
int
wlcsv_callbacks_clear(wlcsv_ctx_st *ctx, uint8_t id);

/*! */
void
wlcsv_callbacks_clear_all(wlcsv_ctx_st *ctx);

/*! Set or unset the default callback function and/or the default callback data.

    Does nothing if `ctx == NULL`.

    @param ctx          An initialized context structure handle.
    @param callback  A function pointer to the new default callback function. If NULL, no
                        function will be called if no keyword/row/column matches exist for the
                        current field.
    @param data
    @see wlcsv_free(), wlcsv_init(), wlcsv_read(), wlcsv_reset_callbacks()
*/
void
wlcsv_callbacks_default_set(wlcsv_ctx_st *ctx, wlcsv_callback_ft *callback, void *data);

/*! Set or unset a new end-of-row callback function.

    Not called in the preview mode launched with wlcsv_preview(). Does nothing if @a ctx is NULL.

    @param ctx              An initialized context structure handle.
    @param new_eor_function A function pointer to the new end-of-row callback function. If NULL, no
                            user-specified function will be called by the end of current row.

    @see wlcsv_free(), wlcsv_init(), wlcsv_read(), wlcsv_reset_callbacks()
*/
void
wlcsv_callbacks_eor_set(wlcsv_ctx_st *ctx, wlcsv_eor_callback_ft *eor_callback);

/*! Associate a callback function with a regex, keyword, row or a column number.

    @param ctx           An initialized context structure handle.
    @param match_by      An enum type denoting the property against which to match.
    @param match_to      A union holding either an unsigned value or an object/function pointer.
    @param callback      If not NULL, call when encountering field with matching data.
    @param callback_data If not NULL, this pointer will be passed to the callback when invoked.
                            Otherwise the context-wise data pointer is used (if set).
    @param append        If 0, append this entry to list end. Else prepend as list head.

    @return  An id for this callback association on success or `UINT8_MAX` on any error.

    @see wlcsv_callbacks_clear(), wlcsv_callbacks_default_set(), wlcsv_callbacks_eor_set(),
    wlcsv_callbacks_toggle()
*/
uint8_t
wlcsv_callbacks_set(wlcsv_ctx_st *ctx,
    wlcsv_callback_match_by_et match_by,
    wlcsv_callback_match_to_ut *match_to,
    wlcsv_callback_ft *callback,
    void *callback_data,
    unsigned once);

/*! Activate a disabled or deactive an enabled callback.

    @param ctx      An initialized context structure handle.
    @param id       An id of the target callback, as obtained from wlcsv_callbacks_set().

    @return `1` on success, `0` on `ctx == NULL` and `-1` if no callback with `id` was found.
    @see wlcsv_callbacks_active(), wlcsv_callbacks_set()
*/
int
wlcsv_callbacks_toggle(wlcsv_ctx_st *ctx, uint8_t id);

/*! Set or unset a PCRE regex that will cause any field that matches to be ignored.

    @param ctx      An initialized context structure handle.
    @param regex    A regular expression string. If NULL, no fields are ignored by a regex match.

    @return `1` on success, `0` on `ctx == NULL` and `-1` on a regex compile error.
    @see wlcsv_read(), wlcsv_preview()
*/
int
wlcsv_ignore_regex_set(wlcsv_ctx_st *ctx, char *regex);

/*! Obtain a pointer to the wlcsv context state structure.

    @param ctx      An initialized context structure handle.

    @return A pointer to the state structure or `NULL` on error.
    @see wlcsv_state_lineskip_set(), wlcsv_state_options_set(), WLCSV_STATE_MEMB_GET()
*/
wlcsv_state_st *
wlcsv_state_get(wlcsv_ctx_st *ctx);

/*  EXTERNALLY VISIBLE INLINE FUNCTIONS  */

inline void
wlcsv_state_lineskip_set(wlcsv_state_st *stt, unsigned skip)
{
    if (stt)
        stt->lineskip = skip;
}

inline void
wlcsv_state_options_set(wlcsv_state_st *stt, unsigned options)
{
    if (stt)
        stt->options ^= options;
}
#endif /* _wlcsv_h_ */
