/* stream_nal.c — Annex-B NAL 分类。判关键帧用于「录像必须从关键帧起」的门控。 */
#include "stream_nal.h"

/* 找下一个起始码，返回其后 NAL 首字节偏移；-1=没有 */
static int next_nal(const uint8_t *d, int len, int from, int *sc_len)
{
    for (int i = from; i + 3 < len; i++) {
        if (d[i] == 0 && d[i+1] == 0) {
            if (d[i+2] == 1)               { *sc_len = 3; return i + 3; }
            if (d[i+2] == 0 && i+3 < len && d[i+3] == 1) { *sc_len = 4; return i + 4; }
        }
    }
    return -1;
}

void nal_classify(const uint8_t *data, int len, int codec, nal_class_t *out)
{
    out->is_key = 0; out->is_param = 0; out->has_param = 0; out->frame_type = 1 /*P*/;
    if (!data || len < 5) return;

    int sc = 0, off = next_nal(data, len, 0, &sc);
    if (off < 0) off = 0;                 /* 无起始码：当作裸 NAL 从头 */

    /* 遍历**整帧所有** NAL(不能只看第一个:有的相机把 SPS+PPS+I帧合在一帧发)。
     * 分别记录:见到参数集 / 见到 VCL slice / VCL 是否 IDR。 */
    int saw_param = 0, saw_vcl = 0, vcl_key = 0;
    while (off >= 0 && off < len) {
        uint8_t nh = data[off];
        if (codec == 0) {                 /* H.264: nal_type = nh & 0x1F */
            int t = nh & 0x1F;
            if (t == 7 || t == 8)        saw_param = 1;                 /* SPS/PPS */
            else if (t == 5)           { saw_vcl = 1; vcl_key = 1; }    /* IDR */
            else if (t >= 1 && t <= 4)   saw_vcl = 1;                   /* non-IDR slice(I/P未知) */
        } else {                          /* H.265: nal_type = (nh>>1) & 0x3F */
            int t = (nh >> 1) & 0x3F;
            if (t >= 32 && t <= 34)       saw_param = 1;                /* VPS/SPS/PPS */
            else if (t >= 16 && t <= 23){ saw_vcl = 1; vcl_key = 1; }   /* IRAP/IDR */
            else if (t <= 15)             saw_vcl = 1;                  /* trailing/leading slice */
        }
        int nsc = 0, nx = next_nal(data, len, off + 1, &nsc);
        if (nx < 0) break;
        off = nx;
    }
    out->has_param  = saw_param;
    out->is_param   = saw_param && !saw_vcl;             /* 纯参数集(无 slice) → 可扣留缓存 */
    /* 关键帧:IDR;或"参数集+slice"合并 AU(参数集紧邻其后的 slice 即该 GOP 关键帧);或纯参数集(供起录) */
    out->is_key     = vcl_key || saw_param;
    out->frame_type = (vcl_key || saw_param || !saw_vcl) ? 0 /*I*/ : 1 /*P*/;
}
