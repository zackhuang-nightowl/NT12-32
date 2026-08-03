#ifndef MOCK_MHAL_VOUT_H
#define MOCK_MHAL_VOUT_H
typedef struct { int chn,x,y,w,h; } mock_rect_t;
extern mock_rect_t mhalmock_rects[64];
extern int mhalmock_nbind;
/* mhal_vout_bind(win,chn) 的窗口号（与 mhalmock_rects 按下标对应；bind_rect 无宫格 win，记 -1） */
extern int mhalmock_bind_win[64];
extern int mhalmock_layout;
extern int mhalmock_unbind_chn[64];
extern int mhalmock_nunbind;
extern int mhalmock_stream_switch[64][2]; /* [i]={chn,stream} */
extern int mhalmock_nswitch;
void mhalmock_reset(void);
#endif
