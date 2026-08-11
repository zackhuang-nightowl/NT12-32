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
     * or contends with request handlers. */
    nop_onvif_device_t   *poll_dev[ONVIF_MAX_SESSIONS];
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
    /* sessions[].dev are borrowed from devpool — free the pool once per device. */
    for (i = 0; i < ONVIF_MAX_SESSIONS; i++) {
        if (be->devpool[i].dev)
            nop_onvif_device_destroy(be->devpool[i].dev);
    }
    osal_mutex_destroy(be->lock);
    nop_onvif_global_cleanup();
    nop_free(be);
}

/* ------------------------------------------------------------------------ */
/* Session acquisition                                                      */
/* ------------------------------------------------------------------------ */

/* Return the shared request-path device handle for @p entry's host:port,
 * creating + authenticating it on first use. Borrowed (owned by devpool);
 * channels of the same device share it. Called with the backend lock held. */
static nop_onvif_device_t *device_acquire(nop_onvif_map_backend_t *be,
                                          const nop_nvr_channel_entry_t *entry)
{
    int i, port = entry->port > 0 ? entry->port : ONVIF_DEFAULT_PORT;

    for (i = 0; i < ONVIF_MAX_SESSIONS; i++)          /* reuse existing device */
        if (be->devpool[i].dev && be->devpool[i].port == port &&
            strcmp(be->devpool[i].host, entry->host) == 0)
            return be->devpool[i].dev;

    for (i = 0; i < ONVIF_MAX_SESSIONS; i++) {        /* else create in a free slot */
        nop_onvif_device_t *d;
        if (be->devpool[i].dev)
            continue;
        d = nop_onvif_device_create(entry->host, port, ONVIF_DEFAULT_SERVICE_URL, 0);
        if (!d)
            return NULL;
        if (entry->username[0] != '\0')
            nop_onvif_device_set_auth(d, entry->username, entry->password);
        nop_onvif_device_set_timeout(d, 5000);
        snprintf(be->devpool[i].host, sizeof(be->devpool[i].host), "%s", entry->host);
        be->devpool[i].port = port;
        be->devpool[i].dev  = d;
        return d;
    }
    return NULL;                                      /* pool exhausted */
}

/* Bring up a channel's device handle + resolve default profile tokens.
 * Called with the backend lock held. @return 0 on success, -1 on failure. */
static int session_ensure(nop_onvif_map_backend_t *be, onvif_session_t *s)
{
    nop_nvr_channel_entry_t entry;

    if (!s->active) {
        if (!nop_nvr_channels_get(be->channels, s->channel, &entry))
            return -1;
        if (entry.backend != NOP_BACKEND_ONVIF || entry.host[0] == '\0')
            return -1;
        snprintf(s->bound_source, sizeof(s->bound_source), "%s", entry.video_source_token);
        s->dev = device_acquire(be, &entry);   /* borrowed, shared per device */
        if (!s->dev)
            return -1;
        s->active = 1;
    }

    if (!s->resolved) {
        nop_onvif_profile_t       p;
        nop_onvif_source_tokens_t st;
        int n = nop_onvif_get_profiles(s->dev);
        if (n > 0 && nop_onvif_get_profile(s->dev, 0, &p) == 0)
            snprintf(s->profile, sizeof(s->profile), "%s", p.token);
        /* Resolve this channel's bound video source once (VSC + analytics cfg).
         * bound_source == "" resolves the first source (single-source cameras),
         * preserving the previous behavior exactly. */
        if (nop_onvif_resolve_source(s->dev, s->bound_source, &st) == 0) {
            snprintf(s->vsc, sizeof(s->vsc), "%s", st.vsc_token);
            snprintf(s->analytics_cfg, sizeof(s->analytics_cfg), "%s", st.analytics_cfg);
            snprintf(s->src_profile, sizeof(s->src_profile), "%s", st.profile);
            snprintf(s->main_venc, sizeof(s->main_venc), "%s", st.main_venc);
            snprintf(s->sub_venc, sizeof(s->sub_venc), "%s", st.sub_venc);
        }
        s->resolved = 1;   /* cache even a zero result: don't refetch per call */
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
        if (strcmp(e.host, ref.host) != 0 || e.port != ref.port)
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
        int port = e->port > 0 ? e->port : ONVIF_DEFAULT_PORT;
        nop_onvif_device_t *d = nop_onvif_device_create(e->host, port,
                                                        ONVIF_DEFAULT_SERVICE_URL, 0);
        if (!d) return NULL;
        if (e->username[0])
            nop_onvif_device_set_auth(d, e->username, e->password);
        nop_onvif_device_set_timeout(d, 5000);
        if (nop_onvif_events_create_pullpoint(d) != 0) {
            nop_onvif_device_destroy(d);
            return NULL;
        }
        /* One subscription per device: resolve the bound-source VSC of EVERY
         * channel on this device (same host:port) once, so pulled events can be
         * fanned out to the right channel by their Source VSCT. */
        {
            int j;
            for (j = 0; j < ONVIF_MAX_SESSIONS; j++) {
                nop_nvr_channel_entry_t   ej;
                nop_onvif_source_tokens_t st;
                if (!nop_nvr_channels_get(be->channels, j, &ej))
                    continue;
                if (ej.backend != NOP_BACKEND_ONVIF)
                    continue;
                if (strcmp(ej.host, e->host) != 0 || ej.port != e->port)
                    continue;
                be->poll_vsct[j][0] = '\0';
                if (nop_onvif_resolve_source(d, ej.video_source_token, &st) == 0)
                    snprintf(be->poll_vsct[j], sizeof(be->poll_vsct[j]), "%s", st.vsc_token);
            }
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
        if (strcmp(ej.host, e->host) == 0 && ej.port == e->port)
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
        if (strcmp(ej.host, e->host) == 0 && ej.port == e->port)
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
                    if (cls || dir) {
                        int off = snprintf(extra, sizeof(extra), "{");
                        if (cls)
                            off += snprintf(extra + off, sizeof(extra) - off,
                                            "\"classType\":\"%s\"", cls);
                        if (dir)
                            off += snprintf(extra + off, sizeof(extra) - off,
                                            "%s\"direction\":\"%s\"", cls ? "," : "", dir);
                        snprintf(extra + off, sizeof(extra) - off, "}");
                        ev.extra_json = extra;
                    }
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
