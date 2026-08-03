#include "nop/nop_envelope.h"
#include "test_util.h"

int main(void)
{
    nop_request_t  req;
    nop_response_t resp;
    char          *out;

    /* valid request */
    {
        const char *r = "{\"func\":\"getDeviceInfo\",\"args\":{\"x\":1}}";
        CHECK_EQ_INT(nop_envelope_parse(r, strlen(r), &req), NOP_OK);
        CHECK_STR(req.func, "getDeviceInfo");
        CHECK(req.args != NULL);
        CHECK_EQ_INT((int)nop_json_num(req.args, "x", 0), 1);
        nop_request_free(&req);
    }

    /* missing func -> param error */
    { const char *r = "{\"args\":{}}"; CHECK(nop_envelope_parse(r, strlen(r), &req) != NOP_OK); }

    /* malformed json -> param error */
    { const char *r = "{not json"; CHECK(nop_envelope_parse(r, strlen(r), &req) != NOP_OK); }

    /* build response with content */
    nop_response_init(&resp);
    resp.content = nop_json_obj();
    nop_json_add_str(resp.content, "model", "NOP-X");
    out = nop_envelope_build(&resp);
    CHECK(out != NULL);
    CHECK_EQ_INT(resp_status(out), 200);
    {
        char *m = resp_content_str(out, "model");
        CHECK_STR(m, "NOP-X");
        free(m);
    }
    nop_envelope_free_str(out);
    nop_response_free(&resp);   /* content already consumed; safe */

    TEST_RETURN();
}
