/***************************************************************************************
 *  nvr_tutk.c — TUTK 设备端 P2P(P2PTunnel 端口映射 + IOTC authkey)
 *
 *  TUNNEL_CONNECT_MANUAL: IOTC_Listen → P2PTunnelServer_Listen(ch=1)
 *  App 侧 PortMapping 到设备 nop_port / rtsp_port → 本地 8089 NOP + RTSP live。
 ***************************************************************************************/
#include "nvr_tutk.h"
#include "nvr_rtsp_live.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(NVR_HAVE_TUTK) && NVR_HAVE_TUTK
#include "IOTCAPIs.h"
#include "IOTCDevice.h"
#include "P2PTunnelAPIs.h"
#include "TUTKGlobalAPIs.h"
#endif

#define NVR_TUTK_LISTEN_SEC 3
#define NVR_TUNNEL_CH       1

typedef struct {
    int in_use;
    int iotc_sid;
} tutk_sess_t;

static struct {
    nvr_tutk_cfg_t  cfg;
    char            uid[32];
    char            key[NVR_TUTK_AUTH_KEY_LEN + 1];
    char            av_key[16];   /* AV password;APP 出图鉴权,供 NOP/AV 层消费 */
    int             inited;
    volatile int    running;
    pthread_t       listen_tid;
    pthread_mutex_t lock;
    tutk_sess_t    *sess;
    int             sess_n;
    int             online;
} g;

static int valid_auth_key(const char *k)
{
    if (!k) return 0;
    size_t n = strlen(k);
    if (n != NVR_TUTK_AUTH_KEY_LEN) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)k[i])) return 0;
    return 1;
}

#if defined(NVR_HAVE_TUTK) && NVR_HAVE_TUTK

static int32_t __stdcall port_verify_cb(uint16_t port, const void *pArg)
{
    const nvr_tutk_cfg_t *c = (const nvr_tutk_cfg_t *)pArg;
    if (!c) return -1;
    if (port == (uint16_t)c->nop_port || port == (uint16_t)c->rtsp_port) return 0;
    return -1;
}

static void session_drop_locked(int idx)
{
    if (idx < 0 || idx >= g.sess_n || !g.sess[idx].in_use) return;
    P2PTunnelServer_Listen_Abort(g.sess[idx].iotc_sid, NVR_TUNNEL_CH);
    IOTC_Session_Close(g.sess[idx].iotc_sid);
    g.sess[idx].in_use = 0;
    if (g.online > 0) g.online--;
}

static void *listen_thread(void *arg)
{
    (void)arg;
    while (g.running) {
        int sid = IOTC_Listen(NVR_TUTK_LISTEN_SEC);
        if (sid < 0) continue;

        int rc = P2PTunnelServer_Listen(sid, NVR_TUNNEL_CH);
        if (rc < 0) {
            IOTC_Session_Close(sid);
            continue;
        }

        pthread_mutex_lock(&g.lock);
        int slot = -1;
        for (int i = 0; i < g.sess_n; i++) {
            if (!g.sess[i].in_use) { slot = i; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < g.sess_n; i++) {
                if (g.sess[i].in_use) { session_drop_locked(i); break; }
            }
            for (int i = 0; i < g.sess_n; i++) {
                if (!g.sess[i].in_use) { slot = i; break; }
            }
        }
        if (slot >= 0) {
            g.sess[slot].in_use = 1;
            g.sess[slot].iotc_sid = sid;
            g.online++;
        } else {
            P2PTunnelServer_Listen_Abort(sid, NVR_TUNNEL_CH);
            IOTC_Session_Close(sid);
        }
        pthread_mutex_unlock(&g.lock);
    }
    return NULL;
}

static int tutk_login(void)
{
    DeviceLoginInput in;
    memset(&in, 0, sizeof(in));
    in.cb = sizeof(in);
    in.authentication_type = AUTHENTICATE_BY_KEY;
    if (g.key[0])
        snprintf(in.auth_key, sizeof(in.auth_key), "%s", g.key);
    return IOTC_Device_LoginEx(g.uid, &in);
}

int nvr_tutk_init(const nvr_tutk_cfg_t *cfg)
{
    if (!cfg || !cfg->uid || !cfg->uid[0]) return -1;
    if (cfg->auth_key && cfg->auth_key[0] && !valid_auth_key(cfg->auth_key)) return -1;

    memset(&g, 0, sizeof(g));
    g.cfg = *cfg;
    if (g.cfg.nop_port <= 0)  g.cfg.nop_port = 8089;
    if (g.cfg.rtsp_port <= 0) g.cfg.rtsp_port = 554;
    if (g.cfg.max_sessions <= 0) g.cfg.max_sessions = 8;

    snprintf(g.uid, sizeof(g.uid), "%s", cfg->uid);
    if (cfg->auth_key && cfg->auth_key[0])
        snprintf(g.key, sizeof(g.key), "%s", cfg->auth_key);
    else
        snprintf(g.key, sizeof(g.key), "%s", NVR_DEF_TUTK_AUTHKEY);
    if (cfg->av_key && cfg->av_key[0])
        snprintf(g.av_key, sizeof(g.av_key), "%s", cfg->av_key);

    g.sess_n = g.cfg.max_sessions;
    g.sess = (tutk_sess_t *)calloc((size_t)g.sess_n, sizeof(tutk_sess_t));
    if (!g.sess) return -1;
    pthread_mutex_init(&g.lock, NULL);

    if (cfg->license_key && cfg->license_key[0])
        TUTK_SDK_Set_License_Key(cfg->license_key);
    else
        TUTK_SDK_Set_License_Key(NVR_DEF_TUTK_LICENSE);

    if (IOTC_Initialize2(0) != IOTC_ER_NoERROR) goto fail;
    if (tutk_login() != IOTC_ER_NoERROR) goto fail_iotc;

    if (P2PTunnelServerInitialize2((uint32_t)g.cfg.max_sessions, 1) != TUNNEL_ER_NoERROR)
        goto fail_login;
    if (P2PTunnelSetConnectionOption(TUNNEL_CONNECT_MANUAL) != TUNNEL_ER_NoERROR)
        goto fail_tunnel;
    if (P2PTunnelServer_Register_Port_Verify(port_verify_cb, &g.cfg) != TUNNEL_ER_NoERROR)
        goto fail_tunnel;

    if (nvr_rtsp_live_start(g.cfg.rtsp_port) != 0)
        goto fail_tunnel;

    g.inited = 1;
    return 0;

fail_tunnel:
    P2PTunnelServerDeInitialize();
fail_login:
    IOTC_DeInitialize();
fail_iotc:
    free(g.sess);
    g.sess = NULL;
    pthread_mutex_destroy(&g.lock);
    return -1;

fail:
    free(g.sess);
    g.sess = NULL;
    pthread_mutex_destroy(&g.lock);
    return -1;
}

int nvr_tutk_start(void)
{
    if (!g.inited || g.running) return g.inited ? 0 : -1;
    g.running = 1;
    if (pthread_create(&g.listen_tid, NULL, listen_thread, NULL) != 0) {
        g.running = 0;
        return -1;
    }
    return 0;
}

void nvr_tutk_stop(void)
{
    if (!g.running) return;
    g.running = 0;
    IOTC_Listen_Exit();
    pthread_join(g.listen_tid, NULL);

    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < g.sess_n; i++)
        if (g.sess[i].in_use) session_drop_locked(i);
    g.online = 0;
    pthread_mutex_unlock(&g.lock);
}

void nvr_tutk_deinit(void)
{
    if (!g.inited) return;
    nvr_tutk_stop();
    nvr_rtsp_live_stop();
    P2PTunnelServerDeInitialize();
    IOTC_DeInitialize();
    pthread_mutex_destroy(&g.lock);
    free(g.sess);
    g.sess = NULL;
    g.inited = 0;
}

int nvr_tutk_update_authkey(const char *auth_key)
{
    if (!auth_key || !valid_auth_key(auth_key)) return -1;
    snprintf(g.key, sizeof(g.key), "%s", auth_key);
    if (g.cfg.auth_key) { /* struct copy pointer; update via restart path in app */ }
    if (!g.inited) return 0;
    if (IOTC_Device_Update_Authkey(g.key) == IOTC_ER_NoERROR) return 0;
    return -2; /* 需 restart */
}

int nvr_tutk_running(void) { return g.inited && g.running; }

int nvr_tutk_online(void)
{
    int n;
    pthread_mutex_lock(&g.lock);
    n = g.online;
    pthread_mutex_unlock(&g.lock);
    return n;
}

const char *nvr_tutk_av_key(void) { return g.av_key; }

#else /* !NVR_HAVE_TUTK — 主机构建 stub */

int nvr_tutk_init(const nvr_tutk_cfg_t *cfg)
{
    (void)cfg;
    return 0;
}

int nvr_tutk_start(void) { return 0; }
void nvr_tutk_stop(void) {}
void nvr_tutk_deinit(void) {}

int nvr_tutk_update_authkey(const char *auth_key)
{
    (void)auth_key;
    return 0;
}

int nvr_tutk_running(void) { return 0; }
int nvr_tutk_online(void) { return 0; }
const char *nvr_tutk_av_key(void) { return ""; }

#endif /* NVR_HAVE_TUTK */
