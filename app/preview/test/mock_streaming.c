#include "nvr_streaming.h"
#include "mock_mhal_vout.h"
rsdk_err_t nvr_stream_switch_stream(nvr_stream_mgr_t *m,int chn,int stream,const char*url){
    (void)m;(void)url; mhalmock_stream_switch[mhalmock_nswitch][0]=chn;
    mhalmock_stream_switch[mhalmock_nswitch][1]=stream; mhalmock_nswitch++; return 0; }
