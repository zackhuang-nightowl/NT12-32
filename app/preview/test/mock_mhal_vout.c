#include "mhal_vout.h"
#include "mock_mhal_vout.h"
#include <string.h>
mock_rect_t mhalmock_rects[64]; int mhalmock_nbind;
int mhalmock_layout; int mhalmock_unbind_chn[64]; int mhalmock_nunbind;
int mhalmock_stream_switch[64][2]; int mhalmock_nswitch;
void mhalmock_reset(void){ mhalmock_nbind=mhalmock_nunbind=mhalmock_nswitch=0; mhalmock_layout=-1; }
int mhal_vout_init(mhal_out_t o,int w,int h){ (void)o;(void)w;(void)h; return 0; }
int mhal_vout_set_layout(mhal_layout_t l){ mhalmock_layout=(int)l; return 0; }
int mhal_vout_bind(int win,int chn){ /* 宫格绑定：记为 rect 未知，仅记 chn */
    mhalmock_rects[mhalmock_nbind++] = (mock_rect_t){chn,-1,-1,-1,-1}; (void)win; return 0; }
int mhal_vout_bind_rect(int chn,int x,int y,int w,int h){
    mhalmock_rects[mhalmock_nbind++]=(mock_rect_t){chn,x,y,w,h}; return 0; }
int mhal_vout_unbind(int chn){ mhalmock_unbind_chn[mhalmock_nunbind++]=chn; return 0; }
int mhal_vout_osd(int win,const char*t){ (void)win;(void)t; return 0; }
void mhal_vout_deinit(mhal_out_t o){ (void)o; }
