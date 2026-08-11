/***************************************************************************************
 *  nvr_cmd_lan.c — lan 域 handler:GUI LAN-Add 六命令 → 真实通道管理器出图。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_log.h"           /* NVR_LOGI(多源枚举诊断) */
#include "nvr_onvif.h"          /* nvr_onvif_discover / nvr_onvif_cam_t */
#include "nvr_dev_classify.h"   /* nvr_dev_classify */
#include "nvr_crypto.h"         /* nvr_pw_activate(nopOnvif 激活密码 P_act) */
#include "nvr_gui_config.h"     /* GUI_CONFIG.json channels=[PoE,LAN] 容量 */
#include "nvr_defaults.h"       /* PoE 内网段宏 NVR_POE_NET_A/B */
#include "nop_sdk/nop_onvif.h"      /* nop_onvif_device_create/set_auth(多源枚举) */
#include "nop_sdk/nop_onvif_ext.h"  /* nop_onvif_list_sources(设备的 VideoSourceToken 列表) */

/* 前置声明:多源枚举(定义在 setLanDevice 之前) */
static int lan_list_sources(const char *ip, int port, const char *usr, const char *pw,
                            char toks[][100], int max);
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define LAN_DISC_SECS 5

/* kind(0=NOP 1=NOPONVIF 2=ONVIF) → 上报协议名:"nop" 仅 NOP,其余走 ONVIF 后端记 "onvif" */
static const char *proto_of_kind(int kind){ return (kind == NVR_DEV_KIND_NOP) ? "nop" : "onvif"; }

/* ------------------------- GUI_LanSearch ------------------------- */
typedef struct { nvr_onvif_cam_t cam[64]; int n; } disc_acc_t;
static void on_found(const nvr_onvif_cam_t *cam, void *user){
    disc_acc_t *acc = user;
    if (acc->n < 64 && cam->host[0]) acc->cam[acc->n++] = *cam;
}
static int is_bound_ip(nvr_chan_mgr_t *cm, const char *ip){
    nvr_channel_t list[NVR_MAX_CH]; int n = cm ? nvr_chan_list(cm, list, NVR_MAX_CH) : 0;
    for (int i = 0; i < n; i++) if (strcmp(list[i].onvif_ip, ip) == 0) return 1;
    return 0;
}
char *cmd_GUI_LanSearch(cJSON *a, const nvr_cmd_ctx_t *c){
    if (!nvr_jhas(a, "protocol") || !cJSON_IsString(cJSON_GetObjectItem(a, "protocol")))
        return nvr_resp_err("invalid_param");
    disc_acc_t acc; acc.n = 0;
    nvr_onvif_discover(NULL, LAN_DISC_SECS, on_found, &acc);

    cJSON *content = cJSON_CreateObject();
    cJSON *devs = cJSON_AddArrayToObject(content, "devices");
    for (int i = 0; i < acc.n; i++) {
        const nvr_onvif_cam_t *cm = &acc.cam[i];
        /* PoE(198.18.<口>.x) 固定IP、按 PoE 口自动绑定,**不列入 LAN 搜索** */
        { int aa, bb, cc, dd; if (sscanf(cm->host, "%d.%d.%d.%d", &aa, &bb, &cc, &dd) == 4 && aa == 198 && bb == 18) continue; }
        if (is_bound_ip(c->cm, cm->host)) continue;
        int dup = 0; for (int j = 0; j < i; j++) if (!strcmp(acc.cam[j].host, cm->host)) { dup = 1; break; }
        if (dup) continue;
        nvr_dev_class_t cls; nvr_dev_classify(cm->scopes, &cls);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "mac", cls.mac);
        cJSON_AddStringToObject(o, "ip", cm->host);
        cJSON_AddNumberToObject(o, "status", cls.active ? 1 : 0);
        cJSON_AddStringToObject(o, "protocol", proto_of_kind(cls.kind));
        cJSON_AddNumberToObject(o, "port", cm->port > 0 ? cm->port : 80);
        cJSON_AddStringToObject(o, "serial", cls.serial);
        cJSON_AddStringToObject(o, "model", cls.model);      /* 型号:discovery scopes 的 /hardware/ */
        cJSON_AddStringToObject(o, "uid", "");
        cJSON_AddStringToObject(o, "name", cls.name);        /* 设备名:discovery scopes 的 /name/ */
        cJSON_AddItemToArray(devs, o);
    }
    return nvr_resp_content(content);
}

/* ------------------------- GUI_GetAddedLanDevices ------------------------- */
char *cmd_GUI_GetAddedLanDevices(cJSON *a, const nvr_cmd_ctx_t *c){
    (void)a;
    cJSON *content = cJSON_CreateObject();
    cJSON *devs = cJSON_AddArrayToObject(content, "devices");
    nvr_channel_t list[NVR_MAX_CH]; int n = c->cm ? nvr_chan_list(c->cm, list, NVR_MAX_CH) : 0;
    for (int i = 0; i < n; i++) {
        /* ★ 只列"真实设备"(经发现拿到 mac → DB 有记录)。空 PoE 口(配置预建、没插相机、无 mac)
         * 不列入已添加清单。状态同样按实际设备记录(status_code_of 对非真机返 0)。 */
        if (list[i].mac[0] == 0 && nvr_chan_status_code_of(c->cm, list[i].chn) == 0) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "channel", list[i].chn + 1);
        cJSON_AddNumberToObject(o, "status", nvr_chan_status_code_of(c->cm, list[i].chn));
        cJSON_AddStringToObject(o, "protocol", proto_of_kind(list[i].kind));
        cJSON_AddNumberToObject(o, "port", list[i].onvif_port > 0 ? list[i].onvif_port : 80);
        cJSON_AddStringToObject(o, "mac", list[i].mac);
        cJSON_AddStringToObject(o, "ip", list[i].onvif_ip);
        cJSON_AddStringToObject(o, "serial", "");
        cJSON_AddStringToObject(o, "model", list[i].model);
        cJSON_AddStringToObject(o, "uid", "");
        cJSON_AddStringToObject(o, "name", list[i].name);
        cJSON_AddItemToArray(devs, o);
    }
    return nvr_resp_content(content);
}

/* ------------------------- GUI_getLanDevice ------------------------- */
char *cmd_GUI_getLanDevice(cJSON *a, const nvr_cmd_ctx_t *c){
    const char *ip = nvr_jstr(a, "ip", NULL);
    if (!ip) return nvr_resp_err("invalid_param");
    nvr_channel_t list[NVR_MAX_CH]; int n = c->cm ? nvr_chan_list(c->cm, list, NVR_MAX_CH) : 0;
    nvr_channel_t *found = NULL;
    for (int i = 0; i < n; i++) if (strcmp(list[i].onvif_ip, ip) == 0) { found = &list[i]; break; }

    cJSON *o = cJSON_CreateObject();
    if (found) cJSON_AddNumberToObject(o, "channel", found->chn + 1);
    cJSON_AddStringToObject(o, "protocol", found ? proto_of_kind(found->kind) : nvr_jstr(a, "protocol", "onvif"));
    cJSON_AddStringToObject(o, "mac", found ? found->mac : "");
    cJSON_AddStringToObject(o, "ip", ip);
    cJSON_AddNumberToObject(o, "port", found ? found->onvif_port : nvr_jint(a, "port", 80));
    cJSON_AddStringToObject(o, "serial", "");
    cJSON_AddStringToObject(o, "model", found ? found->model : "");
    cJSON_AddStringToObject(o, "name", found ? found->user : "");
    cJSON_AddStringToObject(o, "password", found ? found->pass : "");   /* 回显已存密码(GUI 预填) */
    cJSON_AddNumberToObject(o, "status", found ? nvr_chan_status_code_of(c->cm, found->chn) : 0);
    cJSON_AddBoolToObject(o, "enhancedSecurity", 0);

    /* 多视频源:仅当识别到 >1 源(真多源)才回 videoSources;单源设备静默不带该字段。
     * 源序号 k(1-based);enabled = 同 ip 存在 dev_chn==k 的启用 channel。 */
    {
        const char *usr = found ? found->user : nvr_jstr(a, "name", NULL);
        const char *pw  = found ? found->pass : nvr_jstr(a, "password", NULL);
        int port = found ? found->onvif_port : nvr_jint(a, "port", 80);
        char toks[16][100];
        int ntok = lan_list_sources(ip, port, usr, pw, toks, 16);
        if (ntok > 1) {
            cJSON *vsa = cJSON_AddArrayToObject(o, "videoSources");
            for (int k = 1; k <= ntok; k++) {
                int en = 0;
                for (int i = 0; i < n; i++)
                    if (strcmp(list[i].onvif_ip, ip) == 0 && (list[i].dev_chn > 0 ? list[i].dev_chn : 1) == k && list[i].enabled) { en = 1; break; }
                cJSON *s = cJSON_CreateObject();
                cJSON_AddNumberToObject(s, "source", k);
                cJSON_AddBoolToObject(s, "enabled", en);
                cJSON_AddItemToArray(vsa, s);
            }
        }
    }
    return nvr_resp_content(o);
}

/* 枚举一台 ONVIF 设备的 VideoSourceToken 列表(多源识别)。返回源数(0=枚举失败/单源无法确认)。
 * ★ 必须走完握手:create→auth→get_capabilities(解析服务端点)→get_services(解析 Media2 XAddr),
 *   否则 list_sources 内部的 tr2 GetProfiles 因无 Media2 端点直接失败(返回 0)。 */
static int lan_list_sources(const char *ip, int port, const char *usr, const char *pw,
                            char toks[][100], int max){
    if (!ip || !ip[0]) return 0;
    nop_onvif_device_t *dev = nop_onvif_device_create(ip, port > 0 ? port : 80, "/onvif/device_service", 0);
    if (!dev) return 0;
    nop_onvif_device_set_auth(dev, (usr && usr[0]) ? usr : "admin", pw ? pw : "");
    nop_onvif_device_set_timeout(dev, 3000);
    nop_onvif_get_capabilities(dev);        /* 解析 tr1 服务端点 */
    nop_onvif_get_services(dev);            /* 解析 Media2 XAddr(tr2 GetProfiles 前置) */
    int ntok = nop_onvif_list_sources(dev, toks, max);
    NVR_LOGI("router", "多源枚举 %s:%d → ntok=%d%s%s", ip, port > 0 ? port : 80, ntok,
             ntok > 0 ? " tok0=" : "", ntok > 0 ? toks[0] : "");
    if (ntok < 0) ntok = 0;
    nop_onvif_device_destroy(dev);
    return ntok;
}

/* 分配一个空闲 LAN 通道号(额外视频源用;不认显式 channel 键)。满 → -1。 */
static int alloc_free_lan(const nvr_cmd_ctx_t *c){
    int poe_n = NVR_POE_PORTS, lan_n = NVR_MAX_CH - NVR_IP_CH_BASE;
    nvr_gui_config_get_channels(&poe_n, &lan_n);
    int lan_cap = NVR_IP_CH_BASE + lan_n; if (lan_cap > NVR_MAX_CH) lan_cap = NVR_MAX_CH;
    nvr_channel_t list[NVR_MAX_CH]; int n = c->cm ? nvr_chan_list(c->cm, list, NVR_MAX_CH) : 0;
    int occ[NVR_MAX_CH] = {0};
    for (int i = 0; i < n; i++) if (list[i].chn >= 0 && list[i].chn < NVR_MAX_CH && list[i].enabled) occ[list[i].chn] = 1;
    for (int i = NVR_IP_CH_BASE; i < lan_cap; i++) if (!occ[i]) return i;
    return -1;
}

/* ------------------------- GUI_setLanDevice ------------------------- */
/* 物理 channel(添加时那个)配置 + **多视频源 diff**:videoSources[{source,enabled}] 整体下发。
 * source=1 = 物理源(不动);source>1 的额外源:enabled 无 channel → 分配空闲 channel(type=multi,
 * dev_chn=source,同设备信息);disabled 有额外 channel → 删除。物理台只 DelDevice 才删。 */
char *cmd_GUI_setLanDevice(cJSON *a, const nvr_cmd_ctx_t *c){
    const char *ip = nvr_jstr(a, "ip", NULL);
    if (!nvr_jstr(a, "protocol", NULL) || !ip || !nvr_jhas(a, "port") || !nvr_jhas(a, "enhancedSecurity"))
        return nvr_resp_err("invalid_param");

    nvr_channel_t list[NVR_MAX_CH]; int n = c->cm ? nvr_chan_list(c->cm, list, NVR_MAX_CH) : 0;
    /* 物理台 = 同 ip 且 dev_chn<=1(添加时那个);找不到就取同 ip 首个。 */
    nvr_channel_t d; int found = 0;
    for (int i = 0; i < n; i++) if (strcmp(list[i].onvif_ip, ip) == 0 && list[i].dev_chn <= 1) { d = list[i]; found = 1; break; }
    if (!found) for (int i = 0; i < n; i++) if (strcmp(list[i].onvif_ip, ip) == 0) { d = list[i]; found = 1; break; }
    if (!found) return nvr_resp_result("OK");

    const char *usr = nvr_jstr(a, "name", NULL);
    const char *pw  = nvr_jstr(a, "password", NULL);
    if (usr && usr[0]) snprintf(d.user, sizeof(d.user), "%s", usr);
    else               snprintf(d.user, sizeof(d.user), "admin");
    snprintf(d.pass, sizeof(d.pass), "%s", pw ? pw : "");
    int port = nvr_jint(a, "port", 0); if (port > 0) d.onvif_port = port;
    d.url[0] = 0; if (d.dev_chn <= 0) d.dev_chn = 1;
    if (nvr_chan_add(c->cm, &d) < 0) return nvr_resp_result("Failed for other reason");

    /* ---- 多视频源 diff ---- */
    cJSON *vs = cJSON_GetObjectItem(a, "videoSources");
    if (cJSON_IsArray(vs)) {
        /* 枚举该物理设备的 VideoSourceToken(源序号 source → toks[source-1]);
         * 走完整握手(见 lan_list_sources);失败则 ntok=0 → 额外源 token 留空(降级=首源,不崩)。 */
        char toks[16][100];
        int ntok = lan_list_sources(ip, (port > 0) ? port : d.onvif_port, d.user, d.pass, toks, 16);
        cJSON *it;
        cJSON_ArrayForEach(it, vs) {
            int source = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(it, "source"));
            cJSON *en  = cJSON_GetObjectItem(it, "enabled");
            int enabled = cJSON_IsBool(en) ? cJSON_IsTrue(en) : 1;
            if (source <= 1) continue;                          /* 源1 = 物理,始终在 */
            const char *want_tok = (source - 1 < ntok) ? toks[source - 1] : "";
            /* 该额外源当前是否已有 channel(同 ip + dev_chn==source);记录其现有 token */
            nvr_channel_t cur[NVR_MAX_CH]; int m = nvr_chan_list(c->cm, cur, NVR_MAX_CH);
            int exist = -1; const char *exist_tok = "";
            for (int i = 0; i < m; i++)
                if (strcmp(cur[i].onvif_ip, ip) == 0 && cur[i].dev_chn == source) {
                    exist = cur[i].chn; exist_tok = cur[i].video_source_token; break;
                }
            if (enabled && exist < 0) {
                int chn = alloc_free_lan(c);
                if (chn >= 0) {
                    nvr_channel_t e = d;                        /* 复用物理台的设备信息(ip/账密/backend) */
                    e.chn = chn; e.dev_chn = source; e.poe_port = 0; e.vout_win = chn;
                    e.enabled = 1; e.record = 1; e.url[0] = 0;
                    /* 该源真实 VideoSourceToken(SDK 按 token 区分源);枚举失败则留空=降级首源 */
                    snprintf(e.video_source_token, sizeof(e.video_source_token), "%s", want_tok);
                    snprintf(e.type, sizeof(e.type), "multi");
                    snprintf(e.name, sizeof(e.name), "Camera %d", chn + 1);
                    nvr_chan_add(c->cm, &e);
                }
            } else if (enabled && exist >= 0 && want_tok[0] && strcmp(exist_tok, want_tok) != 0) {
                /* 已存在但 token 缺失/过期(如旧库迁移前分配的)→ 修正为真实源 token 并重拉 */
                for (int i = 0; i < m; i++) if (cur[i].chn == exist) {
                    nvr_channel_t e = cur[i];
                    snprintf(e.video_source_token, sizeof(e.video_source_token), "%s", want_tok);
                    snprintf(e.type, sizeof(e.type), "multi");
                    if (e.dev_chn <= 0) e.dev_chn = source;
                    e.url[0] = 0;   /* 清缓存 URL,强制按新 token 重解析(否则沿用旧源的 /10x) */
                    nvr_chan_add(c->cm, &e);
                    break;
                }
            } else if (!enabled && exist >= 0) {
                nvr_chan_remove(c->cm, exist);                  /* 关源 = 删多分配的那个 channel */
            }
        }
    }
    return nvr_resp_result("OK");
}

/* ------------------------- GUI_LanAddDevice ------------------------- */
/* 通道分配:显式 channel 优先;198.18.<口>.100 → PoE 口-1;否则首个空数字通道(≥16)。
 * 容量取自 GUI_CONFIG.json channels=[PoE,LAN](不超硬上限 16 PoE + 16 LAN);超容量 → 返回 -1 不添加。 */
static int assign_channel(const nvr_cmd_ctx_t *c, cJSON *a, const char *ip, int *poe_port){
    *poe_port = 0;
    int poe_n = NVR_POE_PORTS, lan_n = NVR_MAX_CH - NVR_IP_CH_BASE;
    nvr_gui_config_get_channels(&poe_n, &lan_n);
    if (poe_n < 0) poe_n = 0; if (poe_n > NVR_POE_PORTS) poe_n = NVR_POE_PORTS;
    int lan_cap = NVR_IP_CH_BASE + lan_n;   /* LAN 通道号上界(不含) */
    if (lan_cap > NVR_MAX_CH) lan_cap = NVR_MAX_CH;

    if (nvr_jhas(a, "channel")) {
        int ch = nvr_jint(a, "channel", 0);
        if (ch <= 0 || ch > lan_cap) return -1;               /* 显式通道超容量 → 不添加 */
        *poe_port = (ch - 1 < poe_n) ? ch : 0;
        return ch - 1;
    }
    int aa, bb, cc, dd;
    if (sscanf(ip, "%d.%d.%d.%d", &aa, &bb, &cc, &dd) == 4 && aa == NVR_POE_NET_A && bb == NVR_POE_NET_B && cc >= 1 && cc <= poe_n) {
        *poe_port = cc; return cc - 1;                          /* PoE 口(超 poe_n 不认) */
    }
    nvr_channel_t list[NVR_MAX_CH]; int n = c->cm ? nvr_chan_list(c->cm, list, NVR_MAX_CH) : 0;
    int occ[NVR_MAX_CH] = {0};
    for (int i = 0; i < n; i++) if (list[i].chn >= 0 && list[i].chn < NVR_MAX_CH && list[i].enabled) occ[list[i].chn] = 1;
    for (int i = NVR_IP_CH_BASE; i < lan_cap; i++) if (!occ[i]) return i;   /* LAN 槽满 → -1 不添加 */
    return -1;
}
char *cmd_GUI_LanAddDevice(cJSON *a, const nvr_cmd_ctx_t *c){
    const char *protocol = nvr_jstr(a, "protocol", NULL);
    const char *ip = nvr_jstr(a, "ip", NULL);
    if (!protocol || !ip) return nvr_resp_err("invalid_param");

    int poe_port = 0;
    int chn = assign_channel(c, a, ip, &poe_port);
    if (chn < 0) return nvr_resp_result("Failed for no free channel");

    int kind; char pass[64] = {0};
    const char *argpass = nvr_jstr(a, "password", NULL);
    const char *serial = nvr_jstr(a, "serial", NULL);
    if (!strcmp(protocol, "nop")) {
        kind = NVR_DEV_KIND_NOP;
    } else if (argpass && argpass[0]) {
        kind = NVR_DEV_KIND_ONVIF; snprintf(pass, sizeof(pass), "%s", argpass);
    } else if (serial && serial[0]) {
        kind = NVR_DEV_KIND_NOPONVIF;
        nvr_pw_activate(c->nvr_sn[0] ? c->nvr_sn : "", serial, pass, sizeof(pass));
    } else {
        kind = NVR_DEV_KIND_ONVIF;
    }

    nvr_channel_t d; memset(&d, 0, sizeof(d));
    d.chn = chn; d.enabled = 1; d.record = 1;
    d.poe_port = poe_port;
    d.onvif_auto = 1; d.onvif_port = nvr_jint(a, "port", 0) > 0 ? nvr_jint(a, "port", 0) : 80;
    d.stream = NVR_STREAM_MAIN; d.codec = NVR_CODEC_AUTO; d.vout_win = chn;
    d.kind = kind; d.backend = (int)nvr_dev_backend_of((nvr_dev_kind_t)kind);
    snprintf(d.onvif_ip, sizeof(d.onvif_ip), "%s", ip);
    { const char *mac = nvr_jstr(a, "mac", NULL); if (mac && mac[0]) snprintf(d.mac, sizeof(d.mac), "%s", mac); }
    { const char *md = nvr_jstr(a, "model", NULL); if (md && md[0]) snprintf(d.model, sizeof(d.model), "%s", md); } /* 型号:GUI 从 LanSearch 结果带回 */
    snprintf(d.user, sizeof(d.user), "admin");
    snprintf(d.pass, sizeof(d.pass), "%s", pass);
    { const char *nm = nvr_jstr(a, "name", NULL);
      if (nm && nm[0]) snprintf(d.name, sizeof(d.name), "%s", nm);
      else snprintf(d.name, sizeof(d.name), "Camera %d", chn + 1); }

    if (nvr_chan_add(c->cm, &d) < 0) return nvr_resp_result("Failed for other reason");
    return nvr_resp_result("OK");
}

/* ------------------------- GUI_LanDelDevice ------------------------- */
char *cmd_GUI_LanDelDevice(cJSON *a, const nvr_cmd_ctx_t *c){
    if (!nvr_jhas(a, "channel")) return nvr_resp_err("invalid_param");
    int ch = nvr_jint(a, "channel", 0);
    if (c->cm && ch > 0) nvr_chan_remove(c->cm, ch - 1);
    return nvr_resp_ok();
}
