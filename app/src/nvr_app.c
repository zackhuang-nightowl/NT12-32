/***************************************************************************************
 *  nvr_app.c — 整机编排：config/settings → storage → meta → platform → streaming →
 *              record_sched → preview → event → channel_mgr，串成一台 NVR。
 *
 *  分层：本文件只创建模块、连回调、跑主循环；业务在各 app 模块内，模块间不横向调用。
 *  连续录像由 ③streaming 负责（record=1）；事件/云存由 record_sched 协调；
 *  ②ONVIF 取流以弱符号钩子接入（app/onvif 提供强符号则点亮 PoE 自动取流）。
 ***************************************************************************************/
#include "nvr_app.h"
#include "nvr_config.h"
#include "nvr_gui_config.h"
#include "nvr_defaults.h"
#include "nvr_display_modes.h"
#include "nvr_storage.h"
#include "nvr_streaming.h"
#include "mhal_vout.h"

#include "nvr_channel.h"
#include "nvr_preview.h"
#include "nvr_playback.h"
#include "nvr_record_sched.h"
#include "nvr_event.h"
#include "nvr_push.h"
#include "nvr_nop8012.h"
#include "nvr_settings.h"
#include "nvr_identity.h"
#include "nvr_cloud_uploader.h"
#include "nvr_rtsp_live.h"   /* tunnel 内直播 RTSP 服务(:8554),供 ODC agent P2PTunnel 映射 */
#include "nvr_crypto.h"
#include "nvr_netime.h"
#include "nvr_ble.h"
#include "rsdk.h"
#include "nop_sdk/nop_event.h"
#include "nop_sdk/nop_app.h"
#include "nop_sdk/nop_caps.h"        /* CAP_PTZ:NVR 无本地 HAL_PTZ,须显式开(PTZ 经 ONVIF 映射代理相机) */
#include "nop_sdk/nop_nvr_channels.h"
#include "nvr_cmd_router.h"
#include "nvr_gui_notify.h"
#include "nvr_http_assets.h"
#include "nop_sdk/nop_http_server.h"
#include "nop_sdk/nop_onvif.h"
#include "nvr_chan_persist.h"
#include "nvr_chan_nop_sync.h"
#include "nvr_talk.h"
#include "nvr_onvif.h"
#include "nvr_lan34569.h"   /* UDP 34569 本机应答：App 忘 IP 时找回 NVR */
#include "cJSON.h"
#include "nvr_log.h"
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/reboot.h>
#include <unistd.h>

/* ONVIF 映射后端 API 由 nvr_chan_nop_sync.h → nop_sdk/nop_onvif_map.h 提供，
 * 此处不再前向声明（否则与头中 nop_status_t 返回类型冲突）。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct nvr_app {
    nvr_config_t      cfg;
    nvr_settings_t   *settings;
    nvr_storage_t    *stg;
    rsdk_group_t     *group;
    void             *meta;         /* rsdk_meta ctx（云存状态）；可 NULL */
    nvr_stream_mgr_t *sm;
    nop_event_hub_t  *nop_hub;
    nop_app_t        *nop;          /* NOP 分派后端（回落/翻译） */
    nop_nvr_channels_t *nop_chans;  /* 通道注册表（供 ONVIF 映射按 channel 找设备会话） */
    nop_onvif_map_backend_t *onvif_backend;  /* ONVIF 客户端映射后端（逐通道会话缓存+事件轮询）*/
    nvr_cmd_router_t *router;       /* 8089 入口的请求处理器（本地存/查 + 出图 + channel→设备转发）*/
    nop_http_server_t *nop_http;    /* 唯一 8089 入口(inbound)；处理器=nvr_cmd_dispatch */
    nop_http_server_t *nop_http_agent; /* 6061：ODC TUTK agent 的 cgi 命令端点(iotc-tunnel:6061)，同 handler */
    nvr_rec_sched_t  *rs;
    nvr_preview_t    *pv;
    struct nvr_playback *pb;        /* 本机回放引擎(GUI_playbackControl) */
    struct nvr_nop8012  *n8012;     /* NOP 8012 事件中心客户端(逐相机) */
    nvr_chan_persist_t *persist;    /* 通道映射/能力持久化(channels.json) */
    nvr_evt_hub_t    *eh;
    nvr_push_t       *push;         /* 事件→图床/TPNS */
    nvr_chan_mgr_t   *cm;
    nvr_talk_t       *talk;
    nvr_cloud_uploader_t *up;       /* 云存上传器（有盘+meta+udid 时启动） */
    nvr_ble_t        *ble;          /* BLE 配网通路（复用命令路由；板级链路真机接入） */
    int               tutk_on;      /* TUTK P2P 已启动 */
    int               manual_only;  /* 仅连手动添加的相机（NVR_MANUAL_ONLY）：不自动发现/加载配置通道 */
    signed char       rec_applied[32]; /* 连续录像排程:每通道上次已下发的 record 状态(-1=未知),仅变化时才 set_record */
    signed char       evt_arm_applied[32]; /* 仅事件待命上次下发(-1=未知) */
    int               pre_s_applied[32];   /* 上次下发的预录秒数 */
    volatile int      running;
};

static void app_poke_longpoll(void *user)
{
    nvr_app_t *app = (nvr_app_t *)user;
    if (app && app->cm) nvr_chan_poke_longpoll(app->cm);
}

static int remote_access_effective(nvr_app_t *a);

static void app_talk_enh(void *ud, int chn0, const char *random, const char *penh)
{
    nvr_app_t *a = (nvr_app_t *)ud;
    if (a && a->cm) nvr_chan_set_enh(a->cm, chn0, random, penh);
}

/* 8089 入口处理器：把请求体交给命令路由处理(出图/转发/回落 nop)。
 * 返回 malloc 的应答串，nop_http_server 负责 free。 */
static char *app_http_handler(void *ctx, const char *body)
{
    return nvr_cmd_dispatch((nvr_cmd_router_t *)ctx, body);
}

/* 事件抓拍(独立线程回调):优先用 8012 自带 JPEG,否则 ONVIF GetSnapshot,再 rsdk_pic 落盘。
 * 失败只记日志,不影响事件录像/图标。 */
static void app_on_snap(void *user, int chn, uint64_t eid, uint32_t ts,
                        const uint8_t *inline_jpeg, size_t inline_len)
{
    nvr_app_t *a = user;
    unsigned char *jpeg = NULL;
    int jlen = 0, from_onvif = 0;
    if (inline_jpeg && inline_len > 0) {
        jpeg = (unsigned char *)inline_jpeg;
        jlen = (int)inline_len;
    } else {
        nvr_channel_t ch;
        if (!a || !a->cm || nvr_chan_get(a->cm, chn, &ch) != 0 || !ch.onvif_ip[0]) return;
        if (nvr_onvif_get_snapshot(ch.onvif_ip, ch.onvif_port, ch.user, ch.pass,
                                   ch.video_source_token[0] ? ch.video_source_token : NULL,
                                   &jpeg, &jlen) != 0)
            return;
        from_onvif = 1;
    }
    if (!jpeg || jlen <= 0) return;
#if RSDK_CFG_METADATA
    if (a->group && a->meta) {
        rsdk_dev_t *dev = NULL;
        if (rsdk_balance_pick(a->group, chn, &dev) != RSDK_OK || !dev) {
            if (rsdk_group_count(a->group) > 0)
                dev = rsdk_group_dev(a->group, 0);
        }
        if (dev) {
            rsdk_pic_key_t k;
            memset(&k, 0, sizeof(k));
            k.chn = chn; k.ts = ts; k.event_id = eid; k.type = RSDK_PIC_MAIN;
            uint64_t pid = 0;
            if (rsdk_pic_write(dev, a->meta, &k, jpeg, (size_t)jlen, &pid) != RSDK_OK)
                printf("[app] 事件抓拍落盘失败 ch%d eid=%llu\n", chn, (unsigned long long)eid);
        }
    }
#else
    (void)eid; (void)ts;
#endif
    if (from_onvif) nop_onvif_free_buffer(jpeg);
}

static void app_on_push(void *user, int chn, uint64_t eid, uint32_t ts, nop_detect_type_t type)
{
    nvr_push_on_event((nvr_push_t *)user, chn, eid, ts, type);
}

/* ---- NOP EventExtInfo：打开相机缓存，事件结束后取回写入 meta.db ---- */
static int extinfo_on(nvr_app_t *a, int chn)
{
#if !RSDK_CFG_METADATA
    (void)a; (void)chn; return 0;
#else
    if (!a || !a->meta) return 0;
    if (!a->settings) return 1;
    char k[72];
    snprintf(k, sizeof(k), "ai.event_ext_info.ch.%d.enable", chn);
    int v = nvr_settings_get_int(a->settings, k, -1);
    if (v >= 0) return v != 0;
    return nvr_settings_get_int(a->settings, "ai.event_ext_info.enable", 1) != 0;
#endif
}

static int extinfo_interval_ms(nvr_app_t *a, int chn)
{
    int ms = 1000;
    if (a && a->settings) {
        char k[80];
        snprintf(k, sizeof(k), "ai.event_ext_info.ch.%d.interval_ms", chn);
        ms = nvr_settings_get_int(a->settings, k, 0);
        if (ms <= 0) ms = nvr_settings_get_int(a->settings, "ai.event_ext_info.interval_ms", 1000);
    }
    if (ms < 1000) ms = 1000;
    if (ms > 10000) ms = 10000;
    return ms;
}

static cJSON *nop_post_content(nvr_chan_mgr_t *cm, int chn, const char *func, const char *args)
{
    char *resp = nvr_chan_dev_post(cm, chn, func, args);
    if (!resp) return NULL;
    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return NULL;
    int code = 0;
    cJSON *sc = cJSON_GetObjectItem(root, "statusCode");
    if (cJSON_IsNumber(sc)) code = sc->valueint;
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (!content) {
        cJSON *r = cJSON_GetObjectItem(root, "result");
        if (r) {
            content = cJSON_GetObjectItem(r, "content");
            sc = cJSON_GetObjectItem(r, "statusCode");
            if (cJSON_IsNumber(sc)) code = sc->valueint;
        }
    }
    if (code != 0 && code != 200) { cJSON_Delete(root); return NULL; }
    cJSON *dup = content ? cJSON_Duplicate(content, 1) : cJSON_CreateObject();
    cJSON_Delete(root);
    return dup;
}

static void store_extinfo(nvr_app_t *a, int chn, uint64_t eid, uint32_t start, cJSON *doc)
{
#if RSDK_CFG_METADATA
    if (!a || !a->meta || !doc) return;
    cJSON_DeleteItemFromObject(doc, "error");
    cJSON_DeleteItemFromObject(doc, "startTime");
    cJSON_AddNumberToObject(doc, "startTime", (double)start);
    if (eid) {
        rsdk_meta_query_t q; memset(&q, 0, sizeof(q));
        q.t0 = 0; q.t1 = 0xFFFFFFFFu; q.chn = (int16_t)chn;
        q.event_id = eid; q.doc_type = RSDK_DOC_AI_EVENT; q.limit = 1;
        rsdk_metadoc_list_t lst; memset(&lst, 0, sizeof(lst));
        if (rsdk_meta_query(a->meta, &q, &lst) == RSDK_OK && lst.count > 0) {
            rsdk_meta_free_list(&lst);
            return;
        }
        rsdk_meta_free_list(&lst);
    }
    char *js = cJSON_PrintUnformatted(doc);
    if (!js) return;
    rsdk_meta_key_t k; memset(&k, 0, sizeof(k));
    k.ts = start; k.chn = (int16_t)chn; k.event_id = eid; k.doc_type = RSDK_DOC_AI_EVENT;
    if (rsdk_meta_put(a->meta, &k, js, strlen(js), NULL) == RSDK_OK)
        NVR_LOGI("event", "ch%d EventExtInfo 已存 eid=%llu start=%u", chn,
                 (unsigned long long)eid, start);
    free(js);
#else
    (void)a; (void)chn; (void)eid; (void)start; (void)doc;
#endif
}

static void app_on_meta_enable(void *user, int chn)
{
    nvr_app_t *a = user;
    if (!a || !a->cm || !extinfo_on(a, chn)) return;
    nvr_channel_t ch;
    if (nvr_chan_get(a->cm, chn, &ch) != 0 || ch.backend != 0 || !ch.onvif_ip[0]) return;
    int dev_ch = ch.dev_chn > 0 ? ch.dev_chn : 1;
    int ms = extinfo_interval_ms(a, chn);
    char args[128];
    snprintf(args, sizeof(args),
             "{\"channel\":%d,\"enable\":true,\"collectionIntervalMs\":%d}", dev_ch, ms);
    cJSON *c = nop_post_content(a->cm, chn, "AI_setEventExtInfoConfig", args);
    if (c) {
        NVR_LOGI("event", "ch%d 已打开相机 EventExtInfo interval=%dms", chn, ms);
        cJSON_Delete(c);
    } else {
        NVR_LOGW("event", "ch%d 打开 EventExtInfo 失败(相机不支持?)", chn);
    }
}

static cJSON *extinfo_from_batch(cJSON *content, uint32_t start, uint32_t end)
{
    cJSON *list = content ? cJSON_GetObjectItem(content, "list") : NULL;
    if (!cJSON_IsArray(list)) return NULL;
    cJSON *best = NULL;
    uint32_t best_ts = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, list) {
        cJSON *st = cJSON_GetObjectItem(it, "startTime");
        uint32_t ts = cJSON_IsNumber(st) ? (uint32_t)st->valuedouble : 0;
        if (ts < start || (end && ts > end)) continue;
        if (!best || ts >= best_ts) { best = it; best_ts = ts; }
    }
    return best ? cJSON_Duplicate(best, 1) : NULL;
}

static void app_on_meta_pull(void *user, int chn, uint64_t eid, uint32_t start_ts)
{
    nvr_app_t *a = user;
    if (!a || !a->cm || !extinfo_on(a, chn)) return;
    nvr_channel_t ch;
    if (nvr_chan_get(a->cm, chn, &ch) != 0 || ch.backend != 0 || !ch.onvif_ip[0]) return;
    int dev_ch = ch.dev_chn > 0 ? ch.dev_chn : 1;
    char args[160];
    snprintf(args, sizeof(args), "{\"channel\":%d,\"startTime\":%u}", dev_ch, start_ts);
    uint32_t now = (uint32_t)time(NULL);

    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt) sleep(2);
        cJSON *content = nop_post_content(a->cm, chn, "AI_getEventExtInfo", args);
        if (content) {
            cJSON *err = cJSON_GetObjectItem(content, "error");
            if (cJSON_IsString(err) && err->valuestring) {
                if (strcmp(err->valuestring, "Not enabled") == 0)
                    app_on_meta_enable(a, chn);
                cJSON_Delete(content);
                content = NULL;
            } else {
                store_extinfo(a, chn, eid, start_ts, content);
                cJSON_Delete(content);
                return;
            }
        }
        char bargs[192];
        snprintf(bargs, sizeof(bargs),
                 "{\"channel\":%d,\"startTime\":%u,\"listNumber\":10}",
                 dev_ch, now ? now : start_ts);
        cJSON *batch = nop_post_content(a->cm, chn, "AI_getEventExtInfoBatchByReverseTime", bargs);
        if (batch) {
            cJSON *item = extinfo_from_batch(batch, start_ts, now);
            cJSON_Delete(batch);
            if (item) {
                store_extinfo(a, chn, eid, start_ts, item);
                cJSON_Delete(item);
                return;
            }
        }
    }
    NVR_LOGW("event", "ch%d EventExtInfo 未取到 eid=%llu start=%u", chn,
             (unsigned long long)eid, start_ts);
}

static void app_on_event_end(void *user, int chn, uint64_t eid, uint32_t start_epoch)
{
    nvr_app_t *a = user;
    if (!a || !a->eh || !eid) return;
    nvr_evt_queue_meta_pull(a->eh, chn, eid, start_epoch);
}

/* GET /eventSnap?eid= /snapshot/chN.jpg /download/*.mp4 — 隧道映射本机 nop 口取图/录像。
 * POST /APPJsonCmd 不走这里。 */
static int app_http_uri(void *ctx, int fd, const char *method, const char *uri)
{
    nvr_app_t *a = ctx;
    if (!method || strcasecmp(method, "GET") != 0) return 0;
    if (nvr_http_assets_serve(fd, uri)) return 1;
    if (!uri || strncmp(uri, "/eventSnap", 10) != 0) return 0;
    if (uri[10] && uri[10] != '?') return 0;
#if RSDK_CFG_METADATA
    uint64_t eid = 0;
    const char *q = strchr(uri, '?');
    if (q) {
        const char *e = strstr(q, "eid=");
        if (e) eid = strtoull(e + 4, NULL, 10);
    }
    if (!eid || !a || !a->meta || !a->group) {
        nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
        return 1;
    }
    rsdk_pic_ref_t r[1];
    int n = rsdk_pic_list_event(a->meta, eid, RSDK_PIC_MAIN, r, 1);
    if (n <= 0) n = rsdk_pic_list_event(a->meta, eid, -1, r, 1);
    if (n <= 0) {
        nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
        return 1;
    }
    rsdk_dev_t *d = rsdk_group_dev(a->group, (int)r[0].disk);
    void *jpeg = NULL; size_t jlen = 0;
    if (!d || rsdk_pic_read(d, a->meta, r[0].pic_id, &jpeg, &jlen) != RSDK_OK) {
        nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
        return 1;
    }
    nop_http_send_response(fd, 200, "OK", "image/jpeg", jpeg, jlen);
    free(jpeg);
#else
    (void)a;
    nop_http_send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
#endif
    return 1;
}

/* setSysDisplay 分辨率热切:切 HDMI 输出 + 按生效画布重排出图 + 回写生效值 + 重启 LVGL 进程
 * (killall <gui_proc>,看护脚本约 2~4s 内在新分辨率重拉 GUI)。 */
static void app_on_set_resolution(void *user, int w, int h)
{
    nvr_app_t *a = user;
    int ew = w, eh = h;
    nvr_preview_set_hdmi(a->pv, w, h, &ew, &eh);          /* 切模式+重排,取回实际生效(可能降级) */
    if (a->settings) {
        char eff[32]; snprintf(eff, sizeof(eff), "%dx%d", ew, eh);
        nvr_settings_set_str(a->settings, "display.resolution", eff);
    }
    /* 重启 LVGL:进程名默认 nightowl-lvgl,可经 settings(display.gui_proc)/env(NVR_GUI_PROC) 覆盖。 */
    char gui[64] = "nightowl-lvgl";
    if (a->settings) nvr_settings_get_str(a->settings, "display.gui_proc", gui, sizeof(gui), "nightowl-lvgl");
    { const char *e = getenv("NVR_GUI_PROC"); if (e && e[0]) snprintf(gui, sizeof(gui), "%s", e); }
    char cmd[128]; snprintf(cmd, sizeof(cmd), "killall %s 2>/dev/null", gui);
    printf("[app] 分辨率热切 %dx%d(生效 %dx%d),重启 LVGL(%s)\n", w, h, ew, eh, gui);
    int rc = system(cmd); (void)rc;
}

/* ② 未实现时的弱兜底：返回 -1，onvif_auto 通道保持待定 */
__attribute__((weak))
int nvr_onvif_get_url(const char *ip, int port, const char *user, const char *pass,
                      const char *stream, char *out, int out_size,
                      char *scopes_out, int scopes_cap, const char *vsrc_token)
{
    (void)ip; (void)port; (void)user; (void)pass; (void)stream; (void)out; (void)out_size;
    (void)vsrc_token;
    if (scopes_out && scopes_cap > 0) scopes_out[0] = 0;
    return -1;
}

__attribute__((weak))
int nvr_onvif_get_snapshot(const char *ip, int port, const char *user, const char *pass,
                           const char *vsrc_token, unsigned char **out, int *out_len)
{
    (void)ip; (void)port; (void)user; (void)pass; (void)vsrc_token;
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    return -1;
}

/* ---------- 模块间回调（app 编排，模块不横向 include） ---------- */
static void on_chan_online(void *user, int chn)
{
    nvr_app_t *a = user;
    nvr_rec_channel_up(a->rs, chn, NVR_CODEC_AUTO);
    nvr_preview_on_channel_online(a->pv, chn);
    /* 持久化通道状态到 channels.json(GUI 开机可先按上次已知状态绘制) */
    if (a->persist && a->cm) nvr_chan_persist_set_status(a->persist, chn + 1, nvr_chan_status_code_of(a->cm, chn));
    if (a->eh && a->cm && extinfo_on(a, chn)) {
        nvr_channel_t ch;
        if (nvr_chan_get(a->cm, chn, &ch) == 0 && ch.backend == 0)
            nvr_evt_queue_meta_enable(a->eh, chn);
    }
}
static void on_chan_offline(void *user, int chn)
{
    nvr_app_t *a = user;
    nvr_rec_channel_down(a->rs, chn);
    nvr_preview_on_channel_offline(a->pv, chn);
    if (a->persist && a->cm) nvr_chan_persist_set_status(a->persist, chn + 1, nvr_chan_status_code_of(a->cm, chn));
}
static void on_storage_evt(nvr_stg_evt_t e, const nvr_disk_t *d, void *user)
{
    nvr_app_t *a = user;
    const char *p = d ? d->path : "(group)";
    switch (e) {
        case NVR_STG_EVT_DISK_FAILED: printf("[storage] 盘故障: %s\n", p); break;
        case NVR_STG_EVT_DISK_REMOVED:printf("[storage] 盘掉线: %s\n", p); break;
        case NVR_STG_EVT_DISK_ADDED:  printf("[storage] 新盘插入, 需重扫\n"); break;
        case NVR_STG_EVT_FULL:        printf("[storage] 盘组满(策略生效)\n"); break;
        case NVR_STG_EVT_NEED_FORMAT: printf("[storage] 空盘/外来盘, 待格式化: %s\n", p); break;
        default: break;
    }
    if (a && a->rs) nvr_rec_on_storage_evt(a->rs, e);
}

static void apply_remote_access(nvr_app_t *a);   /* 前置声明（下方定义） */
static void rec_schedule_apply(nvr_app_t *app);
static void start_tutk(nvr_app_t *a);
static void stop_tutk(nvr_app_t *a);
static void restart_tutk(nvr_app_t *a);

/* 设置库变更 → 云存开关/stoken + 远程访问门控（cloud.* / nop_owner / service.remote_access） */
static void on_settings_change(void *user, const char *key)
{
    nvr_app_t *a = user;
    if (!a->settings) return;
    if (a->up && strncmp(key, "cloud.", 6) == 0)
        nvr_cloud_uploader_set_switch(a->up, nvr_settings_get_int(a->settings, "cloud.switch", 0));
    if (strncmp(key, "nop_owner", 9) == 0) {
        nvr_owner_row_t ow;
        if (a->up && nvr_settings_owner_get(a->settings, &ow) == 0)
            nvr_cloud_uploader_set_stoken(a->up, ow.stoken);
        apply_remote_access(a);   /* 绑定/解绑 NOP 账户 → 重算 BLE+P2P 门控 */
    }
    if (strncmp(key, "service.remote_access", 21) == 0)
        apply_remote_access(a);   /* 本地 admin 运行时开关 */
    if (strncmp(key, "tutk.authkey", 12) == 0 || strncmp(key, "tutk.uid", 8) == 0) {
        if (a->tutk_on)                       /* agent 重启以重读 UID/authkey(cgi getIotcAuthKey) */
            restart_tutk(a);
        else if (remote_access_effective(a))
            start_tutk(a);
    }
    if (strncmp(key, "record_config.", 14) == 0 ||
        strncmp(key, "record_schedule.", 16) == 0 ||
        strncmp(key, "schedule.", 9) == 0)
        rec_schedule_apply(a);   /* 保存后立刻按库评估，不等 5s tick */
}

/* 有盘 + meta + udid 就绪 → 启动云存上传器，初值取自设置库 */
static void maybe_start_uploader(nvr_app_t *a, const char *config_dir)
{
    (void)config_dir;
    if (!a->group || !a->meta) return;
    char udid[64]; nvr_identity_get_uid(udid, sizeof(udid));  /* UID ← /User/tutk_agent_udid(统一来源) */
    if (!udid[0]) { printf("[app] 云存: 未配置 UID, 上传器未启动\n"); return; }

    nvr_owner_row_t ow; memset(&ow, 0, sizeof(ow));
    if (a->settings) nvr_settings_owner_get(a->settings, &ow);

    nvr_cloud_uploader_cfg_t uc = {
        .group = a->group, .meta = a->meta, .udid = udid, .stoken = ow.stoken,
        .stage = nvr_settings_get_int(a->settings, "cloud.stage", 0),
        .worker_count = 2, .poll_interval_s = 5, .slice_ms = 15000,
    };
    if (nvr_cloud_uploader_start(&uc, &a->up) == 0) {
        nvr_cloud_uploader_set_switch(a->up, nvr_settings_get_int(a->settings, "cloud.switch", 0));
        nvr_settings_subscribe(a->settings, "cloud.", on_settings_change, a);
        /* nop_owner 由 nvr_app_start 统一订阅（同时驱动 stoken 与远程访问门控） */
        printf("[app] 云存上传器已启动(UID=%s)\n", udid);
    }
}

static pv_layout_t pv_layout_of(int n)
{
    return (n <= 1) ? PV_L1 : (n <= 4) ? PV_L4 : (n <= 6) ? PV_L6 :
           (n <= 9) ? PV_L9 : (n <= 12) ? PV_L12 : PV_L16;
}

/* ---- 远程访问(BLE+TUTK) 账户门控 ---- */
/* 有效状态：绑 NOP 账户→常开；出厂(无本地账户)→常开；仅本地 admin→运行时开关(service.remote_access)。*/
static int remote_access_effective(nvr_app_t *a)
{
    nvr_owner_row_t ow;
    if (nvr_settings_owner_get(a->settings, &ow) == 0 && ow.owner_id[0]) return 1;  /* NOP 账户 */
    /* 有本地用户 → 运行时开关；出厂无用户 → 常开 */
    if (nvr_settings_user_count(a->settings, NULL) > 0)
        return nvr_settings_get_int(a->settings, "service.remote_access", 0);
    nvr_auth_row_t au;
    if (nvr_settings_auth_get(a->settings, &au) < 0) return 1;
    return nvr_settings_get_int(a->settings, "service.remote_access", 0);
}

/* BLE 命令桥：组包完成的一整条 NOP JSON → 复用 8089 命令路由 → 应答(malloc)。
 * enc 由路由/envelope 层按 HTTP 同样方式处理，这里透传原文。 */
static char *ble_dispatch_bridge(void *ud, const char *json, int enc)
{
    (void)enc;
    nvr_app_t *a = (nvr_app_t *)ud;
    if (!a || !a->router) return NULL;
    return nvr_cmd_dispatch(a->router, json);
}

/* P2P 采用 ODC 的 TUTK 代理程序(AVAPIs_Server_CLI,含 TUTK 真 license),按官方文档
 * (APP_client_Agent.md)集成:agent 收 APP 命令→cgi→POST 127.0.0.1:6061→本机 nvr_cmd_dispatch;
 * 媒体走 tunnel 内 RTSP。拉起对照相机样例:
 *   device.sh <UID> <cgi> <profile>（同一进程 --cgipath/--profilepath/--start）。 */
#define TUTK_AGENT_DIR "/dvr/tutk_cloud_agent"
#define TUTK_PROFILE_OUT "/tmp/tutk_profile.txt"   /* 由 config 生成(rootfs 只读),device.sh 用它 */

/* 读 rootfs 内 profile.txt 模板,按 config 覆盖 fwVer/type/model/serialNumber,
 * 写到可写路径供 agent 使用。失败(无模板)则返回 -1(device.sh 回退用模板)。 */
static int write_tutk_profile(nvr_app_t *a)
{
    FILE *fp = fopen(TUTK_AGENT_DIR "/profile.txt", "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (n <= 0 || n > 65536) { fclose(fp); return -1; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return -1; }
    size_t rd = fread(buf, 1, (size_t)n, fp); fclose(fp); buf[rd] = '\0';
    cJSON *j = cJSON_Parse(buf); free(buf);
    if (!j) return -1;

    char fw[32], type[32], model[32], sn[64] = "";
    nvr_settings_get_str(a->settings, "system.fw_version",  fw,   sizeof(fw),   NVR_DEF_FW_VERSION);
    nvr_settings_get_str(a->settings, "tutk.device_type",   type, sizeof(type), NVR_DEF_TUTK_DEV_TYPE);
    nvr_identity_get_model(model, sizeof(model));              /* MODEL ← /User/OWLModel */
    nvr_identity_get_sn(sn, sizeof(sn));                       /* SN ← /User/OWLSerialNumber */

    cJSON_DeleteItemFromObject(j, "fwVer");        cJSON_AddStringToObject(j, "fwVer", fw);
    cJSON_DeleteItemFromObject(j, "type");         cJSON_AddStringToObject(j, "type", type);
    cJSON_DeleteItemFromObject(j, "model");        cJSON_AddStringToObject(j, "model", model);
    if (sn[0]) { cJSON_DeleteItemFromObject(j, "serialNumber"); cJSON_AddStringToObject(j, "serialNumber", sn); }

    /* 对讲/命令/缩略图/直播:App 映射 iotc-tunnel:PORT → 本机 127.0.0.1:PORT */
    {
        static const char *need[] = {
            "iotc-tunnel:8554", "iotc-tunnel:6061",
            "iotc-tunnel:7000", "iotc-tunnel:8089"
        };
        cJSON *pp = cJSON_GetObjectItem(j, "p2pProtocols");
        if (!cJSON_IsArray(pp)) {
            cJSON_DeleteItemFromObject(j, "p2pProtocols");
            pp = cJSON_AddArrayToObject(j, "p2pProtocols");
        }
        if (cJSON_IsArray(pp)) {
            for (int k = 0; k < 4; k++) {
                int has = 0;
                cJSON *it;
                cJSON_ArrayForEach(it, pp) {
                    if (cJSON_IsString(it) && it->valuestring &&
                        strcmp(it->valuestring, need[k]) == 0)
                        has = 1;
                }
                if (!has) cJSON_AddItemToArray(pp, cJSON_CreateString(need[k]));
            }
        }
    }

    char *out = cJSON_Print(j);
    cJSON_Delete(j);
    if (!out) return -1;
    FILE *wf = fopen(TUTK_PROFILE_OUT, "wb");
    if (!wf) { free(out); return -1; }
    fputs(out, wf); fclose(wf); free(out);
    printf("[app] TUTK profile 生成: fwVer=%s type=%s model=%s sn=%s\n", fw, type, model, sn);
    return 0;
}
static void start_tutk(nvr_app_t *a)
{
    /* 自愈:以真实进程为准(flag 可能与实际失步),已在跑则只标记返回。 */
    if (system("ps 2>/dev/null | grep -v grep | grep -q AVAPIs_Server_CLI") == 0) {
        a->tutk_on = 1; return;
    }
    char uid[64] = "";
    nvr_identity_get_uid(uid, sizeof(uid));      /* UID ← /User/tutk_agent_udid;返回长度(非0=成功) */
    if (!uid[0])                                  /* 数据分区无 UID → settings 兜底 */
        nvr_settings_get_str(a->settings, "tutk.uid", uid, sizeof(uid), "");
    if (!uid[0]) { printf("[app] TUTK: 无 UID(/User/tutk_agent_udid 空),agent 未启动\n"); a->tutk_on = 0; return; }

    /* squashfs 可能无 +x;运行期补执行位 */
    (void)system("chmod +x " TUTK_AGENT_DIR "/device.sh " TUTK_AGENT_DIR "/nvr_tutk_cgi "
                 TUTK_AGENT_DIR "/AVAPIs_Server_CLI 2>/dev/null");
    if (access(TUTK_AGENT_DIR "/device.sh", R_OK) != 0) {
        printf("[app] TUTK: 未找到 %s/device.sh,agent 未启动\n", TUTK_AGENT_DIR); a->tutk_on = 0; return;
    }
    /* cgi 用 http://iotc-tunnel:6061 走命令 → 确保 iotc-tunnel 解析到本机
     * (/etc/hosts 开机由网络 init 重生成,不含此项,故运行期补) */
    int hrc = system("grep -q iotc-tunnel /etc/hosts || echo '127.0.0.1 iotc-tunnel' >> /etc/hosts");
    (void)hrc;
    write_tutk_profile(a);   /* config → /tmp/tutk_profile.txt(fwVer/type/model/serialNumber) */
    /* APP_client_Agent.md 相机样例: device.sh <UID> <CGI_PATH> <PROFILE_PATH> */
    const char *prof = TUTK_PROFILE_OUT;
    if (access(TUTK_PROFILE_OUT, R_OK) != 0)
        prof = TUTK_AGENT_DIR "/profile.txt";
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "sh %s/device.sh '%s' '%s/nvr_tutk_cgi' '%s' >/tmp/tutk_agent.log 2>&1 &",
             TUTK_AGENT_DIR, uid, TUTK_AGENT_DIR, prof);
    int rc = system(cmd); (void)rc;
    a->tutk_on = 1;
    printf("[app] ODC TUTK agent 已启动(UID=%s cgi=%s/nvr_tutk_cgi profile=%s)\n",
           uid, TUTK_AGENT_DIR, prof);
}
static void stop_tutk(nvr_app_t *a)
{
    if (!a->tutk_on) return;
    int rc = system("killall device.sh AVAPIs_Server_CLI 2>/dev/null"); (void)rc;
    a->tutk_on = 0;
    printf("[app] ODC TUTK agent 已停止\n");
}

static void restart_tutk(nvr_app_t *a)
{
    stop_tutk(a);
    start_tutk(a);
}

static void start_ble(nvr_app_t *a)
{
    if (a->ble) return;
    nvr_ble_cfg_t bc; memset(&bc, 0, sizeof(bc));
    bc.dispatch = ble_dispatch_bridge; bc.dispatch_ud = a; bc.mtu = 40;
    nvr_settings_get_str(a->settings, "system.model", bc.model,  sizeof(bc.model),  a->cfg.sys.model);
    nvr_identity_get_mac("eth0", bc.mac,    sizeof(bc.mac));      /* MAC ← /User/mac_addr_v2(恒定) */
    nvr_identity_get_sn(bc.serial, sizeof(bc.serial));            /* SN  ← /User/OWLSerialNumber(恒定) */
    /* 连接锁定加密密钥：有 ble.key 用 BLEKey，否则 MAC_SN 前 16（见 BLE 通讯文档） */
    {
        char blekey[20];
        nvr_settings_get_str(a->settings, "ble.key", blekey, sizeof(blekey), "");
        nvr_ble_resolve_crypt_key(blekey, bc.mac, bc.serial,
                                  bc.crypt_key, (int)sizeof(bc.crypt_key));
    }
    if (nvr_ble_create(&bc, &a->ble) == 0) {
        nvr_owner_row_t ow; int bound = (nvr_settings_owner_get(a->settings, &ow) == 0 && ow.owner_id[0]);
        nvr_ble_update_adv(a->ble, bound, 0 /*启动即锁定*/);
        /* TODO(板级): 注册 BlueZ GATT Service 0xFFF0 / RX 0xFFF1(write→nvr_ble_on_rx) /
         *   TX 0xFFF2(notify)；把 link.ud/notify/set_adv 指到板级 BLE 栈。
         *   每次 GATT 新连接应重新 resolve crypt_key（setOwner 后的 BLEKey 下连生效）。 */
    }
}
static void stop_ble(nvr_app_t *a)
{
    if (!a->ble) return;
    nvr_ble_destroy(a->ble); a->ble = NULL;
    printf("[app] BLE 通路已停止\n");
}

/* 依据账户门控启停 BLE+TUTK（幂等）。设置库变更或绑定态变化时调用。 */
static void apply_remote_access(nvr_app_t *a)
{
    int eff = remote_access_effective(a);
    /* 门控:无 user → 常开(局域网配对);NOP 账户绑定 → 开(云);本地 Admin → 由接口
     * (service.remote_access)控制。avapi(P2P 代理)与 BLE 同受此门控启停。 */
    if (eff) { start_tutk(a); start_ble(a); }
    else     { stop_tutk(a);  stop_ble(a);  }
    printf("[app] 远程访问(BLE+P2P avapi) = %s\n", eff ? "开" : "关");
}

/* 读 LVGL 的 GUI_CONFIG.json:启动宫格(displayMode/displayPage)+ 通道数(channels=[PoE,LAN])。
 * 路径优先级:$NVR_GUI_CONFIG → /mnt/custom/GUI_CONFIG.json → <config_dir>/GUI_CONFIG.json。
 * 读不到给安全默认(9宫格/page1, 16 PoE + 16 LAN)。 */
static void read_gui_config(const char *config_dir, int *mode, int *page, int *poe_n, int *lan_n)
{
    nvr_gui_config_init(config_dir);              /* 解析并记住路径(供 set/get + LAN 添加容量共用) */
    nvr_gui_config_get_display(mode, page);       /* 读不到给默认 9/1 */
    nvr_gui_config_get_channels(poe_n, lan_n);    /* 读不到给默认 16/16 */
    printf("[app] GUI_CONFIG.json: displayMode=%d page=%d channels=[%d PoE,%d LAN]\n",
           *mode, *page, *poe_n, *lan_n);
}

int nvr_app_start(const char *config_dir, nvr_app_t **out)
{
    if (!config_dir || !out) return -1;
    nvr_app_t *a = calloc(1, sizeof(*a));
    if (!a) return -1;

    /* 1) 只读 JSON 配置 */
    if (nvr_config_load(config_dir, &a->cfg) != 0) {
        printf("[app] 配置加载失败(%s)\n", config_dir); free(a); return -1;
    }

    /* 2) 运行期设置库（首启从 JSON 播种）+ overlay 覆盖
     *
     * ★ 持久化:两个 .db 落**持久分区** /flash/nvrcfg(ubifs),重启不丢;OTA 镜像**跳过**
     *   flash/sys/user 分区(见 FW98633A_ota.ini,ITEM08/09/10=0),故 OTA 也不丢。
     *   config_dir(/tmp/nvrcfg,tmpfs)仅作只读 JSON 默认源(首启一次性播种)。
     *   seeding 由 DB 内 'seeded' 标志护住(见 nvr_settings.c),重启/OTA 不会用 JSON 覆盖已存值。
     *   /flash 不可写时回落 config_dir(至少不崩,退化为不持久)。 */
    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "%s", "/flash/nvrcfg");
    mkdir(data_dir, 0755);                      /* 幂等;父 /flash 已挂载 */
    if (access(data_dir, W_OK) != 0) {
        printf("[app] 警告: 持久目录 %s 不可写, 回落 %s(不持久)\n", data_dir, config_dir);
        snprintf(data_dir, sizeof(data_dir), "%s", config_dir);
    }
    char dbpath[512];
    snprintf(dbpath, sizeof(dbpath), "%s/nvr_settings.db", data_dir);
    if (nvr_settings_open(dbpath, config_dir, &a->settings) == 0)
        nvr_config_overlay_from_settings(&a->cfg, a->settings);
    else
        printf("[app] 警告: 设置库打开失败(%s), 用只读 JSON\n", dbpath);

    if (a->settings) {
        char kx[64], ky[64], ak[80], iv[48];
        nvr_settings_get_str(a->settings, "factory.keyx", kx, sizeof(kx), "");
        nvr_settings_get_str(a->settings, "factory.keyy", ky, sizeof(ky), "");
        nvr_settings_get_str(a->settings, "factory.act_aes_key", ak, sizeof(ak), "");
        nvr_settings_get_str(a->settings, "factory.act_aes_iv", iv, sizeof(iv), "");
        nvr_pw_set_keys(kx[0] ? kx : NULL, ky[0] ? ky : NULL,
                        ak[0] ? ak : NULL, iv[0] ? iv : NULL);
        printf("[app] 口令算法: 增强=%s 激活AES=%s\n",
               nvr_pw_enh_ready() ? "就绪" : "缺 keyx/keyy",
               nvr_pw_act_ready() ? "就绪" : "缺 AES key");
    }

    /* 身份 provisioning 兜底:tutkdata.json 缺则建缺省;打印 SN/MAC/UID 状态。
     * 身份权威源是数据分区 /User(见 nvr_identity),独立于设置库。 */
    nvr_identity_ensure_provisioned();

    /* MODEL 权威源 = /User/OWLModel(identity):播种进设置库并覆盖运行期 cfg,
     * 使 getDeviceInfo / TUTK profile / 广播 / DHCP 主机名(Option 12)同源。 */
    {
        char mdl[64];
        nvr_identity_get_model(mdl, sizeof(mdl));
        if (mdl[0]) {
            if (a->settings) nvr_settings_set_str(a->settings, "system.model", mdl);
            snprintf(a->cfg.sys.model, sizeof(a->cfg.sys.model), "%s", mdl);
        }
    }

    printf("[app] %s: %d 通道(cap=%d, PoE=%d, IP=%d)\n", a->cfg.sys.model, a->cfg.nch,
           a->cfg.sys.capacity, a->cfg.sys.poe_ports, a->cfg.sys.ip_channels);

    /* 2.5) 网络与时间落地：eth0 DHCP(默认)/静态、eth1 PoE VLAN+DHCP 服务、时区+NTP(UTC)
     *      —— 必须在发现/取流之前，保证 eth1 各 PoE 网段可达。 */
    if (a->settings) {
        nvr_net_apply(a->settings);
        nvr_time_apply(a->settings);
    }

    /* 2.6) UDP 34569 本机应答：App 忘管理 IP 时按 MAC/SN 搜到本机。IP 每次从 eth0 现取。 */
    {
        nvr_lan34569_self_t self;
        memset(&self, 0, sizeof(self));
        nvr_identity_get_mac("eth0", self.mac, sizeof(self.mac));
        nvr_identity_get_sn(self.serial, sizeof(self.serial));
        if (a->settings) {
            nvr_settings_get_str(a->settings, "system.model", self.model, sizeof(self.model),
                                 a->cfg.sys.model[0] ? a->cfg.sys.model : NVR_DEF_MODEL);
            nvr_settings_get_str(a->settings, "system.device_name", self.name, sizeof(self.name),
                                 NVR_DEF_NAME);
        } else {
            snprintf(self.model, sizeof(self.model), "%s",
                     a->cfg.sys.model[0] ? a->cfg.sys.model : NVR_DEF_MODEL);
            snprintf(self.name, sizeof(self.name), "%s", NVR_DEF_NAME);
        }
        self.http_port = NVR_DEF_NOP_PORT;
        if (nvr_lan34569_server_start(&self) != 0)
            printf("[app] 警告: UDP 34569 本机应答未启动\n");
        else
            printf("[app] UDP 34569 本机应答已听(mac=%s sn=%s)\n",
                   self.mac[0] ? self.mac : "-", self.serial[0] ? self.serial : "-");
    }

    /* 3) 存储：init → scan → assemble 盘组 */
    a->cfg.storage.cb = on_storage_evt;
    a->cfg.storage.cb_user = a;
    if (nvr_storage_init(&a->cfg.storage, &a->stg) != RSDK_OK) { printf("[app] storage init 失败\n"); goto fail; }
    printf("[app] 发现 %d 块盘\n", nvr_storage_scan(a->stg));
    if (nvr_storage_assemble(a->stg, &a->group) != RSDK_OK) {
        printf("[app] 警告: 盘组装配失败, 录像禁用, 仅预览\n"); a->group = NULL;
    }

    /* 4) 元数据库（云存状态/事件/抓拍）：<config_dir>/meta.db */
#if RSDK_CFG_METADATA
    {
        char metapath[512];
        snprintf(metapath, sizeof(metapath), "%s/meta.db", data_dir);   /* 持久:同 settings 库 */
        if (rsdk_meta_open(metapath, &a->meta) != RSDK_OK) {
            printf("[app] 警告: meta 打开失败(%s), 云存状态禁用\n", metapath); a->meta = NULL;
        }
    }
#endif

    /* 5) 平台显示:分辨率取**记录值**(display.resolution),无则回退 config→默认;
     *    mhal_vout_init 按此请求,屏幕不支持则沿阶梯降级到可用者;取回**实际生效**分辨率,
     *    回写 settings(供 getSysDisplay 报当前) 并用于 preview 布局(避免窗口越界)。 */
    int disp_w = a->cfg.sys.hdmi_w > 0 ? a->cfg.sys.hdmi_w : NVR_DISPLAY_DEFAULT_W;
    int disp_h = a->cfg.sys.hdmi_h > 0 ? a->cfg.sys.hdmi_h : NVR_DISPLAY_DEFAULT_H;
    if (a->settings) {
        char res[32];
        if (nvr_settings_get_str(a->settings, "display.resolution", res, sizeof(res), "") > 0 && res[0]) {
            int w = 0, h = 0;
            if (sscanf(res, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { disp_w = w; disp_h = h; }
        }
    }
    if (mhal_vout_init(MHAL_OUT_HDMI, disp_w, disp_h) != 0)
        printf("[app] 警告: 显示初始化失败(非目标机?)\n");
    else {
        int ew = disp_w, eh = disp_h;
        mhal_vout_get_resolution(&ew, &eh);          /* 实际生效(可能已降级) */
        if (ew > 0 && eh > 0) { disp_w = ew; disp_h = eh; }
        if (a->settings) {
            char eff[32]; snprintf(eff, sizeof(eff), "%dx%d", disp_w, disp_h);
            nvr_settings_set_str(a->settings, "display.resolution", eff);
        }
        printf("[app] HDMI 输出生效分辨率 %dx%d\n", disp_w, disp_h);
    }

    /* 6) 拉流管理器 */
    a->cfg.stream.group = a->group;
    if (nvr_stream_mgr_init(&a->cfg.stream, &a->sm) != RSDK_OK) { printf("[app] streaming init 失败\n"); goto fail; }

    /* 7) 事件脊柱 + NOP 服务端(8089，界面交互) + 录像调度 + 预览 + 事件中枢 */
    a->nop_hub = nop_event_hub_create();
    {
        nop_app_config_t nc; memset(&nc, 0, sizeof(nc)); nc.role = NOP_ROLE_NVR;
        a->nop = nop_app_create(&nc);
        if (a->nop) nop_app_set_event_hub(a->nop, a->nop_hub);
        /* 8089 命令路由在通道管理器就绪后启动（见 §8 后）——它需要 cm 做 channel→设备 解析 */
    }

    nvr_rec_sched_cfg_t rc = { .group = a->group, .meta = a->meta,
                               .hdd_full_policy = a->cfg.storage.hdd_full, .post_record_s = 10,
                               .end_user = a, .on_event_end = app_on_event_end };
    nvr_rec_sched_init(&rc, &a->rs);

    nvr_preview_cfg_t pc = { .sm = a->sm, .hdmi_w = disp_w, .hdmi_h = disp_h };
    nvr_preview_init(&pc, &a->pv);
    nvr_preview_set_layout(a->pv, pv_layout_of(a->cfg.sys.default_layout));

    /* 通道映射/名称/状态持久化:channels.json 落**持久分区** data_dir(/flash/nvrcfg),重启不丢
     * (与两 .db 一致;此前落 config_dir=/tmp/nvrcfg 即 RAM,重启丢失通道名/映射)。 */
    a->persist = nvr_chan_persist_open(data_dir);
    if (a->persist) {
        int map[NVR_PERSIST_MAX_CH];
        int n = nvr_chan_persist_get_mapping(a->persist, map, NVR_PERSIST_MAX_CH);
        if (n > 0) nvr_preview_set_mapping(a->pv, map, n);
    }

    nvr_evt_cfg_t ec = { .nop_hub = a->nop_hub, .rs = a->rs, .sm = a->sm, .settings = a->settings };
    nvr_evt_init(&ec, &a->eh);

    /* 8) 通道管理器：载入配置 → 起流（回调驱动 rec/preview） */
    nvr_chan_mgr_cfg_t cc = { .sm = a->sm, .settings = a->settings, .nop = a->nop,
                              .reconnect_base_s = 5, .reconnect_max_s = 30,
                              .user = a, .on_online = on_chan_online, .on_offline = on_chan_offline };
    nvr_chan_mgr_init(&cc, &a->cm);
    nvr_preview_set_cm(a->pv, a->cm);     /* 迟绑：preview 按显示模式切通道主/子码流 */
    nvr_stream_set_lp_poke(a->sm, app_poke_longpoll, a);
    nvr_evt_set_longpoll_poke(a->eh, app_poke_longpoll, a);
    nvr_evt_set_snap(a->eh, app_on_snap, a);
    nvr_evt_set_meta(a->eh, app_on_meta_enable, app_on_meta_pull, a);
    {
        nvr_push_opt_t po = { .settings = a->settings, .persist = a->persist,
                              .group = a->group, .meta = a->meta };
        if (nvr_push_start(&po, &a->push) == 0 && a->eh)
            nvr_evt_set_push(a->eh, app_on_push, a->push);
        else {
            a->push = NULL;
            printf("[app] 警告: 推送引擎启动失败\n");
        }
    }
    /* NVR_MANUAL_ONLY：只连手动添加(LanAddDevice/setLanDevice)的相机，
     * 不加载配置通道、不自动发现绑定。用于受控测试/护 IPC(不拿错凭据轰别的相机)。 */
    a->manual_only = (getenv("NVR_MANUAL_ONLY") != NULL);
    if (!a->manual_only) nvr_chan_load_config(a->cm, &a->cfg);
    nvr_chan_start_all(a->cm);

    /* ★ 启动宫格由 LVGL 的 GUI_CONFIG.json 决定(displayMode/displayPage)。必须在通道加载后调,
     * set_mode 走解码门控(nvr_stream_set_display),只对可见格通道开解码——通道 slot 先存在才生效。
     * channels=[PoE,LAN] 亦读入(供 16 PoE + 16 LAN 通道布局;完整 32 通道模型见后续)。 */
    {
        int gmode, gpage, poe_n, lan_n;
        read_gui_config(config_dir, &gmode, &gpage, &poe_n, &lan_n);
        nvr_preview_set_mode(a->pv, gmode, gpage);
    }

    /* 8a) 连接策略（用户定）：
     *   · PoE 口：配置已把 16 口(198.18.<口>.1)登记为 onvif_auto 通道；tick 后台对每口做
     *     ONVIF 广播,扫到相机就取流出图(=“PoE 扫到就连”),没相机的口限次退避后停。
     *   · LAN(eth0)：**只连已添加的**(channels.json 显式 IP 相机 / LanAddDevice)，
     *     **不在 eth0 上做广播自动绑定**——否则会把局域网上别人的相机全绑进来并反复骚扰。
     * 故此处不再跑 eth0 全网段自动发现;先监听(下面 8089),通道解析全走 tick 后台限速。 */
    if (a->manual_only) printf("[app] MANUAL_ONLY：仅连手动添加的相机\n");
    else                printf("[app] 通道解析走后台(先监听 8089;PoE 自动扫连, LAN 仅连已添加)\n");

    /* 8b) ONVIF 映射后端：把每通道 host/凭据/backend 注册进 nop_nvr_channels，
     *     建映射后端并挂到 nop_app；通用 ONVIF 通道的控制命令经此翻译成 ONVIF SOAP 发相机。
     *     并启动 ONVIF 事件轮询 → nop_event_hub（AI 事件进事件中枢）。 */
    if (a->nop) {
        a->nop_chans = nop_nvr_channels_create(NVR_MAX_CH);
        nop_app_set_nvr_channels(a->nop, a->nop_chans);
        nvr_chan_mgr_set_nop_registry(a->cm, a->nop_chans);
        a->onvif_backend = nop_onvif_map_backend_create(a->nop_chans);
        nvr_chan_mgr_set_onvif_backend(a->cm, a->onvif_backend);
        nvr_chan_nop_sync_all(a->nop_chans, a->cm);
        if (a->onvif_backend) {
            nop_app_set_onvif_backend(a->nop, a->onvif_backend);
            /* ★ NVR 无本地 HAL_PTZ,auto_light_capabilities 不会点亮 CAP_PTZ → ptz* 命令在 router
             * cap 门(nop_router.c dispatch_one)被 501,永远到不了 cap_ptz→ONVIF 映射。PTZ 在 NVR 上
             * 是"经 ONVIF 映射代理到相机",不依赖本地 HAL,故 ONVIF 后端就绪即显式开 CAP_PTZ。 */
            nop_app_set_capability(a->nop, CAP_PTZ, 1);
            nop_onvif_map_events_start(a->onvif_backend, a->nop_hub);  /* ONVIF 事件→事件中枢 */
            printf("[app] ONVIF 映射后端就绪(%d 通道注册, 事件轮询启动)\n",
                   nop_nvr_channels_count(a->nop_chans));
        }
    }

    /* 8a1.5) NOP 设备事件:连各 NOP 相机的 8012 事件中心,收到告警(+JPEG 快照)归一化
     *        publish 到同一 nop_hub → nvr_evt 订阅 → longPolling/录像触发(与 ONVIF 同桥)。 */
    {
        nvr_nop8012_cfg_t n8012 = { .cm = a->cm, .settings = a->settings, .hub = a->nop_hub, .port = 8012 };
        if (nvr_nop8012_start(&n8012, &a->n8012) != 0) { a->n8012 = NULL; printf("[app] 警告: 8012 事件客户端启动失败\n"); }
        else printf("[app] NOP 8012 事件中心客户端就绪(逐 NOP 相机连接)\n");
    }

    /* 8a2) 本机回放引擎(rsdk_play → mhal_vdec 上屏);盘组/流管/预览就绪后创建 */
    {
        nvr_playback_cfg_t pbc = { .group = a->group, .sm = a->sm, .pv = a->pv,
                                   .hdmi_w = a->cfg.sys.hdmi_w, .hdmi_h = a->cfg.sys.hdmi_h };
        if (nvr_playback_create(&pbc, &a->pb) != 0) { a->pb = NULL; printf("[app] 警告: 回放引擎创建失败\n"); }
    }

    /* 8a3) 双向对讲：仅 127.0.0.1:7000，外网由 TUTK agent 映射 */
    if (nvr_talk_init(NVR_TALK_PORT, &a->talk) != 0) {
        a->talk = NULL;
        printf("[app] 警告: 对讲 127.0.0.1:%d 监听失败\n", NVR_TALK_PORT);
    } else if (a->talk)
        nvr_talk_set_enh_cb(a->talk, app_talk_enh, a);

    /* 8b) 8089 命令路由（本地存/查设置库 + channel→真实设备 转发/翻译；回落 nop_app） */
    {
        nvr_cmd_router_cfg_t rc = { .settings = a->settings, .cm = a->cm,
                                    .stg = a->stg, .sm = a->sm, .group = a->group, .meta = a->meta, .nop = a->nop,
                                    .port = nvr_settings_get_int(a->settings, "system.nop_port", NVR_DEF_NOP_PORT),
                                    .dev_nop_port = 8089,
                                    .pv = a->pv, .pb = a->pb, .eh = a->eh, .persist = a->persist,
                                    .talk = a->talk,
                                    .disp_user = a, .on_set_resolution = app_on_set_resolution };
        if (nvr_cmd_router_start(&rc, &a->router) != 0)
            printf("[app] 警告: 命令路由处理器 初始化失败\n");
    }

    /* 8c) 唯一 8089 入口：nop_http_server(inbound)，请求处理器=nvr_cmd_dispatch
     *     —— 收到 JSON 后内部完成 出图/通道转发/回落 nop_app_dispatch，不再单独监听。 */
    if (a->router) {
        int nop_port = nvr_settings_get_int(a->settings, "system.nop_port", NVR_DEF_NOP_PORT);
        printf("[app] 8c 前: a->nop=%p nop_port=%d\n", (void *)a->nop, nop_port); fflush(stdout);
        a->nop_http = nop_http_server_start(nop_port, a->nop);
        printf("[app] 8c 后: nop_http=%p → 8089 入口 %s\n",
               (void *)a->nop_http, a->nop_http ? "就绪" : "启动失败"); fflush(stdout);
        if (a->nop_http) {
            nop_http_server_set_handler(a->nop_http, app_http_handler, a->router);
            nop_http_server_set_uri_handler(a->nop_http, app_http_uri, a);
        }

        /* 8d) ODC TUTK agent 命令端点：agent 的 cgi 把 APP 命令 POST 到 iotc-tunnel:6061
         *     ({"func","args"} 同 nop 协议) → 复用同一 nvr_cmd_dispatch。
         *     (iotc-tunnel 在 /etc/hosts 指向 127.0.0.1;媒体 :80 属阶段2) */
        int agent_port = nvr_settings_get_int(a->settings, "tutk.agent_cmd_port", 6061);
        a->nop_http_agent = nop_http_server_start(agent_port, a->nop);
        if (a->nop_http_agent)
            nop_http_server_set_handler(a->nop_http_agent, app_http_handler, a->router);
        printf("[app] 8d: TUTK agent 命令端点 :%d %s\n",
               agent_port, a->nop_http_agent ? "就绪" : "启动失败"); fflush(stdout);

        /* 8e) tunnel 内 RTSP(:8554)直播+远程回放;streaming 旁路主/子/音频,
         *     URL host=iotc-tunnel,App 经 P2PTunnel 映射。 */
        int rtsp_port = nvr_settings_get_int(a->settings, "tutk.tunnel_rtsp_port", 8554);
        int rrc = nvr_rtsp_live_start(rtsp_port);
        nvr_rtsp_live_set_group(a->group);
        printf("[app] 8e: tunnel RTSP :%d %s (live+playback)\n",
               rtsp_port, rrc == 0 ? "就绪" : "启动失败");
        fflush(stdout);
    }

    /* 9) 云存上传器（有盘+meta+UID 时） */
    maybe_start_uploader(a, config_dir);

    /* 10) 远程访问(BLE 配网 + TUTK P2P) —— **账户门控**，不是无条件常开：
     *      · 出厂(本地账户系统为空) → 常开，供 APP/向导 BLE 发现绑定
     *      · 已绑 NOP 账户(owner) → 常开
     *      · 仅本地 admin(无 NOP 账户) → 默认关，经 GUI_setRemoteAccessState 运行时开关控制
     *      运行时开关断电丢失：本地 admin 态启动即复位为关。 */
    {
        nvr_owner_row_t ow2;
        int bound = (nvr_settings_owner_get(a->settings, &ow2) == 0 && ow2.owner_id[0]);
        int has_local = (nvr_settings_user_count(a->settings, NULL) > 0);
        if (!has_local) {
            nvr_auth_row_t au2;
            has_local = (nvr_settings_auth_get(a->settings, &au2) >= 0);
        }
        if (!bound && has_local)
            nvr_settings_set_int(a->settings, "service.remote_access", 0);  /* 复位运行时开关 */
    }
    nvr_settings_subscribe(a->settings, "nop_owner",             on_settings_change, a);
    nvr_settings_subscribe(a->settings, "service.remote_access", on_settings_change, a);
    nvr_settings_subscribe(a->settings, "tutk.authkey", on_settings_change, a);
    nvr_settings_subscribe(a->settings, "tutk.uid", on_settings_change, a);
    nvr_settings_subscribe(a->settings, "record_config.", on_settings_change, a);
    nvr_settings_subscribe(a->settings, "record_schedule.", on_settings_change, a);
    nvr_settings_subscribe(a->settings, "schedule.", on_settings_change, a);
    apply_remote_access(a);

    a->running = 1;
    *out = a;
    printf("[app] 启动完成\n");
    return 0;

fail:
    nvr_app_stop(a);
    return -1;
}

/* "HHMMSS" → 当日秒数;非法返回 -1。 */
static int hms_to_sod(const char *hms)
{
    if (!hms) return -1; size_t n = strlen(hms); if (n < 6) return -1;
    for (int i = 0; i < 6; i++) if (hms[i] < '0' || hms[i] > '9') return -1;
    int h = (hms[0]-'0')*10+(hms[1]-'0'), m = (hms[2]-'0')*10+(hms[3]-'0'), s = (hms[4]-'0')*10+(hms[5]-'0');
    return h*3600 + m*60 + s;
}
/* 当前(周几 1-7、当日秒 sod)是否落在排程 rules 任一区间内。rules 空=默认 7×24 全录。 */
static int rules_match_now(const char *rules_json, int wday, int sod)
{
    if (!rules_json || !rules_json[0]) return 1;
    cJSON *arr = cJSON_Parse(rules_json);
    if (!arr || !cJSON_IsArray(arr)) { if (arr) cJSON_Delete(arr); return 1; }
    int match = 0; cJSON *rule;
    cJSON_ArrayForEach(rule, arr) {
        cJSON *wd = cJSON_GetObjectItem(rule, "weekdays"), *d; int day_ok = 0;
        if (cJSON_IsArray(wd)) cJSON_ArrayForEach(d, wd) if ((int)cJSON_GetNumberValue(d) == wday) { day_ok = 1; break; }
        if (!day_ok) continue;
        int st = hms_to_sod(cJSON_GetStringValue(cJSON_GetObjectItem(rule, "startTime")));
        int en = hms_to_sod(cJSON_GetStringValue(cJSON_GetObjectItem(rule, "endTime")));
        if (st < 0 || en < 0) continue;
        int in = (st <= en) ? (sod >= st && sod <= en) : (sod >= st || sod <= en);  /* en<st=跨零点 */
        if (in) { match = 1; break; }
    }
    cJSON_Delete(arr);
    return match;
}
/* 连续/仅事件排程评估:
 *   GET 无库配置仍回「开 + 7×24」给 GUI；真正录像只在对应行已保存后才开。
 *   continuous = 已保存连续排程 && 手动开 && sched_on && 落在时段
 *   event_arm  = 已保存事件排程 && 手动开 && !continuous
 * 仅变化时下发,避免反复重置关键帧门控。 */
static void rec_schedule_apply(nvr_app_t *app)
{
    if (!app->sm || !app->settings) return;
    time_t now = time(NULL); struct tm tmv; localtime_r(&now, &tmv);
    int wday = (tmv.tm_wday == 0) ? 7 : tmv.tm_wday;   /* tm 0=Sun → 协议 7=Sun */
    int sod  = tmv.tm_hour*3600 + tmv.tm_min*60 + tmv.tm_sec;
    for (int chn0 = 0; chn0 < 32; chn0++) {
        nvr_record_cfg_t rc;
        int rec_saved = (nvr_settings_record_get(app->settings, chn0, &rc) == 0);
        nvr_rec_schedule_t s;
        int cont_saved = (nvr_settings_rec_sched_get(app->settings, chn0, &s) == 0);
        int evt_saved  = nvr_settings_schedule_count(app->settings, chn0, "record_event") > 0;
        /* 总开关：有 record_config 行用其值；否则仅当已保存某种排程时视为开(GET 默认开)。 */
        int manual_on = rec_saved ? rc.record_on : ((cont_saved || evt_saved) ? 1 : 0);
        int continuous = manual_on && cont_saved && s.sched_on && rules_match_now(s.rules, wday, sod);
        int event_arm  = manual_on && evt_saved && !continuous;
        int pre_s = nvr_settings_record_pre_s_get(app->settings, chn0);
        if (app->rec_applied[chn0] != (signed char)continuous) {
            nvr_stream_set_record(app->sm, chn0, continuous);
            app->rec_applied[chn0] = (signed char)continuous;
        }
        if (app->evt_arm_applied[chn0] != (signed char)event_arm ||
            app->pre_s_applied[chn0] != pre_s) {
            nvr_stream_set_event_arm(app->sm, chn0, event_arm, pre_s);
            app->evt_arm_applied[chn0] = (signed char)event_arm;
            app->pre_s_applied[chn0] = pre_s;
        }
    }
}

/* WhichDay: 0=每天；1-7=周一..周日(与录像排程一致,7=周日)。
 * WhichTime: 0-23=整点；>=100 视为 HHMM(如 230=02:30)。同一日历日只触发一次。 */
static void auto_reboot_tick(nvr_app_t *app)
{
    if (!app || !app->settings) return;
    if (!nvr_settings_get_int(app->settings, "system.auto_reboot.enable", 0)) return;

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    int wday = (tmv.tm_wday == 0) ? 7 : tmv.tm_wday;
    int day = nvr_settings_get_int(app->settings, "system.auto_reboot.day", 0);
    if (day != 0 && day != wday) return;

    int wt = nvr_settings_get_int(app->settings, "system.auto_reboot.time", 0);
    int want_h, want_m;
    if (wt >= 100) { want_h = wt / 100; want_m = wt % 100; }
    else           { want_h = wt; want_m = 0; }
    if (want_h < 0 || want_h > 23 || want_m < 0 || want_m > 59) return;
    if (tmv.tm_hour != want_h || tmv.tm_min < want_m || tmv.tm_min > want_m + 1) return;

    static int last_ymd = -1;
    int ymd = (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
    if (last_ymd == ymd) return;
    last_ymd = ymd;

    printf("[app] 周维护自动重启(day=%d time=%d)\n", day, wt);
    sync();
    reboot(RB_AUTOBOOT);
}

void nvr_app_run(nvr_app_t *app)
{
    if (!app) return;
    unsigned tick = 0;
    int ble_bound_last = -1, ble_unlock_last = -1;
    memset(app->rec_applied, -1, sizeof(app->rec_applied));       /* 强制首轮下发 */
    memset(app->evt_arm_applied, -1, sizeof(app->evt_arm_applied));
    memset(app->pre_s_applied, -1, sizeof(app->pre_s_applied));
    while (app->running) {
        if (app->stg) nvr_storage_tick(app->stg);   /* 盘健康/满盘/热插拔 */
        nvr_chan_tick(app->cm);                      /* 在线状态机 + 重连 + 解析待定 URL */
        nvr_rec_tick(app->rs);                       /* 结束到期事件时窗 */
        if (tick % 5 == 0) rec_schedule_apply(app);  /* 每 5s 评估连续录像排程(时段+开关) */
        nvr_evt_tick(app->eh);                        /* 事件位图衰减(供 longPolling) */
        if (app->ble) {                               /* 向导/登录解锁变化 → BLE 广播 */
            nvr_owner_row_t ow;
            int bound = (app->settings &&
                         nvr_settings_owner_get(app->settings, &ow) == 0 && ow.owner_id[0]) ? 1 : 0;
            int unlocked = nvr_gui_ui_unlocked();
            if (bound != ble_bound_last || unlocked != ble_unlock_last) {
                nvr_ble_update_adv(app->ble, bound, unlocked);
                ble_bound_last = bound;
                ble_unlock_last = unlocked;
            }
        }
        if (tick % 60 == 0 && app->settings) {        /* 每 60s：NTP 重试 + 周维护重启 */
            nvr_time_tick(app->settings);
            auto_reboot_tick(app);
        }
        ++tick;
        /* 不再在 eth0 上周期性全网段自动发现绑定(会把 LAN 上别人的相机绑进来)。
         * PoE 口即插即用由每口 onvif_auto 通道的 tick 后台 ONVIF 广播覆盖;
         * LAN 新增设备走 LanAddDevice(显式添加)。 */
        sleep(1);
    }
}

void nvr_app_request_exit(nvr_app_t *app) { if (app) app->running = 0; }

void nvr_app_stop(nvr_app_t *app)
{
    if (!app) return;
    nvr_lan34569_server_stop();
    if (app->n8012) { nvr_nop8012_stop(app->n8012); app->n8012 = NULL; }
    if (app->talk) { nvr_talk_deinit(app->talk); app->talk = NULL; }
    if (app->ble) { nvr_ble_destroy(app->ble); app->ble = NULL; }
    stop_tutk(app);
    if (app->up) { nvr_cloud_uploader_stop(app->up); app->up = NULL; }
    if (app->cm) { nvr_chan_stop_all(app->cm); nvr_chan_mgr_deinit(app->cm); app->cm = NULL; }
    if (app->push) { nvr_push_stop(app->push); app->push = NULL; }
    if (app->eh) { nvr_evt_deinit(app->eh); app->eh = NULL; }
    if (app->pb) { nvr_playback_destroy(app->pb); app->pb = NULL; }   /* 回放先停(用 pv/sm) */
    if (app->pv) { nvr_preview_deinit(app->pv); app->pv = NULL; }
    if (app->persist) { nvr_chan_persist_close(app->persist); app->persist = NULL; }
    if (app->rs) { nvr_rec_sched_deinit(app->rs); app->rs = NULL; }
    if (app->nop_hub) { nop_event_hub_destroy(app->nop_hub); app->nop_hub = NULL; }
    if (app->nop_http_agent) { nop_http_server_stop(app->nop_http_agent); app->nop_http_agent = NULL; }
    if (app->nop_http) { nop_http_server_stop(app->nop_http); app->nop_http = NULL; }
    if (app->router) { nvr_cmd_router_stop(app->router); app->router = NULL; }
    if (app->onvif_backend) { nop_onvif_map_events_stop(app->onvif_backend);
                              nop_onvif_map_backend_destroy(app->onvif_backend); app->onvif_backend = NULL; }
    if (app->nop)    { nop_app_destroy(app->nop); app->nop = NULL; }
    if (app->nop_chans) { nop_nvr_channels_destroy(app->nop_chans); app->nop_chans = NULL; }
    if (app->sm) { nvr_stream_stop_all(app->sm); nvr_stream_mgr_deinit(app->sm); app->sm = NULL; }
    nvr_rtsp_live_stop();
    mhal_vout_deinit(MHAL_OUT_HDMI);
#if RSDK_CFG_METADATA
    if (app->meta) { rsdk_meta_close(app->meta); app->meta = NULL; }
#endif
    if (app->stg) { nvr_storage_deinit(app->stg); app->stg = NULL; }
    if (app->settings) { nvr_settings_close(app->settings); app->settings = NULL; }
    free(app);
}
