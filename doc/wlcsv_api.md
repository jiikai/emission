# ‪wlcsv.h - API documentation

## Synopsis

>__`#include "wlcsv.h"`__
> Compile and link with `-lpcre`.

This file documents __`wlcsv`__, a wrapper around the CSV parser `libcsv`.

It aims to ease the association of callback functions to column, row, keyword or regex match in any given field of csv formatted data.

It is a component of the [Emission API](emiss_api.md).


### License

(c) Joa Käis (github.com/jiikai) 2018-2019 under the [MIT license](../LICENSE.md).

-------------------------------------------------------------------------------

## Includes

`wlcsv.h` includes the following standard headers:

```c
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
```

Additionally, PCRE regex library header is included from a system path.
The header for the embedded `libcsv` source is included locally.

```c
#include <pcre.h>
#include "csv.h"
```

-------------------------------------------------------------------------------

## Macros

### Constants

- Defines the name of this interface (for use in error messages).
```c
#define WLCSV "wlcsv"
```

- Define the version number of this interface.
```c
#define WLCSV_VERSION_MAJOR 0
#define WLCSV_VERSION_MINOR 1
#define WLCSV_VERSION_PATCH 0
```

- Defines the number of match types.
>
- API currently supports matching by
    1. keyword (string equality in the `strcmp()` sense),
    2. a PCRE regex (a match returned by `pcre_exec()`),
    3. row number (= index), and
    4. column number (= index).

```c
#define WLCSV_NCALLBACK_MATCH_TYPES 4
```

- Option flag for ignoring empty fields completely when parsing.
```c
#define WLCSV_OPTION_IGNORE_EMPTY_FIELDS 1
```

- Maximun number of enlisted callbacks to match against, *including* the [default callback](#wlcsv_callbacks_default_set-) but *excluding* the [end-of-row callback](#wlcsv_callbacks_eor_set-).
>
    - Change at compile time via passing `DWLCSV_NCALLBACKS_MAX=<integer>` to the compiler.
    - Value - 1 should be divisible by the [number of callback types](#WLCSV_NCALLBACK_MATCH_TYPES).

```c
#ifndef WLCSV_NCALLBACKS_MAX
    #define WLCSV_NCALLBACKS_MAX 0x10
#elif ((WLCSV_NCALLBACKS_MAX - 1) % WLCSV_NCALLBACK_MATCH_TYPES)
    #define  WLCSV_NCALLBACKS_MAX 0x10
#endif
```

### Function-like macros

#### `WLCSV_STATE_MEMB_GET(stt, member)`

A `NULL`-safe, generic convenience "getter" for [the state structure](#wlcsv_state_st).

```c
#define WLCSV_STATE_MEMB_GET(stt, member) (stt ? stt->member : UINT_MAX)
```

- Substitute a state structure pointer for `stt` and the *name* of the struct member for `member`.
- Evaluates to `member`'s value or `UINT_MAX` if `stt` is a `NULL` pointer.

-------------------------------------------------------------------------------

## Types

### Structure types

#### `wlcsv_ctx_st`

An opaque handle for the main context structure.

```c
typedef struct wlcsv_ctx wlcsv_ctx_st
```

__See also:__ [`wlcsv_init()`](#wlcsv_init-)


#### `wlcsv_state_st`

A transparent structure with fields holding data regarding the current state of parsing.

```c
typedef struct wlcsv_state {
    int                     eor_terminator;
    unsigned                col;
    unsigned                row;
    unsigned                lineskip;
    unsigned                options;
} wlcsv_state_st;
```
|__Member__        |__Description__
|:-----------------|:----------------------------------------------------------
|`eor_terminator`  | The code of the last character on the previous row.
|`col`             | The index of the current column.
|`row`             | The index of the current row.
|`lineskip`        | The user-set offset in lines from the start of the csv file to ignore.
|`options`         | A bit mask of currently active wlcsv options.

- Access any of the member fields in a `NULL`-safe manner with the function-like macro expression [`WLCSV_STATE_MEMBER_GET()`](#WLCSV_STATE_MEMBER_GET-).
- Wrapped in [the main context structure](#wlcsv_state_st), which must be [initialized](#wlcsv_init-) first.
- `eor_terminator`, `col`, and `row` are updated by wlcsv during csv parsing. They are intended as read-only values; however, they hold no significance to wlcsv and are not relied upon during any internal operation.
- `lineskip` and `options`, are intended as modifiable entities and accordingly, [setters as inline functions](#wlcsv_state_lineskip_set-) are provided for them. Seeing as wlcsv_state is a transparent structure, there is of course nothing stopping one from directly manipulating the fields themselves. Use common sense.

__See also:__ [`wlcsv_state_get()`](#wlcsv_state_get-), [`wlcsv_init()`](#wlcsv_init-)


### Function types

#### `wlcsv_callback_ft`

A function type for user-provided callbacks that handle parsed csv field data.

```c
typedef void wlcsv_callback_ft(void *, size_t, void *);
```

Callback selection proceeds as follows:
1.  If the pointer to the current field is NULL (or the field is empty in case) ignore_empty_fields is set, return with no callback.
2.  If ignore_regex is not NULL, check field text for match. Return with no callback if so.
3.  If there are callbacks that are matched against a keyword, check if a key matching the field text is found. If so, select the associated callback function.
4.  If there are callbacks that are matched against the current row number, check if a row key (unsigned integer) matching the current row number exists. If so, select the associated function.
5. If there are callbacks that are matched against the current column number, check if a column key (unsigned integer) matching the current column numbe exists. If so, select the associated callback function.
6.  If a default callback has been set, call it and return.

__See also:__ [`wlcsv_callbacks_set`](#wlcsv_callbacks_set-)


#### `wlcsv_eor_callback_ft`

A function type for an optional callback invoked as the end of any row is reached.

```c
typedef void wlcsv_eor_callback_ft(void *);
```

__See also:__ [`wlcsv_callbacks_eor_set()`](#wlcsv_callbacks_eor_set-)


### Enum & Union types

#### `wlcsv_callback_match_by_et`

An enumeration for callback matching types: by keyword, regular expression, row or column match.

```c
typedef enum wlcsv_callback_match_by {
    KEYWORD, REGEX, ROW, COLUMN
} wlcsv_callback_match_by_et;
```

__See also:__ [`wlcsv_callback_match_to_ut()`](#wlcsv_callback_match_to_ut), [`wlcsv_callback_ft`](#wlcsv_callback_ft), [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-)

#### `wlcsv_callback_match_to_ut`

A union type for passing criteria to match a callback against.

```c
typedef union {
    unsigned                    row_or_col;
    char *                      key_or_rgx;
} wlcsv_callback_match_to_ut;
```
|__Member__    |__Description__
|:-------------|:--------------------------------------------------------------
|`row_or_col`  | For use with `wlcsv_callback_match_by_et` type `ROW` or `COLUMN`.
|`key_or_rgx`  | For use with `wlcsv_callback_match_by_et` type `KEYWORD` or `REGEX`.

__See also:__ [`wlcsv_callback_match_by_et()`](#wlcsv_callback_match_by_et), [`wlcsv_callback_ft`](#wlcsv_callback_ft), [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-)

-------------------------------------------------------------------------------

## Functions

#### `wlcsv_free()`

Frees the context structure and all memory that was allocated by it.

```c
void wlcsv_free(wlcsv_ctx_st *ctx);
```
|__Parameter__|__Description__
|:------------|:---------------------------------------------------------------
|`ctx`        | A previously initialized instance of the wlcsv context structure.

- This function will silently fail if `ctx` is a `NULL` pointer.

__See also:__ [`wlcsv_init()`](#wlcsv_init-)


#### `wlcsv_init()`

Initializes the context structure.

```c
wlcsv_ctx_st * wlcsv_init(char *ignore_rgx,)
        wlcsv_callback_ft default_callback,
        unsigned offset, unsigned options;
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ignore_rgx`      | A regular expression with PCRE syntax, a match causes a field to be ignored.
|`default_callback`| A function to call if no condition holds for a field.
|`offset`          | The offset in lines from the beginning of the file that will be skipped over.
|`options`         | TODO

- All of the parameters are optional, *i.e.* can be `0/NULL` (default) and set later if so desired.
- Row and column indexing starts from zero.

__Returns:__ The initialized struct or `NULL` on error.

__See also:__ [`wlcsv_free()`](#wlcsv_free-)


#### `wlcsv_file_path()`

Set a new target csv file path.

```c
unsigned wlcsv_file_path(wlcsv_ctx_st *ctx, const char *path, size_t len);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ctx`             | A previously initialized wlcsv context structure handle.
|`path`            | Path to the target file.
|`len`             | Length of the `path` string.

- Can be used for *e.g.* determining a desired line offset, header field names and other structural features of the csv file.
- Contrary to callbacks used when processing files with [`wlcsv_file_read()`](#wlcsv_file_read-), the function pointed at by `callback` gets called directly by the underlying [`libcsv`]() parser, as such filtering rules provided by this interface have no effect.

__Returns:__ `1` on success or `0` on any error.

__See also:__ [`wlcsv_file_read()`](#wlcsv_file_read-), [`wlcsv_file_preview()`](#wlcsv_file_preview-)


#### `wlcsv_file_preview()`

Sends the first *n* rows from the csv file to a callback.

```c
int wlcsv_preview(wlcsv_ctx_st *ctx,)
        unsigned nrows, size_t buf_size,
        wlcsv_callback_ft *callback;
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ctx`             | A previously initialized wlcsv context structure handle.
|`nrows`           | The number of rows to preview.
|`buf_size`        | Byte size of the data.
|`callback`        | A callback function pointer.

- Set the file path first via [`wlcsv_file_path()`](#wlcsv_file_path-).
- Can be used for *e.g.* determining a desired line offset, header field names and other structural features of the csv file.
- Contrary to callbacks used when processing files with [`wlcsv_file_read()`](wlcsv_file_read-), the function pointed at by `callback` gets called directly by the underlying [`libcsv`]() parser, as such filtering rules provided by this interface have no effect.

__Returns:__ `1` if the file was succesfully processed up to `nrows`, `0` on any error.

__See also:__ [`wlcsv_file_path()`](#wlcsv_file_read-), [`wlcsv_file_read()`](#wlcsv_file_preview-)

***
#### `wlcsv_file_read()`

Read a csv file to a buffer for parsing, sending data field by field to user set callbacks.

```c
int wlcsv_file_read(wlcsv_ctx_st *ctx, size_t buf_size);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ctx`             | A previously initialized instance of the wlcsv context structure.
|`buf_size`        | Byte size of the data.

- Set the file path first via [`wlcsv_file_path()`](#wlcsv_file_path-).

__Returns:__ `1` if the file was succesfully processed, `0` on any error.

__See also:__ [`wlcsv_file_path()`](#wlcsv_file_path-), [`wlcsv_file_preview()`](#wlcsv_file_preview-)


#### `wlcsv_callbacks_active()`

Check if a callback with a given ID is enabled or disabled.

```c
int wlcsv_callbacks_active(wlcsv_ctx_st *ctx, uint8_t id);
```
|__Parameter__ |__Description__
|:-------------|:--------------------------------------------------------------
|`ctx`         | A previously initialized wlcsv context structure handle.
|`id`          | ID of the callback, obtained from [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-).

__Returns:__ `1` if active, `0` if not and `-1` if no callback with ID `id` was found or `ctx == NULL`.
__See also:__ [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-), [`wlcsv_callbacks_toggle()`](#wlcsv_callbacks_toggle-)


#### `wlcsv_callbacks_clear()`

Delete a callback with a given ID.

```c
int wlcsv_callbacks_clear(wlcsv_ctx_st *ctx, uint8_t id);
```
|__Parameter__ |__Description__
|:-------------|:--------------------------------------------------------------
|`ctx`         | A previously initialized wlcsv context structure handle.
|`id`          | ID of the callback, obtained from [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-).

__Returns:__ `1` on success, `0` on `ctx == NULL` and `-1` if no callback with ID `id` was found.
__See also:__ [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-), [`wlcsv_callbacks_clear_all()`](#wlcsv_callbacks_clear_all-)


#### `wlcsv_callbacks_clear_all()`

Delete all callback associations.

```c
void wlcsv_callbacks_clear_all(wlcsv_ctx_st *ctx);
```
|__Parameter__ |__Description__
|:-------------|:--------------------------------------------------------------
|`ctx`         | A previously initialized wlcsv context structure handle.

- Will also delete the default and end-of-row callbacks.
- Fails silently if `ctx == NULL`.

__See also:__ [`wlcsv_callbacks_clear()`](#wlcsv_callbacks_clear-), [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-), [`wlcsv_callbacks_default_set()`](#wlcsv_callbacks_default_set-), [`wlcsv_callbacks_eor_set()`](#wlcsv_callbacks_eor_set-),


#### `wlcsv_callbacks_set()`

Associate a callback function with a regex, keyword, row or a column number.

```c
uint8_t wlcsv_callbacks_set(wlcsv_ctx_st *ctx,
    wlcsv_callback_match_by_et match_by,
    wlcsv_callback_match_to_ut *match_to,
    wlcsv_callback_ft *callback,
    void *callback_data,
    unsigned once);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ctx`             | A previously initialized wlcsv context structure handle.
|`match_by`        | An enum type denoting the property against which to match.
|`match_to`        | A union holding either an unsigned value or a `char *` pointer.
|`callback`        | If not NULL, call when encountering field with matching data.
|`callback_data`   | If not NULL, this pointer will be passed to the callback when invoked.
|`once`            | Whether this callback should be disabled after being matched.

__Returns:__ An id for this callback association on success or `UINT8_MAX` on any error.

__See also:__ [`wlcsv_callbacks_clear()`](#wlcsv_callbacks_clear-), [`wlcsv_callbacks_default_set()`](#wlcsv_callbacks_default_set-), [`wlcsv_callbacks_eor_set()`](#wlcsv_callbacks_eor_set-), [`wlcsv_callbacks_toggle()`](#wlcsv_callbacks_toggle-),


#### `wlcsv_callbacks_default_set()`

Set or unset the default callback function and/or the default callback data.

```c
void wlcsv_callbacks_default_set(wlcsv_ctx_st *ctx, wlcsv_callback_ft *callback, void *data);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ctx`             | A previously initialized wlcsv context structure handle.
|`callback`        | A function pointer to the new default callback function.
|`data`            | A pointer to the new default callback data, cast to `void *`.


- Fails with a warning if `ctx == NULL`.
- If `callback == NULL`, no function will be called if no matches exist for the current field.
- As with other user set callbacks, is ignored by [`wlcsv_file_preview()`](#wlcsv_file_preview-).


#### `wlcsv_callbacks_eor_set()`

Set or unset a new end-of-row callback function.

```c
void wlcsv_end_of_row_callback_set(wlcsv_ctx_st *ctx, wlcsv_eor_callback_ft *new_eor_function);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ctx`             | A previously initialized wlcsv context structure handle.
|`eor_callback`    | A function pointer to the new end-of-row callback function.

- Fails with a warning if `ctx == NULL`.
- If `new_eor_function == NULL`, no user function will be called by the end of a row.
- As with other user set callbacks, is ignored by [`wlcsv_file_preview()`](#wlcsv_file_preview-).


#### `wlcsv_callbacks_toggle()`

Activate a disabled or deactive an enabled callback.

```c
int wlcsv_callbacks_toggle(wlcsv_ctx_st *ctx, uint8_t id);
```
|__Parameter__ |__Description__
|:-------------|:--------------------------------------------------------------
|`ctx`         | A previously initialized wlcsv context structure handle.
|`id`          | ID of the callback, obtained from [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-).

__Returns:__  `1` on success, `0` on `ctx == NULL` and `-1` if no callback with ID `id` was found.
__See also:__ [`wlcsv_callbacks_active()`](#wlcsv_callbacks_active-), [`wlcsv_callbacks_set()`](#wlcsv_callbacks_set-)


#### `wlcsv_ignore_regex_set()`

Set or unset a PCRE regex that will cause any field that matches to be ignored.

```c
int wlcsv_ignore_regex_set(wlcsv_ctx_st *ctx, char *regex);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ctx`             | A previously initialized wlcsv context structure handle.
|`regex`           | A regular expression string with PCRE syntax.

- Has no effect in [`wlcsv_preview()`](#wlcsv_preview-).
- If `regex == NULL`, no regex matching will be performed to ignore a field.

__Returns:__  `1` on success, `0` on `ctx == NULL` and `-1` on a regex compile error.
__See also:__ [`wlcsv_read()`](#wlcsv_read-), [`wlcsv_preview()`](#wlcsv_preview-)


#### `wlcsv_state_get()`

Obtain a pointer to the wlcsv context state structure.

```c
wlcsv_state_st * wlcsv_state_get(wlcsv_ctx_st *ctx);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`ctx`             | A previously initialized wlcsv context structure handle.

__Returns:__  A pointer to the state structure or `NULL` on error.
__See also:__ [`wlcsv_state_lineskip_set()`](#wlcsv_state_lineskip_set-), [`wlcsv_state_options_set()`](#wlcsv_state_options_set-), [`WLCSV_STATE_MEMB_GET()`](#WLCSV_STATE_MEMB_GET-)


#### `wlcsv_state_lineskip_set()`

Set the number of lines from the beginning of the csv file to be skipped over when processing.

```c
inline void wlcsv_state_lineskip_set(const wlcsv_state_st *stt, unsigned skip)
{
    if (stt)
        stt->lineskip = skip;
}
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`stt`             | A pointer to a [wlcsv state structure](#wlcsv_state_st).
|`skip`            | The number of lines, `0` or greater.

__See also:__ [`wlcsv_state_get()`](#wlcsv_state_get-)


#### `wlcsv_state_options_set()`

Set the number of lines from the beginning of the csv file to be skipped over when processing.

```c
inline void wlcsv_state_options_set(const wlcsv_state_st *stt, unsigned options)
{
    if (stt)
        stt->options ^= options;
}
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`stt`             | A pointer to a [wlcsv state structure](#wlcsv_state_st).
|`options`         | A bit mask of option values.

- You can bitwise OR different options together when passing them as a parameter.

__See also:__ [`wlcsv_state_get()`](#wlcsv_state_get-)
