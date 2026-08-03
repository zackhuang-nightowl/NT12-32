/***************************************************************************************
 *  nvr_tutk.c — ④ TUTK 设备端 P2P glue。
 *
 *  流程：IOTC_Initialize2 → IOTC_Device_LoginEx(uid,auth_key) →
 *        监听线程 { IOTC_Listen → avServStart2 → 登记会话 } →
 *        nvr_tutk_send_video → 对每个在线 AV 通道 avSendFrameData。
 ***************************************************************************************/
#include "nvr_tutk.h"

#include "IOTCAPIs.h"
#include "IOTCDevice.h"
#include "AVAPIs.h"
#include "AVServer.h"
#include "AVFRAMEINFO.h"

#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define NVR_TUTK_MAX_SESS 8
#define NVR_TUTK_LISTEN_TO 3         /* IOTC_Listen 超时秒 */

typedef struct {
    int in_use;
    int sid;                          /* IOTC session id */
    int av;                           /* av server channel id */
} tutk_sess_t;

static struct {
    char        uid[24];
    char        key[10];
    int         inited;
    volatile int running;
    pthread_t   listen_tid;
    pthread_mutex_t lock;
    tutk_sess_t sess[NVR_TUTK_MAX_SESS];
} g;

/* 监听线程：接受 App 连接 → 起 AV server → 登记 */
static void *listen_thread(void *arg)
{
    (void)arg;
    while (g.running) {
        int sid = IOTC_Listen(NVR_TUTK_LISTEN_TO);   /* 阻塞至有连接或超时 */
        if (sid < 0) continue;

        /* TODO(鉴权): avServStart2 的 authFn 校验 App 账号/密码；此处传 NULL 免鉴权占位。
         * servType/channel 按业务；单通道用 0。 */
        int av = avServStart2(sid, NULL, 20, 0, 0);
        if (av < 0) { IOTC_Session_Close(sid); continue; }

        pthread_mutex_lock(&g.lock);
        int placed = 0;
        for (int i = 0; i < NVR_TUTK_MAX_SESS; i++) {
            if (!g.sess[i].in_use) {
                g.sess[i].in_use = 1; g.sess[i].sid = sid; g.sess[i].av = av;
                placed = 1; break;
            }
        }
        pthread_mutex_unlock(&g.lock);
        if (!placed) { avServStop(av); IOTC_Session_Close(sid); }  /* 满员拒接 */
    }
    return NULL;
}

int nvr_tutk_init(const char *uid, const char *auth_key)
{
    if (!uid || !auth_key) return -1;
    memset(&g, 0, sizeof(g));
    snprintf(g.uid, sizeof(g.uid), "%s", uid);
    snprintf(g.key, sizeof(g.key), "%s", auth_key);
    pthread_mutex_init(&g.lock, NULL);

    if (IOTC_Initialize2(0) != IOTC_ER_NoERROR) return -1;
    avInitialize(NVR_TUTK_MAX_SESS);

    DeviceLoginInput in;
    memset(&in, 0, sizeof(in));
    in.cb = sizeof(in);
    in.authentication_type = AUTHENTICATE_BY_KEY;   /* 用 auth_key 登录 */
    snprintf(in.auth_key, sizeof(in.auth_key), "%s", auth_key);
    if (IOTC_Device_LoginEx(uid, &in) != IOTC_ER_NoERROR) return -1;

    g.inited = 1;
    return 0;
}

int nvr_tutk_start(void)
{
    if (!g.inited) return -1;
    g.running = 1;
    if (pthread_create(&g.listen_tid, NULL, listen_thread, NULL) != 0) { g.running = 0; return -1; }
    return 0;
}

void nvr_tutk_stop(void)
{
    if (!g.running) return;
    g.running = 0;
    IOTC_Listen_Exit();                 /* 令 IOTC_Listen 立即返回 */
    pthread_join(g.listen_tid, NULL);
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < NVR_TUTK_MAX_SESS; i++) {
        if (g.sess[i].in_use) {
            avServStop(g.sess[i].av);
            IOTC_Session_Close(g.sess[i].sid);
            g.sess[i].in_use = 0;
        }
    }
    pthread_mutex_unlock(&g.lock);
}

void nvr_tutk_deinit(void)
{
    if (!g.inited) return;
    nvr_tutk_stop();
    /* 无独立 device logoff：IOTC_DeInitialize 释放设备登录与全部资源 */
    avDeInitialize();
    IOTC_DeInitialize();
    pthread_mutex_destroy(&g.lock);
    g.inited = 0;
}

int nvr_tutk_send_video(int chn, const uint8_t *data, int len, int codec, int is_key, uint32_t ts_ms)
{
    (void)chn;   /* 单 AV 通道示例；多通道可按 chn 映射 avServStart2 的 channel */
    if (!g.inited || !data || len <= 0) return -1;

    FRAMEINFO_t fi;
    memset(&fi, 0, sizeof(fi));
    fi.codec_id  = (codec == 1) ? MEDIA_CODEC_VIDEO_HEVC : MEDIA_CODEC_VIDEO_H264;
    fi.flags     = is_key ? IPC_FRAME_FLAG_IFRAME : IPC_FRAME_FLAG_PBFRAME;
    fi.timestamp = ts_ms;

    int sent = 0;
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < NVR_TUTK_MAX_SESS; i++) {
        if (g.sess[i].in_use) {
            if (avSendFrameData(g.sess[i].av, (const char *)data, len, &fi, sizeof(fi)) >= 0)
                sent++;
            /* TODO: avSendFrameData 返回 AV_ER_EXCEED_MAX_SIZE/会话断开时清理该 sess */
        }
    }
    pthread_mutex_unlock(&g.lock);
    return sent;
}

int nvr_tutk_online(void)
{
    int n = 0;
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < NVR_TUTK_MAX_SESS; i++) if (g.sess[i].in_use) n++;
    pthread_mutex_unlock(&g.lock);
    return n;
}
