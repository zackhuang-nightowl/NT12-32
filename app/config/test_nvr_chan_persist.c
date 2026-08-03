#include "nvr_chan_persist.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TMP = "/tmp/nvrtest_persist";
static void rm_dir(void){ system("rm -rf /tmp/nvrtest_persist"); }
static void mk_dir(void){ rm_dir(); system("mkdir -p /tmp/nvrtest_persist"); }

static void test_default_when_missing(void){
    mk_dir();
    nvr_chan_persist_t *p = nvr_chan_persist_open(TMP);
    assert(p);
    int m[32]; int n = nvr_chan_persist_get_mapping(p, m, 32);
    assert(n == 32);
    for (int i=0;i<32;i++) assert(m[i] == i+1);   /* 恒等 1..32 */
    nvr_chan_persist_close(p);
}

static void test_set_mapping_roundtrip(void){
    mk_dir();
    nvr_chan_persist_t *p = nvr_chan_persist_open(TMP);
    int in[32]; for(int i=0;i<32;i++) in[i]=i+1; in[0]=3; in[2]=1;  /* 交换格1↔格3 */
    assert(nvr_chan_persist_set_mapping(p, in, 32) == 0);
    nvr_chan_persist_close(p);
    /* 重新打开应保持 */
    p = nvr_chan_persist_open(TMP);
    int m[32]; nvr_chan_persist_get_mapping(p, m, 32);
    assert(m[0]==3 && m[2]==1 && m[1]==2);
    nvr_chan_persist_close(p);
}

static void test_bak_restore_on_corrupt(void){
    mk_dir();
    nvr_chan_persist_t *p = nvr_chan_persist_open(TMP);
    int in[32]; for(int i=0;i<32;i++) in[i]=i+1; in[5]=9;
    nvr_chan_persist_set_mapping(p, in, 32);   /* 产生 channels.json */
    nvr_chan_persist_set_mapping(p, in, 32);   /* 第二次写 → 上次好文件成 .bak */
    nvr_chan_persist_close(p);
    /* 破坏主文件 */
    FILE *f = fopen("/tmp/nvrtest_persist/channels.json","w"); fputs("{bad json", f); fclose(f);
    p = nvr_chan_persist_open(TMP);            /* 应回滚 .bak */
    int m[32]; nvr_chan_persist_get_mapping(p, m, 32);
    assert(m[5]==9);
    nvr_chan_persist_close(p);
}

static void test_caps_roundtrip(void){
    mk_dir();
    nvr_chan_persist_t *p = nvr_chan_persist_open(TMP);
    cJSON *caps = cJSON_CreateObject();
    cJSON_AddStringToObject(caps, "signal", "IPC");
    cJSON *arr = cJSON_AddArrayToObject(caps, "capabilities");
    cJSON_AddItemToArray(arr, cJSON_CreateString("ptz"));
    assert(nvr_chan_persist_set_caps(p, 1, caps) == 0);
    nvr_chan_persist_close(p);
    p = nvr_chan_persist_open(TMP);
    cJSON *got = nvr_chan_persist_get_caps(p, 1);
    assert(got);
    assert(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(got,"signal")),"IPC")==0);
    nvr_chan_persist_close(p);
    rm_dir();
}

int main(void){
    test_default_when_missing();
    test_set_mapping_roundtrip();
    test_bak_restore_on_corrupt();
    test_caps_roundtrip();
    printf("test_nvr_chan_persist: ALL PASS\n");
    return 0;
}
