/**
 * @file nop_snapshot.h
 * @brief Camera-role snapshot helper — capture a JPEG via HAL_VIDEO and write
 *        it to a file. Firmware calls it on the documented triggers (SD card
 *        inserted, or at boot when a card is present) to drop Photograph.jpg on
 *        the SD-card root. Works in standalone / paired / unpaired modes.
 */
#ifndef NOP_SDK_SNAPSHOT_H
#define NOP_SDK_SNAPSHOT_H

#include "nop_sdk/nop_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Capture a JPEG snapshot of @p channel/@p stream (0=main,1=sub) via
 * hal_video_if.capture_snapshot and write it to @p path (overwriting).
 * @return NOP_OK, NOP_ERR_NOTIMPL if HAL_VIDEO/capture_snapshot is absent,
 * NOP_ERR_IO on file/capture failure, or NOP_ERR_PARAM.
 */
nop_status_t nop_snapshot_capture_to_file(int channel, int stream, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_SNAPSHOT_H */
