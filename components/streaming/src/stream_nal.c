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
