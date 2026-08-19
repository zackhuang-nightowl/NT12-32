/***************************************************************************************
 *  nvr_cognito.h — AWS Cognito USER_PASSWORD_AUTH（NOP 账户鉴权）。
 *
 *  对齐 NOP_DOC ServeDomainV2 / Server_NOP_Account / GUI_login(aws)：
 *    hosted UI = NVR_URL_COGNITO；InitiateAuth POST NVR_URL_COGNITO_IDP
 *    X-Amz-Target: AWSCognitoIdentityProviderService.InitiateAuth
 *    ClientId / pool 随 -DNVR_STAGE 编译期选择（见 app/config/nvr_urls.h）。
 *    从 AuthenticationResult.IdToken(JWT) payload.sub 取 owner_id。
 ***************************************************************************************/
#ifndef NVR_COGNITO_H
#define NVR_COGNITO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NVR_COGNITO_OK = 0,
    NVR_COGNITO_ERR_PARAM,
    NVR_COGNITO_ERR_NETWORK,
    NVR_COGNITO_ERR_CREDENTIALS, /* NotAuthorized / 错账密 */
    NVR_COGNITO_ERR_USER_NOT_FOUND,
    NVR_COGNITO_ERR_OTHER
} nvr_cognito_rc_t;

typedef struct {
    char owner_id[64];           /* JWT sub */
    char username[64];           /* cognito:username（可空） */
    char email[128];             /* 可空 */
    char phone[32];              /* 可空 */
    char access_token[2048];     /* GraphQL Authorization（AccessToken） */
} nvr_cognito_user_t;

/* 在线 Cognito 登录。成功填 out；失败返回对应 rc（不写 out）。 */
nvr_cognito_rc_t nvr_cognito_login(const char *username, const char *password,
                                   nvr_cognito_user_t *out);

#ifdef __cplusplus
}
#endif
#endif /* NVR_COGNITO_H */
