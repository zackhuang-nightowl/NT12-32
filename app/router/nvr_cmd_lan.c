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
#include "nvr_identity.h"
#include "nvr_netime.h"

/* 前置声明:多源枚举(定义在 setLanDevice 之前) */
static int lan_list_sources(const char *ip, int port, const char *usr, const char *pw,
                            char toks[][100], int max);
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <curl/curl.h>

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
        /* ★ 播种发现缓存:用户随后添加这台时,resolve 首解析直接命中(免慢/串行 probe)→ 立刻判
         * nopOnvif → 算 P_act → 秒出图。修"首次添加密码对也不出图/要很久"。 */
        if (cm->scopes[0])
            nvr_onvif_cache_seed(cm->host, cm->scopes, cm->service_url, cm->port);
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
        /* mainSource(可选,仅双视频源设备才带):0=主 channel(设备代表,首分配通道 is_main=1);
         * 1=子 channel(额外源)。按持久 is_main 标记判定(与当前显示哪个源解耦)。单源设备不带此字段。 */
        if (strcmp(list[i].type, "multi") == 0)
            cJSON_AddNumberToObject(o, "mainSource", list[i].is_main ? 0 : 1);
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
    /* 设备代表通道:优先 is_main(首分配通道);旧库无 is_main → 退同 IP 最小通道号。 */
    nvr_channel_t *found = NULL;
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].onvif_ip, ip) != 0) continue;
        if (list[i].is_main) { found = &list[i]; break; }
        if (!found || list[i].chn < found->chn) found = &list[i];
    }

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
        } else if (found) {
            enh = found->enh_on ? 1 : 0;   /* ONVIF/其它:持久开关(on=标准 digest;off=无鉴权) */
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

/* 比对"免黑闪"用:清掉**运行期由发现/解析异步填充**的字段(非本函数所配置),避免它们造成假差异
 * 触发无谓重装。清的都是设备侧稍后各自补的:每路自解析的取流 URL、发现/getDeviceInfo 填的 mac/型号/
 * 固件/SN/service_url。**配置字段一律不清** → 新增结构配置字段自动参与比对,不会漏。 */
static void lan_src_mask_runtime(nvr_channel_t *e){
    e->url[0] = 0; e->url_main[0] = 0; e->url_sub[0] = 0;
    e->mac[0] = 0; e->model[0] = 0; e->firmware[0] = 0; e->serial[0] = 0;
    e->service_url[0] = 0;
}

/* 绑定"源 source"到通道 chn = **把当前物理设备(主/creds)整份内容"再添加一次"**,只把**源相关**
 * 字段换成本路的(chn/vout_win/dev_chn/video_source_token/type);额外源恒 LAN(poe=0)+默认录像。
 *   ✦ 用户模型:开启一路源 = NVR 用主设备内容再添加一次设备,源按指定;关闭一路源 = 删除该源通道
 *     (在上层 lan_bind_source_set 里 nvr_chan_remove,不走本函数)。
 *   ✦ **整份 *creds 拷贝**,绝不逐字段挑拷 → 不可能漏设备字段(历史坑:漏 onvif_ip→空 ip 黑、漏
 *     record→第二源不录)。结构新增字段自动随 *creds 带过来、并自动参与下面的免黑闪比对。
 *   ✦ 免黑闪:与现槽(忽略运行期 URL/发现字段)完全一致才跳过,避免每次展开都 remove+install 刷屏。 */
static void lan_bind_src(const nvr_cmd_ctx_t *c, int chn, int source,
                         char toks[][100], int ntok, const nvr_channel_t *creds, int is_main){
    if (!c || !c->cm || chn < 0 || !creds) return;
    const char *tok = (source - 1 >= 0 && source - 1 < ntok) ? toks[source - 1] : "";

    /* 1) 以主设备内容为模板整份继承,只改源相关字段 */
    nvr_channel_t want = *creds;
    want.chn      = chn;
    want.vout_win = chn;
    want.dev_chn  = source;
    want.is_main  = is_main ? 1 : 0;
    want.enabled  = 1;
    if (tok[0]) snprintf(want.video_source_token, sizeof(want.video_source_token), "%s", tok);
    snprintf(want.type, sizeof(want.type), "%s", ntok > 1 ? "multi" : "single");
    if (!is_main) { want.poe_port = 0; want.record = 1; }   /* 额外源:恒 LAN、默认录像 */
    want.url[0] = want.url_main[0] = want.url_sub[0] = 0;    /* 每路各自按本路 token 解析取流 URL */

    /* 2) 找现槽:保留用户可能改过的名字;没有则默认名 */
    nvr_channel_t list[NVR_MAX_CH]; int n = nvr_chan_list(c->cm, list, NVR_MAX_CH);
    const nvr_channel_t *cur = NULL;
    for (int i = 0; i < n; i++) if (list[i].chn == chn) { cur = &list[i]; break; }
    if (cur && cur->name[0]) snprintf(want.name, sizeof(want.name), "%s", cur->name);
    else                     snprintf(want.name, sizeof(want.name), "Camera %d", chn + 1);

    /* 3) 免黑闪:现槽与目标(掩掉运行期字段后)逐字节一致 → 不动 */
    if (cur) {
        nvr_channel_t a = want, b = *cur;
        lan_src_mask_runtime(&a); lan_src_mask_runtime(&b);
        if (memcmp(&a, &b, sizeof(a)) == 0) return;
    }
    nvr_chan_add(c->cm, &want);   /* = 用主设备内容"再添加一次",源用本路的(remove+install+持久化+起流) */
}

/* 等同 IP 各通道首次绑定(每通道上限 per_ch_ms)。★ 本函数**自身绝不碰 disp_lock**:
 * 它也被 attach_worker(detached 线程,不持锁)调用,在此 unlock 未持有的锁是 UB。放锁由
 * **前台 handler** 在调用前后自己做 CMD_UNBLOCK/REBLOCK(见 setLanDevice/LanAddDevice)。
 * per_ch_ms 小(前台)→ 提交快回;大(后台 attach)→ 尽量等出图。 */
static void lan_wait_same_ip(const nvr_cmd_ctx_t *c, const char *ip, int per_ch_ms){
    if (!c || !c->cm || !ip || !ip[0]) return;
    if (per_ch_ms < 0) per_ch_ms = 0;
    nvr_channel_t list[NVR_MAX_CH];
    int n = nvr_chan_list(c->cm, list, NVR_MAX_CH);
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].onvif_ip, ip) != 0) continue;
        nvr_chan_wait_bind(c->cm, list[i].chn, per_ch_ms);
        if (c->pv && nvr_chan_status_code_of(c->cm, list[i].chn) == 1)
            nvr_preview_wait_ready(c->pv, NVR_DEF_WAIT_READY_MS);
    }
}
/* 前台快等(提交快回 + 不饿死其它 8089 命令):放 disp_lock 外、每通道上限短。 */
#define LAN_WAIT_FG_MS 1200
static void lan_wait_same_ip_fg(const nvr_cmd_ctx_t *c, const char *ip){
    CMD_UNBLOCK(c);
    lan_wait_same_ip(c, ip, LAN_WAIT_FG_MS);
    CMD_REBLOCK(c);
}

/* videoSources[{source,enabled}] = 期望的"启用源集"(权威)。多源模型:
 *  · 主 channel(is_main=首分配通道;缺则退同 IP 最小通道号)**恒在、只 delDevice 删**;绑启用集第一个源。
 *  · 其余启用源各占一路:优先复用同 IP 的非主通道,不够再分配空闲 LAN 通道。
 *  · 超出启用数的非主通道 → 移除回收(compact)。启用集为空 → 只留主 channel(不动其源)。
 *  · 主 channel 若原是 PoE 口通道,保留其 poe_port;额外源恒 LAN。绝不移除主 channel。 */
/* 把"启用源集 en[0..nen)"落到同 IP 的通道上(setLanDevice / 默认展开共用):
 *  · 主 channel(is_main;缺退同 IP 最小通道号)恒在 ← en[0];
 *  · en[1..] 各占一路:先复用同 IP 非主通道,不够再分配空闲 LAN 通道;
 *  · 富余的非主通道 compact 回收;nen==0 → 只留主 channel。
 * toks/ntok 为已枚举的源 token(调用方负责用**已验证账密**枚举)。 */
static void lan_bind_source_set(const nvr_cmd_ctx_t *c, const char *ip,
                                const nvr_channel_t *tmpl,
                                const int *en, int nen,
                                char toks[][100], int ntok){
    if (!c || !c->cm || !ip || !tmpl) return;

    /* 找主 channel + 收集同 IP 非主通道(升序) */
    nvr_channel_t cur[NVR_MAX_CH];
    int m = nvr_chan_list(c->cm, cur, NVR_MAX_CH);
    int main_chn = -1, lowest = -1;
    for (int i = 0; i < m; i++) {
        if (strcmp(cur[i].onvif_ip, ip) != 0) continue;
        if (cur[i].is_main && main_chn < 0) main_chn = cur[i].chn;
        if (lowest < 0 || cur[i].chn < lowest) lowest = cur[i].chn;
    }
    if (main_chn < 0) main_chn = lowest;   /* 旧库无 is_main → 退最小通道号 */
    if (main_chn < 0) return;              /* 该 IP 无通道 */

    int extra[NVR_MAX_CH], nex = 0;
    for (int i = 0; i < m; i++)
        if (strcmp(cur[i].onvif_ip, ip) == 0 && cur[i].chn != main_chn && nex < NVR_MAX_CH)
            extra[nex++] = cur[i].chn;
    for (int a = 0; a < nex; a++) for (int b = a + 1; b < nex; b++)
        if (extra[b] < extra[a]) { int t = extra[a]; extra[a] = extra[b]; extra[b] = t; }

    /* 启用集为空 → 只留主 channel(不动其当前源),移除其余额外通道 */
    if (nen == 0) {
        for (int j = 0; j < nex; j++) nvr_chan_remove(c->cm, extra[j]);
        return;
    }

    /* 主 channel ← en[0];en[1..] ← 复用 extra[] 或分配新 LAN 通道 */
    lan_bind_src(c, main_chn, en[0], toks, ntok, tmpl, 1);
    int ux = 0;
    for (int i = 1; i < nen; i++) {
        int chn;
        if (ux < nex) chn = extra[ux++];
        else { chn = alloc_free_lan(c); if (chn < 0) continue; }
        lan_bind_src(c, chn, en[i], toks, ntok, tmpl, 0);
    }
    /* 富余的非主通道 → 移除回收(compact);主 channel 永不动 */
    while (ux < nex) nvr_chan_remove(c->cm, extra[ux++]);
}

static void lan_apply_video_sources(const nvr_cmd_ctx_t *c, const char *ip,
                                    const nvr_channel_t *tmpl, cJSON *vs){
    if (!c || !c->cm || !ip || !tmpl || !cJSON_IsArray(vs)) return;

    /* 1) 启用源集 E(升序去重) */
    int en[17], nen = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, vs) {
        int s = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(it, "source"));
        if (s < 1 || s > 16) continue;
        if (!lan_json_enabled(cJSON_GetObjectItem(it, "enabled"), 1)) continue;
        int dup = 0; for (int k = 0; k < nen; k++) if (en[k] == s) { dup = 1; break; }
        if (!dup && nen < 16) en[nen++] = s;
    }
    for (int a = 0; a < nen; a++) for (int b = a + 1; b < nen; b++)
        if (en[b] < en[a]) { int t = en[a]; en[a] = en[b]; en[b] = t; }

    /* 2) token 枚举(缓存;拿不到则空 token,lan_bind_src 按 dev_chn 兜底) */
    char toks[16][100]; memset(toks, 0, sizeof(toks));
    int ntok = lan_list_sources(ip, tmpl->onvif_port, tmpl->user, tmpl->pass, toks, 16);

    lan_bind_source_set(c, ip, tmpl, en, nen, toks, ntok);
}

/* 默认多源展开:主源验密出图后,用**已验证账密**枚举源;多源(ntok>1)则把源 1..ntok
 * 全部落地出图(主=源1,其余各占一路)。单源(ntok<=1)不动。返回 ntok(0=枚举失败,可重试)。
 *  · 只在主通道 status==1(=账密已验、已写回 slot)后调用 → list_sources 用的是验过的密码。
 *  · 复用主通道在线的 ONVIF 连接(pool 按 ip:port),只发 GetVideoSources,不重连。
 *  · 各源能力集不在此收集,由通道 online 后 tick 各自后台补(见 nvr_channel.c caps_probed)。 */
static int lan_auto_expand_sources(const nvr_cmd_ctx_t *c, const char *ip){
    if (!c || !c->cm || !ip || !ip[0]) return 0;
    /* 取在线主通道(is_main;缺退首个同 IP)的已验证 creds 作模板 */
    nvr_channel_t cur[NVR_MAX_CH];
    int m = nvr_chan_list(c->cm, cur, NVR_MAX_CH);
    nvr_channel_t tmpl; int found = 0;
    for (int i = 0; i < m; i++) {
        if (strcmp(cur[i].onvif_ip, ip) != 0) continue;
        if (cur[i].is_main) { tmpl = cur[i]; found = 1; break; }
        if (!found) { tmpl = cur[i]; found = 1; }
    }
    if (!found) return 0;

    char toks[16][100]; memset(toks, 0, sizeof(toks));
    int ntok = lan_list_sources(ip, tmpl.onvif_port, tmpl.user, tmpl.pass, toks, 16);
    if (ntok <= 1) return ntok;   /* 单源/枚举失败:不展开(0 供调用方重试) */

    int en[17], nen = 0;
    for (int s = 1; s <= ntok && nen < 16; s++) en[nen++] = s;
    NVR_LOGI("lan", "%s 默认展开多源: ntok=%d → 源 1..%d 全部出图", ip, ntok, ntok);
    lan_bind_source_set(c, ip, &tmpl, en, nen, toks, ntok);
    return ntok;
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
    /* 模板/代表通道:优先 is_main(首分配通道);旧库无 is_main → 退同 IP 最小通道号。 */
    nvr_channel_t d; int found = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].onvif_ip, ip) != 0) continue;
        if (list[i].is_main) { d = list[i]; found = 1; break; }
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
        d.enh_on = enh ? 1 : 0;
        NVR_LOGI("lan", "setLanDevice ip=%s enhancedSecurity=%d pass=%s",
                 ip, enh, enh ? "P_enh" : "(empty)");
    } else {
        if (usr && usr[0]) snprintf(d.user, sizeof(d.user), "%s", usr);
        else               snprintf(d.user, sizeof(d.user), "admin");
        /* ★ 普通 ONVIF:**用户填了密码就一律用它**(标准 digest),enhancedSecurity 不再吞掉密码——
         *   之前 `enh ? (pw?pw:"") : ""` 在 enh 关时把用户输入的密码清空 → 普通设备配了密码也连不上
         *   (真机 ch12:配密码没生效、反复重发现)。仅当**没填密码**才空(=无鉴权连接)。enh_on 另行记录。 */
        snprintf(d.pass, sizeof(d.pass), "%s", pw ? pw : "");
        d.enh_on = enh ? 1 : 0;
        NVR_LOGI("lan", "setLanDevice ip=%s onvif user=%s pass=%s enh=%d protocol=%s",
                 ip, d.user, d.pass[0] ? "(set)" : "(empty)", enh, protocol ? protocol : "(null)");
    }

    cJSON *vs = cJSON_GetObjectItem(a, "videoSources");
    if (cJSON_IsArray(vs)) {
        lan_apply_video_sources(c, ip, &d, vs);
    } else {
        nvr_channel_t cur[NVR_MAX_CH];
        int m = nvr_chan_list(c->cm, cur, NVR_MAX_CH);
        for (int i = 0; i < m; i++) {
            if (strcmp(cur[i].onvif_ip, ip) != 0) continue;
            /* ★ setLanDevice 权威:**无条件落库**账密/enh/端口。不再用"内存已一致就跳过写库"的门控——
             * resolve 连上后会把工作凭据写回内存却没落库,DB 与内存脱节,老门控据此漏写(真机 ch19:
             * 密码设了进不了库)。nvr_chan_set_creds:改内存值 + 无条件落库;凭据真变才清 url 重解析,
             * 不整屏 remove/install(免黑闪)。 */
            nvr_chan_set_creds(c->cm, cur[i].chn, d.user, d.pass, d.enh_random, d.enh_on, d.onvif_port);
        }
    }
    lan_wait_same_ip_fg(c, ip);
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
/* ── 多源"首次自动展开"一次性标记 ───────────────────────────────────────────
 * 需求:加设备时首次把多源全铺开出图(用户要的),之后**完全听开关**(setLanDevice
 * videoSources)——关掉的源不被"主源每次上线"重新强开(否则关不掉、还多占解码窗把别路挤黑,
 * 真机 ch10:第二源 ch17 占窗 → 4K 的 ch9 被挤出 16 路解码上限 → 黑)。
 * 做法:LanAddDevice/attach 落库后按 IP 置"待展开",主源 ONLINE 事件**消费一次即清** → 只展开
 * 一次;重启由 load_config 恢复上次留下的通道集,主源上线时标记已无 → 不再强铺。 */
#define LAN_EXPAND_PENDING_MAX 32
static struct { char ip[64]; } g_expand_pending[LAN_EXPAND_PENDING_MAX];
static pthread_mutex_t g_expand_pending_mu = PTHREAD_MUTEX_INITIALIZER;
static void lan_expand_pending_mark(const char *ip){
    if (!ip || !ip[0]) return;
    pthread_mutex_lock(&g_expand_pending_mu);
    int free_i = -1;
    for (int i = 0; i < LAN_EXPAND_PENDING_MAX; i++){
        if (strcmp(g_expand_pending[i].ip, ip) == 0) { pthread_mutex_unlock(&g_expand_pending_mu); return; }
        if (free_i < 0 && !g_expand_pending[i].ip[0]) free_i = i;
    }
    if (free_i >= 0) snprintf(g_expand_pending[free_i].ip, sizeof(g_expand_pending[free_i].ip), "%s", ip);
    pthread_mutex_unlock(&g_expand_pending_mu);
}
static int lan_expand_pending_take(const char *ip){  /* 命中即清,返回 1;否则 0 */
    if (!ip || !ip[0]) return 0;
    int took = 0;
    pthread_mutex_lock(&g_expand_pending_mu);
    for (int i = 0; i < LAN_EXPAND_PENDING_MAX; i++)
        if (strcmp(g_expand_pending[i].ip, ip) == 0) { g_expand_pending[i].ip[0] = 0; took = 1; break; }
    pthread_mutex_unlock(&g_expand_pending_mu);
    return took;
}

/* 默认多源展开(LanAddDevice 后台):等主源在线(已验密)→ 枚举源 → 其余源自动出图。
 * 派 detached 线程,handler 秒回;ctx 长生命(与 attach_worker 一致按指针持有)。 */
typedef struct { const nvr_cmd_ctx_t *c; char ip[64]; } expand_work_t;
static void *lan_expand_worker(void *arg){
    expand_work_t *w = (expand_work_t *)arg;
    if (!w || !w->c || !w->c->cm) { free(w); return NULL; }
    lan_wait_same_ip(w->c, w->ip, NVR_DEF_CMD_TIMEOUT_S * 1000);   /* 等主源验密出图 */
    /* 主通道(is_main;缺退首个同 IP)在线(status==1=账密已验)才展开 */
    nvr_channel_t list[NVR_MAX_CH];
    int n = nvr_chan_list(w->c->cm, list, NVR_MAX_CH);
    int main_chn = -1, lowest = -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].onvif_ip, w->ip) != 0) continue;
        if (list[i].is_main && main_chn < 0) main_chn = list[i].chn;
        if (lowest < 0 || list[i].chn < lowest) lowest = list[i].chn;
    }
    if (main_chn < 0) main_chn = lowest;
    if (main_chn >= 0 && nvr_chan_status_code_of(w->c->cm, main_chn) == 1 &&
        lan_auto_expand_sources(w->c, w->ip) > 1)
        lan_wait_same_ip(w->c, w->ip, NVR_DEF_CMD_TIMEOUT_S * 1000);   /* 等新源出图 */
    free(w);
    return NULL;
}
static void lan_spawn_expand(const nvr_cmd_ctx_t *c, const char *ip){
    if (!c || !ip || !ip[0]) return;
    expand_work_t *w = (expand_work_t *)calloc(1, sizeof(*w));
    if (!w) return;
    w->c = c; snprintf(w->ip, sizeof(w->ip), "%s", ip);
    pthread_t th;
    if (pthread_create(&th, NULL, lan_expand_worker, w) == 0) pthread_detach(th);
    else free(w);
}

/* ★ 事件驱动的默认多源展开:代表通道(主源)**真正出图(ONLINE)后**由 app 的 on_online 事件调用。
 * 修根因——原来只有 LanAddDevice/attach 添加后**一次性**等 8s(NVR_DEF_CMD_TIMEOUT_S)就检查
 * status==1,而 nopOnvif 相机激活+解析常需数十秒~数分钟才出图,那一次检查必然失败 → 展开被永久跳过,
 * 第二路源永不分配。改由「主源出图」这个边沿事件触发:此刻 status 必为 1,门控稳过;掉线再上线会
 * 重新 fire(edge:notified_online),故枚举瞬时失败也能在下次重连自愈。lan_expand_worker 幂等
 * (lan_bind_source_set 无变化不重装),重复触发安全。**不碰 disp_lock**(worker 是 detached 线程)。 */
void nvr_lan_expand_sources(const nvr_cmd_ctx_t *c, const char *ip){
    /* ★ 只在"添加时置了待展开标记"的那一次展开;之后主源再上线(重连/重启)不再强铺 → 听开关。 */
    if (!lan_expand_pending_take(ip)) return;
    lan_spawn_expand(c, ip);
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
     * GUI protocol=="nop" 才是 NOP；其余先当通用 ONVIF。用户密码原样保存(空=空密;123456=真实口令)。 */
    if (!strcmp(protocol, "nop")) {
        kind = NVR_DEV_KIND_NOP;
    } else {
        kind = NVR_DEV_KIND_ONVIF;
        if (argpass && argpass[0])
            snprintf(pass, sizeof(pass), "%s", argpass);
    }

    nvr_channel_t d; memset(&d, 0, sizeof(d));
    d.chn = chn; d.enabled = 1; d.record = 1; d.dev_chn = 1; d.is_main = 1;  /* 首分配通道=主 channel */
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
        /* ★ 置"首次自动展开"标记(主源上线事件消费一次)。之后多源全听开关,不再强铺。
         *   须在等待/上线之前置好,避免主源上线事件早于标记。 */
        lan_expand_pending_mark(ip);
        lan_wait_same_ip_fg(c, ip);
    }
    return nvr_resp_result("OK");
}

/* ------------------------- GUI_LanDelDevice ------------------------- */
/* 删物理台:同 IP 多源 channel 一并删除(不控制相机)。
 * ★ 后台执行、立即返回:删除内含停流(可能阻塞在 RTSP puller 网络 I/O 到超时)+ ONVIF 断连
 * (设备不可达时秒级)+ 多源逐路 → 同步做会卡住派发线程/UI 数秒。改为 detached 线程做真删除
 * (lan_remove_device 用 CM_LOCK 自串行、不碰 disp_lock;attach_worker 已证明可后台调),handler 秒回。
 * GUI 靠 longPolling ChannelStatusNotify 收到删除后重拉清单。 */
typedef struct { nvr_chan_mgr_t *cm; int chn; } lan_del_work_t;
static void *lan_del_worker(void *arg){
    lan_del_work_t *w = (lan_del_work_t *)arg;
    if (w && w->cm) lan_remove_device(w->cm, w->chn);
    free(w);
    return NULL;
}
char *cmd_GUI_LanDelDevice(cJSON *a, const nvr_cmd_ctx_t *c){
    if (!nvr_jhas(a, "channel")) return nvr_resp_err("invalid_param");
    int ch = nvr_jint(a, "channel", 0);
    if (c->cm && ch > 0) {
        lan_del_work_t *w = (lan_del_work_t *)calloc(1, sizeof(*w));
        pthread_t th;
        if (w) {
            w->cm = c->cm; w->chn = ch - 1;
            if (pthread_create(&th, NULL, lan_del_worker, w) == 0) pthread_detach(th);
            else { lan_remove_device(c->cm, ch - 1); free(w); }   /* 起线程失败 → 同步兜底 */
        } else {
            lan_remove_device(c->cm, ch - 1);
        }
    }
    return nvr_resp_ok();
}

/* ------------------------- App LAN attach (无线相机:App 告知 IP/MAC,NVR 占 LAN 槽) ------------------------- */
#define ATTACH_JOB_MAX 16
typedef struct {
    char mac[24];
    char status[16];
    char message[80];
} attach_job_t;

static pthread_mutex_t g_att_mu = PTHREAD_MUTEX_INITIALIZER;
static attach_job_t g_att[ATTACH_JOB_MAX];
static int g_att_n;
static int g_att_nofree;

static void mac_norm(const char *in, char *out, int cap)
{
    int n = 0, i;
    if (!out || cap < 2) return;
    out[0] = 0;
    if (!in) return;
    for (i = 0; in[i] && n < cap - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == ':' || c == '-' || c == ' ') continue;
        out[n++] = (char)tolower(c);
    }
    out[n] = 0;
    if (n == 12 && cap >= 18) {
        char tmp[18];
        snprintf(tmp, sizeof(tmp), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                 out[0], out[1], out[2], out[3], out[4], out[5],
                 out[6], out[7], out[8], out[9], out[10], out[11]);
        snprintf(out, (size_t)cap, "%s", tmp);
    }
}

static int mac_eq(const char *a, const char *b)
{
    char na[24], nb[24];
    mac_norm(a, na, (int)sizeof(na));
    mac_norm(b, nb, (int)sizeof(nb));
    return na[0] && strcmp(na, nb) == 0;
}

static void att_upsert(const char *mac, const char *st, const char *msg)
{
    char nm[24];
    int i;
    mac_norm(mac, nm, (int)sizeof(nm));
    pthread_mutex_lock(&g_att_mu);
    for (i = 0; i < g_att_n; i++) {
        if (!mac_eq(g_att[i].mac, nm)) continue;
        snprintf(g_att[i].status, sizeof(g_att[i].status), "%s", st ? st : "");
        snprintf(g_att[i].message, sizeof(g_att[i].message), "%s", msg ? msg : "");
        pthread_mutex_unlock(&g_att_mu);
        return;
    }
    if (g_att_n >= ATTACH_JOB_MAX) {
        memmove(&g_att[0], &g_att[1], sizeof(g_att[0]) * (ATTACH_JOB_MAX - 1));
        g_att_n = ATTACH_JOB_MAX - 1;
    }
    snprintf(g_att[g_att_n].mac, sizeof(g_att[g_att_n].mac), "%s", nm);
    snprintf(g_att[g_att_n].status, sizeof(g_att[g_att_n].status), "%s", st ? st : "");
    snprintf(g_att[g_att_n].message, sizeof(g_att[g_att_n].message), "%s", msg ? msg : "");
    g_att_n++;
    pthread_mutex_unlock(&g_att_mu);
}

static size_t attach_curl_nop(void *p, size_t sz, size_t n, void *u)
{
    (void)p; (void)u;
    return sz * n;
}

/* POST /AttachToMaster。返 HTTP 状态码;失败 0。 */
static long attach_to_master(const char *ip, int port, const char *body)
{
    CURL *c;
    char url[160];
    struct curl_slist *hdr;
    long code = 0;
    if (!ip || !ip[0] || port <= 0 || !body) return 0;
    c = curl_easy_init();
    if (!c) return 0;
    snprintf(url, sizeof(url), "http://%s:%d/AttachToMaster", ip, port);
    hdr = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, attach_curl_nop);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FORBID_REUSE, 1L);
    if (curl_easy_perform(c) == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return code;
}

static int attach_merge(const char *ip, int *port_io, nvr_settings_t *st)
{
    nvr_owner_row_t ow;
    nvr_local_link_t lk;
    char mac[32] = "", body[512];
    int ports[2];
    int np = 0, i;
    long code;
    memset(&ow, 0, sizeof(ow));
    memset(&lk, 0, sizeof(lk));
    if (st) (void)nvr_settings_owner_get(st, &ow);
    nvr_identity_get_mac("eth0", mac, sizeof(mac));
    if (st) (void)nvr_net_local_link_fill(st, &lk);
    if (!lk.ip[0]) snprintf(lk.ip, sizeof(lk.ip), "%s", "0.0.0.0");
    ports[np++] = 8089;
    if (port_io && *port_io > 0 && *port_io != 8089) ports[np++] = *port_io;
    else ports[np++] = 80;

    snprintf(body, sizeof(body),
             "{\"action\":\"precheck\",\"ownerId\":\"%s\",\"masterType\":\"videoRecorder\"}",
             ow.owner_id);
    for (i = 0; i < np; i++) {
        code = attach_to_master(ip, ports[i], body);
        if (code == 200) break;
    }
    if (i >= np) return -1;   /* 相机不支持 / 不可达:调用方仍走 LAN Add */
    if (port_io) *port_io = ports[i];

    snprintf(body, sizeof(body),
             "{\"action\":\"startMerge\",\"ownerId\":\"%s\",\"masterType\":\"videoRecorder\","
             "\"masterIP\":\"%s\",\"masterMac\":\"%s\"}",
             ow.owner_id, lk.ip, mac);
    code = attach_to_master(ip, ports[i], body);
    if (code == 200 || code == 403) return 0;   /* 403=已是从机 */
    if (code == 401) return -2;                 /* owner 不一致 */
    return -1;
}

typedef struct {
    const nvr_cmd_ctx_t *c;
    char ip[64];
    char mac[24];
    int  chn;
    int  port;
    int  has_battery;
} attach_work_t;

static void *attach_worker(void *arg)
{
    attach_work_t *w = (attach_work_t *)arg;
    int merge;
    nvr_channel_t d;
    if (!w || !w->c || !w->c->cm) { free(w); return NULL; }
    att_upsert(w->mac, "attaching", "");
    merge = attach_merge(w->ip, &w->port, w->c->settings);
    if (merge == -2) {
        lan_remove_device(w->c->cm, w->chn);
        att_upsert(w->mac, "error", "ownerId not the same");
        free(w); return NULL;
    }
    if (w->has_battery)
        NVR_LOGI("lan", "attach %s hasBattery:按 LAN 加机(无 KIT2 保活)", w->ip);
    if (w->port > 0 && nvr_chan_get(w->c->cm, w->chn, &d) == 0 && d.onvif_port != w->port) {
        d.onvif_port = w->port;
        d.url[0] = 0;
        nvr_chan_add(w->c->cm, &d);
    }
    lan_wait_same_ip(w->c, w->ip, NVR_DEF_CMD_TIMEOUT_S * 1000);  /* 后台 attach:等足,不持锁 */
    /* 首次自动展开走"待展开标记 + 主源 ONLINE 事件"(标记已在 attachIPDevices 落库后置),此处只记状态。 */
    att_upsert(w->mac, nvr_chan_status_code_of(w->c->cm, w->chn) == 1 ? "attached" : "attaching", "");
    free(w);
    return NULL;
}

char *cmd_X_NightOwl_attachIPDevices(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *devs = a ? cJSON_GetObjectItem(a, "devices") : NULL;
    cJSON *it;
    int started = 0;
    if (!cJSON_IsArray(devs) || cJSON_GetArraySize(devs) <= 0)
        return nvr_resp_err("invalid_param");
    pthread_mutex_lock(&g_att_mu); g_att_nofree = 0; pthread_mutex_unlock(&g_att_mu);

    cJSON_ArrayForEach(it, devs) {
        attach_work_t *w;
        pthread_t th;
        nvr_channel_t d;
        const char *ip, *mac, *model, *acct, *pw;
        int poe = 0, chn;
        char nmac[24];
        if (!cJSON_IsObject(it)) continue;
        ip = nvr_jstr(it, "ip", NULL);
        mac = nvr_jstr(it, "mac", NULL);
        if (!ip || !ip[0] || !mac || !mac[0]) continue;
        mac_norm(mac, nmac, (int)sizeof(nmac));
        chn = assign_channel(c, NULL, ip, &poe);
        if (chn < 0) {
            att_upsert(nmac, "error", "NoFreeChannel");
            pthread_mutex_lock(&g_att_mu); g_att_nofree = 1; pthread_mutex_unlock(&g_att_mu);
            continue;
        }
        model = nvr_jstr(it, "model", "");
        acct = nvr_jstr(it, "account", "admin");
        pw = nvr_jstr(it, "password", "");
        memset(&d, 0, sizeof(d));
        d.chn = chn; d.enabled = 1; d.record = 1; d.dev_chn = 1; d.is_main = 1;  /* 首分配通道=主 channel */
        snprintf(d.type, sizeof(d.type), "single");
        d.poe_port = poe;
        d.onvif_auto = 1;
        d.onvif_port = 80;
        d.stream = NVR_STREAM_MAIN; d.codec = NVR_CODEC_AUTO; d.vout_win = chn;
        d.kind = NVR_DEV_KIND_ONVIF;
        d.backend = (int)nvr_dev_backend_of(NVR_DEV_KIND_ONVIF);
        snprintf(d.onvif_ip, sizeof(d.onvif_ip), "%s", ip);
        snprintf(d.mac, sizeof(d.mac), "%s", nmac);
        snprintf(d.model, sizeof(d.model), "%s", model ? model : "");
        snprintf(d.user, sizeof(d.user), "%s", acct && acct[0] ? acct : "admin");
        if (pw && pw[0])
            snprintf(d.pass, sizeof(d.pass), "%s", pw);
        snprintf(d.name, sizeof(d.name), "Camera %d", chn + 1);
        if (nvr_chan_add(c->cm, &d) < 0) {
            att_upsert(nmac, "error", "can't find the device");
            continue;
        }
        lan_expand_pending_mark(ip);   /* 首次自动展开一次性标记;主源上线事件消费 */
        w = (attach_work_t *)calloc(1, sizeof(*w));
        if (!w) continue;
        w->c = c;
        snprintf(w->ip, sizeof(w->ip), "%s", ip);
        snprintf(w->mac, sizeof(w->mac), "%s", nmac);
        w->chn = chn;
        w->port = 0;
        w->has_battery = nvr_jbool(it, "hasBattery", 0);
        att_upsert(nmac, "attaching", "");
        if (pthread_create(&th, NULL, attach_worker, w) != 0) {
            att_upsert(nmac, "error", "can't find the device");
            free(w);
            continue;
        }
        pthread_detach(th);
        started++;
    }
    if (!started) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "error", "NoFreeChannel");
        return nvr_resp_content(o);
    }
    return nvr_resp_ok();
}

char *cmd_X_NightOwl_getAttachStatus(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    cJSON *o = cJSON_CreateObject();
    cJSON *arr;
    int i, nofree;
    pthread_mutex_lock(&g_att_mu);
    nofree = g_att_nofree;
    if (nofree && g_att_n == 0) {
        pthread_mutex_unlock(&g_att_mu);
        cJSON_AddStringToObject(o, "error", "NoFreeChannel");
        return nvr_resp_content(o);
    }
    arr = cJSON_AddArrayToObject(o, "devices");
    for (i = 0; i < g_att_n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "mac", g_att[i].mac);
        cJSON_AddStringToObject(e, "status", g_att[i].status);
        if (g_att[i].message[0])
            cJSON_AddStringToObject(e, "message", g_att[i].message);
        cJSON_AddItemToArray(arr, e);
    }
    pthread_mutex_unlock(&g_att_mu);
    return nvr_resp_content(o);
}

char *cmd_X_NightOwl_detachIPDevice(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *dev = a ? cJSON_GetObjectItem(a, "device") : NULL;
    const char *mac, *ip;
    nvr_channel_t list[NVR_MAX_CH];
    int n, i, chn = -1;
    if (!cJSON_IsObject(dev) || !c->cm) return nvr_resp_err("invalid_param");
    mac = nvr_jstr(dev, "mac", "");
    ip = nvr_jstr(dev, "ip", "");
    if (mac && mac[0]) att_upsert(mac, "detaching", "");
    n = nvr_chan_list(c->cm, list, NVR_MAX_CH);
    for (i = 0; i < n; i++) {
        if (mac && mac[0] && mac_eq(list[i].mac, mac)) { chn = list[i].chn; break; }
    }
    if (chn < 0 && ip && ip[0]) {
        for (i = 0; i < n; i++)
            if (list[i].onvif_ip[0] && strcmp(list[i].onvif_ip, ip) == 0) {
                chn = list[i].chn; break;
            }
    }
    if (chn >= 0) lan_remove_device(c->cm, chn);
    if (mac && mac[0]) att_upsert(mac, "detached", "");
    return nvr_resp_ok();
}
