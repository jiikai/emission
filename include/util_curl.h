/*! @file       util_curl.h
    @brief      Convenience macros related to setting options and error handling in libcurl.
    @author     Joa Käis (github.com/jiikai).
    @copyright  Public domain.
*/

#ifndef _util_curl_h_
#define _util_curl_h_

#define LCURL "libcurl"

#ifndef NDEBUG
    #define CURL_VERBOSITY(curl)\
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L)
#else
    #define CURL_VERBOSITY(curl)\
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L)
#endif

#define CURL_ERR_MSG(err_buf, res) strlen(err_buf) ? err_buf : curl_easy_strerror(res)

#define CURL_SET_OPTS(curl, res, err_buf)\
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L) != CURLE_OK ? 0 :\
    curl_easy_setopt(curl, CURLOPT_USERAGENT, curl_version()) != CURLE_OK ? 0 :\
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L) != CURLE_OK ? 0 :\
    curl_easy_setopt(curl, CURLOPT_PIPEWAIT, 1L) != CURLE_OK ? 0 :\
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK ? 0 :\
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L) != CURLE_OK ? 0 :\
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L) != CURLE_OK ? 0 :\
    curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L) != CURLE_OK ? 0 :\
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_1_1) != CURLE_OK ? 0 :\
    CURL_VERBOSITY(curl) != CURLE_OK ? 0 : 1


#endif  /* _util_curl_h_ */
