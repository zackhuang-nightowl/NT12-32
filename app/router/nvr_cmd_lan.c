/***************************************************************************************
 *  nvr_cmd_lan.c — lan 域 handler:GUI LAN-Add 六命令 → 真实通道管理器出图。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_log.h"           /* NVR_LOGI(多源枚举诊断) */
#include "nvr_onvif.h"          /* nvr_onvif_discover / nvr_onvif_cam_t */
#include "nvr_lan34569.h"       /* UDP 34569 备用发现（带 MAC） */
#include "nvr_dev_classify.h"   /* nvr_dev_classify */
#include "nvr_gui_config.h"     /* GUI_CONFIG.json channels=[PoE,LAN] 容量 */
#include "nvr_defaults.h"       /* PoE 内网段宏 NVR_POE_NET_A/B */
#include "nop_sdk/nop_onvif.h"      /* nop_onvif_device_create/set_auth(多源枚举) */
#include "nop_sdk/nop_onvif_ext.h"  /* nop_onvif_list_sources(设备的 VideoSourceToken 列表) */
#include "nvr_chan_bind.h"          /* NOP digest 开/关 */
#include "nvr_crypto.h"

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
    int i;
    if (!cam || !cam->host[0]) return;
    for (i = 0; i < acc->n; i++) {
        if (strcmp(acc->cam[i].host, cam->host) != 0) continue;
        /* 同 IP：ONVIF scopes 覆盖（分类更准）；无 /mac/ 则保留已有 34569 MAC */
        if (cam->scopes[0]) {
            char keep[80] = {0};
            if (strstr(acc->cam[i].scopes, "/mac/") && !strstr(cam->scopes, "/mac/")) {
                const char *p = strstr(acc->cam[i].scopes, "onvif://www.onvif.org/mac/");
                if (p) {
                    size_t k = 0;
                    while (p[k] && p[k] != ' ' && k < sizeof(keep) - 1) { keep[k] = p[k]; k++; }
                    keep[k] = 0;
                }
            }
            snprintf(acc->cam[i].scopes, sizeof(acc->cam[i].scopes), "%s%s%s",
                     cam->scopes, keep[0] ? " " : "", keep);
        }
        if (cam->port > 0) acc->cam[i].port = cam->port;
        if (cam->service_url[0])
            snprintf(acc->cam[i].service_url, sizeof(acc->cam[i].service_url), "%s", cam->service_url);
        return;
    }
    if (acc->n < 64) acc->cam[acc->n++] = *cam;
}
static void on_34569_found(const nvr_lan34569_dev_t *d, void *user){
    disc_acc_t *acc = user;
    nvr_onvif_cam_t cam;
    int i;
    nvr_lan34569_fill_cam(d, &cam);
    for (i = 0; i < acc->n; i++) {
        if (strcmp(acc->cam[i].host, cam.host) != 0) continue;
        /* 同 IP：34569 常带 MAC，补进已有 ONVIF 条目 */
        if (d->mac[0] && !strstr(acc->cam[i].scopes, "/mac/")) {
            char more[160];
            snprintf(more, sizeof(more), " onvif://www.onvif.org/mac/%s", d->mac);
            if (strlen(acc->cam[i].scopes) + strlen(more) < sizeof(acc->cam[i].scopes))
                strcat(acc->cam[i].scopes, more);
        }
        if (acc->cam[i].port <= 0 && cam.port > 0) acc->cam[i].port = cam.port;
        return;
    }
    on_found(&cam, user);
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
    nvr_lan34569_discover(NULL, 1, on_34569_found, &acc);   /* 补 MAC / WS-Discovery 漏掉的机 */

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
        /* 诊断:某设备实际未激活但 status 恒显示激活 → 打出发现到的原始 scopes 与判定结果,
         * 看相机上报的"未激活"字样到底是不是 nopState/inactive(nvr_dev_classify 只认这个子串)。 */
        NVR_LOGI("lansearch", "发现 ip=%s port=%d kind=%s active=%d mac=[%s] sn=[%s] scopes=[%s]",
                 cm->host, cm->port > 0 ? cm->port : 80, proto_of_kind(cls.kind),
                 cls.active, cls.mac, cls.serial, cm->scopes[0] ? cm->scopes : "(空)");
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "mac", cls.mac);
        cJSON_AddStringToObject(o, "ip", cm->host);
        /* 默认 active=1；仅 scopes 含 nopState/inactive 才为 0（未激活） */
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
        /* mainSource(可选,仅双视频源设备才带):0=主 channel(实际物理设备,源1);1=子 channel(第二路,dev_chn>1)。
         * GUI 据此判断哪个通道是物理设备口(见 nop_api_doc GUI_GetAddedLanDevices)。单源设备不带此字段。 */
        if (strcmp(list[i].type, "multi") == 0)
            cJSON_AddNumberToObject(o, "mainSource", (list[i].dev_chn > 1) ? 1 : 0);
        cJSON_AddNumberToObject(o, "status", nvr_chan_status_code_of(c->cm, list[i].chn));
        cJSON_AddStringToObject(o, "protocol", proto_of_kind(list[i].kind));
        cJSON_AddNumberToObject(o, "port", list[i].onvif_port > 0 ? list[i].onvif_port : 80);
        cJSON_AddStringToObject(o, "mac", list[i].mac);
        cJSON_AddStringToObject(o, "ip", list[i].onvif_ip);
        cJSON_AddStringToObject(o, "serial", list[i].serial);
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
    for (int i = 0; i < n; i++)
        if (strcmp(list[i].onvif_ip, ip) == 0 && (!found || list[i].chn < found->chn)) found = &list[i];

    cJSON *o = cJSON_CreateObject();
    if (found) cJSON_AddNumberToObject(o, "channel", found->chn + 1);
    cJSON_AddStringToObject(o, "protocol", found ? proto_of_kind(found->kind) : nvr_jstr(a, "protocol", "onvif"));
    cJSON_AddStringToObject(o, "mac", found ? found->mac : "");
    cJSON_AddStringToObject(o, "ip", ip);
    cJSON_AddNumberToObject(o, "port", found ? found->onvif_port : nvr_jint(a, "port", 80));
    cJSON_AddStringToObject(o, "serial", found ? found->serial : "");
    cJSON_AddStringToObject(o, "model", found ? found->model : "");
    cJSON_AddStringToObject(o, "name", found ? found->user : "");
    cJSON_AddStringToObject(o, "password", found ? found->pass : "");   /* 回显已存密码(GUI 预填) */
    cJSON_AddNumberToObject(o, "status", found ? nvr_chan_status_code_of(c->cm, found->chn) : 0);
    {
        int enh = 0;
        if (found && found->kind == NVR_DEV_KIND_NOP) {
            char rnd[64] = {0};
            int gr = nvr_chan_enh_get(found, rnd, sizeof(rnd));
            nvr_channel_t cur;
            if (nvr_chan_get(c->cm, found->chn, &cur) != 0) cur = *found;
            if (gr == 0) {
                enh = rnd[0] ? 1 : 0;
                if (!rnd[0] && (cur.enh_random[0] || cur.pass[0]))
                    nvr_chan_set_enh(c->cm, found->chn, "", "");
                else if (rnd[0] && strcmp(cur.enh_random, rnd) != 0) {
                    char penh[24];
                    if (nvr_pw_from_random(rnd, penh, sizeof(penh)) == 16)
                        nvr_chan_set_enh(c->cm, found->chn, rnd, penh);
                }
            } else if (gr == 1) {
                nvr_chan_set_enh(c->cm, found->chn, "", "");
                enh = 0;
            } else enh = cur.enh_random[0] ? 1 : 0;
        }
        cJSON_AddBoolToObject(o, "enhancedSecurity", enh);
    }

    /* 多视频源:仅当识别到 >1 源才回 videoSources。source=1-based 枚举序;
     * enabled = 同 ip 已有 channel 绑到该源(不必是源1;只开源2 则仅 source2 enabled)。 */
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

/* 每设备源列表缓存(IP→tokens)。★ 8089 http server 是单线程串行:一次阻塞的 ONVIF 枚举
 * (create+get_capabilities+get_services+list_sources,不可达时秒级)会卡住所有查询。
 * 源列表几乎不变 → 成功结果缓存到进程退出,后续 getLanDevice/setLanDevice 秒回,不再阻塞。 */
static struct { char ip[64]; char toks[8][100]; int n; int valid; } s_src_cache[24];
static int src_cache_get(const char *ip, char toks[][100], int max, int *out_n){
    for (int i = 0; i < 24; i++)
        if (s_src_cache[i].valid && strcmp(s_src_cache[i].ip, ip) == 0){
            int n = s_src_cache[i].n; if (n > max) n = max;
            for (int k = 0; k < n; k++){ strncpy(toks[k], s_src_cache[i].toks[k], 99); toks[k][99] = 0; }
            *out_n = s_src_cache[i].n; return 1;
        }
    return 0;
}
static void src_cache_put(const char *ip, char toks[][100], int n){
    int slot = -1;
    for (int i = 0; i < 24; i++){
        if (s_src_cache[i].valid && strcmp(s_src_cache[i].ip, ip) == 0){ slot = i; break; }
        if (slot < 0 && !s_src_cache[i].valid) slot = i;
    }
    if (slot < 0) slot = 0;
    memset(&s_src_cache[slot], 0, sizeof(s_src_cache[slot]));
    snprintf(s_src_cache[slot].ip, sizeof(s_src_cache[slot].ip), "%s", ip);
    int cn = n; if (cn > 8) cn = 8;
    for (int k = 0; k < cn; k++) strncpy(s_src_cache[slot].toks[k], toks[k], 99);
    s_src_cache[slot].n = n; s_src_cache[slot].valid = 1;
}

/* 枚举一台 ONVIF 设备的 VideoSourceToken 列表(多源识别)。缓存命中立即返回(不阻塞 8089)。
 * ★ 握手 create→auth→get_capabilities→get_services 后 list_sources(见其内 Media1/Media2 兜底)。 */
static int lan_list_sources(const char *ip, int port, const char *usr, const char *pw,
                            char toks[][100], int max){
    if (!ip || !ip[0]) return 0;
    int cn = 0;
    if (src_cache_get(ip, toks, max, &cn)) return cn;   /* 缓存命中 → 秒回,不做 ONVIF */
    int p = port > 0 ? port : 80;
    nop_onvif_device_t *dev = nop_onvif_device_retain(ip, p, "/onvif/device_service", 0);
    if (!dev) return 0;
    if (nop_onvif_device_connect(dev, usr, pw) != 0) {
        nop_onvif_device_drop(ip, p);
        return 0;
    }
    int ntok = nop_onvif_list_sources(dev, toks, max);
    NVR_LOGI("router", "多源枚举 %s:%d → ntok=%d%s%s", ip, p, ntok,
             ntok > 0 ? " tok0=" : "", ntok > 0 ? toks[0] : "");
    if (ntok < 0) ntok = 0;
    nop_onvif_device_drop(ip, p);
    if (ntok > 0) src_cache_put(ip, toks, ntok);   /* 只缓存成功(失败下次再试,不缓存空) */
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

static int lan_json_enabled(cJSON *en, int deflt){
    if (!en) return deflt;
    if (cJSON_IsBool(en)) return cJSON_IsTrue(en) ? 1 : 0;
    if (cJSON_IsNumber(en)) return ((int)cJSON_GetNumberValue(en)) != 0;
    return deflt;
}

/* 该 channel 当前绑的源序号(1-based):优先 VideoSourceToken 对上枚举表,否则 dev_chn(空=1)。 */
static int src_of_ch(const nvr_channel_t *ch, char toks[][100], int ntok){
    if (ch && ch->video_source_token[0] && ntok > 0) {
        for (int k = 0; k < ntok; k++)
            if (toks[k][0] && strcmp(ch->video_source_token, toks[k]) == 0) return k + 1;
    }
    return (ch && ch->dev_chn > 0) ? ch->dev_chn : 1;
}

static void bind_ch_to_source(nvr_chan_mgr_t *cm, nvr_channel_t *e, int source,
                              const char *tok, int ntok, const nvr_channel_t *creds){
    e->dev_chn = source;
    e->enabled = 1;
    e->poe_port = 0;          /* ★ LAN 视频源恒非 PoE:复用旧槽也强制清 poe_port,绝不落 PoE 板 */
    e->url[0] = 0;
    snprintf(e->video_source_token, sizeof(e->video_source_token), "%s", (tok && tok[0]) ? tok : "");
    snprintf(e->type, sizeof(e->type), ntok > 1 ? "multi" : "single");
    if (creds) {
        snprintf(e->user, sizeof(e->user), "%s", creds->user);
        snprintf(e->pass, sizeof(e->pass), "%s", creds->pass);
        snprintf(e->enh_random, sizeof(e->enh_random), "%s", creds->enh_random);
        if (creds->onvif_port > 0) e->onvif_port = creds->onvif_port;
    }
    nvr_chan_add(cm, e);
}

static void lan_wait_same_ip(const nvr_cmd_ctx_t *c, const char *ip){
    if (!c || !c->cm || !ip || !ip[0]) return;
    nvr_channel_t list[NVR_MAX_CH];
    int n = nvr_chan_list(c->cm, list, NVR_MAX_CH);
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].onvif_ip, ip) != 0) continue;
        nvr_chan_wait_bind(c->cm, list[i].chn, NVR_DEF_CMD_TIMEOUT_S * 1000);
        if (c->pv && nvr_chan_status_code_of(c->cm, list[i].chn) == 1)
            nvr_preview_wait_ready(c->pv, NVR_DEF_WAIT_READY_MS);
    }
}

/* videoSources[{source,enabled}] 是启用权威(可只开源2、不开源1)。
 * 每个 enabled 源 ↔ 一只 channel(dev_chn=source, token=toks[source-1])。
 * 关掉的源:其 channel 优先复用给尚无槽的启用源(Add 后只开源2 → 原槽改绑源2),否则删除。 */
static void lan_apply_video_sources(const nvr_cmd_ctx_t *c, const char *ip,
                                    const nvr_channel_t *tmpl, cJSON *vs){
    if (!c || !c->cm || !ip || !tmpl || !cJSON_IsArray(vs)) return;

    int want[17];
    memset(want, 0, sizeof(want));
    int any = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, vs) {
        int source = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(it, "source"));
        int enabled = lan_json_enabled(cJSON_GetObjectItem(it, "enabled"), 1);
        if (source < 1 || source > 16) continue;
        want[source] = enabled ? 1 : 0;
        any = 1;
    }
    if (!any) return;

    char toks[16][100];
    memset(toks, 0, sizeof(toks));
    int ntok = lan_list_sources(ip, tmpl->onvif_port, tmpl->user, tmpl->pass, toks, 16);

    nvr_channel_t cur[NVR_MAX_CH];
    int m = nvr_chan_list(c->cm, cur, NVR_MAX_CH);
    int bound[17];
    int reuse_chn[NVR_MAX_CH], nre = 0;
    for (int s = 0; s < 17; s++) bound[s] = -1;

    for (int i = 0; i < m; i++) {
        if (strcmp(cur[i].onvif_ip, ip) != 0) continue;
        /* ★ LAN 设备的视频源绝不能落 PoE 通道(<NVR_IP_CH_BASE)。历史误占的 PoE 通道直接删除,
         * 既不作源槽也不复用——LAN 源只允许 LAN 通道。 */
        if (cur[i].chn < NVR_IP_CH_BASE) { nvr_chan_remove(c->cm, cur[i].chn); continue; }
        int s = src_of_ch(&cur[i], toks, ntok);
        if (s >= 1 && s <= 16 && want[s] && bound[s] < 0) bound[s] = cur[i].chn;
        else reuse_chn[nre++] = cur[i].chn;
    }

    int reuse_i = 0;
    for (int s = 1; s <= 16; s++) {
        if (!want[s]) continue;
        const char *tok = (s - 1 < ntok) ? toks[s - 1] : "";
        if (bound[s] >= 0) {
            nvr_channel_t latest[NVR_MAX_CH];
            int nm = nvr_chan_list(c->cm, latest, NVR_MAX_CH);
            for (int i = 0; i < nm; i++) if (latest[i].chn == bound[s]) {
                nvr_channel_t e = latest[i];
                int need = (e.dev_chn != s);
                if (tok[0] && strcmp(e.video_source_token, tok) != 0) need = 1;
                if (strcmp(e.user, tmpl->user) != 0 || strcmp(e.pass, tmpl->pass) != 0) need = 1;
                if (tmpl->onvif_port > 0 && e.onvif_port != tmpl->onvif_port) need = 1;
                if (need) bind_ch_to_source(c->cm, &e, s, tok, ntok, tmpl);
                break;
            }
            continue;
        }
        nvr_channel_t e;
        memset(&e, 0, sizeof(e));
        int have = 0;
        if (reuse_i < nre) {
            int chn = reuse_chn[reuse_i++];
            nvr_channel_t latest[NVR_MAX_CH];
            int nm = nvr_chan_list(c->cm, latest, NVR_MAX_CH);
            for (int i = 0; i < nm; i++) if (latest[i].chn == chn) { e = latest[i]; have = 1; break; }
        }
        if (!have) {
            int chn = alloc_free_lan(c);
            if (chn < 0) continue;
            e = *tmpl;
            e.chn = chn; e.poe_port = 0; e.vout_win = chn;
            e.enabled = 1; e.record = 1; e.url[0] = 0;
            snprintf(e.name, sizeof(e.name), "Camera %d", chn + 1);
        }
        bind_ch_to_source(c->cm, &e, s, tok, ntok, tmpl);
        bound[s] = e.chn;
    }
    while (reuse_i < nre) nvr_chan_remove(c->cm, reuse_chn[reuse_i++]);
}

/* 删物理台:同 IP 的全部源通道一起删(含 type=multi 的额外源)。 */
static void lan_remove_device(nvr_chan_mgr_t *cm, int chn){
    if (!cm || chn < 0) return;
    nvr_channel_t list[NVR_MAX_CH];
    int n = nvr_chan_list(cm, list, NVR_MAX_CH);
    char ip[64] = {0};
    for (int i = 0; i < n; i++) if (list[i].chn == chn) {
        snprintf(ip, sizeof(ip), "%s", list[i].onvif_ip);
        break;
    }
    if (!ip[0]) { nvr_chan_remove(cm, chn); return; }
    int del[NVR_MAX_CH], nd = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(list[i].onvif_ip, ip) == 0) del[nd++] = list[i].chn;
    for (int i = 0; i < nd; i++) nvr_chan_remove(cm, del[i]);
}

/* ------------------------- GUI_setLanDevice ------------------------- */
/* 账密/端口写到同 IP 全部通道;videoSources 整体下发决定启用哪些源(不必是源1)。 */
char *cmd_GUI_setLanDevice(cJSON *a, const nvr_cmd_ctx_t *c){
    const char *ip = nvr_jstr(a, "ip", NULL);
    if (!nvr_jstr(a, "protocol", NULL) || !ip || !nvr_jhas(a, "port") || !nvr_jhas(a, "enhancedSecurity"))
        return nvr_resp_err("invalid_param");

    nvr_channel_t list[NVR_MAX_CH]; int n = c->cm ? nvr_chan_list(c->cm, list, NVR_MAX_CH) : 0;
    nvr_channel_t d; int found = 0;
    for (int i = 0; i < n; i++) if (strcmp(list[i].onvif_ip, ip) == 0) {
        if (!found || list[i].chn < d.chn) { d = list[i]; found = 1; }
    }
    if (!found) return nvr_resp_result("OK");

    const char *usr = nvr_jstr(a, "name", NULL);
    const char *pw  = nvr_jstr(a, "password", NULL);
    const char *protocol = nvr_jstr(a, "protocol", NULL);
    int enh = nvr_jbool(a, "enhancedSecurity", 0);
    int port = nvr_jint(a, "port", 0); if (port > 0) d.onvif_port = port;

    /* NOP digest：true=NVR 设 random 并用 P_enh 连（ONVIF/8012）；false=SET random="" 后空凭据。
     * 开关前必须用库里账密（关 digest 时 SET 仍可能要带 P_enh）。 */
    if (protocol && !strcmp(protocol, "nop")) {
        char penh[24] = {0};
        int er = nvr_chan_enh_apply(&d, enh, NULL, penh, sizeof(penh));
        if (er == 1) return nvr_resp_not_support();
        if (er != 0) return nvr_resp_err("failed");
        snprintf(d.user, sizeof(d.user), "admin");
        snprintf(d.pass, sizeof(d.pass), "%s", enh ? penh : "");
        NVR_LOGI("lan", "setLanDevice ip=%s enhancedSecurity=%d pass=%s",
                 ip, enh, enh ? "P_enh" : "(empty)");
    } else {
        if (usr && usr[0]) snprintf(d.user, sizeof(d.user), "%s", usr);
        else               snprintf(d.user, sizeof(d.user), "admin");
        snprintf(d.pass, sizeof(d.pass), "%s", pw ? pw : "");
    }

    cJSON *vs = cJSON_GetObjectItem(a, "videoSources");
    if (cJSON_IsArray(vs)) {
        lan_apply_video_sources(c, ip, &d, vs);
    } else {
        nvr_channel_t cur[NVR_MAX_CH];
        int m = nvr_chan_list(c->cm, cur, NVR_MAX_CH);
        for (int i = 0; i < m; i++) {
            if (strcmp(cur[i].onvif_ip, ip) != 0) continue;
            nvr_channel_t e = cur[i];
            snprintf(e.user, sizeof(e.user), "%s", d.user);
            snprintf(e.pass, sizeof(e.pass), "%s", d.pass);
            snprintf(e.enh_random, sizeof(e.enh_random), "%s", d.enh_random);
            if (d.onvif_port > 0) e.onvif_port = d.onvif_port;
            e.url[0] = 0;
            if (nvr_chan_add(c->cm, &e) < 0) return nvr_resp_result("Failed for other reason");
        }
    }
    lan_wait_same_ip(c, ip);
    return nvr_resp_result("OK");
}

/* ------------------------- GUI_LanAddDevice ------------------------- */
/* 立即入库；回复前等首次取流结束或超时(GUI 靠本接口阻塞)。
 * 通道分配:PoE 段 IP(198.18.<口>.x, eth1)→ 绑对应 PoE 口通道(poe_port=口);
 * LAN(eth0)设备 → 忽略显式 channel,按空闲 LAN 通道(≥NVR_IP_CH_BASE)顺序分配,poe_port 恒 0,
 * LAN 槽满则添加失败(-1)。容量取自 GUI_CONFIG.json channels=[PoE,LAN](硬上限 16 PoE + 16 LAN)。 */
static int assign_channel(const nvr_cmd_ctx_t *c, cJSON *a, const char *ip, int *poe_port){
    *poe_port = 0;
    int poe_n = NVR_POE_PORTS, lan_n = NVR_MAX_CH - NVR_IP_CH_BASE;
    nvr_gui_config_get_channels(&poe_n, &lan_n);
    if (poe_n < 0) poe_n = 0; if (poe_n > NVR_POE_PORTS) poe_n = NVR_POE_PORTS;
    int lan_cap = NVR_IP_CH_BASE + lan_n;   /* LAN 通道号上界(不含) */
    if (lan_cap > NVR_MAX_CH) lan_cap = NVR_MAX_CH;

    /* ★ poe_port 只由 IP 决定(198.18.<口>.x 且口 1..poe_n 才是 PoE 设备),绝不凭通道号——
     * 否则 LAN 设备被误标 PoE、跑到 PoE 板上(严重错误)。 */
    int aa, bb, cc, dd;
    if (sscanf(ip, "%d.%d.%d.%d", &aa, &bb, &cc, &dd) == 4 &&
        aa == NVR_POE_NET_A && bb == NVR_POE_NET_B && cc >= 1 && cc <= poe_n) {
        *poe_port = cc; return cc - 1;                          /* PoE 段 IP → 绑对应 PoE 口通道 */
    }

    /* 到这里必是 LAN(eth0)设备:**不认显式 channel**,一律按空闲 LAN 通道(≥NVR_IP_CH_BASE)
     * 顺序分配;LAN 槽满 → 返回 -1 添加失败。poe_port 恒 0(上面已保证)。
     * 同 IP 已添加 → 复用其原通道,避免重复占槽。 */
    nvr_channel_t list[NVR_MAX_CH]; int n = c->cm ? nvr_chan_list(c->cm, list, NVR_MAX_CH) : 0;
    int occ[NVR_MAX_CH] = {0};
    for (int i = 0; i < n; i++) {
        if (list[i].chn < 0 || list[i].chn >= NVR_MAX_CH || !list[i].enabled) continue;
        occ[list[i].chn] = 1;
        if (list[i].onvif_ip[0] && strcmp(list[i].onvif_ip, ip) == 0) return list[i].chn;
    }
    for (int i = NVR_IP_CH_BASE; i < lan_cap; i++) if (!occ[i]) return i;   /* LAN 槽满 → -1 添加失败 */
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
    /* 先入库。nopOnvif 不能凭 SN 猜，只能 Discovery/GetScopes 有 nopOnvif 标识后再分类。
     * GUI protocol=="nop" 才是 NOP；其余先当通用 ONVIF。用户密码原样保存（123456/空=空密）。 */
    if (!strcmp(protocol, "nop")) {
        kind = NVR_DEV_KIND_NOP;
    } else {
        kind = NVR_DEV_KIND_ONVIF;
        if (argpass && argpass[0] && strcmp(argpass, "123456") != 0)
            snprintf(pass, sizeof(pass), "%s", argpass);
    }

    nvr_channel_t d; memset(&d, 0, sizeof(d));
    d.chn = chn; d.enabled = 1; d.record = 1; d.dev_chn = 1;
    snprintf(d.type, sizeof(d.type), "single");
    d.poe_port = poe_port;
    d.onvif_auto = 1; d.onvif_port = nvr_jint(a, "port", 0) > 0 ? nvr_jint(a, "port", 0) : 80;
    d.stream = NVR_STREAM_MAIN; d.codec = NVR_CODEC_AUTO; d.vout_win = chn;
    d.kind = kind; d.backend = (int)nvr_dev_backend_of((nvr_dev_kind_t)kind);
    snprintf(d.onvif_ip, sizeof(d.onvif_ip), "%s", ip);
    { const char *mac = nvr_jstr(a, "mac", NULL); if (mac && mac[0]) snprintf(d.mac, sizeof(d.mac), "%s", mac); }
    { const char *md = nvr_jstr(a, "model", NULL); if (md && md[0]) snprintf(d.model, sizeof(d.model), "%s", md); }
    if (serial && serial[0]) snprintf(d.serial, sizeof(d.serial), "%s", serial);
    snprintf(d.user, sizeof(d.user), "admin");
    snprintf(d.pass, sizeof(d.pass), "%s", pass);
    { const char *nm = nvr_jstr(a, "name", NULL);
      if (nm && nm[0]) snprintf(d.name, sizeof(d.name), "%s", nm);
      else snprintf(d.name, sizeof(d.name), "Camera %d", chn + 1); }

    /* 立刻入库并回复；连接在 tick 后台做。LanSearch.status=0 → 待激活(码7)。 */
    if (nvr_chan_add(c->cm, &d) < 0) return nvr_resp_result("Failed for other reason");
    /* ★ 添加只建**单个视频源**(source1,dev_chn=1)。多视频源设备(如 .109)默认不展开第二源;
     * 启用更多源须走 GUI_setLanDevice 的 videoSources(见 lan_apply_video_sources)。 */
    if (nvr_jint(a, "status", 1) == 0) {
        nvr_chan_substate_t sub; memset(&sub, 0, sizeof(sub));
        sub.inactive = 1;
        nvr_chan_set_substate(c->cm, chn, &sub);
    } else {
        lan_wait_same_ip(c, ip);
    }
    return nvr_resp_result("OK");
}

/* ------------------------- GUI_LanDelDevice ------------------------- */
/* 删物理台:同 IP 多源 channel 一并删除(不控制相机)。 */
char *cmd_GUI_LanDelDevice(cJSON *a, const nvr_cmd_ctx_t *c){
    if (!nvr_jhas(a, "channel")) return nvr_resp_err("invalid_param");
    int ch = nvr_jint(a, "channel", 0);
    if (c->cm && ch > 0) lan_remove_device(c->cm, ch - 1);
    return nvr_resp_ok();
}
