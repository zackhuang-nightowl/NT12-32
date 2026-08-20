/* cloud_sync.h — uploader 内部：同步上传引擎接口。 */
#ifndef CLOUD_SYNC_H
#define CLOUD_SYNC_H

#include <stdint.h>

struct nvr_cloud_uploader;

#ifdef __cplusplus
extern "C" {
#endif

void cloud_sync_attach(struct nvr_cloud_uploader *up);
void cloud_sync_detach(void);

int  cloud_sync_mode(const struct nvr_cloud_uploader *up);

int  cloud_sync_event_begin(struct nvr_cloud_uploader *up, int chn, uint64_t eid,
                            uint32_t starttime, uint32_t rectype, int rec_stream,
                            const char *stoken);
int  cloud_sync_event_end(struct nvr_cloud_uploader *up, uint64_t eid, uint32_t end_epoch,
                          const char *stoken);
void cloud_sync_drain(struct nvr_cloud_uploader *up, const char *stoken);

#ifdef __cplusplus
}
#endif
#endif /* CLOUD_SYNC_H */
