/* test_dev_classify — 三分类判定（真实 Discovery scopes 样例，见 BIND_IPC_FLOW §1） */
#include "nvr_dev_classify.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c, m) do{ if(!(c)){ printf("FAIL: %s\n", m); g_fail++; } else printf("ok: %s\n", m); }while(0)

int main(void)
{
    nvr_dev_class_t r;

    /* 1) NOP 设备：nopVersion + A1C2B3 + mac */
    nvr_dev_classify(
        "onvif://www.onvif.org/A1C2B3/mac/54:2b:57:70:98:10 "
        "onvif://www.onvif.org/nopVersion/2.0", &r);
    CHECK(r.kind == NVR_DEV_KIND_NOP, "NOP kind");
    CHECK(r.backend == NVR_BACKEND_NOP, "NOP backend=NOP passthrough");
    CHECK(r.nop_version_x == 2, "nopVersion major=2");
    CHECK(strcmp(r.mac, "54:2b:57:70:98:10") == 0, "mac extracted");
    CHECK(r.is_nightowl == 1, "NightOwl by mac prefix");

    /* 2) nopOnvif 设备：nopOnvif + nopState/active */
    nvr_dev_classify(
        "onvif://www.onvif.org/nopOnvif/1.0 "
        "onvif://www.onvif.org/nopState/active", &r);
    CHECK(r.kind == NVR_DEV_KIND_NOPONVIF, "NOPONVIF kind");
    CHECK(r.backend == NVR_BACKEND_ONVIF, "NOPONVIF backend=ONVIF");
    CHECK(r.active == 1, "nopState/active detected");

    /* 3) 通用 onvif：无标识 */
    nvr_dev_classify(
        "onvif://www.onvif.org/type/video_encoder "
        "onvif://www.onvif.org/Profile/Streaming "
        "onvif://www.onvif.org/name/SomeVendorCam", &r);
    CHECK(r.kind == NVR_DEV_KIND_ONVIF, "ONVIF kind (no marker)");
    CHECK(r.backend == NVR_BACKEND_ONVIF, "ONVIF backend");
    CHECK(r.is_nightowl == 0, "not NightOwl");

    /* 4) bound 标记 */
    nvr_dev_classify("onvif://www.onvif.org/nopVersion/1.0 onvif://www.onvif.org/bound", &r);
    CHECK(r.bound == 1, "bound detected");
    CHECK(r.kind == NVR_DEV_KIND_NOP, "bound NOP still NOP");

    /* 5) 大小写不敏感 + 空 */
    nvr_dev_classify("ONVIF://WWW.ONVIF.ORG/NOPONVIF/1.0", &r);
    CHECK(r.kind == NVR_DEV_KIND_NOPONVIF, "case-insensitive nopOnvif");
    nvr_dev_classify("", &r);
    CHECK(r.kind == NVR_DEV_KIND_ONVIF, "empty → ONVIF default");

    /* 6) name/NightOwl 作为厂商判据 */
    nvr_dev_classify("onvif://www.onvif.org/name/NightOwl onvif://www.onvif.org/nopOnvif/1.0", &r);
    CHECK(r.is_nightowl == 1, "NightOwl by name scope");

    /* 7) 提取设备 SN(/serial/) —— 激活密码用 */
    nvr_dev_classify("onvif://www.onvif.org/nopOnvif/1.0 onvif://www.onvif.org/serial/ABC123XYZ", &r);
    CHECK(strcmp(r.serial, "ABC123XYZ") == 0, "serial 从 /serial/ 提取");
    nvr_dev_classify("onvif://www.onvif.org/sn/SN0099", &r);
    CHECK(strcmp(r.serial, "SN0099") == 0, "serial 从 /sn/ 提取");

    printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
