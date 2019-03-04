#include "emiss.h"

int
main(int argc, const char *argv[]) {

    /* Init resource context, checking for updates and performing them if available. */
    emiss_resource_ctx_st *rsrc_ctx = emiss_resource_ctx_init();
    check(rsrc_ctx, ERR_FAIL, EMISS_ERR, "setting up resource context");
    emiss_template_st *template_data = emiss_resource_template_init(rsrc_ctx);
    check(template_data, ERR_FAIL, EMISS_ERR, "setting up templates");

    /* Init server & run event loop. */
    emiss_server_ctx_st *server_ctx = emiss_server_ctx_init(template_data);
    check(server_ctx, ERR_FAIL, EMISS_ERR, "setting up server context");
    emiss_server_run(server_ctx);

    /* Free all resources and exit. */
    emiss_resource_template_free(template_data);
    emiss_resource_ctx_free(rsrc_ctx);
    return 1;
error:
    return 0;
}
