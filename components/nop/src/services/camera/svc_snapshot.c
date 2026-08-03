/**
 * @file svc_snapshot.c
 * @brief Camera-role snapshot: capture a JPEG through HAL_VIDEO and write it to
 *        a file (e.g. Photograph.jpg on the SD root). See nop_sdk/nop_snapshot.h.
 */
#include "nop_sdk/nop_snapshot.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_video.h"

#include "base/nop_mem.h"

#include <stdio.h>

#ifndef NOP_SNAPSHOT_MAX_BYTES
#  define NOP_SNAPSHOT_MAX_BYTES (1024u * 1024u)   /* 1 MiB JPEG ceiling */
#endif

nop_status_t nop_snapshot_capture_to_file(int channel, int stream, const char *path)
{
    const hal_video_if *video = (const hal_video_if *)hal_registry_get(HAL_VIDEO);
    uint8_t            *buffer;
    size_t              length = 0;
    nop_status_t        status;
    FILE               *file;

    if (!path)
        return NOP_ERR_PARAM;
    if (!video || !video->capture_snapshot)
        return NOP_ERR_NOTIMPL;

    buffer = (uint8_t *)nop_malloc(NOP_SNAPSHOT_MAX_BYTES);
    if (!buffer)
        return NOP_ERR_NOMEM;

    status = video->capture_snapshot(video->ctx, channel, stream,
                                     buffer, NOP_SNAPSHOT_MAX_BYTES, &length);
    if (status != NOP_OK || length == 0) {
        nop_free(buffer);
        return (status != NOP_OK) ? status : NOP_ERR_IO;
    }

    file = fopen(path, "wb");
    if (!file) {
        nop_free(buffer);
        return NOP_ERR_IO;
    }
    if (fwrite(buffer, 1, length, file) != length) {
        fclose(file);
        nop_free(buffer);
        return NOP_ERR_IO;
    }
    fclose(file);
    nop_free(buffer);
    return NOP_OK;
}
