/*
**  FILE: 'lcsv_w.c' -implementation of the libcsv wrapper unit,
        as specified in 'lcsv_w.h'.
**  AUTHOR: jiikai@github.com
**  LICENSE: MIT. See '../LICENSE'
*/

/*
**  INCLUDES
*/

#include "lcsv_w.h"
#include <ctype.h>
#include "dbg.h"

/*
**  MACRO DEFINITIONS
*/

/*  A simplified (no type checking) Linux kernel container_of macro
    to access a struct from a pointer to its member.
*/
#define CONTAINER_OF(ptr, type, member)\
    ((type *)((char *)(ptr) - offsetof(type, member)))

/*  Macro for setting PCRE option bits. */
#define PCRE_OPTS(opt)\
    int utf8_support;\
    pcre_config(PCRE_CONFIG_UTF8, &utf8_support);\
    if (utf8_support) {\
        opt =| (PCRE_UTF8 | PCRE_NEWLINE_ANY);\
    } else {\
        opt =| PCRE_NEWLINE_ANYCRLF;\
    }\
        opt =| PCRE_CASELESS

#define OPEN_FILE_AND_CHECK(fp, path, mode)\
    check(path, ERR_FAIL, LCSV_W, "no valid file path present");\
    fp = fopen(path, mode);\
    check(fp, ERR_FAIL, LCSV_W, "opening file")

/*  For setting and deleting callbacks by column or row number. Used
    in externally visible functions lcsv_w_un/set_callback_by_row() and
    lcsv_w_un/set_callback_by_column() to reduce redundancy.
*/
#define HT_ADD_BY_NUMERIC_KEY(hh, ht, key_name, key_val, cb_name, cb_val)\
    do {\
        ht_callbacks_st *entry = NULL;\
        if (HASH_CNT(hh, ht)) {\
            HASH_FIND(hh, ht, &key_val, sizeof(uint32_t), entry);\
        }\
        if (!entry) {\
            ht_callbacks_st *new_entry = calloc(1, sizeof(ht_callbacks_st));\
            check(new_entry, ERR_MEM, LCSV_W);\
            entry = new_entry;\
            entry->key_name = key_val;\
            HASH_ADD(hh, ht, key_name, sizeof(uint32_t), entry);\
        }\
        entry->cb_name = cb_val;\
    } while (0)

#define HT_DELETE_BY_NUMERIC_KEY(hh, ht, key)\
    do {\
        if (HASH_CNT(hh, ht)) {\
            ht_callbacks_st *entry = NULL;\
            HASH_FIND(hh, ht, &key, sizeof(uint32_t), entry);\
            if (entry) {\
                HASH_DELETE(hh, ht, entry);\
                free(entry);\
            }\
        }\
    } while (0)

#define HT_DELETE_ALL(hh, ht, free_key, key_name)\
    do {\
        if (ht) {\
            ht_callbacks_st *current, *tmp;\
            HASH_ITER(hh, ht, current, tmp) {\
                HASH_DELETE(hh, ht, current); /*  Remove from hash table. */\
                if (free_key) { /*  Free internal copy of keyword. */\
                    free(current->key_name);\
                }\
                /*  Free the structure. */\
                free(current);\
            }\
        }\
    } while (0)

/*
**  STRUCTURE/TYPE DEFINITIONS
*/

/*  Definition of a hash table entry structure,
    required by uthash.h hash table macros. */

typedef struct ht_match2function {
    union {
        char               *key;
        uint32_t            row;
        uint32_t            col;
    };
    union {
        UT_hash_handle      hh_k;
        UT_hash_handle      hh_r;
        UT_hash_handle      hh_c;
    };
    lcsv_w_callback_ft     *cb_function;
} ht_callbacks_st;

/*  Definition of libcsv wrapper context structure declared in header 'lcsv_w.h'.

    Structure members:
    - parser: the csv parser structure provided by libcsv in "csv.h".
    - path, path_len: path to the csv file to be parsed next and its length.
    - ignore_regex: PCRE regular expression, on match the field is ignored.
    - h_[krc]callbacks: hash tables of callbacks on column, row, or regex match.
    - default_callback: a default "fallback callback", can be NULL.
    - eor_callback: an optional callback called after the end of every row.
    - callback_data: user data, cast to void *, that is passed to callbacks.
    - row, col: current row/column being processed.
    - offset: user set number of rows to skip from the start of the csv file.
    - options: (can be bitwise OR'ed together)
        BIT     OPTION
        0       ignore_empty_fields;
        1       keywords_case_insensitive;
        2-7     UNDEFINED
*/

struct lcsv_w_ctx {
    struct csv_parser       parser;
    char                   *path;
    size_t                  path_len;
    pcre                   *ignore_regex;
    ht_callbacks_st        *h_kcallbacks;
    ht_callbacks_st        *h_rcallbacks;
    ht_callbacks_st        *h_ccallbacks;
    lcsv_w_callback_ft     *default_callback;
    lcsv_w_eor_callback_ft *eor_callback;
    void                   *callback_data;
    uint32_t                row;
    uint32_t                col;
    uint32_t                offset;
    uint8_t                 options;

};

/*
**  FUNCTION DEFINITIONS
*/

/*  STATIC */

static inline int
inl_advance_to_offset(FILE *fp, int line_offset)
{
    char buffer[0xFFF];
    int i = 0;
    while (i < line_offset && fgets(buffer, 0xFFE, fp)) {
        i++;
    }
    return i;
}

static void
callback_forwarder(void *field, size_t len, void *data)
{
    struct lcsv_w_ctx *ctx = (struct lcsv_w_ctx *)data;
    if (field && len) {
        char *str = (char *)field;
        uint32_t nrow = ctx->row;
        uint32_t ncol = ctx->col;
        ht_callbacks_st *match = NULL;
        int ret;
        if (ctx->ignore_regex) {
            int ovec[OVECCOUNT];
            /* Check if this field should be ignored. */
            ret = pcre_exec(ctx->ignore_regex, NULL,
                str, len, 0, 0, ovec, OVECCOUNT);
            if (ret >= 0) {
                ctx->col++;
                return;
            }
            if (ret != PCRE_ERROR_NOMATCH) {
                log_err(ERR_EXTERN_AT, "PCRE", "matching error", ret);
            }
        }
        if (ctx->h_kcallbacks) {
            /* Check for a keyword match. */
            HASH_FIND(hh_k, ctx->h_kcallbacks, str, len, match);
        }
        if (match) {
            match->cb_function(str, len, ctx->callback_data);
        } else {
            if (ctx->h_rcallbacks) {
                /* If no match, check for a row match. */
                HASH_FIND(hh_r, ctx->h_rcallbacks,
                    &nrow, sizeof(uint32_t), match);
            }
            if (match) {
                match->cb_function(str, len, ctx->callback_data);
            } else {
                if (ctx->h_ccallbacks) {
                    /* If no match, check for a column match. */
                    HASH_FIND(hh_c, ctx->h_ccallbacks,
                        &ncol, sizeof(uint32_t), match);
                }
                if (match) {
                    match->cb_function(str, len, ctx->callback_data);
                } else if (ctx->default_callback) {
                    /* If no match, run default callback if defined. */
                    ctx->default_callback(str, len, ctx->callback_data);
                }
            }
        }
    }
    ctx->col++;
}

static void
callback_row(int terminator, void *init_ptr)
{
    struct csv_parser *parser = (struct csv_parser *)init_ptr;
    /* Use CONTAINER_OF to access the structure the parser is wrapped in. */
    struct lcsv_w_ctx *ctx = CONTAINER_OF(parser, struct lcsv_w_ctx, parser);
    if (ctx->eor_callback != NULL) {
        ctx->eor_callback(ctx->callback_data);
    }
    ctx->col = 0;
    ctx->row++;
}

static void
callback_row_preview(int terminator, void *init_ptr)
{
    struct csv_parser *parser = (struct csv_parser *)init_ptr;
    /* Use CONTAINER_OF to access the structure the parser is wrapped in. */
    struct lcsv_w_ctx *ctx = CONTAINER_OF(parser, struct lcsv_w_ctx, parser);
    ctx->col = 0;
    ctx->row++;
}

/*  IMPLEMENTATIONS FOR FUNCTION PROTOTYPES IN: 'lcsv_w.h' */

lcsv_w_ctx_st *
lcsv_w_init(char *ignore_rgx, lcsv_w_callback_ft *default_callback,
    uint32_t offset, uint8_t options)
{
    struct lcsv_w_ctx *ctx = malloc(sizeof(struct lcsv_w_ctx));
    check(ctx, ERR_MEM, LCSV_W);
    memset(ctx, 0, sizeof(struct lcsv_w_ctx));
    ctx->path = NULL;
    ctx->offset = offset;
    const char *pcre_error_msg;
    int pcre_error_offset;
    if (ignore_rgx) {
        pcre *regex = pcre_compile(ignore_rgx,
            0, &pcre_error_msg, &pcre_error_offset, NULL);
        check(regex, ERR_EXTERN_AT, "PCRE", pcre_error_msg, pcre_error_offset);
        ctx->ignore_regex = regex;
    } else {
        ctx->ignore_regex = NULL;
    }
    ht_callbacks_st *h_kcallbacks = NULL, *h_ccallbacks = NULL, *h_rcallbacks = NULL;
    ctx->h_kcallbacks = h_kcallbacks;
    ctx->h_ccallbacks = h_ccallbacks;
    ctx->h_rcallbacks = h_rcallbacks;
    ctx->default_callback = default_callback;
    ctx->options = options;
    uint8_t libcsv_options = options & 1
                                ? CSV_APPEND_NULL | CSV_EMPTY_IS_NULL
                                : CSV_APPEND_NULL;
    int ret = csv_init(&ctx->parser, libcsv_options);
    check(!ret, ERR_FAIL, LCSV_W, "initializing csv_parser");
    return ctx;
error:
    return NULL;
}

/*  Definition of libcsv wrapper context structure declared in header 'lcsv_w.h'.

    Structure members:
    - parser: the csv parser structure provided by libcsv in "csv.h".
    - path, path_len: path to the csv file to be parsed next and its length.
    - ignore_regex: PCRE regular expression, on match the field is ignored.
    - h_[krc]callbacks: hash tables of callbacks on column, row, or regex match.
    - default_callback: a default "fallback callback", can be NULL.
    - eor_callback: an optional callback called after the end of every row.
    - callback_data: user data, cast to void *, that is passed to callbacks.
    - row, col: current row/column being processed.
    - offset: user set number of rows to skip from the start of the csv file.
    - options: (can be bitwise OR'ed together)
        BIT     OPTION
        0       ignore_empty_fields;
        1       keywords_case_insensitive;
        2-7     UNDEFINED
*/

void
lcsv_w_free(struct lcsv_w_ctx *ctx)
{
    if (ctx) {
        if (ctx->path) {
            free(ctx->path);
        }
        if (&ctx->parser) {
            csv_free(&ctx->parser);
        }
        if (ctx->ignore_regex) {
            pcre_free(ctx->ignore_regex);
        }
        if (ctx->h_kcallbacks) {
            ht_callbacks_st *current, *tmp;
            HASH_ITER(hh_k, ctx->h_kcallbacks, current, tmp) {
                HASH_DELETE(hh_k, ctx->h_kcallbacks, current); /* remove from hash table */
                free(current->key); /* free the internal copy of keyword */
                free(current); /* free the struct itself */
            }
        }
        if (ctx->h_rcallbacks) {
            ht_callbacks_st *current, *tmp;
            HASH_ITER(hh_r, ctx->h_rcallbacks, current, tmp) {
                HASH_DELETE(hh_r, ctx->h_rcallbacks, current); /* remove from hash table */
                free(current); /* free the struct itself */
            }
        }
        if (ctx->h_ccallbacks) {
            ht_callbacks_st *current, *tmp;
            HASH_ITER(hh_c, ctx->h_ccallbacks, current, tmp) {
                HASH_DELETE(hh_c, ctx->h_ccallbacks, current); /* remove from hash table */
                free(current); /* free the struct itself */
            }
        }
        free(ctx);
    }
}

int
lcsv_w_read(struct lcsv_w_ctx *ctx, size_t buf_size)
{
    FILE *fp = NULL;
    OPEN_FILE_AND_CHECK(fp, ctx->path, "r");
    rewind(fp);
    uint32_t ret = 0;
    if (ctx->offset) {
        ret = inl_advance_to_offset(fp, ctx->offset);
    }
    check(ret == ctx->offset, ERR_FAIL, LCSV_W, "unexpected EOF or error");
    ctx->col = 0;
    ctx->row = 0;
    buf_size += 0x100;
    void *buffer = malloc(buf_size);
    check(buffer, ERR_MEM, LCSV_W);
    size_t read = fread(buffer, 1, buf_size, fp);
    check(read, ERR_FAIL, LCSV_W, "no data to read");
    size_t parsed = 0;
    while (read == buf_size) {
        void *ptr = buffer;
        buf_size *= 2;
        buffer = realloc(ptr, buf_size);
        check(buffer, ERR_MEM, LCSV_W);
        read += fread((uint8_t *)buffer + (buf_size / 2), 1, buf_size, fp);
    }
    if (feof(fp)) {
        parsed = csv_parse(&ctx->parser,
                    buffer, read,
                    callback_forwarder,
                    callback_row,
                    ctx);
        check(parsed == read, ERR_EXTERN, "libcsv",
            csv_strerror(csv_error(&ctx->parser)));
        csv_fini(&ctx->parser, callback_forwarder, callback_row, ctx);
    } else {
        log_err(ERR_FAIL, LCSV_W, "an error occured during read operation");
    }
    fclose(fp);
    free(buffer);
    return parsed;
error:
    if (fp) fclose(fp);
    if (buffer) free(buffer);
    return 0;
}

int
lcsv_w_preview(struct lcsv_w_ctx *ctx, uint32_t nrows, size_t buf_size,
    lcsv_w_callback_ft *callback)
{
    FILE *fp = NULL;
    OPEN_FILE_AND_CHECK(fp, ctx->path, "r");
    ctx->col = 0;
    ctx->row = 0;
    void *buffer = malloc(buf_size);
    check(buffer, ERR_MEM, LCSV_W);
    int ret = 0;
    uint64_t i = 0;
    uint32_t current_row = 0;
    while (current_row < nrows) {
        if (i == buf_size && !feof(fp)) {
            void *ptr = buffer;
            buf_size *= 2;
            buffer = realloc(ptr, buf_size);
            check(buffer, ERR_MEM, LCSV_W);
        }
        ret = fread((uint8_t *)buffer + i, sizeof(char), 1, fp);
        if (!ret) {
            check(feof(fp), ERR_FAIL, LCSV_W,
                "an error occured reading the preview portion of file.");
            log_info("End of file was reached after reading %lu bytes of data.",
                i);
            return 1;
        }
        if (*((char *)((uint8_t *)buffer + i)) == '\n') {
            current_row++;
        }
        i++;
    }
    fclose(fp);
    size_t parsed = csv_parse(&ctx->parser,
                        buffer, i, callback,
                        callback_row_preview,
                        ctx->callback_data);
    check(parsed == i, ERR_EXTERN, "libcsv",
        csv_strerror(csv_error(&ctx->parser)));
    csv_fini(&ctx->parser, callback, callback_row, ctx->callback_data);
    free(buffer);
    return parsed;
error:
    if (fp) fclose(fp);
    if (buffer) free(buffer);
    return 0;
}

uint8_t
lcsv_w_set_target_path(struct lcsv_w_ctx *ctx, char* path, size_t path_len)
{
    check(ctx, ERR_NALLOW, LCSV_W, "NULL ctx parameter");
    check(path, ERR_NALLOW, LCSV_W, "NULL path parameter");
    check(path_len, ERR_NALLOW, LCSV_W, "path_len parameter == 0");
    char *path_buffer;
    if (ctx->path) {
        path_buffer = ctx->path;
        size_t old_path_len = ctx->path_len;
        if (old_path_len > path_len) {
            /*  Use realloc() when scaling the buffer down, seeing as there is
                no chance of malloc() deciding to allocate a new memory area,
                which is a slower process than shrinking in-place.
            */
            path_buffer = realloc(ctx->path, path_len + 1);

        } else {
            /*  Otherwise, do a free() & calloc(). Preserving
                the old path data is not required.
            */
            free(ctx->path);
            path_buffer = calloc(path_len + 1, sizeof(char));
        }
    } else {
        /*  This was the first path provided. */
        path_buffer = calloc(path_len + 1, sizeof(char));
    }
    check(path_buffer, ERR_MEM, LCSV_W);
    memcpy(path_buffer, path, path_len);
    ctx->path = path_buffer;
    return 1;
error:
    return 0;
}

void
lcsv_w_set_callback_data(struct lcsv_w_ctx *ctx, void *data)
{
    if (ctx) {
        ctx->callback_data = data;
    } else {
        log_warn(ERR_FAIL, LCSV_W,
            "setting callback_data: NULL 'ctx' parameter");
    }
}

uint8_t
lcsv_w_set_callback_by_regex_match(struct lcsv_w_ctx *ctx, char *match,
    size_t len, lcsv_w_callback_ft *callback)
{
    check(match, ERR_NALLOW, LCSV_W, "NULL 'match' parameter");
    check(callback, ERR_NALLOW, LCSV_W, "NULL 'callback' parameter");
    ht_callbacks_st *entry = NULL;
    if (HASH_CNT(hh_k, ctx->h_kcallbacks)) {
        HASH_FIND(hh_k, ctx->h_kcallbacks, &match, len, entry);
    }
    if (!entry) {
        entry = calloc(1, sizeof(ht_callbacks_st));
        check(entry, ERR_MEM, LCSV_W);
        entry->key = calloc(len + 1, sizeof(char));
        check(entry->key, ERR_MEM, LCSV_W);
        memcpy(entry->key, match, len);
        HASH_ADD_KEYPTR(hh_k, ctx->h_kcallbacks, entry->key, len, entry);
    }
    entry->cb_function = callback;
    return 1;
error:
    return 0;
}

uint8_t
lcsv_w_set_callback_by_row(struct lcsv_w_ctx *ctx, uint32_t nrow,
    lcsv_w_callback_ft *callback)
{
    check(ctx, ERR_NALLOW, LCSV_W, "NULL 'ctx' parameter");
    check(callback, ERR_NALLOW, LCSV_W, "NULL 'callback' parameter");
    HT_ADD_BY_NUMERIC_KEY(hh_r, ctx->h_rcallbacks,
        row, nrow, cb_function, callback);
    return 1;
error:
    return 0;
}

uint8_t
lcsv_w_set_callback_by_column(struct lcsv_w_ctx *ctx,
    uint32_t ncol, lcsv_w_callback_ft *callback)
{
    check(ctx, ERR_NALLOW, LCSV_W, "NULL 'ctx' parameter");
    check(callback, ERR_NALLOW, LCSV_W, "NULL 'callback' parameter");
    HT_ADD_BY_NUMERIC_KEY(hh_c, ctx->h_ccallbacks,
        col, ncol, cb_function, callback);
    return 1;
error:
    return 0;
}

void
lcsv_w_unset_callback_by_regex_match(struct lcsv_w_ctx *ctx,
    char *match, size_t len)
{
    if (ctx && match && len && HASH_CNT(hh_k, ctx->h_kcallbacks)) {
        ht_callbacks_st *entry = NULL;
        HASH_FIND(hh_k, ctx->h_kcallbacks, match, len, entry);
        if (entry) {
            HASH_DELETE(hh_k, ctx->h_kcallbacks, entry);
            free(entry->key);
            free(entry);
        }
    } else {
        log_warn(ERR_FAIL, LCSV_W,
            "unsetting callback: 1 or more NULL parameters");
    }
}

void
lcsv_w_unset_callback_by_row(struct lcsv_w_ctx *ctx, uint32_t nrow)
{
    if (ctx) {
        HT_DELETE_BY_NUMERIC_KEY(hh_r, ctx->h_rcallbacks, nrow);
    } else {
        log_warn(ERR_FAIL, LCSV_W, "unsetting callback: NULL 'ctx' parameter");
    }
}


void
lcsv_w_unset_callback_by_column(struct lcsv_w_ctx *ctx, uint32_t ncol)
{
    if (ctx) {
        HT_DELETE_BY_NUMERIC_KEY(hh_c, ctx->h_ccallbacks, ncol);
    } else {
        log_warn(ERR_FAIL, LCSV_W, "unsetting callback: NULL 'ctx' parameter");
    }
}

void
lcsv_w_set_default_callback(struct lcsv_w_ctx *ctx,
    lcsv_w_callback_ft *new_default)
{
    if (ctx) {
        ctx->default_callback = new_default;
    } else {
        log_warn(ERR_FAIL, LCSV_W,
            "setting default callback: NULL 'ctx' parameter");
    }
}

void
lcsv_w_set_end_of_row_callback(struct lcsv_w_ctx *ctx,
    lcsv_w_eor_callback_ft *new_eor_function)
{
    if (ctx) {
        ctx->eor_callback = new_eor_function;
    } else {
        log_warn(ERR_FAIL, LCSV_W,
            "setting end-of-row callback: NULL 'ctx' parameter");
    }
}

void lcsv_w_set_offset(struct lcsv_w_ctx *ctx, uint32_t offset)
{
    if (ctx) {
        ctx->offset = offset;
    } else {
        log_warn(ERR_FAIL, LCSV_W, "setting offset: NULL 'ctx' parameter");
    }
}

void lcsv_w_set_option(struct lcsv_w_ctx *ctx, uint8_t option)
{
    if (ctx) {
        ctx->options |= option;
    } else {
        log_err(ERR_FAIL, LCSV_W, "setting options: NULL 'ctx' parameter");
    }
}

void lcsv_w_unset_option(struct lcsv_w_ctx *ctx, uint8_t option)
{
    if (ctx) {
        ctx->options ^= option;
    } else {
        log_err(ERR_FAIL, LCSV_W, "unsetting options: NULL 'ctx' parameter");
    }
}

uint8_t lcsv_w_set_ignore_regex(struct lcsv_w_ctx *ctx, char *regex)
{
    check(ctx, ERR_FAIL, LCSV_W, "setting ignore regex: NULL 'ctx' parameter");
    if (regex) {
        const char *pcre_error_msg;
        int pcre_error_offset;
        pcre *rgx = pcre_compile(regex, 0,
            &pcre_error_msg, &pcre_error_offset, NULL);
        check(rgx, ERR_EXTERN_AT, "PCRE", pcre_error_msg, pcre_error_offset);
        ctx->ignore_regex = rgx;
    } else {
        ctx->ignore_regex = NULL;
    }
    return 1;
error:
    return 0;
}

int
lcsv_w_get_row(struct lcsv_w_ctx *ctx)
{
    if (ctx) {
        return ctx->row;
    }
    log_err(ERR_FAIL, LCSV_W, "retrieving row count: NULL 'ctx' parameter");
    return -1;
}

int
lcsv_w_get_col(struct lcsv_w_ctx *ctx)
{
    if (ctx) {
        return ctx->col;
    }
    log_err(ERR_FAIL, LCSV_W, "retrieving column count: NULL 'ctx' parameter");
    return -1;
}

void
lcsv_w_set_options(lcsv_w_ctx_st *ctx, uint8_t options)
{
    if (ctx) {
        ctx->options |= options;
    }
}

void
lcsv_w_unset_options(lcsv_w_ctx_st *ctx, uint8_t options)
{
    if (ctx) {
        ctx->options ^= options;
    }
}

void
lcsv_w_reset_callbacks(lcsv_w_ctx_st *ctx, uint8_t by_row,
    uint8_t by_col, uint8_t by_keyword)
{
    if (ctx) {
        if (by_row) {
            HT_DELETE_ALL(hh_r, ctx->h_rcallbacks, 0, key);
        }
        if (by_col) {
            HT_DELETE_ALL(hh_c, ctx->h_ccallbacks, 0, key);
        }
        if (by_keyword) {
            HT_DELETE_ALL(hh_k, ctx->h_kcallbacks, 1, key);
        }
    }
}
