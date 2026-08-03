/**
 * @file cap_gui_account.c
 * @brief GUI (local LVGL UI) account/login handlers. Gated by CAP_MISC.
 *
 *        Backed by module-static state so the login session round-trips:
 *        GUI_login records the current userType/username/userLevel which
 *        GUI_getLoginStatus reports back, and GUI_logout clears it. Action
 *        commands (login/logout/createUser/deleteUser/setUser/setPassword/
 *        forgetPassword) validate their required args and return a spec-shaped
 *        success object. List/matrix queries (getUsers/getUserGroupPermissions)
 *        return an object holding a (possibly one-default-entry) array/dict.
 *        Modeled on nop_api/Login/<command>.txt "//Format".
 */
#include "business/business.h"
#include "base/nop_json.h"

#include <string.h>

/* ---- module-static login session state (memory only) --------------------- */
static char g_login_user_type[16]  = "local";
static char g_login_username[64]   = "";
static char g_login_user_level[16] = "";

static void copy_str(char *slot, size_t size, const char *value)
{
    strncpy(slot, value, size - 1);
    slot[size - 1] = '\0';
}

/* ===========================================================================
 * Login / logout / login status
 * =========================================================================== */
static nop_status_t handle_login(const nop_request_t *request,
                                 nop_response_t *response,
                                 void *handler_context)
{
    const char *user_type = nop_json_str(request->args, "userType", NULL);
    const char *username  = nop_json_str(request->args, "username", NULL);
    const char *password  = nop_json_str(request->args, "password", NULL);
    (void)handler_context;
    if (!user_type || !username || !password)
        return NOP_ERR_PARAM;
    copy_str(g_login_user_type, sizeof(g_login_user_type), user_type);
    copy_str(g_login_username, sizeof(g_login_username), username);
    copy_str(g_login_user_level, sizeof(g_login_user_level), "Admin");
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    nop_json_add_str(response->content, "userType", g_login_user_type);
    nop_json_add_str(response->content, "userLevel", g_login_user_level);
    return NOP_OK;
}

static nop_status_t handle_login_page(const nop_request_t *request,
                                      nop_response_t *response,
                                      void *handler_context)
{
    (void)handler_context;
    if (!nop_json_get(request->args, "Area") ||
        !nop_json_has(request->args, "Action"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

static nop_status_t handle_logout(const nop_request_t *request,
                                  nop_response_t *response,
                                  void *handler_context)
{
    (void)request; (void)response; (void)handler_context;
    g_login_username[0]   = '\0';
    g_login_user_level[0] = '\0';
    return NOP_OK;
}

static nop_status_t handle_get_login_status(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "userType", g_login_user_type);
    nop_json_add_str(response->content, "username", g_login_username);
    nop_json_add_str(response->content, "userLevel", g_login_user_level);
    return NOP_OK;
}

/* ===========================================================================
 * User management: create / delete / set
 * =========================================================================== */
static nop_status_t handle_create_user(const nop_request_t *request,
                                       nop_response_t *response,
                                       void *handler_context)
{
    const char *username  = nop_json_str(request->args, "username", NULL);
    const char *password  = nop_json_str(request->args, "password", NULL);
    const char *user_type = nop_json_str(request->args, "userType", NULL);
    const char *user_level = nop_json_str(request->args, "userLevel", NULL);
    (void)handler_context;
    if (!username || !password || !user_type || !user_level)
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

static nop_status_t handle_delete_user(const nop_request_t *request,
                                       nop_response_t *response,
                                       void *handler_context)
{
    const char *username = nop_json_str(request->args, "username", NULL);
    (void)handler_context;
    if (!username)
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

static nop_status_t handle_set_user(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    const char *username = nop_json_str(request->args, "username", NULL);
    (void)handler_context;
    if (!username)
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

static nop_status_t handle_get_users(const nop_request_t *request,
                                     nop_response_t *response,
                                     void *handler_context)
{
    nop_json_t *users, *entry;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    users = nop_json_arr();
    entry = nop_json_obj();
    nop_json_add_str(entry, "username", "Admin");
    nop_json_add_str(entry, "userType", "local");
    nop_json_add_str(entry, "userLevel", "Admin");
    nop_json_add_str(entry, "createTime", "");
    nop_json_add_str(entry, "lastLogin", "");
    nop_json_arr_push(users, entry);
    nop_json_add(response->content, "users", users);
    return NOP_OK;
}

/* ===========================================================================
 * Password: set (deprecated) / forget
 * =========================================================================== */
static nop_status_t handle_set_password(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const char *user_type = nop_json_str(request->args, "userType", NULL);
    const char *username  = nop_json_str(request->args, "username", NULL);
    const char *password  = nop_json_str(request->args, "password", NULL);
    (void)handler_context;
    if (!user_type || !username || !password)
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

static nop_status_t handle_forget_password(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    const char *user_type = nop_json_str(request->args, "userType", NULL);
    const char *username  = nop_json_str(request->args, "username", NULL);
    (void)handler_context;
    if (!user_type || !username)
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

/* ===========================================================================
 * User group permission matrix (read-only, fixed)
 * =========================================================================== */
static nop_status_t handle_get_user_group_permissions(const nop_request_t *request,
                                                       nop_response_t *response,
                                                       void *handler_context)
{
    nop_json_t *groups, *admin, *technician, *viewer;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    groups = nop_json_obj();

    admin = nop_json_obj();
    nop_json_add(admin, "LiveView", nop_json_obj());
    nop_json_add(admin, "Control", nop_json_obj());
    nop_json_add(admin, "Setting", nop_json_obj());
    nop_json_add(admin, "Account", nop_json_obj());
    nop_json_add(groups, "Admin", admin);

    technician = nop_json_obj();
    nop_json_add(technician, "LiveView", nop_json_obj());
    nop_json_add(technician, "Control", nop_json_obj());
    nop_json_add(groups, "Technician", technician);

    viewer = nop_json_obj();
    nop_json_add(viewer, "LiveView", nop_json_obj());
    nop_json_add(groups, "Viewer", viewer);

    nop_json_add(response->content, "groups", groups);
    return NOP_OK;
}

/* ===========================================================================
 * Registration: every command under CAP_MISC (11 total)
 * =========================================================================== */
void cap_gui_account_register(nop_router_t *router)
{
    nop_router_register(router, "GUI_login", CAP_MISC, handle_login);
    nop_router_register(router, "GUI_LoginPage", CAP_MISC, handle_login_page);
    nop_router_register(router, "GUI_logout", CAP_MISC, handle_logout);
    nop_router_register(router, "GUI_getLoginStatus", CAP_MISC, handle_get_login_status);
    nop_router_register(router, "GUI_createUser", CAP_MISC, handle_create_user);
    nop_router_register(router, "GUI_deleteUser", CAP_MISC, handle_delete_user);
    nop_router_register(router, "GUI_setUser", CAP_MISC, handle_set_user);
    nop_router_register(router, "GUI_getUsers", CAP_MISC, handle_get_users);
    nop_router_register(router, "GUI_setPassword", CAP_MISC, handle_set_password);
    nop_router_register(router, "GUI_forgetPassword", CAP_MISC, handle_forget_password);
    nop_router_register(router, "GUI_getUserGroupPermissions", CAP_MISC, handle_get_user_group_permissions);
}
