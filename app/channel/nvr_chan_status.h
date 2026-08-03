#ifndef NVR_CHAN_STATUS_H
#define NVR_CHAN_STATUS_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { NVR_CONN_NOCAM=0, NVR_CONN_ONLINE, NVR_CONN_CONNECTING } nvr_conn_t;
typedef struct { int auth_fail, out_of_res, inactive, sleeping, fw_updating; } nvr_chan_substate_t;
/* 返回 getChannelStatus 的 0-7；优先级 7>4>6>2>5>3>1>0。 */
int nvr_chan_status_code(nvr_conn_t conn, const nvr_chan_substate_t *sub);
#ifdef __cplusplus
}
#endif
#endif
