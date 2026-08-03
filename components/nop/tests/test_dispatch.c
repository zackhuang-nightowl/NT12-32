/* End-to-end contract test through the public app facade + stub HAL. */
#include "nop_sdk/nop_app.h"
#include "nop_sdk/nop_caps.h"
#include "nop_sdk/nop_version.h"
#include "hal_stub.h"
#include "test_util.h"

static int status_of(nop_app_t *app, const char *json)
{
    char *out = NULL;
    int   sc;
    nop_app_dispatch(app, json, &out);
    sc = resp_status(out);
    nop_app_free_response(out);
    return sc;
}

int main(void)
{
    nop_app_config_t cfg;
    nop_app_t       *app;
    char            *out = NULL;

    hal_stub_register_all();   /* provides HAL_SYSTEM/VIDEO/PTZ/LIGHT */

    memset(&cfg, 0, sizeof(cfg));
    cfg.role = NOP_ROLE_IPC;
    cfg.auto_caps = 1;         /* light up stream/ptz/light from HALs */
    app = nop_app_create(&cfg);
    CHECK(app != NULL);

    /* getDeviceInfo -> 200 with the stub model */
    nop_app_dispatch(app, "{\"func\":\"getDeviceInfo\",\"args\":{}}", &out);
    CHECK_EQ_INT(resp_status(out), 200);
    { char *m = resp_content_str(out, "model"); CHECK_STR(m, "NOP-STUB-CAM"); free(m); }
    nop_app_free_response(out);

    /* getAPIVersion -> 200 with the advertised version */
    nop_app_dispatch(app, "{\"func\":\"X_NightOwl_getAPIVersion\",\"args\":{}}", &out);
    CHECK_EQ_INT(resp_status(out), 200);
    { char *v = resp_content_str(out, "version"); CHECK_STR(v, NOP_API_VERSION_STR); free(v); }
    nop_app_free_response(out);

    /* getDeviceCapabilities -> 200 */
    CHECK_EQ_INT(status_of(app, "{\"func\":\"X_NightOwl_getDeviceCapabilities\",\"args\":{}}"), 200);

    /* unknown command -> 501 */
    CHECK_EQ_INT(status_of(app, "{\"func\":\"thisDoesNotExist\",\"args\":{}}"), 501);

    /* createCredential: missing args -> 400, full args -> 200 */
    CHECK_EQ_INT(status_of(app, "{\"func\":\"createCredential\",\"args\":{}}"), 400);
    CHECK_EQ_INT(status_of(app,
        "{\"func\":\"createCredential\",\"args\":{\"username\":\"u\",\"password\":\"p\"}}"), 200);

    /* light command: gate off -> 501, gate on -> 200 (HAL present via stub) */
    nop_app_set_capability(app, CAP_LIGHT, 0);
    CHECK_EQ_INT(status_of(app,
        "{\"func\":\"setChannelLightSwitch\",\"args\":{\"channel\":0,\"enable\":true}}"), 501);
    nop_app_set_capability(app, CAP_LIGHT, 1);
    CHECK_EQ_INT(status_of(app,
        "{\"func\":\"setChannelLightSwitch\",\"args\":{\"channel\":0,\"enable\":true}}"), 200);

    /* light command missing required arg -> 400 */
    CHECK_EQ_INT(status_of(app,
        "{\"func\":\"setChannelLightSwitch\",\"args\":{\"channel\":0}}"), 400);

    /* reboot -> 200 via HAL_SYSTEM stub */
    CHECK_EQ_INT(status_of(app, "{\"func\":\"reboot\",\"args\":{}}"), 200);

    nop_app_destroy(app);
    TEST_RETURN();
}
