/* test_nvr_settings — KV / 结构化表 / 订阅 / 种子 冒烟测试 */
#include "nvr_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_fail = 0;
#define CHECK(c, msg) do{ if(!(c)){ printf("FAIL: %s\n", msg); g_fail++; } else printf("ok: %s\n", msg); }while(0)

static int g_notified = 0;
static char g_last_key[64];
static void on_change(void *ud, const char *key){ (void)ud; g_notified++; snprintf(g_last_key,sizeof(g_last_key),"%s",key); }

int main(void)
{
    const char *db = "/tmp/nvr_settings_test.db";
    unlink(db); unlink("/tmp/nvr_settings_test.db-wal"); unlink("/tmp/nvr_settings_test.db-shm");

    nvr_settings_t *s = NULL;
    CHECK(nvr_settings_open(db, NULL, &s) == 0 && s, "open (no seed)");

    /* KV int/str */
    CHECK(nvr_settings_set_int(s, "cloud.switch", 1) == 0, "set_int cloud.switch");
    CHECK(nvr_settings_get_int(s, "cloud.switch", -1) == 1, "get_int cloud.switch==1");
    CHECK(nvr_settings_get_int(s, "missing.key", 42) == 42, "get_int default");

    CHECK(nvr_settings_set_str(s, "system.device_name", "Lobby-NVR") == 0, "set_str name");
    char buf[64];
    nvr_settings_get_str(s, "system.device_name", buf, sizeof(buf), "x");
    CHECK(strcmp(buf, "Lobby-NVR") == 0, "get_str name");

    /* blob (模拟口令 hash) */
    unsigned char h[16]; for (int i=0;i<16;i++) h[i]=(unsigned char)i;
    CHECK(nvr_settings_set_blob(s, "auth.raw", h, 16) == 0, "set_blob");
    unsigned char o[16]; int n = nvr_settings_get_blob(s, "auth.raw", o, sizeof(o));
    CHECK(n == 16 && memcmp(h, o, 16) == 0, "get_blob roundtrip");

    /* 订阅 */
    int sub = nvr_settings_subscribe(s, "cloud.", on_change, NULL);
    CHECK(sub > 0, "subscribe");
    g_notified = 0;
    nvr_settings_set_int(s, "cloud.switch", 0);
    CHECK(g_notified == 1 && strcmp(g_last_key,"cloud.switch")==0, "notify on cloud.* change");
    g_notified = 0;
    nvr_settings_set_str(s, "system.other", "x");
    CHECK(g_notified == 0, "no notify for non-matching prefix");

    /* set_many 事务 */
    nvr_kv_t kv[2] = { {"a.x",0,7,NULL}, {"a.y",1,0,"hi"} };
    CHECK(nvr_settings_set_many(s, kv, 2) == 0, "set_many");
    CHECK(nvr_settings_get_int(s, "a.x", -1) == 7, "set_many int");
    nvr_settings_get_str(s, "a.y", buf, sizeof(buf), "");
    CHECK(strcmp(buf,"hi")==0, "set_many str");

    /* channel 结构化 */
    nvr_chan_row_t r; memset(&r,0,sizeof(r));
    r.chn = 17; r.enabled = 1; r.record = 1; r.kind = 2; r.onvif_port = 80;
    snprintf(r.name,sizeof(r.name),"Gate"); snprintf(r.onvif_ip,sizeof(r.onvif_ip),"192.168.9.55");
    snprintf(r.source,sizeof(r.source),"user");
    CHECK(nvr_settings_channel_upsert(s, &r) == 0, "channel upsert");
    r.chn = 18; snprintf(r.name,sizeof(r.name),"Yard");
    nvr_settings_channel_upsert(s, &r);
    nvr_chan_row_t list[8]; int cn = nvr_settings_channel_list(s, list, 8);
    CHECK(cn == 2, "channel_list count==2");
    CHECK(list[0].chn==17 && strcmp(list[0].name,"Gate")==0, "channel row0");
    CHECK(nvr_settings_channel_delete(s, 17) == 0 && nvr_settings_channel_list(s,list,8)==1, "channel delete");

    /* owner */
    nvr_owner_row_t ow; memset(&ow,0,sizeof(ow));
    snprintf(ow.owner_id,sizeof(ow.owner_id),"oid-1");
    snprintf(ow.username,sizeof(ow.username),"ck");
    snprintf(ow.stoken,sizeof(ow.stoken),"STOKEN-ABC");
    CHECK(nvr_settings_owner_set(s, &ow) == 0, "owner set");
    nvr_owner_row_t og; CHECK(nvr_settings_owner_get(s, &og) == 0 && strcmp(og.stoken,"STOKEN-ABC")==0, "owner get stoken");

    /* cloud_channel */
    nvr_cloud_ch_row_t cc; memset(&cc,0,sizeof(cc));
    cc.chn = 1; snprintf(cc.stream_type,sizeof(cc.stream_type),"main");
    snprintf(cc.triggers,sizeof(cc.triggers),"human,face,vehicle");
    CHECK(nvr_settings_cloud_ch_upsert(s, &cc) == 0, "cloud_ch upsert");
    nvr_cloud_ch_row_t cl[4]; int ccn = nvr_settings_cloud_ch_list(s, cl, 4);
    CHECK(ccn==1 && strcmp(cl[0].triggers,"human,face,vehicle")==0, "cloud_ch list");

    nvr_settings_close(s);

    /* 重开：持久化验证 */
    CHECK(nvr_settings_open(db, NULL, &s) == 0, "reopen");
    CHECK(nvr_settings_get_int(s, "a.x", -1) == 7, "persist across reopen");
    nvr_owner_row_t og2; CHECK(nvr_settings_owner_get(s, &og2)==0 && strcmp(og2.stoken,"STOKEN-ABC")==0, "owner persist");
    nvr_settings_close(s);

    printf("\n%s (%d failures)\n", g_fail? "FAILED":"PASSED", g_fail);
    return g_fail ? 1 : 0;
}
