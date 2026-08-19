/***************************************************************************************
 *  nvr_chan_nop_sync.c — 通道表与 nop_nvr_channels 同步（8089 ONVIF 映射 backend 查表）
 ***************************************************************************************/
#include "nvr_chan_nop_sync.h"
#include "nvr_channel.h"
#include <string.h>
#include <stdio.h>

void nvr_chan_to_nop_entry(const nvr_channel_t *d, nop_nvr_channel_entry_t *e)
{
    memset(e, 0, sizeof(*e));
    if (!d) return;
    e->channel = d->chn;
    e->enabled = d->enabled ? 1 : (d->chn >= 0);
    e->backend = (d->backend != 0) ? NOP_BACKEND_ONVIF : NOP_BACKEND_NOP;
    if (d->poe_port > 0)
        e->source = NOP_CAMERA_SOURCE_POE;
    else if (d->onvif_auto)
        e->source = NOP_CAMERA_SOURCE_ONVIF;
    else
        e->source = NOP_CAMERA_SOURCE_MANUAL;
    snprintf(e->host, sizeof(e->host), "%s", d->onvif_ip);
    e->port = d->onvif_port > 0 ? d->onvif_port : 80;
    snprintf(e->username, sizeof(e->username), "%s", d->user[0] ? d->user : "admin");
    snprintf(e->password, sizeof(e->password), "%s", d->pass);
    snprintf(e->name, sizeof(e->name), "%s", d->name);
    snprintf(e->video_source_token, sizeof(e->video_source_token), "%s", d->video_source_token);
    snprintf(e->service_url, sizeof(e->service_url), "%s", d->service_url);
}

static int entry_connect_changed(const nop_nvr_channel_entry_t *prev,
                                 const nop_nvr_channel_entry_t *next)
{
    int pp = prev->port > 0 ? prev->port : 80;
    int np = next->port > 0 ? next->port : 80;
    return strcmp(prev->host, next->host) != 0 || pp != np ||
           strcmp(prev->username, next->username) != 0 ||
           strcmp(prev->password, next->password) != 0 ||
           prev->backend != next->backend ||
           strcmp(prev->video_source_token, next->video_source_token) != 0;
}

void nvr_chan_nop_sync_upsert(nop_nvr_channels_t *reg, const nvr_channel_t *d,
                              nop_onvif_map_backend_t *onvif_be)
{
    nop_nvr_channel_entry_t prev, next;
    int had;

    if (!reg || !d || !d->onvif_ip[0]) return;
    nvr_chan_to_nop_entry(d, &next);
    had = nop_nvr_channels_get(reg, d->chn, &prev);
    nop_nvr_channels_upsert(reg, &next);
    if (onvif_be && (!had || entry_connect_changed(&prev, &next)))
        nop_onvif_map_invalidate_channel(onvif_be, d->chn, had ? &prev : NULL);
}

void nvr_chan_nop_sync_remove(nop_nvr_channels_t *reg, int chn,
                              nop_onvif_map_backend_t *onvif_be)
{
    nop_nvr_channel_entry_t prev;
    int had;

    if (!reg || chn < 0) return;
    had = nop_nvr_channels_get(reg, chn, &prev);
    nop_nvr_channels_remove(reg, chn);
    if (onvif_be && had)
        nop_onvif_map_invalidate_channel(onvif_be, chn, &prev);
}

void nvr_chan_nop_sync_all(nop_nvr_channels_t *reg, nvr_chan_mgr_t *cm)
{
    nvr_channel_t list[NVR_MAX_CH];
    int n, i;
    if (!reg || !cm) return;
    n = nvr_chan_list(cm, list, NVR_MAX_CH);
    for (i = 0; i < n; i++)
        nvr_chan_nop_sync_upsert(reg, &list[i], NULL);
}
