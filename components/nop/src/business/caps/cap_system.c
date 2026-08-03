/**
 * @file cap_system.c
 * @brief CAP_SYSTEM handlers: reboot / resetDevice (HAL_SYSTEM) plus device
 *        settings held in the SDK — name, date/time, language, timezone,
 *        display mode, logs. Settings live in module state for now (firmware can
 *        override via a system-HAL extension later); CAP_SYSTEM is always on, so
 *        these are always routable.
 *
 * The clock is a soft clock: get_datetime reports the host clock until
 * set_datetime pins an epoch, after which it advances using the monotonic
 * timer (no privileged settimeofday needed).
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_system.h"
#include "nop_sdk/osal/osal.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static char g_device_name[48]    = "NOP Device";
static char g_language[32]       = "english";
static char g_timezone[48]       = "GMT +0:00";
static char g_timezone_dst[80]   = "";

static struct {
    int display_mode;
    int display_page;
    int auto_change_camera;
    int auto_select_event_camera;
    int first_view;
} g_display = { 4, 1, -1, 0, 1 };

/* Soft clock: 0 epoch => follow the host clock. */
static long long          g_clock_epoch     = 0;
static unsigned long long g_clock_mono_base = 0;

/* Gregorian civil date -> Unix epoch seconds (Hinnant's days_from_civil;
 * portable, avoids the non-standard timegm()). */
static long long civil_to_epoch(int year, int month, int day,
                                int hour, int minute, int second)
{
    long long era, days;
    unsigned  year_of_era, day_of_year, day_of_era;
    year -= (month <= 2);
    era = (year >= 0 ? year : year - 399) / 400;
    year_of_era = (unsigned)(year - era * 400);
    day_of_year = (unsigned)((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    day_of_era  = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    days = era * 146097 + (long long)day_of_era - 719468;
    return days * 86400 + (long long)hour * 3600 + (long long)minute * 60 + second;
}

static long long current_epoch(void)
{
    if (g_clock_epoch)
        return g_clock_epoch +
               (long long)((osal_time_ms() - g_clock_mono_base) / 1000);
    return (long long)time(NULL);
}

/* ---- power (HAL-backed) --------------------------------------------------- */
static nop_status_t handle_reboot(const nop_request_t *request,
                                  nop_response_t *response,
                                  void *handler_context)
{
    const hal_system_if *system = (const hal_system_if *)hal_registry_get(HAL_SYSTEM);
    (void)request; (void)response; (void)handler_context;
    if (!system || !system->reboot)
        return NOP_ERR_NOTIMPL;
    return system->reboot(system->ctx);
}

static nop_status_t handle_reset_device(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const hal_system_if *system = (const hal_system_if *)hal_registry_get(HAL_SYSTEM);
    (void)request; (void)response; (void)handler_context;
    if (!system || !system->factory_reset)
        return NOP_ERR_NOTIMPL;
    return system->factory_reset(system->ctx);
}

/* ---- name ----------------------------------------------------------------- */
static nop_status_t handle_get_name(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "name", g_device_name);
    return NOP_OK;
}

static nop_status_t handle_set_name(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    const char *name = nop_json_str(request->args, "name", NULL);
    (void)response; (void)handler_context;
    if (!name)
        return NOP_ERR_PARAM;
    strncpy(g_device_name, name, sizeof(g_device_name) - 1);
    g_device_name[sizeof(g_device_name) - 1] = '\0';
    return NOP_OK;
}

/* ---- date / time ---------------------------------------------------------- */
static nop_status_t handle_get_datetime(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    time_t     now = (time_t)current_epoch();
    struct tm *utc = gmtime(&now);
    char       date[36], clock_str[36];
    (void)request; (void)handler_context;
    if (!utc)
        return NOP_ERR_INTERNAL;
    snprintf(date, sizeof(date), "%04d%02d%02d",
             utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday);
    snprintf(clock_str, sizeof(clock_str), "%02d%02d%02d",
             utc->tm_hour, utc->tm_min, utc->tm_sec);
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "date", date);
    nop_json_add_str(response->content, "time", clock_str);
    return NOP_OK;
}

static nop_status_t handle_set_datetime(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const char *date = nop_json_str(request->args, "date", NULL);
    const char *clock_str = nop_json_str(request->args, "time", NULL);
    int year, month, day, hour, minute, second;
    (void)response; (void)handler_context;

    if (!date || !clock_str || strlen(date) != 8 || strlen(clock_str) != 6)
        return NOP_ERR_PARAM;
    if (sscanf(date, "%4d%2d%2d", &year, &month, &day) != 3 ||
        sscanf(clock_str, "%2d%2d%2d", &hour, &minute, &second) != 3)
        return NOP_ERR_PARAM;
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59)
        return NOP_ERR_PARAM;

    g_clock_epoch     = civil_to_epoch(year, month, day, hour, minute, second);
    g_clock_mono_base = osal_time_ms();
    return NOP_OK;
}

/* ---- language ------------------------------------------------------------- */
static nop_status_t handle_get_language(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "language", g_language);
    return NOP_OK;
}

static nop_status_t handle_set_language(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const char *language = nop_json_str(request->args, "language", NULL);
    (void)response; (void)handler_context;
    if (!language)
        return NOP_ERR_PARAM;
    strncpy(g_language, language, sizeof(g_language) - 1);
    g_language[sizeof(g_language) - 1] = '\0';
    return NOP_OK;
}

/* ---- timezone ------------------------------------------------------------- */
static nop_status_t handle_get_timezone(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "timezone", g_timezone);
    nop_json_add_str(response->content, "tz_dst", g_timezone_dst);
    return NOP_OK;
}

static nop_status_t handle_set_timezone(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const char *timezone_name = nop_json_str(request->args, "timezone", NULL);
    const char *tz_dst = nop_json_str(request->args, "tz_dst", NULL);
    (void)response; (void)handler_context;
    if (!timezone_name)
        return NOP_ERR_PARAM;
    strncpy(g_timezone, timezone_name, sizeof(g_timezone) - 1);
    g_timezone[sizeof(g_timezone) - 1] = '\0';
    if (tz_dst) {
        strncpy(g_timezone_dst, tz_dst, sizeof(g_timezone_dst) - 1);
        g_timezone_dst[sizeof(g_timezone_dst) - 1] = '\0';
    }
    return NOP_OK;
}

/* ---- display mode --------------------------------------------------------- */
static nop_status_t handle_get_display_mode(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    static const int modes[] = { 1, 4, 8, 16 };
    nop_json_t *all_modes;
    size_t      i;
    (void)request; (void)handler_context;

    response->content = nop_json_obj();
    nop_json_add_int(response->content, "displayMode", g_display.display_mode);
    nop_json_add_int(response->content, "displayPage", g_display.display_page);
    all_modes = nop_json_arr();
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
        nop_json_arr_push_int(all_modes, modes[i]);
    nop_json_add(response->content, "allDisplayModes", all_modes);
    nop_json_add_int(response->content, "autoChangeCamera", g_display.auto_change_camera);
    nop_json_add_int(response->content, "autoSelectEventCamera", g_display.auto_select_event_camera);
    nop_json_add_int(response->content, "firstView", g_display.first_view);
    return NOP_OK;
}

static nop_status_t handle_set_display_mode(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "displayMode"))
        return NOP_ERR_PARAM;
    g_display.display_mode = (int)nop_json_num(request->args, "displayMode", g_display.display_mode);
    g_display.display_page = (int)nop_json_num(request->args, "displayPage", g_display.display_page);
    g_display.auto_change_camera = (int)nop_json_num(request->args, "autoChangeCamera", g_display.auto_change_camera);
    g_display.auto_select_event_camera = (int)nop_json_num(request->args, "autoSelectEventCamera", g_display.auto_select_event_camera);
    g_display.first_view = (int)nop_json_num(request->args, "firstView", g_display.first_view);
    return NOP_OK;
}

/* ---- logs ----------------------------------------------------------------- */
static nop_status_t handle_get_logs(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    (void)request; (void)handler_context;
    /* No log source wired yet — report an empty list rather than 501 so clients
     * get a well-formed response. Firmware can supply logs via a HAL later. */
    response->content = nop_json_obj();
    nop_json_add(response->content, "list", nop_json_arr());
    return NOP_OK;
}

void cap_system_register(nop_router_t *router)
{
    nop_router_register(router, "reboot", CAP_SYSTEM, handle_reboot);
    nop_router_register(router, "resetDevice", CAP_SYSTEM, handle_reset_device);
    nop_router_register(router, "getName", CAP_SYSTEM, handle_get_name);
    nop_router_register(router, "setName", CAP_SYSTEM, handle_set_name);
    nop_router_register(router, "get_datetime", CAP_SYSTEM, handle_get_datetime);
    nop_router_register(router, "set_datetime", CAP_SYSTEM, handle_set_datetime);
    nop_router_register(router, "get_language", CAP_SYSTEM, handle_get_language);
    nop_router_register(router, "set_language", CAP_SYSTEM, handle_set_language);
    nop_router_register(router, "X_NightOwl_getTimezone", CAP_SYSTEM, handle_get_timezone);
    nop_router_register(router, "X_NightOwl_setTimezone", CAP_SYSTEM, handle_set_timezone);
    nop_router_register(router, "getDeviceDisplayMode", CAP_SYSTEM, handle_get_display_mode);
    nop_router_register(router, "setDeviceDisplayMode", CAP_SYSTEM, handle_set_display_mode);
    nop_router_register(router, "getLogs", CAP_SYSTEM, handle_get_logs);
}
