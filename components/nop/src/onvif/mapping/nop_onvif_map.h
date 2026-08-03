/**
 * @file nop_onvif_map.h  (internal)
 * @brief Facade of the NOP->ONVIF client mapping layer — the ONLY surface the
 *        business handlers (cap_*.c) and the app integrator (nop_app.c) call.
 *
 * The NVR is an ONVIF *client*: for a channel whose control backend is
 * NOP_BACKEND_ONVIF, a per-channel NOP command is translated to ONVIF client
 * calls (set: NOP->ONVIF) and the ONVIF reply is translated back into the NOP
 * response (get: ONVIF->NOP). Coordinate transforms live in onvif_coord.h; the
 * ONVIF wire calls live behind the C ABI (nop_onvif.h / nop_onvif_ext.h).
 *
 * Front-door pattern in a cap_*.c handler (per-channel commands):
 *     int ch = (int)nop_json_num(request->args, "channel", 0);
 *     if (nop_onvif_map_is_onvif(ctx, ch))
 *         return nop_onvif_map_dispatch(ctx, request, response);
 *     ... existing native / in-memory handling ...
 *
 * When the SDK is built without NOP_WITH_ONVIF, is_onvif() always returns 0 (so
 * handlers keep their native behavior) and the lifecycle calls are no-ops.
 */
#ifndef NOP_ONVIF_MAP_H
#define NOP_ONVIF_MAP_H

#include "nop/nop_envelope.h"   /* nop_request_t / nop_response_t */
#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_nvr_channels.h"  /* nop_camera_backend_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- §4 Device: add-camera backend classification ---------------------- */

/**
 * Classify a discovered camera's control backend from its ONVIF discovery
 * scopes (space-joined, as reported by WS-Discovery). NightOwl-native devices
 * advertise a "nopVersion" scope and are driven by the private NOP protocol
 * (NOP_BACKEND_NOP); everything else is driven through this ONVIF mapping layer
 * (NOP_BACKEND_ONVIF). Call it at add-camera time to stamp
 * nop_nvr_channel_entry_t.backend, which every per-channel front-door reads.
 * Pure; available in all builds (@p scopes may be NULL -> NOP_BACKEND_ONVIF).
 */
nop_camera_backend_t nop_onvif_map_classify_backend(const char *scopes);

/** Opaque ONVIF-client mapping backend: per-channel session cache + event poll. */
typedef struct nop_onvif_map_backend nop_onvif_map_backend_t;

/* ---- lifecycle (integrator / nop_app) ---------------------------------- */

/**
 * Create the mapping backend bound to a bound-camera registry.
 * @p channels is a nop_nvr_channels_t* (void* to avoid a header cycle); it must
 * outlive the backend. @return NULL on OOM or when built without NOP_WITH_ONVIF.
 */
nop_onvif_map_backend_t *nop_onvif_map_backend_create(void *channels);

/** Destroy the backend, closing every cached ONVIF session. NULL-safe. */
void nop_onvif_map_backend_destroy(nop_onvif_map_backend_t *backend);

/**
 * Start the ONVIF event poller: one thread per enabled ONVIF channel pulls
 * PullMessages and republishes mapped detections into @p event_hub
 * (nop_event_hub_t*), so they reach GUI_longPolling / 8012 / push unchanged.
 * @return NOP_OK, or NOP_ERR_NOTIMPL when built without NOP_WITH_ONVIF.
 */
nop_status_t nop_onvif_map_events_start(nop_onvif_map_backend_t *backend, void *event_hub);

/** Stop the ONVIF event poller. Safe to call when not started. */
void nop_onvif_map_events_stop(nop_onvif_map_backend_t *backend);

/* ---- front-door (cap_*.c) ---------------------------------------------- */

/**
 * @return 1 if @p ctx has an attached ONVIF backend AND @p channel is bound with
 * backend == NOP_BACKEND_ONVIF; 0 otherwise (including any build without ONVIF).
 * @p ctx is the nop_business_context_t* handed to handlers.
 */
int nop_onvif_map_is_onvif(void *ctx, int channel);

/**
 * Translate one per-channel NOP request to ONVIF and fill @p resp. Routes on
 * @p req->func to the matching domain mapper. Returns NOP_ERR_NOTIMPL if the
 * function has no ONVIF mapping (the caller already committed to the ONVIF path
 * via is_onvif, so this surfaces as 501). @p ctx is the business context.
 */
nop_status_t nop_onvif_map_dispatch(void *ctx, const nop_request_t *req,
                                    nop_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* NOP_ONVIF_MAP_H */
