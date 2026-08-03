#include "nvr_chan_status.h"
int nvr_chan_status_code(nvr_conn_t conn, const nvr_chan_substate_t *s){
    if(s){
        if(s->inactive)    return 7;
        if(s->auth_fail)   return 4;
        if(s->fw_updating) return 6;
        if(s->sleeping)    return 2;
        if(s->out_of_res)  return 5;
    }
    if(conn==NVR_CONN_CONNECTING) return 3;
    if(conn==NVR_CONN_ONLINE)     return 1;
    return 0;
}
