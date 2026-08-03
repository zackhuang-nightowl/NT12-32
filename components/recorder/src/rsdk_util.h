/* Copyright (C) 2025-2026, Nightowl DG. RSDK 内部工具. */
#ifndef RSDK_UTIL_INT_H
#define RSDK_UTIL_INT_H
#include "rsdk_types.h"
uint32_t rsdk_crc32(const void *data, size_t len);
uint64_t rsdk_align_up(uint64_t v, uint64_t a);
#endif
