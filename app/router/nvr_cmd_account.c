/***************************************************************************************
 *  nvr_cmd_account.c — 账户/鉴权域 LOCAL handler。
 *
 *  本地多用户(Admin1 / Technician≤10 / Viewer≤10) + Cognito NOP(aws) 登录。
 *  GUI_login / createUser / deleteUser / setUser / getUsers / getUserGroupPermissions。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_cognito.h"
#include "nvr_graphql.h"
#include "nvr_resetcode.h"
#include "nvr_crypto.h"
#include "nvr_identity.h"
#include "nvr_ble.h"
#include "nvr_log.h"
#include "nop_sdk/nop_error_str.h"
#include "nvr_defaults.h"
#include "nvr_gui_notify.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- 会话（进程内；GUI_getLoginStatus / logout / 权限门控）---- */
static char g_user_type[16];
static char g_username[64];
static char g_user_level[16];
static int  g_logged_in;

static void session_clear(void)
{
    g_user_type[0] = g_username[0] = g_user_level[0] = 0;
    g_logged_in = 0;
    nvr_gui_set_ui_unlocked(0);
}

static void session_set(const char *ut, const char *user, const char *level)
{
    snprintf(g_user_type, sizeof(g_user_type), "%s", ut ? ut : "");
    snprintf(g_username, sizeof(g_username), "%s", user ? user : "");
    snprintf(g_user_level, sizeof(g_user_level), "%s", level ? level : "Admin");
    g_logged_in = 1;
    nvr_gui_set_ui_unlocked(1);
}

static int session_is_admin(void)
{
    return g_logged_in && strcmp(g_user_level, "Admin") == 0;
}

static char *login_result(const char *result, const char *user_type, const char *user_level)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "result", result ? result : NOP_RESULT_FAIL_OTHER);
    if (user_type && user_type[0]) cJSON_AddStringToObject(o, "userType", user_type);
    if (user_level && user_level[0] && result && strcmp(result, NOP_RESULT_OK) == 0)
        cJSON_AddStringToObject(o, "userLevel", user_level);
    return nvr_resp_content(o);
}

static char *acct_result(const char *result)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "result", result ? result : NOP_RESULT_FAIL_OTHER);
    return nvr_resp_content(o);
}

/* ---- 锁定 ---- */
static int lockout_active(const nvr_cmd_ctx_t *c)
{
    if (!c || !c->settings) return 0;
    int64_t until = (int64_t)nvr_settings_get_int(c->settings, "account.lockout_until", 0);
    return until > (int64_t)time(NULL);
}

static void lockout_on_fail(const nvr_cmd_ctx_t *c)
{
    if (!c || !c->settings) return;
    int n = nvr_settings_get_int(c->settings, "account.fail_count", 0) + 1;
    nvr_settings_set_int(c->settings, "account.fail_count", n);
    if (n >= 3) {
        nvr_settings_set_int(c->settings, "account.lockout_until", (int)(time(NULL) + 3600));
        nvr_settings_set_int(c->settings, "account.fail_count", 0);
        NVR_LOGW("account", "login locked 1h after 3 failures");
    }
}

static void lockout_clear(const nvr_cmd_ctx_t *c)
{
    if (!c || !c->settings) return;
    nvr_settings_set_int(c->settings, "account.fail_count", 0);
    nvr_settings_set_int(c->settings, "account.lockout_until", 0);
}

/* ---- 口令 / 校验 ---- */
static void pw_hash_bin(const char *password, uint8_t out[NVR_SHA256_LEN])
{
    memset(out, 0, NVR_SHA256_LEN);
    if (password) (void)nvr_sha256(password, strlen(password), out);
}

static void pw_hash_hex(const char *password, char *out, size_t cap)
{
    uint8_t dig[NVR_SHA256_LEN];
    out[0] = 0;
    pw_hash_bin(password, dig);
    nvr_hex(dig, NVR_SHA256_LEN, out, cap);
}

static int pw_ok_row(const nvr_user_row_t *u, const char *password)
{
    if (!u || !password || u->hash_len != NVR_SHA256_LEN) return 0;
    if (u->pw_algo[0] && strcmp(u->pw_algo, "sha256") != 0) return 0;
    uint8_t dig[NVR_SHA256_LEN];
    pw_hash_bin(password, dig);
    return memcmp(u->pw_hash, dig, NVR_SHA256_LEN) == 0;
}

static int username_ok(const char *u, char *err /*UsernameTooShort/Long or NULL*/)
{
    size_t n = u ? strlen(u) : 0;
    if (n < 1) { if (err) strcpy(err, "UsernameTooShort"); return 0; }
    if (n > 32) { if (err) strcpy(err, "UsernameTooLong"); return 0; }
    for (size_t i = 0; i < n; i++) {
        char c = u[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) { if (err) strcpy(err, "UsernameTooShort"); return 0; } /* 无效字符归短/非法 */
    }
    return 1;
}

static int password_ok(const char *p)
{
    size_t n = p ? strlen(p) : 0;
    if (n < 6 || n > 64) return 0;
    return strchr(p, ' ') == NULL;
}

static int str_eq_ci(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static int level_is(const char *lv, const char *want)
{
    return lv && want && strcmp(lv, want) == 0;
}

static void iso8601(int64_t epoch, char *out, size_t cap)
{
    out[0] = 0;
    if (epoch <= 0 || cap < 21) return;
    time_t t = (time_t)epoch;
    struct tm tm;
#if defined(_WIN32)
    if (gmtime_s(&tm, &t) != 0) return;
#else
    if (!gmtime_r(&t, &tm)) return;
#endif
    strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void fill_user_pw(nvr_user_row_t *u, const char *password)
{
    uint8_t dig[NVR_SHA256_LEN];
    pw_hash_bin(password, dig);
    snprintf(u->pw_algo, sizeof(u->pw_algo), "sha256");
    memcpy(u->pw_hash, dig, NVR_SHA256_LEN);
    u->hash_len = NVR_SHA256_LEN;
}

/* ---- Cognito 离线缓存 ---- */
static void cache_aws_offline(const nvr_cmd_ctx_t *c, const char *username, const char *password,
                              const nvr_cognito_user_t *u)
{
    if (!c || !c->settings || !username || !password) return;
    char hex[NVR_SHA256_LEN * 2 + 4];
    pw_hash_hex(password, hex, sizeof(hex));
    nvr_settings_set_str(c->settings, "account.aws_cache_user", username);
    nvr_settings_set_str(c->settings, "account.aws_cache_pw", hex);
    if (u) {
        if (u->email[0]) nvr_settings_set_str(c->settings, "account.aws_cache_email", u->email);
        if (u->phone[0]) nvr_settings_set_str(c->settings, "account.aws_cache_phone", u->phone);
    }
}

static int offline_aws_ok(const nvr_cmd_ctx_t *c, const char *username, const char *password)
{
    if (!c || !c->settings || !username || !password) return 0;
    char cu[64], ch[80], hex[80];
    nvr_settings_get_str(c->settings, "account.aws_cache_user", cu, sizeof(cu), "");
    nvr_settings_get_str(c->settings, "account.aws_cache_pw", ch, sizeof(ch), "");
    if (!cu[0] || !ch[0] || strcmp(cu, username) != 0) return 0;
    pw_hash_hex(password, hex, sizeof(hex));
    return strcmp(ch, hex) == 0;
}

/* allow_bind: userType=aws 且 owner 空时可写入 owner；any 为 0。 */
static char *login_aws(const nvr_cmd_ctx_t *c, const char *username, const char *password,
                       int allow_bind)
{
    nvr_owner_row_t ow;
    memset(&ow, 0, sizeof(ow));
    if (c->settings) (void)nvr_settings_owner_get(c->settings, &ow);

    nvr_cognito_user_t u;
    nvr_cognito_rc_t rc = nvr_cognito_login(username, password, &u);

    if (rc == NVR_COGNITO_ERR_NETWORK) {
        if (ow.owner_id[0] && offline_aws_ok(c, username, password)) {
            lockout_clear(c);
            session_set("aws", username, "Admin");
            return login_result(NOP_RESULT_OK, "aws", "Admin");
        }
        lockout_on_fail(c);
        return login_result(NOP_RESULT_FAIL_NETWORK, "aws", NULL);
    }
    if (rc == NVR_COGNITO_ERR_USER_NOT_FOUND) {
        lockout_on_fail(c);
        return login_result("UserNotFound", "aws", NULL);
    }
    if (rc != NVR_COGNITO_OK) {
        lockout_on_fail(c);
        return login_result(NOP_RESULT_FAIL_CREDENTIALS, "aws", NULL);
    }

    if (ow.owner_id[0]) {
        if (strcmp(ow.owner_id, u.owner_id) != 0)
            return login_result("OwnerIdMismatch", "aws", NULL);
    } else if (allow_bind) {
        /* 向导 aws：GraphQL addDevice → cloudToken(=stoken)，再落 nop_owner */
        if (!u.access_token[0]) {
            NVR_LOGW("account", "aws bind: no AccessToken");
            return login_result(NOP_RESULT_FAIL_ADD_TO_ACCOUNT, "aws", NULL);
        }
        char uid[64], name[64], authkey[32], avkey[32], blekey[20];
        nvr_identity_get_uid(uid, sizeof(uid));                               /* UID   ← /User/tutk_agent_udid */
        nvr_settings_get_str(c->settings, "system.device_name", name, sizeof(name), NVR_DEF_NAME);
        nvr_identity_get_tutk_creds(authkey, sizeof(authkey), avkey, sizeof(avkey)); /* IOTCKey+AVKey ← tutkdata.json */
        /* bluetoothId = BLEKey：文档为 16 位十六进制；优先已有，否则生成 */
        nvr_settings_get_str(c->settings, "ble.key", blekey, sizeof(blekey), "");
        if (!blekey[0]) {
            if (nvr_ble_gen_key16(blekey, (int)sizeof(blekey)) != 0) {
                NVR_LOGW("account", "aws bind: gen BLEKey failed");
                return login_result(NOP_RESULT_FAIL_ADD_TO_ACCOUNT, "aws", NULL);
            }
            nvr_settings_set_str(c->settings, "ble.key", blekey);
        }
        if (!uid[0]) {
            NVR_LOGW("account", "aws bind: empty tutk.uid");
            return login_result(NOP_RESULT_FAIL_ADD_TO_ACCOUNT, "aws", NULL);
        }
        char stoken[256];
        nvr_gql_add_device_in_t gin = {
            .access_token = u.access_token,
            .uid = uid,
            .name = name,
            .primary_key = authkey,
            .av_key = avkey,
            .bluetooth_id = blekey,
        };
        nvr_gql_rc_t grc = nvr_graphql_add_device(&gin, stoken, sizeof(stoken));
        if (grc != NVR_GQL_OK) {
            NVR_LOGW("account", "aws bind addDevice rc=%d", (int)grc);
            return login_result(NOP_RESULT_FAIL_ADD_TO_ACCOUNT, "aws", NULL);
        }
        memset(&ow, 0, sizeof(ow));
        snprintf(ow.owner_id, sizeof(ow.owner_id), "%s", u.owner_id);
        snprintf(ow.username, sizeof(ow.username), "%s",
                 u.username[0] ? u.username : username);
        snprintf(ow.stoken, sizeof(ow.stoken), "%s", stoken);
        nvr_settings_owner_set(c->settings, &ow);
        /* 绑定 NOP 后清除本地多用户（文档：删除 local Admin） */
        {
            nvr_user_row_t list[NVR_USER_MAX];
            int n = nvr_settings_user_list(c->settings, list, NVR_USER_MAX);
            for (int i = 0; i < n; i++)
                (void)nvr_settings_user_delete(c->settings, list[i].username);
        }
        NVR_LOGI("account", "aws bind ok owner=%s stoken_len=%d blekey=%s",
                 ow.owner_id, (int)strlen(ow.stoken), blekey);
    }

    cache_aws_offline(c, username, password, &u);
    lockout_clear(c);
    session_set("aws", username, "Admin");
    return login_result(NOP_RESULT_OK, "aws", "Admin");
}

static char *login_local(const nvr_cmd_ctx_t *c, const char *username, const char *password)
{
    if (!c || !c->settings) {
        lockout_on_fail(c);
        return login_result("UserNotFound", "local", NULL);
    }
    nvr_user_row_t u;
    if (nvr_settings_user_get(c->settings, username, &u) != 0) {
        lockout_on_fail(c);
        return login_result("UserNotFound", "local", NULL);
    }
    if (!pw_ok_row(&u, password)) {
        lockout_on_fail(c);
        return login_result(NOP_RESULT_FAIL_CREDENTIALS, "local", NULL);
    }
    lockout_clear(c);
    nvr_settings_user_touch_login(c->settings, u.username);
    session_set("local", u.username, u.user_level);
    return login_result(NOP_RESULT_OK, "local", u.user_level);
}

char *cmd_GUI_login(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *user_type = nvr_jstr(a, "userType", NULL);
    const char *username  = nvr_jstr(a, "username", NULL);
    const char *password  = nvr_jstr(a, "password", NULL);
    if (!user_type || !username || !password || !username[0] || !password[0])
        return nvr_resp_err("invalid_param");

    if (lockout_active(c))
        return login_result(NOP_RESULT_FAIL_LOCKED_10MIN, user_type, NULL);

    if (strcmp(user_type, "aws") == 0)
        return login_aws(c, username, password, 1);
    if (strcmp(user_type, "local") == 0)
        return login_local(c, username, password);
    if (strcmp(user_type, "any") == 0) {
        nvr_user_row_t u;
        if (c && c->settings && nvr_settings_user_get(c->settings, username, &u) == 0) {
            if (pw_ok_row(&u, password)) {
                lockout_clear(c);
                nvr_settings_user_touch_login(c->settings, u.username);
                session_set("local", u.username, u.user_level);
                return login_result(NOP_RESULT_OK, "local", u.user_level);
            }
            /* 用户名命中本地但密码错 → 仍可尝试 aws（同名极少见） */
        }
        return login_aws(c, username, password, 0);
    }
    return login_result(NOP_RESULT_FAIL_OTHER, user_type, NULL);
}

char *cmd_GUI_logout(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    session_clear();
    return nvr_resp_ok();
}

char *cmd_GUI_getLoginStatus(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "userType", g_logged_in ? g_user_type : "local");
    cJSON_AddStringToObject(o, "username", g_logged_in ? g_username : "");
    cJSON_AddStringToObject(o, "userLevel", g_logged_in ? g_user_level : "");
    return nvr_resp_content(o);
}

char *cmd_GUI_LoginPage(cJSON *a, const nvr_cmd_ctx_t *c)
{
    /* GUI→NVR：进出登录窗通知。Area=[TLX,TLY,BRX,BRY] 为分辨率百分比×10（见 nop_api_doc）。
     * Action=1 进入 / 0 离开。固件落状态并 ACK，供 GUI 协调叠层与后续扩展（如遮挡下音视频）。 */
    if (!a) return nvr_resp_err("invalid_param");
    cJSON *area = cJSON_GetObjectItem(a, "Area");
    if (!cJSON_IsArray(area) || cJSON_GetArraySize(area) != 4)
        return nvr_resp_err("invalid_param");
    if (!nvr_jhas(a, "Action"))
        return nvr_resp_err("invalid_param");

    int action = nvr_jint(a, "Action", 1);
    if (action != 0 && action != 1)
        return nvr_resp_err("invalid_param");

    int v[4];
    for (int i = 0; i < 4; i++) {
        cJSON *e = cJSON_GetArrayItem(area, i);
        if (!cJSON_IsNumber(e)) return nvr_resp_err("invalid_param");
        v[i] = e->valueint;
        if (v[i] < 0 || v[i] > 1000) return nvr_resp_err("invalid_param");
    }
    if (v[0] >= v[2] || v[1] >= v[3])
        return nvr_resp_err("invalid_param");

    if (c && c->settings) {
        nvr_settings_set_int(c->settings, "gui.login_page.active", action);
        nvr_settings_set_int(c->settings, "gui.login_page.x0", v[0]);
        nvr_settings_set_int(c->settings, "gui.login_page.y0", v[1]);
        nvr_settings_set_int(c->settings, "gui.login_page.x1", v[2]);
        nvr_settings_set_int(c->settings, "gui.login_page.y1", v[3]);
    }
    NVR_LOGI("account", "GUI_LoginPage Action=%d Area=[%d,%d,%d,%d]",
             action, v[0], v[1], v[2], v[3]);
    return acct_result(NOP_RESULT_OK);
}

/* ============================ 多用户管理 ============================ */
char *cmd_GUI_createUser(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *username  = nvr_jstr(a, "username", NULL);
    const char *password  = nvr_jstr(a, "password", NULL);
    const char *user_type = nvr_jstr(a, "userType", "local");
    const char *user_level = nvr_jstr(a, "userLevel", NULL);
    if (!c || !c->settings) return acct_result(NOP_RESULT_FAIL_OTHER);
    if (!username || !password || !user_level)
        return nvr_resp_err("invalid_param");
    if (user_type && strcmp(user_type, "local") != 0)
        return acct_result("NotAuthorized");

    char uerr[32] = "";
    if (!username_ok(username, uerr)) return acct_result(uerr[0] ? uerr : "UsernameTooShort");
    if (!password_ok(password)) return acct_result("PasswordTooWeak");

    int empty = (nvr_settings_user_count(c->settings, NULL) == 0);
    int want_admin = level_is(user_level, "Admin");
    int want_tech  = level_is(user_level, "Technician");
    int want_view  = level_is(user_level, "Viewer");
    if (!want_admin && !want_tech && !want_view)
        return acct_result("NotAuthorized");
    /* 非 Admin 等级禁止用户名 Admin（文档：不能是 Admin 作为名字） */
    if (!want_admin && str_eq_ci(username, "Admin"))
        return acct_result("NotAuthorized");

    /* 向导：无用户时可建 Admin；否则仅 Admin 会话可建 Technician/Viewer */
    if (empty) {
        if (!want_admin) return acct_result("NotAuthorized");
    } else {
        if (!session_is_admin()) return acct_result("NotAuthorized");
        if (want_admin) return acct_result("NotAuthorized");
    }

    nvr_user_row_t exist;
    int exists = (nvr_settings_user_get(c->settings, username, &exist) == 0);

    if (exists) {
        /* 覆盖：不可把已有 Admin 改成非 Admin，也不可把非 Admin 改成 Admin */
        if (level_is(exist.user_level, "Admin") && !want_admin)
            return acct_result("NotAuthorized");
        if (!level_is(exist.user_level, "Admin") && want_admin)
            return acct_result("NotAuthorized");
        /* 换等级时检查目标等级配额（覆盖自身不算新增） */
        if (!level_is(exist.user_level, user_level)) {
            if (want_tech && nvr_settings_user_count(c->settings, "Technician") >= NVR_USER_MAX_TECH)
                return acct_result("TooManyUsers");
            if (want_view && nvr_settings_user_count(c->settings, "Viewer") >= NVR_USER_MAX_VIEWER)
                return acct_result("TooManyUsers");
        }
        nvr_user_row_t u = exist;
        snprintf(u.user_level, sizeof(u.user_level), "%s", user_level);
        fill_user_pw(&u, password);
        if (nvr_settings_user_upsert(c->settings, &u) != 0)
            return acct_result(NOP_RESULT_FAIL_OTHER);
        NVR_LOGI("account", "createUser overwrite %s level=%s", u.username, u.user_level);
        return acct_result(NOP_RESULT_OK);
    }

    /* 新建配额 */
    if (want_admin && nvr_settings_user_count(c->settings, "Admin") >= 1)
        return acct_result("NotAuthorized");
    if (want_tech && nvr_settings_user_count(c->settings, "Technician") >= NVR_USER_MAX_TECH)
        return acct_result("TooManyUsers");
    if (want_view && nvr_settings_user_count(c->settings, "Viewer") >= NVR_USER_MAX_VIEWER)
        return acct_result("TooManyUsers");

    nvr_user_row_t u;
    memset(&u, 0, sizeof(u));
    snprintf(u.username, sizeof(u.username), "%s", username);
    snprintf(u.user_level, sizeof(u.user_level), "%s", user_level);
    fill_user_pw(&u, password);
    u.create_time = (int64_t)time(NULL);
    if (nvr_settings_user_upsert(c->settings, &u) != 0)
        return acct_result(NOP_RESULT_FAIL_OTHER);

    /* 向导首次建 Admin：自动视为已登录，便于后续 createUser */
    if (empty && want_admin)
        session_set("local", u.username, "Admin");

    NVR_LOGI("account", "createUser %s level=%s", u.username, u.user_level);
    return acct_result(NOP_RESULT_OK);
}

char *cmd_GUI_deleteUser(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *username = nvr_jstr(a, "username", NULL);
    if (!c || !c->settings || !username || !username[0])
        return nvr_resp_err("invalid_param");
    if (!session_is_admin()) return acct_result("NotAuthorized");

    nvr_user_row_t u;
    if (nvr_settings_user_get(c->settings, username, &u) != 0)
        return acct_result("UserNotFound");
    if (level_is(u.user_level, "Admin"))
        return acct_result("FixedUser");
    if (g_logged_in && strcmp(g_username, u.username) == 0)
        return acct_result("CannotDeleteSelf");

    if (nvr_settings_user_delete(c->settings, u.username) != 0)
        return acct_result(NOP_RESULT_FAIL_OTHER);
    NVR_LOGI("account", "deleteUser %s", u.username);
    return acct_result(NOP_RESULT_OK);
}

char *cmd_GUI_setUser(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *username   = nvr_jstr(a, "username", NULL);
    const char *password   = nvr_jhas(a, "password") ? nvr_jstr(a, "password", "") : NULL;
    const char *old_pw     = nvr_jhas(a, "oldPassword") ? nvr_jstr(a, "oldPassword", "") : NULL;
    const char *user_level = nvr_jhas(a, "userLevel") ? nvr_jstr(a, "userLevel", "") : NULL;
    if (!c || !c->settings || !username || !username[0])
        return nvr_resp_err("invalid_param");

    nvr_user_row_t u;
    if (nvr_settings_user_get(c->settings, username, &u) != 0)
        return acct_result("UserNotFound");

    int change_lv = (user_level && user_level[0]);
    int change_pw = (password != NULL); /* 字段存在即改密；空串会 PasswordTooWeak */

    if (change_lv) {
        if (!session_is_admin()) return acct_result("NotAuthorized");
        if (level_is(u.user_level, "Admin")) return acct_result("FixedUser");
        if (level_is(user_level, "Admin")) return acct_result("InvalidUserLevel");
        if (!level_is(user_level, "Technician") && !level_is(user_level, "Viewer"))
            return acct_result("InvalidUserLevel");
        if (!level_is(u.user_level, user_level)) {
            if (level_is(user_level, "Technician") &&
                nvr_settings_user_count(c->settings, "Technician") >= NVR_USER_MAX_TECH)
                return acct_result("TooManyUsers");
            if (level_is(user_level, "Viewer") &&
                nvr_settings_user_count(c->settings, "Viewer") >= NVR_USER_MAX_VIEWER)
                return acct_result("TooManyUsers");
        }
        snprintf(u.user_level, sizeof(u.user_level), "%s", user_level);
    }

    if (change_pw) {
        if (!password_ok(password)) return acct_result("PasswordTooWeak");
        /* local Admin 改自身密：传了 oldPassword 则校验 */
        if (level_is(u.user_level, "Admin") && old_pw) {
            if (!pw_ok_row(&u, old_pw)) return acct_result("WrongOldPassword");
        }
        fill_user_pw(&u, password);
    }

    if (!change_lv && !change_pw) return acct_result(NOP_RESULT_OK);
    if (nvr_settings_user_upsert(c->settings, &u) != 0)
        return acct_result(NOP_RESULT_FAIL_OTHER);
    return acct_result(NOP_RESULT_OK);
}

char *cmd_GUI_getUsers(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "users");
    if (!c || !c->settings) return nvr_resp_content(o);

    nvr_user_row_t list[NVR_USER_MAX];
    int n = nvr_settings_user_list(c->settings, list, NVR_USER_MAX);
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "username", list[i].username);
        cJSON_AddStringToObject(e, "userType", "local");
        cJSON_AddStringToObject(e, "userLevel", list[i].user_level);
        char t0[32], t1[32];
        iso8601(list[i].create_time, t0, sizeof(t0));
        iso8601(list[i].last_login, t1, sizeof(t1));
        if (t0[0]) cJSON_AddStringToObject(e, "createTime", t0);
        if (t1[0]) cJSON_AddStringToObject(e, "lastLogin", t1);
        else       cJSON_AddStringToObject(e, "lastLogin", "");
        cJSON_AddItemToArray(arr, e);
    }

    /* 绑 NOP 时追加 aws 账户行（无密码） */
    nvr_owner_row_t ow;
    if (nvr_settings_owner_get(c->settings, &ow) == 0 && ow.owner_id[0]) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "username", ow.username[0] ? ow.username : ow.owner_id);
        cJSON_AddStringToObject(e, "userType", "aws");
        cJSON_AddStringToObject(e, "userLevel", "Admin");
        cJSON_AddStringToObject(e, "createTime", "");
        cJSON_AddStringToObject(e, "lastLogin", "");
        cJSON_AddItemToArray(arr, e);
    }
    return nvr_resp_content(o);
}

char *cmd_GUI_getUserGroupPermissions(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    cJSON *o = cJSON_CreateObject();
    cJSON *groups = cJSON_AddObjectToObject(o, "groups");

    cJSON *admin = cJSON_AddObjectToObject(groups, "Admin");
    cJSON_AddObjectToObject(admin, "LiveView");
    cJSON_AddObjectToObject(admin, "Control");
    cJSON_AddObjectToObject(admin, "Setting");
    cJSON_AddObjectToObject(admin, "Account");

    cJSON *tech = cJSON_AddObjectToObject(groups, "Technician");
    cJSON_AddObjectToObject(tech, "LiveView");
    cJSON_AddObjectToObject(tech, "Control");

    cJSON *view = cJSON_AddObjectToObject(groups, "Viewer");
    cJSON_AddObjectToObject(view, "LiveView");

    return nvr_resp_content(o);
}

char *cmd_GUI_forgetPassword(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *user_type = nvr_jstr(a, "userType", "local");
    const char *username  = nvr_jstr(a, "username", NULL);
    if (!username) return nvr_resp_err("invalid_param");

    /* aws 联网：界面提示官网/APP 重置，固件不处理 */
    if (strcmp(user_type, "aws") == 0)
        return acct_result(NOP_RESULT_OK);

    if (!c || !c->settings) return acct_result(NOP_RESULT_FAIL_OTHER);

    /* local：校验 ResetCode（UID 前 6 + 本地日期 yyyy-mm-dd） */
    if (!str_eq_ci(username, "admin") && !str_eq_ci(username, "Admin")) {
        /* 允许当前 Admin 用户名 */
        nvr_user_row_t au;
        if (nvr_settings_user_get(c->settings, username, &au) != 0 ||
            !level_is(au.user_level, "Admin"))
            return acct_result(NOP_RESULT_FAIL_USERNAME);
    }

    const char *code = nvr_jstr(a, "localResetCode", NULL);
    if (!code || !code[0])
        return acct_result(NOP_RESULT_FAIL_RESET_CODE);

    /* 连续 3 次错码锁 1 小时（与登录锁共用计数键会互相影响；用独立键） */
    int64_t until = (int64_t)nvr_settings_get_int(c->settings, "account.reset_lockout_until", 0);
    if (until > (int64_t)time(NULL))
        return acct_result(NOP_RESULT_FAIL_LOCKED_10MIN);

    char uid[64];
    nvr_identity_get_uid(uid, sizeof(uid));   /* UID ← /User/tutk_agent_udid(重置码取前 6 位) */
    if (!uid[0] || !nvr_resetcode_verify(uid, code)) {
        int n = nvr_settings_get_int(c->settings, "account.reset_fail_count", 0) + 1;
        nvr_settings_set_int(c->settings, "account.reset_fail_count", n);
        if (n >= 3) {
            nvr_settings_set_int(c->settings, "account.reset_lockout_until", (int)(time(NULL) + 3600));
            nvr_settings_set_int(c->settings, "account.reset_fail_count", 0);
        }
        return acct_result(NOP_RESULT_FAIL_RESET_CODE);
    }

    nvr_settings_set_int(c->settings, "account.reset_fail_count", 0);
    nvr_settings_set_int(c->settings, "account.reset_lockout_until", 0);
    lockout_clear(c);

    /* 离线从 NOP 恢复 Admin：清 owner / 离线缓存，便于重新添加 */
    nvr_owner_row_t ow;
    if (nvr_settings_owner_get(c->settings, &ow) == 0 && ow.owner_id[0]) {
        memset(&ow, 0, sizeof(ow));
        nvr_settings_owner_set(c->settings, &ow);
        nvr_settings_set_str(c->settings, "account.aws_cache_user", "");
        nvr_settings_set_str(c->settings, "account.aws_cache_pw", "");
        nvr_identity_set_tutk_creds(NVR_DEF_TUTK_AUTHKEY, NULL);  /* IOTCKey 复位默认(保留 AVKey) */
        nvr_settings_set_str(c->settings, "ble.key", "");
        NVR_LOGI("account", "forgetPassword: cleared nop_owner + ble.key (recover Admin)");
    }

    /* 校验通过后，新密码由界面再调 GUI_setUser(不传 oldPassword) 写入 */
    NVR_LOGI("account", "forgetPassword ResetCode ok user=%s", username);
    return acct_result(NOP_RESULT_OK);
}
