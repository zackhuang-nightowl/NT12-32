/***************************************************************************************
 *  stream_router.c — 每路裸流 → {硬解上屏(仅 decode_stream 那路), 录像(主+子都录)}（纯 C）
 *
 *  双流:每通道主+子两路常拉,各自 stream_route_video(p,...)。
 *    · 录像:两路都写同一 writer(f.stream 标记主/子),各自关键帧门控。
 *    · 显示:单个硬件解码器,只由 decode_stream(单宫格=主/多宫格=子)那一路喂;show_win<0 不解码。
 *  ⚠️ 解码一律 NA51090 硬件 VPU(不软解)。
 ***************************************************************************************/
#include "stream_internal.h"
#include "stream_nal.h"
#include "mhal_vout.h"        /* mhal_vout_is_deferred —— 批量提交中不喂关键帧(解码器未 start) */
#include "nvr_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static stream_pull_t *pull_of(stream_chan_t *c, int stream)
{
    return (stream == NVR_STREAM_SUB) ? &c->psub : &c->pmain;
}

/* 解码缓冲维度估算(供 max_mem 分配 + 预算准入)。按 stream 区分:子 720p / 主 8K 上限。
 * enc_w/h 已知(ONVIF profile 回填)则精确。理想应回填 enc_w/h(待补)。 */
static void resolve_dim(const nvr_stream_chan_cfg_t *cfg, int stream, int *w, int *h, int *fps)
{
    if (cfg->enc_w > 0 && cfg->enc_h > 0) { *w = cfg->enc_w; *h = cfg->enc_h; }
    else if (stream == NVR_STREAM_SUB)    { *w = 1280; *h = 720;  }   /* 子码流上限 720p */
    else                                  { *w = 7680; *h = 2176; }   /* 主码流上限 8K(7680×2160) */
    *fps = cfg->fps > 0 ? cfg->fps : 20;
}

/* 开解码器绑到窗口 win(>=0),用 **decode_stream 那一路** 的 codec/分辨率。解码=上屏。
 * 只对可见格通道调用;超解码预算则拒绝(录像照常)。 */
/* 供回放引擎:取某通道某码流的解码尺寸(main 录像回放用)。 */
void stream_chan_get_dim(stream_chan_t *c, int stream, int *w, int *h, int *fps)
{
    if (c) resolve_dim(&c->cfg, stream, w, h, fps);
}

void stream_decode_open(stream_chan_t *c, int win)
{
    if (!c || win < 0 || c->vdec) return;         /* 已在解码则不重复开 */
    stream_pull_t *p = pull_of(c, c->decode_stream);
    if (!p->connected || p->codec < 0) return;    /* 该路还没连上/codec 未定 → 等连上再开 */
    c->decode_denied = 0;
    mhal_codec_t mc = (p->codec == RSDK_CODEC_H265) ? MHAL_CODEC_H265 : MHAL_CODEC_H264;
    int w, h, fps; resolve_dim(&c->cfg, c->decode_stream, &w, &h, &fps);
    int rc = mhal_vdec_open(c->cfg.chn, mc, w, h, fps, win, &c->vdec);
    if (rc == MHAL_VDEC_EBUDGET) {
        c->vdec = NULL; c->decode_denied = 1;
        NVR_LOGE("stream", "chn%d 超出解码能力(预算超限) → 仅录像不预览", c->cfg.chn);
    } else if (rc != 0) {
        c->vdec = NULL;
    }
    c->fed_since_open = 0;
    c->live_synced    = 0;   /* 新开解码器:还没从实时 IDR 起播 */
    if (c->vdec) NVR_LOGI("stream", "chn%d ▶开解码 → 格%d (%s码流)",
                          c->cfg.chn, win, c->decode_stream == NVR_STREAM_SUB ? "子" : "主");
    /* ★ 喂缓存关键帧秒出图。但**批量提交中(defer)解码器还没 start**,此时喂无效 → 跳过,
     * 由 mhal_vout_defer_end 之后的 stream_feed_keyframe 统一喂(见 preview set_mode)。 */
    if (!mhal_vout_is_deferred()) stream_feed_keyframe(c);
}

/* 给解码器喂 decode_stream 那路的缓存关键帧(解码器须已 start)→ 不等下个 IDR、瞬时出图。 */
void stream_feed_keyframe(stream_chan_t *c)
{
    if (!c || !c->vdec || c->fed_since_open > 0) return;
    stream_pull_t *p = (c->decode_stream == NVR_STREAM_SUB) ? &c->psub : &c->pmain;
    if (p->kf_len > 0) {
        /* 缓存 IDR 给个即时(可能稍旧)画面,避免起播前全黑。发送成功才计数;失败不计——
         * 由 stream_route_video 的实时 IDR 门控兜底恢复(不会永久黑)。不置 live_synced:
         * 缓存 IDR 与随后实时 P 帧不连续,须等一个实时 IDR 才算真正同步。 */
        if (mhal_vdec_send(c->vdec, p->kf, (uint32_t)p->kf_len, p->kf_ts) == 0) {
            c->fed_since_open++;
            NVR_LOGI("stream", "chn%d 喂缓存关键帧(%s)→ 即时出图", c->cfg.chn, c->decode_stream == NVR_STREAM_SUB ? "子" : "主");
        }
    }
}

/* 关解码器(隐藏)。只停解码,两路拉流+录像不动。 */
void stream_decode_close(stream_chan_t *c)
{
    if (!c || !c->vdec) return;
    mhal_vdec_close(c->vdec);
    c->vdec = NULL;
    c->decode_denied = 0;
    c->pmain.par_len = c->pmain.par_building = 0;
    c->psub.par_len  = c->psub.par_building  = 0;
    NVR_LOGI("stream", "chn%d ■关解码 (隐藏,拉流/录像照常)", c->cfg.chn);
}

/* 只开录像 writer(不碰解码/显示)。供运行时"格式化后重组装盘组"时对已连通道补开 writer:
 * 开机盘未格式化时 grp=NULL、writer 没开;格式化+重组装后 group 就绪,用此对录像通道补开 writer。幂等。 */
void stream_open_writer(stream_chan_t *c, rsdk_group_t *grp)
{
    if (!c || !c->cfg.record || !grp || c->writer) return;
    rsdk_err_t rc = rsdk_rec_open_group(grp, c->cfg.chn, RSDK_REC_CONTINUOUS, &c->writer);
    if (rc != RSDK_OK) { c->writer = NULL; NVR_LOGE("stream", "chn%d 补开 writer 失败 %d", c->cfg.chn, rc); }
    else { c->rec_gated_main = 0; c->rec_gated_sub = 0; NVR_LOGI("stream", "chn%d 盘组就绪→补开录像 writer", c->cfg.chn); }
}

/* 运行时关录像 writer(录像开关关闭时立即停录,拉流/解码不动)。 */
void stream_close_writer(stream_chan_t *c)
{
    if (!c || !c->writer) return;
    rsdk_rec_close(c->writer); c->writer = NULL;
    c->rec_gated_main = 0; c->rec_gated_sub = 0;
    NVR_LOGI("stream", "chn%d 录像开关关→停录(关 writer)", c->cfg.chn);
}

/* 开录像 writer(主+子共用,按 f.stream 区分)。可见则同时开解码。 */
rsdk_err_t stream_router_open(stream_chan_t *c, rsdk_group_t *grp)
{
    if (c->cfg.record && grp && !c->writer) {
        rsdk_err_t rc = rsdk_rec_open_group(grp, c->cfg.chn, RSDK_REC_CONTINUOUS, &c->writer);
        if (rc != RSDK_OK) c->writer = NULL;
    }
    c->rec_gated_main = 0; c->rec_gated_sub = 0;
    if (c->show_win >= 0) stream_decode_open(c, c->show_win);
    return RSDK_OK;
}

/* 一路码流来一帧。 */
void stream_route_video(stream_pull_t *p, const uint8_t *data, int len, uint32_t ts, uint16_t seq)
{
    (void)seq;
    if (!p || !p->owner || !data || len <= 0) return;
    stream_chan_t *c = p->owner;

    nal_class_t nc;
    nal_classify(data, len, p->codec, &nc);

    /* 诊断:主码流 IDR 间隔(GOP 长度)。8K 若 GOP 很长,切回后要久等下个实时 IDR 才恢复 → 看着像"丢失"。 */
    if (p->stream == NVR_STREAM_MAIN && !nc.is_param && nc.is_key) {
        NVR_LOGI("router", "ch%d[主] IDR@f%u len=%d 距上个IDR=%u帧(GOP)",
                 c->cfg.chn, p->vframes, len, p->vframes > p->last_idr_f ? p->vframes - p->last_idr_f : 0);
        p->last_idr_f = p->vframes;
    }

    /* --- 参数集缓存(每路都维护,与是否解码无关 → 切过来时关键帧已备好参数)。 --- */
    if (nc.is_param) {
        if (!p->par_building) { p->par_len = 0; p->par_building = 1; }
        if (p->par_len + len <= (int)sizeof(p->par)) {
            memcpy(p->par + p->par_len, data, len); p->par_len += len;
        }
    } else {
        p->par_building = 0;
    }

    /* --- 组装"送解码/缓存"的完整帧:缺参数的关键帧拼上缓存参数集;其余原样。 --- */
    const uint8_t *sbuf = data; int slen = len; uint8_t *tmp = NULL;
    if (!nc.is_param && nc.is_key && !nc.has_param && p->par_len > 0) {
        tmp = (uint8_t *)malloc((size_t)(p->par_len + len));
        if (tmp) {
            memcpy(tmp, p->par, (size_t)p->par_len);
            memcpy(tmp + p->par_len, data, (size_t)len);
            sbuf = tmp; slen = p->par_len + len;
        }
    }

    /* --- 关键帧缓存(每路都存,与是否解码无关):切到本路开解码时立即喂它 → 秒出图。 --- */
    if (!nc.is_param && nc.is_key && slen > 0) {
        if (p->kf_cap < slen) {
            uint8_t *nk = (uint8_t *)realloc(p->kf, (size_t)slen);
            if (nk) { p->kf = nk; p->kf_cap = slen; }
        }
        if (p->kf && p->kf_cap >= slen) { memcpy(p->kf, sbuf, (size_t)slen); p->kf_len = slen; p->kf_ts = ts; }
    }

    /* --- 上屏:只有 **decode_stream 那一路** 喂解码器(参数集帧本身不单独送)。 ---
     * ★ 起播门控:开解码后必须从一个**真正送进去的关键帧**起播,否则:
     *   · 直接喂实时 P 帧 → 参考链断 → 花屏/卡旧图(冻结);
     *   · 缓存 IDR 发送失败又不重试 → 解码器无 IDR → 全黑。
     * 故 live_synced=0 期间只喂关键帧、且**发送成功**才置 synced(丢弃 P 帧);synced 后正常喂。
     * 长 GOP 8K 切回:最多等一个 GOP(~5s)到下个实时 IDR,但一出就是干净实时画面。 */
    if (c->vdec && p->stream == c->decode_stream && !nc.is_param) {
        int ok = 1;
        if (!c->live_synced) {
            if (nc.is_key) {
                ok = (mhal_vdec_send(c->vdec, sbuf, (uint32_t)slen, ts) == 0);
                if (ok) { c->live_synced = 1; NVR_LOGI("stream", "chn%d 实时IDR起播(len=%d)", c->cfg.chn, slen); }
            } else {
                ok = 0;   /* 未起播且非关键帧 → 丢弃(参考链无效) */
            }
        } else {
            ok = (mhal_vdec_send(c->vdec, sbuf, (uint32_t)slen, ts) == 0);
        }
        if (ok && c->fed_since_open < 1000000) c->fed_since_open++;   /* 出图就绪计数(切宫格阻塞回复用) */
    }

    if (tmp) free(tmp);

    /* --- 录像:本路写 writer,f.stream 标记主/子,各自关键帧门控。
     * cfg.record=0(录像开关关)→ 跳过写入(不 close writer,避免命令线程/puller 竞态)。 --- */
    if (!c->writer || !c->cfg.record) return;
    int *gated = (p->stream == NVR_STREAM_SUB) ? &c->rec_gated_sub : &c->rec_gated_main;
    if (!*gated) {
        if (!nc.is_key) return;            /* 未到关键帧,丢前导 P 帧 */
        *gated = 1;
    }
    rsdk_frame_t f;
    memset(&f, 0, sizeof(f));
    f.chn        = (uint16_t)c->cfg.chn;
    f.stream     = (uint8_t)p->stream;             /* 0主/1子 */
    f.codec      = (uint8_t)p->codec;              /* 0=H264 1=H265 */
    f.frame_type = (uint8_t)nc.frame_type;
    f.pts        = (uint64_t)ts;
    f.wall_time  = (uint64_t)time(NULL);
    f.data       = data;
    f.len        = (uint32_t)len;
    rsdk_rec_write_frame(c->writer, &f);
}

void stream_route_audio(stream_pull_t *p, const uint8_t *data, int len, uint32_t ts)
{
    /* 音频录像:与视频同 writer,stream=2/codec=AAC。仅主路带音频(避免主子重复录音)。 */
    if (!p || !p->owner || p->stream != NVR_STREAM_MAIN) return;
    stream_chan_t *c = p->owner;
    if (!c->writer || !c->rec_gated_main || !data || len <= 0) return;
    rsdk_frame_t f;
    memset(&f, 0, sizeof(f));
    f.chn        = (uint16_t)c->cfg.chn;
    f.stream     = 2;
    f.codec      = RSDK_CODEC_AAC;
    f.frame_type = RSDK_FRAME_AUDIO;
    f.pts        = (uint64_t)ts;
    f.wall_time  = (uint64_t)time(NULL);
    f.data       = data;
    f.len        = (uint32_t)len;
    rsdk_rec_write_frame(c->writer, &f);
}

void stream_router_close(stream_chan_t *c)
{
    if (!c) return;
    if (c->writer) { rsdk_rec_close(c->writer); c->writer = NULL; }
    if (c->vdec)   { mhal_vdec_close(c->vdec);   c->vdec = NULL; }
    c->rec_gated_main = 0; c->rec_gated_sub = 0;
    if (c->pmain.kf) { free(c->pmain.kf); c->pmain.kf = NULL; c->pmain.kf_len = c->pmain.kf_cap = 0; }
    if (c->psub.kf)  { free(c->psub.kf);  c->psub.kf  = NULL; c->psub.kf_len  = c->psub.kf_cap  = 0; }
}
