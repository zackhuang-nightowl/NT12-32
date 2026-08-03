/***************************************************************************************
 *  nvr_settings.h — 运行期可写配置库（SQLite）
 *
 *  设计（计划 §B7）：
 *    - 冻结的 config/*.json 只作「首次启动种子」，永不回写。
 *    - 本库 /config/nvr_settings.db (WAL, 0600) 为运行期权威可写存储：
 *      typed KV + 结构化表（auth / nop_owner / channel / cloud_channel /
 *      rec_schedule / rec_triggers / netif）。
 *    - typed get/set + set_many(单事务) + subscribe(前缀变更通知)。
 *    - 本组件为叶子：仅依赖 sqlite3 + cJSON，不反向依赖 app/。
 *
 *  桥接：app/config/nvr_config.c 的 nvr_config_overlay_from_settings() 在只读加载
 *        JSON 后叠加本库的可写覆盖；NOP handler 经本库读写实现持久化。
 ***************************************************************************************/
#ifndef NVR_SETTINGS_H
#define NVR_SETTINGS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_settings nvr_settings_t;

/* 打开设置库；首次启动（未 seeded）时用 json_defaults_dir 下的 *.json 播种。
 * json_defaults_dir 可为 NULL（不播种）。成功返回 0。 */
int  nvr_settings_open(const char *db_path, const char *json_defaults_dir, nvr_settings_t **out);
void nvr_settings_close(nvr_settings_t *s);

/* ---------- typed KV（点分命名空间，如 "system.device_name" / "cloud.switch"） ---------- */
int  nvr_settings_get_int (nvr_settings_t *s, const char *key, int def);
int  nvr_settings_set_int (nvr_settings_t *s, const char *key, int val);
/* get_str：拷入 out（含结尾 0），返回写入长度(不含 0)；缺失用 def。 */
int  nvr_settings_get_str (nvr_settings_t *s, const char *key, char *out, int cap, const char *def);
int  nvr_settings_set_str (nvr_settings_t *s, const char *key, const char *val);
int  nvr_settings_get_blob(nvr_settings_t *s, const char *key, void *out, int cap); /* 返回字节数 */
int  nvr_settings_set_blob(nvr_settings_t *s, const char *key, const void *val, int len);

/* 一次事务写多个 KV */
typedef struct { const char *key; int is_str; int ival; const char *sval; } nvr_kv_t;
int  nvr_settings_set_many(nvr_settings_t *s, const nvr_kv_t *kv, int n);

/* ---------- 变更通知：按 key 前缀订阅 ---------- */
typedef void (*nvr_settings_cb)(void *ud, const char *key);
int  nvr_settings_subscribe  (nvr_settings_t *s, const char *key_prefix, nvr_settings_cb cb, void *ud);
void nvr_settings_unsubscribe(nvr_settings_t *s, int sub_id);

/* ---------- 结构化：动态通道表（LAN Add 增删覆盖冻结 channels.json） ---------- */
typedef struct {
    int  chn;
    char name[64], url[256], user[64], pass[64];
    int  codec, stream, record;
    int  onvif_auto; char onvif_ip[64]; int onvif_port; int poe_port;
    int  enabled;                 /* 0=禁用(跳过) */
    int  kind;                    /* 0=NOP 1=NOPONVIF 2=ONVIF（见 nvr_dev_kind_t）*/
    char source[8];               /* "json" | "user" */
} nvr_chan_row_t;

int  nvr_settings_channel_upsert(nvr_settings_t *s, const nvr_chan_row_t *row);
int  nvr_settings_channel_delete(nvr_settings_t *s, int chn);
int  nvr_settings_channel_list  (nvr_settings_t *s, nvr_chan_row_t *out, int cap); /* 返回条数 */

/* ---------- 结构化：管理员鉴权（口令 hash + 锁定） ---------- */
typedef struct {
    char     pw_algo[16];         /* "pbkdf2" / "argon2id" ... */
    uint8_t  pw_hash[64]; int hash_len;
    uint8_t  pw_salt[32]; int salt_len;
    int      fail_count;
    int64_t  lockout_until;       /* epoch sec; 0=未锁 */
} nvr_auth_row_t;
int  nvr_settings_auth_get(nvr_settings_t *s, nvr_auth_row_t *out);   /* 无记录返回 <0 */
int  nvr_settings_auth_set(nvr_settings_t *s, const nvr_auth_row_t *row);

/* ---------- 结构化：NOP owner / 云存凭据（stoken 非易失） ---------- */
typedef struct { char owner_id[64], username[64], stoken[256]; } nvr_owner_row_t;
int  nvr_settings_owner_get(nvr_settings_t *s, nvr_owner_row_t *out);
int  nvr_settings_owner_set(nvr_settings_t *s, const nvr_owner_row_t *row);

/* ---------- 结构化：每通道云存配置（streamType/triggers） ---------- */
typedef struct { int chn; char stream_type[8]; char triggers[128]; } nvr_cloud_ch_row_t;
int  nvr_settings_cloud_ch_upsert(nvr_settings_t *s, const nvr_cloud_ch_row_t *row);
int  nvr_settings_cloud_ch_list  (nvr_settings_t *s, nvr_cloud_ch_row_t *out, int cap);

#ifdef __cplusplus
}
#endif
#endif /* NVR_SETTINGS_H */
