/***************************************************************************************
 *  nvr_event.c — 事件中枢。见 nvr_event.h / 计划 §B4。
 *
 *  ★ 作为 nop 事件脊柱(nop_hub)的**订阅者**:相机 ONVIF 事件与本地 AI 事件都经 nop_event_publish
 *  进 nop_hub → 本中枢 sink 统一处理:①每通道每类型状态位(供 GUI_longPolling 的 Motion/Human/Face/Car
 *  位图)②事件录像触发 ③预览图标。此前 ONVIF 事件直接进 nop_hub、绕过本中枢 → longPolling 恒 0
 *  且不触发事件录像;订阅后两者都通。
 ***************************************************************************************/
#include "nvr_event.h"
#include "nvr_log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define EVT_MAX_CH 32
#define ICON_DECAY_S 5        /* 无事件多少秒后清状态位/图标 */

struct nvr_evt_hub {
    nvr_evt_cfg_t cfg;
    pthread_mutex_t lock;                 /* sink 在发布线程更新;longPolling 在命令线程读 */
    nop_event_subscription_t *sub;        /* nop_hub 订阅句柄 */
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

/* 事件类型 → GUI_longPolling 的四类状态位。motion/human/face/car 对应 GUI 四个位图。 */
static unsigned icon_of(nop_detect_type_t type)
{
    switch (type) {
        case NOP_DETECT_MOTION:
        case NOP_DETECT_PIXEL_CHANGE:    return NVR_ICON_MOTION;
        case NOP_DETECT_HUMAN:
        case NOP_DETECT_FIELD_INTRUSION:
        case NOP_DETECT_LINE_CROSS:      return NVR_ICON_HUMAN;   /* 人/越线/入侵 → 人形 */
        case NOP_DETECT_FACE:
        case NOP_DETECT_FACIAL_RECOGNITION: return NVR_ICON_FACE;
        case NOP_DETECT_VEHICLE:         return NVR_ICON_CAR;
        default:                         return 0;
    }
}

/* 统一事件处理(nop_hub 发布线程回调):置状态位 + 事件录像触发 + 预览图标。 */
static void evt_sink(void *sink_ctx, const nop_event_t *ev)
{
    nvr_evt_hub_t *h = (nvr_evt_hub_t *)sink_ctx;
    if (!h || !ev || ev->channel < 0 || ev->channel >= EVT_MAX_CH) return;
    int chn = ev->channel;
    unsigned bit = icon_of(ev->type);
    int rectype = nvr_evt_rectype_of(ev->type);

    unsigned newbits = 0;
    if (bit) {
        pthread_mutex_lock(&h->lock);
        h->icon[chn].bits |= bit;
        h->icon[chn].last = time(NULL);
        newbits = h->icon[chn].bits;
        pthread_mutex_unlock(&h->lock);
        if (h->cfg.on_icon) h->cfg.on_icon(h->cfg.user, chn, newbits);
    }
    if (rectype >= 0 && h->cfg.rs)
        nvr_rec_trigger_event(h->cfg.rs, chn, rectype, (uint32_t)(ev->timestamp_ms / 1000));
    NVR_LOGI("event", "ch%d AI事件 type=%d → rectype=%d bits=0x%x", chn, (int)ev->type, rectype, newbits);
}

int nvr_evt_init(const nvr_evt_cfg_t *cfg, nvr_evt_hub_t **out)
{
    if (!cfg || !out) return -1;
    nvr_evt_hub_t *h = calloc(1, sizeof(*h));
    if (!h) return -1;
    h->cfg = *cfg;
    pthread_mutex_init(&h->lock, NULL);
    /* 订阅事件脊柱:相机 ONVIF + 本地 AI 事件都经此回调统一处理。 */
    if (cfg->nop_hub) h->sub = nop_event_subscribe(cfg->nop_hub, evt_sink, h);
    *out = h;
    return 0;
}

void nvr_evt_deinit(nvr_evt_hub_t *h)
{
    if (!h) return;
    if (h->cfg.nop_hub && h->sub) nop_event_unsubscribe(h->cfg.nop_hub, h->sub);
    pthread_mutex_destroy(&h->lock);
    free(h);
}

/* 本地(app 侧)产生事件:发布到脊柱 → evt_sink 统一处理(状态位/录像/图标)。 */
int nvr_evt_ingest(nvr_evt_hub_t *h, int chn, nop_detect_type_t type, uint64_t ts_ms)
{
    if (!h || chn < 0 || chn >= EVT_MAX_CH) return -1;
    if (h->cfg.nop_hub) {
        nop_event_t ev; memset(&ev, 0, sizeof(ev));
        ev.channel = chn; ev.type = type; ev.timestamp_ms = ts_ms;
        nop_event_publish(h->cfg.nop_hub, &ev);   /* → evt_sink */
    } else {
        evt_sink(h, &(nop_event_t){ .channel = chn, .type = type, .timestamp_ms = ts_ms });
    }
    return 0;
}

/* GUI_longPolling 用:四类状态的**每通道位图**(bit chn=通道 chn+1 近 ICON_DECAY_S 秒内有该类事件)。 */
void nvr_evt_masks(nvr_evt_hub_t *h, uint32_t *motion, uint32_t *human, uint32_t *face, uint32_t *car)
{
    uint32_t m = 0, hu = 0, f = 0, c = 0;
    if (h) {
        pthread_mutex_lock(&h->lock);
        for (int i = 0; i < EVT_MAX_CH; i++) {
            unsigned b = h->icon[i].bits;
            if (b & NVR_ICON_MOTION) m  |= (1u << i);
            if (b & NVR_ICON_HUMAN)  hu |= (1u << i);
            if (b & NVR_ICON_FACE)   f  |= (1u << i);
            if (b & NVR_ICON_CAR)    c  |= (1u << i);
        }
        pthread_mutex_unlock(&h->lock);
    }
    if (motion) *motion = m; if (human) *human = hu; if (face) *face = f; if (car) *car = c;
}

void nvr_evt_tick(nvr_evt_hub_t *h)
{
    if (!h) return;
    time_t now = time(NULL);
    pthread_mutex_lock(&h->lock);
    for (int i = 0; i < EVT_MAX_CH; i++) {
        if (h->icon[i].bits && now - h->icon[i].last >= ICON_DECAY_S) {
            h->icon[i].bits = 0;
            pthread_mutex_unlock(&h->lock);
            if (h->cfg.on_icon) h->cfg.on_icon(h->cfg.user, i, 0);
            pthread_mutex_lock(&h->lock);
        }
    }
    pthread_mutex_unlock(&h->lock);
}
