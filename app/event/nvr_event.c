/***************************************************************************************
 *  nvr_event.c — 事件中枢。见 nvr_event.h / 计划 §B4。
 ***************************************************************************************/
#include "nvr_event.h"
#include "nvr_log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EVT_MAX_CH 32
#define ICON_DECAY_S 5        /* 无事件多少秒后清图标 */

struct nvr_evt_hub {
    nvr_evt_cfg_t cfg;
    struct { unsigned bits; time_t last; } icon[EVT_MAX_CH];
};

int nvr_evt_rectype_of(nop_detect_type_t type)
{
    switch (type) {
        case NOP_DETECT_MOTION:          return RSDK_REC_MOTION;
        case NOP_DETECT_PIXEL_CHANGE:    return RSDK_REC_MOTION;
        case NOP_DETECT_HUMAN:           return RSDK_REC_HUMAN;
        case NOP_DETECT_FACE:            return RSDK_REC_FACE;
        case NOP_DETECT_VEHICLE:         return RSDK_REC_VEHICLE;
        case NOP_DETECT_LINE_CROSS:      return RSDK_REC_LINECROSS;
        case NOP_DETECT_FIELD_INTRUSION: return RSDK_REC_INTRUSION;
        case NOP_DETECT_ANIMAL:          return RSDK_REC_ANIMAL;
        case NOP_DETECT_PACKAGE:         return RSDK_REC_PACKAGE;
        case NOP_DETECT_DOORBELL_RING:   return RSDK_REC_DOORBELL;
        default:                         return -1;
    }
}

static unsigned icon_of(nop_detect_type_t type)
{
    switch (type) {
        case NOP_DETECT_MOTION:
        case NOP_DETECT_PIXEL_CHANGE: return NVR_ICON_MOTION;
        case NOP_DETECT_HUMAN:        return NVR_ICON_HUMAN;
        case NOP_DETECT_FACE:         return NVR_ICON_FACE;
        default:                      return 0;
    }
}

int nvr_evt_init(const nvr_evt_cfg_t *cfg, nvr_evt_hub_t **out)
{
    if (!cfg || !out) return -1;
    nvr_evt_hub_t *h = calloc(1, sizeof(*h));
    if (!h) return -1;
    h->cfg = *cfg;
    *out = h;
    return 0;
}

void nvr_evt_deinit(nvr_evt_hub_t *h) { if (h) free(h); }

int nvr_evt_ingest(nvr_evt_hub_t *h, int chn, nop_detect_type_t type, uint64_t ts_ms)
{
    if (!h || chn < 0 || chn >= EVT_MAX_CH) return -1;

    /* 1) 发布到 nop 事件脊柱：自动扇出到 8012-server / ONVIF 桥 / longPolling / 推送 / queryEventList */
    if (h->cfg.nop_hub) {
        nop_event_t ev; memset(&ev, 0, sizeof(ev));
        ev.channel = chn; ev.type = type; ev.timestamp_ms = ts_ms;
        nop_event_publish(h->cfg.nop_hub, &ev);
    }

    /* 2) 本地扇出：事件录像触发 */
    int rectype = nvr_evt_rectype_of(type);
    NVR_LOGI("event", "ch%d AI事件 type=%d → rectype=%d", chn, (int)type, rectype);
    if (rectype >= 0 && h->cfg.rs)
        nvr_rec_trigger_event(h->cfg.rs, chn, rectype, (uint32_t)(ts_ms / 1000));

    /* 3) 预览状态图标 */
    unsigned bit = icon_of(type);
    if (bit) {
        h->icon[chn].bits |= bit;
        h->icon[chn].last = time(NULL);
        if (h->cfg.on_icon) h->cfg.on_icon(h->cfg.user, chn, h->icon[chn].bits);
    }
    return 0;
}

void nvr_evt_tick(nvr_evt_hub_t *h)
{
    if (!h) return;
    time_t now = time(NULL);
    for (int i = 0; i < EVT_MAX_CH; i++) {
        if (h->icon[i].bits && now - h->icon[i].last >= ICON_DECAY_S) {
            h->icon[i].bits = 0;
            if (h->cfg.on_icon) h->cfg.on_icon(h->cfg.user, i, 0);
        }
    }
}
