/* http_vsaas.h — 云存 VSaaS HTTP：GET stream_url + multipart POST ts。见 cloudRec 文档。 */
#ifndef HTTP_VSAAS_H
#define HTTP_VSAAS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GET stream_url 返回。err_code: 200 OK；-1002/-1003/-1004=不应再上传（上层强关）。 */
typedef struct {
    int  http_ok;             /* 1=拿到上传 url */
    int  err_code;            /* 服务器 error.code（0=无） */
    char url[1024];           /* 实际上传 url（含 OTP，事件内复用 ~300s） */
    int  event_recording_max_length;  /* 合约最大秒（默认 300） */
} vsaas_url_t;

/* host: NULL 用内置(stage/prod)。starttime 为**埋通道后**的 10 位；event_id 为数字码；
 * tags 为逗号串（可含 starttimeOrgx / durationN）。 */
int vsaas_get_url(int stage, const char *udid, const char *stoken,
                  uint32_t starttime_embedded, int event_id_code, const char *tags,
                  vsaas_url_t *out);

/* multipart POST 一个 ts 切片到 get_url 得到的 url。
 * file_start_ms/file_dur_ms 为切片开始(13 位 ms)与时长；event_end=1 时 filename 加 E。 */
int vsaas_post_ts(const char *upload_url,
                  const uint8_t *ts, size_t ts_len,
                  uint64_t file_start_ms, uint32_t file_dur_ms, int event_end);

/* Update Tags（补 duration/eventTypes）：GET stream_event。 */
int vsaas_update_tags(int stage, const char *udid, const char *stoken,
                      uint32_t starttime_embedded, const char *tags);

/* 进程级 init/cleanup（curl_global_*）。 */
int  vsaas_http_init(void);
void vsaas_http_cleanup(void);

#ifdef __cplusplus
}
#endif
#endif /* HTTP_VSAAS_H */
