/***************************************************************************************
 *  nvr_config.c — JSON 配置加载 + channels.json 扁平化（device→source→channel）
 ***************************************************************************************/
#include "nvr_config.h"
#include "nvr_defaults.h"   /* PoE IP 模板宏 NVR_POE_CAM_IP_PAT */
#include "nvr_identity.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 小工具 ---------- */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = 0;
    return buf;
}

static cJSON *load_json(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    char *txt = read_file(path);
    if (!txt) return NULL;
    cJSON *j = cJSON_Parse(txt);
    free(txt);
    return j;
}

static const char *jstr(const cJSON *o, const char *k, const char *def)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return (v && cJSON_IsString(v)) ? v->valuestring : def;
}
static int jint(const cJSON *o, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return (v && cJSON_IsNumber(v)) ? v->valueint : def;
}
static int jbool(const cJSON *o, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    if (!v) return def;
    if (cJSON_IsBool(v))   return cJSON_IsTrue(v) ? 1 : 0;
    if (cJSON_IsNumber(v)) return v->valueint ? 1 : 0;
    return def;
}

static int codec_of(const char *s)
{
    if (!s) return NVR_CODEC_AUTO;
    if (!strcmp(s, "h264")) return NVR_CODEC_H264;
    if (!strcmp(s, "h265")) return NVR_CODEC_H265;
    return NVR_CODEC_AUTO;
}
static int stream_of(const char *s)
{
    return (s && !strcmp(s, "sub")) ? NVR_STREAM_SUB : NVR_STREAM_MAIN;
}

/* ---------- system.json ---------- */
static void load_system(nvr_config_t *c, cJSON *j)
{
    nvr_sys_cfg_t *s = &c->sys;
    s->capacity = 32; s->poe_ports = 16; s->ip_channels = 16;
    s->hdmi_w = NVR_DEF_HDMI_W; s->hdmi_h = NVR_DEF_HDMI_H; s->default_layout = NVR_DEF_LAYOUT;  /* 默认 1080p(实际按屏,4K可切,失败回落) */
    if (!j) return;

    cJSON *dev = cJSON_GetObjectItem(j, "device");
    if (dev) {
        snprintf(s->model, sizeof(s->model), "%s", jstr(dev, "model", NVR_DEF_MODEL));
        snprintf(s->name,  sizeof(s->name),  "%s", jstr(dev, "name",  NVR_DEF_NAME));
        snprintf(s->type,  sizeof(s->type),  "%s", jstr(dev, "type",  NVR_DEF_DEVICE_TYPE));
    }
    /* SN 来自数据分区 /User/OWLSerialNumber(恒定),非 JSON;供磁盘加密派生 device_sn。 */
    nvr_identity_get_sn(s->sn, sizeof(s->sn));
    cJSON *ch = cJSON_GetObjectItem(j, "channels");
    if (ch) {
        s->capacity     = jint(ch, "capacity", NVR_DEF_CAPACITY);
        s->poe_ports    = jint(ch, "poe_ports", 16);
        s->ip_channels  = jint(ch, "ip_channels", 16);
    }
    cJSON *vo = cJSON_GetObjectItem(j, "video_out");
    if (vo) {
        s->default_layout = jint(vo, "default_layout", 16);
        cJSON *hdmi = cJSON_GetObjectItem(vo, "hdmi");
        const char *res = hdmi ? jstr(hdmi, "resolution", "3840x2160") : "3840x2160";
        sscanf(res, "%dx%d", &s->hdmi_w, &s->hdmi_h);
    }
}

/* ---------- storage.json ---------- */
static void load_storage(nvr_config_t *c, cJSON *j)
{
    nvr_storage_cfg_t *st = &c->storage;
    memset(st, 0, sizeof(*st));
    st->device_sn      = c->sys.sn[0] ? c->sys.sn : NULL;
    st->want_encryption = 1;
    st->hdd_full        = RSDK_HDDFULL_OVERWRITE;
    st->want_metadata   = 1;
    if (!j) return;

    cJSON *enc = cJSON_GetObjectItem(j, "encryption");
    if (enc) st->want_encryption = jbool(enc, "enable", 1);
    const char *full = jstr(j, "hdd_full", "overwrite");
    st->hdd_full = (full && !strcmp(full, "stop")) ? RSDK_HDDFULL_STOP : RSDK_HDDFULL_OVERWRITE;
    cJSON *meta = cJSON_GetObjectItem(j, "metadata");
    if (meta) st->want_metadata = jbool(meta, "enable", 1);
}

/* ---------- streaming.json ---------- */
static void load_streaming(nvr_config_t *c, cJSON *j)
{
    nvr_stream_mgr_cfg_t *m = &c->stream;
    memset(m, 0, sizeof(*m));
    m->conn_timeout = 5; m->rx_timeout = 10;
    if (!j) return;
    cJSON *tr = cJSON_GetObjectItem(j, "transport");
    if (tr) {
        m->conn_timeout = jint(tr, "conn_timeout_s", 5);
        m->rx_timeout   = jint(tr, "rx_timeout_s", 10);
    }
}

/* ---------- channels.json 扁平化 ---------- */
typedef struct {
    char ip_pattern[64], user[64], pass[64], rec_stream[16], prev_stream[16];
    int  codec, record, over_tcp, ch_base;
} ch_defaults_t;

static void ipfmt(char *dst, size_t n, const char *pat, int port)
{
    /* pat 形如 "198.18.%d.100" */
    if (pat && strstr(pat, "%d")) snprintf(dst, n, pat, port);
    else if (pat) snprintf(dst, n, "%s", pat);
    else dst[0] = 0;
}

/* 追加一个通道（带边界/去重检查） */
static nvr_channel_t *emit(nvr_config_t *c, int chn)
{
    if (chn < 0 || c->nch >= NVR_CFG_MAX_CH) return NULL;
    for (int i = 0; i < c->nch; i++)
        if (c->ch[i].chn == chn) return &c->ch[i];   /* 已存在则覆盖(如 main 覆盖占位) */
    nvr_channel_t *e = &c->ch[c->nch++];
    memset(e, 0, sizeof(*e));
    e->chn = chn; e->vout_win = chn; e->codec = NVR_CODEC_AUTO; e->stream = NVR_STREAM_MAIN;
    return e;
}

static void fill_poe_channel(nvr_config_t *c, const ch_defaults_t *d, int port, cJSON *onvif)
{
    int chn = d->ch_base + (port - 1);
    nvr_channel_t *e = emit(c, chn);
    if (!e) return;
    e->enabled = 1;                  /* ★ 配置加载的通道即为启用 —— 否则 nvr_chan_load_config
                                      * 见 enabled=0 会标 DISABLED、不做 ONVIF 解析(之前靠 DB
                                      * overlay 补 enabled,清了 DB 就全失效、自动发现停摆)。 */
    snprintf(e->name, sizeof(e->name), "Camera %d", port);
    snprintf(e->user, sizeof(e->user), "%s", d->user);
    snprintf(e->pass, sizeof(e->pass), "%s", d->pass);
    /* ★ PoE 口↔网段 1:1:口 P → 段 P(198.18.P.x)。交换芯片把口 P tag 到 VLAN(2000+P),
     * NVR 侧 eth1.(2000+P) 取 IP 198.18.P.100(见 nvr_netime + NVR_DEF_VLAN_BASE=2000,对齐 ODC),
     * 相机固定 198.18.P.1。故 onvif_ip 段号 = 口号(与 sources 路径一致,无偏移)。 */
    ipfmt(e->onvif_ip, sizeof(e->onvif_ip), d->ip_pattern, port);
    e->onvif_port = 80;
    e->onvif_auto = onvif ? jbool(onvif, "auto", 1) : 1;   /* PoE 默认 ONVIF 自动取流 */
    e->codec  = d->codec;
    e->record = d->record;
    e->stream = stream_of(d->prev_stream);   /* 默认子码流(多宫格预览) */
    e->poe_port = port;
    e->url[0] = 0;   /* 待 ONVIF 填 */
}

static void load_channels(nvr_config_t *c, cJSON *j)
{
    c->nch = 0;
    if (!j) return;

    ch_defaults_t d = { NVR_POE_CAM_IP_PAT, "admin", "", "main", "sub", NVR_CODEC_AUTO, 1, 1, 0 };  /* 原厂 PoE：相机 .1，NVR .100 */
    cJSON *df = cJSON_GetObjectItem(j, "defaults");
    if (df) {
        snprintf(d.ip_pattern, sizeof(d.ip_pattern), "%s", jstr(df, "poe_ip_pattern", d.ip_pattern));
        snprintf(d.user, sizeof(d.user), "%s", jstr(df, "poe_user", "admin"));
        snprintf(d.pass, sizeof(d.pass), "%s", jstr(df, "poe_pass", ""));
        snprintf(d.rec_stream,  sizeof(d.rec_stream),  "%s", jstr(df, "record_stream", "main"));
        snprintf(d.prev_stream, sizeof(d.prev_stream), "%s", jstr(df, "preview_stream", "sub"));
        d.codec    = codec_of(jstr(df, "codec", "auto"));
        d.record   = jbool(df, "record", 1);
        d.over_tcp = jbool(df, "over_tcp", 1);
        d.ch_base  = jint(df, "poe_channel_base", 0);
    }

    cJSON *devs = cJSON_GetObjectItem(j, "devices");
    cJSON *dev;
    if (cJSON_IsArray(devs))          /* 无 devices 也不早退——下方仍要补齐 16 个 PoE 口 */
    cJSON_ArrayForEach(dev, devs) {
        if (!jbool(dev, "enable", 1)) continue;
        const char *type = jstr(dev, "type", "ip");
        /* ★ 只从配置自动加载 PoE 口设备。LAN/IP(数字)相机一律**不**从配置预置自动连接
         * —— 那会在 eth0 主网上对配置里写死的 IP 反复探测/连不存在的设备，扰乱局域网
         * (影响 NFS 及网内其他设备)。业务规则:PoE 口即插即用自动出图;LAN 设备只能运行期
         * 经 GUI_LanAddDevice 显式添加、且只连已添加的(见 nvr_cmd_lan.c / channel FSM)。 */
        if (strcmp(type, "poe") != 0) continue;
        cJSON *onvif = cJSON_GetObjectItem(dev, "onvif");
        cJSON *conn  = cJSON_GetObjectItem(dev, "conn");

        /* PoE 简写：expand_ports:[3,4,...] 批量展开同构口 */
        cJSON *expand = cJSON_GetObjectItem(dev, "expand_ports");
        if (!strcmp(type, "poe") && cJSON_IsArray(expand)) {
            cJSON *p;
            cJSON_ArrayForEach(p, expand)
                if (cJSON_IsNumber(p)) fill_poe_channel(c, &d, p->valueint, onvif);
            continue;
        }

        cJSON *sources = cJSON_GetObjectItem(dev, "sources");

        /* PoE 且无 sources：单源，通道 = 口-1 */
        if (!strcmp(type, "poe") && !cJSON_IsArray(sources)) {
            int port = jint(dev, "poe_port", 0);
            if (port > 0) fill_poe_channel(c, &d, port, onvif);
            continue;
        }
        if (!cJSON_IsArray(sources)) continue;

        /* 设备连接信息（ip/user/pass）：onvif 优先，其次 conn，其次 PoE 默认 */
        const char *dev_ip   = onvif ? jstr(onvif, "ip", "") : "";
        int         dev_port = onvif ? jint(onvif, "port", NVR_DEF_ONVIF_PORT) : NVR_DEF_ONVIF_PORT;
        const char *dev_user = onvif ? jstr(onvif, "user", NULL) : NULL;
        const char *dev_pass = onvif ? jstr(onvif, "pass", NULL) : NULL;
        if (!dev_user && conn) dev_user = jstr(conn, "user", NULL);
        if (!dev_pass && conn) dev_pass = jstr(conn, "pass", NULL);
        int dev_onvif_auto = onvif ? jbool(onvif, "auto", 0) : 0;
        int poe_port = !strcmp(type, "poe") ? jint(dev, "poe_port", 0) : 0;

        cJSON *src;
        cJSON_ArrayForEach(src, sources) {
            const char *role = jstr(src, "role", "");
            if (!strcmp(role, "preview")) continue;        /* 子码流预览伴随，非独立通道 */
            int chn = jint(src, "channel", -1);
            if (chn < 0) continue;

            nvr_channel_t *e = emit(c, chn);
            if (!e) continue;
            e->enabled = 1;          /* ★ 同 fill_poe_channel：配置加载的通道即启用 */
            snprintf(e->name, sizeof(e->name), "%s", jstr(src, "name", "Camera"));
            snprintf(e->url,  sizeof(e->url),  "%s", jstr(src, "url", ""));
            snprintf(e->user, sizeof(e->user), "%s", dev_user ? dev_user : d.user);
            snprintf(e->pass, sizeof(e->pass), "%s", dev_pass ? dev_pass : d.pass);
            /* onvif_ip：显式 ip 优先；PoE 口无显式 ip → 用 poe_ip_pattern(198.18.<口>.1)，
             * 否则(数字/LAN 无 ip 且非直给 url)留空。修复:带 sources 的 PoE 口(poe-1/2)
             * 之前漏了 pattern，导致 onvif_ip 空、后台永不解析。 */
            if (dev_ip && dev_ip[0])
                snprintf(e->onvif_ip, sizeof(e->onvif_ip), "%s", dev_ip);
            else if (poe_port > 0)
                ipfmt(e->onvif_ip, sizeof(e->onvif_ip), d.ip_pattern, poe_port);
            else
                e->onvif_ip[0] = 0;
            e->onvif_port = dev_port;
            e->codec  = codec_of(jstr(src, "codec", "auto"));
            e->stream = stream_of(jstr(src, "stream", "main"));
            e->record = jbool(src, "record", d.record);
            e->poe_port = poe_port;
            e->onvif_auto = (e->url[0] == 0) && dev_onvif_auto;
            e->vout_win = chn;
        }
    }

    /* ★ PoE 口是固定 16 口硬件:补齐 channels.json 未列的 PoE 口通道(只补缺失的,不动已配的单/多源口)。
     * 修根因:channels.json 的 devices 漏列某口(实测漏口16)→ 该口通道从不生成 → install_slot 从不建成
     * in_use PoE 槽 → nvr_channel.c 的 tick 发现(line ~1836)与 on_discovered(line ~1063)都要求
     * "in_use && poe_port>0" 才探测/匹配 → 段 198.18.16.x 从不被广播探测 → 口16 永不发现、DB camera
     * 永远空(死循环:不 in_use→不探测→不发现→不落库→不 in_use)。补齐后 16 个 PoE 口全预建、全被探测。 */
    {
        int poe_n = (c->sys.poe_ports > 0) ? c->sys.poe_ports : 16;
        if (poe_n > NVR_CFG_MAX_CH) poe_n = NVR_CFG_MAX_CH;
        for (int port = 1; port <= poe_n; port++) {
            int chn = d.ch_base + (port - 1);
            int exists = 0;
            for (int i = 0; i < c->nch; i++)
                if (c->ch[i].chn == chn) { exists = 1; break; }
            if (!exists)
                fill_poe_channel(c, &d, port, NULL);   /* 缺失口:按默认预建(enabled=1,占位 IP 198.18.<口>.1) */
        }
    }
}

/* ---------- 入口 ---------- */
int nvr_config_load(const char *dir, nvr_config_t *out)
{
    if (!dir || !out) return -1;
    memset(out, 0, sizeof(*out));

    cJSON *js = load_json(dir, "system.json");
    cJSON *jt = load_json(dir, "storage.json");
    cJSON *jr = load_json(dir, "streaming.json");
    cJSON *jc = load_json(dir, "channels.json");

    load_system(out, js);
    load_storage(out, jt);      /* 依赖 sys.sn，故在 system 之后 */
    load_streaming(out, jr);
    load_channels(out, jc);

    if (js) cJSON_Delete(js);
    if (jt) cJSON_Delete(jt);
    if (jr) cJSON_Delete(jr);
    if (jc) cJSON_Delete(jc);

    return (out->nch > 0) ? 0 : -1;
}

/* ---------- 运行期设置库 overlay（桥接，最小改动冻结 JSON 模型） ---------- */
int nvr_config_overlay_from_settings(nvr_config_t *cfg, nvr_settings_t *settings)
{
    if (!cfg) return -1;
    if (!settings) return 0;

    /* 标量覆盖：设备名 */
    char name[64];
    if (nvr_settings_get_str(settings, "system.device_name", name, sizeof(name), "") > 0)
        snprintf(cfg->sys.name, sizeof(cfg->sys.name), "%s", name);

    /* 存储策略覆盖 */
    char hf[16];
    if (nvr_settings_get_str(settings, "storage.hdd_full", hf, sizeof(hf), "") > 0)
        cfg->storage.hdd_full = (!strcmp(hf, "stop")) ? RSDK_HDDFULL_STOP : RSDK_HDDFULL_OVERWRITE;

    /* 设备表覆盖/新增：settings.camera 行叠加到 cfg->ch[] */
    nvr_camera_row_t rows[NVR_CFG_MAX_CH];
    int nr = nvr_settings_camera_list(settings, rows, NVR_CFG_MAX_CH);
    for (int i = 0; i < nr; i++) {
        nvr_camera_row_t *r = &rows[i];
        nvr_channel_t *e = emit(cfg, r->chn);   /* 复用: 存在则取现有, 否则新增 */
        if (!e) continue;
        /* enabled=0 行:出厂预建的 32 个占位行 + 已删设备都是 enabled=0(见 nvr_settings.c
         * 预建注释:"加载器(跳 !enabled)")。绝不能用它覆盖 config 里 enabled=1 的 PoE 口——
         * PoE 口是即插即用、config 恒 enabled=1;一旦被占位/残留的 enabled=0 标成 DISABLED,
         * install_slot 早退 → streaming 未注册(used=0)→ 后续发现解析出的 URL 在 set_url 被拒
         * → PoE 口永不起 puller、永不出图。故 PoE 口跳过该行、保留 config 的 enabled(真实相机
         * 上线后 persist_camera 会以 enabled=1 回写、自愈 DB);非 PoE(手动 IP 通道)仍尊重 DB
         * 的 enabled=0(支持禁用/删除)。 */
        if (!r->enabled) {
            if (e->poe_port > 0) continue;      /* PoE 口:忽略占位/残留禁用,保留 config 自动发现 */
            e->enabled = 0;                      /* 非 PoE:尊重 DB 禁用,保留占位但标记 */
            continue;
        }
        e->enabled = r->enabled;
        if (r->name[0])     snprintf(e->name, sizeof(e->name), "%s", r->name);
        if (r->url[0])      snprintf(e->url,  sizeof(e->url),  "%s", r->url);
        if (r->username[0]) snprintf(e->user, sizeof(e->user), "%s", r->username);
        if (r->password[0]) snprintf(e->pass, sizeof(e->pass), "%s", r->password);
        if (r->ip[0])       snprintf(e->onvif_ip, sizeof(e->onvif_ip), "%s", r->ip);
        if (r->mac[0])      snprintf(e->mac, sizeof(e->mac), "%s", r->mac);
        if (r->model[0])    snprintf(e->model, sizeof(e->model), "%s", r->model);  /* 型号:重启回显 */
        if (r->firmware[0]) snprintf(e->firmware, sizeof(e->firmware), "%s", r->firmware);
        if (r->onvif_port)  e->onvif_port = r->onvif_port;
        if (r->serial[0])   snprintf(e->serial, sizeof(e->serial), "%s", r->serial);
        if (r->service_url[0]) snprintf(e->service_url, sizeof(e->service_url), "%s", r->service_url);
        if (r->url_main[0]) snprintf(e->url_main, sizeof(e->url_main), "%s", r->url_main);  /* 解析后主流:同设备直连 */
        if (r->url_sub[0])  snprintf(e->url_sub,  sizeof(e->url_sub),  "%s", r->url_sub);
        e->onvif_auto = r->onvif_auto;
        e->poe_port   = r->poe_port;
        e->codec      = r->codec;
        e->stream     = r->stream;
        e->record     = r->record;
        e->kind       = r->kind;
        e->backend    = r->backend;    /* DB 权威(nopOnvif/onvif=1) */
        e->vout_win   = e->chn;
        /* 多视频源:恢复源序号/类型/token(重启后 get_url 按 token 拉各自源流) */
        e->dev_chn    = r->dev_chn > 0 ? r->dev_chn : 1;
        if (r->type[0]) snprintf(e->type, sizeof(e->type), "%s", r->type);
        snprintf(e->video_source_token, sizeof(e->video_source_token), "%s", r->video_source_token);
        snprintf(e->enh_random, sizeof(e->enh_random), "%s", r->enh_random);
    }
    return 0;
}
