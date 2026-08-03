#include "nvr_cmd_display.h"
#include "nvr_preview.h"
#include "nvr_chan_persist.h"
#include "mhal_vout.h"
#include "mock_mhal_vout.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON* content_of(char *resp){ cJSON*j=cJSON_Parse(resp); cJSON*c=cJSON_Duplicate(cJSON_GetObjectItem(j,"content"),1); cJSON_Delete(j); return c; }

int main(void){
    system("rm -rf /tmp/nvrtest_disp && mkdir -p /tmp/nvrtest_disp");
    nvr_preview_cfg_t pc; memset(&pc,0,sizeof(pc)); pc.hdmi_w=1920; pc.hdmi_h=1080; pc.sm=(nvr_stream_mgr_t*)1;
    nvr_preview_t *pv; nvr_preview_init(&pc,&pv);
    nvr_chan_persist_t *ps=nvr_chan_persist_open("/tmp/nvrtest_disp");
    nvr_display_ctx_t ctx={ .pv=pv, .persist=ps, .cm=(nvr_chan_mgr_t*)1 };

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

    /* getChannelStatus：通道1(0-based 0) 置为 4(鉴权失败) */
    extern int mockchan_count; extern int mockchan_status[32];
    mockchan_count=2; mockchan_status[0]=4; mockchan_status[1]=1;
    a=cJSON_Parse("{\"channel\":1}");
    r=nvr_cmd_display_handle("X_NightOwl_getChannelStatus",a,&ctx); c=content_of(r);
    assert((int)cJSON_GetNumberValue(cJSON_GetObjectItem(c,"status"))==4);
    free(r); cJSON_Delete(a); cJSON_Delete(c);

    /* getDeviceCapabilities：缓存通道1 能力后应回读；device.capabilities 必须是
     * 旧路由分支(multiStorage/format/cloudRecording) ∪ 新分支(displayMode/
     * groupInPrimary) 的超集（Critical 修复：不能再让 display 分支的返回
     * 收窄，丢掉客户端判断多盘位/格式化/云存录像开关所需的字段）。 */
    { cJSON *caps=cJSON_CreateObject(); cJSON_AddStringToObject(caps,"signal","IPC");
      cJSON *cc=cJSON_AddArrayToObject(caps,"capabilities"); cJSON_AddItemToArray(cc,cJSON_CreateString("ptz"));
      nvr_chan_persist_set_caps(ps,1,caps); }
    a=cJSON_CreateObject();
    r=nvr_cmd_display_handle("X_NightOwl_getDeviceCapabilities",a,&ctx); c=content_of(r);
    assert(cJSON_GetObjectItem(c,"device"));
    {
        cJSON *dcap = cJSON_GetObjectItem(cJSON_GetObjectItem(c,"device"),"capabilities");
        assert(cJSON_IsArray(dcap));
        int has_display=0, has_multi=0, has_format=0, has_cloud=0;
        for (int i=0;i<cJSON_GetArraySize(dcap);i++){
            const char *s = cJSON_GetStringValue(cJSON_GetArrayItem(dcap,i));
            if (s && !strcmp(s,"displayMode"))    has_display=1;
            if (s && !strcmp(s,"multiStorage"))   has_multi=1;
            if (s && !strcmp(s,"format"))         has_format=1;
            if (s && !strcmp(s,"cloudRecording")) has_cloud=1;
        }
        assert(has_display && has_multi && has_format && has_cloud);
    }
    cJSON *chs=cJSON_GetObjectItem(c,"channels"); assert(cJSON_IsArray(chs)&&cJSON_GetArraySize(chs)>=1);
    /* mockchan_count=2 (设于上面 getChannelStatus 用例)：通道1(chn0=0) 有缓存(ptz)，
     * 通道2(chn0=1) 无缓存 → 必须有安全默认 capabilities(含 cloudRecording)。 */
    {
        int found_ch2 = 0;
        for (int i=0;i<cJSON_GetArraySize(chs);i++){
            cJSON *o = cJSON_GetArrayItem(chs,i);
            cJSON *cc = cJSON_GetObjectItem(o,"capabilities");
            assert(cJSON_IsArray(cc) && cJSON_GetArraySize(cc)>=1);   /* 任何通道不缺 capabilities */
            if ((int)cJSON_GetNumberValue(cJSON_GetObjectItem(o,"channel"))==2) {
                found_ch2 = 1;
                int has_cloud=0;
                for (int j=0;j<cJSON_GetArraySize(cc);j++){
                    const char *s = cJSON_GetStringValue(cJSON_GetArrayItem(cc,j));
                    if (s && !strcmp(s,"cloudRecording")) has_cloud=1;
                }
                assert(has_cloud);
            }
        }
        assert(found_ch2);
    }
    free(r); cJSON_Delete(a); cJSON_Delete(c);

    /* ZoomPan 预留：回显 */
    a=cJSON_Parse("{\"channel\":1,\"enable\":true,\"CenterPointX\":500,\"CenterPointY\":500,\"ZoomRatio\":120}");
    r=nvr_cmd_display_handle("X_NightOwl_setChannelZoomPan",a,&ctx); c=content_of(r);
    assert((int)cJSON_GetNumberValue(cJSON_GetObjectItem(c,"ZoomRatio"))==120);
    free(r); cJSON_Delete(a); cJSON_Delete(c);

    nvr_chan_persist_close(ps); nvr_preview_deinit(pv);
    printf("test_nvr_cmd_display: ALL PASS\n");
    return 0;
}
