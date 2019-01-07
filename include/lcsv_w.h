#ifndef _lcsv_w_
#define _lcsv_w_

#define _XOPEN_SOURCE 700

/*
**  INCLUDES
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pcre.h>
#include "csv.h"
#include "uthash.h"

/*
**  MACRO DEFINITIONS
*/

/*  MACRO CONSTANTS */

/*  Error message provider name. */
#define LCSV_W "LCSV Wrapper"

/*  Default size (4 KB) in bytes for the temporary data buffer. */
#define LCSV_W_DEFAULT_TEMP_SIZE 0xFFF

/*  PCRE substring buffer default size in bytes (255). */
#define OVECCOUNT 0xFF


/*
**  TYPE DEFINITIONS/DECLARATIONS
*/

typedef void (lcsv_w_callback_ft)(void *, size_t, void *);
typedef void (lcsv_w_eor_callback_ft)(void *);

typedef struct lcsv_w_ctx lcsv_w_ctx_st;

/*  Callback selection goes as follows:
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

/*
**  FUNCTION PROTOYPES
*/

/*  lcsv_w_init() - initializes the libcsv wrapper struct.
    PARAMETERS:
    - init_at: initalize at the (preallocated) address pointed to. If NULL, this function
      will allocate the necessary memory.
    - ignore_rgx: regular expression with PCRE syntax, a match causes a field to be ignored.
    - default_callback: a function to call if no other condition holds for a field.
      Can be NULL/set later.
    - offset: offset in lines from the beginning of the file that will be skipped over.
    - options: [DOCUMENTATION HERE]
    RETURN VALUE: the initialized struct or NULL in case an error occured or invalid parameters were passed.
*/
lcsv_w_ctx_st *
lcsv_w_init(char *ignore_rgx, lcsv_w_callback_ft default_callback,
    uint32_t offset, uint8_t options);

/*  lcsv_w_free() - frees the wrapper struct and all memory that was allocated by the wrapper.
    PARAMETERS: an instance of the wrapper structure lcsv_w that was previously initialized.
*/
void
lcsv_w_free(lcsv_w_ctx_st *ctx);

/*  lcsv_w_read() - processes a csv file, the path to which was set via lcsv_w_set_target_path().
    PARAMETERS: an initialized lcsv_w wrapper structure (ctx) and size of the data (buf_size).
    RETURN VALUE: 1 if the file was succesfully processed, 0 on any error.
*/
int
lcsv_w_read(lcsv_w_ctx_st *ctx, size_t buf_size);

/*  lcsv_w_preview() - sends the first nrows from the csv file set via lcsv_w_set_target_path() to
    the callback function pointed to by parameter callback. Can be used for e.g. determining a
    desired offset, header field names and other structural features.
    PARAMETERS:
    - ctx: an initialized lcsv_w wrapper structure.
    - nrows: the number of rows to preview.
    - buf_size: an expected size of the data in memory.
    - callback: pointer to a function. Contrary to callbacks used when processing files with
      lcsv_w_read(), this callback is passed directly to the underlying libcsv parser. As such ignore_regex and other filtering rules have no effect.
    RETURN VALUE: number of parsed bytes on success, 0 on any error.
*/
int
lcsv_w_preview(lcsv_w_ctx_st *ctx, uint32_t nrows, size_t buf_size,
    lcsv_w_callback_ft *callback);

/*  lcsv_w_set_target_path() - set a new target file.
    PARAMETERS:
    - ctx: an initialized lcsv_w wrapper structure.
    - path, len: path to the target file and its length.
    RETURN VALUE: 1 on success, 0 on failure or if any of the parameters were NULL/0.
*/
uint8_t
lcsv_w_set_target_path(lcsv_w_ctx_st *ctx, char* path, size_t len);

/*  [DOCUMENT HERE] */
void
lcsv_w_set_callback_data(lcsv_w_ctx_st *ctx, void *data);

/*  lcsv_w_(un)set_callback_by_column(), lcsv_w_(un)set_callback_by_row()
    - associate a callback function with a column/row number or remove a previously set association.
    PARAMETERS:
    - ctx: an initialized lcsv wrapper structure.
    - ncol/nrow: the column or row number.
    - [only for set-functions] callback: function pointer to the callback.
    [only for set-functions] RETURN VALUE: 1 on success, 0 on failure.
*/
uint8_t
lcsv_w_set_callback_by_column(lcsv_w_ctx_st *ctx, uint32_t ncol, lcsv_w_callback_ft *callback);
uint8_t
lcsv_w_set_callback_by_row(lcsv_w_ctx_st *ctx, uint32_t nrow, lcsv_w_callback_ft *callback);
void
lcsv_w_unset_callback_by_column(lcsv_w_ctx_st *ctx, uint32_t ncol);
void
lcsv_w_unset_callback_by_row(lcsv_w_ctx_st *ctx, uint32_t nrow);

/*  lcsv_w_(un)set_callback_by_regex_match()
    - associate a callback function with a keyword or remove a previously set association.
    PARAMETERS:
    - ctx: an initialized lcsv wrapper structure.
    - match: the keyword string. Will be copied to internal memory; the user needn't keep it around.
    - len: length of the above string.
    - [only for set-functions] callback: function pointer to the callback.
    [only for set-functions] RETURN VALUE: 1 on success, 0 on failure.
*/
uint8_t
lcsv_w_set_callback_by_regex_match(lcsv_w_ctx_st *ctx, char *match, size_t len,
    lcsv_w_callback_ft callback);
void
lcsv_w_unset_callback_by_regex_match(lcsv_w_ctx_st *ctx, char *match, size_t len);

/*  lcsv_set_default_callback() - set a new default callback function.
    PARAMETERS:
    - ctx: an initialized lcsv wrapper structure.
    - new_default: a function pointer to the new default callback function. Can be NULL, meaning
    that no function will be called if no keyword/row/column matches exist for the current field.
*/
void
lcsv_w_set_default_callback(lcsv_w_ctx_st *ctx, lcsv_w_callback_ft *new_default);

/*  lcsv_set_end_of_row_callback() - set a new end-of-row callback function. Not called in preview.
    PARAMETERS:
    - ctx: an initialized lcsv wrapper structure.
    - new_eor_function: a function pointer to the new end-of-row callback function. Can be NULL,
      meaning that no user-specified function will be called by the end of current row. */
void
lcsv_w_set_end_of_row_callback(lcsv_w_ctx_st *ctx, lcsv_w_eor_callback_ft *new_eor_function);

/* [DOCUMENT HERE] */
void
lcsv_w_set_offset(lcsv_w_ctx_st *ctx, uint32_t offset);

void
lcsv_w_set_options(lcsv_w_ctx_st *ctx, uint8_t options);

void
lcsv_w_unset_options(lcsv_w_ctx_st *ctx, uint8_t options);

/* [DOCUMENT HERE] */
uint8_t
lcsv_w_set_ignore_regex(lcsv_w_ctx_st *ctx, char *regex);

/* [DOCUMENT HERE] */
int
lcsv_w_get_col(lcsv_w_ctx_st *ctx);

/* [DOCUMENT HERE] */
int
lcsv_w_get_row(lcsv_w_ctx_st *ctx);

void
lcsv_w_reset_callbacks(lcsv_w_ctx_st *ctx, uint8_t by_row,
    uint8_t by_col, uint8_t by_keyword);

#endif /* _lcsv_w_ */
