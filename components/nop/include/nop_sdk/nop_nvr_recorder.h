/**
 * @file nop_nvr_recorder.h
 * @brief videoRecorder-role multi-channel recording manager. Runs one
 *        per-channel SD recording engine (nop_record.h) for each managed
 *        channel, all fed by the shared media plane and detection hub, and
 *        spreads channels across the configured storage disks.
 *
 * Role: VIDEO RECORDER (NOP_ROLE_NVR). An NVR records several cameras at once:
 * this manager owns a nop_record_t per channel (each filters the shared
 * nop_media hub by its channel), assigns each channel a storage root from the
 * disk pool round-robin (segment filenames already carry the channel, so
 * channels may share a disk without collision), and exposes each channel's
 * segment index for playback/download/query.
 *
 * Wiring:
 *   nop_nvr_recorder_config_t cfg; memset(&cfg,0,sizeof cfg);
 *   cfg.mode = NOP_RECORD_CONTINUOUS;
 *   strcpy(cfg.storage_roots[0], "/mnt/disk0"); cfg.storage_root_count = 1;
 *   nop_nvr_recorder_t *nvr = nop_nvr_recorder_create(&cfg, media, hub);
 *   nop_nvr_recorder_add_channel(nvr, 0);
 *   nop_nvr_recorder_add_channel(nvr, 1);
 *   ...
 *   nop_nvr_recorder_destroy(nvr);
 *
 * Threading: the management calls (add/remove/query/destroy) are expected from
 * a single control thread. The per-channel engines' interaction with the media
 * plane and detection hub is fully thread-safe (each engine locks internally
 * and drains in-flight sinks on teardown); only the manager's channel table is
 * assumed to be mutated by one thread at a time.
 */
#ifndef NOP_SDK_NVR_RECORDER_H
#define NOP_SDK_NVR_RECORDER_H

#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_record.h"
#include "nop_sdk/nop_media.h"
#include "nop_sdk/nop_event.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NOP_NVR_MAX_DISKS
#  define NOP_NVR_MAX_DISKS 8
#endif
#ifndef NOP_NVR_MAX_CHANNELS
#  define NOP_NVR_MAX_CHANNELS 16
#endif

/**
 * Manager configuration; a template applied to every channel plus the disk
 * pool. Zero-initialize, then set at least one storage root. Fields left 0 take
 * the per-channel engine defaults (see nop_record.h).
 */
typedef struct nop_nvr_recorder_config {
    nop_record_mode_t mode;
    int  stream;                    /**< which stream to record (0=main, 1=sub) */
    int  segment_seconds;           /**< continuous rotation period */
    int  max_segments_per_channel;  /**< Overwrite cap, per channel */
    int  pre_record_seconds;
    int  post_record_seconds;
    int  event_max_seconds;

    char storage_roots[NOP_NVR_MAX_DISKS][256]; /**< disk mount dirs */
    int  storage_root_count;        /**< number of valid entries in storage_roots (>=1) */

    /**
     * Capacity-aware placement. When non-zero and HAL_STORAGE is available, each
     * new channel is placed on the configured disk whose paired volume
     * (storage_volume_names[i]) is READY and has the most free space; if no
     * volume matches / HAL is absent, placement falls back to round-robin.
     */
    int  capacity_aware_placement;
    char storage_volume_names[NOP_NVR_MAX_DISKS][16]; /**< HAL_STORAGE volume name for storage_roots[i] */

    uint32_t reserved[4];
} nop_nvr_recorder_config_t;

/** Opaque manager. */
typedef struct nop_nvr_recorder nop_nvr_recorder_t;

/**
 * Create the manager from @p config (copied). @p media and @p hub are the
 * shared media plane and detection hub every channel engine subscribes to;
 * both are borrowed and must outlive the manager (@p hub may be NULL to record
 * without event tagging/triggering). Returns NULL on bad config (no storage
 * root / NULL media) or allocation failure.
 */
nop_nvr_recorder_t *nop_nvr_recorder_create(const nop_nvr_recorder_config_t *config,
                                            nop_media_t *media, nop_event_hub_t *hub);

/** Destroy the manager: stop and free every channel engine. NULL-safe. */
void nop_nvr_recorder_destroy(nop_nvr_recorder_t *manager);

/**
 * Start recording @p channel: create its engine on the next storage disk
 * (round-robin) and attach it to the shared media/hub. @return NOP_OK;
 * NOP_ERR_CONFLICT if the channel is already managed; NOP_ERR_STATE if the
 * channel table is full or engine creation/attach fails.
 */
nop_status_t nop_nvr_recorder_add_channel(nop_nvr_recorder_t *manager, int channel);

/** Stop recording @p channel and free its engine. @return NOP_OK, or
 *  NOP_ERR_NOTFOUND if the channel is not managed. */
nop_status_t nop_nvr_recorder_remove_channel(nop_nvr_recorder_t *manager, int channel);

/** @return number of channels currently managed. */
int nop_nvr_recorder_channel_count(const nop_nvr_recorder_t *manager);

/**
 * Copy up to @p max recent segments (newest first) for @p channel into @p out.
 * @return the count written, or 0 if the channel is not managed.
 */
int nop_nvr_recorder_segments(nop_nvr_recorder_t *manager, int channel,
                              nop_record_segment_t *out, int max);

/**
 * Copy the storage root assigned to @p channel into @p out (capacity
 * @p out_cap). @return non-zero if the channel is managed (root copied), 0
 * otherwise. Copies out rather than exposing internal storage, so the result
 * stays valid after the channel is later removed or the manager destroyed.
 */
int nop_nvr_recorder_channel_storage(nop_nvr_recorder_t *manager, int channel,
                                     char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_NVR_RECORDER_H */
