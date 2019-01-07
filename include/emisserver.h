#ifndef _emisserver_
#define _emisserver_

#define _XOPEN_SOURCE 700

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include "civetweb.h"
#include "bstrlib.h"
#include "emiss.h"
#ifdef _WIN32
    #include <windows.h>
    #define sleeper(x) Sleep(x * 1000)
#else
    #include <unistd.h>
    #define sleeper(x) sleep(x)
#endif

/*
**  MACRO DEFINITIONS
*/

/*  Error message provider name. */
#define EMISSERVER "EMISSERVER"


/* Options for the CivetWeb server */
#define NUM_THREADS "64"
#define DOCUMENT_ROOT "../resources/html"
#ifdef HEROKU
    #define PORT ((char*) getenv("PORT"))
    #define HOST "emiss.herokuapp.com"
    #define HTTP_PREFIX "https://"
#else
    #define PORT "8080"
    #define HOST "localhost"
    #define DOMAIN "localhost:8080"
    #define HTTP_PREFIX "http://"
#endif
#define REQUEST_TIMEOUT "30000"
#define ERR_LOG "../log/server.log"
#define AUTH_DOM_CHECK "no"

/* Valid relative URIs */
#define NTEMPLATES_HTML 4
#define NTEMPLATES_JS 3
#define EMISS_URI_INDEX "/"
#define EMISS_URI_EXIT "/exit"
#define EMISS_URI_NEW "/new"
#define EMISS_URI_CHART "/chart"
#define EMISS_URI_ABOUT "/about"
#define CSS_URI "/css/style.css"
#define FONT_URI "/fonts"
#define JS_URI "/js"

/* HTML template paths (relative to the running binary) */
#define EMISS_HTML_INDEX "../resources/index.html"
#define EMISS_HTML_NEW "../resources/new.html"
#define EMISS_HTML_CHART "../resources/chart.html"
#define EMISS_HTML_ABOUT "../resources/about.html"

/* JavaScript paths */
#define EMISS_JS_CHART_PARAMS "../resources/js/emiss-chart-params.js"
#define EMISS_JS_CHART_LINE "../resources/js/emiss-line-chart-template.js"
#define EMISS_JS_CHART_MAP "../resources/js/emiss-map-chart-template.js"
#define EMISS_JS_CP2CB "../resources/js/cp2cb-async-fallback.js"

/* CSS & font paths (relative to document root)
#define EMISS_STYLE_CSS "../resources/css/style.css"
#define EMISS_STYLE_FONT "../resources/fonts/TFArrow-Bold.%s"
*/

/* HTTP response headers currently used. */
#define HTTP_RES_200_ARGS "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nConnection: %s\r\n\r\n"
#define HTTP_RES_200 "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
#define HTTP_RES_404 "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"
#define HTTP_RES_405 "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\nConnection: close\r\n\r\n"
#define HTTP_RES_500 "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n"

/*  ARGUMENT MACROS */

/*  Byte length calculation. */
#define SIZE_IN_BYTES(object)\
    (long long) sizeof(object) * (sizeof(size_t) / sizeof(uint8_t))

/*  Port protocol getter expression. */
#define DEFINE_PROTOCOL(server, i)\
    server->ports[i].is_ssl ? "https" : "http"

/*  Keep-alive options */
#ifdef WITH_KEEP_ALIVE_SUPPORT
    #define KEEP_ALIVE_SUPPORT(timeout_ms)\
        "additional_header", "Connection: Keep-Alive",\
        "tcp_nodelay", "1",\
        "enable_keep-alive", "1",\
        "keep_alive_timeout_ms", timeout_ms
    #define HTTP_CONTENT_LENGTH_TO_STR(body, str_out)\
        sprintf(str_out, "Content-Length: %ll", SIZE_IN_BYTES(body))
#endif

/*  Server wrapper structure */
struct emisserver_ctx {
    struct emiss_data_ctx  *emiss_ctx;
    struct mg_context      *civet_ctx;
    struct mg_callbacks     callbacks;
    struct mg_server_ports  ports[32];
    struct sigaction        sigactor;
    bstring                 html_templates[NTEMPLATES_HTML];
    bstring                 js_templates[NTEMPLATES_JS];
    int8_t                  ports_count;
    char                   *sys_info;
};

/*
**  FUNCTION PROTOTYPES
*/
struct emisserver_ctx *
emisserver_init();

void
emisserver_close(struct emisserver_ctx *server);

int emisserver_run();

#endif /* _emisserver_ */
