# emiss.h

## Synopsis

> `#include "emiss.h"`
> Compile and link with `-lpcre -lpq` (dependencies of `wlcsv.h` and `ẁlpq.h`)


This file documents [`emiss.h`](../include/emiss.h), the main header file of the [Emission API](../README.md).

See also the documentation for the other components, [`wlcsv.h`](./wlcsv_api.md) and [`wlpq.h`](./wlpq_api.md).

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

- Defines the name of this interface (for use in error messages).
```c
#define EMISS_ERR "EMISSION"
```

- Data sources. Definable at compile-time, defaults to the below values.
```c
#ifndef EMISS_WORLDBANK_HOST
    #define EMISS_WORLDBANK_HOST "api.worldbank.org"
    #define EMISS_WORLDBANK_HOST_PROTOCOL "http"
#endif
#ifndef EMISS_COUNTRY_CODES_HOST
    #define EMISS_COUNTRY_CODES_HOST "raw.githubusercontent.com"
    #define EMISS_COUNTRY_CODES_HOST_PROTOCOL "https"
#endif
#ifndef EMISS_TUI_CHART_MAPS_CDN_HOST
    #define EMISS_TUI_CHART_MAPS_CDN_HOST "uicdn.toast.com"
    #define EMISS_TUI_CHART_MAPS_CDN_HOST_PROTOCOL "https"
#endif
#ifndef EMISS_TUI_CHART_CDN_MAPS_REL_URI
    #define EMISS_TUI_CHART_CDN_MAPS_REL_URI "tui.chart/latest/maps/"
#endif
#ifndef EMISS_WORLDBANK_REL_URI
    #define EMISS_WORLDBANK_REL_URI "v2/en/indicator/"
#endif
#ifndef EMISS_COUNTRY_CODES_REL_URI
    #define EMISS_COUNTRY_CODES_REL_URI "datasets/country-codes/master/data/"
#endif
#ifndef EMISS_WORLDBANK_QSTR_DOWNLOAD_FORMAT
    #define EMISS_WORLDBANK_QSTR_DOWNLOAD_FORMAT "downloadformat=csv"
#endif
```

- Current number of indicators/datasets tracked in the database.
```c
#ifndef EMISS_NINDICATORS
    #define EMISS_NINDICATORS 3
#endif
```

- Dataset names for retrieval of indicators and numeric IDs for them.

    > `DATASET_0` is not sourced from Worldbank, but from the following repository:

    > https://raw.githubusercontent.com/datasets/country-codes/master/data/country-codes.csv

    > It is needed to provide a mapping from the three-letter ISO codes used by Worldbank to
    the two letter ones used by the tui.chart graph library.

    > `DATASET_META` references metadata common to Worldbank indicator files.

    > Others reference actual Worldbank indicator codenames.

```c
#define DATASET_COUNTRY_CODES 0
#define DATASET_0_NAME "country-codes"
#define DATASET_CO2E 1
#define DATASET_1_NAME "EN.ATM.CO2E.KT"
#define DATASET_POPT 2
#define DATASET_2_NAME "SP.POP.TOTL"
#define DATASET_META 0xFF
#define DATASET_META_NAME "Meta"
```

- Indicators provided by Worldbank date back to 1960.
```c
#define EMISS_DATA_STARTS_FROM 1960
```

- However, due to limitations of the hobby-dev db in Heroku (10 000 rows), we need to cut some stuff out.
```c
#ifndef EMISS_YEAR_ZERO
    #define EMISS_YEAR_ZERO 1980
#endif
```

- Both `YEAR_ZERO` and `YEAR_LAST` can be changed at compile time in.
```c
#ifndef EMISS_YEAR_LAST
    #define EMISS_YEAR_LAST 2014
#endif
```

- Currently, data provided by Worldbank ends at 2017.
```c
#define EMISS_DATA_ENDS_AT 2017
```

- Number of "country slots."

    Currently, there are ~260 "Country or Area" entries in the data, a portion of which are
    either aggregates of many countries or not independent. 300 different country slots for arrays
    holding country-related data ought to be enough for the foreseeable future.
```c
#define NCOUNTRY_DATA_SLOTS 300
```

- Remote data update interval, defaults to 1 week.
```c
#ifndef EMISS_UPDATE_INTERVAL
    #define EMISS_UPDATE_INTERVAL 604800
#endif
```

- A PCRE regex to pick out fields that are not to be included as rows in the database.
```c
#ifndef EMISS_IGNORE_REGEX
    #define EMISS_IGNORE_REGEX\
        "((Country|Indicator)( Code| Name))"\
        "|(Population.*|CO2 emissions.* |Region|IncomeGroup|SpecialNotes|WLD|INX|Not classified)"\
        "|(\\w+\\.\\w+\\.\\w+)"
#endif
```

- File path prefixes.
```c
#define EMISS_RESOURCE_ROOT "../resources"
#define EMISS_DATA_ROOT EMISS_RESOURCE_ROOT"/data"
#define EMISS_JS_ROOT EMISS_RESOURCE_ROOT"/js"
#define EMISS_HTML_ROOT EMISS_RESOURCE_ROOT
#define EMISS_CSS_ROOT EMISS_RESOURCE_ROOT"/css"
#define EMISS_FONT_ROOT EMISS_RESOURCE_ROOT"/fonts"
```

- Number of static and template (dynamically modified) assets.
```c
#define EMISS_NSTATICS 5
#define EMISS_NTEMPLATES 2
```

All URIs to resources made available on the server.
```c
#define EMISS_URI_INDEX "/"
#define EMISS_URI_EXIT "/exit"
#define EMISS_URI_NEW "/new"
#define EMISS_URI_SHOW "/show"
#define EMISS_URI_ABOUT "/about"
#define EMISS_URI_STYLE_CSS "/css/all.min.css"
#define EMISS_URI_FONTS "/fonts"
#define EMISS_FONT_SANS "fira-sans-v8"
#define EMISS_VALID_FONT_NAMES EMISS_FONT_SANS"-latin-regular"
#define EMISS_URI_CHART_JS "/js/chart.js"
#define EMISS_URI_PARAM_JS "/js/param.js"
#define EMISS_URI_VERGE_JS "/js/verge.min.js"
```
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
__See also:__ [`emiss_should_check_for_update()`](#emiss_should_check_for_update)


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
__See also:__ [`emiss_retrieve_data()`](#emiss_retrieve_data)


### From `emiss_update.c`

#### `emiss_update_ctx_free()`

Deallocate the data parser & updater context structure.

```c
void emiss_update_ctx_free(emiss_update_ctx_st *upd_ctx);;
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`upd_ctx`         | An initialized data update context structure.

__See also:__ [`emiss_update_ctx_init()`](#emiss_update_ctx_init)


#### `emiss_update_ctx_init()`

Allocate and initialize the data parser & updater context structure.

```c
emiss_update_ctx_st * emiss_update_ctx_init(char *tui_chart_worldmap_data_path);
```
|__Parameter__        |__Description__
|:--------------------|:----------------------------------------------------------
|`tui_chart_data_path`| [TODO]

__Returns:__ The initialized data update context structure or `NULL` on error.
__See also:__ [`emiss_update_ctx_free()`](#emiss_update_ctx_free)


#### `emiss_update_parse_send()`

Allocate and initialize the data parser & updater context structure.

```c
size_t emiss_update_parse_send(emiss_update_ctx_st *upd_ctx,
    char **paths, uintmax_t *file_sizes, size_t npaths,
    int *dataset_ids, time_t last_update);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`upd_ctx`         | An initialized data update context structure.
|`paths`           | A string array containing paths to the csv data files.
|`file_sizes`      | The total byte size of each respective csv file.
|`npaths`          | The number of csv files in paths.
|`dataset_ids`     | An array of integer codes used to control the interpretation of data.
|`last_update`     | UNIX timestamp of the last time updates were checked for.

__Returns:__ Size in bytes of all parsed data or `-1` on error.
__See also:__ [`emiss_update_ctx_free()`](#emiss_update_ctx_free), [`emiss_update_ctx_init()`](#emiss_update_ctx_init)


### From `emiss_resource.c`

#### `emiss_resource_ctx_free()`

Deallocate the application resource context structure.

```c
void emiss_resource_ctx_free(emiss_resource_ctx_st *rsrc_ctx);;
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`rsrc_ctx`         | An initialized resource context structure.

__See also:__ [`emiss_resource_ctx_init()`](#emiss_resource_ctx_init)


#### `emiss_resource_ctx_init()`

Allocate and initialize application resource context structure.

```c
emiss_resource_ctx_st * emiss_resource_ctx_init();
```

__Returns:__ The initialized resource context structure or `NULL` on error.
__See also:__ [`emiss_resource_ctx_free()`](#emiss_resource_ctx_free)


#### `emiss_resource_template_free()`

Deallocate the application resource context structure.

```c
inline void emiss_resource_template_free(emiss_template_st *template_data)
{
    if (template_data)
        free(template_data);
}
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`template_data`   | An initialized document template data structure.

__See also:__ [`emiss_resource_template_init()`](#emiss_resource_template_init)


#### `emiss_resource_template_init()`

Allocate and initialize application resource context structure.

```c
emiss_template_st * emiss_resource_template_init(emiss_resource_ctx_st *rsrc_ctx);
```

__Returns:__ The initialized document template data structure or `NULL` on error.
__See also:__ [`emiss_resource_template_free()`](#emiss_resource_template_free)


#### `emiss_resource_static_get()`

Return a pointer to a static asset stored in memory.

```c
char * emiss_resource_static_get(emiss_resource_ctx_st *rsrc_ctx, size_t i);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`rsrc_ctx`        | An initialized resource context structure.
|`i`               | The index (unsigned integer) of the resource.

__Returns:__ A pointer to the resource as a `char *` or `NULL` on any error.
__See also:__ [`emiss_resource_ctx_init()`](#emiss_resource_ctx_init), [`emiss_resource_static_size()`](#emiss_resource_static_size)


#### `emiss_resource_static_size()`

Return the size in bytes of a static asset stored in memory.

```c
size_t emiss_resource_static_size(emiss_resource_ctx_st *rsrc_ctx, size_t i);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`rsrc_ctx`        | An initialized resource context structure.
|`i`               | The index (unsigned integer) of the resource.

__Returns:__ The byte size as a positive, unsigned integer or `0` on any error.
__See also:__ [`emiss_resource_ctx_init()`](#emiss_resource_ctx_init), [`emiss_resource_static_get()`](#emiss_resource_static_get)


### From `emiss_server.c`

#### `emiss_server_ctx_free()`

Deallocate the web server context structure.

```c
void emiss_server_ctx_free(emiss_server_ctx_st *server_ctx);;
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`server_ctx`      | An initialized web server context structure.

__See also:__ [`emiss_server_ctx_init()`](#emiss_server_ctx_init)


#### `emiss_server_ctx_init()`

Allocate and initialize web server context structure.

```c
emiss_server_ctx_st * emiss_server_ctx_init(emiss_template_st *template_data);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`template_data`   | Page template object structure, see  [`emiss_resource_template_init()`](#emiss_resource_template_init).

- `template_data` is optional if only static content should be served.

__Returns:__ The initialized server context structure or `NULL` on error.
__See also:__ [`emiss_server_ctx_free()`](#emiss_server_ctx_free), [`emiss_server_run()`](#emiss_server_run)


#### `emiss_server_run()`

Allocate and initialize web server context structure.

```c
int emiss_server_run(emiss_server_ctx_st *server_ctx);
```
|__Parameter__     |__Description__
|:-----------------|:----------------------------------------------------------
|`server_ctx`      | An initialized web server context structure.

__Returns:__ `EXIT_SUCCESS` on a succesful exit or `EXIT_FAILURE` if any errors occured during the run.
__See also:__ [`emiss_server_ctx_free()`](#emiss_server_ctx_free), [`emiss_server_ctx_init()`](#emiss_server_ctx_init)
