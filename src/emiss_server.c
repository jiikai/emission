#include "emiss.h"

#include <sys/random.h>

#include "dbg.h"

/*  A volatile variable that gets switched on SIGTERM or other exit request */
volatile sig_atomic_t terminate;

/*
**  MACRO DEFINITIONS
*/

#define CHECK_REQ_METHOD_EQ(method, len)\
 	mg_strncasecmp(req_info->request_method, method, len) ? 0 : 1

/*
**  TYPE AND STRUCT DEFINITIONS
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
**  FUNCTION DEFINITIONS
*/

/*  STATIC & INLINE FUNCTIONS */

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

/*  STATIC FUNCTIONS */

/*  A logger callback for CivetWeb */
static int
log_message(const struct mg_connection *conn, const char *message)
{
	printf("[CivetWeb]: %s\n", message);
	return 1;
}

static struct mg_context *
inl_civet_init_start(struct emiss_server_ctx *server)
{
    mg_init_library(0);
    //const char *options[] = {"document_root", "../resources/html"};
    server->civet_callbacks.log_message = log_message;
    struct mg_context *ctx;
    ctx = mg_start(&server->civet_callbacks, 0, NULL);
    return ctx;
}

static int
emiss_conn_printf_function(void *at,
    const signed http_response_code,
    const char *mime_type,
    const char *conn_action,
    const char *restrict frmt, ...)
{
	struct mg_connection *conn = (struct mg_connection *)at;
    mg_printf(conn, HTTP_RES_200_ARGS, mime_type, conn_action);
	va_list args;
	va_start(args, frmt);
    printf("asdf\n");
	int ret = modified_mg_vprintf(conn, frmt, args);
    va_end(args);
	return ret;
}

/*  Request handler callbacks for CivetWeb. */

static int
css_request_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *req_info = mg_get_request_info(conn);
    int ret = mg_strncasecmp(req_info->request_method, "GET", 3);
    check(!ret, ERR_NALLOW_A, "YOGISERVER", req_info->request_method, "GET");
	mg_send_mime_file(conn, (char *)cbdata, "text/css");
	return 200;
error:
	mg_printf(conn, HTTP_RES_405);
	return 405;
}

static int
static_resource_request_handler(struct mg_connection *conn, void *cbdata)
{
    const struct mg_request_info *req_info = mg_get_request_info(conn);
    check(CHECK_REQ_METHOD_EQ("GET", 3), ERR_NALLOW_A, EMISS_MSG,
        req_info->request_method, "GET");
	const char *requested = strrchr(req_info->local_uri, '/');
	int rsrc_idx =  mg_strncasecmp(requested, EMISS_URI_INDEX, 4) == 0
                    ? 0 : mg_strncasecmp(requested, EMISS_URI_NEW, 4) == 0
                    ? 1 : 2;
    char *resource = emiss_get_static_resource((emiss_resource_ctx_st *)cbdata, rsrc_idx);
    const char *mime_type = resource[strlen(resource) - 1] == 's'
                            ? "application/javascript"
                            : "text/html";
    mg_printf(conn, HTTP_RES_200_ARGS, mime_type, "close");
    mg_printf(conn, "%s", resource);
    return 200;
error:
    mg_printf(conn, HTTP_RES_405);
    return 405;
}

static int
template_resource_request_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *req_info = mg_get_request_info(conn);
	int ret = mg_strncasecmp(req_info->request_method, "GET", 3);
	check(ret == 0, ERR_NALLOW_A, EMISS_MSG, req_info->request_method, "GET");
	const char *requested = strrchr(req_info->request_uri, '/') + 1;
	emiss_template_st *template_data = (emiss_template_st *)cbdata;
	int i = 0;
	do {
		if (strstr(requested, template_data->template_name[i])) {
			emiss_template_ft *template_func = template_data->template_function[i];
			template_func(template_data, i, req_info->query_string, (void *)conn);
			return 200;
		}
	} while (++i < EMISS_NTEMPLATES);
	mg_printf(conn, HTTP_RES_404);
	return 404;
error:
    mg_printf(conn, HTTP_RES_405);
    return 405;
}

static void
dyno_signal_handler(int sig)
{
	log_info("Signaling event: %d", sig);
    if (sig == SIGTERM) {
        terminate = 1;
    }
}

#ifndef NDEBUG
static int
exit_request_handler(struct mg_connection *conn, void *cbdata)
{
    terminate = 1;
    fprintf((FILE *)cbdata, "SERVER EXITING.\n");
    mg_printf(conn, HTTP_RES_200_ARGS, "text/plain", "close");
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
	struct mg_context *civet_ctx = inl_civet_init_start(server);
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
    if (!ret) {
		log_warn(ERR_FAIL, EMISS_MSG, "obtaining system information.");
	}
#endif

    /*  Associate a function to print output to client connection. */
    template_data->output_function = emiss_conn_printf_function;
    /*  Hook up template data. */
    server->template_data = template_data;

    /*  Set up request handler callbacks for CivetWeb. */
    mg_set_request_handler(civet_ctx, EMISS_URI_NEW,
        static_resource_request_handler, template_data->rsrc_ctx);
	mg_set_request_handler(civet_ctx, EMISS_URI_CHART_PARAM_JS,
        static_resource_request_handler, template_data->rsrc_ctx);
    mg_set_request_handler(civet_ctx, EMISS_URI_CHART,
        template_resource_request_handler, server->template_data);
    mg_set_request_handler(civet_ctx, EMISS_URI_LINE_CHART_JS,
        template_resource_request_handler, server->template_data);
    mg_set_request_handler(civet_ctx, EMISS_URI_MAP_CHART_JS,
        template_resource_request_handler, server->template_data);
    mg_set_request_handler(civet_ctx, EMISS_URI_STYLE_CSS,
        css_request_handler, EMISS_CSS_ROOT"/style.css");
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
            if (server_ctx->civet_ctx) {
				/* 	Call to mg_stop() will block until all of CivetWeb's worker
					(== connection) threads have returned.
				*/
                mg_stop(server_ctx->civet_ctx);
            }
            if (server_ctx->sys_info) {
                free(server_ctx->sys_info);
            }
            free(server_ctx);
        }
        mg_exit_library();
    }
}

/*  A main event loop. */

int emiss_server_run(emiss_server_ctx_st *server_ctx)
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
