/*  @file   emiss.retrieve.c
    @brief  Part of the implementation of "emiss.h"
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
**  MACROS
*/

#define COMPILE_PCRE_REGEX(str, rgx)\
    do {\
        const char *pcre_error_msg;\
        int pcre_error_offset;\
        rgx = pcre_compile(str, 0, &pcre_error_msg, &pcre_error_offset, NULL);\
        check(rgx, ERR_EXTERN_AT, "PCRE", pcre_error_msg, pcre_error_offset);\
    } while(0)

/*
**  FUNCTIONS
*/

/*  INLINE INSTANTIATIONS */

extern inline int
emiss_should_check_for_update();

/*  STATIC DEFINITIONS */

/*  decompress_to_disk(): Extracts a zip archive from path 'src_file' to a folder 'dest_path',
    excluding entries with a filename matching 'ignore_rgx' while saving the uncompressed
    size in bytes of all extracted files at 'file_size'.

    Returns the count of extracted files i.e. size of 'file_size' or -1 on error.
*/
static int
decompress_to_disk(char *src_file, char *dest_folder,
    pcre *ignore_rgx, uintmax_t *file_size)
{
    int ret, total;
    struct zip_t *zip = zip_open(src_file, 0, 'r');
    check(zip, ERR_EXTERN, "zip.h", "could not open archive file")
    total = zip_total_entries(zip);
    check(total > 0, ERR_EXTERN, "zip.h", "archive has no entries or error");
    int i = 0;
    *(strrchr(src_file, '.')) = '\0';
    int processed = 0;
    do {
        int pcre_ovec[300];
        ret = zip_entry_openbyindex(zip, i);
        check(ret >= 0, ERR_EXTERN_AT, "zip.h",
            "error opening archive entry by index", i);
        const char *entry_name = zip_entry_name(zip);
        check(entry_name, ERR_EXTERN_AT, "zip.h",
            "error obtaining current archive entry name", i);
        ret = pcre_exec(ignore_rgx, NULL,
                entry_name, strlen(entry_name),
                0, 0, pcre_ovec, 300);
        if (ret < 0) {
            check(ret == PCRE_ERROR_NOMATCH, ERR_EXTERN_AT, "PCRE",
                "matching error", ret);
            char buf[0x500];
            char *filename = strstr(entry_name, "Meta") ? "Meta" : src_file;
            snprintf(buf, 0x4FF, "%s/%s.csv", dest_folder, filename);
            ret = zip_entry_fread(zip, buf);
            check(ret == 0, ERR_EXTERN_AT, "zip.h",
                "error writing archive entry to file", i);
            *file_size = zip_entry_size(zip);
            ++file_size;
            processed++;
        }
        zip_entry_close(zip);
    } while (++i < total);
    zip_close(zip);
    return processed;
error:
    if (zip) zip_close(zip);
    return -1;
}

/*  callback_curl_write_to_file():
*/
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

/*  fetch_data_from_remote():

    Use libcurl to download data from 'count' addresses, defined via parameters 'protocols',
    'hosts', 'uris', 'query_strings' and 'resources':
        [protocol]://[host]/[uri]/[resource](?[query_string]).
    Return number of retrieved files or -1 on error.
*/
static int
fetch_from_remote(char const **protocols, char const **hosts,
    char const **uris, char const **query_strings, char const **resources,
    uintmax_t *file_sizes, size_t nitems)
{
    FILE *fp_hdr = NULL, *fp_body = NULL;
    struct file_metadata *filedata = NULL;
    CURL *curl = NULL;
    filedata = calloc(1, sizeof(struct file_metadata));
    check(filedata, ERR_MEM, EMISS_ERR);

    CURLcode res;
    char curl_err[CURL_ERROR_SIZE];

    curl = curl_easy_init();
    check(curl, ERR_EXTERN, "CURL",
        "something went wrong initializing libcurl handle");
    res = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err);
    check(res == CURLE_OK, ERR_EXTERN, "libcurl", curl_easy_strerror(res));
    CURL_SET_OPTS(curl, res, curl_err);
    res = curl_easy_setopt(curl,
            CURLOPT_WRITEFUNCTION, callback_curl_write_to_file_saving_size);
    CURL_ERR_CHK(curl_err, res);
    res = curl_easy_setopt(curl,
            CURLOPT_HEADERFUNCTION, callback_curl_write_to_file);
    CURL_ERR_CHK(curl_err, res);
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, filedata);
    CURL_ERR_CHK(curl_err, res);
    size_t i = 0;
    int ret = 0;
    do  {
        fp_hdr = NULL, fp_body = NULL;
        filedata->byte_size = 0;
        char path[0xFF];
        const char *resource = resources[i];
        const char *filetype = strrchr(resource, '.') + 1;
        const char *frmt =  strstr(filetype, "js")
                            ? "../resources/js/%s" : strstr(filetype, "csv")
                            ? "../resources/data/%s" : "%s.zip";
        sprintf(path, frmt, resource);

        fp_body = fopen(path, "w");
        check(fp_body, ERR_FAIL, EMISS_ERR, "opening file");
        filedata->fp = fp_body;

        fp_hdr = fopen("header.log", "a");
        check(fp_hdr, ERR_FAIL, EMISS_ERR, "opening file");
        res = curl_easy_setopt(curl, CURLOPT_HEADERDATA, fp_hdr);
        CURL_ERR_CHK(curl_err, res);

        char url[0xFFF];
        if (query_strings[i]) {
            sprintf(url, "%s://%s/%s%s?%s",
                protocols[i], hosts[i], uris[i],
                resource, query_strings[i]);
        } else {
            sprintf(url, "%s://%s/%s%s",
                protocols[i], hosts[i],
                uris[i], resource);
        }
        res = curl_easy_setopt(curl, CURLOPT_URL, url);
        CURL_ERR_CHK(curl_err, res);

        res = curl_easy_perform(curl);
        CURL_ERR_CHK(curl_err, res);
        ret++;
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
        if (filedata->fp) fclose(filedata->fp);
        free(filedata);
    }
    if (fp_hdr) fclose(fp_hdr);
    return -1;
}

/*  PROTYPE IMPLEMENTATIONS */

int
emiss_retrieve_data()
{
    /*  Protocol to use (http or https). */
    char const *protocols[] = {
        EMISS_COUNTRY_CODES_HOST_PROTOCOL,
        EMISS_WORLDBANK_HOST_PROTOCOL,
        EMISS_WORLDBANK_HOST_PROTOCOL
    };
    /*  Host base address. */
    char const *hosts[] = {
        EMISS_COUNTRY_CODES_HOST,
        EMISS_WORLDBANK_HOST,
        EMISS_WORLDBANK_HOST
    };
    /*  Local URI relative to host address. */
    char const *uris[] = {
        EMISS_COUNTRY_CODES_REL_URI,
        EMISS_WORLDBANK_REL_URI,
        EMISS_WORLDBANK_REL_URI
    };
    /*  An optional query string. */
    char const *query_strings[] = {
        NULL,
        EMISS_WORLDBANK_QSTR_DOWNLOAD_FORMAT,
        EMISS_WORLDBANK_QSTR_DOWNLOAD_FORMAT
    };
    /*  The particular resource (dataset) we are interested in. */
    char const *resources[] = {
        DATASET_0_NAME".csv",
        DATASET_1_NAME,
        DATASET_2_NAME
    };
    uintmax_t file_sizes[4] = {0UL};
    /*  Retrieve remote data (compressed). */
    int ret = fetch_from_remote(protocols,
                hosts, uris, query_strings,
                resources, file_sizes, EMISS_NINDICATORS);
    check(ret == EMISS_NINDICATORS, ERR_FAIL, EMISS_ERR,
        "fetching data from the server");

    /*  Extract only files with names not matching these regular expressions. */
    char *exclusion_rgxs[] = {
        NULL,
        /*  Metadata about individual indicators is not currently stored. */
        "Metadata_Indicator",
        /*  Country metadata is identical between indicator files,
            retrieve only from first.
        */
        "Metadata_(Indicator|Country)"
    };
    /*  Decompress data retrieved from Worldbank and store uncompressed
        sizes at file_sizes.
    */
    size_t i = 1;
    size_t j = i;
    do {
        pcre *regex;
        COMPILE_PCRE_REGEX(exclusion_rgxs[i], regex);
        char path[0x100];
        sprintf(path, "%s.zip", resources[i]);
        int filecount = decompress_to_disk(path, EMISS_DATA_ROOT, regex, &file_sizes[j]);
        check(filecount != -1, ERR_FAIL, EMISS_ERR, "decompressing files");
        j += filecount;
        pcre_free(regex);
        remove(path);
    } while (++i < EMISS_NINDICATORS);

    /*  Parse the extracted csv files at 'paths' with dataset codes in 'dataset_ids' and
    value of LAST_DATA_ACCESS environment/configuration variable.
        upload the results to database if they have been marked as updated later than the
    */

    char *paths[] = {
        EMISS_DATA_ROOT"/"DATASET_0_NAME".csv",
        EMISS_DATA_ROOT"/"DATASET_1_NAME".csv",
        EMISS_DATA_ROOT"/"DATASET_2_NAME".csv",
        EMISS_DATA_ROOT"/"DATASET_META_NAME".csv"
    };
    int dataset_ids[] = {
        DATASET_COUNTRY_CODES,
        DATASET_CO2E,
        DATASET_POPT,
        DATASET_META
    };
    time_t last_update;
    GET_LAST_DATA_ACCESS(last_update);
    emiss_update_ctx_st *upd_ctx = emiss_update_ctx_init("../resources/data/in_tui_chart_map.txt");
    check(upd_ctx, ERR_FAIL, EMISS_ERR, "initializing update context structure");
    uintmax_t tmp = file_sizes[2];
    file_sizes[2] = file_sizes[3];
    file_sizes[3] = tmp;
    ret = emiss_update_parse_send(upd_ctx, paths,
            file_sizes, 4, dataset_ids,
            last_update);
    check(ret != -1, ERR_FAIL, EMISS_ERR, "processing csv data");
    /*  Log update status to console, save the time to
        LAST_DATA_ACCESS and return.
    */
    struct tm update_time_utc;
    char time_str_buf[0x100];
    strftime(time_str_buf, 0xFF, "%F",
        gmtime_r(&last_update, &update_time_utc));
    if (!ret)
        fprintf(stdout, "Data (last checked at %s) was already up to date.",
            time_str_buf);
    else
        fprintf(stdout, "Data (last checked at %s) was succesfully updated.",
            time_str_buf);
    return (int) time(0);
error:
    return 0;
}

void *
emiss_retrieve_async_start()
{
    return 0;
}
