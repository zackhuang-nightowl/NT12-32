/**
 * @file cap_ptz_patrol.c
 * @brief CAP_PTZ handlers for PTZ patrols, presets, and home position:
 *        setPtzPatrol (无 token=新建 / 有 token=全量更新) / operatePtzPatrol /
 *        removePtzPatrol / getPtzPatrols / getPtzPresets / setPtzPreset /
 *        removePtzPreset / gotoPtzPreset / setPtzHome / gotoPtzHome.
 *        gotoPtzPreset / setPtzHome / gotoPtzHome. All gated by CAP_PTZ.
 *
 *        Preset/patrol/home metadata is kept in a small in-memory store (so the
 *        query commands round-trip), and the physical operations are forwarded
 *        to HAL_PTZ: setPtzPreset/removePtzPreset/gotoPtzPreset →
 *        set_preset/remove_preset/goto_preset (by numeric preset index),
 *        setPtzHome/gotoPtzHome → set_home/goto_home. Patrol execution
 *        (operatePtzPatrol start/stop) is driven by the nop_ptz_patrol engine
 *        attached via nop_app_set_ptz_patrol(), cycling the stored spots.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_ptz.h"
#include "nop_sdk/nop_ptz_patrol.h"
#include "onvif/mapping/nop_onvif_map.h"

#include <string.h>

/* ---- in-memory store ------------------------------------------------------ */

#define CAP_PTZ_PATROL_MAX_PRESETS 32
#define CAP_PTZ_PATROL_MAX_PATROLS 16
#define CAP_PTZ_PATROL_MAX_SPOTS   16
#define CAP_PTZ_PATROL_NAME_LEN    64
#define CAP_PTZ_PATROL_TOKEN_LEN   32

typedef struct {
    int  used;
    int  index;                              /* HAL preset index (= slot) */
    char token[CAP_PTZ_PATROL_TOKEN_LEN];
    char name[CAP_PTZ_PATROL_NAME_LEN];
} ptz_preset_t;

typedef struct {
    int            used;
    char           token[CAP_PTZ_PATROL_TOKEN_LEN];
    char           name[CAP_PTZ_PATROL_NAME_LEN];
    nop_ptz_spot_t spots[CAP_PTZ_PATROL_MAX_SPOTS];
    int            spot_count;
} ptz_patrol_t;

static ptz_preset_t s_presets[CAP_PTZ_PATROL_MAX_PRESETS];
static ptz_patrol_t s_patrols[CAP_PTZ_PATROL_MAX_PATROLS];
static int          s_preset_seq;
static int          s_patrol_seq;
/* Token of the patrol currently driven on the engine ("" = none). The engine
 * runs one patrol at a time, so this attributes running/stop to the right one. */
static char         s_active_patrol[CAP_PTZ_PATROL_TOKEN_LEN];

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap)
        n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Append the decimal of @p n after @p prefix into @p out. */
static void make_token(char *out, size_t cap, const char *prefix, int n)
{
    char  digits[16];
    int   d = 0, k;
    size_t p = 0;
    while (prefix[p] && p + 1 < cap) { out[p] = prefix[p]; p++; }
    if (n <= 0) {
        digits[d++] = '0';
    } else {
        while (n > 0 && d < (int)sizeof digits) { digits[d++] = (char)('0' + (n % 10)); n /= 10; }
    }
    for (k = 0; k < d && p + 1 < cap; k++)
        out[p++] = digits[d - 1 - k];
    out[p] = '\0';
}

static const hal_ptz_if *ptz_hal(void)
{
    return (const hal_ptz_if *)hal_registry_get(HAL_PTZ);
}

static ptz_preset_t *preset_find(const char *token)
{
    int i;
    if (!token || token[0] == '\0')
        return NULL;
    for (i = 0; i < CAP_PTZ_PATROL_MAX_PRESETS; ++i)
        if (s_presets[i].used && !strcmp(s_presets[i].token, token))
            return &s_presets[i];
    return NULL;
}

static ptz_preset_t *preset_alloc(void)
{
    int i;
    for (i = 0; i < CAP_PTZ_PATROL_MAX_PRESETS; ++i)
        if (!s_presets[i].used) {
            s_presets[i].index = i;          /* slot position = HAL preset index */
            return &s_presets[i];
        }
    return NULL;
}

static ptz_patrol_t *patrol_find(const char *token)
{
    int i;
    if (!token || token[0] == '\0')
        return NULL;
    for (i = 0; i < CAP_PTZ_PATROL_MAX_PATROLS; ++i)
        if (s_patrols[i].used && !strcmp(s_patrols[i].token, token))
            return &s_patrols[i];
    return NULL;
}

static ptz_patrol_t *patrol_alloc(void)
{
    int i;
    for (i = 0; i < CAP_PTZ_PATROL_MAX_PATROLS; ++i)
        if (!s_patrols[i].used)
            return &s_patrols[i];
    return NULL;
}

/* Parse a JSON "spots" array into @p out; @return the spot count. Each spot:
 * {"preset": <index>, "dwell"|"dwellTime": <sec>, "speed": <0..100>}. */
static int parse_spots(const nop_json_t *spots_json, nop_ptz_spot_t *out, int max)
{
    int size = nop_json_arr_size(spots_json), i, count = 0;
    for (i = 0; i < size && count < max; ++i) {
        const nop_json_t *spot = nop_json_arr_at(spots_json, i);
        if (!spot)
            continue;
        out[count].preset        = (int)nop_json_num(spot, "preset", 0);
        out[count].dwell_seconds = (int)nop_json_num(spot, "dwell",
                                        nop_json_num(spot, "dwellTime", 5));
        out[count].speed         = (int)nop_json_num(spot, "speed", 50);
        count++;
    }
    return count;
}

/* ---- preset handlers ------------------------------------------------------ */

static nop_status_t handle_get_ptz_presets(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_ptz_preset(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    const hal_ptz_if *ptz = ptz_hal();
    const char       *token;
    const char       *name;
    ptz_preset_t     *preset;
    int               channel;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    /* ★ ONVIF 相机:先委派给映射层(SetPreset),再谈参数。缺 token=**新建**(设备分配)、有 token=更新
     * (见 setPreset 语义);token/name 由 ONVIF 层按需处理。若像原来那样先校验 token/name 再委派,
     * 新建预置位(无 token)会被误拒 400 → "preset 无法创建"。与 handle_get_ptz_presets(先委派)一致。 */
    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_remove_ptz_preset(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const hal_ptz_if *ptz = ptz_hal();
    const char       *token;
    ptz_preset_t     *preset;
    int               channel;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    token = nop_json_str(request->args, "token", NULL);
    if (!token || token[0] == '\0')
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_goto_ptz_preset(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    const hal_ptz_if *ptz = ptz_hal();
    const char       *token;
    ptz_preset_t     *preset;
    int               channel;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    token = nop_json_str(request->args, "token", NULL);
    if (!token || token[0] == '\0')
        return NOP_ERR_PARAM;
    if (!nop_json_has(request->args, "speed"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

/* ---- home handlers -------------------------------------------------------- */

static nop_status_t handle_set_ptz_home(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const hal_ptz_if *ptz = ptz_hal();
    int               channel;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_goto_ptz_home(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    const hal_ptz_if *ptz = ptz_hal();
    int               channel;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (!nop_json_has(request->args, "speed"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    if (nop_onvif_map_is_onvif(handler_context, channel))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

/* ---- patrol handlers ------------------------------------------------------ */

static void emit_patrol(nop_json_t *array, const ptz_patrol_t *patrol, int running)
{
    nop_json_t *entry = nop_json_obj();
    nop_json_t *spots = nop_json_arr();
    int         i;
    nop_json_add_str(entry, "token", patrol->token);
    nop_json_add_str(entry, "name", patrol->name);
    nop_json_add_str(entry, "status", running ? "Running" : "Idle");
    nop_json_add_bool(entry, "autoStart", false);
    for (i = 0; i < patrol->spot_count; ++i) {
        nop_json_t *spot = nop_json_obj();
        nop_json_add_int(spot, "preset", patrol->spots[i].preset);
        nop_json_add_int(spot, "dwell",  patrol->spots[i].dwell_seconds);
        nop_json_add_int(spot, "speed",  patrol->spots[i].speed);
        nop_json_arr_push(spots, spot);
    }
    nop_json_add(entry, "spots", spots);
    nop_json_arr_push(array, entry);
}

/* getPtzCapabilities: per-channel PTZ numeric limits. ONVIF channels answer from
 * the PTZ Node via the mapping layer; native/in-memory has no limits store, so
 * it returns an empty ptz[] (feature flags live in getDeviceCapabilities). */
static nop_status_t handle_get_ptz_capabilities(const nop_request_t *request,
                                                nop_response_t *response,
                                                void *handler_context)
{
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_get_ptz_patrols(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_set_ptz_patrol(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    const char   *token;
    const char   *name;
    nop_json_t   *spots;
    ptz_patrol_t *patrol;
    int           has_token;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    /* ONVIF：无 token → CreatePresetTour + ModifyPresetTour；有 token → Modify。 */
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_operate_ptz_patrol(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    const nop_business_context_t *context = (const nop_business_context_t *)handler_context;
    nop_ptz_patrol_t             *engine = context ? (nop_ptz_patrol_t *)context->ptz_patrol : NULL;
    const char                   *token;
    const char                   *op;
    ptz_patrol_t                 *patrol;
    int                           channel;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

static nop_status_t handle_remove_ptz_patrol(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const nop_business_context_t *context = (const nop_business_context_t *)handler_context;
    nop_ptz_patrol_t             *engine = context ? (nop_ptz_patrol_t *)context->ptz_patrol : NULL;
    const char                   *token;
    ptz_patrol_t                 *patrol;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    return NOP_ERR_NOTIMPL;
}

/* ---- registration --------------------------------------------------------- */

void cap_ptz_patrol_register(nop_router_t *router)
{
    nop_router_register(router, "setPtzPatrol",     CAP_PTZ, handle_set_ptz_patrol);
    nop_router_register(router, "operatePtzPatrol", CAP_PTZ, handle_operate_ptz_patrol);
    nop_router_register(router, "removePtzPatrol",  CAP_PTZ, handle_remove_ptz_patrol);
    nop_router_register(router, "getPtzCapabilities", CAP_PTZ, handle_get_ptz_capabilities);
    nop_router_register(router, "getPtzPatrols",    CAP_PTZ, handle_get_ptz_patrols);
    nop_router_register(router, "getPtzPresets",    CAP_PTZ, handle_get_ptz_presets);
    nop_router_register(router, "setPtzPreset",     CAP_PTZ, handle_set_ptz_preset);
    nop_router_register(router, "removePtzPreset",  CAP_PTZ, handle_remove_ptz_preset);
    nop_router_register(router, "gotoPtzPreset",    CAP_PTZ, handle_goto_ptz_preset);
    nop_router_register(router, "setPtzHome",       CAP_PTZ, handle_set_ptz_home);
    nop_router_register(router, "gotoPtzHome",      CAP_PTZ, handle_goto_ptz_home);
}
