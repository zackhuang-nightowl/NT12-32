/***************************************************************************************
 *  nvr_urls.h — 出站 HTTP(S) URL 总表。对齐 NOP_DOC ServeDomainV2。
 *
 *  编译二选一（两套独立固件）：
 *    默认 production
 *    -DNVR_STAGE=ON  →  NVR_BUILD_STAGE=1  →  stage
 *
 *  不收录：本机相机拼接（APPJsonCmd / upload.cgi）、iotc-tunnel、ONVIF 命名空间。
 ***************************************************************************************/
#ifndef NVR_URLS_H
#define NVR_URLS_H

#if defined(NVR_BUILD_STAGE) && (NVR_BUILD_STAGE)
#  define NVR_URL_IS_STAGE 1
#else
#  define NVR_URL_IS_STAGE 0
#endif

#if NVR_URL_IS_STAGE
/* ========================= Stage（ServeDomainV2）========================= */

/* Cognito 鉴权登录 */
#define NVR_URL_COGNITO            "https://nowl-protect-staging.auth.us-east-1.amazoncognito.com"
#define NVR_URL_COGNITO_POOL_ID    "RE647Cind"
#define NVR_URL_COGNITO_REGION     "us-east-1"
#define NVR_URL_COGNITO_CLIENT_ID  "5vmvkt4bh9ravj4kf0hnuqiq2t"

/* 账户服务器 GraphQL */
#define NVR_URL_GRAPHQL            "https://protect-staging.nowlsp.com/graphql"

/* push_notification */
#define NVR_URL_PUSH               "https://push-staging.kalay.us/tpns"

/* OTA：https://ota.nowlsp.com/ota/GET/i/NightOwl/<product>/<model> */
#define NVR_URL_OTA_DOMAIN         "ota.nowlsp.com"
#define NVR_URL_OTA_ENV            "NightOwl"

/* smart_home */
#define NVR_URL_SMART_HOME         "https://protect_google_home_staging.nowlsp.com"

/* upload_image */
#define NVR_URL_UPLOAD_IMAGE       "https://asia-upload-tutk-stg.kalay.us/filestorage/api/v1/upload"

/* cloudRec / VSaaS */
#define NVR_URL_CLOUD_REC          "https://asia-vpapi-tutk-stg.kalay.us"
#define NVR_URL_VSAAS_HOST         "asia-vpapi-tutk-stg.kalay.us"

#else
/* ========================= Production（ServeDomainV2）==================== */

/* Cognito 鉴权登录 */
#define NVR_URL_COGNITO            "https://no-protect-prod.auth.us-east-1.amazoncognito.com"
#define NVR_URL_COGNITO_POOL_ID    "oX4P9V0Ig"
#define NVR_URL_COGNITO_REGION     "us-east-1"
#define NVR_URL_COGNITO_CLIENT_ID  "7a7t4ds667njvemo0e6aobotce"

/* 账户服务器 GraphQL */
#define NVR_URL_GRAPHQL            "https://protect.nowlsp.com/graphql"

/* push_notification */
#define NVR_URL_PUSH               "https://notifications.nowlsp.com/tpns"

/* OTA：https://ota.nowlsp.com/ota/GET/i/NightOwl_Production/<product>/<model> */
#define NVR_URL_OTA_DOMAIN         "ota.nowlsp.com"
#define NVR_URL_OTA_ENV            "NightOwl_Production"

/* smart_home */
#define NVR_URL_SMART_HOME         "https://protect_google_home_prod.nowlsp.com"

/* upload_image */
#define NVR_URL_UPLOAD_IMAGE       "https://smart-notifications.nowlsp.com/filestorage/api/v1/upload"

/* cloudRec / VSaaS */
#define NVR_URL_CLOUD_REC          "https://us-vsaasapi-nop.kalayservice.com"
#define NVR_URL_VSAAS_HOST         "us-vsaasapi-nop.kalayservice.com"

#endif /* NVR_URL_IS_STAGE */

/* ========================= 由上表推导、两边相同 ========================= */

/* InitiateAuth 走 Cognito IdP API（region 来自 ServeDomainV2 aws_region），
 * 不是 hosted UI（NVR_URL_COGNITO）。 */
#define NVR_URL_COGNITO_IDP \
    "https://cognito-idp." NVR_URL_COGNITO_REGION ".amazonaws.com/"

/* OTA 查询前缀：GET <base>/<env>/<product>/<model> */
#define NVR_URL_OTA_PATH           "/ota/GET/i"
#define NVR_URL_OTA_BASE           "https://" NVR_URL_OTA_DOMAIN NVR_URL_OTA_PATH
#define NVR_URL_OTA_NVR_PRODUCT    "networkVideoRecorder"   /* NVR OTA 查询设备类型(非 videoRecorder) */
#define NVR_URL_OTA_CAM_PRODUCT    "standaloneIpCamera"

/* VSaaS 路径模板（host = NVR_URL_VSAAS_HOST） */
#define NVR_URL_VSAAS_STREAM \
    "https://%s/vsaas/api/v1/stream/stream_url/%s?stoken=%s&starttime=%u&protocol=upload&event_id=%d&tags=%s"
#define NVR_URL_VSAAS_EVENT \
    "https://%s/vsaas/api/v1/stream/stream_event/%s?stoken=%s&starttime=%u&tags=%s"

/* ========================= 推送（pushNotification_standalone）================
 * 域名已按环境选好（NVR_URL_PUSH / NVR_URL_UPLOAD_IMAGE）。下面是协议常量，
 * 编译期生效；未接线的类型也先收在这里，避免以后再散落到 .c。 */
#define NVR_URL_PUSH_CMD           "event"
#define NVR_URL_PUSH_DEV_TYPE      "networkVideoRecorder"   /* DVR/NVR；单机是 standaloneIpCamera */
#define NVR_URL_PUSH_DEV_TYPE_IPC  "standaloneIpCamera"     /* 文档单机值，本机不用 */
#define NVR_URL_UPLOAD_REALM       "tutk"
#define NVR_URL_PUSH_TLS_PORT      443
#define NVR_URL_PUSH_INTERVAL_MS   1000    /* 两次发送间隔；prod 丢 1s 内、stage 丢 5s 内 */
#if NVR_URL_IS_STAGE
#  define NVR_URL_PUSH_DEBUG       1       /* stage 带 &debug=1 便于看 411 Mapping Error */
#else
#  define NVR_URL_PUSH_DEBUG       0
#endif
#define NVR_URL_PUSH_SILENT_MS     60000   /* 同类型静默 1 分钟（doorbellRing 除外） */
#define NVR_URL_PUSH_SNAP_WAIT_MS  400     /* 等事件图落盘再读 */
#define NVR_URL_PUSH_SNAP_RETRY    5
#define NVR_URL_PUSH_IMG_MAX       10240   /* 推送配图 ≤10KB；超则只推文字 */
#define NVR_URL_UPLOAD_ACCEPT_MAX  204800  /* 图床上限 200KB（文档）；本机仍按 10KB 配图 */
#define NVR_URL_PUSH_DND_START     "2100"   /* 免打扰窗口默认;开关默认关 */
#define NVR_URL_PUSH_DND_END       "0700"

/* TPNS event_type（文档表；8012 msgType 的 human/face 是 2/3，推送必须用下面） */
#define NVR_URL_PUSH_EVT_MOTION    1
#define NVR_URL_PUSH_EVT_HUMAN     30302
#define NVR_URL_PUSH_EVT_FACE      30303
#define NVR_URL_PUSH_EVT_VEHICLE   30305
#define NVR_URL_PUSH_EVT_DOORBELL  30312
/* 以下文档未给 NVR 数值，先占位（low battery / full charged 不受 switch/trigger） */
#define NVR_URL_PUSH_EVT_ANIMAL    30316
#define NVR_URL_PUSH_EVT_PACKAGE   30317
#define NVR_URL_PUSH_EVT_LINECROSS 30103
#define NVR_URL_PUSH_EVT_INTRUSION 30104

/* customized_payload.event_type（NVR 用 E_DVR_*） */
#define NVR_URL_PUSH_KEY_MOTION    "E_DVR_MOTION"
#define NVR_URL_PUSH_KEY_HUMAN     "E_DVR_HUMAN"
#define NVR_URL_PUSH_KEY_FACE      "E_DVR_FACE_DETECTION"
#define NVR_URL_PUSH_KEY_VEHICLE   "E_DVR_VEHICLE_DETECTED"
#define NVR_URL_PUSH_KEY_DOORBELL  "E_DVR_DOORBELL_RING"
#define NVR_URL_PUSH_KEY_SA_MOTION "E_SA_MOTION"            /* 单机值，本机不用 */

#endif /* NVR_URLS_H */
