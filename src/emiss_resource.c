#include "emiss.h"
#include <math.h>

/*
** MACROS
*/

#define FILL_YEARDATA(str)\
    do {\
        unsigned i, j;\
        j = 0;\
        for (i = EMISS_YEAR_ZERO; i <= EMISS_YEAR_LAST; i++) {\
            sprintf(&str[j], "'%u',", i);\
            j += 7;\
        }\
    } while (0)

/*
**   STRUCTURES & TYPES
*/

/*  Definition & declaration of a key-value type structure for asynchronous operation
    by callbacks on database result sets. The atomic 'in_progress' flag is set to false
    to establish between threads that the result set has been processed.
*/
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
    struct psqldb_conn_ctx     *conn_ctx;
    struct country_data        *cdata;
    char                        yeardata_formatted[EMISS_SIZEOF_FORMATTED_YEARDATA];
    bstring                     static_resource[EMISS_NSTATICS];
    char                        static_resource_name[EMISS_NSTATICS][0x20];
    bstring                     template[EMISS_NTEMPLATES];
};

/*
**  FUNCTIONS
*/

/*  STATIC INLINE  */

static inline char *
inl_escape_single_quotes(char *buf, char *src)
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
inl_init_result_storage_s()
{
    struct result_storage_s *dest_buf = malloc(sizeof(struct result_storage_s));
    check(dest_buf, ERR_MEM, EMISS_ERR);
    atomic_flag_clear(&dest_buf->in_progress);
    atomic_flag_test_and_set(&dest_buf->in_progress);
    dest_buf->data = NULL;
    dest_buf->count = 0;
    dest_buf->name = NULL;
    return dest_buf;
error:
    return NULL;
}

static inline bstring
inl_read_file_to_bstring(char *path)
{
	FILE *fp = fopen(path, "r");
    check(fp, ERR_FAIL_A, EMISS_ERR, "opening file", path);
    bstring read_here = bread((bNread) fread, fp);
    check(fp, ERR_FAIL_A, EMISS_ERR, "reading file", path);
    fclose(fp);
	return read_here;
error:
	if (fp) {
		fclose(fp);
	}
	return NULL;
}

static inline bstring
inl_format_new_chart_html(struct country_data *cdata, char *html)
{
    size_t ncountries = cdata->ccount;
    char *buf = calloc(64 * ncountries + cdata->total_byte_length_of_names + 1,
                    sizeof(char));
    check(buf, ERR_MEM, EMISS_ERR);
    size_t i, j = 0;
    for (i = 0; i < ncountries; i++) {
        const char *type = cdata->country_type[i] == 4 ? "a"
                         : cdata->country_type[i] >= 8 ? "n"
                         : "i";
        FRMT_HTML_OPTION_ID_VALUE(&buf[j], type, cdata->iso3[i], cdata->name[i], 1);
        j += strlen(&buf[j]);
    }
    bstring formatted_html = bformat(html, EMISS_ABS_ROOT_URL, EMISS_ABS_ROOT_URL, buf);
    free(buf);
    return formatted_html;
error:
    return NULL;
}

static inline bstring
inl_format_chart_params_js(char *js)
{
    return bformat(js, EMISS_YEAR_ZERO, EMISS_YEAR_LAST - 1,
        EMISS_YEAR_ZERO, EMISS_YEAR_LAST);
}

static inline int
inl_binary_search_str_arr(int count, size_t el_size, char (*data)[el_size], char *key)
{
    int l = 0;
    int r = count - 1;
    size_t len = el_size - 1;
    while (l <= r) {
        int m = (int) floor((l + r) / 2);
        int diff = strncmp(data[m], key, len);
        if (diff < 0) {
            l = m + 1;
        } else if (diff > 0) {
            r = m - 1;
        } else {
            return m;
        }
    }
    return -1;
}

/*  STATIC  */

static void
callback_countrydata_res_handler(PGresult *res, void *arg)
{
    emiss_resource_ctx_st *rsrc_ctx = (emiss_resource_ctx_st *)arg;
    struct country_data *cdata = rsrc_ctx->cdata;
    size_t rows = PQntuples(res);
    char *field;
    size_t len = 0, total_byte_length_of_names = 0;
    for (size_t i = 0; i < rows; i++) {
        field = PQgetvalue(res, i, 0);
        len = strlen(field);
        if (len == 3) {
            memcpy(&cdata->iso3[i], field, 3);
        }
        field = PQgetvalue(res, i, 1);
        len = strlen(field);
        if (len == 2) {
            memcpy(&cdata->iso2[i], field, 2);
        }
        field = PQgetvalue(res, i, 2);
        len = strlen(field);
        if (len) {
            total_byte_length_of_names += len;
            cdata->name[i] = calloc(len + 1, sizeof(char));
            check(cdata->name[i], ERR_MEM, EMISS_ERR);
            memcpy(cdata->name[i], field, len);
        }
        uint8_t region_id = (uint8_t) atoi(PQgetvalue(res, i, 3));
        uint8_t income_id = (uint8_t) atoi(PQgetvalue(res, i, 4));
        cdata->region_and_income[i] = (region_id | (income_id << 4));
        uint8_t is_independent = strstr(PQgetvalue(res, i, 5), "t") ? 1 : 0;
        uint8_t is_an_aggregate = strstr(PQgetvalue(res, i, 6), "t")  ? 1 : 0;
        uint8_t in_tui_chart = strstr(PQgetvalue(res, i, 7), "t")  ? 1 : 0;
        cdata->country_type[i] = is_independent && in_tui_chart
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
callback_row_count_res_handler(PGresult *res, void *arg)
{
    emiss_resource_ctx_st *rsrc_ctx = (emiss_resource_ctx_st *)arg;
    rsrc_ctx->cdata->ccount = (size_t) atoi(PQgetvalue(res, 0, 0));
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
        size_t j = 0;
        size_t k = 0;
        for (size_t i = 0; i < rows; i++) {
            char *country_code = PQgetvalue(res, i, 1);
            while (strcmp(country_code, iso3codes[j])) {
                if (++j == ccount) {
                    /*  shouldn't happen */
                    dest->count = k;
                    atomic_flag_clear(&dest->in_progress);
                    return;
                }
            }
            uint8_t in_tui_chart = country_type[j] == 1 || country_type[j] == 8 ? 1 : 0;
            if (in_tui_chart && iso2codes[j] && strlen(iso2codes[j])) {
                char *datapoint = PQgetvalue(res, i, 0);
                memcpy(dest_data[k], datapoint, strlen(datapoint));
                dest_name[k] = iso2codes[j];
                ++k;
            }
        }
        dest->count = k;
    } else {
        char buffer[0x2000] = {0};
        char *frmt = "%s";
        sprintf(buffer, frmt, PQgetvalue(res, 0, 0));
        size_t j = strlen(buffer);
        frmt = ",%s";
        for (size_t i = 1; i < rows; i++) {
            sprintf(&buffer[j], frmt, PQgetvalue(res, i, 0));
            j += strlen(&buffer[j]);
            dest->count++;
        }
        size_t sz = strlen(buffer) + 1;
        dest->data = malloc(sz * sizeof(char));
        check(dest->data, ERR_MEM, EMISS_ERR);
        memcpy(dest->data, buffer, sz);

    }
    atomic_flag_clear(&dest->in_progress);
    return;
error:
    exit(0);
}

static int
retrieve_country_data(emiss_resource_ctx_st *rsrc_ctx)
{
    /* First, retrieve row count for allocating a correct amount of memory. */
    int ret;
    char *cnt_qr = "SELECT count(*) FROM Country;";
    ret = psqldb_blocking_query(rsrc_ctx->conn_ctx, cnt_qr, NULL, NULL, 0,
        (psqldb_res_handler_ft *)callback_row_count_res_handler, rsrc_ctx);
    check(ret, ERR_FAIL, EMISS_ERR, "running a blocking db query");
    size_t ccount = rsrc_ctx->cdata->ccount;
    check(ccount, ERR_FAIL, EMISS_ERR,
        "retrieving country data: row count zero!");
    rsrc_ctx->cdata = calloc(ccount, sizeof(struct country_data));
    check(rsrc_ctx->cdata, ERR_MEM, EMISS_ERR);

    /* Then, get the data. A callback will enter the data into the array. */
    char buf[0x1000] = {0};
    char *cols = "code_iso_a3, code_iso_a2, name, region_id, income_id, "\
                    "is_independent, is_an_aggregate, in_tui_chart";
    sprintf(buf, "SELECT %s AS %s FROM %s ORDER BY Country.code_iso_a3", cols, cols, "Country");
    ret = psqldb_blocking_query(rsrc_ctx->conn_ctx, buf,
            NULL, NULL, 0, (psqldb_res_handler_ft *)
            callback_countrydata_res_handler,
            rsrc_ctx);
    check(ret, ERR_FAIL, EMISS_ERR, "running a blocking db query");
    check(rsrc_ctx->cdata[ccount - 1].name, ERR_FAIL, EMISS_ERR,
        "saving data to cdata array");

    return 1;
error:
    return 0;
}

static int
format_map_chart_data(emiss_template_st *template_data,
    struct result_storage_s *query_res, size_t ncountries,
    uint8_t dataset_id, uint8_t per_capita, unsigned year,
    void *cbdata)
{
    /*  Wait for data retrieval to complete. */
    volatile atomic_flag *not_ready = &query_res->in_progress;
    do {
        /* nothing */
    } while (atomic_flag_test_and_set(not_ready));

    /*  Allocate space for result string and grab pointers. */
    size_t count = query_res->count;
    char *countrydata = calloc((count * strlen(",{code:'XX',data:}")
                            + count * 0xF),
                            sizeof(char));
    check(countrydata, ERR_MEM, EMISS_ERR);
    char **iso2 = (char **)query_res->name;
    char (*data)[0xF] = (char (*)[0xF]) query_res->data;

    /*  Format data from iso2 and data to a single JSON array string. */
    size_t j = 0;
    for (size_t i = 0; i < count; i++) {
        JSON_DATA_ENTRY_BY_CODE(&countrydata[j], iso2[i], data[i], i);
        j += strlen(&countrydata[j]);
    }
    free(iso2);
    free(data);
    free(query_res);

    /*  Call provided output function. */
    const char *js = bdata(template_data->rsrc_ctx->template[2]);
    char title[80];
    sprintf(title, CHOOSE_MAP_CHART_TITLE_FRMT(dataset_id, per_capita), year);
    int ret = template_data->output_function(cbdata,
                200,"application/javascript", "close",
                js, countrydata, title);
    free(countrydata);
    return ret;
error:
    return 0;
}


static int
format_line_chart_data(emiss_template_st *template_data, unsigned year_start,
    unsigned year_end, struct result_storage_s **query_res, size_t nitems,
    size_t names_bytelen, uint8_t dataset_id, uint8_t per_capita, void *cbdata)
{
    if (year_start < EMISS_YEAR_ZERO) year_start = EMISS_YEAR_ZERO;
    if (year_end > EMISS_YEAR_LAST) year_end = EMISS_YEAR_LAST;
    if (year_end < year_start + 1) year_end = year_start + 1;

    emiss_resource_ctx_st *rsrc_ctx = template_data->rsrc_ctx;

    char *yeardata = calloc((1 + year_end - year_start) * 7, sizeof(char));
    check(yeardata, ERR_MEM, EMISS_ERR);
    char *years_formatted = rsrc_ctx->yeardata_formatted;
    memcpy(yeardata,
        &years_formatted[(1 + year_start - EMISS_YEAR_ZERO) * 7],
        ((1 + year_end - year_start) * 7) - 1
    );

    unsigned ndatapoints = (1 + year_end - year_start) * nitems;
    char *countrydata = calloc((names_bytelen
                            + nitems * strlen(",{name:'',data:[]}")
                            + ndatapoints * 0x10),
                            sizeof(char));
    check(countrydata, ERR_MEM, EMISS_ERR);
    size_t j = 0;
    for (size_t i = 0; i < nitems; i++) {
        volatile atomic_flag *not_ready = &query_res[i]->in_progress;
        do {
            /* nothing */
        } while (atomic_flag_test_and_set(not_ready));
        char *name = (char *)query_res[i]->name;
        char *data = (char *)query_res[i]->data;
        if (name && data) {
            char buf[64] = {0};
            name = inl_escape_single_quotes(buf, name);
            JSON_DATA_ENTRY_BY_NAME(&countrydata[j], name, data, i);
            j += strlen(&countrydata[j]);
            free(data);
        }
        free(query_res[i]);
    }
    free(query_res);
    const char *js = bdata(rsrc_ctx->template[1]);
    int ret = template_data->output_function(cbdata,
                200, "application/javascript", "close",
                js, LINE_CHART_PARAMS(yeardata, countrydata,
                dataset_id, per_capita));
    free(countrydata);
    free(yeardata);
    return ret;
error:
    return 0;
}

static int
retrieve_matching_data(emiss_template_st *template_data, unsigned from_year,
    unsigned to_year, uint8_t dataset, uint8_t per_capita, char *country_codes,
    size_t ncountries, void *cbdata)
{
    printf("template count: %d\n", template_data->template_count);
    emiss_resource_ctx_st *rsrc_ctx = template_data->rsrc_ctx;
    char buf[0xFF] = {0};
    char out[0xFF] = {0};
    char *col, *alias, *where, *tbl;
    col = CHOOSE_COL(dataset, per_capita);
    alias = CHOOSE_ALIAS(dataset, per_capita);
    where = CHOOSE_WHERE_CLAUSE(from_year, to_year);
    tbl = "Datapoint";

    /*  Enqueue non-blocking queries for the data values. Results will be
        parsed by a callback to a buffer struct, the address of which is passed
        forward formatting the data.
    */

    int ret;
    char (*iso3codes)[4] = rsrc_ctx->cdata->iso3;
    uint8_t map_chart = from_year == to_year ? 1 : 0;
    if (map_chart) {
        ncountries = rsrc_ctx->cdata->ccount;
        SQL_SELECT_WHERE(buf, out, col, alias, tbl, where, from_year);

        struct result_storage_s *res_dest = inl_init_result_storage_s();
        check(res_dest, ERR_FAIL, EMISS_ERR,
            "initializing result destination buffer");

        res_dest->data = rsrc_ctx->cdata;
        psqldb_query_data_st *qr_dt;
        qr_dt = psqldb_init_query_data(out, NULL, NULL, 0,
                    (psqldb_res_handler_ft *)callback_datapoint_res_handler,
                    res_dest, 0);
        check(qr_dt, ERR_FAIL, EMISS_ERR,
            "initializing query data structure");

        ret = psqldb_enqueue_query(rsrc_ctx->conn_ctx, qr_dt);
        check(ret, ERR_FAIL, EMISS_ERR, "enqueuing query to db");

        return format_map_chart_data(template_data,
            res_dest, ncountries, dataset,
            per_capita, from_year, cbdata);
    } else {
        struct result_storage_s **res_dest_arr;
        res_dest_arr = malloc(sizeof(struct result_storage_s *) * ncountries);
        check(res_dest_arr, ERR_MEM, EMISS_ERR);
        char **names = rsrc_ctx->cdata->name;
        size_t ccount = rsrc_ctx->cdata->ccount;
        size_t selected_names_byte_length = 0;
        char *ptr = country_codes;
        for (size_t i = 0; i < ncountries; i++) {
            ptr = strchr(ptr, '=') + 1;
            char code[4] = {0};
            memcpy(code, ptr, 3);
            SQL_SELECT_WHERE(buf, out,
                col, alias, tbl, where,
                code, from_year, to_year);

            res_dest_arr[i] = inl_init_result_storage_s();
            check(res_dest_arr[i], ERR_FAIL, EMISS_ERR,
                "initializing result destination buffer");

            psqldb_query_data_st *qr_dt;
            qr_dt = psqldb_init_query_data(out, NULL, NULL, 0,
                        (psqldb_res_handler_ft *)callback_datapoint_res_handler,
                        res_dest_arr[i], 0);
            check(qr_dt, ERR_FAIL, EMISS_ERR,
                "initializing query data structure");

            ret = psqldb_enqueue_query(rsrc_ctx->conn_ctx, qr_dt);
            check(ret, ERR_FAIL, EMISS_ERR, "enqueuing query to db");

            ret = inl_binary_search_str_arr(ccount, 4, iso3codes, code);
            if (ret != -1) {
                res_dest_arr[i]->name = names[ret];
                selected_names_byte_length += strlen(names[ret]);
            } else {
                log_warn(ERR_FAIL_A, EMISS_ERR,
                    "finding name for requested code", code);
            }
        }
        return format_line_chart_data(template_data,
            from_year, to_year, res_dest_arr,
            ncountries, selected_names_byte_length,
            dataset, per_capita, cbdata);
    }
error:
    return 0;
}

static int
forward_to_format(emiss_template_st *template_data, size_t i,
    const char *qstr, void *cbdata)
{
    uint8_t dataset = strstr(qstr, "co2e") ? DATASET_CO2E : DATASET_POPT;
    uint8_t per_capita = dataset == DATASET_CO2E && strstr(qstr, "co2e_percapita") ? 1 : 0;
    /*  Parse years from query string. */
    char *from_year_str = strstr(qstr, "from_year");
    if (from_year_str) {
        unsigned from_year = (unsigned) strtoul(strchr(from_year_str, '=') + 1, NULL, 10);
        char *to_year_str = strstr(from_year_str, "to_year");
        unsigned to_year = from_year + 1;
        if (to_year_str) {
            to_year = (unsigned) strtoul(strchr(to_year_str, '=') + 1, NULL, 10);
        }
        char *count_str = strstr(qstr, "count");
        unsigned count = 0;
        if (!count_str) {
            char *ccode = strstr(qstr, "ccode");
            while (ccode) {
                ++count;
                ccode = strstr(ccode, "ccode");
            }
        } else {
            count = (unsigned) strtoul(strchr(count_str, '=') + 1, NULL, 10);
        }
        retrieve_matching_data(template_data,
            from_year, to_year, dataset,
            per_capita, strstr(count_str, "ccode"),
            count, cbdata);
    } else {
        char *select_year_str = strstr(qstr, "select_year");
        unsigned select_year = EMISS_YEAR_ZERO;
        if (select_year_str) {
            select_year = (unsigned) strtoul(strchr(select_year_str, '=') + 1, NULL, 10);
        }
        retrieve_matching_data(template_data,
            select_year, select_year, dataset,
            per_capita, NULL, 0, cbdata);
    }
    return 1;
}

static int
format_chart_html(emiss_template_st *template_data,
    size_t i, const char *qstr, void *cbdata)
{
    const char *script_src = strstr(qstr, "line")
                                ? "line_chart.js"
                                : "map_chart.js";
    char *template = bdata(template_data->rsrc_ctx->template[i]);
    template_data->output_function(cbdata, 200, "text/html",
        "close", template, EMISS_ABS_ROOT_URL,
        script_src, qstr);
    return 0;
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
    memcpy(template_data->template_name[1], "line", 4);
    memcpy(template_data->template_name[2], "map", 3);
    template_data->template_function[0] = format_chart_html;
    template_data->template_function[1] = forward_to_format;
    template_data->template_function[2] = forward_to_format;
    return template_data;
error:
    return NULL;
}

char *
emiss_get_static_resource(emiss_resource_ctx_st *rsrc_ctx, size_t i)
{
    if (rsrc_ctx && i < EMISS_NSTATICS) {
        return bdata(rsrc_ctx->static_resource[i]);
    }
    return NULL;
}

emiss_resource_ctx_st *
emiss_init_resource_ctx()
{
    emiss_resource_ctx_st *rsrc_ctx = malloc(sizeof(emiss_resource_ctx_st));
    check(rsrc_ctx, ERR_MEM, EMISS_ERR);

    rsrc_ctx->conn_ctx = psqldb_init_conn_ctx(NULL);
    check(rsrc_ctx->conn_ctx, ERR_FAIL, EMISS_ERR, "initializing resources: unable to init db");

    rsrc_ctx->cdata = calloc(1, sizeof(struct country_data));
    check(rsrc_ctx->cdata, ERR_MEM, EMISS_ERR);
    int ret = retrieve_country_data(rsrc_ctx);
    check(ret, ERR_FAIL, EMISS_ERR, "initializing resources: unable to retrieve country data");

    FILL_YEARDATA(rsrc_ctx->yeardata_formatted);

    rsrc_ctx->static_resource[0] = inl_read_file_to_bstring(EMISS_HTML_ROOT "/index.html");
    bstring new_html = inl_read_file_to_bstring(EMISS_HTML_ROOT "/new.html");
    rsrc_ctx->static_resource[1] = inl_format_new_chart_html(rsrc_ctx->cdata, bdata(new_html));
    bdestroy(new_html);
    bstring chart_params_js = inl_read_file_to_bstring(EMISS_JS_ROOT "/chart_params.js");
    rsrc_ctx->static_resource[2] = inl_format_chart_params_js(bdata(chart_params_js));
    bdestroy(chart_params_js);

    rsrc_ctx->template[0] = inl_read_file_to_bstring(EMISS_HTML_ROOT "/show.html");
    rsrc_ctx->template[1] = inl_read_file_to_bstring(EMISS_JS_ROOT "/line_chart.js");
    rsrc_ctx->template[2] = inl_read_file_to_bstring(EMISS_JS_ROOT "/map_chart.js");

    psqldb_launch_conn_threads(rsrc_ctx->conn_ctx);
    return rsrc_ctx;
error:
    return NULL;
}

void
emiss_free_resource_ctx(emiss_resource_ctx_st *rsrc_ctx)
{
    if (rsrc_ctx) {
        if (rsrc_ctx->conn_ctx) {
            psqldb_free_conn_ctx(rsrc_ctx->conn_ctx);
        }
        char **cdata_name = rsrc_ctx->cdata->name;
        unsigned count = rsrc_ctx->cdata->ccount;
        for (size_t i = 0; i < count; i++) {
            if (cdata_name[i]) {
                free(cdata_name[i]);
            }
        }
        free(rsrc_ctx->cdata);
        bstring *rsrc = rsrc_ctx->static_resource;
        for (size_t i = 0; i < EMISS_NSTATICS; i++) {
            if (rsrc[i]) {
                bdestroy(rsrc[i]);
            }
        }
        bstring *template = rsrc_ctx->template;
        for (size_t i = 0; i < EMISS_NTEMPLATES; i++) {
            if (template[i]) {
                bdestroy(template[i]);
            }
        }
        free(rsrc_ctx);
    }
}
