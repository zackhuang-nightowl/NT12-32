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

/* ★ 从实际码流首 NAL 检测编码(不信 SDP 声明):关键帧首 NAL 是参数集,可靠区分 H264/H265。
 * 返回 0=H264 / 1=H265 / -1=无法判定(首 NAL 非参数集,如 P 帧 → 保持原 codec)。
 * H265 基层参数集 byte0:VPS=0x40 SPS=0x42 PPS=0x44(type 32/33/34, layer0);
 * H264 参数集:type=(b0&0x1F)=7(SPS)/8(PPS)(如 0x67/0x27/0x68)。
 * H264 P 帧首 NAL(0x41/0x61/0x01)既不在 H265 精确集、(b0&0x1F)也非 7/8 → 返 -1,不误判。 */
int nal_detect_codec(const uint8_t *data, int len)
{
    if (!data || len < 5) return -1;
    int sc = 0, off = next_nal(data, len, 0, &sc);
    if (off < 0 || off >= len) return -1;
    uint8_t b0 = data[off];
    if (b0 == 0x40 || b0 == 0x42 || b0 == 0x44) return 1;   /* H265 VPS/SPS/PPS(基层) */
    int t264 = b0 & 0x1F;
    if (t264 == 7 || t264 == 8)               return 0;    /* H264 SPS/PPS */
    return -1;                                              /* 首 NAL 非参数集 → 不判 */
}

/* ---- 轻量 bit reader:只服务 SPS log2 / slice frame_num 连续性 ---- */
typedef struct { const uint8_t *p; int n; int bit; } nal_br_t;

static void br_init(nal_br_t *b, const uint8_t *p, int n) { b->p = p; b->n = n; b->bit = 0; }

static int br_u(nal_br_t *b, int bits)
{
    int v = 0;
    while (bits-- > 0) {
        int bi = b->bit >> 3, bj = 7 - (b->bit & 7);
        if (bi >= b->n) return 0;
        v = (v << 1) | ((b->p[bi] >> bj) & 1);
        b->bit++;
    }
    return v;
}

static unsigned br_ue(nal_br_t *b)
{
    int z = 0;
    while (br_u(b, 1) == 0 && z < 31) z++;
    if (z == 0) return 0;
    return ((1u << z) - 1u) + (unsigned)br_u(b, z);
}

static int br_se(nal_br_t *b)
{
    unsigned c = br_ue(b);
    if (c & 1) return (int)((c + 1) / 2);
    return -(int)(c / 2);
}

/* 从 SPS RBSP(已跳过 NAL header)解析 log2_max_frame_num;失败返回 0。 */
static int sps_log2_max_frame_num(const uint8_t *rbsp, int len)
{
    if (!rbsp || len < 4) return 0;
    nal_br_t b; br_init(&b, rbsp, len);
    int profile_idc = br_u(&b, 8);
    br_u(&b, 1 + 1 + 1 + 5 + 8);          /* constraint + level */
    (void)br_ue(&b);                      /* sps_id */
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
        profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
        profile_idc == 86  || profile_idc == 118 || profile_idc == 128 ||
        profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
        profile_idc == 135) {
        unsigned chroma = br_ue(&b);
        if (chroma == 3) br_u(&b, 1);
        (void)br_ue(&b); (void)br_ue(&b);
        br_u(&b, 1);
        if (br_u(&b, 1)) {                /* scaling_matrix */
            int i, j;
            for (i = 0; i < 8; i++) {
                if (!br_u(&b, 1)) continue;
                int last = 8;
                for (j = 0; j < (i < 6 ? 16 : 64); j++) {
                    last = last + br_se(&b);
                    if (last == 0) break;
                }
            }
        }
    }
    int log2 = (int)br_ue(&b) + 4;
    if (log2 < 4 || log2 > 16) return 0;
    return log2;
}

/* 从 slice RBSP 读 frame_num;需要已知 log2。失败 -1。 */
static int slice_frame_num(const uint8_t *rbsp, int len, int log2)
{
    if (!rbsp || len < 1 || log2 < 4 || log2 > 16) return -1;
    nal_br_t b; br_init(&b, rbsp, len);
    (void)br_ue(&b);                      /* first_mb */
    (void)br_ue(&b);                      /* slice_type */
    (void)br_ue(&b);                      /* pps_id */
    return br_u(&b, log2);
}

/* 去 RBSP emulation-prevention(00 00 03 → 00 00),拷到 out。返回有效字节数(≤outcap)。
 * 分辨率字段在 SPS 较靠后,必须先去转义再按位解析,否则遇 03 字节会错位。 */
static int rbsp_unescape(const uint8_t *in, int n, uint8_t *out, int outcap)
{
    int o = 0, zeros = 0;
    for (int i = 0; i < n && o < outcap; i++) {
        uint8_t b = in[i];
        if (zeros >= 2 && b == 0x03) { zeros = 0; continue; }   /* emulation byte,丢 */
        out[o++] = b;
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return o;
}

/* H.264 SPS RBSP(不含 NAL 头)→ 宽高。成功返回 1。含 frame_cropping 修正。 */
static int h264_sps_dims(const uint8_t *rbsp, int len, int *w, int *h)
{
    if (!rbsp || len < 4) return 0;
    nal_br_t b; br_init(&b, rbsp, len);
    int profile_idc = br_u(&b, 8);
    br_u(&b, 8 + 8);                          /* constraint flags + level_idc */
    (void)br_ue(&b);                          /* sps_id */
    unsigned chroma = 1;                      /* 缺省 4:2:0 */
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
        profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
        profile_idc == 86  || profile_idc == 118 || profile_idc == 128 ||
        profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
        profile_idc == 135) {
        chroma = br_ue(&b);
        if (chroma == 3) br_u(&b, 1);         /* separate_colour_plane */
        (void)br_ue(&b); (void)br_ue(&b);     /* bit_depth luma/chroma */
        br_u(&b, 1);                          /* qpprime_y_zero_transform_bypass */
        if (br_u(&b, 1)) {                    /* seq_scaling_matrix_present */
            for (int i = 0; i < 8; i++) {
                if (!br_u(&b, 1)) continue;
                int last = 8, cnt = (i < 6 ? 16 : 64);
                for (int j = 0; j < cnt; j++) { last = last + br_se(&b); if (last == 0) break; }
            }
        }
    }
    (void)br_ue(&b);                          /* log2_max_frame_num_minus4 */
    unsigned poc_type = br_ue(&b);
    if (poc_type == 0) {
        (void)br_ue(&b);                      /* log2_max_pic_order_cnt_lsb_minus4 */
    } else if (poc_type == 1) {
        br_u(&b, 1);                          /* delta_pic_order_always_zero */
        (void)br_se(&b); (void)br_se(&b);
        unsigned n = br_ue(&b);
        if (n > 256) return 0;                /* 防御:异常循环数 */
        for (unsigned i = 0; i < n; i++) (void)br_se(&b);
    }
    (void)br_ue(&b);                          /* max_num_ref_frames */
    br_u(&b, 1);                              /* gaps_in_frame_num_allowed */
    unsigned w_mbs = br_ue(&b) + 1;           /* pic_width_in_mbs_minus1 */
    unsigned h_mu  = br_ue(&b) + 1;           /* pic_height_in_map_units_minus1 */
    int frame_mbs_only = br_u(&b, 1);
    if (!frame_mbs_only) br_u(&b, 1);         /* mb_adaptive_frame_field */
    br_u(&b, 1);                              /* direct_8x8_inference */
    int cw = (int)(w_mbs * 16);
    int ch = (int)((2 - frame_mbs_only) * h_mu * 16);
    if (br_u(&b, 1)) {                        /* frame_cropping_flag */
        unsigned cl = br_ue(&b), cr = br_ue(&b), ct = br_ue(&b), cb = br_ue(&b);
        int subW = (chroma == 1 || chroma == 2) ? 2 : 1;
        int subH = (chroma == 1) ? 2 : 1;
        int cropUnitX = subW;
        int cropUnitY = subH * (2 - frame_mbs_only);
        cw -= (int)(cl + cr) * cropUnitX;
        ch -= (int)(ct + cb) * cropUnitY;
    }
    if (cw < 16 || ch < 16) return 0;
    *w = cw; *h = ch; return 1;
}

/* H.265 general profile_tier_level 跳过(仅跳位,不取值)。 */
static void h265_skip_ptl(nal_br_t *b, int max_sub_minus1)
{
    for (int i = 0; i < 12; i++) br_u(b, 8);  /* general PTL = 96 bits(含 level_idc) */
    int prof[8] = {0}, lvl[8] = {0};
    for (int i = 0; i < max_sub_minus1 && i < 8; i++) { prof[i] = br_u(b, 1); lvl[i] = br_u(b, 1); }
    if (max_sub_minus1 > 0) for (int i = max_sub_minus1; i < 8; i++) br_u(b, 2);
    for (int i = 0; i < max_sub_minus1 && i < 8; i++) {
        if (prof[i]) for (int k = 0; k < 11; k++) br_u(b, 8);   /* sub-layer PTL 88 bits */
        if (lvl[i])  br_u(b, 8);
    }
}

/* H.265 SPS RBSP(不含 2 字节 NAL 头)→ 宽高。成功返回 1。含 conformance_window 修正。 */
static int h265_sps_dims(const uint8_t *rbsp, int len, int *w, int *h)
{
    if (!rbsp || len < 8) return 0;
    nal_br_t b; br_init(&b, rbsp, len);
    br_u(&b, 4);                              /* sps_video_parameter_set_id */
    int max_sub_minus1 = br_u(&b, 3);
    br_u(&b, 1);                              /* temporal_id_nesting */
    h265_skip_ptl(&b, max_sub_minus1);
    (void)br_ue(&b);                          /* sps_seq_parameter_set_id */
    unsigned chroma = br_ue(&b);
    if (chroma == 3) br_u(&b, 1);             /* separate_colour_plane */
    unsigned pw = br_ue(&b);                  /* pic_width_in_luma_samples */
    unsigned ph = br_ue(&b);                  /* pic_height_in_luma_samples */
    if (pw < 16 || ph < 16 || pw > 16384 || ph > 16384) return 0;
    int cw = (int)pw, chh = (int)ph;
    if (br_u(&b, 1)) {                        /* conformance_window_flag */
        unsigned cl = br_ue(&b), cr = br_ue(&b), ct = br_ue(&b), cb = br_ue(&b);
        int subW = (chroma == 1 || chroma == 2) ? 2 : 1;
        int subH = (chroma == 1) ? 2 : 1;
        cw  -= (int)(cl + cr) * subW;
        chh -= (int)(ct + cb) * subH;
    }
    if (cw < 16 || chh < 16) return 0;
    *w = cw; *h = chh; return 1;
}

/* 从整帧 Annex-B 里找 SPS 并解析出编码分辨率。codec:0=H264 1=H265。成功返回 1,填 *w/*h。
 * 失败返回 0(调用方须保护:不得据此改解码尺寸)。 */
int nal_sps_dims(const uint8_t *data, int len, int codec, int *w, int *h)
{
    if (!data || len < 6 || !w || !h) return 0;
    int sc = 0, off = next_nal(data, len, 0, &sc);
    if (off < 0) return 0;
    while (off >= 0 && off < len) {
        uint8_t nh = data[off];
        int is_sps = (codec == 0) ? ((nh & 0x1F) == 7)
                                  : (((nh >> 1) & 0x3F) == 33);
        if (is_sps) {
            int hdr = (codec == 0) ? 1 : 2;   /* H264 NAL 头 1B,H265 2B */
            const uint8_t *nal = data + off + hdr;
            int nsc = 0, nx = next_nal(data, len, off + 1, &nsc);
            int rlen = (nx > off + 1) ? (nx - nsc - (off + hdr)) : (len - (off + hdr));
            if (rlen < 4) return 0;
            uint8_t rb[512];                  /* SPS RBSP 远小于此;截断也够取分辨率字段 */
            int rl = rbsp_unescape(nal, rlen, rb, (int)sizeof(rb));
            return (codec == 0) ? h264_sps_dims(rb, rl, w, h)
                                : h265_sps_dims(rb, rl, w, h);
        }
        int nsc = 0, nx = next_nal(data, len, off + 1, &nsc);
        if (nx < 0) break;
        off = nx;
    }
    return 0;
}

int nal_h264_frame_num_gap(const uint8_t *data, int len, int is_idr,
                           int *log2_io, int *prev_fn_io)
{
    if (!data || len < 5 || !log2_io || !prev_fn_io) return 0;

    int sc = 0, off = next_nal(data, len, 0, &sc);
    if (off < 0) off = 0;

    int gap = 0;
    while (off >= 0 && off < len) {
        int t = data[off] & 0x1F;
        const uint8_t *rbsp = data + off + 1;
        int rlen = 0;
        int nsc = 0, nx = next_nal(data, len, off + 1, &nsc);
        if (nx > off + 1) rlen = nx - nsc - (off + 1);
        else rlen = len - (off + 1);
        if (rlen < 0) rlen = 0;

        if (t == 7) {                     /* SPS → 学 log2 */
            int lg = sps_log2_max_frame_num(rbsp, rlen);
            if (lg > 0) *log2_io = lg;
        } else if (t >= 1 && t <= 5) {
            if (*log2_io <= 0) break;     /* 尚无 SPS,无法可靠比 */
            int fn = slice_frame_num(rbsp, rlen, *log2_io);
            if (fn < 0) break;
            if (is_idr || t == 5) {
                *prev_fn_io = fn;         /* IDR 重置参考,不判 gap */
                break;
            }
            if (*prev_fn_io >= 0) {
                int maxv = 1 << *log2_io;
                int expect = (*prev_fn_io + 1) % maxv;
                if (fn != expect) gap = 1;
            }
            *prev_fn_io = fn;
            break;                        /* 只看首个 VCL */
        }

        if (nx < 0) break;
        off = nx;
    }
    return gap;
}
