/*! @file       emiss_retrieve.c
    @brief      Part of the implementation of [Emission](../include/emiss.h).
    @details    See [documentation](../doc/emiss_api.md).
    @copyright: (c) Joa Käis [github.com/jiikai] 2018-2019, [MIT](../LICENSE).
*/

/*
**  INCLUDES
*/

#include "emiss.h"
#include <curl/curl.h>
#include <pcre.h>
#include "zip.h"
#include "util_curl.h"

/*
**  TYPES
*/

struct file_metadata {
    FILE       *fp;
    uintmax_t   byte_size;
};

/*
**  FUNCTIONS
*/

static int
decompress_to_disk(const char *src_file, const char *dest_folder,
    pcre *ignore_rgx, uintmax_t *file_size)
{
    struct zip_t *zip = zip_open(src_file, 0, 'r');
    check(zip, ERR_EXTERN, "zip.h", "could not open archive file")
    int total = zip_total_entries(zip);
    check(total > 0, ERR_EXTERN, "zip.h", "archive has no entries or error");
    int processed = 0,
        ret = -1,
        i = 0;
    *(strrchr(src_file, '.')) = '\0';
    do {
        int pcre_ovec[300];
        check(zip_entry_openbyindex(zip, i) >= 0, ERR_EXTERN_AT,
                "zip.h", "error opening archive entry by index",
                i);
        const char *entry_name = zip_entry_name(zip);
        check(entry_name, ERR_EXTERN_AT, "zip.h",
                "error obtaining current archive entry name", i);
        if (ignore_rgx)
            ret = pcre_exec(ignore_rgx, 0, entry_name,
                    strlen(entry_name), 0, 0,
                    pcre_ovec, 300);
        if (ret < 0) {
            check(ret == PCRE_ERROR_NOMATCH, ERR_EXTERN_AT, "PCRE",
                    "matching error", ret);
            char buf[0x500];
            const char *filename = strstr(entry_name, "Meta") ? "Meta" : src_file;
            check(snprintf(buf, 0x4FF, "%s/%s.csv", dest_folder, filename) != -1,
                    ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
            check(zip_entry_fread(zip, buf) == 0, ERR_EXTERN_AT, "zip.h",
                "error writing archive entry to file", i);
            *file_size = zip_entry_size(zip);
            ++file_size;
            ++processed;
        }
        zip_entry_close(zip);
    } while (++i < total);
    zip_close(zip);
    return processed;
error:
    if (zip)
        zip_close(zip);
    return -1;
}

static size_t
callback_curl_write_to_file_saving_size(char *buffer,
    size_t size, size_t nitems, void *userdata)
{
    struct file_metadata *filedata = (struct file_metadata *)userdata;
    size_t written = fwrite(buffer, size, nitems, filedata->fp);
    filedata->byte_size += written;
    return written;
}

static size_t
callback_curl_write_to_file(char *buffer,
    size_t size, size_t nitems, void *userdata)
{
    return fwrite(buffer, size, nitems, (FILE *)userdata);
}

static int
fetch_from_remote(const char **protocols, const char **hosts,
    const char **uris, const char **query_strings, const char **resources,
    uintmax_t *file_sizes, size_t nitems)
{
    FILE *fp_hdr = 0,
         *fp_body = 0;
    CURL *curl = 0;

    struct file_metadata *filedata = 0;
    filedata = calloc(1, sizeof(struct file_metadata));
    check(filedata, ERR_MEM, EMISS_ERR);

    curl = curl_easy_init();
    check(curl, ERR_EXTERN, "CURL", "something went wrong initializing CURL handle");

    char curl_err[CURL_ERROR_SIZE];
    CURLcode res = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err);
    check(res == CURLE_OK, ERR_EXTERN, LCURL, curl_easy_strerror(res));

    res = CURL_SET_OPTS(curl, res, curl_err);
    check(res, ERR_EXTERN, LCURL, CURL_ERR_MSG(curl_err, res));

    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback_curl_write_to_file_saving_size);
    check(res == CURLE_OK, ERR_EXTERN, LCURL, CURL_ERR_MSG(curl_err, res));

    res = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, callback_curl_write_to_file);
    check(res == CURLE_OK, ERR_EXTERN, LCURL, CURL_ERR_MSG(curl_err, res));

    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, filedata);
    check(res == CURLE_OK, ERR_EXTERN, LCURL, CURL_ERR_MSG(curl_err, res));

    size_t i = 0;
    int ret = 0;
    do {
        fp_hdr = 0, fp_body = 0;
        filedata->byte_size = 0;
        char path[0x100];
        char url[0x1000];
        const char  *resource   = resources[i],
                    *filetype   = strrchr(resource, '.') + 1,
                    *path_frmt  = strstr(filetype, "js") ? "../resources/js/%s"
                                : strstr(filetype, "csv") ? "../resources/data/%s"
                                : "%s.zip",
                    *url_frmt   = query_strings[i] ? "%s://%s/%s%s?%s"
                                : "%s://%s/%s%s";
        check(snprintf(path, 0xFF, path_frmt, resource) != -1,
                ERR_FAIL, EMISS_ERR, "printf'ing to buffer");

        fp_body = fopen(path, "w");
        check(fp_body, ERR_FAIL, EMISS_ERR, "opening file");
        filedata->fp = fp_body;

        fp_hdr = fopen("header.log", "a");
        check(fp_hdr, ERR_FAIL, EMISS_ERR, "opening file");
        res = curl_easy_setopt(curl, CURLOPT_HEADERDATA, fp_hdr);
        check(res == CURLE_OK, ERR_EXTERN, LCURL, CURL_ERR_MSG(curl_err, res));

        check(snprintf(url, 0xFFF, url_frmt, protocols[i], hosts[i], uris[i],
                resource, query_strings[i] ? query_strings[i] : "") != -1,
                ERR_FAIL, EMISS_ERR, "printf'ing to buffer");
        res = curl_easy_setopt(curl, CURLOPT_URL, url);
        check(res == CURLE_OK, ERR_EXTERN, LCURL, CURL_ERR_MSG(curl_err, res));

        res = curl_easy_perform(curl);
        check(res == CURLE_OK, ERR_EXTERN, LCURL, CURL_ERR_MSG(curl_err, res));
        ++ret;
        file_sizes[i] = filedata->byte_size;
        fclose(fp_body);
        fclose(fp_hdr);
    } while (++i < nitems);
    free(filedata);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return ret;
error:
    if (curl) {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
    }
    if (filedata) {
        if (filedata->fp)
            fclose(filedata->fp);
        free(filedata);
    }
    if (fp_hdr)
        fclose(fp_hdr);
    return -1;
}

/*  PROTYPE IMPLEMENTATIONS */

int
emiss_retrieve_data(emiss_file_data_st *file_data_dest)
{
    memset(file_data_dest, 0, sizeof(emiss_file_data_st));
    /*  Protocol to use (http or https). */
    const char *protocols[] = {
        EMISS_COUNTRY_CODES_HOST_PROTOCOL,
        EMISS_WORLDBANK_HOST_PROTOCOL,
        EMISS_WORLDBANK_HOST_PROTOCOL
    };
    /*  Host base address. */
    const char *hosts[] = {
        EMISS_COUNTRY_CODES_HOST,
        EMISS_WORLDBANK_HOST,
        EMISS_WORLDBANK_HOST
    };
    /*  Local URI relative to host address. */
    const char *uris[] = {
        EMISS_COUNTRY_CODES_REL_URI,
        EMISS_WORLDBANK_REL_URI,
        EMISS_WORLDBANK_REL_URI
    };
    /*  An optional query string. */
    const char *query_strings[] = {
        0,
        EMISS_WORLDBANK_QSTR_DOWNLOAD_FORMAT,
        EMISS_WORLDBANK_QSTR_DOWNLOAD_FORMAT
    };
    /*  The particular resource (dataset) we are interested in. */
    const char *resources[] = {
        DATASET_0_NAME".csv",
        DATASET_1_NAME,
        DATASET_2_NAME
    };
    uintmax_t *file_sizes = file_data_dest->file_sizes;
    /*  Retrieve remote data. */
    int ret = fetch_from_remote(protocols,
                    hosts, uris, query_strings,
                    resources, file_sizes,
                    EMISS_NINDICATORS);
    check(ret == EMISS_NINDICATORS, ERR_FAIL, EMISS_ERR,
        "fetching data from the server");

    size_t  i = 1,
            j = 1;
    do {
        const char  *pcre_error_msg,
                    *exclusion_str = i == 1 ? "Metadata_Indicator" :
                                    "Metadata_(Indicator|Country)";
        int pcre_error_offset;
        pcre *regex = pcre_compile(exclusion_str, 0, &pcre_error_msg, &pcre_error_offset, 0);
        check(regex, ERR_EXTERN_AT, "PCRE", pcre_error_msg, pcre_error_offset);
        char path[0x100];
        snprintf(path, 0xFF, "%s.zip", resources[i]);
        int filecount = decompress_to_disk(path, EMISS_DATA_ROOT, regex, &file_sizes[j]);
        check(filecount != -1, ERR_FAIL, EMISS_ERR, "decompressing files");
        j += filecount;
        pcre_free(regex);
    } while (++i < EMISS_NINDICATORS);

    const char *paths[] = {
        EMISS_DATA_ROOT"/"DATASET_0_NAME".csv",
        EMISS_DATA_ROOT"/"DATASET_1_NAME".csv",
        EMISS_DATA_ROOT"/"DATASET_META_NAME".csv",
        EMISS_DATA_ROOT"/"DATASET_2_NAME".csv"
    };
    memcpy(file_data_dest->paths, paths, sizeof(paths));
    memcpy(file_data_dest->dataset_ids, (int[]) {
        DATASET_COUNTRY_CODES, DATASET_CO2E, DATASET_META, DATASET_POPT
    }, (EMISS_NINDICATORS + 1) * sizeof(int));
    return 1;
error:
    return 0;
}

void *
emiss_retrieve_async_start()
{
    return 0;
}
