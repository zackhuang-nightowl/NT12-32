/***************************************************************************************
 *  nvr_settings.h — 运行期可写配置库（SQLite）
 *
 *  设计（计划 §B7 / server-nop-onvif plan P1）：
 *    - 冻结的 config/*.json 只作「首次启动种子」,永不回写。
 *    - 本库 /config/nvr_settings.db (WAL, 0600) 为运行期权威可写存储:
 *      typed KV + 结构化表(auth / local_user / nop_owner / camera / camera_capability /
 *      record_config / push_config / cloud_channel / schedule /
 *      local_link / email_alert / ftp / ddns)。
 *    - typed get/set + set_many(单事务) + subscribe(前缀变更通知)。
 *    - 本组件为叶子:仅依赖 sqlite3 + cJSON,不反向依赖 app/。
 *
 *  schema_version=2:camera 取代旧 channel;网络服务表取代死表 netif;录像/推送/云存
 *  排程规范化。首次建库直接建新表,不做 v1→v2 数据迁移(旧库删除重建)。
 ***************************************************************************************/
#ifndef NVR_SETTINGS_H
#define NVR_SETTINGS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_settings nvr_settings_t;

/* 打开设置库;首次启动(未 seeded)时用 json_defaults_dir 下的 *.json 播种。
 * json_defaults_dir 可为 NULL(不播种)。成功返回 0。 */
int  nvr_settings_open(const char *db_path, const char *json_defaults_dir, nvr_settings_t **out);
void nvr_settings_close(nvr_settings_t *s);
int  nvr_settings_factory_reset(nvr_settings_t *s);  /* 清用户数据后重播种 32 通道默认行,保留 meta_kv */

/* ---------- typed KV(点分命名空间,如 "system.device_name" / "cloud.switch") ---------- */
int  nvr_settings_get_int (nvr_settings_t *s, const char *key, int def);
int  nvr_settings_set_int (nvr_settings_t *s, const char *key, int val);
/* get_str:拷入 out(含结尾 0),返回写入长度(不含 0);缺失用 def。 */
int  nvr_settings_get_str (nvr_settings_t *s, const char *key, char *out, int cap, const char *def);
int  nvr_settings_set_str (nvr_settings_t *s, const char *key, const char *val);
int  nvr_settings_get_blob(nvr_settings_t *s, const char *key, void *out, int cap); /* 返回字节数 */
int  nvr_settings_set_blob(nvr_settings_t *s, const char *key, const void *val, int len);

/* 一次事务写多个 KV */
typedef struct { const char *key; int is_str; int ival; const char *sval; } nvr_kv_t;
int  nvr_settings_set_many(nvr_settings_t *s, const nvr_kv_t *kv, int n);

/* ---------- 变更通知:按 key 前缀订阅 ---------- */
typedef void (*nvr_settings_cb)(void *ud, const char *key);
int  nvr_settings_subscribe  (nvr_settings_t *s, const char *key_prefix, nvr_settings_cb cb, void *ud);
void nvr_settings_unsubscribe(nvr_settings_t *s, int sub_id);

/* ---------- 结构化:camera 设备表(逻辑通道 1-based;有真实 ip 即落库,mac 等可后补) ---------- */
typedef struct {
    int  chn;                     /* 逻辑通道,1-based */
    char name[64];
    int  enabled;                 /* 0=禁用(跳过) */
    char source[8];               /* "POE" | "LAN" */
    char protocol[8];             /* "nop" | "onvif"(= backend 可读表达) */
    int  kind;                    /* 0=NOP 1=NOPONVIF 2=ONVIF(见 nvr_dev_kind_t) */
    int  backend;                 /* 0=NOP 透传 / 1=ONVIF 翻译 */
    char type[8];                 /* "single" | "multi"(多源占 2 通道,同 mac) */
    int  dev_chn;                 /* 设备侧 channel(透传改写目标;单目=1) */
    char ip[64];
    char mac[24];                 /* IP 变化后按 mac 找回 */
    char username[64], password[64];
    int  onvif_port, nop_port;
    char service_url[128];        /* device_service XAddr */
    char url[256];                /* 显式 RTSP(可空,ONVIF 解析优先) */
    int  onvif_auto;
    int  poe_port;                /* >0=PoE 物理口;0=LAN */
    int  codec, stream, record;
    char uuid[100], serial[64], manufacturer[64], model[64], firmware[64];
    int  bound, active;
    char video_source_token[100]; /* 多源:该通道绑定的 ONVIF VideoSourceToken(空=单源/首源) */
    char enh_random[32];          /* NOP EnhancedSecurity random；空=普通模式 */
} nvr_camera_row_t;

/* 通道 PK 表(camera / camera_capability / record_* / push_config / cloud_channel)
 * 开库即预置 0..31 行;接口只 UPDATE 已有行,不 INSERT/DELETE 通道行。 */
int  nvr_settings_camera_upsert(nvr_settings_t *s, const nvr_camera_row_t *row);
int  nvr_settings_camera_delete(nvr_settings_t *s, int chn); /* 只清本 chn 的 camera 字段+能力,不删行 */
int  nvr_settings_camera_get   (nvr_settings_t *s, int chn, nvr_camera_row_t *out);       /* 无返回 <0 */
int  nvr_settings_camera_find_by_mac(nvr_settings_t *s, const char *mac, nvr_camera_row_t *out); /* 无返回 <0 */
int  nvr_settings_camera_list  (nvr_settings_t *s, nvr_camera_row_t *out, int cap);        /* 返回条数 */

/* ---------- 结构化:每通道能力集(首次上线探测后写;getDeviceCapabilities 只读) ---------- */
/* caps_json 由调用方分配;get 返回写入长度(不含 0),无记录 out[0]=0 返回 0。 */
int  nvr_settings_caps_set(nvr_settings_t *s, int chn, const char *caps_json, const char *signal);
int  nvr_settings_caps_get(nvr_settings_t *s, int chn, char *caps_json_out, int cap,
                           char *signal_out, int signal_cap);

/* ---------- 结构化:录像配置(开关/触发源/码流;排程见 schedule) ---------- */
typedef struct { int chn, record_on; char triggers[128], stream_type[8]; } nvr_record_cfg_t;
int  nvr_settings_record_set(nvr_settings_t *s, const nvr_record_cfg_t *row);
int  nvr_settings_record_get(nvr_settings_t *s, int chn, nvr_record_cfg_t *out);   /* 无返回 <0 */
/* 事件后录秒数(get/setChannelRecordingTime)；无 KV 返回 -1；合法范围 1..600 */
int  nvr_settings_record_post_s_get(nvr_settings_t *s, int chn);
int  nvr_settings_record_post_s_set(nvr_settings_t *s, int chn, int sec);
/* 事件预录秒数(仅事件/待命模式)；无 KV 返回 -1；合法范围 0..30（0=关预录） */
int  nvr_settings_record_pre_s_get(nvr_settings_t *s, int chn);
int  nvr_settings_record_pre_s_set(nvr_settings_t *s, int chn, int sec);

/* ---------- 结构化:持续录像排程(定时录像总开关 + 周排程 rules JSON;NVR 本地) ----------
 * rules 为 X_NightOwl_*ContinuousRecordingSchedule 的 rules 数组原文(JSON 字符串,秒级区间)。 */
typedef struct { int chn, sched_on; char rules[2048]; } nvr_rec_schedule_t;
int  nvr_settings_rec_sched_set(nvr_settings_t *s, const nvr_rec_schedule_t *row);
int  nvr_settings_rec_sched_get(nvr_settings_t *s, int chn, nvr_rec_schedule_t *out); /* 无行返回 <0 */

/* ---------- 结构化:推送配置(开关 + 免打扰单日时段 + 日期) ---------- */
typedef struct {
    int  chn, switch_on, dnd_enable, photo_on;
    int  snooze_end;                  /* UTC unix；0=关 */
    char dnd_start[8], dnd_end[8];    /* "HHMM" */
    char dnd_weekdays[16];            /* CSV "1..7",默认全周 */
    char time_unit[8];                /* "hour" | "minute" */
    char triggers[128];               /* CSV；空=未设置,GET 回 [] */
} nvr_push_cfg_t;
int  nvr_settings_push_set(nvr_settings_t *s, const nvr_push_cfg_t *row);
int  nvr_settings_push_get(nvr_settings_t *s, int chn, nvr_push_cfg_t *out);       /* 无返回 <0 */

/* ---------- 结构化:每通道云存配置(streamType/triggers/enable) ---------- */
typedef struct { int chn; char stream_type[8]; char triggers[128]; int enable; } nvr_cloud_ch_row_t;
int  nvr_settings_cloud_ch_upsert(nvr_settings_t *s, const nvr_cloud_ch_row_t *row); /* UPDATE 已有 chn 行 */
int  nvr_settings_cloud_ch_get   (nvr_settings_t *s, int chn, nvr_cloud_ch_row_t *out); /* 无返回 <0 */
int  nvr_settings_cloud_ch_list  (nvr_settings_t *s, nvr_cloud_ch_row_t *out, int cap);

/* ---------- 结构化:排程规则(录像连续/事件 + 云存;rules[] 模型) ---------- */
typedef struct {
    int  chn;
    char domain[16];              /* "record_cont" | "record_event" | "cloud" */
    char sensor[16];              /* 事件录像用;连续/云存留空 */
    char rule_id[32];
    char weekdays[16];            /* CSV "1..7" */
    char start_hms[8], end_hms[8];/* "HHMMSS" */
} nvr_schedule_row_t;
/* 覆盖一个 (chn,domain[,sensor]) 的规则列表(复合主键,非整通道删行)。rows 可为 0 条=该组清空。 */
int  nvr_settings_schedule_replace(nvr_settings_t *s, int chn, const char *domain, const char *sensor,
                                   const nvr_schedule_row_t *rows, int n);
int  nvr_settings_schedule_list(nvr_settings_t *s, int chn, const char *domain, const char *sensor,
                                nvr_schedule_row_t *out, int cap);
/* 该通道 domain 下已保存的规则条数(任意 sensor)。0=从未保存。 */
int  nvr_settings_schedule_count(nvr_settings_t *s, int chn, const char *domain);
/* 当前 (wday 1-7, sod 当日秒) 是否落在排程内。跨零点区间支持。
 * record_event 无规则=未保存,不允许(GET 仍可合成 7×24 给 GUI)。其它 domain 无规则=7×24。 */
int  nvr_settings_schedule_allows(nvr_settings_t *s, int chn, const char *domain, const char *sensor,
                                  int wday, int sod);

/* ---------- 结构化:管理员鉴权(口令 hash + 锁定) ---------- */
typedef struct {
    char     pw_algo[16];         /* "pbkdf2" / "argon2id" ... */
    uint8_t  pw_hash[64]; int hash_len;
    uint8_t  pw_salt[32]; int salt_len;
    int      fail_count;
    int64_t  lockout_until;       /* epoch sec; 0=未锁 */
} nvr_auth_row_t;
int  nvr_settings_auth_get(nvr_settings_t *s, nvr_auth_row_t *out);   /* 无记录返回 <0 */
int  nvr_settings_auth_set(nvr_settings_t *s, const nvr_auth_row_t *row);

/* ---------- 结构化:本地多用户(Admin1 / Technician≤10 / Viewer≤10) ---------- */
#define NVR_USER_MAX           21
#define NVR_USER_MAX_TECH      10
#define NVR_USER_MAX_VIEWER    10
typedef struct {
    char     username[33];        /* 1..32 */
    char     user_level[16];      /* Admin | Technician | Viewer */
    char     pw_algo[16];         /* "sha256" */
    uint8_t  pw_hash[64]; int hash_len;
    int64_t  create_time;         /* epoch sec */
    int64_t  last_login;          /* epoch sec; 0=从未 */
} nvr_user_row_t;
int  nvr_settings_user_get  (nvr_settings_t *s, const char *username, nvr_user_row_t *out); /* 无 <0 */
int  nvr_settings_user_upsert(nvr_settings_t *s, const nvr_user_row_t *row);  /* 按 username 覆盖 */
int  nvr_settings_user_delete(nvr_settings_t *s, const char *username);
int  nvr_settings_user_list (nvr_settings_t *s, nvr_user_row_t *out, int cap); /* 返回条数 */
int  nvr_settings_user_count(nvr_settings_t *s, const char *user_level); /* NULL=总数; 指定等级计数 */
int  nvr_settings_user_touch_login(nvr_settings_t *s, const char *username); /* 更新 last_login */

/* ---------- 结构化:NOP owner / 云存凭据(stoken 非易失) ---------- */
typedef struct { char owner_id[64], username[64], stoken[256]; } nvr_owner_row_t;
int  nvr_settings_owner_get(nvr_settings_t *s, nvr_owner_row_t *out);
int  nvr_settings_owner_set(nvr_settings_t *s, const nvr_owner_row_t *row);

/* ---------- 结构化:网络服务(设备级单例,取代死表 netif) ---------- */
typedef struct {
    char network_type[8];         /* "DHCP" | "Static" */
    char mac[24];                 /* 只读,硬件 MAC */
    char ip[64], subnet_mask[64], gateway[64], dns1[64], dns2[64];
} nvr_local_link_t;               /* GUI_get/setLocalLink */
int  nvr_settings_local_link_get(nvr_settings_t *s, nvr_local_link_t *out);   /* 无返回 <0 */
int  nvr_settings_local_link_set(nvr_settings_t *s, const nvr_local_link_t *row);

typedef struct {
    int  enable;
    char receiver[5][128];        /* Receiver1..5 */
    char sender[128]; int smtp_port; char smtp_server[128];
    char username[64], password[64]; int use_ssl;
    char title[64]; int interval;
} nvr_email_cfg_t;                /* GUI_get/setEmailAlert */
int  nvr_settings_email_get(nvr_settings_t *s, nvr_email_cfg_t *out);         /* 无返回 <0 */
int  nvr_settings_email_set(nvr_settings_t *s, const nvr_email_cfg_t *row);

typedef struct {
    int  enable, anonymous, max_file_len, port;
    char password[64], remote_dir[128], server[128], username[64];
} nvr_ftp_cfg_t;                  /* GUI_get/setFTP */
int  nvr_settings_ftp_get(nvr_settings_t *s, nvr_ftp_cfg_t *out);             /* 无返回 <0 */
int  nvr_settings_ftp_set(nvr_settings_t *s, const nvr_ftp_cfg_t *row);

typedef struct {
    int  idx, enable;
    char domain[64], password[64], hostname[128], ddns_key[64], username[64];
} nvr_ddns_row_t;                 /* GUI_get/setDDNS(数组→多行) */
int  nvr_settings_ddns_replace(nvr_settings_t *s, const nvr_ddns_row_t *rows, int n); /* 覆盖全部 */
int  nvr_settings_ddns_list   (nvr_settings_t *s, nvr_ddns_row_t *out, int cap);

#ifdef __cplusplus
}
#endif
#endif /* NVR_SETTINGS_H */
