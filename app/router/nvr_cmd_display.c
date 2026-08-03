#include "nvr_cmd_display.h"
#include <stdlib.h>
#include <string.h>

static char *resp_ok_content(cJSON *content){
    cJSON *r=cJSON_CreateObject();
    cJSON_AddNumberToObject(r,"statusCode",200);
    cJSON_AddStringToObject(r,"statusMsg","OK");
    cJSON_AddItemToObject(r,"content",content?content:cJSON_CreateObject());
    char *s=cJSON_PrintUnformatted(r); cJSON_Delete(r); return s;
}
static char *resp_result(const char *result){
    cJSON *c=cJSON_CreateObject(); cJSON_AddStringToObject(c,"result",result); return resp_ok_content(c);
}

char *nvr_cmd_display_handle(const char *func, cJSON *args, const nvr_display_ctx_t *ctx){
    if(!func||!ctx) return NULL;

    if(!strcmp(func,"GUI_setDeviceDisplayMode")){
        int mode=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(args,"displayMode"));
        int page=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(args,"displayPage"));
        nvr_preview_set_mode(ctx->pv,mode,page);
        return resp_result("OK");
    }
    if(!strcmp(func,"GUI_getDeviceDisplayMode")){
        int m=0,pg=1; nvr_preview_get_mode(ctx->pv,&m,&pg);
        cJSON *c=cJSON_CreateObject(); cJSON_AddNumberToObject(c,"displayMode",m); cJSON_AddNumberToObject(c,"displayPage",pg);
        return resp_ok_content(c);
    }
    if(!strcmp(func,"GUI_setChannelMapping")){
        cJSON *arr=cJSON_GetObjectItem(args,"ChannelMapping");
        if(!cJSON_IsArray(arr)) return resp_result("input channel num exception");
        int n=cJSON_GetArraySize(arr), map[32]; if(n>32)n=32;
        for(int i=0;i<n;i++) map[i]=(int)cJSON_GetNumberValue(cJSON_GetArrayItem(arr,i));
        nvr_preview_set_mapping(ctx->pv,map,n);
        if(ctx->persist) nvr_chan_persist_set_mapping(ctx->persist,map,n);
        return resp_result("OK");
    }
    if(!strcmp(func,"GUI_getChannelMapping")){
        int map[32]; int n = ctx->persist? nvr_chan_persist_get_mapping(ctx->persist,map,32):0;
        cJSON *c=cJSON_CreateObject(); cJSON *arr=cJSON_AddArrayToObject(c,"ChannelMapping");
        for(int i=0;i<n;i++) cJSON_AddItemToArray(arr,cJSON_CreateNumber(map[i]));
        return resp_ok_content(c);
    }
    if(!strcmp(func,"GUI_setDeviceDisplayExt")){
        cJSON *arr=cJSON_GetObjectItem(args,"channels");
        int n=cJSON_IsArray(arr)?cJSON_GetArraySize(arr):0;
        nvr_pv_ext_t b[16]; if(n>16)n=16;
        for(int i=0;i<n;i++){ cJSON *o=cJSON_GetArrayItem(arr,i);
            b[i].chn0=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(o,"channel"))-1;
            b[i].x=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(o,"x"));
            b[i].y=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(o,"y"));
            b[i].w=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(o,"w"));
            b[i].h=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(o,"h"));
            const char *st=cJSON_GetStringValue(cJSON_GetObjectItem(o,"streamType"));
            b[i].stream=(st&&!strcmp(st,"sub"))?NVR_STREAM_SUB:NVR_STREAM_MAIN;
        }
        nvr_preview_set_ext(ctx->pv,n?b:NULL,n);
        return resp_ok_content(NULL);
    }
    if(!strcmp(func,"GUI_getDeviceDisplayExt")){
        nvr_pv_ext_t b[16]; int n=nvr_preview_get_ext(ctx->pv,b,16);
        cJSON *c=cJSON_CreateObject(); cJSON *arr=cJSON_AddArrayToObject(c,"channels");
        for(int i=0;i<n;i++){ cJSON *o=cJSON_CreateObject();
            cJSON_AddNumberToObject(o,"channel",b[i].chn0+1);
            cJSON_AddNumberToObject(o,"x",b[i].x); cJSON_AddNumberToObject(o,"y",b[i].y);
            cJSON_AddNumberToObject(o,"w",b[i].w); cJSON_AddNumberToObject(o,"h",b[i].h);
            cJSON_AddStringToObject(o,"streamType",b[i].stream==NVR_STREAM_SUB?"sub":"main");
            cJSON_AddItemToArray(arr,o); }
        return resp_ok_content(c);
    }
    if(!strcmp(func,"GUI_getSysDisplay")){
        cJSON *c=cJSON_CreateObject();
        cJSON *rl=cJSON_AddArrayToObject(c,"resolutionList");
        cJSON_AddItemToArray(rl,cJSON_CreateString("1920*1080"));
        cJSON_AddStringToObject(c,"resolution","1920*1080");
        cJSON_AddNumberToObject(c,"displayOpacity",255);
        cJSON_AddStringToObject(c,"fb","fb0");
        return resp_ok_content(c);
    }
    if(!strcmp(func,"GUI_setSysDisplay")){
        cJSON *c=cJSON_CreateObject(); cJSON_AddStringToObject(c,"fb","fb0");
        return resp_ok_content(c);  /* 分辨率固定 1080P，仅回 fb */
    }
    if(!strcmp(func,"X_NightOwl_getChannelStatus")){
        int ch1=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(args,"channel"));
        int code = ctx->cm? nvr_chan_status_code_of(ctx->cm, ch1-1) : 0;
        cJSON *c=cJSON_CreateObject(); cJSON_AddNumberToObject(c,"status",code);
        return resp_ok_content(c);
    }
    if(!strcmp(func,"X_NightOwl_getDeviceCapabilities")){
        cJSON *c=cJSON_CreateObject();
        cJSON *dev=cJSON_AddObjectToObject(c,"device");
        cJSON *dc=cJSON_AddArrayToObject(dev,"capabilities");
        cJSON_AddItemToArray(dc,cJSON_CreateString("displayMode"));
        cJSON_AddItemToArray(dc,cJSON_CreateString("groupInPrimary"));
        cJSON *chs=cJSON_AddArrayToObject(c,"channels");
        int list_n=0; nvr_channel_t list[32];
        if(ctx->cm) list_n=nvr_chan_list(ctx->cm,list,32);
        for(int i=0;i<list_n;i++){
            int ch1=list[i].chn+1;
            cJSON *cached = ctx->persist? nvr_chan_persist_get_caps(ctx->persist,ch1):NULL;
            cJSON *o = cached? cJSON_Duplicate(cached,1) : cJSON_CreateObject();
            /* 确保有 channel/signal 字段 */
            if(!cJSON_GetObjectItem(o,"channel")) cJSON_AddNumberToObject(o,"channel",ch1);
            if(!cJSON_GetObjectItem(o,"signal"))  cJSON_AddStringToObject(o,"signal","IPC");
            cJSON_AddItemToArray(chs,o);
        }
        return resp_ok_content(c);
    }
    if(!strcmp(func,"X_NightOwl_setChannelZoomPan")){
        /* 预留：回显生效值，不驱动实际裁剪 */
        cJSON *c=cJSON_CreateObject();
        cJSON_AddStringToObject(c,"result","OK");
        cJSON_AddNumberToObject(c,"CenterPointX",(int)cJSON_GetNumberValue(cJSON_GetObjectItem(args,"CenterPointX")));
        cJSON_AddNumberToObject(c,"CenterPointY",(int)cJSON_GetNumberValue(cJSON_GetObjectItem(args,"CenterPointY")));
        cJSON_AddNumberToObject(c,"ZoomRatio",(int)cJSON_GetNumberValue(cJSON_GetObjectItem(args,"ZoomRatio")));
        return resp_ok_content(c);
    }
    if(!strcmp(func,"X_NightOwl_getChannelZoomPan")){
        cJSON *c=cJSON_CreateObject();
        cJSON_AddBoolToObject(c,"enable",0);
        cJSON_AddNumberToObject(c,"CenterPointX",500); cJSON_AddNumberToObject(c,"CenterPointY",500);
        cJSON_AddNumberToObject(c,"ZoomRatio",100);
        return resp_ok_content(c);
    }
    return NULL;
}
