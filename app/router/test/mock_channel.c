#include "nvr_channel.h"
#include <string.h>
int  mockchan_count=0; int mockchan_status[32];
int nvr_chan_list(nvr_chan_mgr_t*m,nvr_channel_t*o,int cap){ (void)m;
    int n=mockchan_count<cap?mockchan_count:cap;
    for(int i=0;i<n;i++){ memset(&o[i],0,sizeof(o[i])); o[i].chn=i; o[i].enabled=1; }
    return n; }
nvr_chan_status_t nvr_chan_status(nvr_chan_mgr_t*m,int chn){ (void)m;(void)chn; return NVR_CHAN_EMPTY; }
int nvr_chan_status_code_of(nvr_chan_mgr_t*m,int chn){ (void)m; return (chn>=0&&chn<32)?mockchan_status[chn]:0; }
int nvr_chan_get(nvr_chan_mgr_t*m,int chn,nvr_channel_t*o){ (void)m;(void)chn;(void)o; return -1; }
