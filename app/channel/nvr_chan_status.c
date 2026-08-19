#include "nvr_chan_status.h"
int nvr_chan_status_code(nvr_conn_t conn, const nvr_chan_substate_t *s){
    if(s){
        if(s->inactive)    return 7;   /* 未激活 */
        if(s->auth_fail)   return 4;   /* 鉴权失败 / 等用户密码 */
        if(s->fw_updating) return 6;
        if(s->sleeping)    return 2;
        if(s->out_of_res)  return 5;
    }
    if(conn==NVR_CONN_ONLINE)     return 1;   /* 出图 */
    if(conn==NVR_CONN_CONNECTING) return 3;   /* 已有物理设备，未出图/连不上 */
    return 0;                                 /* 没设备 */
}
