#ifndef _wbindica_h_
#define _wbindica_h_

/*! @headerfile emiss.h "include/emiss.h"
    @brief An interface to the Emission API.
*/

#define __STDC_WANT_LIB_EXT1__ 1
#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 700

/*  Data sources. Definable at compile-time, defaults to the below values. */
#define WBINDICA_PROTOCOL "http"
#define WBINDICA_HOST_URL "api.worldbank.org"
#define WBINDICA_REL_URL "v2/en/indicator/"
#define WBINDICA_DATA_DOWNLOAD_FORMAT "downloadformat=csv"

#define WBINDICA_COUNTRY_CODES_HOST_URL "raw.githubusercontent.com"
#define WBINDICA_COUNTRY_CODES_HOST_PROTOCOL "https"
#define WBINDICA_COUNTRY_CODES_REL_URL "datasets/country-codes/master/data/"

#define WBINDICA_TUI_CHART_MAPS_HOST_URL "uicdn.toast.com"
#define WBINDICA_TUI_CHART_MAPS_HOST_PROTOCOL "https"
#define WBINDICA_TUI_CHART_MAPS_REL_URL "tui.chart/latest/maps/"

#define WBINDICA_DEFAULT_NINDICATORS 3

/*!  Indicators provided by Worldbank date back to 1960. */
#define WBINDICA_DATA_STARTS_FROM 1960
/*  However, due to limitations of the hobby-dev db in Heroku (10 000 rows),
     we need to cut some stuff out. */
#ifndef WBINDICA_YEAR_ZERO
    #define WBINDICA_YEAR_ZERO 1980 // TODO EDIT THIS
#endif
/*  Both YEAR_ZERO and YEAR_LAST can be changed by a #define at compile time in CFLAGS. */
#ifndef WBINDICA_YEAR_LAST
    #define WBINDICA_YEAR_LAST 2014 // TODO EDIT THIS
#endif
/*!  Currently, data provided by Worldbank ends at 2017. */
#define WBINDICA_DATA_ENDS_AT 2017

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

 /* _wbindica_h_ */
