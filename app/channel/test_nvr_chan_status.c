#include "nvr_chan_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static nvr_chan_substate_t z(void){ nvr_chan_substate_t s; memset(&s,0,sizeof(s)); return s; }

int main(void){
    nvr_chan_substate_t s;
    assert(nvr_chan_status_code(NVR_CONN_NOCAM, (s=z(),&s)) == 0);
    assert(nvr_chan_status_code(NVR_CONN_ONLINE, (s=z(),&s)) == 1);
    assert(nvr_chan_status_code(NVR_CONN_CONNECTING, (s=z(),&s)) == 3);
    s=z(); s.auth_fail=1;   assert(nvr_chan_status_code(NVR_CONN_CONNECTING,&s)==4);
    s=z(); s.out_of_res=1;  assert(nvr_chan_status_code(NVR_CONN_ONLINE,&s)==5);
    s=z(); s.inactive=1;    assert(nvr_chan_status_code(NVR_CONN_CONNECTING,&s)==7);
    s=z(); s.sleeping=1;    assert(nvr_chan_status_code(NVR_CONN_ONLINE,&s)==2);
    s=z(); s.fw_updating=1; assert(nvr_chan_status_code(NVR_CONN_ONLINE,&s)==6);
    /* 优先级：inactive(7) 压过 auth_fail(4) */
    s=z(); s.inactive=1; s.auth_fail=1; assert(nvr_chan_status_code(NVR_CONN_CONNECTING,&s)==7);
    /* 优先级：auth_fail(4) 压过 fw_updating(6)? 表为 7>4>6>2>5>3 → 4 胜 */
    s=z(); s.auth_fail=1; s.fw_updating=1; assert(nvr_chan_status_code(NVR_CONN_CONNECTING,&s)==4);
    printf("test_nvr_chan_status: ALL PASS\n");
    return 0;
}
