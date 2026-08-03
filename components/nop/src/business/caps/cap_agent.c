/**
 * @file cap_agent.c
 * @brief Device agent / diagnostics / logging handlers. Gated by CAP_MISC.
 *
 *        Covers Agent self-extend (enableLog/getLog/errorReport/restartAgent),
 *        diagnostic/tunnel actions (agent_diagnosis/agent_resetTunnel/
 *        enableDiagnostic), NOP_APP report/environment queries
 *        (getReportServer/getEnvironment) and the APP setup notification
 *        (notify_appSetupStatus).
 *
 *        get* commands return spec-shaped objects (getLog: {"list":[]} style
 *        empty payload; getReportServer: scalar fields plus spec arrays;
 *        getEnvironment: spec-shaped object). enableLog/enableDiagnostic store
 *        an in-memory bool/window and return success. Action and notify
 *        commands validate their required args and return spec-shaped success.
 *        Modeled on nop_api/<group>/<command>.txt "//Format".
 */
#include "business/business.h"
#include "base/nop_json.h"

#include <string.h>
#include <stdlib.h>

/* ---- module-static state -------------------------------------------------- */

/* enableLog / getLog: in-memory enable flag plus last applied configuration. */
static bool g_log_enabled = false;

/* enableDiagnostic: in-memory enable flag plus last requested window. */
static bool g_diagnostic_enabled = false;

/* ===========================================================================
 * Agent self-extend: logging
 * =========================================================================== */
static nop_status_t handle_enable_log(const nop_request_t *request,
                                      nop_response_t *response,
                                      void *handler_context)
{
    (void)response; (void)handler_context;
    /* All args optional per spec; presence simply (re)configures logging. */
    (void)request;
    g_log_enabled = true;
    return NOP_OK;
}

static nop_status_t handle_get_log(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    (void)request; (void)handler_context;
    /* No real log capture in the SDK stub; return an empty spec-shaped object. */
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "data", "");
    return NOP_OK;
}

static nop_status_t handle_error_report(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const char *api = nop_json_str(request->args, "api", NULL);
    (void)response; (void)handler_context;
    if (!api || !nop_json_has(request->args, "error"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

static nop_status_t handle_restart_agent(const nop_request_t *request,
                                         nop_response_t *response,
                                         void *handler_context)
{
    (void)request; (void)response; (void)handler_context;
    /* securityCode is optional per spec; accept and acknowledge. */
    return NOP_OK;
}

/* ===========================================================================
 * Diagnostics / tunnel actions
 * =========================================================================== */
static nop_status_t handle_agent_diagnosis(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    nop_json_t *ver, *sdk_options, *agent_options, *status, *cur_conn;
    (void)request; (void)handler_context;

    response->content = nop_json_obj();

    ver = nop_json_obj();
    nop_json_add_str(ver, "IOTCVer", "-");
    nop_json_add_str(ver, "AVVer", "-");
    nop_json_add_str(ver, "RDTVer", "-");
    nop_json_add_str(ver, "TunnelVer", "-");
    nop_json_add_str(ver, "AgentVer", "-");
    nop_json_add(response->content, "ver", ver);

    sdk_options = nop_json_obj();
    nop_json_add_int(sdk_options, "iotcConnMaxNumber", 10);
    nop_json_add_int(sdk_options, "iotcConnTimeout", 10);
    nop_json_add_int(sdk_options, "iotcSessionAliveTimeout", 60);
    nop_json_add_int(sdk_options, "iotcSessionMaxChannleNumber", 2);
    nop_json_add_int(sdk_options, "avMaxChannelNumber", 12);
    nop_json_add_int(sdk_options, "avConnTimeout", 30);
    nop_json_add_int(sdk_options, "rdtConnMaxNumber", 10);
    nop_json_add_int(sdk_options, "rdtConnTimeout", 10);
    nop_json_add_int(sdk_options, "tunnelBufSize", 5120000);
    nop_json_add(response->content, "sdkOptions", sdk_options);

    agent_options = nop_json_obj();
    nop_json_add_int(agent_options, "AgentMaxClientNumber", 10);
    nop_json_add_int(agent_options, "AgentMaxTunnelNumber", 10);
    nop_json_add(response->content, "agentOptions", agent_options);

    status = nop_json_obj();
    nop_json_add_bool(status, "online", true);
    nop_json_add_int(status, "last_errorNum", 0);
    nop_json_add_str(status, "last_errorInfo", "");
    cur_conn = nop_json_arr();
    nop_json_add(status, "curConn", cur_conn);
    nop_json_add(response->content, "status", status);

    return NOP_OK;
}

static nop_status_t handle_agent_reset_tunnel(const nop_request_t *request,
                                              nop_response_t *response,
                                              void *handler_context)
{
    (void)request; (void)response; (void)handler_context;
    /* sid is optional: absent clears the whole tunnel module. Acknowledge. */
    return NOP_OK;
}

static nop_status_t handle_enable_diagnostic(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    int max_events;
    (void)handler_context;
    if (!nop_json_has(request->args, "durationMinutes") ||
        !nop_json_has(request->args, "maxEvents"))
        return NOP_ERR_PARAM;
    max_events = (int)nop_json_num(request->args, "maxEvents", 50);
    g_diagnostic_enabled = true;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "enabled", g_diagnostic_enabled);
    nop_json_add_str(response->content, "windowStart", "");
    nop_json_add_str(response->content, "windowEnd", "");
    nop_json_add_int(response->content, "maxEvents", max_events);
    return NOP_OK;
}

/* ===========================================================================
 * NOP_APP report / environment queries
 * =========================================================================== */
static nop_status_t handle_get_report_server(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    nop_json_t *report_to, *cmd;
    const char *model        = nop_json_str(request->args, "model", NULL);
    const char *serial_number = nop_json_str(request->args, "serialNumber", NULL);
    (void)handler_context;
    if (!model || !serial_number)
        return NOP_ERR_PARAM;

    response->content = nop_json_obj();
    nop_json_add_str(response->content, "queryRedirect", "");
    nop_json_add_int(response->content, "reportIntervalMins", 1440);
    report_to = nop_json_obj();
    nop_json_add_str(report_to, "url", "");
    nop_json_add_str(report_to, "auth", "");
    nop_json_add(response->content, "reportTo", report_to);
    cmd = nop_json_arr();
    nop_json_add(response->content, "cmd", cmd);
    return NOP_OK;
}

static nop_status_t handle_get_environment(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "env", "prodution");
    return NOP_OK;
}

/* ===========================================================================
 * APP setup status notification
 * =========================================================================== */
static nop_status_t handle_notify_app_setup_status(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "status"))
        return NOP_ERR_PARAM;
    return NOP_OK;
}

/* ===========================================================================
 * Registration: every command under CAP_MISC (10 total)
 * =========================================================================== */
void cap_agent_register(nop_router_t *router)
{
    nop_router_register(router, "agent_diagnosis", CAP_MISC, handle_agent_diagnosis);
    nop_router_register(router, "agent_resetTunnel", CAP_MISC, handle_agent_reset_tunnel);
    nop_router_register(router, "enableLog", CAP_MISC, handle_enable_log);
    nop_router_register(router, "getLog", CAP_MISC, handle_get_log);
    nop_router_register(router, "errorReport", CAP_MISC, handle_error_report);
    nop_router_register(router, "restartAgent", CAP_MISC, handle_restart_agent);
    nop_router_register(router, "enableDiagnostic", CAP_MISC, handle_enable_diagnostic);
    nop_router_register(router, "getReportServer", CAP_MISC, handle_get_report_server);
    nop_router_register(router, "notify_appSetupStatus", CAP_MISC, handle_notify_app_setup_status);
    nop_router_register(router, "getEnvironment", CAP_MISC, handle_get_environment);
}
