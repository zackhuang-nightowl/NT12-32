#include "nop/nop_router.h"
#include "capability/cap_registry.h"
#include "base/nop_json.h"
#include "test_util.h"

static nop_status_t handle_ping(const nop_request_t *req, nop_response_t *resp, void *ctx)
{
    (void)req; (void)ctx;
    resp->content = nop_json_obj();
    nop_json_add_str(resp->content, "pong", "ok");
    return NOP_OK;
}

int main(void)
{
    cap_registry_t *reg = cap_registry_create();
    nop_router_t   *r   = nop_router_create(reg);
    char           *out;

    CHECK(reg && r);

    /* ping under always-on CAP_DEVICE, gated_ping under CAP_PTZ (off) */
    CHECK_EQ_INT(nop_router_register(r, "ping", CAP_DEVICE, handle_ping), NOP_OK);
    CHECK_EQ_INT(nop_router_register(r, "gated_ping", CAP_PTZ, handle_ping), NOP_OK);
    CHECK_EQ_INT((int)nop_router_count(r), 2);

    /* success */
    nop_router_dispatch(r, "{\"func\":\"ping\",\"args\":{}}", 25, &out);
    CHECK_EQ_INT(resp_status(out), 200);
    { char *p = resp_content_str(out, "pong"); CHECK_STR(p, "ok"); free(p); }
    nop_router_free_response(out);

    /* unknown command -> 501 */
    nop_router_dispatch(r, "{\"func\":\"nope\",\"args\":{}}", 25, &out);
    CHECK_EQ_INT(resp_status(out), 501);
    nop_router_free_response(out);

    /* gated command -> 501, then enable -> 200 */
    nop_router_dispatch(r, "{\"func\":\"gated_ping\",\"args\":{}}", 31, &out);
    CHECK_EQ_INT(resp_status(out), 501);
    nop_router_free_response(out);

    cap_registry_enable(reg, CAP_PTZ, true);
    nop_router_dispatch(r, "{\"func\":\"gated_ping\",\"args\":{}}", 31, &out);
    CHECK_EQ_INT(resp_status(out), 200);
    nop_router_free_response(out);

    /* malformed -> 400 */
    nop_router_dispatch(r, "{bad", 4, &out);
    CHECK_EQ_INT(resp_status(out), 400);
    nop_router_free_response(out);

    /* batchCmd: one known + one unknown */
    {
        const char *batch =
            "{\"func\":\"batchCmd\",\"args\":{\"cmds\":["
            "{\"func\":\"ping\",\"args\":{}},"
            "{\"func\":\"nope\",\"args\":{}}]}}";
        cJSON *root, *content, *results, *e0, *e1;
        nop_router_dispatch(r, batch, strlen(batch), &out);
        CHECK_EQ_INT(resp_status(out), 200);
        root = cJSON_Parse(out);
        content = cJSON_GetObjectItem(root, "content");
        results = cJSON_GetObjectItem(content, "results");
        CHECK(cJSON_IsArray(results));
        CHECK_EQ_INT(cJSON_GetArraySize(results), 2);
        e0 = cJSON_GetArrayItem(results, 0);
        e1 = cJSON_GetArrayItem(results, 1);
        CHECK_EQ_INT(cJSON_GetObjectItem(e0, "statusCode")->valueint, 200);
        CHECK_EQ_INT(cJSON_GetObjectItem(e1, "statusCode")->valueint, 501);
        cJSON_Delete(root);
        nop_router_free_response(out);
    }

    nop_router_destroy(r);
    cap_registry_destroy(reg);
    TEST_RETURN();
}
