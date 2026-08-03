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
    out->is_key = 0; out->is_param = 0; out->frame_type = 1 /*P*/;
    if (!data || len < 5) return;

    int sc = 0, off = next_nal(data, len, 0, &sc);
    if (off < 0) off = 0;                 /* 无起始码：当作裸 NAL 从头 */

    /* 遍历 NAL：遇到 VCL(图像)即定帧类型；遇到参数集标记 is_param/is_key */
    while (off >= 0 && off < len) {
        uint8_t nh = data[off];
        if (codec == 0) {                 /* H.264: nal_type = nh & 0x1F */
            int t = nh & 0x1F;
            if (t == 7 || t == 8) { out->is_param = 1; out->is_key = 1; }     /* SPS/PPS */
            else if (t == 5)      { out->frame_type = 0; out->is_key = 1; return; } /* IDR */
            else if (t == 1)      { out->frame_type = 1; return; }            /* non-IDR */
        } else {                          /* H.265: nal_type = (nh>>1) & 0x3F */
            int t = (nh >> 1) & 0x3F;
            if (t >= 32 && t <= 34) { out->is_param = 1; out->is_key = 1; }   /* VPS/SPS/PPS */
            else if (t >= 16 && t <= 23) { out->frame_type = 0; out->is_key = 1; return; } /* IRAP/IDR */
            else if (t <= 9 || (t >= 10 && t <= 15)) { out->frame_type = 1; return; }      /* trailing/leading */
        }
        int nsc = 0, nx = next_nal(data, len, off + 1, &nsc);
        if (nx < 0) break;
        off = nx;
    }
    /* 只有参数集、无 VCL：保持 is_key=1/is_param=1、frame_type=I(供门控起录) */
    if (out->is_param) out->frame_type = 0;
}
