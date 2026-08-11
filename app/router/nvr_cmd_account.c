/***************************************************************************************
 *  nvr_cmd_account.c — 账户/鉴权域 LOCAL handler(回落 components/nop cap_gui_account)。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"

#define NVR_NOP(name) \
char *cmd_##name(cJSON *a, const nvr_cmd_ctx_t *c) { return nvr_cmd_nop_dispatch(a, c, #name); }

NVR_NOP(GUI_login)
NVR_NOP(GUI_logout)
NVR_NOP(GUI_LoginPage)
NVR_NOP(GUI_getLoginStatus)
NVR_NOP(GUI_createUser)
NVR_NOP(GUI_deleteUser)
NVR_NOP(GUI_getUsers)
NVR_NOP(GUI_getUserGroupPermissions)
NVR_NOP(GUI_forgetPassword)

#undef NVR_NOP
