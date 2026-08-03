/* Copyright (C) 2025-2026, Nightowl DG. RSDK 特性控制模块(设计 §13). */
#include "rsdk_feature.h"

int rsdk_feature_on(rsdk_feature_id_t f)
{
    switch (f) {
    case RSDK_F_ENCRYPTION:        return RSDK_CFG_ENCRYPTION;
    case RSDK_F_METADATA:          return RSDK_CFG_METADATA;
    case RSDK_F_MULTIDISK_BALANCE: return RSDK_CFG_MULTIDISK_BALANCE;
    case RSDK_F_BACKUP_FMP4:       return RSDK_CFG_BACKUP_FMP4;
    default:                       return 0;
    }
}

uint32_t rsdk_feature_mask(void)
{
    uint32_t m = 0;
    if (RSDK_CFG_ENCRYPTION)        m |= RSDK_FEAT_ENCRYPTION;
    if (RSDK_CFG_METADATA)          m |= RSDK_FEAT_METADATA;
    if (RSDK_CFG_MULTIDISK_BALANCE) m |= RSDK_FEAT_MULTIDISK_BAL;
    if (RSDK_CFG_BACKUP_FMP4)       m |= RSDK_FEAT_BACKUP_FMP4;
    return m;
}
