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
    onvif_session_t       sessions[ONVIF_MAX_SESSIONS];
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
    for (i = 0; i < ONVIF_MAX_SESSIONS; i++) {
        if (be->sessions[i].dev)
            nop_onvif_device_destroy(be->sessions[i].dev);
    }
    osal_mutex_destroy(be->lock);
    nop_onvif_global_cleanup();
    nop_free(be);
}

/* ------------------------------------------------------------------------ */
/* Session acquisition                                                      */
/* ------------------------------------------------------------------------ */

/* Bring up a channel's device handle + resolve default profile tokens.
 * Called with the backend lock held. @return 0 on success, -1 on failure. */
static int session_ensure(nop_onvif_map_backend_t *be, onvif_session_t *s)
{
    nop_nvr_channel_entry_t entry;
    int                     port;

    if (!s->active) {
        if (!nop_nvr_channels_get(be->channels, s->channel, &entry))
            return -1;
        if (entry.backend != NOP_BACKEND_ONVIF || entry.host[0] == '\0')
            return -1;
        port = entry.port > 0 ? entry.port : ONVIF_DEFAULT_PORT;
        s->dev = nop_onvif_device_create(entry.host, port,
                                         ONVIF_DEFAULT_SERVICE_URL, 0);
        if (!s->dev)
            return -1;
        if (entry.username[0] != '\0')
            nop_onvif_device_set_auth(s->dev, entry.username, entry.password);
        nop_onvif_device_set_timeout(s->dev, 5000);
        s->active = 1;
    }

    if (!s->resolved) {
        nop_onvif_profile_t p;
        int n = nop_onvif_get_profiles(s->dev);
        if (n > 0 && nop_onvif_get_profile(s->dev, 0, &p) == 0)
            snprintf(s->profile, sizeof(s->profile), "%s", p.token);
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
const char *onvif_session_profile(onvif_session_t *s) { return s ? s->profile : ""; }
const char *onvif_session_media2_profile(onvif_session_t *s) { return s ? s->media2_profile : ""; }
const char *onvif_session_video_source(onvif_session_t *s) { return s ? s->video_source : ""; }

/* ------------------------------------------------------------------------ */
/* §1 Events poller — one dedicated pull-point per ONVIF channel, mapped to  */
/* the shared nop_event_hub (which the app already bridges to longpoll/8012). */
/* ------------------------------------------------------------------------ */

/* Map an ONVIF notification topic to a NOP detection type (MAX = ignore). */
static nop_detect_type_t topic_to_detect(const char *t)
{
    if (!t) return NOP_DETECT_TYPE_MAX;
    if (strstr(t, "LineDetector") || strstr(t, "LineCross"))   return NOP_DETECT_LINE_CROSS;
    if (strstr(t, "FieldDetector") || strstr(t, "Field"))      return NOP_DETECT_FIELD_INTRUSION;
    if (strstr(t, "Vehicle"))                                  return NOP_DETECT_VEHICLE;
    if (strstr(t, "Human") || strstr(t, "Person"))             return NOP_DETECT_HUMAN;
    if (strstr(t, "Face"))                                     return NOP_DETECT_FACE;
    if (strstr(t, "Animal"))                                   return NOP_DETECT_ANIMAL;
    if (strstr(t, "CellMotion") || strstr(t, "Motion"))        return NOP_DETECT_MOTION;
    return NOP_DETECT_TYPE_MAX;
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
        be->poll_dev[ch] = d;
    }
    return be->poll_dev[ch];
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
            d = poll_dev_ensure(be, ch, &e);
            if (!d)
                continue;

            n = nop_onvif_events_pull_msgs(d, 1, 8, msgs);   /* 1s block */
            for (i = 0; i < n; i++) {
                nop_detect_type_t t = topic_to_detect(msgs[i].topic);
                nop_event_t ev;
                if (t == NOP_DETECT_TYPE_MAX)
                    continue;
                memset(&ev, 0, sizeof(ev));
                ev.channel      = ch;
                ev.type         = t;
                ev.timestamp_ms = osal_time_ms();
                if (be->event_hub)
                    nop_event_publish((nop_event_hub_t *)be->event_hub, &ev);
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
