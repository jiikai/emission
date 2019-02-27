#include "emiss.h"

#include <sys/random.h>

#include "dbg.h"

/*
**  GLOBAL VARIABLES
*/

/*  A volatile variable that gets switched on SIGTERM or other exit request. */
volatile sig_atomic_t terminate;

/*
**  MACROS
*/

/*  Options for the CivetWeb server. *////@{
#ifdef HEROKU
    #define CIVET_SERVER_PORT  getenv("PORT")
    #define CIVET_SERVER_HOST "emiss.herokuapp.com"
    #define CIVET_SERVER_PROTOCOL "https://"
    #define CIVET_ABS_ROOT_URL "https://emiss.herokuapp.com"
#else
    #ifndef CIVET_SERVER_PORT
        #define CIVET_SERVER_PORT "8080"
    #endif
    #ifndef CIVET_SERVER_HOST
        #define CIVET_SERVER_HOST "localhost"
    #endif
    #ifndef CIVET_USE_SSL
        #define CIVET_SERVER_PROTOCOL "http://"
    #else
        #define CIVET_SERVER_PROTOCOL "https://"
    #endif
#endif
#ifndef CIVET_ACCESS_CONTROL_METHODS
    #define CIVET_ACCESS_CONTROL_METHODS "GET, HEAD, OPTIONS, TRACE"
#endif
#ifndef CIVET_ACCESS_CONTROL_HEADER
    #define CIVET_ACCESS_CONTROL_HEADER "*"
#endif
#ifndef CIVET_ACCESS_CONTROL_ORIGIN
    #define CIVET_ACCESS_CONTROL_ORIGIN "*"
#endif
#define CIVET_AUTH_DOM_CHECK "no"
#ifndef CIVET_DEFAULT_NTHREADS
    #define CIVET_DEFAULT_NTHREADS "64"
#endif
#ifndef CIVET_DOC_ROOT
    #define CIVET_DOC_ROOT "../resources"
#endif
#define CIVET_REQUEST_TIMEOUT "30000"
#define CIVET_STATICS_MAX_AGE "3600"
#ifdef WITH_KEEP_ALIVE_SUPPORT
    #define CIVET_KEEP_ALIVE_SUPPORT\
        "tcp_nodelay", "1",\
        "enable_keep-alive", "1",\
        "keep_alive_timeout_ms", CIVET_REQUEST_TIMEOUT
    #define CONN_ALIVE "keep-alive"
#else
    #define CIVET_KEEP_ALIVE_SUPPORT\
        "enable_keep_alive", "0"\
        "keep_alive_timeout_ms", "0"
#endif
#define CIVET_ADDITIONAL_HEADERS\
    "additional_header", "X-Content-Type-Options: nosniff",\
    "additional_header", "X-Frame-Options: deny",\
    "additional_header", "X-XSS-Protection: 1",\
    "additional_header", "Content-Security-Policy: "\
        "default_src 'none'; child_src 'self', script_src 'self' code.jquery.com uicdn.toast.com;"\
        "style_src 'self' *.fontawesome.com; font_src 'self' *.fontawesome.com; img_src 'self';"\
        "frame_ancestors 'none'; form_action 'self'"
///@}

#define HTTP_RESPONSE_HDR\
    "HTTP/1.1 %u %s\r\n"\
    "Content-Length: %lu\r\n"\
    "Content-Type: %s\r\n"\
    "Connection: %s\r\n"\
    "Transfer-Encoding: %s\r\n"\
    "%s\r\n"

#define TRANSFER_ENCODING_NONE "identity"
#define TRANSFER_ENCODING_DEFL "deflate"

#define RES_200_TXT "OK"
#define RES_404_TXT "Not Found"
#define RES_405_TXT "Method Not Allowed"
#define RES_500_TXT "Internal Server Error"

#define HTTP_MIMETYPE_JS "application/javascript"
#define HTTP_MIMETYPE_CSS "text/css"
#define HTTP_MIMETYPE_HTML "text/html"
#define HTTP_MIMETYPE_PLAIN "text/plain"
#define HTTP_MIMETYPE_WOFF "font/woff"
#define HTTP_MIMETYPE_WOFF2 "font/woff2"
#define HTTP_MIMETYPE_EOT "font/eot"
#define HTTP_MIMETYPE_SVG "font/svg"
#define HTTP_MIMETYPE_TTF "font/ttf"

/*  Port protocol getter expression. */
#define DEFINE_PROTOCOL(server, i)\
    (server->civet_ports[i].is_ssl ? "https" : "http")

#define HTTP_EXPIRATION(date_str, buflen)\
    snprintf(str_out, buflen - 1,"Content-Expire: %s", date_str)

#define EXPLAIN_SEND_FAILURE(ret)\
    if (ret == -1) {\
        log_err(ERR_FAIL, EMISS_ERR, "sending HTTP response header");\
    } else if (!ret) {\
        log_warn("[emiss_server]: %s", "Connection was closed before trying to send response.");\
    }

/*
**  TYPES AND STRUCTURES
*/

struct emiss_server_ctx {
    struct sigaction                sigactor;
    struct mg_context              *civet_ctx;
    struct mg_callbacks             civet_callbacks;
    struct mg_server_ports          civet_ports[32];
    emiss_template_st              *template_data;
    int8_t                          ports_count;
    char                           *sys_info;
};

/*
**  FUNCTIONS
*/

/*  STATIC  */

static inline int
inl_send_error_response(struct mg_connection *conn, const int code)
{
    int ret = -1;
    if (code == 404)
        ret = mg_printf(conn, HTTP_RESPONSE_HDR, 404,
            RES_404_TXT, 0UL, HTTP_MIMETYPE_PLAIN, "close",
            TRANSFER_ENCODING_NONE, "");
    else if (code == 405)
        ret = mg_printf(conn, HTTP_RESPONSE_HDR, 405,
            RES_405_TXT, 0UL, HTTP_MIMETYPE_PLAIN, "close",
            TRANSFER_ENCODING_NONE, "Allow: GET\r\n");
    else if (code == 500)
        ret = mg_printf(conn, HTTP_RESPONSE_HDR, 500,
            RES_500_TXT, 0UL, HTTP_MIMETYPE_PLAIN, "close",
            TRANSFER_ENCODING_NONE, "Allow: GET\r\n");

    if (ret == -1)
        return 0;
    else if (!ret)
        return 418;

    return code;
}

static inline int
inl_get_sys_info(struct emiss_server_ctx *server)
{
	int req_bytes = (mg_get_system_info(NULL, 0)) * 1.2 + 1;
	server->sys_info = malloc(sizeof(char) * (req_bytes));
    check(server->sys_info, ERR_MEM, EMISS_MSG);
    mg_get_system_info(server->sys_info, req_bytes);
    return 1;
error:
    return 0;
}

/*  Formatted output to connection callback. */
static int
emiss_conn_printf_function(void *at,
    const unsigned http_response_code,
    const uintmax_t byte_size,
    const char *restrict mime_type,
    const char *restrict conn_action,
    const char *restrict frmt, ...)
{
    printf("output\n");
	struct mg_connection *conn = (struct mg_connection *)at;
    /*  Try to send response header. */
    int ret = mg_printf(conn, HTTP_RESPONSE_HDR, http_response_code,
                    mg_get_response_code_text(conn, http_response_code),
                    byte_size, mime_type, conn_action, TRANSFER_ENCODING_NONE, "");
    if (ret < 1) {
        EXPLAIN_SEND_FAILURE(ret);
        return ret < 0 ? -1 : 418;
    }
    /*  Send message body. */
    va_list args;
	va_start(args, frmt);
    printf("send body\n");
	ret = modified_mg_vprintf(conn, frmt, args);
    va_end(args);
    printf("sent!\n" );
	return ret;
}

/*  Request handlers for CivetWeb. */

static int
css_request_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *req_info = mg_get_request_info(conn);
    int ret = mg_strncasecmp(req_info->request_method, "GET", 3);
    if (ret)
        return inl_send_error_response(conn, 405);

    mg_send_mime_file(conn, (const char *)cbdata, HTTP_MIMETYPE_CSS);
	return 200;
}

static int
font_request_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *req_info = mg_get_request_info(conn);
    int ret = mg_strncasecmp(req_info->request_method, "GET", 3);
    if (ret)
        return inl_send_error_response(conn, 405);

    char filepath[0x100] = {0};
    const char *req_font = strchr(strchr(req_info->local_uri, '/') + 1, '/');
    if (!strstr(req_font, (char *)cbdata)) {
    	inl_send_error_response(conn, 404);
    	return 404;
    }
    const char *f_ext     = strrchr(req_font, '.') + 1;
    const char *mime_type = f_ext[strlen(f_ext) - 1] == '2' ? HTTP_MIMETYPE_WOFF2
                            : f_ext[0] == 'w' ? HTTP_MIMETYPE_WOFF
                            : f_ext[0] == 't' ? HTTP_MIMETYPE_TTF
                            : f_ext[0] == 's' ? HTTP_MIMETYPE_SVG
                            : HTTP_MIMETYPE_EOT;
    snprintf(filepath, 0xFF, "%s%s", EMISS_FONT_ROOT, req_font);
	mg_send_mime_file(conn, filepath, mime_type);
	return 200;
}

static int
static_resource_request_handler(struct mg_connection *conn, void *cbdata)
{
    const struct mg_request_info *req_info = mg_get_request_info(conn);
    int ret = mg_strncasecmp(req_info->request_method, "GET", 3);
    if (ret)
        return inl_send_error_response(conn, 405);

    emiss_resource_ctx_st *rsrc_ctx = (emiss_resource_ctx_st *)cbdata;
	const char *requested   = strrchr(req_info->local_uri, '/');
	const size_t rsrc_idx   = !mg_strncasecmp(requested, EMISS_URI_INDEX, 2)
                            ? 0 : !mg_strncasecmp(requested, EMISS_URI_NEW, 2)
                            ? 1 : !mg_strncasecmp(requested, "/param.js", 2)
                            ? 2 : !mg_strncasecmp(requested, "/verge.min.js", 2)
                            ? 3 : !mg_strncasecmp(requested, EMISS_URI_ABOUT, 2)
                            ? 4 : 0;
    const char *resource    = emiss_get_static_resource(rsrc_ctx, rsrc_idx);
    const char *mime_type   = resource[strlen(resource) - 1] == 's'
                            ? HTTP_MIMETYPE_JS
                            : HTTP_MIMETYPE_HTML;
    ret = mg_printf(conn, HTTP_RESPONSE_HDR, 200, RES_200_TXT,
            (uintmax_t) emiss_get_static_resource_size(rsrc_ctx, rsrc_idx),
            mime_type, "close", TRANSFER_ENCODING_NONE, "");

    if (ret < 1) {
        EXPLAIN_SEND_FAILURE(ret);
        return ret < 0 ? -1 : 418;
    }
    ret = mg_printf(conn, "%s", resource);
    if (ret < 1) {
        EXPLAIN_SEND_FAILURE(ret);
        return ret < 0 ? -1 : 418;
    }
    return 200;
}

static int
template_resource_request_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *req_info = mg_get_request_info(conn);
	int ret = mg_strncasecmp(req_info->request_method, "GET", 3);
	if (ret)
        return inl_send_error_response(conn, 405);

	const char *requested = strrchr(req_info->request_uri, '/') + 1;
	emiss_template_st *template_data = (emiss_template_st *)cbdata;
	size_t i = 0;
	do {
		if (strstr(requested, template_data->template_name[i])) {
			emiss_template_ft *template_func = template_data->template_function[i];
			ret = template_func(template_data, i, req_info->query_string, (void *)conn);
            if (ret < 0) {
                EXPLAIN_SEND_FAILURE(ret);
                return ret < 0 ? -1 : 418;
            }
			return 200;
		}
	} while (++i < EMISS_NTEMPLATES);

	return inl_send_error_response(conn, 404);
}

static void
dyno_signal_handler(int sig)
{
    if (sig == SIGTERM)
        terminate = 1;
}

#ifndef NDEBUG
static int
exit_request_handler(struct mg_connection *conn, void *cbdata)
{
    terminate = 1;
    fprintf((FILE *)cbdata, "SERVER EXITING.\n");
    mg_printf(conn, "SERVER WILL CLOSE.");
    return 200;
}
#endif

/*  IMPLEMENTATION OF FUNCTION PROTOTYPES IN: 'emiss.h' */

emiss_server_ctx_st *
emiss_init_server_ctx(emiss_template_st *template_data)
{
	int ret;
	struct emiss_server_ctx *server = calloc(1, sizeof(struct emiss_server_ctx));
	check(server, ERR_MEM, EMISS_MSG);

	/* 	Initialize the CivetWeb server. */
    mg_init_library(0);
    /*const char *options[] = {
        "document_root", "../resources/html",
        "listening_ports", EMISS_SERVER_PORT,
        "auth_dom_check", CIVET_AUTH_DOM_CHECK,

    };
    server->civet_callbacks.log_message = civet_log_message_redirect;*/
    struct mg_context *civet_ctx = mg_start(&server->civet_callbacks, 0, NULL);
	check(civet_ctx, ERR_FAIL, EMISS_MSG, "initializing Civetweb server");
	server->civet_ctx = civet_ctx;
	server->ports_count = mg_get_server_ports(civet_ctx, 32, server->civet_ports);
    check(server->ports_count >= 0, ERR_FAIL, EMISS_MSG, "fetching ports");

	/* 	Set up a signal handler. */
	server->sigactor.sa_handler = dyno_signal_handler;
    sigemptyset(&server->sigactor.sa_mask);
    server->sigactor.sa_flags = SA_RESTART;

#ifndef NDEBUG
	/*  Gather system information for diagnostics. */
    ret = inl_get_sys_info(server);
    if (!ret)
		log_warn(ERR_FAIL, EMISS_MSG, "obtaining system information.");
#endif

    /*  Associate a function to print output to client connection. */
    template_data->output_function = emiss_conn_printf_function;
    /*  Hook up template data. */
    server->template_data = template_data;

    /*  Set up request handler callbacks for CivetWeb. */
    mg_set_request_handler(civet_ctx, EMISS_URI_INDEX,
            static_resource_request_handler, template_data->rsrc_ctx);
    mg_set_request_handler(civet_ctx, EMISS_URI_NEW,
            static_resource_request_handler, template_data->rsrc_ctx);
	mg_set_request_handler(civet_ctx, EMISS_URI_PARAM_JS,
            static_resource_request_handler, template_data->rsrc_ctx);
    mg_set_request_handler(civet_ctx, EMISS_URI_VERGE_JS,
            static_resource_request_handler, template_data->rsrc_ctx);

    mg_set_request_handler(civet_ctx, EMISS_URI_SHOW,
            template_resource_request_handler, server->template_data);
    mg_set_request_handler(civet_ctx, EMISS_URI_CHART_JS,
            template_resource_request_handler, server->template_data);

    mg_set_request_handler(civet_ctx, EMISS_URI_STYLE_CSS,
            css_request_handler, EMISS_RESOURCE_ROOT EMISS_URI_STYLE_CSS);
    mg_set_request_handler(civet_ctx, EMISS_URI_FONTS,
            font_request_handler, EMISS_VALID_FONT_NAMES);

#ifndef NDEBUG
    mg_set_request_handler(civet_ctx, EMISS_URI_EXIT, exit_request_handler, stdout);
#endif

	return server;
error:
    if (server) {
        emiss_free_server_ctx(server);
    }
	return NULL;
}

void
emiss_free_server_ctx(emiss_server_ctx_st *server_ctx)
{
    if (server_ctx) {
        if (server_ctx->civet_ctx) {
            mg_stop(server_ctx->civet_ctx);
            mg_exit_library();
        }
        if (server_ctx->sys_info)
            free(server_ctx->sys_info);
        free(server_ctx);
    }
}

/*  A main event loop. */

int
emiss_server_run(emiss_server_ctx_st *server_ctx)
{
    check(server_ctx, ERR_NALLOW, EMISS_ERR, "NULL server_ctx parameter");
	terminate = 0;

    int ret = sigaction(SIGTERM, &server_ctx->sigactor, NULL);
    check(ret == 0, ERR_FAIL, EMISS_MSG, "to set signal handler, aborting process");

    int i;
    for (i = 0; i < server_ctx->ports_count && i < 32; i++) {
		const char *protocol = DEFINE_PROTOCOL(server_ctx, i);
    	if ((server_ctx->civet_ports[i].protocol & 1) == 1) {
            fprintf(stdout, "%s IPv4 connection on port %d\n", protocol, i);
    	}
    }

    while (!terminate) {
        sleeper(1);
    }

    emiss_free_server_ctx(server_ctx);
	fprintf(stdout, "Server stopped without errors.\n");
	return EXIT_SUCCESS;
error:
    emiss_free_server_ctx(server_ctx);
    fprintf(stderr, "Server stopped on error.\n");
    return EXIT_FAILURE;
}
