/*! @file           wlcsv.c
**  @version        0.1.0
**  @brief          Implements [wlcsv.h](../include/wlcsv.h), a [libcsv](../include/dep/csv.h) wrapper.
**  @details        See (../README.md) and [API documentation]./doc/wlcsv_api.md.
**  @copyright:     (c) Joa Käis (github.com/jiikai) 2018-2019, [MIT](../LICENSE).
*/

/*
**  INCLUDES
*/

#include "wlcsv.h"
#include <ctype.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdbool.h>
#include "dbg.h"

/*
**  MACROS
*/

/*  The default callback is always stored at index 0 in the callback array. */
#define DEFAULT_CALLBACK_IDX 0

/*  Shorthand for calling the default callback. */
#define DEFAULT_CALLBACK(tbl, ...)\
    tbl[DEFAULT_CALLBACK_IDX]->function(__VA_ARGS__)

/*  Shorthand for retrieving a pointer to the default callback data. */
#define DEFAULT_CALLBACK_DATA(tbl)\
    tbl[DEFAULT_CALLBACK_IDX]->data

/*  Access a structure of type *type* from a *pointer* to its *member*.
    This is a simplified (sans type checking) version of the Linux kernel container_of() macro. */
#define CONTAINER_OF(pointer, type, member)\
    ((type *)((char *)(pointer) - offsetof(type, member)))

/*
**  STRUCTURES & TYPES
*/

struct wlcsv_callback_entry {
    bool                            once;
    wlcsv_callback_match_by_et      match_by;
    union {
        unsigned                    row_or_col;
        char                       *key;
        pcre                       *rgx;
    };
    wlcsv_callback_ft              *function;
    void                           *data;
};

/*  Main context @struct for wlcsv.

    @member parser:         The csv parser structure from [libcsv](../include/dep/csv.h).
    @member path, path_len: Path to the csv file to be parsed next and its length.
    @member ignore_regex:   PCRE regular expression, on match the field is ignored.
    @member callbacks:      An anonymous inner @struct, containing:
        @member eor_callback:   An optional, user-provided function called at the end of every row.
        @member tb, tbl_length: Table of callback entries and their count.
        @member tbl_idx_skip:   An array for traversing the table omitting disabled callbacks.
        @member tbl_idx_type:   The starting indices for each type of callback.
        @member tbl_offs_col:   A count of (active) COLUMN callbacks encountered earlier on the row.
    @member state:          A structure storing the current state of parsing.

    @remark tbl_offs_col is nullified internally after each row.
    @remark ROW callbacks are automatically disabled after the relevant row has passed.
    @remark The callback table tbl is traversed using skip_idx as follows:
        0. Initialize i = 0.
        Repeat:
        1. If skip_idx[i] == UINT8_MAX, quit. Otherwise inspect callback entry at tbl[skip_idx[i]].
        2. If a match is found, return the index number skip_idx[i].
           Otherwise set i = skip_idx[skip_idx[i]].
*/

struct wlcsv_ctx {
    struct csv_parser           parser;
    size_t                      path_len;
    char                       *path;
    pcre                       *ignore_regex;
    struct {
        wlcsv_callback_ft          *preview_callback;
        wlcsv_eor_callback_ft      *eor_callback;
        wlcsv_callback_entry_st    *tbl[WLCSV_NCALLBACKS_MAX];
        uint8_t                     tbl_length;
        uint8_t                     tbl_idx_skip[WLCSV_NCALLBACKS_MAX];
        uint8_t                     tbl_idx_type[WLCSV_NCALLBACK_MATCH_TYPES];
        uint8_t                     tbl_offs_col;
    } callbacks;
    wlcsv_state_st              state;
};

/*
**  FUNCTIONS
*/

/*  STATIC */

static inline int
file_skip_lines(FILE *fp, int line_offset)
{
    char buffer[0x1000];
    int i = 0;
    while (i < line_offset && fgets(buffer, 0xFFF, fp))
        ++i;
    return i;
}

static inline void
callbacks_clear_all(wlcsv_ctx_st *ctx, bool final)
{
    wlcsv_callback_entry_st **tbl = ctx->callbacks.tbl;
    uint8_t length = ctx->callbacks.tbl_length;
    for (uint8_t i = final ? 0 : 1; i < length; ++i)
        if (tbl[i]) {
            if (tbl[i]->match_by == REGEX)
                pcre_free(tbl[i]->rgx);
            else if (tbl[i]->match_by == KEYWORD)
                free(tbl[i]->key);
            free(tbl[i]);
            tbl[i] = NULL;
        }

    if (!final) {
        memset(ctx->callbacks.tbl_idx_skip, 0, sizeof(ctx->callbacks.tbl_idx_skip));
        ctx->callbacks.tbl_idx_skip[DEFAULT_CALLBACK_IDX] = UINT8_MAX;
    }
}

/* Enable/disable a callback. */
static inline void
callbacks_toggle(wlcsv_ctx_st *ctx, uint8_t i)
{
    uint8_t *skip_idx = ctx->callbacks.tbl_idx_skip;
    uint8_t j, prev, next = skip_idx[i];
    /*  Locate the first active preceding callback index.
        Entry skip_idx[0] houses the index of the first active entry. */
    for (j = 0, prev = skip_idx[0]; prev < i; prev = skip_idx[j])
        j = prev;

    if (ctx->callbacks.tbl[i]->match_by == COLUMN) {
        uint8_t col_offs = ctx->callbacks.tbl_offs_col;
        if (next && i == col_offs)
             ctx->callbacks.tbl_offs_col = skip_idx[i];
        else if (!next && i < col_offs)
            ctx->callbacks.tbl_offs_col = i;
    }
    uint8_t tmp = skip_idx[j];
    skip_idx[j] = next ? next : i;
    skip_idx[i] = next ? 0 : tmp;
}

static inline uint8_t
callbacks_search(wlcsv_ctx_st *ctx, const char *field_str, size_t len)
{
    wlcsv_callback_entry_st **tbl = ctx->callbacks.tbl;
    unsigned  col = ctx->state.col,
              row = ctx->state.row;
    uint8_t *skip_idx = ctx->callbacks.tbl_idx_skip;
    uint8_t i,
        rgx_i = ctx->callbacks.tbl_idx_type[REGEX],
        row_i = ctx->callbacks.tbl_idx_type[ROW],
        col_i = ctx->callbacks.tbl_idx_type[COLUMN],
        col_offs = ctx->callbacks.tbl_offs_col;
    for (i = skip_idx[0]; i < rgx_i; i = skip_idx[i]) {
        int ret = strncmp(field_str, tbl[i]->key, len);
        if (!ret)
            goto MATCH;
        if (ret < 0) {
            i = skip_idx[i];
            break;
        }
    }
    for (i = i; i < row_i; i = skip_idx[i]) {
        int ret = pcre_exec(tbl[i]->rgx, 0, field_str, len, 0, 0, 0, 0);
        if (ret >= 0)
            goto MATCH;
        else if (ret != PCRE_ERROR_NOMATCH)
            log_err(ERR_EXTERN_AT, "PCRE", "matching error", ret);
    }
    uint8_t j = UINT8_MAX;
    for (i = i; i < col_i; i = j) {
        if (tbl[i]->row_or_col == row)
            goto MATCH;
        j = skip_idx[i];
        if (tbl[i]->row_or_col < row)
            callbacks_toggle(ctx, i);
    }
    i = col_offs;
    for (i = col_offs; i != UINT8_MAX; i = skip_idx[i])
        if (tbl[i]->row_or_col <= col) {
            ctx->callbacks.tbl_offs_col = skip_idx[i];
            if (tbl[i]->row_or_col == col)
                goto MATCH;
        }

    return UINT8_MAX;
MATCH:
    if (tbl[i]->once)
        callbacks_toggle(ctx, i);
    return i;
}

static inline uint8_t
callbacks_enlist(wlcsv_ctx_st *ctx, wlcsv_callback_entry_st *entry)
{
    wlcsv_callback_match_by_et type = entry->match_by;
    wlcsv_callback_entry_st **tbl = ctx->callbacks.tbl;
    uint8_t i = ctx->callbacks.tbl_idx_type[type];
    if (!tbl[i])
        tbl[i] = entry;
    else {
        wlcsv_callback_entry_st *elem = tbl[i];
        if (type == COLUMN || type == ROW)
            while (elem && elem->row_or_col < entry->row_or_col)
                elem = tbl[++i];
        else
            while (elem && strcmp(elem->key, entry->key) < 0)
                elem = tbl[++i];
        if (!elem)
            tbl[i] = entry;
        else {
            uint8_t j = i + 1;
            while (tbl[j])
                ++j;
            while (j >= i) {
                tbl[j] = tbl[j - 1];
                --j;
            }
            tbl[i] = entry;
        }
    }
    callbacks_toggle(ctx, i);
    return i;
}

static inline void
callbacks_reset(struct wlcsv_ctx *ctx)
{
    wlcsv_callback_entry_st **tbl = ctx->callbacks.tbl;
    ctx->callbacks.tbl_offs_col = ctx->callbacks.tbl_idx_type[COLUMN];
    uint8_t i,
            length = ctx->callbacks.tbl_length,
           *skip_idx = ctx->callbacks.tbl_idx_skip;
    for (i = 0; i < length - 1; ++i)
        if (tbl[i + 1])
            skip_idx[i] = i + 1;

    skip_idx[i] = UINT8_MAX;
}

static void
callbacks_forward(void *field, size_t len, void *data)
{
    struct wlcsv_ctx *ctx = (struct wlcsv_ctx *)data;
    bool process = !(ctx->state.options & WLCSV_IGNORE_EMPTY_FIELDS) ? true
                    : field && len ? true
                    : false;
    if (process) {
        const char *str = (const char *)field;
        if (field && len && ctx->ignore_regex) {
            /* Check if this field should be ignored. */
            int ret = pcre_exec(ctx->ignore_regex, 0,
                        str, len, 0, 0, 0, 0);
            if (ret >= 0)
                goto CONTINUE;
            if (ret != PCRE_ERROR_NOMATCH)
                log_err(ERR_EXTERN_AT, "PCRE", "matching error", ret);
        }
        wlcsv_callback_entry_st **tbl = ctx->callbacks.tbl;
        uint8_t i = callbacks_search(ctx, str, len);
        if (i != UINT8_MAX)
            tbl[i]->function(field, len,
                    (tbl[i]->data ? tbl[i]->data : DEFAULT_CALLBACK_DATA(tbl)));
        else if (tbl[DEFAULT_CALLBACK_IDX]->function)
            DEFAULT_CALLBACK(tbl, field, len, DEFAULT_CALLBACK_DATA(tbl));
    }
CONTINUE:
    ctx->state.col++;
}

static void
callbacks_forward_preview(void *field, size_t len, void *data)
{
    struct wlcsv_ctx *ctx = (struct wlcsv_ctx *)data;
    ctx->callbacks.preview_callback(field, len, DEFAULT_CALLBACK_DATA(ctx->callbacks.tbl));
}

static inline void
callbacks_eor_update_state(wlcsv_ctx_st *ctx, int terminator)
{
    ctx->state.eor_terminator = terminator;
    ctx->state.col = 0;
    ctx->state.row++;
}

static void
callbacks_eor(int terminator, void *data)
{
    struct wlcsv_ctx *ctx = (struct wlcsv_ctx *)data;
    uint8_t first_col_match = ctx->callbacks.tbl_idx_type[COLUMN];
    uint8_t *skip_idx = ctx->callbacks.tbl_idx_skip;
    while (!skip_idx[first_col_match]) {
        if (first_col_match == ctx->callbacks.tbl_length) {
            first_col_match = UINT8_MAX;
            break;
        }
        ++first_col_match;
    }
    ctx->callbacks.tbl_offs_col = first_col_match;
    if (ctx->callbacks.eor_callback)
        ctx->callbacks.eor_callback(DEFAULT_CALLBACK_DATA(ctx->callbacks.tbl));
    callbacks_eor_update_state(ctx, terminator);
}

static void
callbacks_eor_preview(int terminator, void *data)
{
    struct wlcsv_ctx *ctx = (struct wlcsv_ctx *)data;
    ctx->state.eor_terminator = terminator;
    ctx->state.col = 0;
    ctx->state.row++;
}

/*  IMPLEMENTATIONS FOR FUNCTION PROTOTYPES */

void
wlcsv_free(struct wlcsv_ctx *ctx)
{
    if (ctx) {
        if (ctx->path)
            free(ctx->path);
        if (&ctx->parser)
            csv_free(&ctx->parser);
        if (ctx->ignore_regex)
            pcre_free(ctx->ignore_regex);
        callbacks_clear_all(ctx, true);
        free(ctx);
    }
}

wlcsv_ctx_st *
wlcsv_init(char *ignore_rgx, wlcsv_callback_ft *default_callback,
    void *default_callback_data, uint8_t nkeycallbacks,
    uint8_t nrgxcallbacks, uint8_t nrowcallbacks,
    uint8_t ncolcallbacks, unsigned offset,
    unsigned options)
{
    unsigned callback_tbl_size = nkeycallbacks + nrgxcallbacks
                                + nrowcallbacks + ncolcallbacks + 1;
    check(callback_tbl_size <= WLCSV_NCALLBACKS_MAX, ERR_FAIL_N, WLCSV,
            "initializing: sum of callbacks exceeds maximum", WLCSV_NCALLBACKS_MAX);

    struct wlcsv_ctx *ctx = malloc(sizeof(struct wlcsv_ctx));
    check(ctx, ERR_MEM, WLCSV);
    memset(ctx, 0, sizeof(struct wlcsv_ctx));

    ctx->ignore_regex = 0;
    if (ignore_rgx) {
        const char *error_msg;
        int error_offset;
        ctx->ignore_regex = pcre_compile(ignore_rgx, 0, &error_msg, &error_offset, 0);
        if (!ctx->ignore_regex)
            log_err(ERR_EXTERN_AT, "PCRE", error_msg, error_offset);
    }
    memset(&ctx->state, 0, sizeof(ctx->state));
    memset(&ctx->callbacks, 0, sizeof(ctx->callbacks));
    ctx->callbacks.tbl_length            =  callback_tbl_size;
    ctx->callbacks.tbl_idx_type[KEYWORD] = 1;
    ctx->callbacks.tbl_idx_type[REGEX]   = 1 + nkeycallbacks;
    ctx->callbacks.tbl_idx_type[ROW]     = ctx->callbacks.tbl_idx_type[REGEX] + nrgxcallbacks;
    ctx->callbacks.tbl_idx_type[COLUMN]  = ctx->callbacks.tbl_idx_type[ROW] + nrowcallbacks;

    ctx->callbacks.tbl[DEFAULT_CALLBACK_IDX] = calloc(1, sizeof(wlcsv_callback_entry_st));
    check(ctx, ERR_MEM, WLCSV);
    ctx->callbacks.tbl[DEFAULT_CALLBACK_IDX]->function  = default_callback;
    ctx->callbacks.tbl[DEFAULT_CALLBACK_IDX]->data      = default_callback_data;
    ctx->callbacks.tbl_idx_skip[DEFAULT_CALLBACK_IDX]   = UINT8_MAX;

    ctx->callbacks.tbl_offs_col = ctx->callbacks.tbl_idx_type[COLUMN];
    ctx->state.options = options;
    ctx->state.lineskip = offset;

    int ret = csv_init(&ctx->parser, options & WLCSV_IGNORE_EMPTY_FIELDS
                ? CSV_APPEND_NULL | CSV_EMPTY_IS_NULL
                : CSV_APPEND_NULL);
    check(!ret, ERR_FAIL, WLCSV, "initializing csv_parser");
    return ctx;
error:
    return 0;
}

int
wlcsv_file_path(struct wlcsv_ctx *ctx, const char *path, size_t path_len)
{
    if (!ctx || !path || !path_len) {
        log_err(ERR_NALLOW, WLCSV, "NULL parameter");
        return 0;
    }
    char *path_buffer;
    if (ctx->path) {
        path_buffer = ctx->path;
        size_t old_path_len = ctx->path_len;
        if (old_path_len > path_len)
            /*  Use realloc() when scaling the buffer down, seeing as there is
                no chance of malloc() deciding to allocate a new memory area,
                which is a slower process than shrinking in-place. */
            path_buffer = realloc(ctx->path, path_len + 1);

        else {
            /*  Otherwise, do a free() & calloc(). Preserving
                the old path data is not required. */
            free(ctx->path);
            path_buffer = calloc(path_len + 1, sizeof(char));
        }
    } else
        /*  This was the first path provided. */
        path_buffer = calloc(path_len + 1, sizeof(char));

    if (!path_buffer) {
        log_err(ERR_MEM, WLCSV);
        return -1;
    }
    memcpy(path_buffer, path, path_len);
    ctx->path = path_buffer;
    return 1;
}

int
wlcsv_file_preview(struct wlcsv_ctx *ctx, unsigned nrows, size_t buf_size,
    wlcsv_callback_ft *callback)
{
    if (!ctx || !ctx->path) {
        log_err(ERR_NALLOW, WLCSV, !ctx ? "NULL ctx parameter" : "file path not set");
        return 0;
    }
    ctx->callbacks.preview_callback = callback;
    FILE *fp = NULL;
    void *buffer = NULL;
    fp = fopen(ctx->path, "r");
    check(fp, ERR_FAIL, WLCSV, "opening file");
    ctx->state.col = 0;
    ctx->state.row = 0;
    buffer = malloc(buf_size);
    check(buffer, ERR_MEM, WLCSV);
    int ret = 0;
    unsigned long i = 0;
    unsigned current_row = 0;
    while (current_row < nrows) {
        if (i == buf_size && !feof(fp)) {
            void *ptr = buffer;
            buf_size *= 2;
            buffer = realloc(ptr, buf_size);
            check(buffer, ERR_MEM, WLCSV);
        }
        ret = fread((uint8_t *)buffer + i, sizeof(char), 1, fp);
        if (!ret) {
            check(feof(fp), ERR_FAIL, WLCSV,
                "an error occured reading the preview portion of file.");
            log_info("End of file was reached after reading %lu bytes of data.",
                i);
            return 1;
        }
        if (*((char *)((uint8_t *)buffer + i)) == '\n')
            current_row++;
        ++i;
    }
    fclose(fp);
    size_t parsed = csv_parse(&ctx->parser, buffer, i,
                        callbacks_forward_preview,
                        callbacks_eor_preview,
                        ctx);
    check(parsed == i, ERR_EXTERN, "libcsv", csv_strerror(csv_error(&ctx->parser)));
    csv_fini(&ctx->parser, callback, callbacks_eor, DEFAULT_CALLBACK_DATA(ctx->callbacks.tbl));
    free(buffer);
    ctx->callbacks.preview_callback = 0;
    return parsed;
error:
    if (fp)
        fclose(fp);
    if (buffer)
        free(buffer);
    return -1;
}

int
wlcsv_file_read(struct wlcsv_ctx *ctx, size_t buf_size)
{
    if (!ctx || !ctx->path) {
        log_err(ERR_NALLOW, WLCSV, !ctx ? "NULL ctx parameter" : "file path not set");
        return 0;
    }
    FILE *fp = NULL;
    void *buffer = NULL;
    fp = fopen(ctx->path, "r");
    check(fp, ERR_FAIL, WLCSV, "opening file")
    rewind(fp);
    unsigned ret = 0;
    if (ctx->state.lineskip) {
        unsigned skip = ctx->state.lineskip;
        ret = file_skip_lines(fp, skip);
        check(ret == skip, ERR_FAIL, WLCSV, "unexpected EOF or error");
    }
    ctx->state.col = 0;
    ctx->state.row = 0;
    buf_size      += 0x100;
    buffer         = calloc(1, buf_size);
    check(buffer, ERR_MEM, WLCSV);

    size_t read  = fread(buffer, 1, buf_size, fp);
    check(read, ERR_FAIL, WLCSV, "no data to read");
    size_t parsed = 0;
    while (read == buf_size) {
        void *ptr = buffer;
        buf_size *= 2;
        buffer = realloc(ptr, buf_size);
        check(buffer, ERR_MEM, WLCSV);
        read += fread((uint8_t *)buffer + (buf_size / 2), 1, buf_size, fp);
    }
    if (feof(fp)) {
        parsed = csv_parse(&ctx->parser,
                    buffer, read, callbacks_forward,
                    callbacks_eor, ctx);
        check(parsed == read, ERR_EXTERN, "libcsv",
            csv_strerror(csv_error(&ctx->parser)));
        csv_fini(&ctx->parser, callbacks_forward, callbacks_eor, ctx);
    } else
        log_err(ERR_FAIL, WLCSV, "an error occured during read operation");

    fclose(fp);
    free(buffer);
    return parsed;
error:
    if (fp)
        fclose(fp);
    if (buffer)
        free(buffer);
    return -1;
}

int
wlcsv_callbacks_active(wlcsv_ctx_st *ctx, uint8_t i)
{
    if (ctx)
        if (ctx->callbacks.tbl[i])
            return ctx->callbacks.tbl_idx_skip[i] ? 1 : 0;
        else
            log_err(ERR_FAIL, WLCSV, "retrieving active state: not found in callback table.");
    else
        log_warn(ERR_NALLOW, WLCSV, "NULL ctx parameter");
    return -1;
}

int
wlcsv_callbacks_clear(wlcsv_ctx_st *ctx, uint8_t i)
{
    if (ctx) {
        wlcsv_callback_entry_st **tbl = ctx->callbacks.tbl;
        if (tbl[i]) {
            if (ctx->callbacks.tbl_idx_skip[i])
                callbacks_toggle(ctx, i);
            if (tbl[i]->match_by == REGEX)
                pcre_free(tbl[i]->rgx);
            else if (tbl[i]->match_by == KEYWORD)
                free(tbl[i]->key);
            free(tbl[i]);
            tbl[i] = NULL;
            return 1;
        } else
            log_err(ERR_FAIL, WLCSV, "deleting element: not found in callback table.");
    } else
        log_warn(ERR_NALLOW, WLCSV, "NULL ctx parameter");
    return 0;
}

void
wlcsv_callbacks_clear_all(wlcsv_ctx_st *ctx)
{
    if (ctx)
        callbacks_clear_all(ctx, false);
    else
        log_warn(ERR_NALLOW, WLCSV, "NULL parameter");
}

void
wlcsv_callbacks_default_set(struct wlcsv_ctx *ctx,
        wlcsv_callback_ft *callback, void *data)
{
    if (ctx) {
        ctx->callbacks.tbl[DEFAULT_CALLBACK_IDX]->function = callback;
        ctx->callbacks.tbl[DEFAULT_CALLBACK_IDX]->data = data;
    } else
        log_warn(ERR_FAIL, WLCSV, "setting default callback: NULL 'ctx' parameter");
}

void
wlcsv_callbacks_eor_set(struct wlcsv_ctx *ctx,
    wlcsv_eor_callback_ft *eor_callback)
{
    if (ctx)
        ctx->callbacks.eor_callback = eor_callback;
    else
        log_warn(ERR_FAIL, WLCSV, "setting end-of-row callback: NULL 'ctx' parameter");
}

uint8_t
wlcsv_callbacks_set(struct wlcsv_ctx *ctx,
        wlcsv_callback_match_by_et match_by,
        wlcsv_callback_match_to_ut *match_to,
        wlcsv_callback_ft *callback,
        void *callback_data,
        unsigned once)
{
    check(ctx, ERR_NALLOW, WLCSV, "NULL parameter");

    wlcsv_callback_entry_st *entry = calloc(1, sizeof(wlcsv_callback_entry_st));
    check(entry, ERR_MEM, WLCSV);

    entry->function = callback;
    entry->data     = callback_data;
    entry->match_by = match_by;
    entry->once     = once;

    if (match_by == COLUMN || match_by == ROW)
        entry->row_or_col = match_to->row_or_col;
    else if (match_by == KEYWORD) {
        size_t len = strlen(match_to->key_or_rgx);
        entry->key = calloc(len + 1, sizeof(char));
        check(entry->key, ERR_MEM, WLCSV);
        memcpy(entry->key, match_to->key_or_rgx, len);
    } else {
        const char *error_msg;
        int error_offset;
        entry->rgx = pcre_compile(match_to->key_or_rgx, 0, &error_msg, &error_offset, 0);
        check(entry->rgx, ERR_FAIL, WLCSV, "compiling regex");
    }
    return callbacks_enlist(ctx, entry);
error:
    return UINT8_MAX;
}

int
wlcsv_callbacks_toggle(wlcsv_ctx_st *ctx, uint8_t id)
{
    if (ctx)
        if (ctx->callbacks.tbl[id])
            callbacks_toggle(ctx, id);
        else
            log_err(ERR_FAIL, WLCSV, "toggling: not found in callback table.");
    else
        log_warn(ERR_NALLOW, WLCSV, "NULL ctx parameter");
    return -1;
}

int
wlcsv_ignore_regex_set(struct wlcsv_ctx *ctx, char *regex)
{
    if (!ctx) {
        log_err(ERR_NALLOW, WLCSV, "NULL parameter");
        return 0;
    }
    if (ctx->ignore_regex)
        pcre_free(ctx->ignore_regex);
    ctx->ignore_regex = 0;
    if (regex) {
        const char *error_msg;
        int error_offset;
        ctx->ignore_regex = pcre_compile(regex, 0, &error_msg, &error_offset, 0);
        check(ctx->ignore_regex, ERR_EXTERN_AT, "PCRE", error_msg, error_offset);
    }
    return 1;
error:
    return -1;
}

wlcsv_state_st *
wlcsv_state_get(wlcsv_ctx_st *ctx)
{
    return ctx ? &ctx->state : 0;
}

/*  INLINE FUNCTION INSTANTIATIONS */

extern inline void
wlcsv_state_lineskip_set(wlcsv_state_st *stt, unsigned skip);

extern inline void
wlcsv_state_options_set(wlcsv_state_st *stt, unsigned options);
