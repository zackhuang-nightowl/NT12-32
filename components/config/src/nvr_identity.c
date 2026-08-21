/***************************************************************************************
 *  nvr_identity.c — 设备身份读写实现（数据分区文件权威源，对齐 ODC libDVRAPI）
 ***************************************************************************************/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* O_DIRECTORY / localtime_r */
#endif
#include "nvr_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#include "cJSON.h"
#include "nvr_log.h"

/* 数据分区基目录(可编译期覆盖供 host 单测) */
#ifndef NVR_USER_DIR
#define NVR_USER_DIR "/User"
#endif
#ifndef NVR_SYS_DIR
#define NVR_SYS_DIR "/SYS"
#endif

#define TAG "identity"

#define P_SN        NVR_USER_DIR "/OWLSerialNumber"
#define P_MAC0      NVR_USER_DIR "/mac_addr_v2"
#define P_MAC1      NVR_USER_DIR "/mac_addr_v2.eth1"
#define P_UID       NVR_USER_DIR "/tutk_agent_udid"
#define P_MODEL     NVR_USER_DIR "/OWLModel"
#define P_OWL_DIR   NVR_USER_DIR "/OWL"
#define P_TUTKDATA  NVR_USER_DIR "/OWL/tutkdata.json"

/* ---------- 小工具 ---------- */

/* 读整文件到 buf,并去掉尾部空白/换行。成功返回长度,失败/空返回 <0。 */
static int read_trim(const char *path, char *out, size_t cap)
{
    if (!out || cap == 0) return -1;
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    /* trim 尾部 \r\n 空格 制表 */
    while (n > 0) {
        char c = out[n - 1];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') { out[--n] = 0; }
        else break;
    }
    return n > 0 ? (int)n : -1;
}

/* 原子写文本:写 <path>.tmp → fsync → rename → fsync(dir)。成功 0,失败 <0。 */
static int atomic_write(const char *path, const char *text)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { NVR_LOGE(TAG, "open %s: fail", tmp); return -1; }
    size_t len = strlen(text);
    int ok = (write(fd, text, len) == (ssize_t)len);
    if (ok) fsync(fd);
    close(fd);
    if (!ok) { unlink(tmp); NVR_LOGE(TAG, "write %s: fail", tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); NVR_LOGE(TAG, "rename %s: fail", path); return -1; }
    /* 目录 fsync 落实 rename(best-effort) */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        int dfd = open(dir[0] ? dir : "/", O_RDONLY | O_DIRECTORY);
        if (dfd >= 0) { fsync(dfd); close(dfd); }
    }
    return 0;
}

/* ---------- 恒定身份缓存(SN/MAC 恒定,首读缓存) ---------- */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_sn[64];       static int g_sn_cached;
static char g_mac0[24];     static int g_mac0_cached;
static char g_mac1[24];     static int g_mac1_cached;

void nvr_identity_cache_invalidate(void)
{
    pthread_mutex_lock(&g_lock);
    g_sn_cached = g_mac0_cached = g_mac1_cached = 0;
    pthread_mutex_unlock(&g_lock);
}

int nvr_identity_get_sn(char *out, size_t cap)
{
    if (!out || cap == 0) return -1;
    pthread_mutex_lock(&g_lock);
    if (!g_sn_cached) {
        if (read_trim(P_SN, g_sn, sizeof(g_sn)) < 0) g_sn[0] = 0;
        g_sn_cached = 1;
        if (!g_sn[0]) NVR_LOGW(TAG, "SN not provisioned (%s missing/empty)", P_SN);
    }
    int n = (int)snprintf(out, cap, "%s", g_sn);
    pthread_mutex_unlock(&g_lock);
    return (n < 0) ? 0 : (n >= (int)cap ? (int)cap - 1 : n);
}

/* 从 /sys/class/net/<iface>/address 回退读 MAC */
static int read_sys_mac(const char *iface, char *out, size_t cap)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface);
    return read_trim(path, out, cap);
}

int nvr_identity_get_mac(const char *iface, char *out, size_t cap)
{
    if (!out || cap == 0) return -1;
    if (!iface || !iface[0]) iface = "eth0";
    int eth1 = (strcmp(iface, "eth1") == 0);

    pthread_mutex_lock(&g_lock);
    char *cache = eth1 ? g_mac1 : g_mac0;
    int  *flag  = eth1 ? &g_mac1_cached : &g_mac0_cached;
    size_t clen = eth1 ? sizeof(g_mac1) : sizeof(g_mac0);
    if (!*flag) {
        const char *file = eth1 ? P_MAC1 : P_MAC0;
        if (read_trim(file, cache, clen) < 0) {
            /* 回退网卡实时 MAC(与 nvr_netime 一致) */
            if (read_sys_mac(iface, cache, clen) < 0) {
                cache[0] = 0;
                NVR_LOGW(TAG, "MAC %s not found (%s + /sys both missing)", iface, file);
            }
        }
        *flag = 1;
    }
    int n = (int)snprintf(out, cap, "%s", cache);
    pthread_mutex_unlock(&g_lock);
    return (n < 0) ? 0 : (n >= (int)cap ? (int)cap - 1 : n);
}

/* ---------- UID(可配置) ---------- */
int nvr_identity_get_uid(char *out, size_t cap)
{
    if (!out || cap == 0) return -1;
    if (read_trim(P_UID, out, cap) < 0) {
        out[0] = 0;
        return 0;
    }
    return (int)strlen(out);
}

int nvr_identity_set_uid(const char *uid)
{
    if (!uid) return -1;
    return atomic_write(P_UID, uid);
}

/* ---------- MODEL(机型,可配置;缺失回退缺省 NOP12-32) ---------- */
int nvr_identity_get_model(char *out, size_t cap)
{
    if (!out || cap == 0) return -1;
    if (read_trim(P_MODEL, out, cap) < 0)
        return (int)snprintf(out, cap, "%s", NVR_IDENTITY_DEF_MODEL);
    return (int)strlen(out);
}

int nvr_identity_set_model(const char *model)
{
    if (!model || !model[0]) return -1;
    return atomic_write(P_MODEL, model);
}

/* ---------- TUTK 凭据(可配置,单文件 tutkdata.json) ---------- */

static cJSON *read_tutkdata(void)
{
    char buf[512];
    if (read_trim(P_TUTKDATA, buf, sizeof(buf)) < 0) return NULL;
    return cJSON_Parse(buf);
}

int nvr_identity_get_tutk_creds(char *iotckey, size_t kc, char *avkey, size_t ac)
{
    const char *ik = NVR_IDENTITY_DEF_IOTCKEY;
    const char *av = NVR_IDENTITY_DEF_AVKEY;
    cJSON *j = read_tutkdata();
    if (j) {
        cJSON *v;
        if ((v = cJSON_GetObjectItem(j, "IotcAuthKey")) && cJSON_IsString(v) && v->valuestring[0])
            ik = v->valuestring;
        if ((v = cJSON_GetObjectItem(j, "AvPassword")) && cJSON_IsString(v) && v->valuestring[0])
            av = v->valuestring;
    } else {
        NVR_LOGW(TAG, "%s missing; using default creds", P_TUTKDATA);
    }
    if (iotckey && kc) snprintf(iotckey, kc, "%s", ik);
    if (avkey && ac)   snprintf(avkey, ac, "%s", av);
    if (j) cJSON_Delete(j);
    return 0;
}

int nvr_identity_get_av_account(char *out, size_t cap)
{
    const char *acct = NVR_IDENTITY_DEF_AVACCOUNT;
    cJSON *j = read_tutkdata();
    if (j) {
        cJSON *v = cJSON_GetObjectItem(j, "AvAccount");
        if (v && cJSON_IsString(v) && v->valuestring[0]) acct = v->valuestring;
    }
    int n = (int)strlen(acct);
    if (out && cap) snprintf(out, cap, "%s", acct);
    if (j) cJSON_Delete(j);
    return n;
}

void nvr_identity_ensure_provisioned(void)
{
    char sn[64], mac[24], uid[64], model[64];
    nvr_identity_get_sn(sn, sizeof(sn));
    nvr_identity_get_mac("eth0", mac, sizeof(mac));
    nvr_identity_get_uid(uid, sizeof(uid));
    nvr_identity_get_model(model, sizeof(model));

    /* OWLModel 缺失 → 落缺省(NOP12-32),使 DHCP 主机名等 boot 期读取有权威文件 */
    if (access(P_MODEL, F_OK) != 0) {
        if (nvr_identity_set_model(model) == 0)
            NVR_LOGI(TAG, "provision: created %s = %s", P_MODEL, model);
        else
            NVR_LOGW(TAG, "provision: create %s failed", P_MODEL);
    }

    /* tutkdata.json 缺失 → 用缺省生成一份(不覆盖已有真实值) */
    if (access(P_TUTKDATA, F_OK) != 0) {
        if (nvr_identity_set_tutk_creds(NVR_IDENTITY_DEF_IOTCKEY, NVR_IDENTITY_DEF_AVKEY) == 0)
            NVR_LOGI(TAG, "provision: created %s with default creds (待产测/注册写入真实值)", P_TUTKDATA);
        else
            NVR_LOGW(TAG, "provision: create %s failed", P_TUTKDATA);
    }

    NVR_LOGI(TAG, "identity: SN=%s MAC=%s UID=%s MODEL=%s", sn[0] ? sn : "(空)",
             mac[0] ? mac : "(空)", uid[0] ? uid : "(空,TUTK不启动)", model);
    if (!sn[0])  NVR_LOGW(TAG, "SN 未 provision(%s)", P_SN);
    if (!uid[0]) NVR_LOGW(TAG, "UID 未 provision(%s);TUTK P2P 将不启动", P_UID);
}

int nvr_identity_set_tutk_creds(const char *iotckey, const char *avkey)
{
    if (!iotckey && !avkey) return -1;

    /* 读旧值以保留未改字段(任一入参为 NULL 则不改) */
    char cur_ik[64], cur_av[64], cur_acct[64];
    nvr_identity_get_tutk_creds(cur_ik, sizeof(cur_ik), cur_av, sizeof(cur_av));
    nvr_identity_get_av_account(cur_acct, sizeof(cur_acct));   /* 保留 AvAccount,重写时不丢 */
    const char *ik = iotckey ? iotckey : cur_ik;
    const char *av = avkey   ? avkey   : cur_av;

    /* 确保 /User/OWL 存在 */
    mkdir(P_OWL_DIR, 0755);

    cJSON *j = cJSON_CreateObject();
    if (!j) return -1;
    cJSON_AddStringToObject(j, "IotcAuthKey", ik);
    cJSON_AddStringToObject(j, "AvPassword", av);
    cJSON_AddStringToObject(j, "AvAccount", cur_acct);
    char *txt = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!txt) return -1;
    int rc = atomic_write(P_TUTKDATA, txt);
    free(txt);
    return rc;
}
