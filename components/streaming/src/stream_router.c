/***************************************************************************************
 *  stream_router.c — 一帧裸流 → {硬解上屏, 旁路录像}（纯 C）
 *
 *  拉流回调 video_cb 里每来一帧就调 stream_route_video()：
 *    1) 送 platform/media_hal 硬解（vdec 已 bind vout_win → 直接上屏预览）
 *    2) 旁路录像：从「关键帧」起门控，构造 rsdk_frame_t → rsdk_rec_write_frame
 *  ⚠️ 不走 ffmpeg 软解——16 路软解 CPU 扛不住，解码一律 NA51090 硬件 VPU。
 ***************************************************************************************/
#include "stream_internal.h"
#include "stream_nal.h"
#include "nvr_log.h"
#include <string.h>
#include <time.h>

/* 分辨率/帧率未知时按码流类型估算（供解码预算准入；真值应由 ONVIF 编码配置回填 cfg.enc_*）*/
static void resolve_dim(const nvr_stream_chan_cfg_t *cfg, int *w, int *h, int *fps)
{
    if (cfg->enc_w > 0 && cfg->enc_h > 0) { *w = cfg->enc_w; *h = cfg->enc_h; }
    else if (cfg->stream == NVR_STREAM_SUB) { *w = 640;  *h = 360;  }   /* 子码流估 360p */
    else                                    { *w = 1920; *h = 1080; }   /* 主码流估 1080p */
    *fps = cfg->fps > 0 ? cfg->fps : 20;
}

rsdk_err_t stream_router_open(stream_chan_t *c, rsdk_group_t *grp)
{
    c->decode_denied = 0;
    /* 1) 解码器：bind 到预览窗口（vout_win>=0 时解码结果直连 HDMI 分屏）
     *    ★ 按实际分辨率×帧率做吞吐预算准入；超 746 Mpix/s → 拒绝这一路预览(录像照常) */
    if (c->cfg.vout_win >= 0) {
        mhal_codec_t mc = (c->codec == RSDK_CODEC_H265) ? MHAL_CODEC_H265 : MHAL_CODEC_H264;
        int w, h, fps; resolve_dim(&c->cfg, &w, &h, &fps);
        int rc = mhal_vdec_open(c->cfg.chn, mc, w, h, fps, c->cfg.vout_win, &c->vdec);
        if (rc == MHAL_VDEC_EBUDGET) {
            c->vdec = NULL;
            c->decode_denied = 1;           /* 解码预算超限：不解这一路，UI 需提示 */
            NVR_LOGE("stream", "chn%d 超出解码能力(预算超限) → 该路仅录像不预览", c->cfg.chn);
        } else if (rc != 0) {
            c->vdec = NULL;                 /* 其它解码失败也不阻塞录像 */
        }
    }

    /* 2) 录像写入器（盘组，跨段自动均衡选盘） */
    if (c->cfg.record && grp) {
        rsdk_err_t rc = rsdk_rec_open_group(grp, c->cfg.chn, RSDK_REC_CONTINUOUS, &c->writer);
        if (rc != RSDK_OK) c->writer = NULL;
    }
    c->rec_gated = 0;
    return RSDK_OK;
}

void stream_route_video(stream_chan_t *c, const uint8_t *data, int len,
                        uint32_t ts, uint16_t seq)
{
    (void)seq;
    if (!c || !data || len <= 0) return;

    /* --- 上屏：整帧送硬解，解码器已 bind 分屏窗口 --- */
    if (c->vdec)
        mhal_vdec_send(c->vdec, data, (uint32_t)len, ts);

    /* --- 录像：关键帧门控（必须从 IDR/参数集起，否则回放花屏） --- */
    if (!c->writer) return;

    nal_class_t nc;
    nal_classify(data, len, c->codec, &nc);

    if (!c->rec_gated) {
        if (!nc.is_key) return;            /* 还没等到关键帧，丢弃前导 P 帧 */
        c->rec_gated = 1;                  /* 从这个关键帧开始录 */
    }

    rsdk_frame_t f;
    memset(&f, 0, sizeof(f));
    f.chn        = (uint16_t)c->cfg.chn;
    f.stream     = (uint8_t)c->cfg.stream;         /* 0主/1子 */
    f.codec      = (uint8_t)c->codec;              /* 0=H264 1=H265 */
    f.frame_type = (uint8_t)nc.frame_type;         /* I/P */
    f.pts        = (uint64_t)ts;                   /* RTP 90kHz 时间戳 */
    f.wall_time  = (uint64_t)time(NULL);           /* 墙钟 epoch（掉索引可自解析） */
    f.data       = data;                           /* 明文 Annex-B（内部按特性 AES-CTR） */
    f.len        = (uint32_t)len;
    rsdk_rec_write_frame(c->writer, &f);
}

void stream_route_audio(stream_chan_t *c, const uint8_t *data, int len, uint32_t ts)
{
    /* 音频录像（可选）：与视频同 writer，stream=2/codec=AAC。
     * 预览音频→platform 音频输出，另行接。此处仅录像旁路占位。 */
    if (!c || !c->writer || !c->rec_gated || !data || len <= 0) return;
    rsdk_frame_t f;
    memset(&f, 0, sizeof(f));
    f.chn        = (uint16_t)c->cfg.chn;
    f.stream     = 2;                              /* 音频 */
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
    c->rec_gated = 0;
}
