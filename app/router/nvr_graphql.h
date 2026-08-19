/***************************************************************************************
 *  nvr_graphql.h — Night Owl Protect GraphQL（addDevice → cloudToken/stoken）。
 ***************************************************************************************/
#ifndef NVR_GRAPHQL_H
#define NVR_GRAPHQL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NVR_GQL_OK = 0,
    NVR_GQL_ERR_PARAM,
    NVR_GQL_ERR_NETWORK,
    NVR_GQL_ERR_AUTH,
    NVR_GQL_ERR_ADD_DEVICE,   /* mutation 失败 / 无 cloudToken */
    NVR_GQL_ERR_OTHER
} nvr_gql_rc_t;

typedef struct {
    const char *access_token;    /* Cognito AccessToken（必填） */
    const char *uid;             /* TUTK UID */
    const char *name;            /* 设备显示名 */
    const char *primary_key;     /* IOTC authkey */
    const char *av_key;          /* AV 密码 */
    const char *bluetooth_id;    /* 可空 */
} nvr_gql_add_device_in_t;

/* 向导 aws 绑定：addDevice，成功写出 cloudToken(=stoken)。 */
nvr_gql_rc_t nvr_graphql_add_device(const nvr_gql_add_device_in_t *in,
                                    char *stoken_out, size_t stoken_cap);

#ifdef __cplusplus
}
#endif
#endif /* NVR_GRAPHQL_H */
