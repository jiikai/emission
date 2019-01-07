/*
**  NAME:       emiss_update.c
**  PURPOSE:    part of the implementation of emiss.h
*/

/*
**  INCLUDES
*/

#include "emiss.h"

#include <math.h>
#include "lcsv_w.h"

/*
**  STRUCTURE/TYPE DEFINITIONS
*/

/*  A hash table structure to use with uthash.h hash macros.
    For ISO country codes from country_codes.csv for later access when parsing
    the Worldbank indicator data files.
*/
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
    an identifier for the current dataset.
*/
struct emiss_update_ctx {
    struct lcsv_w_ctx          *lcsv_ctx;
    struct psqldb_conn_ctx     *conn_ctx;
    char                       *cbdata;
    size_t                      cbdata_max_size;
    ht_country_code_st         *ccodes;
    int                         ccount;
    uint8_t                     dataset_id;
    char                      (*tui_chart_worldmap_data)[3];
    int                         tui_chart_worldmap_ccount;
};

/*
**  FUNCTION DEFINITIONS
*/

/*  STATIC */

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

static void
callback_flush_callback_data_buffer(void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    memset(upd_ctx->cbdata, 0, upd_ctx->cbdata_max_size);
}

static void
callback_wait_until_queries_done(void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
	psqldb_wait_on_threads_until_idle(upd_ctx->conn_ctx);
    lcsv_w_set_end_of_row_callback(upd_ctx->lcsv_ctx,
        callback_flush_callback_data_buffer);
    memset(upd_ctx->cbdata, 0, upd_ctx->cbdata_max_size);
}

static void
callback_country_codes_independency_status(void *field, size_t len, void* data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    char *str = (char *)field;
    char *iso3 = upd_ctx->cbdata;
    ht_country_code_st *entry;
    HASH_FIND(hh, upd_ctx->ccodes, iso3, len, entry);
    if (entry) {
        entry->is_independent = !strcmp(str, "Yes") ? 1 : 0;
    }
}

static void
callback_country_codes_iso_a2(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    char *str = (char *)field;
    char *iso3 = upd_ctx->cbdata;
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
callback_country_codes_iso_a3(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    char *str = (char *)field;
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
callback_country_code_data_header(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    char *str = (char *)field;
    const char *substr = "ISO3166-1-Alpha";
    char *end = strstr(str, substr);
    struct lcsv_w_ctx *lcsv_w = upd_ctx->lcsv_ctx;
    int col = lcsv_w_get_col(lcsv_w);
    if (end) {
        if (strstr(&end[strlen(substr)], "3")) {
            lcsv_w_set_callback_by_column(lcsv_w,
                col, callback_country_codes_iso_a3);
        } else {
            lcsv_w_set_callback_by_column(lcsv_w,
                col, callback_country_codes_iso_a2);
        }
    } else if (strstr(str, "independent")) {
        lcsv_w_set_callback_by_column(lcsv_w,
            col, callback_country_codes_independency_status);
    }
}


static void
callback_year(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    char *str = (char *)field;
    int year = atoi(str);
    if (upd_ctx->dataset_id == 1
    && (year >= EMISS_YEAR_ZERO && year <= EMISS_YEAR_LAST)) {
        char buf[0xFF];
        char out[0xFF];
        SQL_INSERT_INTO(buf, out, "YearData", "year", "%s", str);
        char *query = calloc(strlen(out) + 1, sizeof(char));
        check(query, ERR_MEM, EMISS_ERR);
        memcpy(query, &out, strlen(out));
        psqldb_query_data_st *query_data;
		query_data = psqldb_init_query_data(query,
                        NULL, NULL, 0, NULL, NULL, 0);
		check(query_data, ERR_FAIL, EMISS_ERR, "creating query data struct");
        int ret = psqldb_enqueue_query(upd_ctx->conn_ctx, query_data);
		check(ret, ERR_FAIL, EMISS_ERR, "appending to db job queue");
        free(query);
    }
    return;
error:
// ADD CLEANUP ROUTINES
    exit(0);
}

static void
callback_country(void *field, size_t len, void *data)
{
    if (!field && !len) {
        return;
    }
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    struct lcsv_w_ctx *lcsv_w = upd_ctx->lcsv_ctx;
    char buf[0xFFF];
    char out[0xFFF];
    char *str = (char *)field;
    char *tmp = upd_ctx->cbdata;

    int current_col = lcsv_w_get_col(lcsv_w);
    uint8_t dataset_id = upd_ctx->dataset_id;
    if (dataset_id != DATASET_META) {
        if (dataset_id == DATASET_CO2E) {
            if (current_col == 1 && strlen(tmp)) {
                char *cols, *vals;
                ht_country_code_st *ccode_entry;
                HASH_FIND(hh, upd_ctx->ccodes, str, 3, ccode_entry);
                if (ccode_entry) {
                    cols = "code_iso_a3, code_iso_a2, name, is_independent, in_tui_chart";
                    vals = "'%s', '%s', $$%s$$, %s, %s";
                    char *iso2 = ccode_entry->iso2;
                    char *independent = ccode_entry->is_independent
                                        ? "TRUE"
                                        : "FALSE";
                    char *in_tui_chart = ccode_entry->in_tui_chart
                                        ? "TRUE"
                                        : "FALSE";
                    SQL_INSERT_INTO(buf, out,
                        "Country", cols, vals,
                        str, iso2, tmp,
                        independent,
                        in_tui_chart);
                } else {
                    cols = "code_iso_a3, name, in_tui_chart";
                    vals = "'%s', $$%s$$, FALSE";
                    SQL_INSERT_INTO(buf, out,
                        "Country", cols, vals,
                        str, tmp);
                }
                char *query = calloc(strlen(out) + 1, sizeof(char));
                check(query, ERR_MEM, EMISS_ERR);
                memcpy(query, &out, strlen(out));
				psqldb_query_data_st *query_data;
				query_data = psqldb_init_query_data(query,
                                NULL, NULL, 0, NULL, NULL, 1);
				check(query_data, ERR_FAIL, EMISS_ERR,
                    "creating query data struct");
				int ret = psqldb_enqueue_query(upd_ctx->conn_ctx, query_data);
				check(ret, ERR_FAIL, EMISS_ERR, "appending to db job queue");
                memset(tmp, 0, strlen(tmp));
                memcpy(tmp, str, 3);
                free(query);
            } else {
                memcpy(tmp, str, len);
            }
        } else if (current_col == 1) {
            memcpy(tmp, str, len);
        }
    } else if (strlen(str)) {
        if (current_col == 0) {
            memcpy(tmp, str, len);
        } else if (current_col == 1) {
            SQL_WITH_SELECT_WHERE(buf,
                out, "region_t", "id", "id",
                "Region", "name=$$%s$$", str);
            strcat(tmp, out);
        } else {
            SQL_APPEND_WITH_SELECT_WHERE(buf,
                out, (tmp + 3), "income_t", "id",
                "id", "IncomeGroup", "name=$$%s$$", str);
            memcpy(tmp + 3, out, strlen(out));
        }
    }
    return;
error:
// ADD CLEANUP ROUTINES
    exit(0);
}

static void
callback_data(void *field, size_t len, void *data)
{
    if (!field || !len) {
        return;
    }
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    struct lcsv_w_ctx *lcsv_w = upd_ctx->lcsv_ctx;
    char buf[0x2000];
	char out[0x2000];
    char *str = (char *)field;
    char *tmp = upd_ctx->cbdata;
    int current_col = lcsv_w_get_col(lcsv_w);
    uint8_t dataset_id = upd_ctx->dataset_id;
	int year = EMISS_DATA_STARTS_FROM + (current_col - 4);
	psqldb_query_data_st *query_data;
	char *set, *query;
    if (dataset_id == DATASET_META) {
        char *where;
		if (current_col > 3) {
			return;
		} else if (strlen(tmp) > 3) {
            char country_code[4] = {0};
            memcpy(&country_code, tmp, 3);
            set =   "region_id=(SELECT id FROM region_t), "\
                    "income_id=(SELECT id FROM income_t), "\
                    "is_an_aggregate=FALSE, metadata=$$%s$$";
            where = "code_iso_a3='%s'";
            SQL_UPDATE_WITH_WHERE(buf, out,
                (tmp + 3), "Country", set,
                where, str, country_code);
            memset(tmp, 0, strlen(tmp));
        } else if (strlen(tmp) == 3) {
            set =   "is_an_aggregate=TRUE, metadata=$$%s$$";
            where = "code_iso_a3='%s'";
            SQL_UPDATE_WHERE(buf, out, "Country", set, where, str, tmp);
            memset(tmp, 0, strlen(tmp));
        } else {
            return;
        }
		query = calloc(strlen(out) + 1, sizeof(char));
		memcpy(query, out, strlen(out));
		query_data = psqldb_init_query_data(query,
                        NULL, NULL, 0, NULL, NULL, 0);
        free(query);
    } else if (year >= EMISS_YEAR_ZERO && year <= EMISS_YEAR_LAST) {
        char *table = "Datapoint";
        char *vals = "'%s', %d, %s";
        char *cols = "country_code, yeardata_year, %s";
        if (dataset_id == DATASET_CO2E) {
			SQL_INSERT_INTO(buf, out,
                table, cols, vals,
                "emission_kt",
                tmp, year,
                str);
        } else {
            char insert_sql[0x100];
            SQL_INSERT_INTO(buf,
                insert_sql, table,
                cols, vals, "population_total",
                tmp, year, str);
            /*  Remove end-of-query semicolon. */
            insert_sql[strlen(insert_sql) - 1] = '\0';
            memset(buf, 0, sizeof(buf));
            char *arbiter = "country_code, yeardata_year";
            /*  arbiter: a conflict fires an update instead of insert
                if a datapoint for this country and year already exists.
             */
			set = "population_total=%s";
            SQL_UPSERT(buf, out, insert_sql, arbiter, set, str);
        }
		query = calloc(strlen(out) + 1, sizeof(char));
		memcpy(query, out, strlen(out));
		query_data = psqldb_init_query_data(query,
                        NULL, NULL, 0, NULL, NULL, 0);
        free(query);
    } else {
		return;
	}
	check(query_data, ERR_FAIL, EMISS_ERR, "creating query data struct");
	int ret = psqldb_enqueue_query(upd_ctx->conn_ctx, query_data);
	check(ret, ERR_FAIL, EMISS_ERR, "appending to db job queue");
    return;
error:
// ADD CLEANUP ROUTINES
    exit(0);
}

static void
callback_world_data(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    struct lcsv_w_ctx *lcsv_w = upd_ctx->lcsv_ctx;
	int year = EMISS_DATA_STARTS_FROM + (lcsv_w_get_col(lcsv_w) - 4);
	if (year < EMISS_YEAR_ZERO || year > EMISS_YEAR_LAST) {
		return;
	}
    char buf[0xFFF];
    char out[0xFFF] = {0};
    char *str = (char*) field;
    uint8_t dataset_id = upd_ctx->dataset_id;
    char *set =  dataset_id == DATASET_CO2E
                    ? "world_co2emissions=%s" : "world_population=%s";
    char *where = "year=%d";
    SQL_UPDATE_WHERE(buf, out, "YearData", set, where, str, year);
    char *query = calloc(strlen(out) + 1, sizeof(char));
    check(query, ERR_MEM, EMISS_ERR);
    memcpy(query, &out, strlen(out));
    psqldb_query_data_st *query_data;
	query_data = psqldb_init_query_data(query,
                    NULL, NULL, 0, NULL, NULL, 0);
	check(query_data, ERR_FAIL, EMISS_ERR, "creating query data struct");
    int ret = psqldb_enqueue_query(upd_ctx->conn_ctx, query_data);
	check(ret, ERR_FAIL, EMISS_ERR, "enqueuing query to thread jobs");
    free(query);
    return;
error:
    exit(0);
}

static void
callback_world(void *field, size_t len, void *data)
{
    emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
    struct lcsv_w_ctx *lcsv_w = upd_ctx->lcsv_ctx;
    lcsv_w_set_callback_by_row(lcsv_w,
        lcsv_w_get_row(lcsv_w), callback_world_data);
}

static void
callback_preview(void *field, size_t len, void *data)
{
    if (len) {
        emiss_update_ctx_st *upd_ctx = (emiss_update_ctx_st *)data;
        char *str = (char *)field;
        char *tmp = upd_ctx->cbdata;
        if (strstr(str, "Last Updated")) {
            memcpy(tmp, str, strlen(str));
        } else if (tmp && strstr(tmp, "Last Updated")) {
            memset(tmp, 0, strlen(tmp));
            memcpy(tmp, str, strlen(str));
        }
    }
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

/*
**  FUNCTION PROTOYPE IMPLEMENTATIONS
*/

emiss_update_ctx_st *
emiss_init_update_ctx(char *tui_chart_worldmap_data_path, uintmax_t tui_chart_worldmap_data_size)
{
    emiss_update_ctx_st *upd_ctx = malloc(sizeof(emiss_update_ctx_st));
    check(upd_ctx, ERR_MEM, EMISS_ERR);
    memset(upd_ctx, 0, sizeof(emiss_update_ctx_st));

    /*  Read the tui.chart component world.js to memory for checking the country codes included
        threre against the Worldbank data.
    */

    FILE *worldmap_js_fp = fopen("../resources/js/world.js", "r");
    check(worldmap_js_fp, ERR_FAIL, EMISS_ERR, "opening file");
    rewind(worldmap_js_fp);
    char *worldmap_js_buf = calloc(tui_chart_worldmap_data_size + 1, sizeof(char));
    check(worldmap_js_buf, ERR_MEM, EMISS_ERR);
    size_t bytes_read = fread(worldmap_js_buf, 1, tui_chart_worldmap_data_size, worldmap_js_fp);
    check(bytes_read == tui_chart_worldmap_data_size, ERR_FAIL, EMISS_ERR,
        "reading file to buffer");
    fclose(worldmap_js_fp);
    upd_ctx->tui_chart_worldmap_data = calloc(NCOUNTRY_DATA_SLOTS, sizeof(char (*)[3]));
    check(upd_ctx->tui_chart_worldmap_data, ERR_MEM, EMISS_ERR);
    char (*worldmap_codes_arr)[3] = upd_ctx->tui_chart_worldmap_data;

    setvbuf(stdout, NULL, _IONBF, 0);
    size_t i = 0;
    char *ptr = worldmap_js_buf;
    do {
        char *res = strchr(ptr, ':');
        if (res && res + 4) {
            if (res[1] == '\"' && res[4] == '\"') {
                memcpy(worldmap_codes_arr[i], res + 2, 2);
                i++;
                res += 5;
            } else {
                ++res;
            }
        }
        ptr = res;
    } while (ptr);

    free(worldmap_js_buf);
    upd_ctx->tui_chart_worldmap_ccount = i;
    upd_ctx->tui_chart_worldmap_data = worldmap_codes_arr;

    upd_ctx->ccodes = NULL;
    upd_ctx->ccount = 0;
    upd_ctx->conn_ctx = psqldb_init_conn_ctx(NULL);
    check(upd_ctx->conn_ctx, ERR_FAIL, EMISS_ERR,
        "initializing db connection context structure");
    upd_ctx->lcsv_ctx = lcsv_w_init(NULL, NULL, 0, 1);
    check(upd_ctx->conn_ctx, ERR_FAIL, EMISS_ERR,
        "initializing libcsv wrapper structure");
    upd_ctx->cbdata_max_size = 0;
    return upd_ctx;
error:
    return NULL;
}

void
emiss_free_update_ctx(emiss_update_ctx_st *upd_ctx)
{
    if (upd_ctx) {
        lcsv_w_free(upd_ctx->lcsv_ctx);
		if (upd_ctx->conn_ctx) {
			psqldb_free_conn_ctx(upd_ctx->conn_ctx);
		}
        if (upd_ctx->ccodes) {
            ht_country_code_st *current, *tmp;
            HASH_ITER(hh, upd_ctx->ccodes, current, tmp) {
                /* Remove from hash table. */
                HASH_DELETE(hh, upd_ctx->ccodes, current);
                /* Free the struct itself. */
                free(current);
            }
        }
        if (upd_ctx->cbdata) {
            free(upd_ctx->cbdata);
        }
        if (upd_ctx->tui_chart_worldmap_data) {
            free(upd_ctx->tui_chart_worldmap_data);
        }
		free(upd_ctx);
    }
}

size_t
emiss_parse_and_update(emiss_update_ctx_st *upd_ctx,
    char **paths, uintmax_t *file_sizes, size_t npaths,
    int *dataset_ids, time_t current_version)
{
    /*  Set up a buffer to store data between callbacks. */
    upd_ctx->cbdata = calloc(0x500, sizeof(char));
    check(upd_ctx->cbdata, ERR_MEM, EMISS_ERR);
    upd_ctx->cbdata_max_size = 0x500;
    struct lcsv_w_ctx *lcsv_w = upd_ctx->lcsv_ctx;
    lcsv_w_set_callback_data(lcsv_w, upd_ctx);

    /*  Read ISO-3166-1 Alpha-3 && Alpha-2 codes and independency status
        from country_codes.csv. Worldbank uses the three-letter Alpha-3
        codes, while Toast UI map charts use the two letter Alpha-2 ones.
        When Worldbank data is entered into a database, these values can
        be consulted to obtain the two-letter counterpart of the Alpha-3
        codes.
    */
    lcsv_w_set_callback_by_row(lcsv_w,
        0, callback_country_code_data_header);
    int ret = lcsv_w_set_target_path(lcsv_w, paths[0], strlen(paths[0]));
    check(ret, ERR_EXTERN, LCSV_W, "setting file path");
    size_t data_size = file_sizes[0] ? file_sizes[0] + 10 : 0x100000;
    ret = lcsv_w_read(lcsv_w, data_size);
    check(ret, ERR_EXTERN, LCSV_W, "reading csv file");
    free(upd_ctx->tui_chart_worldmap_data);
    lcsv_w_reset_callbacks(lcsv_w, 1, 1, 1);
    /*  Set up some settings and callbacks for the Worldbank files. */
    lcsv_w_set_offset(lcsv_w, 4);
    ret = lcsv_w_set_ignore_regex(lcsv_w, EMISS_IGNORE_REGEX);
    check(ret, ERR_EXTERN, LCSV_W, "setting ignore regex");
    lcsv_w_set_default_callback(lcsv_w, callback_data);
    lcsv_w_set_callback_by_regex_match(lcsv_w,
        "World", 5, callback_world);
    lcsv_w_set_callback_by_row(lcsv_w, 0, callback_year);
    lcsv_w_set_callback_by_column(lcsv_w, 0, callback_country);
    lcsv_w_set_callback_by_column(lcsv_w, 1, callback_country);
    lcsv_w_set_end_of_row_callback(lcsv_w,
        callback_wait_until_queries_done);

    ret = 0;
    size_t retval = 0;
    for (size_t i = 1; i < npaths; i++) {
        ret = lcsv_w_set_target_path(lcsv_w, paths[i], strlen(paths[i]));
        check(ret, ERR_EXTERN, LCSV_W, "setting file path");
        upd_ctx->dataset_id = dataset_ids[i];
        if (dataset_ids[i] != DATASET_META) {
            if (current_version) {
                ret = lcsv_w_preview(lcsv_w,
                    3, 0x10000, callback_preview);
                check(ret, ERR_EXTERN, LCSV_W,
                    "obtaining preview of csv file");
    			char *temp = upd_ctx->cbdata;
                time_t updated = temp && strlen(temp)
                                    ? parse_last_updated_date(temp)
                                    : (time_t) 0;
                if (updated && (updated <= current_version)) continue;
            }
			ret = psqldb_launch_conn_threads(upd_ctx->conn_ctx);
			check(ret, ERR_FAIL, EMISS_ERR,
                "launching database connection threads");
            data_size = file_sizes[i] ? file_sizes[i] + 10: 0x100000;
            ret = lcsv_w_read(lcsv_w, data_size);
            check(ret, ERR_EXTERN, LCSV_W, "reading csv file");
            retval += ret;
        } else {
            lcsv_w_unset_callback_by_regex_match(lcsv_w, "World", 5);
            lcsv_w_set_callback_by_column(lcsv_w, 2, callback_country);
            lcsv_w_set_offset(lcsv_w, 0);
            lcsv_w_set_options(lcsv_w, 1);
			ret = psqldb_launch_conn_threads(upd_ctx->conn_ctx);
			check(ret, ERR_FAIL, EMISS_ERR,
                "launching database connection threads");
            data_size = file_sizes[i] ? file_sizes[i] + 10 : 0x100000;
            ret = lcsv_w_read(lcsv_w, data_size);
            check(ret, ERR_EXTERN, LCSV_W, "reading csv file");
            retval += ret;
        }
		ret = psqldb_stop_and_join_threads(upd_ctx->conn_ctx);
		check(ret == 0, ERR_FAIL, EMISS_ERR,
            "there were errors closing threads");
    }
    emiss_free_update_ctx(upd_ctx);
    return retval;
error:
	emiss_free_update_ctx(upd_ctx);
    return -1;
}
