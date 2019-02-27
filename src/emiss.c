#include "emiss.h"

int
main(int argc, const char *argv[]) {
    /* -->  [Check for options passed as arguments] <-- */
    int ret = 0;
    /*  Check for updates. */
    /* ret = emiss_should_check_for_update();
    if (!ret) {
        log_info("[%s]: Data up to date.", EMISS_MSG);
    } else {
        if (ret == -1) {
            log_warn(ERR_FAIL, EMISS_ERR,
                "retrieving last access time - will try to update");
        } else {
            log_info("[%s]: Data out of date - will try to update.", EMISS_MSG);
        }
        ret = emiss_retrieve_data();
        check(ret != -1, ERR_FAIL, EMISS_ERR, "performing data update");
    }*/

    /*  Init resource context.  */

    emiss_resource_ctx_st *rsrc_ctx = emiss_init_resource_ctx();
    check(rsrc_ctx, ERR_FAIL, EMISS_ERR, "setting up resource context");
    emiss_template_st *template_data = emiss_construct_template_structure(rsrc_ctx);
    check(template_data, ERR_FAIL, EMISS_ERR, "setting up templates");

    /*  Init server & run event loop. */

    emiss_server_ctx_st *server_ctx = emiss_init_server_ctx(template_data);
    check(server_ctx, ERR_FAIL, EMISS_ERR, "setting up server context");
    ret = emiss_server_run(server_ctx);
    emiss_free_template_structure(template_data);

    /*  Free all resources and exit. */

    emiss_free_resource_ctx(rsrc_ctx);
error:
    return ret;
}
