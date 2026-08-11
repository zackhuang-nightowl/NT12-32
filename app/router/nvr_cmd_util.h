/***************************************************************************************
 *  nvr_cmd_util.h — 路由层共享:应答构造 + JSON 取值(收敛原 router/display 各自重复的一份)。
 ***************************************************************************************/
#ifndef NVR_CMD_UTIL_H
#define NVR_CMD_UTIL_H
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 应答串(malloc;调用方 free)。 */
char *nvr_resp_status (int code, const char *msg);            /* {"statusCode","statusMsg"} 底层 */
char *nvr_resp_content(cJSON *content);                       /* 200 + content;接管 content 所有权 */
char *nvr_resp_result (const char *result);                   /* 200 + {"result":...} */
/* NOP 统一返回(全软件遵循):失败=200+content.error,成功(动作类)=200+空content(无error),
 * 不支持=501 NOT_SUPPORT。查询类成功仍用 nvr_resp_content(data)。 */
char *nvr_resp_err        (const char *error);                /* 失败:200 + {"error": reason} */
char *nvr_resp_ok         (void);                             /* 成功(无数据):200 + {} */
char *nvr_resp_not_support(void);                             /* 501 + "NOT_SUPPORT" */

/* JSON 取值(o 可空)。 */
const char *nvr_jstr (const cJSON *o, const char *k, const char *d);
int         nvr_jint (const cJSON *o, const char *k, int d);
int         nvr_jbool(const cJSON *o, const char *k, int d);
int         nvr_jhas (const cJSON *o, const char *k);

/* 回落 components/nop cap handler(LOCAL 表命中后仍走 NVR 进程内 nop_app)。 */
struct nvr_cmd_ctx_t;
char *nvr_cmd_nop_dispatch(cJSON *args, const struct nvr_cmd_ctx_t *ctx, const char *func);

#ifdef __cplusplus
}
#endif
#endif /* NVR_CMD_UTIL_H */
