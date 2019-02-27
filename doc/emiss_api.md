# emiss

## Synopsis

> `#include "emiss.h"`
> Compile and link with `-lpcre -lpq` (dependencies of `wlcsv.h` and `ẁlpq.h`)


This file documents `emiss.h`, the main header file of the Emission API.

### License

(c) Joa Käis (github.com/jiikai) 2018-2019 under the [MIT license](../LICENSE.md).

-------------------------------------------------------------------------------

## Includes

The header `emiss.h` includes the following:

```c
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "bstrlib.h"
#include "civetweb.h"
#include "wlcsv.h"
#include "wlpq.h"
```

-------------------------------------------------------------------------------

## Macros


-------------------------------------------------------------------------------

## Types

### Structure types

#### `emiss_update_ctx_st`

An opaque handle for a data parser/uploader context structure.

```c
typedef struct emiss_update_ctx emiss_update_ctx_st;
```
- Implemented in `emiss_upload.c`.


#### `emiss_resource_ctx_st`

An opaque handle for a data retrieval and format context structure.

```c
typedef struct emiss_resource_ctx emiss_resource_ctx_st;
```
- Implemented in `emiss_resource.c`.


#### `emiss_server_ctx_st`

An opaque handle for a server context structure.

```c
typedef struct emiss_server_ctx emiss_server_ctx_st;
```
- Implemented in `emiss_server.c`.


#### `emiss_template_st`

A document template structure & type.

```c
typedef struct emiss_template_s  {
    emiss_resource_ctx_st *         rsrc_ctx;
    char                            template_name[EMISS_NTEMPLATES][0x10];
    emiss_template_ft *             template_function[EMISS_NTEMPLATES];
    int                             template_count;
    emiss_printfio_ft *             output_function;
} emiss_template_st;
```


### Function types

#### `emiss_printfio_ft`

[TODO]

```c
typedef int (emiss_printfio_ft)(void * at,
    const unsigned http_response_code,
    const uintmax_t byte_size,
    const char * restrict mime_type,
    const char * restrict conn_action,
    const char * restrict frmt,...);
```

#### `emiss_template_ft`

[TODO]

```c
typedef int (emiss_template_ft)(emiss_template_st * template_data,
    size_t i, const char * qstr, void * cbdata);
```

-------------------------------------------------------------------------------

## Functions

### From `emiss_retrieve.c`

#### `emiss_retrieve_data()`

Fetch data from a remote source [TODO elaboration] and decompress all files if they zipped.

```c
int emiss_retrieve_data();
```

__Returns:__ `1` on success or `0` on error.
__See also:__ [`emiss_should_check_for_update()`](#emiss_should_check_for_update-)


#### `emiss_should_check_for_update()`

Check the timestamp of last data retrieval.

```c
inline int emiss_should_check_for_update()
{
    time_t current_time, last_access_time = (time_t) strtol(getenv("LAST_DATA_ACCESS"), 0, 10);
    if (last_access_time == LONG_MAX)
        log_err(ERR_FAIL_A, EMISS_ERR, "converting string to long:", "integer overflow");
	else if (time(&current_time) == -1)
        log_err(ERR_FAIL, EMISS_ERR, "obtaining current time in seconds");
	else if (difftime(current_time, last_access_time) >= EMISS_UPDATE_INTERVAL)
        return 1;
    else
        return 0;
    return -1;
}
```
- Queries the environment variable `LAST_DATA_ACCESS` for the last time data was downloaded from
a remote source.

__Returns:__ If less than `EMISS_UPDATE_INTERVAL`, `0`, if equal to or greater, `1`, on error, `-1`.
__See also:__ [`emiss_retrieve_data()`](#emiss_retrieve_data-)


### From `emiss_update.c`

#### `emiss_free_update_ctx()`

Deallocate the data parser & updater context structure.

```c
void emiss_free_update_ctx(emiss_update_ctx_st *upd_ctx);;
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`upd_ctx`         | An initialized data update context structure.

__See also:__ [`emiss_init_update_ctx()`](#emiss_init_update_ctx-)


#### `emiss_init_update_ctx()`

Allocate and initialize the data parser & updater context structure.

```c
emiss_update_ctx_st * emiss_init_update_ctx(char *tui_chart_worldmap_data_path);
```
|__Parameter__        |__Description__
|:--------------------|:----------------------------------------------------------
|`tui_chart_data_path`| [TODO]

__Returns:__ The initialized data update context structure or `NULL` on error.
__See also:__ [`emiss_free_update_ctx()`](#emiss_free_update_ctx-)


### From `emiss_resource.c`

### From `emiss_server.c`
