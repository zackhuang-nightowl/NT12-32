/* Copyright (C) 2025-2026, Nightowl DG. RSDK 工具: CRC32 / 对齐. */
#include "rsdk_types.h"

uint32_t rsdk_crc32(const void *data, size_t len)
{
    static uint32_t tab[256]; static int init = 0;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tab[i] = c;
        }
        init = 1;
    }
    const uint8_t *p = (const uint8_t*)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint64_t rsdk_align_up(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }
