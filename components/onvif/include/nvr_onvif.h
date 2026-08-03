/***************************************************************************************
 *  nvr_onvif.h — ② ONVIF glue（NVR 侧）
 *
 *  实现两件事：
 *   1) nvr_onvif_get_url() —— app 编排里的取流钩子（强符号覆盖 app 的弱兜底）：
 *      给 ip/port/user/pass → 取 profiles → GetStreamUri，PoE/自动发现通道由此点亮。
 *   2) nvr_onvif_discover() —— WS-Discovery 扫网段，回调候选相机给 app/channel。
 *
 *  底层复用 nop_sdk 已封装的 ONVIF 客户端（components/nop 的 nop_onvif_*，包 Happytime）。
 ***************************************************************************************/
#ifndef NVR_ONVIF_H
#define NVR_ONVIF_H

#ifdef __cplusplus
extern "C" {
#endif

/* 全局 init/cleanup（幂等；get_url 内部会惰性 init） */
int  nvr_onvif_init(void);
void nvr_onvif_cleanup(void);

/* 取流 URL（= app/src/nvr_app.h 声明的钩子；stream: "main"/"sub"）。成功填 out 返 0。 */
int  nvr_onvif_get_url(const char *ip, int port, const char *user, const char *pass,
                       const char *stream, char *out, int out_size);

/* 发现回调：一台相机 */
typedef struct {
    char host[128];
    int  port;
    char uuid[100];
    char scopes[512];
} nvr_onvif_cam_t;
typedef void (*nvr_onvif_found_cb)(const nvr_onvif_cam_t *cam, void *user);

/* WS-Discovery：在 local_ip 所在网段扫 seconds 秒，逐台回调。返回发现数。 */
int  nvr_onvif_discover(const char *local_ip, int seconds, nvr_onvif_found_cb cb, void *user);

#ifdef __cplusplus
}
#endif
#endif
