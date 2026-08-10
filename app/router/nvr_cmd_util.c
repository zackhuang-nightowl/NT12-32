/***************************************************************************************
 *  nvr_cmd_util.c — 见 nvr_cmd_util.h。
 ***************************************************************************************/
#include "nvr_cmd_util.h"
#include <stdlib.h>

char *nvr_resp_status(int code, const char *msg)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "statusCode", code);
    cJSON_AddStringToObject(r, "statusMsg", msg ? msg : (code == 200 ? "OK" : "ERR"));
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}
char *nvr_resp_content(cJSON *content)   /* 接管 content 所有权 */
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "statusCode", 200);
    cJSON_AddStringToObject(r, "statusMsg", "OK");
    cJSON_AddItemToObject(r, "content", content ? content : cJSON_CreateObject());
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}
char *nvr_resp_result(const char *result)
{
    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "result", result);
    return nvr_resp_content(c);
}

/* NOP 统一**失败**返回:statusCode 200 + content.error=失败原因(如 "format_failed"/"invalid_param")。
 * 全软件失败一律走此,不用裸 400/404/409/500。**成功不带 error**(见 nvr_resp_ok)。 */
char *nvr_resp_err(const char *error)
{
    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "error", error ? error : "unknown");
    return nvr_resp_content(c);
}
/* NOP 统一**成功**返回(无数据的动作类命令):仅 {"statusCode":200,"statusMsg":"OK"},**不带 content**。
 * 有数据的查询类命令用 nvr_resp_content(data)。 */
char *nvr_resp_ok(void)
{
    return nvr_resp_status(200, "OK");
}
/* 命令在本通道/相机上不支持:statusCode 501 + statusMsg "NOT_SUPPORT",无 content。 */
char *nvr_resp_not_support(void)
{
    return nvr_resp_status(501, "NOT_SUPPORT");
}

const char *nvr_jstr(const cJSON *o, const char *k, const char *d)
{ const cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return (v && cJSON_IsString(v)) ? v->valuestring : d; }
int nvr_jint(const cJSON *o, const char *k, int d)
{ const cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return (v && cJSON_IsNumber(v)) ? v->valueint : d; }
int nvr_jbool(const cJSON *o, const char *k, int d)
{ const cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL;
  if (!v) return d; if (cJSON_IsBool(v)) return cJSON_IsTrue(v); if (cJSON_IsNumber(v)) return v->valueint != 0; return d; }
int nvr_jhas(const cJSON *o, const char *k)
{ return o && cJSON_GetObjectItem(o, k) ? 1 : 0; }
