/* ts_mux.c — 极简 MPEG2-TS 封装。见 ts_mux.h。
 * 结构：PAT(pid0) + PMT(pid0x1000) + 视频(pid0x100) + 音频(pid0x101)。
 * 每个 AU 打成一个 PES，再切成 188B TS 包；关键帧带 PCR。上真机再调优。 */
#include "ts_mux.h"
#include <stdlib.h>
#include <string.h>

#define TS_PKT     188
#define PID_PMT    0x1000
#define PID_VIDEO  0x100
#define PID_AUDIO  0x101
#define STREAM_VIDEO 0xE0
#define STREAM_AUDIO 0xC0

struct ts_mux {
    uint8_t *buf; size_t len, cap;
    int  h265;
    int  wrote_psi;      /* 本切片是否已发 PAT/PMT */
    uint8_t cc_pat, cc_pmt, cc_v, cc_a;
};

static int ensure(ts_mux_t *m, size_t add)
{
    if (m->len + add <= m->cap) return 0;
    size_t nc = m->cap ? m->cap * 2 : 65536;
    while (nc < m->len + add) nc *= 2;
    uint8_t *nb = realloc(m->buf, nc);
    if (!nb) return -1;
    m->buf = nb; m->cap = nc; return 0;
}
static void put(ts_mux_t *m, const uint8_t *p, size_t n) { if (ensure(m, n) == 0) { memcpy(m->buf + m->len, p, n); m->len += n; } }

ts_mux_t *ts_mux_new(int h265)
{
    ts_mux_t *m = calloc(1, sizeof(*m));
    if (m) m->h265 = h265;
    return m;
}
void ts_mux_free(ts_mux_t *m) { if (m) { free(m->buf); free(m); } }

/* CRC32/MPEG2 for PSI */
static uint32_t crc32_mpeg(const uint8_t *d, int n)
{
    uint32_t c = 0xFFFFFFFF;
    for (int i = 0; i < n; i++) {
        c ^= (uint32_t)d[i] << 24;
        for (int b = 0; b < 8; b++) c = (c & 0x80000000) ? (c << 1) ^ 0x04C11DB7 : (c << 1);
    }
    return c;
}

/* 发一个只含 payload 的 PSI TS 包（section 短，单包内） */
static void emit_psi(ts_mux_t *m, uint16_t pid, uint8_t *cc, const uint8_t *sec, int seclen)
{
    uint8_t pkt[TS_PKT];
    memset(pkt, 0xFF, TS_PKT);
    pkt[0] = 0x47;
    pkt[1] = 0x40 | ((pid >> 8) & 0x1F);        /* PUSI=1 */
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x10 | (*cc & 0x0F);               /* payload only */
    *cc = (*cc + 1) & 0x0F;
    pkt[4] = 0x00;                              /* pointer field */
    memcpy(pkt + 5, sec, seclen);
    put(m, pkt, TS_PKT);
}

static void emit_pat(ts_mux_t *m)
{
    uint8_t s[16];
    int i = 0;
    s[i++] = 0x00;                 /* table_id PAT */
    s[i++] = 0xB0; s[i++] = 0x0D;  /* section_syntax=1, len=13 */
    s[i++] = 0x00; s[i++] = 0x01;  /* ts_id */
    s[i++] = 0xC1;                 /* version0, current */
    s[i++] = 0x00; s[i++] = 0x00;  /* sec no / last */
    s[i++] = 0x00; s[i++] = 0x01;  /* program 1 */
    s[i++] = 0xE0 | ((PID_PMT >> 8) & 0x1F); s[i++] = PID_PMT & 0xFF;
    uint32_t crc = crc32_mpeg(s, i);
    s[i++] = crc >> 24; s[i++] = crc >> 16; s[i++] = crc >> 8; s[i++] = crc;
    emit_psi(m, 0x0000, &m->cc_pat, s, i);
}

static void emit_pmt(ts_mux_t *m)
{
    uint8_t s[32];
    int i = 0;
    s[i++] = 0x02;                 /* table_id PMT */
    int len_pos = i; s[i++] = 0xB0; s[i++] = 0x00;   /* len 占位 */
    s[i++] = 0x00; s[i++] = 0x01;  /* program 1 */
    s[i++] = 0xC1; s[i++] = 0x00; s[i++] = 0x00;
    s[i++] = 0xE0 | ((PID_VIDEO >> 8) & 0x1F); s[i++] = PID_VIDEO & 0xFF; /* PCR PID=video */
    s[i++] = 0xF0; s[i++] = 0x00;  /* program_info_length=0 */
    /* video ES */
    s[i++] = m->h265 ? 0x24 : 0x1B;    /* HEVC / H264 */
    s[i++] = 0xE0 | ((PID_VIDEO >> 8) & 0x1F); s[i++] = PID_VIDEO & 0xFF;
    s[i++] = 0xF0; s[i++] = 0x00;
    /* audio ES (AAC ADTS = 0x0F) */
    s[i++] = 0x0F;
    s[i++] = 0xE0 | ((PID_AUDIO >> 8) & 0x1F); s[i++] = PID_AUDIO & 0xFF;
    s[i++] = 0xF0; s[i++] = 0x00;
    int seclen = i - (len_pos + 2) + 4;             /* 之后含 CRC4 */
    s[len_pos]   = 0xB0 | ((seclen >> 8) & 0x0F);
    s[len_pos+1] = seclen & 0xFF;
    uint32_t crc = crc32_mpeg(s, i);
    s[i++] = crc >> 24; s[i++] = crc >> 16; s[i++] = crc >> 8; s[i++] = crc;
    emit_psi(m, PID_PMT, &m->cc_pmt, s, i);
}

/* 写一个 PES（payload 为 ES），切成多 TS 包；首包 PUSI，keyframe 带 PCR+RAI。 */
static void emit_pes(ts_mux_t *m, uint16_t pid, uint8_t *cc, uint8_t sid,
                     const uint8_t *es, size_t eslen, uint64_t pts90k, int rai)
{
    /* PES 头 */
    uint8_t pes[19]; int ph = 0;
    pes[ph++] = 0x00; pes[ph++] = 0x00; pes[ph++] = 0x01; pes[ph++] = sid;
    int pes_payload = (int)eslen + 3 + 5;           /* opt hdr(3) + PTS(5) */
    int pkt_len = (sid == STREAM_VIDEO) ? 0 : pes_payload; /* 视频可置 0(不定长) */
    pes[ph++] = (pkt_len >> 8) & 0xFF; pes[ph++] = pkt_len & 0xFF;
    pes[ph++] = 0x80;                               /* '10', no scrambling */
    pes[ph++] = 0x80;                               /* PTS_DTS=10 */
    pes[ph++] = 0x05;                               /* PES_header_data_length */
    uint64_t p = pts90k;
    pes[ph++] = 0x21 | ((p >> 29) & 0x0E);
    pes[ph++] = (p >> 22) & 0xFF;
    pes[ph++] = 0x01 | ((p >> 14) & 0xFE);
    pes[ph++] = (p >> 7) & 0xFF;
    pes[ph++] = 0x01 | ((p << 1) & 0xFE);

    const uint8_t *parts[2] = { pes, es };
    size_t plen[2] = { (size_t)ph, eslen };
    int first = 1;
    size_t off0 = 0, off1 = 0;

    while (off0 < plen[0] || off1 < plen[1]) {
        uint8_t pkt[TS_PKT]; memset(pkt, 0xFF, TS_PKT);
        pkt[0] = 0x47;
        pkt[1] = (first ? 0x40 : 0x00) | ((pid >> 8) & 0x1F);
        pkt[2] = pid & 0xFF;
        int payload_start = 4;
        uint8_t adapt = 0;

        /* 首包且关键帧：加自适应字段带 PCR + RAI */
        if (first && rai && sid == STREAM_VIDEO) {
            adapt = 1;
            pkt[3] = 0x30 | (*cc & 0x0F);           /* adaptation + payload */
            uint8_t aflen_pos = 4;
            pkt[5] = 0x40;                          /* RAI=1 + PCR_flag 下设 */
            pkt[5] |= 0x10;                         /* PCR_flag */
            uint64_t pcr = pts90k;                  /* 简化：PCR=PTS */
            pkt[6] = (pcr >> 25) & 0xFF; pkt[7] = (pcr >> 17) & 0xFF;
            pkt[8] = (pcr >> 9) & 0xFF;  pkt[9] = (pcr >> 1) & 0xFF;
            pkt[10] = ((pcr & 1) << 7) | 0x7E; pkt[11] = 0x00;
            pkt[aflen_pos] = 1 + 6;                 /* flags(1)+PCR(6) */
            payload_start = 4 + 1 + (1 + 6);
        } else {
            pkt[3] = 0x10 | (*cc & 0x0F);           /* payload only */
        }
        *cc = (*cc + 1) & 0x0F;

        int space = TS_PKT - payload_start;
        int filled = 0;
        /* 需要填满 188；不足则用自适应填充。先算总剩余 */
        size_t remain = (plen[0] - off0) + (plen[1] - off1);
        if ((int)remain < space && !adapt) {
            /* 用自适应字段填充 */
            int stuff = space - (int)remain;
            pkt[3] = 0x30 | (pkt[3] & 0x0F);
            pkt[4] = stuff - 1;                      /* adaptation_field_length */
            if (stuff >= 2) pkt[5] = 0x00;
            for (int k = 6; k < 4 + stuff && k < TS_PKT; k++) pkt[k] = 0xFF;
            payload_start = 4 + stuff;
            space = TS_PKT - payload_start;
        }
        /* 拷 PES 头剩余 */
        while (off0 < plen[0] && filled < space) {
            int n = (int)(plen[0] - off0); if (n > space - filled) n = space - filled;
            memcpy(pkt + payload_start + filled, parts[0] + off0, n);
            off0 += n; filled += n;
        }
        while (off1 < plen[1] && filled < space) {
            int n = (int)(plen[1] - off1); if (n > space - filled) n = space - filled;
            memcpy(pkt + payload_start + filled, parts[1] + off1, n);
            off1 += n; filled += n;
        }
        put(m, pkt, TS_PKT);
        first = 0;
    }
}

static void ensure_psi(ts_mux_t *m)
{
    if (!m->wrote_psi) { emit_pat(m); emit_pmt(m); m->wrote_psi = 1; }
}

int ts_mux_write_video(ts_mux_t *m, const uint8_t *annexb, size_t len, uint64_t pts90k, int is_key)
{
    if (!m || !annexb || !len) return -1;
    ensure_psi(m);
    emit_pes(m, PID_VIDEO, &m->cc_v, STREAM_VIDEO, annexb, len, pts90k, is_key ? 1 : 0);
    return 0;
}
int ts_mux_write_audio(ts_mux_t *m, const uint8_t *adts, size_t len, uint64_t pts90k)
{
    if (!m || !adts || !len) return -1;
    ensure_psi(m);
    emit_pes(m, PID_AUDIO, &m->cc_a, STREAM_AUDIO, adts, len, pts90k, 0);
    return 0;
}

const uint8_t *ts_mux_data(ts_mux_t *m, size_t *len) { if (len) *len = m ? m->len : 0; return m ? m->buf : NULL; }
void ts_mux_reset(ts_mux_t *m) { if (m) { m->len = 0; m->wrote_psi = 0; } }
