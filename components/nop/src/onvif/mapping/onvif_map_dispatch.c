/**
 * @file onvif_map_dispatch.c
 * @brief Front-door of the NOP->ONVIF mapping layer: is_onvif() gate + the
 *        func-name -> domain-mapper routing table. Backend lifecycle lives in
 *        onvif_map_session.c (NOP_ONVIF_MAP builds).
 *
 * Built two ways from one file:
 *   - NOP_ONVIF_MAP on : real routing; is_onvif reads the channel's backend.
 *   - NOP_ONVIF_MAP off: is_onvif()==0 (handlers stay native), dispatch()==501,
 *     and the lifecycle entry points are no-ops — so the SDK links with no ONVIF.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "business/business.h"
#include "nop_sdk/nop_nvr_channels.h"
#include "base/nop_json.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Look the NOP func up in the single authoritative table (onvif_map_table.c). */
static onvif_mapper_fn find_mapper(const char *func)
{
    int i;
    if (!func)
        return NULL;
    for (i = 0; i < g_onvif_map_table_len; i++) {
        if (strcmp(g_onvif_map_table[i].func, func) == 0)
            return g_onvif_map_table[i].fn;
    }
    return NULL;
}

int nop_onvif_map_is_onvif(void *ctx, int channel)
{
    nop_business_context_t *bc = (nop_business_context_t *)ctx;
    nop_nvr_channel_entry_t entry;

    if (!bc || !bc->onvif_backend || !bc->channels)
        return 0;
    if (!nop_nvr_channels_get(bc->channels, channel, &entry))
        return 0;
    return entry.backend == NOP_BACKEND_ONVIF ? 1 : 0;
}

nop_status_t nop_onvif_map_dispatch(void *ctx, const nop_request_t *req,
                                    nop_response_t *resp)
{
    nop_business_context_t *bc = (nop_business_context_t *)ctx;
    onvif_mapper_fn         fn;
    int                     channel;

    if (!bc || !bc->onvif_backend || !req) {
        fprintf(stderr, "[onvif_map] dispatch '%s': no onvif_backend/ctx → 501\n",
                (req && req->func) ? req->func : "?");
        return NOP_ERR_NOTIMPL;
    }
    fn = find_mapper(req->func);
    if (!fn) {
        fprintf(stderr, "[onvif_map] dispatch '%s': func NOT in map table → 501\n", req->func);
        return NOP_ERR_NOTIMPL;
    }
    channel = (int)nop_json_num(req->args, "channel", 0);
    {
        struct timespec t0, t1;
        nop_status_t rc;
        long ms;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        rc = fn((nop_onvif_map_backend_t *)bc->onvif_backend, channel, req, resp);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms = (t1.tv_sec - t0.tv_sec) * 1000L + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
        fprintf(stderr, "[onvif_map] dispatch '%s' ch=%d → rc=%d %ldms\n",
                req->func, channel, (int)rc, ms);
        return rc;
    }
}

#else /* !NOP_ONVIF_MAP — ONVIF compiled out: keep the SDK linkable & native. */

nop_onvif_map_backend_t *nop_onvif_map_backend_create(void *channels)
{
    (void)channels;
    return NULL;
}
void nop_onvif_map_backend_destroy(nop_onvif_map_backend_t *be) { (void)be; }

nop_status_t nop_onvif_map_events_start(nop_onvif_map_backend_t *be, void *hub)
{
    (void)be; (void)hub;
    return NOP_ERR_NOTIMPL;
}
void nop_onvif_map_events_stop(nop_onvif_map_backend_t *be) { (void)be; }

void nop_onvif_map_invalidate_channel(nop_onvif_map_backend_t *be, int channel,
                                      const nop_nvr_channel_entry_t *prev)
{
    (void)be; (void)channel; (void)prev;
}

int nop_onvif_map_is_onvif(void *ctx, int channel)
{
    (void)ctx; (void)channel;
    return 0;
}
nop_status_t nop_onvif_map_dispatch(void *ctx, const nop_request_t *req,
                                    nop_response_t *resp)
{
    (void)ctx; (void)req; (void)resp;
    return NOP_ERR_NOTIMPL;
}

#endif /* NOP_ONVIF_MAP */
