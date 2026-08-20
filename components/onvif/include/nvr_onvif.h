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

/* 取流 URL（= app/src/nvr_app.h 声明的钩子；stream: "main"/"sub"）。成功填 out 返 0。
 * scopes_out(可空):顺带回传发现广播 scopes(供通道 nvr_dev_classify 分类 kind/mac)。 */
/* vsrc_token:多源设备该通道绑定的 VideoSourceToken(空=单源/首源)。解析时只在归属该源的
 * profile 里挑主/子,保证多源各通道拉到各自的源流。 */
int  nvr_onvif_get_url(const char *ip, int port, const char *user, const char *pass,
                       const char *stream, char *out, int out_size,
                       char *scopes_out, int scopes_cap, const char *vsrc_token);

/* 已连接后 GetNetworkInterfaces 取网口 MAC（HwAddress）。成功填 out 返 0。
 * 优先匹配该 ip 的 IPv4 口；ARP 未就绪时作补全，落库仍以 ARP 为准。 */
int  nvr_onvif_get_mac(const char *ip, int port, const char *user, const char *pass,
                       char *out, int cap);

/* 连接时建齐一机一个 OnvifDev（端点+profiles+token）。service_url 可空。0=ok。 */
int  nvr_onvif_connect(const char *ip, int port, const char *service_url,
                       const char *user, const char *pass);
void nvr_onvif_disconnect(const char *ip, int port);

/* 对该 IP 做一次 WS-Discovery，取 scopes / device_service（无需账密）。
 * 未激活 nopOnvif 只保证 Discovery。命中填 scopes 返 0。 */
int  nvr_onvif_probe_scopes(const char *ip, char *scopes, int scopes_cap,
                            char *service_url, int svc_cap, int *port_io);

/* 发现缓存:开机/周期"扫一次"(nvr_onvif_scan,一次广播缓存全部相机 scopes),各通道解析查缓存
 * (nvr_onvif_cached_scopes)免每路 2s probe(probe 全局串行)。cached 命中返 0,否则 -1 回落 probe。 */
int  nvr_onvif_scan(const char *local_ip, int seconds);
int  nvr_onvif_cached_scopes(const char *ip, char *scopes, int scopes_cap,
                             char *service_url, int svc_cap, int *port_io);

/* 已连接则 GetScopes SOAP；否则只靠 probe_scopes。优先 SOAP。0=有内容。 */
int  nvr_onvif_get_scopes(const char *ip, int port, const char *user, const char *pass,
                          char *out, int cap);

/* 设备探测结果(首次上线一次会话拿全:身份 + 主/子流 + 能力标识)。 */
typedef struct {
    char scopes[1024];   /* 1024:NightOwl nopOnvif/serial/nopState 排最后,512 会截断丢激活标记 */
    char manufacturer[64], model[64], firmware[64], serial[64];
    char main_uri[300], sub_uri[300];
    int  ptz;             /* PTZ 节点数>0 → 有云台 */
    int  time_set;        /* 已成功把 NVR 时间下发到相机 */
    /* --- getDeviceCapabilities 映射:通道能力(按 NOPMappingONVIF.md,Media2 ConfigurationSet + Analytics + PTZ) --- */
    int  cap_mic;         /* AudioSource 存在 → mic */
    int  cap_speaker;     /* AudioOutput 声明 && AudioDecoder(backchannel) → speaker(双向) */
    int  cap_sensor;      /* Analytics 配置存在 → sensor */
    int  cap_motion;      /* Analytics 含 CellMotion → sensor motion(pixelChange) */
    int  cap_objdet;      /* Analytics 含 ObjectDetection → sensor objectDetection */
    int  obj_human, obj_vehicle, obj_animal, obj_face;  /* objectDetection 类别 */
    /* PTZ 子能力(ptz[]) */
    int  ptz_presets;     /* 支持预置位(nodes) */
    int  ptz_tours;       /* 支持巡航(patrol) */
    int  ptz_focus;       /* 支持对焦(Imaging) */
    /* ruledDetection(AI_getChannelAICapabilities):越线/区域入侵 */
    int  line_cross, line_max, line_max_points;
    int  field_intrusion, field_max, field_max_verts;
} nvr_onvif_info_t;

/* 一次 ONVIF 会话:发现→建设备→GetDeviceInformation/GetServices/GetCapabilities→GetProfiles(主/子 URI)
 * →PTZ 节点检测→SetSystemDateAndTime(把 NVR 时间下发相机,setTime 走 ONVIF)。成功填 out 返 0。
 * 供"首次上线按设备构建能力级 + 校时"。stream 影响不大(主/子都取)。 */
int  nvr_onvif_probe(const char *ip, int port, const char *user, const char *pass, nvr_onvif_info_t *out);

/* 仅把 NVR 当前时间经 ONVIF 下发到指定相机(供命令触发的显式校时)。成功返 0。 */
/* ONVIF SetSystemDateAndTime 时区/DST(与 NVR system.timezone/tz_dst 对齐)。 */
typedef struct {
    char tz_posix[128];   /* TimeZone.TZ: 无夏令=固定 GMT 偏移; 有夏令=完整 tz_dst POSIX */
    int  daylight;        /* DaylightSavings: 0=关(忽略 TZ 内 DST 规则); 1=开(启用规则) */
} nvr_onvif_time_cfg_t;

int  nvr_onvif_set_time_now(const char *ip, int port, const char *user, const char *pass,
                            const nvr_onvif_time_cfg_t *tz_cfg);

/* 事件抓拍：借一机一 handle 做 GetSnapshot。成功时 *out 由 nop_onvif_free_buffer 释放。
 * vsrc_token 空=首源。超时约数秒；失败返 -1 且 *out=NULL。 */
int  nvr_onvif_get_snapshot(const char *ip, int port, const char *user, const char *pass,
                            const char *vsrc_token, unsigned char **out, int *out_len);

/* 发现回调：一台相机 */
typedef struct {
    char host[128];
    int  port;
    char service_url[128];
    char uuid[100];
    char scopes[1024];   /* 1024:同上,避免截断 nopState/inactive 等尾部 scope */
} nvr_onvif_cam_t;
typedef void (*nvr_onvif_found_cb)(const nvr_onvif_cam_t *cam, void *user);

/* WS-Discovery：在 local_ip 所在网段扫 seconds 秒，逐台回调。返回发现数。 */
int  nvr_onvif_discover(const char *local_ip, int seconds, nvr_onvif_found_cb cb, void *user);

#ifdef __cplusplus
}
#endif
#endif
