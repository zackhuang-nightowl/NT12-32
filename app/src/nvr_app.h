/***************************************************************************************
 *  nvr_app.h — 整机集成层：编排 config → storage → platform → streaming → recorder
 *
 *  启动时序（见 app/README.md §启动时序）：
 *    load config → storage(scan/assemble 盘组) → media_hal(vout) → streaming(加通道/起流)
 ***************************************************************************************/
#ifndef NVR_APP_H
#define NVR_APP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_app nvr_app_t;

int  nvr_app_start(const char *config_dir, nvr_app_t **out);  /* 加载+起全链路; 0=ok */
void nvr_app_run  (nvr_app_t *app);                           /* 主循环: 维护/重连, 收到停止信号返回 */
void nvr_app_stop (nvr_app_t *app);                           /* 停并释放 */
void nvr_app_request_exit(nvr_app_t *app);                    /* 供信号处理触发退出 */

/* ONVIF 取流 URL 钩子（② onvif 模块实现；未实现时弱符号返回 -1，通道保持待定）。
 * stream: "main"/"sub"。成功填 out 并返回 0。
 * scopes_out(可空):顺带回传发现广播的 scopes(供通道分类 kind/mac);scopes_cap 为其容量。 */
int  nvr_onvif_get_url(const char *ip, int port, const char *user, const char *pass,
                       const char *stream, char *out, int out_size,
                       char *scopes_out, int scopes_cap, const char *vsrc_token);

#ifdef __cplusplus
}
#endif
#endif
