/***************************************************************************************
 *  stream_mgr.cpp — 16 路拉流生命周期管理（实现 nvr_streaming.h 的 C API）
 *
 *  持有通道表；start/stop/switch 委托 stream_puller。录像目标盘组来自 storage。
 ***************************************************************************************/
#include "stream_internal.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

extern "C" int  puller_start(stream_chan_t *c, int conn_to, int rx_to, rsdk_group_t *grp);
extern "C" void puller_stop (stream_chan_t *c);

struct nvr_stream_mgr {
    nvr_stream_mgr_cfg_t cfg;
    stream_chan_t        ch[NVR_MAX_CH];
    int                  used[NVR_MAX_CH];
};

/* chn → 槽位（本实现直接用 chn 作下标，0..15） */
static stream_chan_t *slot(nvr_stream_mgr_t *m, int chn)
{
    if (!m || chn < 0 || chn >= NVR_MAX_CH || !m->used[chn]) return NULL;
    return &m->ch[chn];
}

extern "C" rsdk_err_t nvr_stream_mgr_init(const nvr_stream_mgr_cfg_t *cfg, nvr_stream_mgr_t **out)
{
    if (!cfg || !out) return RSDK_E_PARAM;
    nvr_stream_mgr_t *m = (nvr_stream_mgr_t *)calloc(1, sizeof(*m));
    if (!m) return RSDK_E_NOSPACE;
    m->cfg = *cfg;
    if (m->cfg.conn_timeout <= 0) m->cfg.conn_timeout = 5;
    if (m->cfg.rx_timeout   <= 0) m->cfg.rx_timeout   = 10;
    *out = m;
    return RSDK_OK;
}

extern "C" void nvr_stream_mgr_deinit(nvr_stream_mgr_t *m)
{
    if (!m) return;
    nvr_stream_stop_all(m);
    free(m);
}

extern "C" rsdk_err_t nvr_stream_add_channel(nvr_stream_mgr_t *m, const nvr_stream_chan_cfg_t *c)
{
    if (!m || !c || c->chn < 0 || c->chn >= NVR_MAX_CH) return RSDK_E_PARAM;
    stream_chan_t *s = &m->ch[c->chn];
    memset(s, 0, sizeof(*s));
    s->cfg   = *c;
    s->codec = (c->codec == NVR_CODEC_AUTO) ? -1 : c->codec;
    s->state = NVR_CH_IDLE;
    s->mgr   = m;
    m->used[c->chn] = 1;
    return RSDK_OK;
}

extern "C" rsdk_err_t nvr_stream_start(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    if (c->puller) return RSDK_OK;                 /* 已在跑 */
    return puller_start(c, m->cfg.conn_timeout, m->cfg.rx_timeout, m->cfg.group) == 0
           ? RSDK_OK : RSDK_E_IO;
}

extern "C" rsdk_err_t nvr_stream_stop(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    puller_stop(c);
    return RSDK_OK;
}

extern "C" rsdk_err_t nvr_stream_start_all(nvr_stream_mgr_t *m)
{
    if (!m) return RSDK_E_PARAM;
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->used[i]) nvr_stream_start(m, i);
    return RSDK_OK;
}

extern "C" void nvr_stream_stop_all(nvr_stream_mgr_t *m)
{
    if (!m) return;
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->used[i]) puller_stop(&m->ch[i]);
}

extern "C" rsdk_err_t nvr_stream_switch_stream(nvr_stream_mgr_t *m, int chn, int stream, const char *url)
{
    stream_chan_t *c = slot(m, chn);
    if (!c || !url) return RSDK_E_PARAM;
    puller_stop(c);                                /* 换主/子码流 = 换 url 重连 */
    c->cfg.stream = stream;
    snprintf(c->cfg.url, sizeof(c->cfg.url), "%s", url);
    return nvr_stream_start(m, chn);
}

extern "C" rsdk_err_t nvr_stream_set_record(nvr_stream_mgr_t *m, int chn, int on)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    if (c->cfg.record == on) return RSDK_OK;
    c->cfg.record = on;
    if (c->puller) {                               /* 运行中：重启使录像开关生效 */
        puller_stop(c);
        return nvr_stream_start(m, chn);
    }
    return RSDK_OK;
}

extern "C" nvr_ch_state_t nvr_stream_state(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    return c ? c->state : NVR_CH_IDLE;
}

extern "C" int nvr_stream_decode_denied(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    return c ? c->decode_denied : 0;
}
