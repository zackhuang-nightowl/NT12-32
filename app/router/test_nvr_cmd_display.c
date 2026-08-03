#include "nvr_cmd_display.h"
#include "nvr_preview.h"
#include "nvr_chan_persist.h"
#include "mock_mhal_vout.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cJSON* content_of(char *resp){ cJSON*j=cJSON_Parse(resp); cJSON*c=cJSON_Duplicate(cJSON_GetObjectItem(j,"content"),1); cJSON_Delete(j); return c; }

int main(void){
    system("rm -rf /tmp/nvrtest_disp && mkdir -p /tmp/nvrtest_disp");
    nvr_preview_cfg_t pc; memset(&pc,0,sizeof(pc)); pc.hdmi_w=1920; pc.hdmi_h=1080; pc.sm=(nvr_stream_mgr_t*)1;
    nvr_preview_t *pv; nvr_preview_init(&pc,&pv);
    nvr_chan_persist_t *ps=nvr_chan_persist_open("/tmp/nvrtest_disp");
    nvr_display_ctx_t ctx={ .pv=pv, .persist=ps, .cm=NULL };

    /* setChannelMapping 持久化 */
    cJSON*a=cJSON_Parse("{\"ChannelMapping\":[3,2,1,4,5,6,7,8,9,10,11,12,13,14,15,16]}");
    char*r=nvr_cmd_display_handle("GUI_setChannelMapping",a,&ctx); assert(r);
    cJSON*c=content_of(r); assert(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(c,"result")),"OK")==0);
    free(r); cJSON_Delete(a); cJSON_Delete(c);
    int mm[32]; assert(nvr_chan_persist_get_mapping(ps,mm,32)>=3 && mm[0]==3 && mm[2]==1);

    /* setDeviceDisplayMode 驱动 preview */
    mhalmock_reset();
    a=cJSON_Parse("{\"displayMode\":4,\"displayPage\":1}");
    r=nvr_cmd_display_handle("GUI_setDeviceDisplayMode",a,&ctx); assert(r);
    assert(mhalmock_layout==MHAL_LAYOUT_4 && mhalmock_nbind==4);
    free(r); cJSON_Delete(a);

    /* getDeviceDisplayMode 回读 */
    a=cJSON_CreateObject();
    r=nvr_cmd_display_handle("GUI_getDeviceDisplayMode",a,&ctx); c=content_of(r);
    assert((int)cJSON_GetNumberValue(cJSON_GetObjectItem(c,"displayMode"))==4);
    free(r); cJSON_Delete(a); cJSON_Delete(c);

    /* setDeviceDisplayExt 悬浮块 */
    mhalmock_reset();
    a=cJSON_Parse("{\"channels\":[{\"channel\":6,\"x\":200,\"y\":250,\"w\":200,\"h\":200,\"streamType\":\"sub\"}]}");
    r=nvr_cmd_display_handle("GUI_setDeviceDisplayExt",a,&ctx); assert(r);
    assert(mhalmock_nbind==1 && mhalmock_rects[0].chn==5 /*ch6→0based5*/ && mhalmock_rects[0].x==384);
    free(r); cJSON_Delete(a);

    /* getSysDisplay */
    a=cJSON_CreateObject();
    r=nvr_cmd_display_handle("GUI_getSysDisplay",a,&ctx); c=content_of(r);
    assert(cJSON_GetObjectItem(c,"resolution"));
    free(r); cJSON_Delete(a); cJSON_Delete(c);

    /* 非显示命令 → NULL */
    a=cJSON_CreateObject();
    assert(nvr_cmd_display_handle("someOtherCmd",a,&ctx)==NULL);
    cJSON_Delete(a);

    nvr_chan_persist_close(ps); nvr_preview_deinit(pv);
    printf("test_nvr_cmd_display: ALL PASS\n");
    return 0;
}
