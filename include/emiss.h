#ifndef _emiss_
#define _emiss_

#define __STDC_WANT_LIB_EXT1__ 1
#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 700

/*
** INCLUDES
*/

#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include <sys/time.h>

#include "civetweb.h"
#include "dbg.h"
#include "bstrlib.h"
#include "psql-db.h"

#ifdef _WIN32
    #include <windows.h>
    #define sleeper(x) Sleep(x * 1000)
#else
    #include <unistd.h>
    #define sleeper(x) sleep(x)
#endif

/*
** MACRO CONSTANTS
*/

/* For specifying aligment of elements in structs. */
#define SYS_MAX_ALIGNMENT alignof(max_align_t)
#define __MAX_ALIGNED alignas(SYS_MAX_ALIGNMENT)

/*  Error message provider name. */
#define EMISS_ERR "EMISSION"
#define EMISS_MSG EMISS_ERR
/*  Remote data update interval, default 1 week. */
#ifndef EMISS_UPDATE_INTERVAL
    #define EMISS_UPDATE_INTERVAL 604800
#endif

/*  Data sources. Definable at compile-time, defaults to the below values. */
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

/*  Current number of indicators/datasets tracked in the database. */
#define EMISS_NINDICATORS 3

/*  Indicators provided by Worldbank date back to 1960. */
#define EMISS_DATA_STARTS_FROM 1960
/*  However, due to limitations of the hobby-dev db in Heroku (10 000 rows),
    we need to cut some stuff out. */
#ifndef EMISS_YEAR_ZERO
    #define EMISS_YEAR_ZERO 1980
#endif
/*  Both YEAR_ZERO and YEAR_LAST can be changed by passing a #define at compile time in CFLAGS. */
#ifndef EMISS_YEAR_LAST
    #define EMISS_YEAR_LAST 2014
#endif
/*  Currently, data ends at 2017. */
#define EMISS_DATA_ENDS_AT 2017

/*  Dataset names for retrieval of indicators and numeric IDs for them. */

/*  The first one is not sourced from Worldbank, but from the following dataset repository:
        https://raw.githubusercontent.com/datasets/country-codes/master/data/country-codes.csv
    It is needed to provide a mapping from the three-letter country codes used by Worldbank to the
    two letter ones used by the js library tui.chart that is used to draw the graphs client-side.
*/
#define DATASET_COUNTRY_CODES 0
#define DATASET_0_NAME "country-codes"

/*  The below reference actual Worldbank indicator names. */
#define DATASET_CO2E 1
#define DATASET_1_NAME "EN.ATM.CO2E.KT"
#define DATASET_POPT 2
#define DATASET_2_NAME "SP.POP.TOTL"
/*  ---> if indicators are added in the future, their names and ids will be placed here <--- */

/*  A special dataset id (== max value of 1 byte integer) and name for the country-specific
    metadata included with the Worldbank indicator csv files.
*/
#define DATASET_META 0xFF
#define DATASET_META_NAME "Meta"

/*  Currently, there are ~260 "Country or Area" entries in the data, a portion of which are
    either aggregates of many countries or not independent.

    300 different country slots for arrays holding country-related data ought to be enough
    for the foreseeable future.
*/
#define NCOUNTRY_DATA_SLOTS 300

/*  A PCRE regex to pick out fields that are not to be included as rows in the database. */
#define EMISS_IGNORE_REGEX\
    "((Country|Indicator)( Code| Name))"\
    "|(Population.*|CO2 emissions.*|Region|IncomeGroup|SpecialNotes|WLD|INX|Not classified)"\
    "|(\\w+\\.\\w+\\.\\w+)"

/*  File path prefixes. */
#define EMISS_RESOURCE_ROOT "../resources"
#define EMISS_DATA_ROOT EMISS_RESOURCE_ROOT"/data"
#define EMISS_JS_ROOT EMISS_RESOURCE_ROOT"/js"
#define EMISS_HTML_ROOT EMISS_RESOURCE_ROOT"/html"
#define EMISS_CSS_ROOT EMISS_RESOURCE_ROOT"/css"

#define EMISS_NSTATICS 3  /* index.html, new.html, chart_params.js */
#define EMISS_NTEMPLATES 3 /* show.html, line_chart.js, map_chart.js */
#define EMISS_SIZEOF_FORMATTED_YEARDATA (EMISS_YEAR_LAST - EMISS_YEAR_ZERO) * 7 + 1

/* All currently valid relative URIs on the server. */
#define EMISS_URI_INDEX "/"
#define EMISS_URI_EXIT "/exit"
#define EMISS_URI_NEW "/new"
#define EMISS_URI_CHART "/show"
#define EMISS_URI_ABOUT "/about"
#define EMISS_URI_STYLE_CSS "/css/style.css"
//#define EMISS_FONT_URI "/fonts"
#define EMISS_URI_CHART_PARAM_JS "/js/chart_params.js"
#define EMISS_URI_LINE_CHART_JS "/js/line_chart.js"
#define EMISS_URI_MAP_CHART_JS "/js/map_chart.js"


/*  Options for the CivetWeb server. */
#define NUM_THREADS "64"
#define DOCUMENT_ROOT "../resources"
#ifdef HEROKU
    #define EMISS_SERVER_PORT getenv("PORT")
    #define EMISS_SERVER_HOST "emiss.herokuapp.com"
    #define EMISS_SERVER_PROTOCOL_PREFIX "https://"
    #define EMISS_ABS_ROOT_URL "https://emiss.herokuapp.com"

#else
    #ifndef EMISS_SERVER_PORT
        #define EMISS_SERVER_PORT "8080"
    #endif
    #ifndef EMISS_SERVER_HOST
        #define EMISS_SERVER_HOST "localhost"
    #endif
    #define EMISS_SERVER_PROTOCOL_PREFIX "http://"
    #define EMISS_ABS_ROOT_URL "http://joa-p702a:8080"
#endif
#define REQUEST_TIMEOUT "30000"
#define AUTH_DOM_CHECK "no"

/*  Keep-alive options if WITH_KEEP_ALIVE_SUPPORT #defined at compile-time */
#ifdef WITH_KEEP_ALIVE_SUPPORT
    #define KEEP_ALIVE_SUPPORT\
        "additional_header", "Connection: Keep-Alive",\
        "tcp_nodelay", "1",\
        "enable_keep-alive", "1",\
        "keep_alive_timeout_ms", REQUEST_TIMEOUT
#else
    #define KEEP_ALIVE_SUPPORT\
        "enable_keep_alive", "0"
#endif

#define HTTP_RES_200_ARGS "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nConnection: %s\r\n\r\n"
#define HTTP_RES_200 "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
#define HTTP_RES_404 "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"
#define HTTP_RES_405 "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\nConnection: close\r\n\r\n"
#define HTTP_RES_405_ARGS "HTTP/1.1 405 Method Not Allowed\r\nAllow: %s\r\nConnection: close\r\n\r\n"
#define HTTP_RES_500 "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n"

/*
**  FUNCTION MACROS
*/

#define STRINGIFY(A) #A
#define TOSTRING(A) STRINGIFY(A)

#define GET_LAST_DATA_ACCESS(arg)\
    do {\
        char *ptr;\
        arg = (time_t) strtoul(getenv("LAST_DATA_ACCESS"), &ptr, 10);\
        check(arg != ULONG_MAX, ERR_FAIL_A, EMISS_ERR,\
            "converting string to unsigned long:", "integer overflow");\
    } while (0)

#define UPDATE_LAST_DATA_ACCESS(arg)\
    do {\
        check(time(&arg) != -1, ERR_FAIL, EMISS_ERR,\
            "obtaining current time in seconds");\
        char buf[65] = {0};\
        sprintf(buf, "%lu", (unsigned long) arg);\
        setenv("LAST_DATA_ACCESS", buf, 1);\
    } while (0)

/*  Byte length calculation. */
#define SIZE_IN_BYTES(object)\
    ((uintmax_t) sizeof(object) * (sizeof(size_t) / sizeof(uint8_t)))

#define HTTP_CONTENT_LENGTH_TO_STR(body, str_out)\
    sprintf(str_out, "Content-Length: %lu", (uintmax_t) SIZE_IN_BYTES(body))

/*  Port protocol getter expression. */
#define DEFINE_PROTOCOL(server, i)\
    (server->civet_ports[i].is_ssl ? "https" : "http")

/*  Macros for formatting SQL statements. */

#define SQL_PREPARE_INSERT_INTO(output, stmt_name, types, table, columns, ...)\
    sprintf(output, "PREPARE %s (%s) AS INSERT INTO %s (%s) VALUES (%s);",\
        stmt_name, types, table, columns, __VA_ARGS__)

#define SQL_PREPARE_UPDATE_WHERE(output, stmt_name, types, table, columns, ...)\
    sprintf(output, "PREPARE %s (%s) AS UPDATE %s SET %s WHERE %s;",\
        stmt_name, types, table, columns, __VA_ARGS__)

#define SQL_SELECT(buffer, output, columns, aliases, tables, ...)\
    sprintf(buffer, "SELECT %s AS %s FROM %s;", columns, aliases, tables);\
    sprintf(output, buffer, __VA_ARGS__)

#define SQL_SELECT_WHERE(buffer, output, columns, aliases, tables, where, ...)\
    sprintf(buffer, "SELECT %s AS %s FROM %s WHERE %s;", columns, aliases, tables, where);\
    sprintf(output, buffer, __VA_ARGS__)

#define SQL_INSERT_INTO(buffer, output, table, columns, values, ...)\
    sprintf(buffer, "INSERT INTO %s (%s) VALUES (%s);", table, columns, values);\
    sprintf(output, buffer, __VA_ARGS__)

#define SQL_UPDATE_WHERE(buffer, output, table, set, where, ...)\
    sprintf(buffer, "UPDATE %s SET %s WHERE %s;", table, set, where);\
    sprintf(output, buffer, __VA_ARGS__)

#define SQL_UPSERT(buffer, output, insertsql, arbiter, set, ...)\
    sprintf(buffer, "%s ON CONFLICT (%s) DO UPDATE SET %s;", insertsql, arbiter, set);\
    sprintf(output, buffer, __VA_ARGS__)

#define SQL_WITH_SELECT_WHERE(buffer, output, with_table,\
        columns, aliases, from_tables, where, ...)\
    sprintf(buffer, "WITH %s AS (SELECT %s AS %s FROM %s WHERE %s)",\
        with_table, columns, aliases, from_tables, where);\
    sprintf(output, buffer, __VA_ARGS__)

#define SQL_APPEND_WITH_SELECT_WHERE(buffer, output, sql, with_table,\
        columns, aliases, from_tables, where, ...)\
    sprintf(buffer, "%s, %s AS (SELECT %s AS %s FROM %s WHERE %s)",\
        sql, with_table, columns, aliases, from_tables, where);\
    sprintf(output, buffer, __VA_ARGS__)

#define SQL_UPDATE_WITH_WHERE(buffer, output, withsql, table, set, where, ...)\
    sprintf(buffer, "%s UPDATE %s SET %s WHERE %s;", withsql, table, set, where);\
    sprintf(output, buffer, __VA_ARGS__)

#define SQL_WHEN_THEN_ELSE(buffer, output, condition, sql_1, sql_2, ...)\
    sprintf(buffer, "CASE WHEN %s THEN %s ELSE %s;");\
    sprintf(output, buffer, __VA_ARGS__)

/*  Macros for formatting JSON entries. */

#define JSON_DATA_ENTRY_BY_CODE(output, iso2_code, value, append)\
    do {\
        char *frmt = append ? ",{code:'%s',data:%s}" : "{code:'%s',data:%s}";\
        sprintf(output, frmt, iso2_code, value);\
    } while (0)

#define JSON_DATA_ENTRY_BY_NAME(output, name, value, append)\
    do {\
        char *frmt = append ? ",{name:'%s',data:[%s]}" : "{name:'%s',data:[%s]}";\
        sprintf(output, frmt, name, value);\
    } while (0)

#define FRMT_JSON_ENTRY(output, buffer, append, key_name, val_name, ...)\
    do {\
        char *frmt = append ? ",{%s:%s}" : "{%s:%s}";\
        sprintf(buffer, frmt, key_name, val_name);\
        sprintf(output, buffer, __VA_ARGS__);\
    } while (0)

#define JSON_SYNTACTIC_LENGTH(key_name, value_name)\
    strlen(key_name) + strlen(value_name) + 10
/*  e.g.  strlen("code")       == 4
        + strlen("data")       == 4
        + strlen(",{:'',:[]}") == 10
                               == 18    */

/*  Expands to arguments for a format string specifying the js for producing line charts. */
#define LINE_CHART_PARAMS(ydata, cdata, dataset, per_capita)\
    ydata, cdata, CHOOSE_LINE_CHART_TITLE(dataset, per_capita),\
    CHOOSE_Y_AXIS_TITLE(dataset, per_capita), CHOOSE_SUFFIX(dataset, per_capita)

#define MAP_CHART_COUNTRYDATA_SIZE(ncountries)\
    (2 * ncountries * JSON_SYNTACTIC_LENGTH("code", "data") + ncountries * 0x10)

#define LINE_CHART_COUNTRYDATA_SIZE(ncountries, names_bytelen, ndatapoints)\
    (names_bytelen + ncountries * JSON_SYNTACTIC_LENGTH("name", "data") + ndatapoints * 0x10)

/*  Ugly but useful conditional operator expressions. */
#define CHOOSE_COL(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ?\
        "round(((emission_kt/population_total) * 1000000)::numeric, 3), country_code" :\
    dataset == DATASET_CO2E ? "emission_kt, country_code" : "population_total, country_code")


#define CHOOSE_ALIAS(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ? "emission_kg_per_capita" :\
    dataset == DATASET_CO2E ? "emission_kt" : "population_total")

#define CHOOSE_WHERE_CLAUSE(from, to)\
    (from < to ?\
        "country_code='%s' AND yeardata_year>=%d AND yeardata_year<=%d ORDER BY yeardata_year" :\
        "yeardata_year=%d ORDER BY country_code")

#define CHOOSE_SUFFIX(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ? "kg/person" :\
    dataset == DATASET_CO2E ? "kt" : "")

#define CHOOSE_Y_AXIS_TITLE(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ?\
        "CO2 emissions in kilograms per capita (kg/person)" :\
    dataset == DATASET_CO2E ?\
        "CO2 emissions in kilotonnes (kt)" :\
        "Population count, total")

#define CHOOSE_LINE_CHART_TITLE(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ?\
        "CO2 emissions in kilograms per capita (kg/person)" :\
    dataset == DATASET_CO2E ?\
        "Carbon dioxide emissions, total by country and year" :\
        "Population by country and year")

#define CHOOSE_MAP_CHART_TITLE_FRMT(dataset, per_capita)\
    dataset == DATASET_CO2E && per_capita ?\
        "Carbon dioxide emissions, year %u, per capita (kg) by country." :\
    dataset == DATASET_CO2E ?\
        "Carbon dioxide emissions, year %u, total (kt) by country.":\
        "Population, year %u, total by country."

/*  Formats a HTML option to a buffer for a datalist with country ids and values. */
#define FRMT_HTML_OPTION_ID_VALUE(buffer, type, id, value, newline)\
    sprintf(buffer, "<option class=\"opt-cntr-type-%s\" id=\"%s\" value=\"%s\"></option>%s",\
        type, id, value, (newline ? "\n" : ""))

/*
** TYPE DEFINITIONS/DECLARATIONS
*/

/*  Parser and uploader context structure declaration and type. Defined in emiss_upload.c. */
typedef struct emiss_update_ctx emiss_update_ctx_st;

/*  Declaration & typedef of a document template structure. Defined as a transparent
    structure below.
*/
typedef struct emiss_template_s emiss_template_st;

/*  A context structure declaration and typedef. Defined in emiss_resource.c. */
typedef struct emiss_resource_ctx emiss_resource_ctx_st;

/*  Function typesdefs. Used in the context of the template structure declared above and
    defined below.
*/
typedef int (emiss_printfio_ft)(void *at, const signed http_response_code,
    const char *mime_type, const char *conn_action, const char *restrict frmt, ...);
typedef int (emiss_template_ft)(emiss_template_st *template_data, size_t i,
    const char *qstr, void *cbdata);
/*  For use with bsearch() */
typedef int (emiss_compar_ft)(const void *a, const void *b);

/*  Definition of the template structure declared above. */
typedef struct emiss_template_s {
    emiss_resource_ctx_st          *rsrc_ctx;
    char                            template_name[EMISS_NTEMPLATES][0x10];
    emiss_template_ft              *template_function[EMISS_NTEMPLATES];
    int                             template_count;
    emiss_printfio_ft              *output_function;
} emiss_template_st;

/*  Opaque server context structure declaration. Implemented in emiss_server.c. */

typedef struct emiss_server_ctx emiss_server_ctx_st;

/*
** FUNCTION PROTOTYPES & INLINE FUNCTION DEFINITIONS
*/

/*  "emiss_retrieve.c":
*/

/*  int emiss_should_check_for_update():
    Query the environment variable 'LAST_DATA_ACCESS' for the last time data was downloaded from
    remote source. If less than EMISS_UPDATE_INTERVAL (see header emiss.h), return 0, if equal to
    or greater than, return 1. Return -1 on error.
*/
inline int
emiss_should_check_for_update()
{
    time_t last_access_time, current_time;
	GET_LAST_DATA_ACCESS(last_access_time);
	check(time(&current_time) != -1, ERR_FAIL, EMISS_ERR,
        "obtaining current time in seconds");
	if (difftime(current_time, last_access_time) >= EMISS_UPDATE_INTERVAL) {
        return 1;
	}
    return 0;
error:
    return -1;
}

/*  [TODO DOCUMENTATION HERE] */
int
emiss_retrieve_data();

/*  "emiss_update.c":
*/

/*  Allocator/initializer. */
emiss_update_ctx_st *
emiss_init_update_ctx(char *tui_chart_worldmap_data_path, uintmax_t tui_chart_worldmap_data_size);

/*  Deallocator/cleaner. */
void
emiss_free_update_ctx(emiss_update_ctx_st *upd_ctx);

/*  Preview csv files and check "Last updated" information.
    If newer than last_update, update records in the database
    concerning the data in that file. For data of type "Meta",
    run an update if any other update is run (Worldbank sadly
    does not annotate their metadata files with dates).
*/
size_t
emiss_parse_and_update(emiss_update_ctx_st *upd_ctx,
    char **paths, uintmax_t *file_sizes, size_t npaths,
    int *dataset_ids, time_t last_update);

/*  "emiss_prepare.c":
*/

/*  Allocator/initializer for resource context structure. */
emiss_resource_ctx_st *
emiss_init_resource_ctx();

/*  Deallocator/cleaner for resource context structure. */
void
emiss_free_resource_ctx();

/*  Allocator/initializer for document template data structure. */
emiss_template_st *
emiss_construct_template_structure(emiss_resource_ctx_st *rsrc_ctx);

/*  Deallocator/cleaner for document template data structure. */
inline void
emiss_free_template_structure(emiss_template_st *template_data)
{
    if (template_data) {
        free(template_data);
    }
}

char *
emiss_get_static_resource(emiss_resource_ctx_st *rsrc_ctx, size_t i);

/*  "emiss_server.c":
*/

/*  Allocator/initializer for server context structure. */
emiss_server_ctx_st *
emiss_init_server_ctx(emiss_template_st *template_data);

/*  Deallocator/cleaner for resource context structure. */
void
emiss_free_server_ctx(emiss_server_ctx_st *server_ctx);

/*  Run server in an event loop until a terminating signal/request is received. */
int
emiss_server_run(emiss_server_ctx_st *server_ctx);

#endif /* _emiss_ */
