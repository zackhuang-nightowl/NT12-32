/**
 * @file onvif_map_session.c
 * @brief ONVIF client mapping backend: per-channel session cache + lifecycle.
 *        The backend owns one authenticated nop_onvif_device_t per ONVIF channel
 *        (created lazily on first use) plus the resolved default profile tokens,
 *        so PTZ/OSD/AI/etc. calls reuse a single connection. Backend access is
 *        serialized by a mutex (the vendored ONVIF handle is not reentrant).
 *
 * Whole file is a no-op shell unless built with NOP_ONVIF_MAP (== NOP_WITH_ONVIF):
 * the C ABI it drives (nop_onvif.h) only exists then.
 */
#include "onvif/mapping/nop_onvif_map.h"

#if NOP_ONVIF_MAP

#include "onvif/mapping/onvif_map_internal.h"
#include "nop_sdk/nop_onvif_ext.h"
#include "nop_sdk/nop_nvr_channels.h"
#include "nop_sdk/nop_event.h"
#include "nop_sdk/nop_detect_types.h"
#include "nop_sdk/osal/osal.h"
#include "base/nop_mem.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/** Max channels the session cache indexes (NVRs are well under this). */
#define ONVIF_MAX_SESSIONS 64
/** Default ONVIF device-service path when the registry stores none. */
#define ONVIF_DEFAULT_SERVICE_URL "/onvif/device_service"
/** Default control port when the channel entry leaves it 0. */
#define ONVIF_DEFAULT_PORT 80

struct onvif_session {
    int  channel;
    int  active;                 /* device handle created + authenticated */
    int  resolved;              /* Media1 profiles fetched */
    int  media2_resolved;       /* Media2 profile fetched */
    nop_onvif_device_t *dev;
    char acq_host[64];
    int  acq_port;
    char profile[100];          /* default Media1 profile token */
    char media2_profile[100];   /* default Media2 profile token */
    char video_source[100];     /* first video-source token */
    /* §10 multi-source: the VideoSourceToken this channel is bound to, and the
     * per-source tokens resolved for it (empty bound_source == first source). */
    char bound_source[100];     /* channel_entry.video_source_token */
    char vsc[100];              /* this source's VideoSourceConfiguration token */
    char analytics_cfg[100];    /* this source's VideoAnalyticsConfiguration token */
    char src_profile[100];      /* this source's Media2 ProfileToken (PTZ/stream) */
    char main_venc[100];        /* this source's main VideoEncoderConfiguration token */
    char sub_venc[100];         /* this source's sub VideoEncoderConfiguration token */
};

struct nop_onvif_map_backend {
    nop_nvr_channels_t   *channels;      /* borrowed; must outlive the backend */
    osal_mutex_t         *lock;
    struct nop_event_hub *event_hub;     /* borrowed; set by events_start */
    int                   events_running;
    int                   poller_started;
    pthread_t             poller;
    /* Dedicated per-channel pull-point handles for the events poller, kept
     * SEPARATE from sessions[] so a blocking pull never holds the backend lock
     * or contends with request handlers. LIFECYCLE IS POLLER-OWNED: only the
     * poller thread creates/destroys poll_dev[]/poll_vsct[]. Other threads (a
     * request-path invalidate_channel) never touch them directly — they set
     * poll_drop[ch] under the backend lock and the poller drops+resubscribes at
     * the top of its next sweep. This closes a use-after-free: previously
     * invalidate destroyed a poll_dev while the poller was mid-PullMessages. */
    nop_onvif_device_t   *poll_dev[ONVIF_MAX_SESSIONS];
    int                   poll_drop[ONVIF_MAX_SESSIONS];  /* 1 → poller must drop ch's poll_dev */
    /* §10: each poll channel's bound-source VSC token, resolved once. An event
     * is delivered to this channel only if its Source VSCT matches (so on a
     * multi-source device each channel reports only its own source). */
    char                  poll_vsct[ONVIF_MAX_SESSIONS][100];
    onvif_session_t       sessions[ONVIF_MAX_SESSIONS];
    /* §10: shared request-path device handles, ONE per physical device
     * (host:port). Channels of the same device borrow the same handle
     * (sessions[].dev points in here); the single backend lock serializes use. */
    struct {
        char                host[64];
        int                 port;
        nop_onvif_device_t *dev;
    } devpool[ONVIF_MAX_SESSIONS];
};

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

nop_onvif_map_backend_t *nop_onvif_map_backend_create(void *channels)
{
    nop_onvif_map_backend_t *be;
    int                      i;

    if (!channels)
        return NULL;
    be = (nop_onvif_map_backend_t *)nop_calloc(1, sizeof(*be));
    if (!be)
        return NULL;
    be->lock = osal_mutex_create();
    if (!be->lock) {
        nop_free(be);
        return NULL;
    }
    be->channels = (nop_nvr_channels_t *)channels;
    for (i = 0; i < ONVIF_MAX_SESSIONS; i++)
        be->sessions[i].channel = i;

    nop_onvif_global_init();
    return be;
}

void nop_onvif_map_backend_destroy(nop_onvif_map_backend_t *be)
{
    int i;

    if (!be)
        return;
    nop_onvif_map_events_stop(be);
    /* sessions/poll_dev borrow the process pool — drop refs, do not destroy. */
    for (i = 0; i < ONVIF_MAX_SESSIONS; i++) {
        nop_nvr_channel_entry_t e;
        if (be->sessions[i].dev && be->channels &&
            nop_nvr_channels_get(be->channels, i, &e) && e.host[0])
            nop_onvif_device_drop(e.host, e.port > 0 ? e.port : ONVIF_DEFAULT_PORT);
        be->sessions[i].dev = NULL;
        if (be->poll_dev[i]) {
            /* Dedicated poller handle (normally already freed + nulled by the
             * events_stop above); destroy defensively, never pool-drop. */
            nop_onvif_device_destroy(be->poll_dev[i]);
            be->poll_dev[i] = NULL;
        }
    }
    osal_mutex_destroy(be->lock);
    nop_onvif_global_cleanup();
    nop_free(be);
}

/* ------------------------------------------------------------------------ */
/* Session acquisition                                                      */
/* ------------------------------------------------------------------------ */

/* Borrow the process-wide OnvifDev (one per host:port). Must already be
 * connected by the channel tick; mapping does not create a second handle. */
static nop_onvif_device_t *device_acquire(nop_onvif_map_backend_t *be,
                                          const nop_nvr_channel_entry_t *entry)
{
    int port = entry->port > 0 ? entry->port : ONVIF_DEFAULT_PORT;
    const char *path = entry->service_url[0] ? entry->service_url : ONVIF_DEFAULT_SERVICE_URL;
    nop_onvif_device_t *d;
    (void)be;
    d = nop_onvif_device_retain(entry->host, port, path, 0);
    if (!d)
        return NULL;
    if (!nop_onvif_device_connected(d)) {
        nop_onvif_device_drop(entry->host, port);
        return NULL;
    }
    return d;
}

/* Bring up a channel's device handle + resolve default profile tokens.
 * Called with the backend lock held. @return 0 on success, -1 on failure. */
static int session_ensure(nop_onvif_map_backend_t *be, onvif_session_t *s)
{
    nop_nvr_channel_entry_t entry;

    if (!s->active) {
        if (!nop_nvr_channels_get(be->channels, s->channel, &entry))
            return -1;
        /* 已连接的 handle 即可发 SOAP。backend 不限：NOP 透传失败后的活动区域回落
         * 也走 mapping（一机一 handle，retain 已连接的；事件轮询仍只扫 ONVIF）。 */
        if (entry.host[0] == '\0')
            return -1;
        snprintf(s->bound_source, sizeof(s->bound_source), "%s", entry.video_source_token);
        s->dev = device_acquire(be, &entry);   /* borrowed from process pool */
        if (!s->dev)
            return -1;
        snprintf(s->acq_host, sizeof(s->acq_host), "%s", entry.host);
        s->acq_port = entry.port > 0 ? entry.port : ONVIF_DEFAULT_PORT;
        s->active = 1;
    }

    if (!s->resolved) {
        nop_onvif_source_tokens_t st;
        nop_onvif_profile_t       p;
        int                       cs;
        if (nop_onvif_get_profile(s->dev, 0, &p) == 0)
            snprintf(s->profile, sizeof(s->profile), "%s", p.token);
        if (nop_onvif_get_profile2(s->dev, 0, &p) == 0)
            snprintf(s->media2_profile, sizeof(s->media2_profile), "%s", p.token);
        cs = nop_onvif_device_cached_source(s->dev, s->bound_source, &st);
        if (cs == 0) {
            snprintf(s->vsc, sizeof(s->vsc), "%s", st.vsc_token);
            snprintf(s->analytics_cfg, sizeof(s->analytics_cfg), "%s", st.analytics_cfg);
            snprintf(s->src_profile, sizeof(s->src_profile), "%s", st.profile);
            snprintf(s->main_venc, sizeof(s->main_venc), "%s", st.main_venc);
            snprintf(s->sub_venc, sizeof(s->sub_venc), "%s", st.sub_venc);
            if (st.source_token[0])
                snprintf(s->video_source, sizeof(s->video_source), "%s", st.source_token);
        }
        /* Latch only once we actually resolved a usable token. If the device is
         * connected but its connect-time map cache isn't built yet, everything
         * above returns empty; latching then would cache "" tokens permanently.
         * Leave unresolved so the next begin() retries. */
        if (cs == 0 || s->profile[0] || s->media2_profile[0]) {
            s->resolved = 1;
            s->media2_resolved = 1;
        }
    }
    return 0;
}

onvif_session_t *onvif_session_begin(nop_onvif_map_backend_t *be, int channel)
{
    onvif_session_t *s;

    if (!be || channel < 0 || channel >= ONVIF_MAX_SESSIONS)
        return NULL;
    osal_mutex_lock(be->lock);
    s = &be->sessions[channel];
    if (session_ensure(be, s) != 0) {
        osal_mutex_unlock(be->lock);
        return NULL;
    }
    return s;
}

void onvif_session_end(nop_onvif_map_backend_t *be)
{
    if (be)
        osal_mutex_unlock(be->lock);
}

/* ------------------------------------------------------------------------ */
/* Session invalidation (IP / credentials / backend / source token change)  */
/* ------------------------------------------------------------------------ */

static void session_reset(onvif_session_t *s)
{
    int ch;
    if (!s)
        return;
    if (s->dev && s->acq_host[0])
        nop_onvif_device_drop(s->acq_host, s->acq_port > 0 ? s->acq_port : ONVIF_DEFAULT_PORT);
    ch = s->channel;
    memset(s, 0, sizeof(*s));
    s->channel = ch;
}

static int entry_port(const nop_nvr_channel_entry_t *e)
{
    return (e && e->port > 0) ? e->port : ONVIF_DEFAULT_PORT;
}

static int devpool_users(nop_onvif_map_backend_t *be, const char *host, int port)
{
    int i, n = 0, p = port > 0 ? port : ONVIF_DEFAULT_PORT;
    nop_nvr_channel_entry_t e;

    if (!be || !be->channels || !host || !host[0])
        return 0;
    for (i = 0; i < ONVIF_MAX_SESSIONS; i++) {
        if (!nop_nvr_channels_get(be->channels, i, &e))
            continue;
        if (e.backend != NOP_BACKEND_ONVIF)
            continue;
        if (strcmp(e.host, host) == 0 && entry_port(&e) == p)
            n++;
    }
    return n;
}

static void devpool_drop(nop_onvif_map_backend_t *be, const char *host, int port)
{
    (void)be;
    if (host && host[0])
        nop_onvif_device_drop(host, port > 0 ? port : ONVIF_DEFAULT_PORT);
}

static void sessions_reset_for_device(nop_onvif_map_backend_t *be,
                                      const char *host, int port)
{
    int i, p = port > 0 ? port : ONVIF_DEFAULT_PORT;
    nop_nvr_channel_entry_t e;

    if (!be || !host || !host[0])
        return;
    for (i = 0; i < ONVIF_MAX_SESSIONS; i++) {
        if (!nop_nvr_channels_get(be->channels, i, &e)) {
            session_reset(&be->sessions[i]);
            continue;
        }
        if (e.backend == NOP_BACKEND_ONVIF &&
            strcmp(e.host, host) == 0 && entry_port(&e) == p)
            session_reset(&be->sessions[i]);
    }
}

/* Actually tear down a channel's pull-point. POLLER-THREAD-ONLY (or after the
 * poller is joined in events_stop): never call from a request thread. */
static void poll_drop_channel(nop_onvif_map_backend_t *be, int ch)
{
    if (!be || ch < 0 || ch >= ONVIF_MAX_SESSIONS)
        return;
    if (be->poll_dev[ch]) {
        /* Dedicated poller handle (not the shared request-path pool): unsubscribe
         * then destroy it outright — never pool-drop. */
        nop_onvif_events_unsubscribe(be->poll_dev[ch]);
        nop_onvif_device_destroy(be->poll_dev[ch]);
        be->poll_dev[ch] = NULL;
    }
    be->poll_vsct[ch][0] = '\0';
}

/* Request-thread side of a drop: only FLAG the channel (backend lock held by the
 * caller). The poller performs the real poll_drop_channel() on its own thread at
 * the top of its next sweep, so poll_dev[] is destroyed only where it is used —
 * closing the invalidate-vs-poller use-after-free. */
static void poll_mark_drop_channel(nop_onvif_map_backend_t *be, int ch)
{
    if (be && ch >= 0 && ch < ONVIF_MAX_SESSIONS)
        be->poll_drop[ch] = 1;
}

static void poll_mark_drop_device(nop_onvif_map_backend_t *be, const char *host, int port)
{
    int ch, p = port > 0 ? port : ONVIF_DEFAULT_PORT;
    nop_nvr_channel_entry_t e;

    if (!be || !host || !host[0])
        return;
    for (ch = 0; ch < ONVIF_MAX_SESSIONS; ch++) {
        if (!nop_nvr_channels_get(be->channels, ch, &e)) {
            poll_mark_drop_channel(be, ch);       /* orphaned slot → drop its poller handle */
            continue;
        }
        if (e.backend == NOP_BACKEND_ONVIF &&
            strcmp(e.host, host) == 0 && entry_port(&e) == p)
            poll_mark_drop_channel(be, ch);
    }
}

static int entry_device_connect_changed(const nop_nvr_channel_entry_t *prev,
                                        const nop_nvr_channel_entry_t *cur,
                                        int have_cur)
{
    if (!prev)
        return 1;
    if (!have_cur)
        return 1;
    return strcmp(prev->host, cur->host) != 0 ||
           entry_port(prev) != entry_port(cur) ||
           strcmp(prev->username, cur->username) != 0 ||
           strcmp(prev->password, cur->password) != 0 ||
           prev->backend != cur->backend ||
           strcmp(prev->video_source_token, cur->video_source_token) != 0;
}

void nop_onvif_map_invalidate_channel(nop_onvif_map_backend_t *be, int channel,
                                      const nop_nvr_channel_entry_t *prev)
{
    nop_nvr_channel_entry_t cur;
    int                     have_cur = 0;
    int                     dev_changed;

    if (!be || channel < 0 || channel >= ONVIF_MAX_SESSIONS)
        return;

    osal_mutex_lock(be->lock);

    have_cur = be->channels &&
               nop_nvr_channels_get(be->channels, channel, &cur);
    dev_changed = entry_device_connect_changed(prev, have_cur ? &cur : NULL, have_cur);

    session_reset(&be->sessions[channel]);
    poll_mark_drop_channel(be, channel);   /* defer poll_dev teardown to the poller thread */

    if (prev && prev->host[0] && dev_changed) {
        int pp = entry_port(prev);
        sessions_reset_for_device(be, prev->host, pp);
        poll_mark_drop_device(be, prev->host, pp);
        if (devpool_users(be, prev->host, pp) == 0)
            devpool_drop(be, prev->host, pp);
    }

    if (have_cur && cur.backend == NOP_BACKEND_ONVIF && cur.host[0] && dev_changed) {
        int cp = entry_port(&cur);
        sessions_reset_for_device(be, cur.host, cp);
        poll_mark_drop_device(be, cur.host, cp);
        devpool_drop(be, cur.host, cp);
    }

    osal_mutex_unlock(be->lock);
}

nop_onvif_device_t *onvif_session_dev(onvif_session_t *s) { return s ? s->dev : NULL; }
/* PTZ/stream profile: prefer this channel's bound-source profile (§10 per-source
 * PTZ); fall back to the default Media1 profile when the source is unresolved. */
const char *onvif_session_profile(onvif_session_t *s)
{
    if (!s) return "";
    return s->src_profile[0] ? s->src_profile : s->profile;
}
const char *onvif_session_main_venc(onvif_session_t *s) { return s ? s->main_venc : ""; }
const char *onvif_session_sub_venc(onvif_session_t *s) { return s ? s->sub_venc : ""; }
const char *onvif_session_media2_profile(onvif_session_t *s) { return s ? s->media2_profile : ""; }
const char *onvif_session_video_source(onvif_session_t *s) { return s ? s->video_source : ""; }

/* §10: find the NVR channel bound to (this device, @p source_token). Same device
 * = same host:port as @p ref_channel. Returns the channel index, or ref_channel
 * itself when it is a single-source (empty-binding) channel, or -1. */
int onvif_backend_channel_for_source(nop_onvif_map_backend_t *be, int ref_channel,
                                     const char *source_token)
{
    nop_nvr_channel_entry_t ref, e;
    int i;
    if (!be || !be->channels)
        return -1;
    if (!nop_nvr_channels_get(be->channels, ref_channel, &ref))
        return -1;
    for (i = 0; i < ONVIF_MAX_SESSIONS; i++) {
        if (!nop_nvr_channels_get(be->channels, i, &e))
            continue;
        if (e.backend != NOP_BACKEND_ONVIF)
            continue;
        if (strcmp(e.host, ref.host) != 0 || entry_port(&e) != entry_port(&ref))
            continue;
        if (source_token && source_token[0]) {
            if (strcmp(e.video_source_token, source_token) == 0)
                return i;
        } else if (e.video_source_token[0] == '\0') {
            return i;
        }
    }
    if (ref.video_source_token[0] == '\0')
        return ref_channel;   /* single-source device: the only source is this channel */
    return -1;
}

/* §10: copy this channel's bound-source VSC token. @return 0 on success (mirrors
 * nop_onvif_media2_video_source_token so call sites are a drop-in swap), -1 if
 * unresolved. */
int onvif_session_vsc(onvif_session_t *s, char *out, unsigned size)
{
    if (!s || !out || size == 0) return -1;
    if (s->vsc[0] == '\0') { out[0] = '\0'; return -1; }
    snprintf(out, size, "%s", s->vsc);
    return 0;
}

/* §10: copy this channel's bound-source VideoAnalyticsConfiguration token.
 * @return 0 on success (mirrors nop_onvif_analytics_config_token), -1 if none. */
int onvif_session_analytics_cfg(onvif_session_t *s, char *out, unsigned size)
{
    if (!s || !out || size == 0) return -1;
    if (s->analytics_cfg[0] == '\0') { out[0] = '\0'; return -1; }
    snprintf(out, size, "%s", s->analytics_cfg);
    return 0;
}

const char *onvif_session_bound_source(onvif_session_t *s) { return s ? s->bound_source : ""; }

/* Bind state for graceful NOP errors: 1 = channel has an ONVIF camera bound
 * (backend ONVIF + host set), 0 otherwise. Reads the registry, not the session,
 * so it works even when onvif_session_begin() failed to connect. */
int onvif_map_channel_bound(nop_onvif_map_backend_t *be, int channel)
{
    nop_nvr_channel_entry_t e;
    if (!be || !be->channels || channel < 0 || channel >= ONVIF_MAX_SESSIONS)
        return 0;
    if (!nop_nvr_channels_get(be->channels, channel, &e))
        return 0;
    return (e.backend == NOP_BACKEND_ONVIF && e.host[0] != '\0') ? 1 : 0;
}

/* ------------------------------------------------------------------------ */
/* §1 Events poller — one dedicated pull-point per ONVIF channel, mapped to  */
/* the shared nop_event_hub (which the app already bridges to longpoll/8012). */
/* ------------------------------------------------------------------------ */

/* Map an ONVIF notification TOPIC to a NOP detection type (MAX = not by topic).
 * Line/Field/Motion carry the kind in the topic. NOTE: the ObjectDetection topic
 * (tns1:RuleEngine/ObjectDetection/Object) carries NO class — the class lives in
 * Data.ClassTypes, handled by class_to_detect(). */
static nop_detect_type_t topic_to_detect(const char *t)
{
    if (!t) return NOP_DETECT_TYPE_MAX;
    if (strstr(t, "LineDetector") || strstr(t, "LineCross"))   return NOP_DETECT_LINE_CROSS;
    if (strstr(t, "FieldDetector"))                            return NOP_DETECT_FIELD_INTRUSION;
    if (strstr(t, "CellMotion") || strstr(t, "Motion"))        return NOP_DETECT_MOTION;
    /* Class names may also appear directly in vendor topics: */
    if (strstr(t, "Vehicle"))                                  return NOP_DETECT_VEHICLE;
    if (strstr(t, "Human") || strstr(t, "Person"))             return NOP_DETECT_HUMAN;
    if (strstr(t, "Face"))                                     return NOP_DETECT_FACE;
    if (strstr(t, "Animal"))                                   return NOP_DETECT_ANIMAL;
    return NOP_DETECT_TYPE_MAX;
}

/* Map an ObjectDetection Data.ClassTypes value to a NOP detection type. */
static nop_detect_type_t class_to_detect(const char *c)
{
    if (!c || !c[0]) return NOP_DETECT_TYPE_MAX;
    if (strstr(c, "Vehicle"))                    return NOP_DETECT_VEHICLE;
    if (strstr(c, "Human") || strstr(c, "Person")) return NOP_DETECT_HUMAN;
    if (strstr(c, "Face"))                       return NOP_DETECT_FACE;
    if (strstr(c, "Animal"))                     return NOP_DETECT_ANIMAL;
    return NOP_DETECT_TYPE_MAX;
}

/* ONVIF object class -> NOP trigger name (for the event's classType 回显). */
static const char *nop_evt_class(const char *c)
{
    if (!c || !c[0]) return NULL;
    if (strstr(c, "Vehicle"))                      return "vehicle";
    if (strstr(c, "Human") || strstr(c, "Person")) return "human";
    if (strstr(c, "Face"))                         return "face";
    if (strstr(c, "Animal"))                       return "animal";
    return NULL;
}

/* ONVIF line-cross Direction (Left/Right) -> NOP direction (BA/AB). */
static const char *nop_evt_dir(const char *d)
{
    if (!d || !d[0]) return NULL;
    if (!strcmp(d, "Left"))  return "BA";
    if (!strcmp(d, "Right")) return "AB";
    return NULL;
}

/* Lazily create a channel's pull-point handle. @return handle or NULL. */
static nop_onvif_device_t *poll_dev_ensure(nop_onvif_map_backend_t *be, int ch,
                                           const nop_nvr_channel_entry_t *e)
{
    if (be->poll_dev[ch])
        return be->poll_dev[ch];
    {
        int port = entry_port(e);
        const char *path = e->service_url[0] ? e->service_url : ONVIF_DEFAULT_SERVICE_URL;
        /* DEDICATED poller handle — created + owned solely by the poller thread,
         * NOT the shared request-path pool. A blocking PullMessages must never run
         * concurrently with request SOAP on the same non-reentrant handle, so the
         * poller keeps its own connection (one subscription per device: only the
         * owner channel reaches here). */
        nop_onvif_device_t *d = nop_onvif_device_create(e->host, port, path, 0);
        int j;
        if (!d)
            return NULL;
        if (e->username[0])
            nop_onvif_device_set_auth(d, e->username, e->password);
        nop_onvif_device_set_timeout(d, 5000);
        if (nop_onvif_device_connect(d, e->username, e->password) != 0 ||
            nop_onvif_events_create_pullpoint(d) != 0) {
            fprintf(stderr, "[onvif_evt] ch%d %s:%d CreatePullPointSubscription 失败"
                            "(相机不支持事件/鉴权失败?) → 本通道不上报事件\n",
                    ch, e->host, port);
            nop_onvif_device_destroy(d);
            return NULL;
        }
        fprintf(stderr, "[onvif_evt] ch%d %s:%d 事件订阅成功,开始 PullMessages\n",
                ch, e->host, port);
        /* Resolve the bound-source VSC of EVERY channel on this device (same
         * host:port) once, so pulled events fan out to the right channel by their
         * Source VSCT. A dedicated handle isn't in the request-path cache, so
         * resolve live (GetProfiles) rather than nop_onvif_device_cached_source. */
        for (j = 0; j < ONVIF_MAX_SESSIONS; j++) {
            nop_nvr_channel_entry_t   ej;
            nop_onvif_source_tokens_t st;
            if (!nop_nvr_channels_get(be->channels, j, &ej))
                continue;
            if (ej.backend != NOP_BACKEND_ONVIF)
                continue;
            if (strcmp(ej.host, e->host) != 0 || entry_port(&ej) != port)
                continue;
            be->poll_vsct[j][0] = '\0';
            if (nop_onvif_resolve_source(d, ej.video_source_token, &st) == 0)
                snprintf(be->poll_vsct[j], sizeof(be->poll_vsct[j]), "%s", st.vsc_token);
        }
        be->poll_dev[ch] = d;
    }
    return be->poll_dev[ch];
}

/* Is @p ch the owner (lowest-index enabled ONVIF channel) of its device? Only
 * the owner subscribes/pulls, so a multi-source device has ONE subscription. */
static int poll_device_owner(nop_onvif_map_backend_t *be, int ch,
                             const nop_nvr_channel_entry_t *e)
{
    int j;
    for (j = 0; j < ch; j++) {
        nop_nvr_channel_entry_t ej;
        if (!nop_nvr_channels_get(be->channels, j, &ej))
            continue;
        if (ej.backend != NOP_BACKEND_ONVIF || !ej.enabled)
            continue;
        if (strcmp(ej.host, e->host) == 0 && entry_port(&ej) == entry_port(e))
            return 0;                     /* a lower-index channel owns the device */
    }
    return 1;
}

/* Route an event (by its Source VSCT) to the device channel bound to that
 * source; fall back to the owner channel when unknown. */
static int poll_route_channel(nop_onvif_map_backend_t *be, int owner_ch,
                              const nop_nvr_channel_entry_t *e, const char *vsct)
{
    int j;
    if (!vsct || !vsct[0])
        return owner_ch;
    for (j = 0; j < ONVIF_MAX_SESSIONS; j++) {
        nop_nvr_channel_entry_t ej;
        if (be->poll_vsct[j][0] == '\0' || strcmp(be->poll_vsct[j], vsct) != 0)
            continue;
        if (!nop_nvr_channels_get(be->channels, j, &ej))
            continue;
        if (strcmp(ej.host, e->host) == 0 && entry_port(&ej) == entry_port(e))
            return j;
    }
    return owner_ch;
}

static void *poller_main(void *arg)
{
    nop_onvif_map_backend_t *be = (nop_onvif_map_backend_t *)arg;

    while (be->events_running) {
        int ch;
        for (ch = 0; ch < ONVIF_MAX_SESSIONS && be->events_running; ch++) {
            nop_nvr_channel_entry_t e;
            nop_onvif_event_msg_t   msgs[8];
            nop_onvif_device_t     *d;
            int                     n, i;

            /* Consume a deferred drop request (set by invalidate_channel under
             * the backend lock). Doing the teardown HERE, on the poller thread,
             * is what makes poll_dev[] single-owner and free of the cross-thread
             * use-after-free. Brief lock only for the flag — never across pull. */
            {
                int drop;
                osal_mutex_lock(be->lock);
                drop = be->poll_drop[ch];
                be->poll_drop[ch] = 0;
                osal_mutex_unlock(be->lock);
                if (drop)
                    poll_drop_channel(be, ch);
            }

            if (!nop_nvr_channels_get(be->channels, ch, &e))
                continue;
            if (e.backend != NOP_BACKEND_ONVIF || !e.enabled)
                continue;
            /* One subscription per device: only the owner channel pulls; it fans
             * events out to sibling source-channels by VSCT below. */
            if (!poll_device_owner(be, ch, &e))
                continue;
            d = poll_dev_ensure(be, ch, &e);
            if (!d)
                continue;

            n = nop_onvif_events_pull_msgs(d, 1, 8, msgs);   /* 1s block */
            if (n > 0)
                fprintf(stderr, "[onvif_evt] ch%d 收到 %d 条 ONVIF 事件消息\n", ch, n);
            for (i = 0; i < n; i++) {
                nop_detect_type_t t = topic_to_detect(msgs[i].topic);
                nop_event_t ev;
                if (t == NOP_DETECT_TYPE_MAX)
                    t = class_to_detect(msgs[i].class_types);  /* ObjectDetection: class from Data */
                if (t == NOP_DETECT_TYPE_MAX)
                    continue;
                memset(&ev, 0, sizeof(ev));
                /* §10 fan-out: route to the channel bound to the event's source. */
                ev.channel      = poll_route_channel(be, ch, &e, msgs[i].source_vsct);
                ev.type         = t;
                ev.timestamp_ms = osal_time_ms();
                {
                    /* Data 回显: attach the triggering classType + crossing
                     * direction as extendData JSON. `extra` is borrowed for the
                     * synchronous publish only (nop_event.h contract). */
                    char        extra[128];
                    const char *cls = nop_evt_class(msgs[i].class_types);
                    const char *dir = nop_evt_dir(msgs[i].direction);
                    /* cls/dir are short fixed literals; one bounded snprintf per
                     * case avoids any offset accumulation (and its overflow). */
                    if (cls && dir)
                        snprintf(extra, sizeof(extra),
                                 "{\"classType\":\"%s\",\"direction\":\"%s\"}", cls, dir);
                    else if (cls)
                        snprintf(extra, sizeof(extra), "{\"classType\":\"%s\"}", cls);
                    else if (dir)
                        snprintf(extra, sizeof(extra), "{\"direction\":\"%s\"}", dir);
                    if (cls || dir)
                        ev.extra_json = extra;
                    if (be->event_hub)
                        nop_event_publish((nop_event_hub_t *)be->event_hub, &ev);
                }
            }
        }
        osal_sleep_ms(200);
    }
    return NULL;
}

nop_status_t nop_onvif_map_events_start(nop_onvif_map_backend_t *be, void *event_hub)
{
    if (!be)
        return NOP_ERR_PARAM;
    if (be->poller_started)
        return NOP_OK;
    be->event_hub      = (struct nop_event_hub *)event_hub;
    be->events_running = 1;
    if (pthread_create(&be->poller, NULL, poller_main, be) != 0) {
        be->events_running = 0;
        return NOP_ERR_INTERNAL;
    }
    be->poller_started = 1;
    return NOP_OK;
}

void nop_onvif_map_events_stop(nop_onvif_map_backend_t *be)
{
    int ch;
    if (!be || !be->poller_started)
        return;
    be->events_running = 0;
    pthread_join(be->poller, NULL);
    be->poller_started = 0;
    for (ch = 0; ch < ONVIF_MAX_SESSIONS; ch++) {
        if (be->poll_dev[ch]) {
            nop_onvif_events_unsubscribe(be->poll_dev[ch]);
            nop_onvif_device_destroy(be->poll_dev[ch]);
            be->poll_dev[ch] = NULL;
        }
    }
}

#endif /* NOP_ONVIF_MAP */
