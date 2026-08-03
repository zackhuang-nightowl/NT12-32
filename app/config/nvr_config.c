/***************************************************************************************
 *  nvr_config.c — JSON 配置加载 + channels.json 扁平化（device→source→channel）
 ***************************************************************************************/
#include "nvr_config.h"
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
    s->hdmi_w = 3840; s->hdmi_h = 2160; s->default_layout = 16;
    if (!j) return;

    cJSON *dev = cJSON_GetObjectItem(j, "device");
    if (dev) {
        snprintf(s->model, sizeof(s->model), "%s", jstr(dev, "model", "NT12-32"));
        snprintf(s->name,  sizeof(s->name),  "%s", jstr(dev, "name",  "NVR"));
        snprintf(s->sn,    sizeof(s->sn),    "%s", jstr(dev, "sn",    ""));
    }
    cJSON *ch = cJSON_GetObjectItem(j, "channels");
    if (ch) {
        s->capacity     = jint(ch, "capacity", 32);
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
    snprintf(e->name, sizeof(e->name), "Camera %d", port);
    snprintf(e->user, sizeof(e->user), "%s", d->user);
    snprintf(e->pass, sizeof(e->pass), "%s", d->pass);
    ipfmt(e->onvif_ip, sizeof(e->onvif_ip), d->ip_pattern, port);
    e->onvif_port = 80;
    e->onvif_auto = onvif ? jbool(onvif, "auto", 1) : 1;   /* PoE 默认 ONVIF 自动取流 */
    e->codec  = d->codec;
    e->record = d->record;
    e->stream = stream_of(d->rec_stream);
    e->poe_port = port;
    e->url[0] = 0;   /* 待 ONVIF 填 */
}

static void load_channels(nvr_config_t *c, cJSON *j)
{
    c->nch = 0;
    if (!j) return;

    ch_defaults_t d = { "198.18.%d.100", "admin", "", "main", "sub", NVR_CODEC_AUTO, 1, 1, 0 };
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
    if (!cJSON_IsArray(devs)) return;

    cJSON *dev;
    cJSON_ArrayForEach(dev, devs) {
        if (!jbool(dev, "enable", 1)) continue;
        const char *type = jstr(dev, "type", "ip");
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
        int         dev_port = onvif ? jint(onvif, "port", 80) : 80;
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
            snprintf(e->name, sizeof(e->name), "%s", jstr(src, "name", "Camera"));
            snprintf(e->url,  sizeof(e->url),  "%s", jstr(src, "url", ""));
            snprintf(e->user, sizeof(e->user), "%s", dev_user ? dev_user : d.user);
            snprintf(e->pass, sizeof(e->pass), "%s", dev_pass ? dev_pass : d.pass);
            snprintf(e->onvif_ip, sizeof(e->onvif_ip), "%s", dev_ip ? dev_ip : "");
            e->onvif_port = dev_port;
            e->codec  = codec_of(jstr(src, "codec", "auto"));
            e->stream = stream_of(jstr(src, "stream", "main"));
            e->record = jbool(src, "record", d.record);
            e->poe_port = poe_port;
            e->onvif_auto = (e->url[0] == 0) && dev_onvif_auto;
            e->vout_win = chn;
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

    /* 通道表覆盖/新增：settings.channel 行叠加到 cfg->ch[] */
    nvr_chan_row_t rows[NVR_CFG_MAX_CH];
    int nr = nvr_settings_channel_list(settings, rows, NVR_CFG_MAX_CH);
    for (int i = 0; i < nr; i++) {
        nvr_chan_row_t *r = &rows[i];
        nvr_channel_t *e = emit(cfg, r->chn);   /* 复用: 存在则取现有, 否则新增 */
        if (!e) continue;
        e->enabled = r->enabled;
        if (!r->enabled) continue;              /* 禁用通道: 保留占位但标记 */
        if (r->name[0])     snprintf(e->name, sizeof(e->name), "%s", r->name);
        if (r->url[0])      snprintf(e->url,  sizeof(e->url),  "%s", r->url);
        if (r->user[0])     snprintf(e->user, sizeof(e->user), "%s", r->user);
        if (r->pass[0])     snprintf(e->pass, sizeof(e->pass), "%s", r->pass);
        if (r->onvif_ip[0]) snprintf(e->onvif_ip, sizeof(e->onvif_ip), "%s", r->onvif_ip);
        if (r->onvif_port)  e->onvif_port = r->onvif_port;
        e->onvif_auto = r->onvif_auto;
        e->poe_port   = r->poe_port;
        e->codec      = r->codec;
        e->stream     = r->stream;
        e->record     = r->record;
        e->kind       = r->kind;
        e->backend    = (r->kind == 0 /*NOP*/) ? 0 : 1;
        e->vout_win   = e->chn;
    }
    return 0;
}
