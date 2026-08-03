/**
 * @file cap_event.c
 * @brief CAP_NOTIFY event handlers: calendar/list queries and event download.
 *        Covers X_NightOwl_queryEventCalendar / queryEventList /
 *        queryEventListWithSpecificOrder / getEventDownloadCapability /
 *        getEventDownloadProgress / startEventDownload / startEventDownloadwithURL.
 *        Gated by CAP_NOTIFY. Returns spec-shaped responses with sensible defaults
 *        (empty event lists, zero progress) since no event store is wired here.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/nop_event.h"

#include <string.h>

/* Retrieves calendar info on when events occured. No store wired: echo the
 * requested resolution and return an empty list of time intervals. */
static nop_status_t handle_query_event_calendar(const nop_request_t *request,
                                                 nop_response_t *response,
                                                 void *handler_context)
{
    const char *resolution;
    (void)handler_context;

    if (!nop_json_has(request->args, "resolution") ||
        !nop_json_has(request->args, "year") ||
        !nop_json_has(request->args, "month") ||
        !nop_json_has(request->args, "channels"))
        return NOP_ERR_PARAM;

    resolution = nop_json_str(request->args, "resolution", "day");

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "resolution", resolution);
    nop_json_add(response->content, "list", nop_json_arr());
    return NOP_OK;
}

/* Retrieves a list of events. No store wired: echo the queryID and return an
 * empty event list. */
static nop_status_t handle_query_event_list(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const nop_business_context_t *context = (const nop_business_context_t *)handler_context;
    nop_json_t *list;
    int         want;

    if (!nop_json_has(request->args, "startTime") ||
        !nop_json_has(request->args, "eventTypes") ||
        !nop_json_has(request->args, "channels") ||
        !nop_json_has(request->args, "command") ||
        !nop_json_has(request->args, "listNumber"))
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "queryID",
                     nop_json_num(request->args, "queryID", 0));
    list = nop_json_arr();

    /* Populate from the recent-event store when a hub is attached. */
    want = (int)nop_json_num(request->args, "listNumber", 20);
    if (want <= 0 || want > 64)
        want = 64;
    if (context && context->event_hub) {
        nop_event_t recent[64];
        int n = nop_event_hub_recent((nop_event_hub_t *)context->event_hub, recent, want);
        int i;
        for (i = 0; i < n; i++) {
            nop_json_t *entry = nop_json_obj();
            nop_json_add_int(entry, "channel", recent[i].channel);
            nop_json_add_str(entry, "eventType", nop_detect_type_name(recent[i].type));
            nop_json_add_int(entry, "msgType", (double)nop_event_msgtype_code(recent[i].type));
            nop_json_add_int(entry, "time", (double)recent[i].timestamp_ms);
            nop_json_arr_push(list, entry);
        }
    }
    nop_json_add(response->content, "list", list);
    return NOP_OK;
}

/* Like queryEventList but with an explicit query direction. No store wired:
 * return an empty event list. Per spec, omit queryID when there are no items. */
static nop_status_t handle_query_event_list_with_specific_order(const nop_request_t *request,
                                                                nop_response_t *response,
                                                                void *handler_context)
{
    (void)handler_context;

    if (!nop_json_has(request->args, "startTime") ||
        !nop_json_has(request->args, "order") ||
        !nop_json_has(request->args, "eventTypes") ||
        !nop_json_has(request->args, "channels") ||
        !nop_json_has(request->args, "command") ||
        !nop_json_has(request->args, "listNumber"))
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();
    nop_json_add(response->content, "list", nop_json_arr());
    return NOP_OK;
}

/* Retrieves the event download capability. Reports dynamic produce mode with an
 * mp4 container, the common default. */
static nop_status_t handle_get_event_download_capability(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    (void)request; (void)handler_context;

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "produceMode", "dynamic");
    nop_json_add_str(response->content, "container", "mp4");
    return NOP_OK;
}

/* Retrieves the progress of a download preparation. No conversion engine wired:
 * report zero percent. downloadUrl/filesize are only sent when percentage is
 * 100, so they are omitted here per spec. */
static nop_status_t handle_get_event_download_progress(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    (void)handler_context;

    if (!nop_json_has(request->args, "processId"))
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "percentage", 0);
    return NOP_OK;
}

/* Starts preparing a recording segment for download (dynamic produce mode).
 * No conversion engine wired: return a deterministic processId for tracking. */
static nop_status_t handle_start_event_download(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    (void)handler_context;

    if (!nop_json_has(request->args, "channel") ||
        !nop_json_has(request->args, "startTime") ||
        !nop_json_has(request->args, "duration") ||
        !nop_json_has(request->args, "resolution"))
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "processId", "");
    return NOP_OK;
}

/* Retrieves the download URL for an already-reproduced event (static produce
 * mode). No store wired: return an empty URL and zero filesize. */
static nop_status_t handle_start_event_download_with_url(const nop_request_t *request,
                                                         nop_response_t *response,
                                                         void *handler_context)
{
    (void)handler_context;

    if (!nop_json_has(request->args, "channel") ||
        !nop_json_has(request->args, "startTime"))
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "downloadUrl", "");
    nop_json_add_int(response->content, "filesize", 0);
    return NOP_OK;
}

void cap_event_register(nop_router_t *router)
{
    nop_router_register(router, "X_NightOwl_queryEventCalendar", CAP_NOTIFY,
                        handle_query_event_calendar);
    nop_router_register(router, "X_NightOwl_queryEventList", CAP_NOTIFY,
                        handle_query_event_list);
    nop_router_register(router, "X_NightOwl_queryEventListWithSpecificOrder", CAP_NOTIFY,
                        handle_query_event_list_with_specific_order);
    nop_router_register(router, "X_NightOwl_getEventDownloadCapability", CAP_NOTIFY,
                        handle_get_event_download_capability);
    nop_router_register(router, "X_NightOwl_getEventDownloadProgress", CAP_NOTIFY,
                        handle_get_event_download_progress);
    nop_router_register(router, "X_NightOwl_startEventDownload", CAP_NOTIFY,
                        handle_start_event_download);
    nop_router_register(router, "X_NightOwl_startEventDownloadwithURL", CAP_NOTIFY,
                        handle_start_event_download_with_url);
}
