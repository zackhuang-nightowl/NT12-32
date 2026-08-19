/***************************************************************************************
 *  nvr_http_assets.h — 8089 GET 静态资源:通道抓拍 JPEG + 事件下载 MP4。
 *  App 经 iotc-tunnel 映射本机 nop 口取文件;命令层只回 URL。
 ***************************************************************************************/
#ifndef NVR_HTTP_ASSETS_H
#define NVR_HTTP_ASSETS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rsdk_group;

int  nvr_http_assets_put_jpeg(int chn0, const void *jpeg, int len);
int  nvr_http_assets_get_jpeg(int chn0, const void **jpeg, int *len); /* 锁外勿持指针过久 */

/* 启动动态转封装。process_id 由调用方填好。返回 0=已排队。 */
int  nvr_http_dl_start(struct rsdk_group *group, const char *process_id,
                       int chn0, uint32_t t0, uint32_t t1, int stream);
/* 查进度。url_token 供拼 downloadUrl(无空格)。filesize 仅 percent==100。 */
int  nvr_http_dl_progress(const char *process_id, int *percent,
                          char *url_token, int token_cap, int *filesize);

/* GET 拦截: /snapshot/chN.jpg 或 /download/<token>.mp4。已写应答返回 1。 */
int  nvr_http_assets_serve(int fd, const char *uri);

#ifdef __cplusplus
}
#endif
#endif
