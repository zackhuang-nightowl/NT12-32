#include "nvr_channel.h"
int nvr_chan_list(nvr_chan_mgr_t*m,nvr_channel_t*o,int cap){ (void)m;(void)o;(void)cap; return 0; }
nvr_chan_status_t nvr_chan_status(nvr_chan_mgr_t*m,int chn){ (void)m;(void)chn; return NVR_CHAN_EMPTY; }
int nvr_chan_status_code_of(nvr_chan_mgr_t*m,int chn){ (void)m;(void)chn; return 0; }
int nvr_chan_get(nvr_chan_mgr_t*m,int chn,nvr_channel_t*o){ (void)m;(void)chn;(void)o; return -1; }
