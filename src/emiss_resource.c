/*  @file  emiss_resource.c
**  @brief Part of the implementation of "emiss.h".
*/

/*
**  INCLUDES
*/

#include "emiss.h"

#include <math.h>
#include "util_json.h"
#include "util_sql.h"

/*
**  MACRO CONSTANTS
*/

#define INTERNAL_ERROR_MSG "An internal error occured processing the request."

#define DATA_NOT_FOUND_MSG "No data for the selected time range could be found for: "

#define COUNTRY_COL_NAMES "code_iso_a3, code_iso_a2, name, region_id, "\
    "income_id, is_independent, is_an_aggregate, in_tui_chart"

/*
**  FUNCTION MACROS
*/

/*  Expands to arguments for a format string specifying the js for producing line charts. */

#define SQL_SELECT_COUNTRY_ORDER_BY(criterion)\
    "SELECT "COUNTRY_COL_NAMES" AS "COUNTRY_COL_NAMES" FROM Country ORDER BY "criterion";"

#define MAP_CHART_COUNTRYDATA_SIZE(ncountries)\
    (2 * ncountries * JSON_SYNTACTIC_LENGTH("code", "data") + ncountries * 16)

#define LINE_CHART_COUNTRYDATA_SIZE(ncountries, names_bytelen, ndatapoints)\
    (names_bytelen + ncountries * JSON_SYNTACTIC_LENGTH("name", "data") + ndatapoints * 16)

/*  Ugly but useful conditional operator expressions. */
#define CHOOSE_COL_LINE_CHART(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ? "Yeardata.year, "\
        "round(((Datapoint.emission_kt/Datapoint.population_total) * 1000000)::numeric, 3)"\
    : dataset == DATASET_CO2E ? "Yeardata.year, Datapoint.emission_kt"\
    : "Yeardata.year, Datapoint.population_total")

#define CHOOSE_COL_MAP_CHART(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ?\
        "round(((emission_kt/population_total) * 1000000)::numeric, 3), country_code"\
    : dataset == DATASET_CO2E ? "emission_kt, country_code" : "population_total, country_code")

#define CHOOSE_ALIAS_MAP_CHART(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ? "emission_kg_per_capita"\
    : dataset == DATASET_CO2E ? "emission_kt" : "population_total")

#define CHOOSE_MAP_CHART_TITLE_FRMT(dataset, per_capita)\
    ((dataset == DATASET_CO2E && per_capita) ?\
        "Carbon dioxide emissions, year %u, per capita (kg/person) by country."\
    : dataset == DATASET_CO2E ? "Carbon dioxide emissions, year %u, total (kt) by country."\
    : "Population, year %u, total by country.")

#define CHOOSE_WHERE_CLAUSE(from, to)\
    (from < to ? "Yeardata.year>=%u AND Yeardata.year<=%u ORDER BY Yeardata.year"\
    : "yeardata_year=%u ORDER BY country_code")

#define CHOOSE_ALIAS_LINE_CHART(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ? "emission_kg_per_capita"\
    : dataset == DATASET_CO2E ? "emission_kt" : "population_total")

#define CHOOSE_LINE_CHART_TITLE(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ? "CO2 emissions in kilograms per capita (kg/person)"\
    : dataset == DATASET_CO2E ? "Carbon dioxide emissions, total by country and year"\
    : "Population by country and year")

#define CHOOSE_SUFFIX(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ? "kg/person" : dataset == DATASET_CO2E ? "kt" : "")

#define CHOOSE_Y_AXIS_TITLE(dataset, per_capita)\
    (dataset == DATASET_CO2E && per_capita ? "CO2 emissions in kilograms per capita (kg/person)"\
    : dataset == DATASET_CO2E ? "CO2 emissions in kilotonnes (kt)"\
    : "Population count, total")

#define LINE_CHART_PARAMS(dataset, per_capita)\
    CHOOSE_LINE_CHART_TITLE(dataset, per_capita),\
    CHOOSE_SUFFIX(dataset, per_capita),\
    CHOOSE_Y_AXIS_TITLE(dataset, per_capita)

#define LINE_CHART_PARAMS_LEN(dataset, per_capita)\
    strlen(CHOOSE_LINE_CHART_TITLE(dataset, per_capita))\
    + strlen(CHOOSE_SUFFIX(dataset, per_capita))\
    + strlen(CHOOSE_Y_AXIS_TITLE(dataset, per_capita))

#define STRLLEN(str_lit) (sizeof(str_lit) - 1U)

#define FILL_YEARDATA(str)\
    do {\
        unsigned i, j;\
        j = 0;\
        for (i = EMISS_YEAR_ZERO; i <= EMISS_YEAR_LAST; ++i) {\
            sprintf(&str[j], "\"%u\",", i);\
            j += 7;\
        }\
    } while (0)

#define TIMESPEC_INIT_S_MS(s_val, ms_val)\
    (struct timespec) {.tv_sec = s_val, .tv_nsec = ms_val * 1000000L}

/*
**   STRUCTURES & TYPES
*/

/*  Definition & declaration of a key-value type structure for asynchronous operation
    by callbacks on database result sets. The atomic 'in_progress' flag is set to false
    to establish between threads that the result set has been processed. */
struct result_storage_s {
    void                   *name;
    void                   *data;
    unsigned                count;
    volatile atomic_flag    in_progress;
};

/*  Definition & declaration of a country data structure holding various
    relevant information. Cached from the database for faster access.
    Structure member description:
    - ccount: Holds the actual number of countries.
    - iso3: ISO-3166-1 Alpha-3 code and the NULL byte
    - iso2: ISO-3166-1 Alpha-2 code and the NULL byte.
    - name: Fixed-length array of variable length country names.
    - total_byte_length_of_names: total length of the above names in bytes.
    - region_and_income: an 8-bit value:
        (bit no)           0 1 2 3   | 4 5 6 7
        (data id)          region_id | income_id
        (valid values)     1 - 7     | 1 - 4
        (value if not set) == 0      | == 0
    - country_type: valid 8-bit values:
        00 = not set,
        01 = independent and in tui.chart,
        02 = independent and not in tui.chart,
        04 = aggregate,
        08 = not independent and not an aggregate and in tui.chart,
        16 = not independent and not an aggregate and not in tui.chart
*/
struct country_data {
    char                       *name[NCOUNTRY_DATA_SLOTS];
    char                        iso3[NCOUNTRY_DATA_SLOTS][4];
    char                        iso2[NCOUNTRY_DATA_SLOTS][3];
    size_t                      ccount;
    size_t                      total_byte_length_of_names;
    uint8_t                     region_and_income[NCOUNTRY_DATA_SLOTS];
    uint8_t                     country_type[NCOUNTRY_DATA_SLOTS];
};

/*  Definition of an application resource structure declared and typedef'd in header, housing
    a db connection context pointer, a country data structure pointer of the type specified above,
     a string with the data range in years formatted
    for convenience and two bstring arrays of in-memory html and js source.
*/
struct emiss_resource_ctx {
    struct wlpq_conn_ctx       *conn_ctx;
    struct country_data        *cdata;
    char                        yeardata_formatted[EMISS_SIZEOF_FORMATTED_YEARDATA];
    size_t                      yeardata_size;
    bstring                     static_resource[EMISS_NSTATICS];
    char                        static_resource_name[EMISS_NSTATICS][0x20];
    uintmax_t                   static_resource_size[EMISS_NSTATICS];
    bstring                     template[EMISS_NTEMPLATES];
    uintmax_t                   template_frmtless_size[EMISS_NTEMPLATES];
};

/*
**  FUNCTIONS
*/

/*  STATIC  */

static inline char *
escape_single_quotes(char *buf, char *src)
{
    char *single_quote = strchr(src, '\'');
    if (single_quote) {
        char *ptr = buf;
        while (single_quote) {
            single_quote[0] = '\0';
            memcpy(ptr, src, strlen(src));
            strcat(ptr, "\\'");
            ptr += strlen(ptr);
            src = single_quote + 1;
            single_quote = strchr(src, '\'');
        }
        strcat(buf, src);
        return buf;
    }
    return src;
}

static inline struct result_storage_s *
init_result_storage_s()
{
    struct result_storage_s *dest_buf = malloc(sizeof(struct result_storage_s));
    check(dest_buf, ERR_MEM, EMISS_ERR);
    atomic_flag_clear(&dest_buf->in_progress);
    atomic_flag_test_and_set(&dest_buf->in_progress);
    dest_buf->data = 0;
    dest_buf->count = 0;
    dest_buf->name = 0;
    return dest_buf;
error:
    return 0;
}


static inline bstring
read_to_bstring(char *path, void *placeholder_count_dest)
{
    FILE *fp = 0;
	fp = fopen(path, "r");
    check(fp, ERR_FAIL_A, EMISS_ERR, "opening file", path);
    bstring read_here = bread((bNread) fread, fp);
    check(read_here, ERR_FAIL_A, EMISS_ERR, "reading file", path);
    if (placeholder_count_dest) {
        size_t nplaceholders = 0;
        char *ptr = bdata(read_here);
        ptr = strstr(ptr, "%s");
        while (ptr) {
            ++nplaceholders;
            ptr += 2;
            ptr = ptr ? strstr(ptr, "%s") : 0;
        }
        if (nplaceholders)
            memcpy(placeholder_count_dest, &nplaceholders, sizeof(size_t));
    } fclose(fp);
	return read_here;
error:
	if (fp)
		fclose(fp);
	return 0;
}

static inline bstring
frmt_new_chart_html(struct country_data *cdata, char *html)
{
    size_t ncountries = cdata->ccount;
    char *buf = calloc(64 * ncountries + cdata->total_byte_length_of_names * 2 + 1,
                    sizeof(char));
    check(buf, ERR_MEM, EMISS_ERR);
    size_t i, j = 0;
    for (i = 0; i < ncountries; ++i) {
        const char *type = cdata->country_type[i] == 4 ? "a"
                         : cdata->country_type[i] >= 8 ? "n"
                         : "i";
        FRMT_HTML_OPTION_ID_VALUE(&buf[j], type, cdata->iso3[i], cdata->name[i], 1);
        j += strlen(&buf[j]);
    }
    bstring formatted_html = bformat(html, buf);
    free(buf);
    return formatted_html;
error:
    return 0;
}

static inline bstring
frmt_chart_params_js(char *js)
{
    return bformat(js, EMISS_YEAR_ZERO, EMISS_YEAR_LAST - 1,
        EMISS_YEAR_ZERO, EMISS_YEAR_LAST);
}

static inline int
binary_search_str_arr(int count, size_t el_size, char (*data)[el_size], char *key)
{
    int l = 0;
    int r = count - 1;
    size_t len = el_size - 1;
    while (l <= r) {
        int m = (int) floor((l + r) / 2);
        int diff = strncmp(data[m], key, len);
        if (diff < 0)
            l = m + 1;
        else if (diff > 0)
            r = m - 1;
        else
            return m;
    }
    return -1;
}

static void
callback_countrydata_res_handler(PGresult *res, void *arg)
{
    emiss_resource_ctx_st *rsrc_ctx = (emiss_resource_ctx_st *)arg;
    struct country_data *cdata = rsrc_ctx->cdata;
    size_t rows = PQntuples(res);
    char *field;
    size_t total_byte_length_of_names = 0;
    for (size_t i = 0; i < rows; ++i) {
        field = PQgetvalue(res, i, 0);
        if (strlen(field) == 3)
            memcpy(&cdata->iso3[i], field, 3);
        field = PQgetvalue(res, i, 1);
        if (strlen(field) == 2)
            memcpy(&cdata->iso2[i], field, 2);
        field = PQgetvalue(res, i, 2);
        size_t len = strlen(field);
        if (len) {
            field = PQgetvalue(res, i, 2);
            total_byte_length_of_names += len;
            cdata->name[i] = calloc(len + 1, sizeof(char));
            check(cdata->name[i], ERR_MEM, EMISS_ERR);
            memcpy(cdata->name[i], field, len);
        }

        uint8_t region_id           = (uint8_t) atoi(PQgetvalue(res, i, 3));
        uint8_t income_id           = (uint8_t) atoi(PQgetvalue(res, i, 4));
        cdata->region_and_income[i] = (region_id | (income_id << 4));
        uint8_t is_independent      = strstr(PQgetvalue(res, i, 5), "t") ? 1 : 0;
        uint8_t is_an_aggregate     = strstr(PQgetvalue(res, i, 6), "t")  ? 1 : 0;
        uint8_t in_tui_chart        = strstr(PQgetvalue(res, i, 7), "t")  ? 1 : 0;
        cdata->country_type[i]      = is_independent && in_tui_chart
                                    ? 1 : is_independent
                                    ? 2 : is_an_aggregate
                                    ? 4 : in_tui_chart
                                    ? 8 : 16;
    }

    cdata->ccount = rows;
    cdata->total_byte_length_of_names = total_byte_length_of_names;
    return;
error:
    exit(0);
}

static void
callback_datapoint_res_handler(PGresult *res, void *arg)
{
    struct result_storage_s *dest = (struct result_storage_s *)arg;
    size_t rows = (size_t) PQntuples(res);
    if (dest->data) {
        struct country_data *cdata = (struct country_data *)dest->data;
        size_t ccount = cdata->ccount;
        char (*iso2codes)[3] = cdata->iso2;
        char (*iso3codes)[4] = cdata->iso3;
        uint8_t *country_type = cdata->country_type;
        char (*dest_data)[0xF] = calloc(rows, sizeof(*dest_data));
        check(dest_data, ERR_MEM, EMISS_ERR);
        memset(dest_data, 0, sizeof(*dest_data));
        dest->data = dest_data;
        char **dest_name = malloc(rows * sizeof(char *));
        check(dest_name, ERR_MEM, EMISS_ERR);
        dest->name = dest_name;
        size_t j = 0, k = 0, len = 0;
        for (size_t i = 0; i < rows; ++i) {
            len = PQgetlength(res, i, 0);
            if (len) {
                char *country_code = PQgetvalue(res, i, 1);
                while (strncmp(country_code, iso3codes[j], 3))
                    if (++j == ccount) {
                        /*  shouldn't happen */
                        dest->count = k;
                        atomic_flag_clear(&dest->in_progress);
                        return;
                    }

                uint8_t in_tui_chart = country_type[j] == 1 || country_type[j] == 8 ? 1 : 0;
                if (in_tui_chart && iso2codes[j] && iso2codes[j][0]) {
                    memcpy(dest_data[k], PQgetvalue(res, i, 0), len);
                    dest_name[k] = iso2codes[j];
                    ++k;
                }
            }
        }
        dest->count = k;
    } else {
        char buffer[0x2000] = {0};
        size_t j = 0, k = 0, count = 0;
        const char *frmt = "%s,";
        for (size_t i = 0; i < rows; ++i) {
            size_t len = PQgetlength(res, i, 1);
            if (len) {
                snprintf(&buffer[j], 0xFFF, frmt, PQgetvalue(res, i, 1));
                ++count;
            } else
                if (rows - i == 1) {
                    /*  Handle some border cases lest tui.chart crash. */
                    len = 12;
                    snprintf(&buffer[j], 0xFFF, frmt, "Number(null)");
                } else {
                    len = 4;
                    snprintf(&buffer[j], 0xFFF, frmt, "null");
                }

            j += len + 1;
            ++k;
        }
        if (count > 1) {
            dest->count = count;
            buffer[j - 1] = '\0';
            dest->data = malloc(++j * sizeof(char));
            check(dest->data, ERR_MEM, EMISS_ERR);
            memcpy(dest->data, buffer, j);
        }
    }
    atomic_flag_clear(&dest->in_progress);
    return;
error:
    exit(0);
}

static int
retrieve_country_data(emiss_resource_ctx_st *rsrc_ctx)
{
    char *cmd = SQL_SELECT_COUNTRY_ORDER_BY("Country.code_iso_a3");
    int ret   = wlpq_query_run_blocking(rsrc_ctx->conn_ctx,
                        cmd, 0, 0, 0, (wlpq_res_handler_ft *)
                        callback_countrydata_res_handler,
                        rsrc_ctx);
    check(ret, ERR_FAIL_N, EMISS_ERR, "running a blocking db query: returned", ret);
    check(rsrc_ctx->cdata->name[rsrc_ctx->cdata->ccount - 1], ERR_FAIL, EMISS_ERR,
        "saving data to cdata array");
    return 1;
error:
    return 0;
}

static int
frmt_map_chart_data(emiss_template_st *template_data,
    struct result_storage_s *query_res, size_t ncountries,
    uint8_t dataset_id, uint8_t per_capita, unsigned year,
    void *cbdata)
{
    printf("proceeded to format\n");
    /*  Wait for data retrieval to complete. */
    volatile atomic_flag *not_ready = &query_res->in_progress;
    struct timespec timer = TIMESPEC_INIT_S_MS(0, 5);
    do
        nanosleep(&timer, 0);
    while (atomic_flag_test_and_set(not_ready));
    printf("queries ready\n");
    /*  Allocate space for result string and grab pointers. */
    size_t count      = query_res->count,
    countrydata_len   = count * STRLLEN(",{\"code\":\"XX\",\"data\":}") + count * 0xF;
    char *countrydata = calloc(countrydata_len, sizeof(char));
    check(countrydata, ERR_MEM, EMISS_ERR);
    char **iso2 = (char **)query_res->name;
    char (*data)[0xF] = (char (*)[0xF]) query_res->data;

    /*  Format data from iso2 and data to a single JSON array string. */
    size_t j = 0;
    for (size_t i = 0; i < count; ++i) {
        int ret = JSON_FRMT_KEY_VALUE_PAIR(&countrydata[j],
                        countrydata_len - j, "code", "data",
                        i, iso2[i], data[i]);
        check(ret >= 0, ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
        j += ret;
    }
    free(iso2);
    free(data);
    free(query_res);

    /*  Call provided output function. */
    const char *js = bdata(template_data->rsrc_ctx->template[1]);
    char title[0x80];
    int ret = snprintf(title, 0x7F, CHOOSE_MAP_CHART_TITLE_FRMT(dataset_id, per_capita), year);
    check(ret >= 0, ERR_FAIL, EMISS_ERR, "printf'ing to buffer\n");
    uintmax_t byte_size = template_data->rsrc_ctx->template_frmtless_size[1]
                                + STRLLEN("map") + j + ret;
    ret = template_data->output_function(cbdata, 200,
                                byte_size, "application/javascript",
                                "close", js, "map", "", countrydata,
                                title, "", "", "");
    free(countrydata);
    return ret;
error:
    return template_data->output_function(cbdata, 500,
                                STRLLEN(INTERNAL_ERROR_MSG),
                                "text/plain", "close",
                                "%s", INTERNAL_ERROR_MSG);
}

static int
frmt_line_chart_data(emiss_template_st *template_data, unsigned year_start,
    unsigned year_end, struct result_storage_s **query_res, size_t nitems,
    size_t names_bytelen, uint8_t dataset_id, uint8_t per_capita, void *cbdata)
{
    if (year_start < EMISS_YEAR_ZERO)
        year_start = EMISS_YEAR_ZERO;
    if (year_end > EMISS_YEAR_LAST)
        year_end = EMISS_YEAR_LAST;
    if (year_end < year_start + 1)
        year_end = year_start + 1;
    emiss_resource_ctx_st *rsrc_ctx = template_data->rsrc_ctx;

    char *yeardata = calloc((1 + year_end - year_start) * 7, sizeof(char));
    check(yeardata, ERR_MEM, EMISS_ERR);
    char *years_formatted = rsrc_ctx->yeardata_formatted;
    size_t yeardata_len = (1 + year_end - year_start) * (sizeof(",\"0123\"") - 1) - 1;
    memcpy(yeardata, &years_formatted[(year_start - EMISS_YEAR_ZERO) * 7], yeardata_len);

    unsigned ndatapoints = (1 + year_end - year_start) * nitems;
    size_t countrydata_len = nitems * (STRLLEN("{\"name\":\"\",\"data\":[]},") - 1
                             + ndatapoints * 0x10) + names_bytelen;
    char *countrydata = calloc(countrydata_len, sizeof(char));
    check(countrydata, ERR_MEM, EMISS_ERR);
    size_t j = 0, k = 0;
    char not_found_msg[0x1000] = {0};
    strncpy(not_found_msg, DATA_NOT_FOUND_MSG, 0xFFF);
    struct timespec timer = TIMESPEC_INIT_S_MS(0, 5);
    for (size_t i = 0; i < nitems; ++i) {
        volatile atomic_flag *not_ready = &query_res[i]->in_progress;
        do
            nanosleep(&timer, 0);
        while (atomic_flag_test_and_set(not_ready));
        char *name = (char *)query_res[i]->name;
        char *data = (char *)query_res[i]->data;
        if (name && data) {
            char buf[64] = {0};
            name = escape_single_quotes(buf, name);
            if (!query_res[i]->count) {
                size_t len = strlen(name);
                memcpy(&not_found_msg[k], name, len);
                k += len;
                memcpy(&not_found_msg[k], ", ", 2);
                k += 2;
            } else {
                int ret = JSON_FRMT_KEY_ARRAY_VALUE_PAIR(&countrydata[j], countrydata_len - j,
                        "name", "data", i, name, data);
                check(ret >= 0, ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
                j += ret;
                free(data);
            }

        }
        free(query_res[i]);
    }
    free(query_res);
    const char *js = bdata(rsrc_ctx->template[1]);
    uintmax_t byte_size = rsrc_ctx->template_frmtless_size[1]
                            + STRLLEN("line") + j + yeardata_len + k +
                            + LINE_CHART_PARAMS_LEN(dataset_id, per_capita);

    int ret = template_data->output_function(cbdata, 200,
                                byte_size, "application/javascript", "close",
                                js, "line", yeardata, countrydata,
                                LINE_CHART_PARAMS(dataset_id, per_capita),
                                k ? not_found_msg : "");
    free(countrydata);
    free(yeardata);
    return ret;
error:
    return template_data->output_function(cbdata, 500,
                                STRLLEN(INTERNAL_ERROR_MSG),
                                "text/plain", "close",
                                "%s", INTERNAL_ERROR_MSG);
}

static int
retrieve_matching_data(emiss_template_st *template_data, unsigned from_year,
    unsigned to_year, uint8_t dataset, uint8_t per_capita, char *country_codes,
    size_t ncountries, void *cbdata)
{
    printf("retrieve matching\n");
    emiss_resource_ctx_st *rsrc_ctx = template_data->rsrc_ctx;
    char buf[0x600] = {0};
    char out[0x600] = {0};
    uint8_t map_chart = from_year == to_year ? 1 : 0;
    const char *where = CHOOSE_WHERE_CLAUSE(from_year, to_year);

    /*  Enqueue non-blocking queries for the data values. Results will be
        parsed by a callback to a buffer struct, the address of which is passed
        forward formatting the data. */
    int ret;
    char (*iso3codes)[4] = rsrc_ctx->cdata->iso3;
    if (map_chart) {
        const char *tbl   = "Datapoint";
        const char *col   = CHOOSE_COL_MAP_CHART(dataset, per_capita);
        const char *alias = CHOOSE_ALIAS_MAP_CHART(dataset, per_capita);
        ncountries        = rsrc_ctx->cdata->ccount;
        check(SQL_SELECT_WHERE(buf, 0x5FF, out, 0x5FF, col, alias,
                tbl, where, from_year) >= 0, ERR_FAIL, ERR_MEM,
                "printf'ing to buffer");
        struct result_storage_s *res_dest = init_result_storage_s();
        check(res_dest, ERR_FAIL, EMISS_ERR, "initializing result destination buffer");
        res_dest->data            = rsrc_ctx->cdata;

        wlpq_query_data_st *qr_dt = wlpq_query_init(out, 0, 0, 0,
                                            callback_datapoint_res_handler,
                                            res_dest, 0);
        check(qr_dt, ERR_FAIL, EMISS_ERR, "initializing query data structure");

        ret = wlpq_query_queue_enqueue(rsrc_ctx->conn_ctx, qr_dt);
        check(ret, ERR_FAIL, EMISS_ERR, "enqueuing query to db");

        return frmt_map_chart_data(template_data, res_dest, ncountries,
                    dataset, per_capita, from_year, cbdata);
    } else {

        const char *col         = CHOOSE_COL_LINE_CHART(dataset, per_capita);
        const char *alias       = CHOOSE_ALIAS_LINE_CHART(dataset, per_capita);
        const char *from_tbl    = "Yeardata";
        const char *join_tbl    = "Datapoint";
        const char *join_on     = "Yeardata.year=Datapoint.yeardata_year "\
                                    "AND Datapoint.country_code='%s'";
        struct result_storage_s **res_dest_arr;
        res_dest_arr            = malloc(sizeof(struct result_storage_s *) * ncountries);
        check(res_dest_arr, ERR_MEM, EMISS_ERR);
        char **names            = rsrc_ctx->cdata->name;
        size_t ccount           = rsrc_ctx->cdata->ccount;
        size_t names_bytelength = 0;
        char *ptr               = country_codes;
        for (size_t i = 0; i < ncountries; ++i) {
            ptr = strchr(ptr, '=') + 1;
            char code[4] = {0};
            memcpy(code, ptr, 3);
            check(SQL_SELECT_JOIN_WHERE(buf, 0x5FF, out, 0x5FF,
                    col, alias, from_tbl, "LEFT", join_tbl,
                    join_on, where, code, from_year, to_year) >= 0,
                    ERR_FAIL, EMISS_ERR, "printf'ing to buffer");

            res_dest_arr[i] = init_result_storage_s();
            check(res_dest_arr[i], ERR_FAIL, EMISS_ERR, "initializing result buffer");
            wlpq_query_data_st *qr_dt = wlpq_query_init(out, 0, 0, 0,
                                            callback_datapoint_res_handler,
                                            res_dest_arr[i], 0);
            check(qr_dt, ERR_FAIL, EMISS_ERR, "initializing query data structure");
            check(wlpq_query_queue_enqueue(rsrc_ctx->conn_ctx, qr_dt),
                    ERR_FAIL, EMISS_ERR, "enqueuing query to db");
            printf("enqueued query:\n%s\n", out);
            ret = binary_search_str_arr(ccount, 4, iso3codes, code);
            if (ret != -1) {
                res_dest_arr[i]->name = names[ret];
                names_bytelength     += strlen(names[ret]);
            } else
                log_warn(ERR_FAIL_A, EMISS_ERR, "finding country name for code", code);
            memset(buf, 0, sizeof(buf));
            memset(out, 0, sizeof(out));
        }
        return frmt_line_chart_data(template_data, from_year, to_year,
                    res_dest_arr, ncountries, names_bytelength,
                    dataset, per_capita, cbdata);
    }
error:
    return template_data->output_function(cbdata, 500,
                            STRLLEN(INTERNAL_ERROR_MSG),
                            "text/plain", "close",
                            "%s", INTERNAL_ERROR_MSG);
}

static int
forward_to_format(emiss_template_st *template_data, size_t i, const char *qstr, void *cbdata)
{
    const uint8_t dataset = strstr(qstr, "co2e") ? DATASET_CO2E : DATASET_POPT;
    const uint8_t per_capita = dataset == DATASET_CO2E && strstr(qstr, "co2e_percapita") ? 1 : 0;
    const char *invalid = "";
    if (strstr(qstr, "line")) {
        const char *from_year_str = strstr(qstr, "from_year");
        unsigned long from_year   = from_year_str
                                  ? strtoul(strchr(from_year_str, '=') + 1, 0, 10)
                                  : 0;
        if (!from_year || from_year == ULONG_MAX)
            invalid = "from_year";
        else {
            const char *to_year_str = strstr(from_year_str, "to_year");
            unsigned long to_year   = to_year_str
                                    ? strtoul(strchr(to_year_str, '=') + 1, 0, 10)
                                    : 0;
            if (!to_year || to_year == ULONG_MAX)
                invalid = "to_year";
            else {
                unsigned long count   = 0;
                const char *count_str = strstr(qstr, "count");
                if (!count_str) {
                    char *ccode = strstr(qstr, "ccode");
                    while (ccode) {
                        ++count;
                        ccode = strstr(ccode, "ccode");
                    }
                } else {
                    count = strtoul(strchr(count_str, '=') + 1, 0, 10);
                    if (!count || count == ULONG_MAX)
                        invalid = "count";
                    else {
                        return retrieve_matching_data(template_data,
                                from_year, to_year, dataset,
                                per_capita, strstr(count_str, "ccode"),
                                count, cbdata);
                    }
                }
            }
        }
    } else if (strstr(qstr, "map")) {
        const char *sel_year_str = strstr(qstr, "select_year");
        unsigned long sel_year   = sel_year_str
                                 ? strtoul(strchr(sel_year_str, '=') + 1, 0, 10)
                                 : 0;
        if (!sel_year || sel_year == ULONG_MAX)
            invalid = "select_year";
        else
            return retrieve_matching_data(template_data,
                sel_year, sel_year, dataset,
                per_capita, NULL, 0, cbdata);
    } else
        invalid = "chart_type";
    /*  Invalid request. */
    const char *frmt = "Invalid or missing parameter %s.";
    return template_data->output_function(cbdata,
                            400, strlen(frmt) - 2 + strlen(invalid),
                            "text/plain", "close", frmt, invalid);
}

static int
format_chart_html(emiss_template_st *template_data,
    size_t i, const char *qstr, void *cbdata)
{
    printf("%d\n", (int)strlen(qstr));
    return template_data->output_function(cbdata, 200,
                            template_data->rsrc_ctx->template_frmtless_size[i] + strlen(qstr),
                            "text/html", "close", bdata(template_data->rsrc_ctx->template[i]),
                            qstr);
}

/*  EXTERN INLINE INSTANTIATIONS */

extern inline void
emiss_free_template_structure(emiss_template_st *template_data);

/*  PROTOTYPE IMPLEMENTATIONS */

emiss_template_st *
emiss_construct_template_structure(emiss_resource_ctx_st *rsrc_ctx)
{
    emiss_template_st *template_data = calloc(1, sizeof(emiss_template_st));
    check(template_data, ERR_MEM, EMISS_ERR);
    template_data->rsrc_ctx = rsrc_ctx;
    template_data->template_count = EMISS_NTEMPLATES;
    memcpy(template_data->template_name[0], "show", 4);
    memcpy(template_data->template_name[1], "chart", 5);
    template_data->template_function[0] = format_chart_html;
    template_data->template_function[1] = forward_to_format;
    return template_data;
error:
    return 0;
}

char *
emiss_get_static_resource(emiss_resource_ctx_st *rsrc_ctx, size_t i)
{
    if (rsrc_ctx && i < EMISS_NSTATICS)
        return bdata(rsrc_ctx->static_resource[i]);
    return 0;
}

size_t
emiss_get_static_resource_size(emiss_resource_ctx_st *rsrc_ctx, size_t i)
{
    if (rsrc_ctx && i < EMISS_NSTATICS)
        return blength(rsrc_ctx->static_resource[i]);
    return 0;
}

emiss_resource_ctx_st *
emiss_init_resource_ctx()
{
    emiss_resource_ctx_st *rsrc_ctx = malloc(sizeof(emiss_resource_ctx_st));
    check(rsrc_ctx, ERR_MEM, EMISS_ERR);

    rsrc_ctx->conn_ctx = wlpq_conn_ctx_init(0);
    check(rsrc_ctx->conn_ctx, ERR_FAIL, EMISS_ERR, "initializing resources: unable to init db");

    rsrc_ctx->cdata = calloc(1, sizeof(struct country_data));
    check(rsrc_ctx->cdata, ERR_MEM, EMISS_ERR);
    int ret = retrieve_country_data(rsrc_ctx);
    check(ret, ERR_FAIL, EMISS_ERR, "initializing resources: unable to retrieve country data");

    FILL_YEARDATA(rsrc_ctx->yeardata_formatted);

    size_t nplacehold[EMISS_NTEMPLATES] = {0};
    rsrc_ctx->static_resource[0]        = read_to_bstring(EMISS_HTML_ROOT"/index.html", 0);
    rsrc_ctx->static_resource_size[0]   = blength(rsrc_ctx->static_resource[0]);

    bstring new_html                    = read_to_bstring(EMISS_HTML_ROOT"/new.html", 0);
    rsrc_ctx->static_resource[1]        = frmt_new_chart_html(rsrc_ctx->cdata, bdata(new_html));
    rsrc_ctx->static_resource_size[1]   = blength(rsrc_ctx->static_resource[1]);
    bdestroy(new_html);

    bstring chart_params_js             = read_to_bstring(EMISS_JS_ROOT"/param.js", 0);
    rsrc_ctx->static_resource[2]        = frmt_chart_params_js(bdata(chart_params_js));
    rsrc_ctx->static_resource_size[2]   = blength(rsrc_ctx->static_resource[2]);
    bdestroy(chart_params_js);

    rsrc_ctx->static_resource[3]        = read_to_bstring(EMISS_JS_ROOT"/verge.min.js", 0);
    rsrc_ctx->static_resource_size[3]   = blength(rsrc_ctx->static_resource[3]);

    rsrc_ctx->static_resource[4]        = read_to_bstring(EMISS_HTML_ROOT"/about.html", 0);
    rsrc_ctx->static_resource_size[3]   = blength(rsrc_ctx->static_resource[4]);

    rsrc_ctx->template[0]               = read_to_bstring(EMISS_HTML_ROOT"/show.html",
                                            &nplacehold[0]);
    rsrc_ctx->template_frmtless_size[0] = blength(rsrc_ctx->template[0]) - nplacehold[0] * 2;
    rsrc_ctx->template[1]               = read_to_bstring(EMISS_JS_ROOT"/chart.js", &nplacehold[1]);
    rsrc_ctx->template_frmtless_size[1] = blength(rsrc_ctx->template[1]) - nplacehold[1] * 2;

    wlpq_threads_launch_async(rsrc_ctx->conn_ctx);

    return rsrc_ctx;
error:
    return NULL;
}

void
emiss_free_resource_ctx(emiss_resource_ctx_st *rsrc_ctx)
{
    if (rsrc_ctx) {
        if (rsrc_ctx->conn_ctx)
            wlpq_conn_ctx_free(rsrc_ctx->conn_ctx);

        char **cdata_name = rsrc_ctx->cdata->name;
        unsigned count = rsrc_ctx->cdata->ccount;
        for (size_t i = 0; i < count; ++i)
            if (cdata_name[i])
                free(cdata_name[i]);
        free(rsrc_ctx->cdata);

        bstring *rsrc = rsrc_ctx->static_resource;
        for (size_t i = 0; i < EMISS_NSTATICS; ++i)
            if (rsrc[i])
                bdestroy(rsrc[i]);

        bstring *template = rsrc_ctx->template;
        for (size_t i = 0; i < EMISS_NTEMPLATES; ++i)
            if (template[i])
                bdestroy(template[i]);

        free(rsrc_ctx);
    }
}
