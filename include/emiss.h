#ifndef _emiss_
#define _emiss_

/*! @file  emiss.h "include/emiss.h"
    @version 0.1.0
    @brief An interface to the Emission API.
*/

#define __STDC_WANT_LIB_EXT1__ 1
#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 700

/*
** INCLUDES
*/

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

#include "bstrlib.h"
#include "civetweb.h"
#include "dbg.h"
#include "wlpq.h"

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

/*! Error message provider name. */
#define EMISS_ERR "EMISSION"
#define EMISS_MSG EMISS_ERR
/*! Remote data update interval, defaults to 1 week. */
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

/*!  Current number of indicators/datasets tracked in the database. */
#define EMISS_NINDICATORS 3

/*!  Indicators provided by Worldbank date back to 1960. */
#define EMISS_DATA_STARTS_FROM 1960
/*  However, due to limitations of the hobby-dev db in Heroku (10 000 rows),
     we need to cut some stuff out. */
#ifndef EMISS_YEAR_ZERO
    #define EMISS_YEAR_ZERO 1980 // TODO EDIT THIS
#endif
/*  Both YEAR_ZERO and YEAR_LAST can be changed by a #define at compile time in CFLAGS. */
#ifndef EMISS_YEAR_LAST
    #define EMISS_YEAR_LAST 2014 // TODO EDIT THIS
#endif
/*!  Currently, data provided by Worldbank ends at 2017. */
#define EMISS_DATA_ENDS_AT 2017

/*! Dataset names for retrieval of indicators and numeric IDs for them.

    @def DATASET_0 is not sourced from Worldbank, but from the following repository:

    https://raw.githubusercontent.com/datasets/country-codes/master/data/country-codes.csv

    It is needed to provide a mapping from the three-letter ISO codes used by Worldbank to
    the two letter ones used by the tui.chart graph library.

    @def DATASET_META references metadata common to Worldbank indicator files.

    Others reference actual Worldbank indicator codenames.
*////@{
#define DATASET_COUNTRY_CODES 0
#define DATASET_0_NAME "country-codes"
#define DATASET_CO2E 1
#define DATASET_1_NAME "EN.ATM.CO2E.KT"
#define DATASET_POPT 2
#define DATASET_2_NAME "SP.POP.TOTL"
/*  --> if any indicators are added in the future, their names and ids will be here <-- */
#define DATASET_META 0xFF
#define DATASET_META_NAME "Meta"
///@}

/*! Number of "country slots."

    Currently, there are ~260 "Country or Area" entries in the data, a portion of which are
    either aggregates of many countries or not independent. 300 different country slots for arrays
    holding country-related data ought to be enough for the foreseeable future.
*/
#define NCOUNTRY_DATA_SLOTS 300

/*! A PCRE regex to pick out fields that are not to be included as rows in the database. */
#define EMISS_IGNORE_REGEX\
    "((Country|Indicator)( Code| Name))"\
    "|(Population.*|CO2 emissions.*|Region|IncomeGroup|SpecialNotes|WLD|INX|Not classified)"\
    "|(\\w+\\.\\w+\\.\\w+)"

/*! File path prefixes. *////@{
#define EMISS_RESOURCE_ROOT "../resources"
#define EMISS_DATA_ROOT EMISS_RESOURCE_ROOT"/data"
#define EMISS_JS_ROOT EMISS_RESOURCE_ROOT"/js"
#define EMISS_HTML_ROOT EMISS_RESOURCE_ROOT
#define EMISS_CSS_ROOT EMISS_RESOURCE_ROOT"/css"
#define EMISS_FONT_ROOT EMISS_RESOURCE_ROOT"/fonts"
///@}

/*! Number of static assets. */
#define EMISS_NSTATICS 5
/*! Number of template assets. */
#define EMISS_NTEMPLATES 2
/*! Required size for a buffer holding comma separated years in string format. */
#define EMISS_SIZEOF_FORMATTED_YEARDATA (EMISS_YEAR_LAST - EMISS_YEAR_ZERO) * 7 + 1

/*! All relative URIs available on the server. *////@{
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
///@}

/*
**  FUNCTION MACROS
*/

#define GET_LAST_DATA_ACCESS(arg)\
    do {\
        char *ptr;\
        arg = (time_t) strtol(getenv("LAST_DATA_ACCESS"), &ptr, 10);\
        check(arg != LONG_MAX, ERR_FAIL_A, EMISS_ERR,\
            "converting string to long:", "integer overflow");\
    } while (0)

/*  Byte length calculation. */
#define SIZE_IN_BYTES(object)\
    ((uintmax_t) sizeof(object) * (sizeof(size_t) / sizeof(uint8_t)))


/*  Formats a HTML option to a buffer for a datalist with country ids and values. */
#define FRMT_HTML_OPTION_ID_VALUE(buffer, type, id, value, newline)\
    sprintf(buffer, "<option class=\"opt-cntr-type-%s\" id=\"%s\" value=\"%s\">%s</option>%s",\
        type, id, value, value, (newline ? "\n" : ""))

#define FRMT_HTML_OPTION_YEAR "<option id=\"f%u\" value=\"%u\">%u</option>\n"

/*
** TYPEDEFS AND STRUCTS
*/

/*! An opaque handle for the parser and data uploader context struct.
    Implemented in emiss_upload.c.
*/
typedef struct emiss_update_ctx emiss_update_ctx_st;

/*! Type of a document template structure.
    Defined as a transparent structure below.
*/
typedef struct emiss_template_s emiss_template_st;

/*! An opaque handle for the data retrieval and format context struct.
    Implemented in emiss_resource.c.
*/
typedef struct emiss_resource_ctx emiss_resource_ctx_st;

/*  Function typesdefs. Used in the context of the template structure declared above and
    defined below.
*/
typedef int (emiss_printfio_ft)(void *at,
    const unsigned http_response_code,
    const uintmax_t byte_size,
    const char *restrict mime_type,
    const char *restrict conn_action,
    const char *restrict frmt,...);

typedef int (emiss_template_ft)(emiss_template_st *template_data,
    size_t i, const char *qstr, void *cbdata);

/*!  For use with bsearch() */
typedef int (emiss_compar_ft)(const void *a, const void *b);

/*!  Definition of the template structure declared above. */
struct emiss_template_s {
    emiss_resource_ctx_st          *rsrc_ctx;
    char                            template_name[EMISS_NTEMPLATES][0x10];
    emiss_template_ft              *template_function[EMISS_NTEMPLATES];
    int                             template_count;
    emiss_printfio_ft              *output_function;
};

/*! An opaque handle for the server context structure.
    Implemented in emiss_server.c.
*/
typedef struct emiss_server_ctx emiss_server_ctx_st;

/*
** FUNCTIONS
*/

/*  "emiss_retrieve.c" */

/*! Check the timestamp of last data retrieval.

    Queries the environment variable `LAST_DATA_ACCESS` for the last time data was downloaded from
    a remote source. An inline function.

    @return If less than `EMISS_UPDATE_INTERVAL`, `0`, if equal to or greater, `1`, on error, `-1`.
    @see emiss_retrieve_data(), `EMISS_UPDATE_INTERVAL`
*/
inline int
emiss_should_check_for_update()
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

/*! Fetch data from a remote source [TODO elaboration] and decompress all files if they zipped.

    @return 1 on success, 0 on error.

    @see emiss_should_check_for_update()
*/
int
emiss_retrieve_data();

/*  "emiss_update.c" */

/*! Allocate and initialize the data parser & updater context structure.

    @param tui_chart_data_path [TODO]

    @return The initialized data update context structure or NULL on error.

    @see emiss_free_update_ctx()
*/
emiss_update_ctx_st *
emiss_init_update_ctx(char *tui_chart_worldmap_data_path);

/*! Deallocate the data parser & updater context structure.

    @param upd_ctx An initialized data update context structure.

    @see emiss_init_update_ctx()
*/
void
emiss_free_update_ctx(emiss_update_ctx_st *upd_ctx);

/*! Preview csv files and check "Last updated" information from the Worldbank data files, if newer
    than `last_update`, update records in the database concerning the data in that file.

    For data of type "Meta", run an update in case any other update is run. Worldbank
    sadly does not annotate their metadata csv files with dates.

    @param upd_ctx      An initialized data update context structure.
    @param paths        A string array containing paths to the csv data files.
    @param file_sizes   The total byte size of each respective csv file.
    @param npaths       The number of csv files in paths.
    @param dataset_ids  An array of integer codes that can be used to control how the file data is
                        to be interpreted [TODO elaboration]

    @return Size in bytes of all parsed data or -1 on error.

    @see emiss_free_update_ctx(), emiss_init_update_ctx()
*/
size_t
emiss_parse_and_update(emiss_update_ctx_st *upd_ctx,
    char **paths, uintmax_t *file_sizes, size_t npaths,
    int *dataset_ids, time_t last_update);

/*  "emiss_resource.c" */

/*! Allocator/initializer for resource context structure.

    @return The initialized resource context structure or NULL on error.

    @see emiss_free_resource_ctx()
*/
emiss_resource_ctx_st *
emiss_init_resource_ctx();

/*! Deallocator/cleaner for resource context structure.

    @param template_data An initialized resource context structure.

    @see emiss_init_resource_ctx()
*/
void
emiss_free_resource_ctx();

/*!  Allocator/initializer for document template data structure.

    @param rsrc_ctx An initialized resource context structure.

    @return The initialized document template data structure or NULL on error.

    @see emiss_free_template_structure(), emiss_init_resource_ctx(), emiss_free_resource_ctx()
*/
emiss_template_st *
emiss_construct_template_structure(emiss_resource_ctx_st *rsrc_ctx);

/*! Deallocator/cleaner or document template data structure.

    Function implemented as inline.

    @param template_data An initialized document template data structure.

    @see emiss_construct_template_structure(), emiss_init_resource_ctx(), emiss_free_resource_ctx()
*/
inline void
emiss_free_template_structure(emiss_template_st *template_data)
{
    if (template_data) {
        free(template_data);
    }
}

/*! Return a pointer to a static asset stored in memory.

    @param rsrc_ctx: An initialized resource context structure.
    @param The index of the resource.

    @return A pointer to the resource or NULL on any error.

    @see emiss_init_resource_ctx(), emiss_free_resource_ctx(), emiss_get_static_resource_size()
*/
char *
emiss_get_static_resource(emiss_resource_ctx_st *rsrc_ctx, size_t i);

/*! Return the size in bytes of a static asset stored in memory.

    @param rsrc_ctx: An initialized resource context structure.
    @param The index of the resource.

    @return The byte size as a positive integer or 0 on any error.

    @see emiss_init_resource_ctx(), emiss_free_resource_ctx(), emiss_get_static_resource()
*/
size_t
emiss_get_static_resource_size(emiss_resource_ctx_st *rsrc_ctx, size_t i);

/*  "emiss_server.c" */

/*! Allocator and initializer for server context structure.

    @param template_data    An array of page template objects, only necessary if dynamic content is
                            to be served.

    @return A pointer to the initialized structure or NULL on any error.

    @see emiss_free_server_ctx, emiss_server_run()
*/
emiss_server_ctx_st *
emiss_init_server_ctx(emiss_template_st *template_data);

/*! Deallocator and cleaner for server context structure.

    @param server_ctx An initialized server structure.

    @see emiss_init_server_ctx(), emiss_server_run()
*/
void
emiss_free_server_ctx(emiss_server_ctx_st *server_ctx);

/*! Run server in an event loop until a terminating signal/request is received.

    @param server_ctx An initialized server structure.

    @return EXIT_SUCCESS on a succesful exit, EXIT_FAILURE if any errors occured in server
    startup, operation or cleanup.

    @see emiss_free_server_ctx, emiss_init_server_ctx()
*/
int
emiss_server_run(emiss_server_ctx_st *server_ctx);

#endif /* _emiss_ */
