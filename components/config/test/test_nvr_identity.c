/* test_nvr_identity.c — nvr_identity 读写往返 + 缺文件回退单测(host)
 * NVR_USER_DIR 由 CMake 定义重定向到 <build>/id_test_user。 */
#include "nvr_identity.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef NVR_USER_DIR
#error "test expects NVR_USER_DIR compile define"
#endif

static void write_file(const char *rel, const char *content)
{
    char path[512];
    snprintf(path, sizeof(path), NVR_USER_DIR "/%s", rel);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs(content, f);
    fclose(f);
}

static void rm_file(const char *rel)
{
    char path[512];
    snprintf(path, sizeof(path), NVR_USER_DIR "/%s", rel);
    remove(path);
}

int main(void)
{
    mkdir(NVR_USER_DIR, 0755);
    /* 清场 */
    rm_file("OWLSerialNumber"); rm_file("mac_addr_v2"); rm_file("mac_addr_v2.eth1");
    rm_file("tutk_agent_udid"); rm_file("OWL/tutkdata.json"); rm_file("OWLModel");

    char buf[128], av[128];

    /* --- 缺文件回退 --- */
    nvr_identity_cache_invalidate();
    assert(nvr_identity_get_sn(buf, sizeof(buf)) == 0 && buf[0] == 0);
    assert(nvr_identity_get_uid(buf, sizeof(buf)) == 0 && buf[0] == 0);
    /* creds 缺文件 → 缺省 88888888 / 888888 */
    nvr_identity_get_tutk_creds(buf, sizeof(buf), av, sizeof(av));
    assert(strcmp(buf, "88888888") == 0);
    assert(strcmp(av, "888888") == 0);

    /* --- SN 只读 + trim 尾换行 --- */
    write_file("OWLSerialNumber", "P1IQFLXD4FEJ\n");
    nvr_identity_cache_invalidate();
    assert(nvr_identity_get_sn(buf, sizeof(buf)) == 12);
    assert(strcmp(buf, "P1IQFLXD4FEJ") == 0);

    /* --- MAC eth0/eth1 文件优先 --- */
    write_file("mac_addr_v2", "00:50:56:36:DE:6A\n");
    write_file("mac_addr_v2.eth1", "00:50:56:6F:D2:F9\n");
    nvr_identity_cache_invalidate();
    assert(nvr_identity_get_mac("eth0", buf, sizeof(buf)) == 17);
    assert(strcmp(buf, "00:50:56:36:DE:6A") == 0);
    assert(nvr_identity_get_mac("eth1", buf, sizeof(buf)) == 17);
    assert(strcmp(buf, "00:50:56:6F:D2:F9") == 0);
    /* NULL iface 视作 eth0 */
    assert(nvr_identity_get_mac(NULL, buf, sizeof(buf)) == 17);
    assert(strcmp(buf, "00:50:56:36:DE:6A") == 0);

    /* --- MODEL 缺文件 → 缺省 NOP12-32,读写往返 --- */
    assert(nvr_identity_get_model(buf, sizeof(buf)) == (int)strlen("NOP12-32"));
    assert(strcmp(buf, "NOP12-32") == 0);
    assert(nvr_identity_set_model("NOP12-32-CUSTOM") == 0);
    assert(nvr_identity_get_model(buf, sizeof(buf)) == (int)strlen("NOP12-32-CUSTOM"));
    assert(strcmp(buf, "NOP12-32-CUSTOM") == 0);
    /* 空串拒写,不改已有值 */
    assert(nvr_identity_set_model("") < 0);
    assert(nvr_identity_get_model(buf, sizeof(buf)) > 0 && strcmp(buf, "NOP12-32-CUSTOM") == 0);
    rm_file("OWLModel");

    /* --- UID 读写往返 --- */
    assert(nvr_identity_set_uid("ATDUZ7XNFKTTJP79111A") == 0);
    assert(nvr_identity_get_uid(buf, sizeof(buf)) == 20);
    assert(strcmp(buf, "ATDUZ7XNFKTTJP79111A") == 0);

    /* --- TUTK creds 读写往返(设备真实值 00000000 / 888888) --- */
    assert(nvr_identity_set_tutk_creds("00000000", "888888") == 0);
    nvr_identity_get_tutk_creds(buf, sizeof(buf), av, sizeof(av));
    assert(strcmp(buf, "00000000") == 0);
    assert(strcmp(av, "888888") == 0);

    /* --- 部分更新:只改 IOTCKey,保留 AvPassword --- */
    assert(nvr_identity_set_tutk_creds("12345678", NULL) == 0);
    nvr_identity_get_tutk_creds(buf, sizeof(buf), av, sizeof(av));
    assert(strcmp(buf, "12345678") == 0);
    assert(strcmp(av, "888888") == 0);

    /* --- 只取一个字段(另一个传 NULL) --- */
    nvr_identity_get_tutk_creds(buf, sizeof(buf), NULL, 0);
    assert(strcmp(buf, "12345678") == 0);

    /* --- provisioning:缺 tutkdata.json 时建缺省,不覆盖已有 --- */
    rm_file("OWL/tutkdata.json");
    nvr_identity_ensure_provisioned();               /* 缺 → 建缺省 */
    nvr_identity_get_tutk_creds(buf, sizeof(buf), av, sizeof(av));
    assert(strcmp(buf, "88888888") == 0 && strcmp(av, "888888") == 0);
    /* 已存在 → 不覆盖真实值 */
    assert(nvr_identity_set_tutk_creds("00000000", "888888") == 0);
    nvr_identity_ensure_provisioned();
    nvr_identity_get_tutk_creds(buf, sizeof(buf), av, sizeof(av));
    assert(strcmp(buf, "00000000") == 0);

    printf("test_nvr_identity: ALL PASS\n");
    return 0;
}
