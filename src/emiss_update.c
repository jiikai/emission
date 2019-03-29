/*! @file       emiss_update.c
    @brief      Part of the implementation of [Emission](../include/emiss.h).
    @details    See [documentation](../doc/emiss_api.md).
    @copyright: (c) Joa Käis [github.com/jiikai] 2018-2019, [MIT](../LICENSE).
*/

/*
**  INCLUDES
*/

#include "emiss.h"
#include <math.h>
#include <pthread.h>
#include "uthash.h"
#include "util_sql.h"

/*
** MACROS
*/

#define NCALLBACKS 10

#define DATA_COLUMN_NAME(dataset_id)\
    dataset_id == DATASET_CO2E ? "emission_kt" :\
    dataset_id == DATASET_POPT ? "population_total"\
    : ""

/*
**  STRUCTURES AND TYPES
*/

/*  A hash table structure to use with uthash.h hash macros.
    For ISO country codes from country_codes.csv for later access when parsing
    the Worldbank indicator data files. */
typedef struct ht_country_code {
    char                        iso3[4];
    char                        iso2[3];
    uint8_t                     is_independent;
    uint8_t                     in_tui_chart;
    UT_hash_handle              hh;
} ht_country_code_st;

/*  Context structure type emiss_update_ctx_st definition, housing a csv parser wrapper,
    database connection context pointer, callback data buffer, a hash table of
    the type defined above (ht_country_code_st) with a count of its items, plus
    an identifier for the current dataset. */
struct emiss_update_ctx {
    wlcsv_ctx_st               *lcsv_ctx;
    wlcsv_state_st             *lcsv_stt;
    char                       *cbdata;
    size_t                      cbdata_max_size;
    ht_country_code_st         *ccodes;
    int                         ccount;
    wlpq_conn_ctx_st           *conn_ctx;
    uint8_t                     callback_ids[NCALLBACKS];
    uint8_t                     conn_ctx_free_after_use;
    uint8_t                     dataset_id;
    uint8_t                     countries_updated;
    char                      (*tui_chart_worldmap_data)[3];
    int                         tui_chart_worldmap_ccount;
};

/*
**  FUNCTION DEFINITIONS
*/

/*  STATIC */

static inline int
binary_search_str_arr(int count, size_t el_size, char (*data)[el_size], char *key)
{
    int l = 0,
        r = count - 1;
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
cb_codes_independency_status(void *field, size_t len, void* data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    const char *str = (const char *)field;
    char *iso3 = upd_ctx->cbdata;
    ht_country_code_st *entry;
    HASH_FIND(hh, upd_ctx->ccodes, iso3, len, entry);
    if (entry)
        entry->is_independent = !strncmp(str, "Yes", 3) ? 1 : 0;
}

static void
cb_codes_iso_a2(void *field, size_t len, void *data)
{
    if (len > 2)
        return;
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    const char *str              = (const char *)field;
    char *iso3                   = upd_ctx->cbdata;
    ht_country_code_st *entry;
    HASH_FIND(hh, upd_ctx->ccodes, iso3, 3, entry);
    if (entry) {
        memcpy(entry->iso2, str, len);
        memset(upd_ctx->cbdata, 0, 3);
        char *ret = bsearch(str, upd_ctx->tui_chart_worldmap_data,
                        upd_ctx->tui_chart_worldmap_ccount, 3,
                        (emiss_compar_ft *)strcmp);
        entry->in_tui_chart = ret ? 1 : 0;
    }
}

static void
cb_codes_iso_a3(void *field, size_t len, void *data)
{
    if (len > 3)
        return;
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    const char *str = (const char *)field;
    ht_country_code_st *entry = calloc(1, sizeof(struct ht_country_code));
    check(entry, ERR_MEM, EMISS_ERR);
    memcpy(entry->iso3, str, len);
    HASH_ADD(hh, upd_ctx->ccodes, iso3, len, entry);
    memcpy(upd_ctx->cbdata, str, len);
    return;
error:
    exit(0);
}

static void
cb_codes_data_header(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx    = (emiss_update_ctx_st *)data;
    const char  *str                = (const char *)field,
                *substr             = "ISO3166-1-Alpha";
    char *end                       = strstr(str, substr);
    unsigned col                    = WLCSV_STATE_MEMB_GET(upd_ctx->lcsv_stt, col);
    uint8_t *callback_ids           = upd_ctx->callback_ids;
    if (end) {
        if (!callback_ids[1] && strstr(&end[strlen(substr)], "3"))
            callback_ids[1] = wlcsv_callbacks_set(upd_ctx->lcsv_ctx,
                                    COLUMN, WLCSV_MATCH_NUM(col),
                                    cb_codes_iso_a3, upd_ctx, 0);
        else if (!callback_ids[2])
            callback_ids[2] = wlcsv_callbacks_set(upd_ctx->lcsv_ctx,
                                    COLUMN, WLCSV_MATCH_NUM(col),
                                    cb_codes_iso_a2, upd_ctx, 0);
    } else if (!callback_ids[3] && strstr(str, "independent"))
        callback_ids[3] = wlcsv_callbacks_set(upd_ctx->lcsv_ctx,
                                COLUMN, WLCSV_MATCH_NUM(col),
                                cb_codes_independency_status, upd_ctx, 0);
    if (callback_ids[1] && callback_ids[2] && callback_ids[3]) {
        wlcsv_callbacks_toggle(upd_ctx->lcsv_ctx, callback_ids[0]);
        callback_ids[0] = 0;
    }
}

static void
cb_country(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    if (!field || !len || WLCSV_STATE_MEMB_GET(upd_ctx->lcsv_stt, row) < 1)
        return;

    char buf[0x1000];
    char *str = (char *)field, *tmp = upd_ctx->cbdata;
    unsigned current_col = WLCSV_STATE_MEMB_GET(upd_ctx->lcsv_stt, col);
    uint8_t dataset_id = upd_ctx->dataset_id;
    if (dataset_id != DATASET_META) {
        if (!upd_ctx->countries_updated) {
            size_t tmp_len = strlen(tmp);
            if (current_col == 1 && tmp_len) {
                const char *cols, *vals;
                char insert_sql[0x1000];
                ht_country_code_st *ccode_entry;
                HASH_FIND(hh, upd_ctx->ccodes, str, 3, ccode_entry);
                if (ccode_entry) {
                    cols = "code_iso_a3, code_iso_a2, name, is_independent, in_tui_chart";
                    vals = "'%s', '%s', $$%s$$, %s, %s";
                    char *iso2 = ccode_entry->iso2;
                    const char  *independent = ccode_entry->is_independent ? "TRUE" : "FALSE",
                                *in_tuichart = ccode_entry->in_tui_chart ? "TRUE" : "FALSE";

                    check(SQL_INSERT_INTO(buf, 0xFFF, insert_sql, 0xFFF, "Country",
                        cols, vals, str, iso2, tmp, independent,
                        in_tuichart) >= 0, ERR_FAIL, EMISS_ERR,
                        "printf'ing to buffer");
                } else {
                    cols = "code_iso_a3, name, in_tui_chart";
                    vals = "'%s', $$%s$$, FALSE";
                    check(SQL_INSERT_INTO(buf, 0xFFF, insert_sql, 0xFFF, "Country",
                    cols, vals, str, tmp) >= 0, ERR_FAIL, EMISS_ERR,
                    "printf'ing to buffer");
                }
                insert_sql[strlen(insert_sql) - 1] = '\0';
                memset(buf, 0, sizeof(buf));
                check(SQL_INSERT_IF_NCONFLICT(buf, 0xFFF, insert_sql, "code_iso_a3") >= 0,
                        ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
				wlpq_query_data_st *query_data = wlpq_query_init(buf, 0, 0, 0, 0, 0, 1);
				check(query_data, ERR_FAIL, EMISS_ERR, "creating query struct");
				check(wlpq_query_queue_enqueue(upd_ctx->conn_ctx, query_data),
                    ERR_FAIL, EMISS_ERR, "appending to db job queue");
                memset(tmp, 0, tmp_len);
                memcpy(tmp, str, 3);
            } else
                memcpy(tmp, str, len);

        } else if (current_col == 1)
            memcpy(tmp, str, len);

    } else if (len) {
        char out[0x1000];
        if (!current_col)
            memcpy(tmp, str, len);
        else if (current_col == 1) {
            check(SQL_WITH_SELECT_WHERE(buf, 0xFFF, out, 0xFFF,
                "region_t", "id", "id", "Region", "name=$$%s$$",
                str) >= 0, ERR_FAIL, EMISS_ERR,
                "printf'ing to buffer");
            strcat(tmp, out);
        } else {
            check(SQL_APPEND_WITH_SELECT_WHERE(buf, 0xFFF, out, 0xFFF,
                (tmp + 3), "income_t", "id", "id", "IncomeGroup",
                "name=$$%s$$", str) >= 0, ERR_FAIL, EMISS_ERR,
                "printf'ing to buffer");

            memcpy(tmp + 3, out, strlen(out));
        }
    }
    return;
error:
// ADD CLEANUP ROUTINES
    exit(0);
}

static void
cb_data(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    if ((!field && !len) || WLCSV_STATE_MEMB_GET(upd_ctx->lcsv_stt, row) < 1)
        return;

    char buf[0x2000], out[0x2000];
    char *str = field ? (char *)field : "",
         *tmp = upd_ctx->cbdata;

    unsigned current_col = WLCSV_STATE_MEMB_GET(upd_ctx->lcsv_stt, col);
    uint8_t dataset_id = upd_ctx->dataset_id;
	int year = EMISS_DATA_STARTS_FROM + (current_col - 4);
	wlpq_query_data_st *query_data;
	const char *set;
    if (dataset_id == DATASET_META) {
		if (current_col > 3)
			return;
        const char *where;
        size_t tmp_len = strlen(tmp);
		if (tmp_len > 3) {
            char country_code[4] = {0};
            memcpy(&country_code, tmp, 3);
            set     = "region_id=(SELECT id FROM region_t), "\
                      "income_id=(SELECT id FROM income_t), "\
                      "is_an_aggregate=FALSE, metadata=$$%s$$";
            where   = "code_iso_a3='%s'";
            check(SQL_UPDATE_WITH_WHERE(buf, 0x1FFF, out, 0x1FFF,
                (tmp + 3), "Country", set, where, str, country_code) >= 0,
                ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
            memset(tmp, 0, tmp_len);
        } else if (tmp_len == 3) {
            set     = "is_an_aggregate=TRUE, metadata=$$%s$$";
            where   = "code_iso_a3='%s'";
            check(SQL_UPDATE_WHERE(buf, 0x1FFF, out, 0x1FFF,
                "Country", set, where, str, tmp) >= 0,
                ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
            memset(tmp, 0, tmp_len);
        } else
            return;
		query_data = wlpq_query_init(out, 0, 0, 0, 0, 0, 0);
    } else if (year >= EMISS_YEAR_ZERO && year <= EMISS_YEAR_LAST) {
        const char *table   = "Datapoint",
                   *cols    = "country_code, yeardata_year, %s",
                   *vals    = "'%s', %d, %s",
                   *arbiter = "country_code, yeardata_year",
                   *set     = "%s=%s";
        /*  arbiter: a conflict fires an update instead of insert
            if a datapoint for this country and year already exists. */
        char insert_sql[0x100];
        check(SQL_INSERT_INTO(buf, 0x1FFF, insert_sql, 0xFF,
            table, cols, vals, DATA_COLUMN_NAME(dataset_id),
            tmp, year, str) >= 0, ERR_FAIL, EMISS_ERR,
            "printf'ing to buffer");
        /*  Remove end-of-query semicolon. */
        insert_sql[strlen(insert_sql) - 1] = '\0';
        memset(buf, 0, sizeof(buf));
        check(SQL_UPSERT(buf, 0x1FFF, out, 0x1FFF, insert_sql,
            arbiter, set, DATA_COLUMN_NAME(dataset_id), str) >= 0,
            ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
		query_data = wlpq_query_init(out, 0, 0, 0, 0, 0, 0);

    } else
		return;

	check(query_data, ERR_FAIL, EMISS_ERR, "creating query data struct");
	check(wlpq_query_queue_enqueue(upd_ctx->conn_ctx, query_data),
            ERR_FAIL, EMISS_ERR, "appending to db job queue");
    return;

error:
    exit(0);
}

static void
cb_year(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    const char *str = (const char *)field;
    if (upd_ctx->dataset_id == DATASET_POPT) {
        printf("%s\n", str);
    }
    int year = atoi(str);
    if (upd_ctx->dataset_id == 1 && (year >= EMISS_YEAR_ZERO && year <= EMISS_YEAR_LAST)) {
        char buf[0x100], insert_sql[0x100];
        check(SQL_INSERT_INTO(buf, 0xFF, insert_sql, 0xFF, "YearData", "year", "%s", str) >= 0,
                ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
        insert_sql[strlen(insert_sql) - 1] = '\0';
        memset(buf, 0, sizeof(buf));
        check(SQL_INSERT_IF_NCONFLICT(buf, 0xFF, insert_sql, "year") >= 0,
                ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
		wlpq_query_data_st *query_data = wlpq_query_init(buf, 0, 0, 0, 0, 0, 1);
		check(query_data, ERR_FAIL, EMISS_ERR, "creating query data struct");
		check(wlpq_query_queue_enqueue(upd_ctx->conn_ctx, query_data), ERR_FAIL, EMISS_ERR, "appending to db job queue");
    }
    return;
error:
    exit(0);
}

static void
cb_preview(void *field, size_t len, void *data)
{
    if (len) {
        emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
        char *str = (char *)field;
        char *tmp = upd_ctx->cbdata;
        if (strstr(str, "Last Updated"))
            memcpy(tmp, str, strlen(str));
        else if (tmp && strstr(tmp, "Last Updated")) {
            memset(tmp, 0, strlen(tmp));
            memcpy(tmp, str, strlen(str));
        }
    }
}

static void
eor_flush_cbdata_buffer(void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    memset(upd_ctx->cbdata, 0, strlen(upd_ctx->cbdata));
}

static void
eor_wait_until_queries_done(void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    wlcsv_ctx_st *wlcsv_ctx = upd_ctx->lcsv_ctx;
    if (!upd_ctx->callback_ids[1])
        upd_ctx->callback_ids[1] = wlcsv_callbacks_set(wlcsv_ctx,
                                        COLUMN, WLCSV_MATCH_NUM(0U),
                                        cb_country, upd_ctx, 0);
    if (!upd_ctx->callback_ids[2])
        upd_ctx->callback_ids[2] = wlcsv_callbacks_set(wlcsv_ctx,
                                    COLUMN, WLCSV_MATCH_NUM(1U),
                                    cb_country, upd_ctx, 0);
    if (upd_ctx->dataset_id == DATASET_META)
        upd_ctx->callback_ids[5] = wlcsv_callbacks_set(wlcsv_ctx,
                                        COLUMN, WLCSV_MATCH_NUM(2U),
                                        cb_country, upd_ctx, 0);

    wlcsv_callbacks_eor_set(wlcsv_ctx, eor_flush_cbdata_buffer);
    memset(upd_ctx->cbdata, 0, upd_ctx->cbdata_max_size);
}

static time_t
parse_last_updated_date(char *date_str)
{
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));
    char *ret = strptime(date_str, "%Y-%m-%d", &tm);
    check(ret, ERR_FAIL, EMISS_ERR, "parsing the date string");
    time_t time = mktime(&tm);
    check(time != -1, ERR_FAIL, EMISS_ERR, "converting tm to time_t");
    return time;
error:
    return -1;
}

static int
read_tui_chart_worldmap_data(emiss_update_ctx_st *upd_ctx, const char *data_path)
{
    FILE *fp = fopen(data_path, "r");
    check(fp, ERR_FAIL, EMISS_ERR, "opening file");
    rewind(fp);
    char iso2_code_buf[4] = {0};
    upd_ctx->tui_chart_worldmap_data = calloc(NCOUNTRY_DATA_SLOTS, sizeof(char (*)[3]));
    check(upd_ctx->tui_chart_worldmap_data, ERR_MEM, EMISS_ERR);
    char (*worldmap_codes_arr)[3] = upd_ctx->tui_chart_worldmap_data;
    size_t i = 0;
    while (fread(iso2_code_buf, 1, 3, fp)) {
        memcpy(worldmap_codes_arr[i], iso2_code_buf, 2);
        ++i;
    }
    check(feof(fp), ERR_FAIL, EMISS_ERR, "reading file");
    fclose(fp);
    upd_ctx->tui_chart_worldmap_ccount = i;
    upd_ctx->tui_chart_worldmap_data = worldmap_codes_arr;
    return 1;
error:
    if (fp)
        fclose(fp);
    if (upd_ctx->tui_chart_worldmap_data)
        free(upd_ctx->tui_chart_worldmap_data);
    return 0;
}

void
emiss_update_ctx_free(emiss_update_ctx_st *upd_ctx)
{
    if (upd_ctx) {
        wlcsv_free(upd_ctx->lcsv_ctx);
		if (upd_ctx->conn_ctx_free_after_use && upd_ctx->conn_ctx)
			wlpq_conn_ctx_free(upd_ctx->conn_ctx);
        if (upd_ctx->ccodes) {
            ht_country_code_st *current, *tmp;
            HASH_ITER(hh, upd_ctx->ccodes, current, tmp) {
                HASH_DELETE(hh, upd_ctx->ccodes, current);
                free(current);
            }
        }
        if (upd_ctx->cbdata)
            free(upd_ctx->cbdata);
        if (upd_ctx->tui_chart_worldmap_data)
            free(upd_ctx->tui_chart_worldmap_data);
		free(upd_ctx);
    }
}

emiss_update_ctx_st *
emiss_update_ctx_init(wlpq_conn_ctx_st *conn_ctx, const char *tui_chart_data)
{
    emiss_update_ctx_st *upd_ctx = calloc(1, sizeof(emiss_update_ctx_st));
    check(upd_ctx, ERR_MEM, EMISS_ERR);

    upd_ctx->conn_ctx = conn_ctx ? conn_ctx : wlpq_conn_ctx_init(0);
    check(upd_ctx->conn_ctx, ERR_FAIL, EMISS_ERR, "setting up database context");
    upd_ctx->conn_ctx_free_after_use = conn_ctx ? 0 : 1;

    check(read_tui_chart_worldmap_data(upd_ctx, tui_chart_data),
            ERR_FAIL, EMISS_ERR, "reading tui.chart worldmap data from file");

    upd_ctx->lcsv_ctx = wlcsv_init(0, 0, upd_ctx, 1, 0, 3, 4, 0, WLCSV_IGNORE_EMPTY_FIELDS);
    check(upd_ctx->conn_ctx, ERR_FAIL, EMISS_ERR, "initializing libcsv wrapper structure");
    upd_ctx->lcsv_stt = wlcsv_state_get(upd_ctx->lcsv_ctx);
    upd_ctx->ccodes   = 0;

    return upd_ctx;
error:
    if (upd_ctx)
        emiss_update_ctx_free(upd_ctx);
    return 0;
}

size_t
emiss_update_parse_send(emiss_update_ctx_st *upd_ctx,
    char **paths, uintmax_t *file_sizes, size_t npaths,
    int *dataset_ids, time_t current_version)
{
    printf("%p\n", (void *) upd_ctx->lcsv_ctx);
    for (size_t i = 0; i < npaths; i++) {
        printf("%s\n", paths[i]);
        printf("%d\n", dataset_ids[i]);
    }
    if (!wlpq_threads_active(upd_ctx->conn_ctx))
        wlpq_threads_launch_async(upd_ctx->conn_ctx);
    upd_ctx->cbdata_max_size = 0x666;
    upd_ctx->cbdata   = calloc(0x666, sizeof(char));
    check(upd_ctx->cbdata, ERR_MEM, EMISS_ERR);

    const uintmax_t default_sz  = 0x100000;
    wlcsv_ctx_st *wlcsv_ctx     = upd_ctx->lcsv_ctx;
    wlcsv_state_st *wlcsv_stt   = upd_ctx->lcsv_stt;
    uint8_t *callback_ids       = upd_ctx->callback_ids;
    callback_ids[0]             = wlcsv_callbacks_set(wlcsv_ctx,
                                        ROW, WLCSV_MATCH_NUM(0U),
                                        cb_codes_data_header, upd_ctx, 0);

    int ret = wlcsv_file_path(wlcsv_ctx, paths[0], strlen(paths[0]));
    check(ret, ERR_EXTERN, "wlcsv", "setting file path");
    ret = wlcsv_file_read(wlcsv_ctx, file_sizes[0] ? file_sizes[0] + 10 : default_sz);
    check(ret, ERR_EXTERN, "wlcsv", "reading csv file");
    wlcsv_callbacks_clear_all(wlcsv_ctx);
    memset(callback_ids, 0, NCALLBACKS);

    /*  Set up some settings and callbacks for the Worldbank files. */
    wlcsv_state_lineskip_set(wlcsv_stt, 4);
    ret = wlcsv_ignore_regex_set(wlcsv_ctx, EMISS_IGNORE_REGEX);
    check(ret, ERR_EXTERN, "wlcsv", "setting ignore regex");


    callback_ids[0] = wlcsv_callbacks_set(wlcsv_ctx,
                            ROW, WLCSV_MATCH_NUM(0U),
                            cb_year, upd_ctx, 0);
    ret = 0;
    size_t retval = 0;
    for (size_t i = 1; i < npaths; ++i) {
        wlcsv_callbacks_eor_set(wlcsv_ctx, eor_wait_until_queries_done);
        wlcsv_callbacks_default_set(wlcsv_ctx, cb_data, upd_ctx);
        check(wlcsv_file_path(wlcsv_ctx, paths[i], strlen(paths[i])),
                ERR_EXTERN, WLCSV, "setting file path");
        upd_ctx->dataset_id = dataset_ids[i];
        if (dataset_ids[i] != DATASET_META) {
            if (current_version) {
                ret = wlcsv_file_preview(wlcsv_ctx, 3, 0x10000, cb_preview);
                check(ret, ERR_EXTERN, WLCSV, "obtaining preview of csv file");
    			char *temp = upd_ctx->cbdata;
                time_t updated = temp && strlen(temp)
                                ? parse_last_updated_date(temp)
                                : 0;
                if (updated && (updated <= current_version))
                    continue;
            }
            ret = wlcsv_file_read(wlcsv_ctx, file_sizes[i] ?
                        file_sizes[i] + 10 : default_sz);
            check(ret, ERR_EXTERN, WLCSV, "reading csv file");
            if (!upd_ctx->countries_updated)
                upd_ctx->countries_updated = 1;
        } else {
            wlcsv_state_lineskip_set(wlcsv_stt, 0);
            wlcsv_state_options_set(wlcsv_stt, WLCSV_IGNORE_EMPTY_FIELDS);
            ret = wlcsv_file_read(wlcsv_ctx, file_sizes[i]);
            check(ret, ERR_EXTERN, WLCSV, "reading csv file");
            wlcsv_callbacks_clear(wlcsv_ctx, callback_ids[5]);
            callback_ids[5] = 0;
            wlcsv_state_lineskip_set(wlcsv_stt, 4);
            wlcsv_state_options_set(wlcsv_stt, WLCSV_IGNORE_EMPTY_FIELDS);
        }
        if (callback_ids[0]) {
            wlcsv_callbacks_clear(wlcsv_ctx, callback_ids[0]);
            callback_ids[0] = 0;
        }
        struct timespec timer = (struct timespec){.tv_sec = 1, .tv_nsec = 0};
        while (!wlpq_query_queue_empty(upd_ctx->conn_ctx))
            nanosleep(&timer, NULL);
        retval += ret;
    }
    emiss_update_ctx_free(upd_ctx);
    return retval;
error:
	emiss_update_ctx_free(upd_ctx);
    return -1;
}
