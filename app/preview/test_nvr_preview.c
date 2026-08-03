#include "nvr_preview.h"
#include "mhal_vout.h"
#include "mock_mhal_vout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void){
    /* 千分比→像素 */
    assert(pv_thousandths_to_px(0,1920)==0);
    assert(pv_thousandths_to_px(1000,1920)==1920);
    assert(pv_thousandths_to_px(200,1000)==200);

    nvr_preview_cfg_t cfg; memset(&cfg,0,sizeof(cfg));
    cfg.hdmi_w=1920; cfg.hdmi_h=1080; cfg.sm=(nvr_stream_mgr_t*)1;
    nvr_preview_t *p; assert(nvr_preview_init(&cfg,&p)==0);

    /* 映射：格0←通道3, 格2←通道1（1-based 输入） */
    int map[32]; for(int i=0;i<32;i++) map[i]=i+1; map[0]=3; map[2]=1;
    assert(nvr_preview_set_mapping(p,map,32)==0);
    mhalmock_reset();
    /* 4 宫格第 1 页：应绑 4 窗，窗0=通道2(0-based, =map[0]-1=2), 窗2=通道0 */
    assert(nvr_preview_set_mode(p,4,1)==0);
    assert(mhalmock_layout==MHAL_LAYOUT_4);
    assert(mhalmock_nbind==4);
    assert(mhalmock_rects[0].chn==2);   /* map[0]=3 → 0-based 2 */
    assert(mhalmock_rects[2].chn==0);   /* map[2]=1 → 0-based 0 */

    /* ---- 承前项(Task4 遗留/Task7 收尾)：display_mode>0 时 on_channel_online 必须按
     * map0 重算窗口，不能再用旧 page*win_count+chn 公式（否则绑错窗/丢窗）。
     * 场景：窗0 因映射被指到通道2(非默认恒等映射)；通道2 断线重连(win2chn[0] 被 unmap 清空)
     * 后 on_channel_online(2) 应重新命中窗0，而不是旧公式算出的窗2(=chn-page*win_count=2-0，
     * 因为 p->page 恒为0)。 */
    assert(nvr_preview_unmap(p,0)==0);          /* 模拟窗0(通道2)掉线：清绑定 */
    mhalmock_reset();
    assert(nvr_preview_on_channel_online(p,2)==0);
    assert(mhalmock_nbind==1);                   /* 应重新 bind 一次 */
    assert(mhalmock_rects[0].chn==2);
    assert(mhalmock_bind_win[0]==0);             /* 命中 map0 对应的窗0，不是旧公式的窗2 */

    /* mode=0 → 停所有解码（unbind） */
    mhalmock_reset();
    assert(nvr_preview_set_mode(p,0,0)==0);
    assert(mhalmock_nunbind>=4);

    /* ---- Important2 修复验证：display_mode==0（已离开 LiveView）时 on_channel_online
     * 不得落回旧公式绑回幽灵宫格窗。通道2 曾映射到格0(map[0]=3→0-based 2)，若走旧公式
     * win=chn-page*win_count=2-0=2 会误绑窗2；正确行为是 display_mode==0 时什么都不做，
     * 断言不产生任何 mhal_vout_bind。 */
    mhalmock_reset();
    assert(nvr_preview_on_channel_online(p,2)==0);
    assert(mhalmock_nbind==0);          /* 不应绑回任何窗（保持隐藏） */

    /* 悬浮块：通道5 子码流 在 20%,25%,20%,20% */
    mhalmock_reset();
    nvr_pv_ext_t b={ .chn0=5,.x=200,.y=250,.w=200,.h=200,.stream=NVR_STREAM_SUB };
    assert(nvr_preview_set_ext(p,&b,1)==0);
    assert(mhalmock_nbind==1);
    assert(mhalmock_rects[0].chn==5);
    assert(mhalmock_rects[0].x==384 && mhalmock_rects[0].y==270); /* 200/1000*1920, 250/1000*1080 */
    assert(mhalmock_rects[0].w==384 && mhalmock_rects[0].h==216);
    assert(mhalmock_nswitch==1 && mhalmock_stream_switch[0][0]==5 && mhalmock_stream_switch[0][1]==NVR_STREAM_SUB);

    /* getter 回读 */
    int mode,page; nvr_preview_get_mode(p,&mode,&page);
    int gm[32]; int gn=nvr_preview_get_mapping(p,gm,32);
    assert(gn==32 && gm[0]==3 && gm[2]==1);

    /* 清空悬浮块 */
    mhalmock_reset();
    assert(nvr_preview_set_ext(p,NULL,0)==0);
    assert(mhalmock_nunbind>=1);

    nvr_preview_deinit(p);
    printf("test_nvr_preview: ALL PASS\n");
    return 0;
}
