#ifndef _CURLOPTS_H_
#define _CURLOPTS_H_

#define LCURL "libcurl"

#ifndef NDEBUG
    #define DEBUGINFO 1
#else
    #define DEBUGINFO 0
#endif

#define CURL_ERR_MSG(err_buf, res) strlen(err_buf) ? err_buf : curl_easy_strerror(res)

#define CURL_ERR_CHK(err_buf, res) check(res == CURLE_OK, ERR_EXTERN, LCURL, CURL_ERR_MSG(err_buf, res))

#define CURL_SET_OPTS(curl, res, err_buf)\
    res = curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);\
    CURL_ERR_CHK(err_buf, res);\
    res = curl_easy_setopt(curl, CURLOPT_USERAGENT, curl_version());\
    CURL_ERR_CHK(err_buf, res);\
	res = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);\
    CURL_ERR_CHK(err_buf, res);\
    res = curl_easy_setopt(curl, CURLOPT_PIPEWAIT, 1L);\
    CURL_ERR_CHK(err_buf, res);\
    res = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);\
    CURL_ERR_CHK(err_buf, res);\
    res = curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);\
    CURL_ERR_CHK(err_buf, res);\
    res = curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);\
    CURL_ERR_CHK(err_buf, res);\
    res = curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);\
    CURL_ERR_CHK(err_buf, res);\
    if (DEBUGINFO) res = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);\
    if (DEBUGINFO) CURL_ERR_CHK(err_buf, res);\
    res = curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_1_1);\
    CURL_ERR_CHK(err_buf, res)


#endif
