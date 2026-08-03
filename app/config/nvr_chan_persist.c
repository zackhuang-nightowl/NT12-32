#include "nvr_chan_persist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

struct nvr_chan_persist {
    char  path[256];       /* <dir>/channels.json */
    char  bak[256];        /* <dir>/channels.json.bak */
    char  dir[200];
    cJSON *root;           /* 持有整棵 channels.json */
};

static cJSON *read_json_file(const char *path){
    FILE *f = fopen(path, "rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    if(n<=0){ fclose(f); return NULL; }
    char *buf=malloc(n+1); if(!buf){ fclose(f); return NULL; }
    size_t rd=fread(buf,1,n,f); fclose(f); buf[rd]=0;
    cJSON *j=cJSON_Parse(buf); free(buf); return j;
}

static cJSON *default_root(void){
    cJSON *r=cJSON_CreateObject();
    cJSON *m=cJSON_AddArrayToObject(r,"channelMapping");
    for(int i=1;i<=NVR_PERSIST_MAX_CH;i++) cJSON_AddItemToArray(m,cJSON_CreateNumber(i));
    cJSON_AddArrayToObject(r,"channelCaps");
    return r;
}

/* 原子写：tmp → fsync → 旧文件转 .bak → rename → fsync(dir) */
static int atomic_save(nvr_chan_persist_t *p){
    char tmp[260]; snprintf(tmp,sizeof(tmp),"%s.tmp",p->path);
    char *txt=cJSON_PrintUnformatted(p->root); if(!txt) return -1;
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd<0){ free(txt); return -1; }
    size_t len=strlen(txt); int ok = (write(fd,txt,len)==(ssize_t)len);
    free(txt);
    if(ok) fsync(fd); close(fd);
    if(!ok){ unlink(tmp); return -1; }
    /* 旧好文件保为 .bak（rename 覆盖，best-effort） */
    if(access(p->path,F_OK)==0){ rename(p->path,p->bak); }
    if(rename(tmp,p->path)!=0){ return -1; }
    int dfd=open(p->dir,O_RDONLY|O_DIRECTORY);
    if(dfd>=0){ fsync(dfd); close(dfd); }
    return 0;
}

static void ensure_keys(cJSON *root){
    if(!cJSON_GetObjectItem(root,"channelMapping")){
        cJSON *m=cJSON_AddArrayToObject(root,"channelMapping");
        for(int i=1;i<=NVR_PERSIST_MAX_CH;i++) cJSON_AddItemToArray(m,cJSON_CreateNumber(i));
    }
    if(!cJSON_GetObjectItem(root,"channelCaps"))
        cJSON_AddArrayToObject(root,"channelCaps");
}

nvr_chan_persist_t *nvr_chan_persist_open(const char *config_dir){
    nvr_chan_persist_t *p=calloc(1,sizeof(*p)); if(!p) return NULL;
    snprintf(p->dir,sizeof(p->dir),"%s",config_dir);
    snprintf(p->path,sizeof(p->path),"%s/channels.json",config_dir);
    snprintf(p->bak,sizeof(p->bak),"%s/channels.json.bak",config_dir);
    p->root=read_json_file(p->path);
    if(!p->root) p->root=read_json_file(p->bak);   /* 回滚 */
    if(!p->root) p->root=default_root();
    ensure_keys(p->root);
    return p;
}

void nvr_chan_persist_close(nvr_chan_persist_t *p){
    if(!p) return; if(p->root) cJSON_Delete(p->root); free(p);
}

int nvr_chan_persist_get_mapping(nvr_chan_persist_t *p, int *out, int cap){
    if(!p||!out) return 0;
    cJSON *m=cJSON_GetObjectItem(p->root,"channelMapping");
    int n=cJSON_GetArraySize(m), w=0;
    for(int i=0;i<n && w<cap;i++){ out[w++]=(int)cJSON_GetNumberValue(cJSON_GetArrayItem(m,i)); }
    return w;
}

int nvr_chan_persist_set_mapping(nvr_chan_persist_t *p, const int *map, int n){
    if(!p||!map) return -1;
    cJSON *arr=cJSON_CreateArray();
    for(int i=0;i<n;i++) cJSON_AddItemToArray(arr,cJSON_CreateNumber(map[i]));
    cJSON_DeleteItemFromObject(p->root,"channelMapping");
    cJSON_AddItemToObject(p->root,"channelMapping",arr);
    return atomic_save(p);
}

static cJSON *find_caps(cJSON *root, int ch){
    cJSON *caps=cJSON_GetObjectItem(root,"channelCaps");
    cJSON *it; cJSON_ArrayForEach(it,caps){
        cJSON *c=cJSON_GetObjectItem(it,"channel");
        if(c && (int)cJSON_GetNumberValue(c)==ch) return it;
    }
    return NULL;
}

cJSON *nvr_chan_persist_get_caps(nvr_chan_persist_t *p, int ch){
    return p?find_caps(p->root,ch):NULL;
}

int nvr_chan_persist_set_caps(nvr_chan_persist_t *p, int ch, cJSON *obj){
    if(!p||!obj) return -1;
    cJSON_DeleteItemFromObject(obj,"channel");
    cJSON_AddNumberToObject(obj,"channel",ch);
    cJSON *caps=cJSON_GetObjectItem(p->root,"channelCaps");
    /* 重建不含该 channel 的新数组：旧项 Duplicate 复制，避免悬垂指针；
       最后把接管所有权的 obj 挂入新数组，整体替换 channelCaps。 */
    cJSON *na=cJSON_CreateArray(); cJSON *it;
    cJSON_ArrayForEach(it,caps){
        cJSON *c=cJSON_GetObjectItem(it,"channel");
        if(!(c&&(int)cJSON_GetNumberValue(c)==ch))
            cJSON_AddItemToArray(na, cJSON_Duplicate(it,1));
    }
    cJSON_AddItemToArray(na,obj);
    cJSON_DeleteItemFromObject(p->root,"channelCaps");
    cJSON_AddItemToObject(p->root,"channelCaps",na);
    return atomic_save(p);
}
