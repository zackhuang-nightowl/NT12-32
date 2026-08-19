/**
 * @file cap_ptz_patrol.c
 * @brief CAP_PTZ handlers for PTZ patrols, presets, and home position:
 *        createPtzPatrol / modifyPtzPatrol / operatePtzPatrol / removePtzPatrol /
 *        getPtzPatrols / getPtzPresets / setPtzPreset / removePtzPreset /
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
    nop_json_t *presets;
    int         i;

    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);

    response->content = nop_json_obj();
    presets = nop_json_arr();
    for (i = 0; i < CAP_PTZ_PATROL_MAX_PRESETS; ++i) {
        nop_json_t *entry;
        if (!s_presets[i].used)
            continue;
        entry = nop_json_obj();
        nop_json_add_str(entry, "token", s_presets[i].token);
        nop_json_add_str(entry, "name", s_presets[i].name);
        nop_json_add_int(entry, "index", s_presets[i].index);
        nop_json_arr_push(presets, entry);
    }
    nop_json_add(response->content, "presets", presets);
    return NOP_OK;
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

    /* --- 以下为 HAL 本地路径:需要 token(""=自动分配) 与 name。 --- */
    if (!nop_json_has(request->args, "token"))    /* Must; "" = auto-assign */
        return NOP_ERR_PARAM;
    name = nop_json_str(request->args, "name", NULL);
    if (!name)
        return NOP_ERR_PARAM;

    token  = nop_json_str(request->args, "token", "");
    preset = preset_find(token);
    if (!preset) {
        preset = preset_alloc();
        if (!preset)
            return NOP_ERR_PARAM;
        preset->used = 1;
        if (token[0] != '\0')
            copy_str(preset->token, sizeof preset->token, token);
        else
            make_token(preset->token, sizeof preset->token, "preset_", s_preset_seq++);
    }
    copy_str(preset->name, sizeof preset->name, name);

    /* Save the current gimbal position as this preset (best-effort). */
    if (ptz && ptz->set_preset)
        ptz->set_preset(ptz->ctx, channel, preset->index);

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "token", preset->token);
    return NOP_OK;
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

    preset = preset_find(token);
    if (preset) {
        if (ptz && ptz->remove_preset)
            ptz->remove_preset(ptz->ctx, channel, preset->index);
        memset(preset, 0, sizeof *preset);
    }

    response->content = nop_json_obj();
    return NOP_OK;
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

    preset = preset_find(token);
    if (!preset)
        return NOP_ERR_PARAM;             /* unknown preset token */
    if (!ptz || !ptz->goto_preset)
        return NOP_ERR_NOTIMPL;           /* pure action needs the HAL */

    response->content = nop_json_obj();
    return ptz->goto_preset(ptz->ctx, channel, preset->index);
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

    if (ptz && ptz->set_home)
        ptz->set_home(ptz->ctx, channel);

    response->content = nop_json_obj();
    return NOP_OK;
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

    if (!ptz || !ptz->goto_home)
        return NOP_ERR_NOTIMPL;
    response->content = nop_json_obj();
    return ptz->goto_home(ptz->ctx, channel);
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

    response->content = nop_json_obj();
    nop_json_add(response->content, "ptz", nop_json_arr());
    return NOP_OK;
}

static nop_status_t handle_get_ptz_patrols(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    const nop_business_context_t *context = (const nop_business_context_t *)handler_context;
    nop_ptz_patrol_t             *engine = context ? (nop_ptz_patrol_t *)context->ptz_patrol : NULL;
    int                           engine_running = engine ? nop_ptz_patrol_is_running(engine) : 0;
    nop_json_t                   *patrols;
    int                           i;

    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);

    response->content = nop_json_obj();
    patrols = nop_json_arr();
    for (i = 0; i < CAP_PTZ_PATROL_MAX_PATROLS; ++i) {
        if (!s_patrols[i].used)
            continue;
        /* Only the patrol whose token is the active one is reported Running. */
        emit_patrol(patrols, &s_patrols[i],
                    engine_running && !strcmp(s_patrols[i].token, s_active_patrol));
    }
    nop_json_add(response->content, "patrols", patrols);
    return NOP_OK;
}

static nop_status_t handle_create_ptz_patrol(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const char   *name;
    nop_json_t   *spots;
    ptz_patrol_t *patrol;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    name = nop_json_str(request->args, "name", NULL);
    if (!name)
        return NOP_ERR_PARAM;
    spots = nop_json_get(request->args, "spots");
    if (!spots || !nop_json_is_arr(spots))
        return NOP_ERR_PARAM;

    patrol = patrol_alloc();
    if (!patrol)
        return NOP_ERR_PARAM;
    patrol->used = 1;
    make_token(patrol->token, sizeof patrol->token, "patrol_", s_patrol_seq++);
    copy_str(patrol->name, sizeof patrol->name, name);
    patrol->spot_count = parse_spots(spots, patrol->spots, CAP_PTZ_PATROL_MAX_SPOTS);

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "token", patrol->token);
    return NOP_OK;
}

static nop_status_t handle_modify_ptz_patrol(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    const char   *token;
    nop_json_t   *spots;
    ptz_patrol_t *patrol;
    (void)handler_context;

    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    if (nop_onvif_map_is_onvif(handler_context, (int)nop_json_num(request->args, "channel", 0)))
        return nop_onvif_map_dispatch(handler_context, request, response);
    token = nop_json_str(request->args, "token", NULL);
    if (!token || token[0] == '\0')
        return NOP_ERR_PARAM;
    spots = nop_json_get(request->args, "spots");
    if (!spots || !nop_json_is_arr(spots))
        return NOP_ERR_PARAM;

    patrol = patrol_find(token);
    if (!patrol)
        return NOP_ERR_PARAM;
    patrol->spot_count = parse_spots(spots, patrol->spots, CAP_PTZ_PATROL_MAX_SPOTS);

    response->content = nop_json_obj();
    return NOP_OK;
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
    token = nop_json_str(request->args, "token", NULL);
    if (!token || token[0] == '\0')
        return NOP_ERR_PARAM;
    op = nop_json_str(request->args, "op", NULL);
    if (!op || op[0] == '\0')
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);

    patrol = patrol_find(token);
    if (!patrol)
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();

    /* Drive the execution engine when attached; otherwise acknowledge. */
    if (engine) {
        if (!strcmp(op, "start")) {
            /* start() replaces any running patrol, so this token becomes active
             * only if it actually starts (propagate 501 when there is no HAL). */
            nop_status_t status = nop_ptz_patrol_start(engine, channel,
                                                       patrol->spots, patrol->spot_count);
            if (status != NOP_OK)
                return status;
            copy_str(s_active_patrol, sizeof s_active_patrol, patrol->token);
        } else if (!strcmp(op, "stop")) {
            /* Stop only if THIS patrol is the one running. */
            if (!strcmp(s_active_patrol, patrol->token)) {
                nop_ptz_patrol_stop(engine);
                s_active_patrol[0] = '\0';
            }
        }
    }
    return NOP_OK;
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
    token = nop_json_str(request->args, "token", NULL);
    if (!token || token[0] == '\0')
        return NOP_ERR_PARAM;

    patrol = patrol_find(token);
    if (patrol) {
        /* Stop the engine only if the removed patrol is the one running. */
        if (engine && !strcmp(s_active_patrol, patrol->token)) {
            nop_ptz_patrol_stop(engine);
            s_active_patrol[0] = '\0';
        }
        memset(patrol, 0, sizeof *patrol);
    }

    response->content = nop_json_obj();
    return NOP_OK;
}

/* ---- registration --------------------------------------------------------- */

void cap_ptz_patrol_register(nop_router_t *router)
{
    nop_router_register(router, "createPtzPatrol",  CAP_PTZ, handle_create_ptz_patrol);
    nop_router_register(router, "modifyPtzPatrol",  CAP_PTZ, handle_modify_ptz_patrol);
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
