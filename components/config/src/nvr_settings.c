/***************************************************************************************
 *  nvr_settings.c — 运行期可写配置库（SQLite 实现）。见 nvr_settings.h / 计划 §B7。
 *  schema_version=2:camera 取代 channel;网络服务表取代 netif;录像/推送/云存排程规范化。
 ***************************************************************************************/
#include "nvr_settings.h"
#include "cJSON.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NVR_STG_MAX_SUB 32

typedef struct { char prefix[64]; nvr_settings_cb cb; void *ud; int id; int used; } sub_t;

struct nvr_settings {
    sqlite3 *db;
    sub_t    subs[NVR_STG_MAX_SUB];
    int      next_sub_id;
};

/* ---------------- 内部小工具 ---------------- */
static int exec_sql(sqlite3 *db, const char *sql)
{
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[nvr_settings] sql err: %s (%s)\n", err ? err : "?", sql);
        if (err) sqlite3_free(err);
        return -1;
    }
    return 0;
}

static void notify(nvr_settings_t *s, const char *key)
{
    for (int i = 0; i < NVR_STG_MAX_SUB; i++) {
        sub_t *u = &s->subs[i];
        if (!u->used || !u->cb) continue;
        size_t pl = strlen(u->prefix);
        if (pl == 0 || strncmp(key, u->prefix, pl) == 0) u->cb(u->ud, key);
    }
}

/* 绑定/取列小助手 */
static void bind_txt(sqlite3_stmt *st, int i, const char *v)
{ sqlite3_bind_text(st, i, v ? v : "", -1, SQLITE_TRANSIENT); }
static void col_txt(sqlite3_stmt *st, int i, char *dst, int cap)
{ const char *t = (const char *)sqlite3_column_text(st, i); snprintf(dst, (size_t)cap, "%s", t ? t : ""); }

static const char *DDL =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS setting("
    "  key TEXT PRIMARY KEY, ival INTEGER, sval TEXT, bval BLOB,"
    "  updated INTEGER DEFAULT (strftime('%s','now')));"
    "CREATE TABLE IF NOT EXISTS auth("
    "  id INTEGER PRIMARY KEY CHECK(id=1),"
    "  pw_algo TEXT, pw_hash BLOB, pw_salt BLOB,"
    "  fail_count INTEGER DEFAULT 0, lockout_until INTEGER DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS nop_owner("
    "  id INTEGER PRIMARY KEY CHECK(id=1),"
    "  owner_id TEXT, username TEXT, stoken TEXT, updated INTEGER);"
    /* 设备=通道(逻辑通道 1-based;仅有真实 ip+mac 才落库) */
    "CREATE TABLE IF NOT EXISTS camera("
    "  chn INTEGER PRIMARY KEY, name TEXT, enabled INTEGER DEFAULT 1,"
    "  source TEXT, protocol TEXT, kind INTEGER DEFAULT 0, backend INTEGER DEFAULT 0,"
    "  type TEXT DEFAULT 'single', dev_chn INTEGER DEFAULT 1,"
    "  ip TEXT, mac TEXT, username TEXT, password TEXT,"
    "  onvif_port INTEGER, nop_port INTEGER, service_url TEXT,"
    "  url TEXT, onvif_auto INTEGER, poe_port INTEGER,"
    "  codec INTEGER, stream INTEGER, record INTEGER,"
    "  uuid TEXT, serial TEXT, manufacturer TEXT, model TEXT, firmware TEXT,"
    "  bound INTEGER, active INTEGER, video_source_token TEXT,"
    "  updated INTEGER DEFAULT (strftime('%s','now')));"
    /* 每通道能力集(首次上线探测后写)。按 chn 独立键,不设外键——允许配置先于设备落库;
     * 删除设备时由 nvr_settings_camera_delete 手动级联清理。 */
    "CREATE TABLE IF NOT EXISTS camera_capability("
    "  chn INTEGER PRIMARY KEY,"
    "  caps_json TEXT, signal TEXT DEFAULT 'IPC', probed_at INTEGER);"
    /* 录像配置 */
    "CREATE TABLE IF NOT EXISTS record_config("
    "  chn INTEGER PRIMARY KEY,"
    "  record_on INTEGER DEFAULT 1, triggers TEXT DEFAULT 'human,face,vehicle',"
    "  stream_type TEXT DEFAULT 'main');"
    /* 持续录像排程:定时录像总开关 + 周排程 rules(JSON,秒级区间);NVR 本地处理 */
    "CREATE TABLE IF NOT EXISTS record_schedule("
    "  chn INTEGER PRIMARY KEY,"
    "  sched_on INTEGER DEFAULT 1, rules TEXT DEFAULT '');"
    /* 推送配置(免打扰单日时段 + 日期) */
    "CREATE TABLE IF NOT EXISTS push_config("
    "  chn INTEGER PRIMARY KEY,"
    "  switch_on INTEGER DEFAULT 0, dnd_enable INTEGER DEFAULT 0,"
    "  dnd_start TEXT, dnd_end TEXT, dnd_weekdays TEXT DEFAULT '1,2,3,4,5,6,7',"
    "  time_unit TEXT DEFAULT 'hour');"
    /* 每通道云存 */
    "CREATE TABLE IF NOT EXISTS cloud_channel("
    "  chn INTEGER PRIMARY KEY,"
    "  stream_type TEXT DEFAULT 'main', triggers TEXT DEFAULT 'human,face,vehicle',"
    "  enable INTEGER DEFAULT 0);"
    /* 排程规则(录像连续/事件 + 云存) */
    "CREATE TABLE IF NOT EXISTS schedule("
    "  chn INTEGER, domain TEXT, sensor TEXT DEFAULT '', rule_id TEXT,"
    "  weekdays TEXT, start_hms TEXT, end_hms TEXT,"
    "  PRIMARY KEY(chn,domain,sensor,rule_id));"
    /* 网络服务(取代死表 netif) */
    "CREATE TABLE IF NOT EXISTS local_link("
    "  id INTEGER PRIMARY KEY CHECK(id=1), network_type TEXT DEFAULT 'DHCP',"
    "  mac TEXT, ip TEXT, subnet_mask TEXT, gateway TEXT, dns1 TEXT, dns2 TEXT);"
    "CREATE TABLE IF NOT EXISTS email_alert("
    "  id INTEGER PRIMARY KEY CHECK(id=1), enable INTEGER DEFAULT 0,"
    "  receiver1 TEXT, receiver2 TEXT, receiver3 TEXT, receiver4 TEXT, receiver5 TEXT,"
    "  sender TEXT, smtp_port INTEGER, smtp_server TEXT,"
    "  username TEXT, password TEXT, use_ssl INTEGER DEFAULT 1,"
    "  title TEXT, interval INTEGER DEFAULT 600);"
    "CREATE TABLE IF NOT EXISTS ftp("
    "  id INTEGER PRIMARY KEY CHECK(id=1), enable INTEGER DEFAULT 0,"
    "  anonymous INTEGER DEFAULT 0, max_file_len INTEGER DEFAULT 100,"
    "  password TEXT, port INTEGER DEFAULT 21, remote_dir TEXT, server TEXT, username TEXT);"
    "CREATE TABLE IF NOT EXISTS ddns("
    "  idx INTEGER PRIMARY KEY, domain TEXT, enable INTEGER DEFAULT 0,"
    "  password TEXT, hostname TEXT, ddns_key TEXT, username TEXT);"
    "CREATE TABLE IF NOT EXISTS meta_kv(key TEXT PRIMARY KEY, val TEXT);";

/* ---------------- 播种（首次启动，从只读 JSON 默认值） ---------------- */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    char *b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t rd = fread(b, 1, (size_t)n, f); fclose(f); b[rd] = 0;
    return b;
}
static cJSON *load_json_dir(const char *dir, const char *name)
{
    char p[512]; snprintf(p, sizeof(p), "%s/%s", dir, name);
    char *t = read_file(p); if (!t) return NULL;
    cJSON *j = cJSON_Parse(t); free(t); return j;
}
static const char *jstr(const cJSON *o, const char *k, const char *d)
{ const cJSON *v = cJSON_GetObjectItem(o, k); return (v && cJSON_IsString(v)) ? v->valuestring : d; }
static int jint(const cJSON *o, const char *k, int d)
{ const cJSON *v = cJSON_GetObjectItem(o, k); return (v && cJSON_IsNumber(v)) ? v->valueint : d; }

static void seed_from_json(nvr_settings_t *s, const char *dir)
{
    if (!dir) return;
    exec_sql(s->db, "BEGIN;");

    /* system.json → 标量 KV */
    cJSON *sys = load_json_dir(dir, "system.json");
    if (sys) {
        cJSON *dev = cJSON_GetObjectItem(sys, "device");
        if (dev) {
            nvr_settings_set_str(s, "system.model", jstr(dev, "model", "NT12-32"));
            nvr_settings_set_str(s, "system.device_name", jstr(dev, "name", "NVR"));
            nvr_settings_set_str(s, "system.sn", jstr(dev, "sn", ""));
        }
        cJSON *tm = cJSON_GetObjectItem(sys, "time");
        if (tm) {
            nvr_settings_set_str(s, "system.timezone", jstr(tm, "timezone", "UTC"));
            nvr_settings_set_str(s, "system.ntp",      jstr(tm, "ntp", "pool.ntp.org"));
        }
        cJSON *net = cJSON_GetObjectItem(sys, "network");
        if (net) {
            cJSON *e0 = cJSON_GetObjectItem(net, "eth0");
            if (e0) {
                nvr_settings_set_int(s, "network.eth0.dhcp",
                    cJSON_IsBool(cJSON_GetObjectItem(e0, "dhcp"))
                        ? (cJSON_IsTrue(cJSON_GetObjectItem(e0, "dhcp")) ? 1 : 0) : 1);
                nvr_settings_set_str(s, "network.eth0.ip",   jstr(e0, "ip",   ""));
                nvr_settings_set_str(s, "network.eth0.mask", jstr(e0, "mask", "255.255.255.0"));
                nvr_settings_set_str(s, "network.eth0.gw",   jstr(e0, "gw",   ""));
            }
            cJSON *e1 = cJSON_GetObjectItem(net, "eth1");
            if (e1) nvr_settings_set_int(s, "network.eth1.vlan_base", jint(e1, "vlan_base", 2000));
        }
        cJSON *ch = cJSON_GetObjectItem(sys, "channels");
        if (ch) {
            nvr_settings_set_int(s, "system.capacity",    jint(ch, "capacity", 32));
            nvr_settings_set_int(s, "system.poe_ports",   jint(ch, "poe_ports", 16));
            nvr_settings_set_int(s, "system.ip_channels", jint(ch, "ip_channels", 16));
        }
        cJSON_Delete(sys);
    }

    /* storage.json → 策略 KV */
    cJSON *st = load_json_dir(dir, "storage.json");
    if (st) {
        nvr_settings_set_str(s, "storage.hdd_full", jstr(st, "hdd_full", "overwrite"));
        cJSON *enc = cJSON_GetObjectItem(st, "encryption");
        nvr_settings_set_int(s, "storage.encryption", enc ? (cJSON_IsTrue(cJSON_GetObjectItem(enc,"enable"))?1:jint(enc,"enable",1)) : 1);
        cJSON_Delete(st);
    }

    /* 云存全局开关默认关(每通道开关在 cloud_channel.enable) */
    nvr_settings_set_int(s, "cloud.switch", 0);

    /* 注:camera 行不再由 JSON 预置——仅设备真实发现(ip+mac)时落库。 */

    /* schema_version 由 nvr_settings_open 的迁移逻辑统一维护(见 NVR_SETTINGS_SCHEMA_VERSION),此处不再写。 */
    exec_sql(s->db, "INSERT OR REPLACE INTO meta_kv(key,val) VALUES('seeded','1');");
    exec_sql(s->db, "COMMIT;");
}

/* ---------------- 生命周期 ---------------- */
/* ---------------- schema 版本 + 迁移 ----------------
 * DB 持久化到设备,建好后不重建;仅固件更新(代码 SCHEMA_VERSION 升高)时对已有库做增量迁移。
 * 规则:改动 DB 结构时 → SCHEMA_VERSION +1,并在 migrate_settings 里加对应 `if (from < N)` 的
 * ALTER(仅用 ADD COLUMN 等**向后兼容**改动)。ADD COLUMN 幂等(重复列错误忽略),故对全新库
 * (DDL 已按当前结构建好)重复跑迁移也安全。 */
/* 当前结构版本 = 2(camera 取代 channel、网络服务表取代 netif、录像/推送/云存排程规范化;见文件头)。
 * 这是已发布基线,无 v1→v2 迁移(v1 未出厂)。以后改结构 → 版本 +1 + 在 migrate_settings 加 ALTER。 */
#define NVR_SETTINGS_SCHEMA_VERSION 3

static int settings_schema_version(sqlite3 *db)
{
    int v = 0; sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT val FROM meta_kv WHERE key='schema_version';", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *t = (const char *)sqlite3_column_text(st, 0);
            if (t) v = atoi(t);
        }
        sqlite3_finalize(st);
    }
    return v;
}

/* 逐版本增量迁移(from → 当前)。ALTER 用 sqlite3_exec 直接跑,重复列/已存在的错误忽略(幂等)。
 * ★ 未来加结构:提升 SCHEMA_VERSION,并在此按 `if (from < N)` 追加 ALTER,不要动 DDL 的已发布列。 */
static void migrate_settings(sqlite3 *db, int from)
{
    /* v3:多视频源 —— camera 增列 video_source_token(该通道绑定的 ONVIF VideoSourceToken)。
     * ADD COLUMN 幂等:全新库已由 DDL 建好该列,重复列错误忽略。 */
    if (from < 3)
        sqlite3_exec(db, "ALTER TABLE camera ADD COLUMN video_source_token TEXT;", 0, 0, 0);
}

int nvr_settings_open(const char *db_path, const char *json_defaults_dir, nvr_settings_t **out)
{
    if (!db_path || !out) return -1;
    nvr_settings_t *s = (nvr_settings_t *)calloc(1, sizeof(*s));
    if (!s) return -1;
    s->next_sub_id = 1;

    if (sqlite3_open(db_path, &s->db) != SQLITE_OK) {
        fprintf(stderr, "[nvr_settings] open %s failed: %s\n", db_path, sqlite3_errmsg(s->db));
        sqlite3_close(s->db); free(s); return -1;
    }
    sqlite3_busy_timeout(s->db, 3000);
    /* DDL 只 CREATE TABLE IF NOT EXISTS —— 已存在的库不动数据、不重建;缺表才补建。 */
    if (exec_sql(s->db, DDL) != 0) { nvr_settings_close(s); return -1; }
    chmod(db_path, 0600);

    /* schema 迁移:已有库版本 < 代码版本(固件更新)才做增量 ALTER;随后标记为当前版本。
     * 全新库(DDL 刚建=当前结构)版本记 0,跑一遍幂等迁移后同样标为当前版本。绝不重建/清数据。 */
    {
        int db_ver = settings_schema_version(s->db);
        if (db_ver < NVR_SETTINGS_SCHEMA_VERSION) {
            migrate_settings(s->db, db_ver);
            char sv[32]; snprintf(sv, sizeof(sv), "%d", NVR_SETTINGS_SCHEMA_VERSION);
            sqlite3_stmt *up = NULL;
            if (sqlite3_prepare_v2(s->db,
                    "INSERT OR REPLACE INTO meta_kv(key,val) VALUES('schema_version',?);",
                    -1, &up, NULL) == SQLITE_OK) {
                sqlite3_bind_text(up, 1, sv, -1, SQLITE_TRANSIENT);
                sqlite3_step(up); sqlite3_finalize(up);
            }
            if (db_ver > 0)
                fprintf(stderr, "[nvr_settings] schema 迁移 v%d → v%d\n", db_ver, NVR_SETTINGS_SCHEMA_VERSION);
        }
    }

    /* 未 seeded → 播种 */
    sqlite3_stmt *st = NULL;
    int seeded = 0;
    if (sqlite3_prepare_v2(s->db, "SELECT val FROM meta_kv WHERE key='seeded';", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) seeded = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (!seeded) seed_from_json(s, json_defaults_dir);

    *out = s;
    return 0;
}

void nvr_settings_close(nvr_settings_t *s)
{
    if (!s) return;
    if (s->db) sqlite3_close(s->db);
    free(s);
}

/* 恢复出厂:清所有数据表的行(表结构保留),**不清 meta_kv**(存 schema_version + seeded;
 * 保留 seeded → 不自动重播种 → 出厂真正空表,交向导初始化)。单事务,失败回滚。 */
int nvr_settings_factory_reset(nvr_settings_t *s)
{
    if (!s || !s->db) return -1;
    static const char *TABLES[] = {
        "setting", "auth", "nop_owner", "camera", "camera_capability",
        "record_config", "record_schedule", "push_config", "cloud_channel", "schedule",
        "local_link", "email_alert", "ftp", "ddns",
    };
    if (exec_sql(s->db, "BEGIN;") != 0) return -1;
    for (unsigned i = 0; i < sizeof(TABLES)/sizeof(TABLES[0]); i++) {
        char sql[64]; snprintf(sql, sizeof(sql), "DELETE FROM %s;", TABLES[i]);
        if (exec_sql(s->db, sql) != 0) { exec_sql(s->db, "ROLLBACK;"); return -1; }
    }
    if (exec_sql(s->db, "COMMIT;") != 0) return -1;
    notify(s, "");   /* 全量变更通知 */
    return 0;
}

/* ---------------- KV ---------------- */
int nvr_settings_get_int(nvr_settings_t *s, const char *key, int def)
{
    if (!s || !key) return def;
    sqlite3_stmt *st = NULL; int v = def;
    if (sqlite3_prepare_v2(s->db, "SELECT ival FROM setting WHERE key=?;", -1, &st, NULL) == SQLITE_OK) {
        bind_txt(st, 1, key);
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st,0) != SQLITE_NULL)
            v = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return v;
}
int nvr_settings_set_int(nvr_settings_t *s, const char *key, int val)
{
    if (!s || !key) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO setting(key,ival,updated) VALUES(?,?,strftime('%s','now')) "
        "ON CONFLICT(key) DO UPDATE SET ival=excluded.ival, updated=excluded.updated;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    bind_txt(st, 1, key);
    sqlite3_bind_int(st, 2, val);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, key); return 0;
}
int nvr_settings_get_str(nvr_settings_t *s, const char *key, char *out, int cap, const char *def)
{
    if (!out || cap <= 0) return -1;
    out[0] = 0;
    const char *src = def;
    sqlite3_stmt *st = NULL;
    char *heap = NULL;
    if (s && key && sqlite3_prepare_v2(s->db, "SELECT sval FROM setting WHERE key=?;", -1, &st, NULL) == SQLITE_OK) {
        bind_txt(st, 1, key);
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st,0) != SQLITE_NULL) {
            const char *t = (const char *)sqlite3_column_text(st, 0);
            if (t) { heap = strdup(t); src = heap; }
        }
        sqlite3_finalize(st);
    }
    if (!src) src = "";
    int n = (int)strlen(src);
    if (n > cap - 1) n = cap - 1;
    memcpy(out, src, (size_t)n); out[n] = 0;
    if (heap) free(heap);
    return n;
}
int nvr_settings_set_str(nvr_settings_t *s, const char *key, const char *val)
{
    if (!s || !key) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO setting(key,sval,updated) VALUES(?,?,strftime('%s','now')) "
        "ON CONFLICT(key) DO UPDATE SET sval=excluded.sval, updated=excluded.updated;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    bind_txt(st, 1, key);
    bind_txt(st, 2, val);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, key); return 0;
}
int nvr_settings_get_blob(nvr_settings_t *s, const char *key, void *out, int cap)
{
    if (!s || !key || !out || cap <= 0) return -1;
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db, "SELECT bval FROM setting WHERE key=?;", -1, &st, NULL) == SQLITE_OK) {
        bind_txt(st, 1, key);
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st,0) == SQLITE_BLOB) {
            n = sqlite3_column_bytes(st, 0);
            if (n > cap) n = cap;
            memcpy(out, sqlite3_column_blob(st, 0), (size_t)n);
        }
        sqlite3_finalize(st);
    }
    return n;
}
int nvr_settings_set_blob(nvr_settings_t *s, const char *key, const void *val, int len)
{
    if (!s || !key || (!val && len)) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO setting(key,bval,updated) VALUES(?,?,strftime('%s','now')) "
        "ON CONFLICT(key) DO UPDATE SET bval=excluded.bval, updated=excluded.updated;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    bind_txt(st, 1, key);
    sqlite3_bind_blob(st, 2, val, len, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, key); return 0;
}
int nvr_settings_set_many(nvr_settings_t *s, const nvr_kv_t *kv, int n)
{
    if (!s || !kv || n <= 0) return -1;
    if (exec_sql(s->db, "BEGIN;") != 0) return -1;
    for (int i = 0; i < n; i++) {
        int rc = kv[i].is_str ? nvr_settings_set_str(s, kv[i].key, kv[i].sval)
                              : nvr_settings_set_int(s, kv[i].key, kv[i].ival);
        if (rc != 0) { exec_sql(s->db, "ROLLBACK;"); return -1; }
    }
    return exec_sql(s->db, "COMMIT;");
}

/* ---------------- 订阅 ---------------- */
int nvr_settings_subscribe(nvr_settings_t *s, const char *key_prefix, nvr_settings_cb cb, void *ud)
{
    if (!s || !cb) return -1;
    for (int i = 0; i < NVR_STG_MAX_SUB; i++) {
        if (s->subs[i].used) continue;
        s->subs[i].used = 1; s->subs[i].cb = cb; s->subs[i].ud = ud;
        s->subs[i].id = s->next_sub_id++;
        snprintf(s->subs[i].prefix, sizeof(s->subs[i].prefix), "%s", key_prefix ? key_prefix : "");
        return s->subs[i].id;
    }
    return -1;
}
void nvr_settings_unsubscribe(nvr_settings_t *s, int sub_id)
{
    if (!s) return;
    for (int i = 0; i < NVR_STG_MAX_SUB; i++)
        if (s->subs[i].used && s->subs[i].id == sub_id) { s->subs[i].used = 0; s->subs[i].cb = NULL; }
}

/* ---------------- camera ---------------- */
int nvr_settings_camera_upsert(nvr_settings_t *s, const nvr_camera_row_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO camera(chn,name,enabled,source,protocol,kind,backend,type,dev_chn,"
        " ip,mac,username,password,onvif_port,nop_port,service_url,url,onvif_auto,poe_port,"
        " codec,stream,record,uuid,serial,manufacturer,model,firmware,bound,active,video_source_token,updated)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,strftime('%s','now'))"
        " ON CONFLICT(chn) DO UPDATE SET name=excluded.name,enabled=excluded.enabled,source=excluded.source,"
        "  protocol=excluded.protocol,kind=excluded.kind,backend=excluded.backend,type=excluded.type,"
        "  dev_chn=excluded.dev_chn,ip=excluded.ip,mac=excluded.mac,username=excluded.username,"
        "  password=excluded.password,onvif_port=excluded.onvif_port,nop_port=excluded.nop_port,"
        "  service_url=excluded.service_url,url=excluded.url,onvif_auto=excluded.onvif_auto,"
        "  poe_port=excluded.poe_port,codec=excluded.codec,stream=excluded.stream,record=excluded.record,"
        "  uuid=excluded.uuid,serial=excluded.serial,manufacturer=excluded.manufacturer,"
        "  model=excluded.model,firmware=excluded.firmware,bound=excluded.bound,active=excluded.active,"
        "  video_source_token=excluded.video_source_token,updated=excluded.updated;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    int c = 1;
    sqlite3_bind_int (st, c++, r->chn);
    bind_txt(st, c++, r->name);
    sqlite3_bind_int (st, c++, r->enabled);
    bind_txt(st, c++, r->source[0] ? r->source : "LAN");
    bind_txt(st, c++, r->protocol);
    sqlite3_bind_int (st, c++, r->kind);
    sqlite3_bind_int (st, c++, r->backend);
    bind_txt(st, c++, r->type[0] ? r->type : "single");
    sqlite3_bind_int (st, c++, r->dev_chn ? r->dev_chn : 1);
    bind_txt(st, c++, r->ip);
    bind_txt(st, c++, r->mac);
    bind_txt(st, c++, r->username);
    bind_txt(st, c++, r->password);
    sqlite3_bind_int (st, c++, r->onvif_port);
    sqlite3_bind_int (st, c++, r->nop_port);
    bind_txt(st, c++, r->service_url);
    bind_txt(st, c++, r->url);
    sqlite3_bind_int (st, c++, r->onvif_auto);
    sqlite3_bind_int (st, c++, r->poe_port);
    sqlite3_bind_int (st, c++, r->codec);
    sqlite3_bind_int (st, c++, r->stream);
    sqlite3_bind_int (st, c++, r->record);
    bind_txt(st, c++, r->uuid);
    bind_txt(st, c++, r->serial);
    bind_txt(st, c++, r->manufacturer);
    bind_txt(st, c++, r->model);
    bind_txt(st, c++, r->firmware);
    sqlite3_bind_int (st, c++, r->bound);
    sqlite3_bind_int (st, c++, r->active);
    bind_txt(st, c++, r->video_source_token);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "camera."); return 0;
}
static const char *CAMERA_COLS =
    "chn,name,enabled,source,protocol,kind,backend,type,dev_chn,ip,mac,username,password,"
    "onvif_port,nop_port,service_url,url,onvif_auto,poe_port,codec,stream,record,"
    "uuid,serial,manufacturer,model,firmware,bound,active,video_source_token";
static void camera_row_from_stmt(sqlite3_stmt *st, nvr_camera_row_t *r)
{
    memset(r, 0, sizeof(*r)); int c = 0;
    r->chn = sqlite3_column_int(st, c++);
    col_txt(st, c++, r->name, sizeof(r->name));
    r->enabled = sqlite3_column_int(st, c++);
    col_txt(st, c++, r->source, sizeof(r->source));
    col_txt(st, c++, r->protocol, sizeof(r->protocol));
    r->kind = sqlite3_column_int(st, c++);
    r->backend = sqlite3_column_int(st, c++);
    col_txt(st, c++, r->type, sizeof(r->type));
    r->dev_chn = sqlite3_column_int(st, c++);
    col_txt(st, c++, r->ip, sizeof(r->ip));
    col_txt(st, c++, r->mac, sizeof(r->mac));
    col_txt(st, c++, r->username, sizeof(r->username));
    col_txt(st, c++, r->password, sizeof(r->password));
    r->onvif_port = sqlite3_column_int(st, c++);
    r->nop_port = sqlite3_column_int(st, c++);
    col_txt(st, c++, r->service_url, sizeof(r->service_url));
    col_txt(st, c++, r->url, sizeof(r->url));
    r->onvif_auto = sqlite3_column_int(st, c++);
    r->poe_port = sqlite3_column_int(st, c++);
    r->codec = sqlite3_column_int(st, c++);
    r->stream = sqlite3_column_int(st, c++);
    r->record = sqlite3_column_int(st, c++);
    col_txt(st, c++, r->uuid, sizeof(r->uuid));
    col_txt(st, c++, r->serial, sizeof(r->serial));
    col_txt(st, c++, r->manufacturer, sizeof(r->manufacturer));
    col_txt(st, c++, r->model, sizeof(r->model));
    col_txt(st, c++, r->firmware, sizeof(r->firmware));
    r->bound = sqlite3_column_int(st, c++);
    r->active = sqlite3_column_int(st, c++);
    col_txt(st, c++, r->video_source_token, sizeof(r->video_source_token));
}
int nvr_settings_camera_delete(nvr_settings_t *s, int chn)
{
    if (!s) return -1;
    /* 手动级联:删设备连带清其 每通道配置/能力/排程(无外键约束)。 */
    static const char *tbls[] = { "camera", "camera_capability", "record_config",
                                  "push_config", "cloud_channel", "schedule" };
    exec_sql(s->db, "BEGIN;");
    for (size_t i = 0; i < sizeof(tbls)/sizeof(tbls[0]); i++) {
        char sql[64]; snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE chn=?;", tbls[i]);
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) { exec_sql(s->db, "ROLLBACK;"); return -1; }
        sqlite3_bind_int(st, 1, chn);
        int rc = sqlite3_step(st); sqlite3_finalize(st);
        if (rc != SQLITE_DONE) { exec_sql(s->db, "ROLLBACK;"); return -1; }
    }
    if (exec_sql(s->db, "COMMIT;") != 0) return -1;
    notify(s, "camera."); return 0;
}
int nvr_settings_camera_get(nvr_settings_t *s, int chn, nvr_camera_row_t *out)
{
    if (!s || !out) return -1;
    char sql[512]; snprintf(sql, sizeof(sql), "SELECT %s FROM camera WHERE chn=?;", CAMERA_COLS);
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, chn);
        if (sqlite3_step(st) == SQLITE_ROW) { camera_row_from_stmt(st, out); found = 0; }
        sqlite3_finalize(st);
    }
    return found;
}
int nvr_settings_camera_find_by_mac(nvr_settings_t *s, const char *mac, nvr_camera_row_t *out)
{
    if (!s || !out || !mac || !mac[0]) return -1;
    char sql[512]; snprintf(sql, sizeof(sql), "SELECT %s FROM camera WHERE mac=? COLLATE NOCASE;", CAMERA_COLS);
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) == SQLITE_OK) {
        bind_txt(st, 1, mac);
        if (sqlite3_step(st) == SQLITE_ROW) { camera_row_from_stmt(st, out); found = 0; }
        sqlite3_finalize(st);
    }
    return found;
}
int nvr_settings_camera_list(nvr_settings_t *s, nvr_camera_row_t *out, int cap)
{
    if (!s || !out || cap <= 0) return -1;
    char sql[512]; snprintf(sql, sizeof(sql), "SELECT %s FROM camera ORDER BY chn;", CAMERA_COLS);
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(st) == SQLITE_ROW && n < cap) { camera_row_from_stmt(st, &out[n]); n++; }
    sqlite3_finalize(st);
    return n;
}

/* ---------------- camera_capability ---------------- */
int nvr_settings_caps_set(nvr_settings_t *s, int chn, const char *caps_json, const char *signal)
{
    if (!s) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO camera_capability(chn,caps_json,signal,probed_at) VALUES(?,?,?,strftime('%s','now'))"
        " ON CONFLICT(chn) DO UPDATE SET caps_json=excluded.caps_json,signal=excluded.signal,"
        " probed_at=excluded.probed_at;", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, chn);
    bind_txt(st, 2, caps_json);
    bind_txt(st, 3, signal && signal[0] ? signal : "IPC");
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "camera_capability."); return 0;
}
int nvr_settings_caps_get(nvr_settings_t *s, int chn, char *caps_json_out, int cap,
                          char *signal_out, int signal_cap)
{
    if (caps_json_out && cap > 0) caps_json_out[0] = 0;
    if (signal_out && signal_cap > 0) snprintf(signal_out, (size_t)signal_cap, "IPC");
    if (!s) return 0;
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db, "SELECT caps_json,signal FROM camera_capability WHERE chn=?;",
        -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, chn);
        if (sqlite3_step(st) == SQLITE_ROW) {
            if (caps_json_out && cap > 0) { col_txt(st, 0, caps_json_out, cap); n = (int)strlen(caps_json_out); }
            if (signal_out && signal_cap > 0) col_txt(st, 1, signal_out, signal_cap);
        }
        sqlite3_finalize(st);
    }
    return n;
}

/* ---------------- record_config ---------------- */
int nvr_settings_record_set(nvr_settings_t *s, const nvr_record_cfg_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO record_config(chn,record_on,triggers,stream_type) VALUES(?,?,?,?)"
        " ON CONFLICT(chn) DO UPDATE SET record_on=excluded.record_on,triggers=excluded.triggers,"
        " stream_type=excluded.stream_type;", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, r->chn);
    sqlite3_bind_int(st, 2, r->record_on);
    bind_txt(st, 3, r->triggers);
    bind_txt(st, 4, r->stream_type[0] ? r->stream_type : "main");
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "record_config."); return 0;
}
int nvr_settings_record_get(nvr_settings_t *s, int chn, nvr_record_cfg_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out)); out->chn = chn;
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db, "SELECT record_on,triggers,stream_type FROM record_config WHERE chn=?;",
        -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, chn);
        if (sqlite3_step(st) == SQLITE_ROW) {
            out->record_on = sqlite3_column_int(st, 0);
            col_txt(st, 1, out->triggers, sizeof(out->triggers));
            col_txt(st, 2, out->stream_type, sizeof(out->stream_type));
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}

/* ---------------- record_schedule(持续录像排程) ---------------- */
int nvr_settings_rec_sched_set(nvr_settings_t *s, const nvr_rec_schedule_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO record_schedule(chn,sched_on,rules) VALUES(?,?,?)"
        " ON CONFLICT(chn) DO UPDATE SET sched_on=excluded.sched_on,rules=excluded.rules;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, r->chn);
    sqlite3_bind_int(st, 2, r->sched_on);
    bind_txt(st, 3, r->rules);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "record_schedule."); return 0;
}
int nvr_settings_rec_sched_get(nvr_settings_t *s, int chn, nvr_rec_schedule_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out)); out->chn = chn; out->sched_on = 1;   /* 默认开(定时录像总开关默认 true) */
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db, "SELECT sched_on,rules FROM record_schedule WHERE chn=?;",
        -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, chn);
        if (sqlite3_step(st) == SQLITE_ROW) {
            out->sched_on = sqlite3_column_int(st, 0);
            col_txt(st, 1, out->rules, sizeof(out->rules));
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}

/* ---------------- push_config ---------------- */
int nvr_settings_push_set(nvr_settings_t *s, const nvr_push_cfg_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO push_config(chn,switch_on,dnd_enable,dnd_start,dnd_end,dnd_weekdays,time_unit)"
        " VALUES(?,?,?,?,?,?,?)"
        " ON CONFLICT(chn) DO UPDATE SET switch_on=excluded.switch_on,dnd_enable=excluded.dnd_enable,"
        " dnd_start=excluded.dnd_start,dnd_end=excluded.dnd_end,dnd_weekdays=excluded.dnd_weekdays,"
        " time_unit=excluded.time_unit;", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, r->chn);
    sqlite3_bind_int(st, 2, r->switch_on);
    sqlite3_bind_int(st, 3, r->dnd_enable);
    bind_txt(st, 4, r->dnd_start);
    bind_txt(st, 5, r->dnd_end);
    bind_txt(st, 6, r->dnd_weekdays[0] ? r->dnd_weekdays : "1,2,3,4,5,6,7");
    bind_txt(st, 7, r->time_unit[0] ? r->time_unit : "hour");
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "push_config."); return 0;
}
int nvr_settings_push_get(nvr_settings_t *s, int chn, nvr_push_cfg_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out)); out->chn = chn;
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db,
        "SELECT switch_on,dnd_enable,dnd_start,dnd_end,dnd_weekdays,time_unit FROM push_config WHERE chn=?;",
        -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, chn);
        if (sqlite3_step(st) == SQLITE_ROW) {
            out->switch_on = sqlite3_column_int(st, 0);
            out->dnd_enable = sqlite3_column_int(st, 1);
            col_txt(st, 2, out->dnd_start, sizeof(out->dnd_start));
            col_txt(st, 3, out->dnd_end, sizeof(out->dnd_end));
            col_txt(st, 4, out->dnd_weekdays, sizeof(out->dnd_weekdays));
            col_txt(st, 5, out->time_unit, sizeof(out->time_unit));
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}

/* ---------------- cloud_channel ---------------- */
int nvr_settings_cloud_ch_upsert(nvr_settings_t *s, const nvr_cloud_ch_row_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO cloud_channel(chn,stream_type,triggers,enable) VALUES(?,?,?,?)"
        " ON CONFLICT(chn) DO UPDATE SET stream_type=excluded.stream_type,triggers=excluded.triggers,"
        " enable=excluded.enable;", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int (st, 1, r->chn);
    bind_txt(st, 2, r->stream_type[0] ? r->stream_type : "main");
    bind_txt(st, 3, r->triggers);
    sqlite3_bind_int (st, 4, r->enable);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "cloud_channel."); return 0;
}
int nvr_settings_cloud_ch_get(nvr_settings_t *s, int chn, nvr_cloud_ch_row_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out)); out->chn = chn;
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db, "SELECT stream_type,triggers,enable FROM cloud_channel WHERE chn=?;",
        -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, chn);
        if (sqlite3_step(st) == SQLITE_ROW) {
            col_txt(st, 0, out->stream_type, sizeof(out->stream_type));
            col_txt(st, 1, out->triggers, sizeof(out->triggers));
            out->enable = sqlite3_column_int(st, 2);
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}
int nvr_settings_cloud_ch_list(nvr_settings_t *s, nvr_cloud_ch_row_t *out, int cap)
{
    if (!s || !out || cap <= 0) return -1;
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db, "SELECT chn,stream_type,triggers,enable FROM cloud_channel ORDER BY chn;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(st) == SQLITE_ROW && n < cap) {
        nvr_cloud_ch_row_t *r = &out[n]; memset(r, 0, sizeof(*r));
        r->chn = sqlite3_column_int(st, 0);
        col_txt(st, 1, r->stream_type, sizeof(r->stream_type));
        col_txt(st, 2, r->triggers, sizeof(r->triggers));
        r->enable = sqlite3_column_int(st, 3);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

/* ---------------- schedule ---------------- */
int nvr_settings_schedule_replace(nvr_settings_t *s, int chn, const char *domain, const char *sensor,
                                  const nvr_schedule_row_t *rows, int n)
{
    if (!s || !domain) return -1;
    if (exec_sql(s->db, "BEGIN;") != 0) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db, "DELETE FROM schedule WHERE chn=? AND domain=? AND sensor=?;",
        -1, &st, NULL) != SQLITE_OK) { exec_sql(s->db, "ROLLBACK;"); return -1; }
    sqlite3_bind_int(st, 1, chn); bind_txt(st, 2, domain); bind_txt(st, 3, sensor ? sensor : "");
    if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); exec_sql(s->db, "ROLLBACK;"); return -1; }
    sqlite3_finalize(st);
    for (int i = 0; i < n && rows; i++) {
        const nvr_schedule_row_t *r = &rows[i];
        if (sqlite3_prepare_v2(s->db,
            "INSERT INTO schedule(chn,domain,sensor,rule_id,weekdays,start_hms,end_hms)"
            " VALUES(?,?,?,?,?,?,?);", -1, &st, NULL) != SQLITE_OK) { exec_sql(s->db, "ROLLBACK;"); return -1; }
        sqlite3_bind_int(st, 1, chn);
        bind_txt(st, 2, domain);
        bind_txt(st, 3, sensor ? sensor : "");
        bind_txt(st, 4, r->rule_id);
        bind_txt(st, 5, r->weekdays);
        bind_txt(st, 6, r->start_hms);
        bind_txt(st, 7, r->end_hms);
        int rc = sqlite3_step(st); sqlite3_finalize(st);
        if (rc != SQLITE_DONE) { exec_sql(s->db, "ROLLBACK;"); return -1; }
    }
    int rc = exec_sql(s->db, "COMMIT;");
    if (rc == 0) notify(s, "schedule.");
    return rc;
}
int nvr_settings_schedule_list(nvr_settings_t *s, int chn, const char *domain, const char *sensor,
                               nvr_schedule_row_t *out, int cap)
{
    if (!s || !out || cap <= 0 || !domain) return -1;
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db,
        "SELECT rule_id,weekdays,start_hms,end_hms FROM schedule"
        " WHERE chn=? AND domain=? AND sensor=? ORDER BY rule_id;", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, chn); bind_txt(st, 2, domain); bind_txt(st, 3, sensor ? sensor : "");
    while (sqlite3_step(st) == SQLITE_ROW && n < cap) {
        nvr_schedule_row_t *r = &out[n]; memset(r, 0, sizeof(*r));
        r->chn = chn; snprintf(r->domain, sizeof(r->domain), "%s", domain);
        snprintf(r->sensor, sizeof(r->sensor), "%s", sensor ? sensor : "");
        col_txt(st, 0, r->rule_id, sizeof(r->rule_id));
        col_txt(st, 1, r->weekdays, sizeof(r->weekdays));
        col_txt(st, 2, r->start_hms, sizeof(r->start_hms));
        col_txt(st, 3, r->end_hms, sizeof(r->end_hms));
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

/* ---------------- auth ---------------- */
int nvr_settings_auth_get(nvr_settings_t *s, nvr_auth_row_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db, "SELECT pw_algo,pw_hash,pw_salt,fail_count,lockout_until FROM auth WHERE id=1;",
        -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *algo = (const char *)sqlite3_column_text(st, 0);
            snprintf(out->pw_algo, sizeof(out->pw_algo), "%s", algo ? algo : "");
            int hl = sqlite3_column_bytes(st, 1); if (hl > (int)sizeof(out->pw_hash)) hl = sizeof(out->pw_hash);
            if (hl > 0) memcpy(out->pw_hash, sqlite3_column_blob(st, 1), (size_t)hl); out->hash_len = hl;
            int sl = sqlite3_column_bytes(st, 2); if (sl > (int)sizeof(out->pw_salt)) sl = sizeof(out->pw_salt);
            if (sl > 0) memcpy(out->pw_salt, sqlite3_column_blob(st, 2), (size_t)sl); out->salt_len = sl;
            out->fail_count = sqlite3_column_int(st, 3);
            out->lockout_until = sqlite3_column_int64(st, 4);
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}
int nvr_settings_auth_set(nvr_settings_t *s, const nvr_auth_row_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO auth(id,pw_algo,pw_hash,pw_salt,fail_count,lockout_until) VALUES(1,?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET pw_algo=excluded.pw_algo,pw_hash=excluded.pw_hash,"
        " pw_salt=excluded.pw_salt,fail_count=excluded.fail_count,lockout_until=excluded.lockout_until;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    bind_txt(st, 1, r->pw_algo);
    sqlite3_bind_blob(st, 2, r->pw_hash, r->hash_len, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st, 3, r->pw_salt, r->salt_len, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 4, r->fail_count);
    sqlite3_bind_int64(st, 5, r->lockout_until);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "auth."); return 0;
}

/* ---------------- owner ---------------- */
int nvr_settings_owner_get(nvr_settings_t *s, nvr_owner_row_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db, "SELECT owner_id,username,stoken FROM nop_owner WHERE id=1;",
        -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            col_txt(st, 0, out->owner_id, sizeof(out->owner_id));
            col_txt(st, 1, out->username, sizeof(out->username));
            col_txt(st, 2, out->stoken,   sizeof(out->stoken));
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}
int nvr_settings_owner_set(nvr_settings_t *s, const nvr_owner_row_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO nop_owner(id,owner_id,username,stoken,updated) VALUES(1,?,?,?,strftime('%s','now'))"
        " ON CONFLICT(id) DO UPDATE SET owner_id=excluded.owner_id,username=excluded.username,"
        " stoken=excluded.stoken,updated=excluded.updated;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    bind_txt(st, 1, r->owner_id);
    bind_txt(st, 2, r->username);
    bind_txt(st, 3, r->stoken);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "nop_owner."); return 0;
}

/* ---------------- local_link ---------------- */
int nvr_settings_local_link_get(nvr_settings_t *s, nvr_local_link_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out)); snprintf(out->network_type, sizeof(out->network_type), "DHCP");
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db,
        "SELECT network_type,mac,ip,subnet_mask,gateway,dns1,dns2 FROM local_link WHERE id=1;",
        -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            col_txt(st, 0, out->network_type, sizeof(out->network_type));
            col_txt(st, 1, out->mac, sizeof(out->mac));
            col_txt(st, 2, out->ip, sizeof(out->ip));
            col_txt(st, 3, out->subnet_mask, sizeof(out->subnet_mask));
            col_txt(st, 4, out->gateway, sizeof(out->gateway));
            col_txt(st, 5, out->dns1, sizeof(out->dns1));
            col_txt(st, 6, out->dns2, sizeof(out->dns2));
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}
int nvr_settings_local_link_set(nvr_settings_t *s, const nvr_local_link_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO local_link(id,network_type,mac,ip,subnet_mask,gateway,dns1,dns2)"
        " VALUES(1,?,?,?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET network_type=excluded.network_type,mac=excluded.mac,ip=excluded.ip,"
        " subnet_mask=excluded.subnet_mask,gateway=excluded.gateway,dns1=excluded.dns1,dns2=excluded.dns2;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    bind_txt(st, 1, r->network_type[0] ? r->network_type : "DHCP");
    bind_txt(st, 2, r->mac);
    bind_txt(st, 3, r->ip);
    bind_txt(st, 4, r->subnet_mask);
    bind_txt(st, 5, r->gateway);
    bind_txt(st, 6, r->dns1);
    bind_txt(st, 7, r->dns2);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "local_link."); return 0;
}

/* ---------------- email_alert ---------------- */
int nvr_settings_email_get(nvr_settings_t *s, nvr_email_cfg_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out)); out->use_ssl = 1; out->interval = 600; out->smtp_port = 465;
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db,
        "SELECT enable,receiver1,receiver2,receiver3,receiver4,receiver5,sender,smtp_port,smtp_server,"
        "username,password,use_ssl,title,interval FROM email_alert WHERE id=1;",
        -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            int c = 0;
            out->enable = sqlite3_column_int(st, c++);
            for (int i = 0; i < 5; i++) col_txt(st, c++, out->receiver[i], sizeof(out->receiver[i]));
            col_txt(st, c++, out->sender, sizeof(out->sender));
            out->smtp_port = sqlite3_column_int(st, c++);
            col_txt(st, c++, out->smtp_server, sizeof(out->smtp_server));
            col_txt(st, c++, out->username, sizeof(out->username));
            col_txt(st, c++, out->password, sizeof(out->password));
            out->use_ssl = sqlite3_column_int(st, c++);
            col_txt(st, c++, out->title, sizeof(out->title));
            out->interval = sqlite3_column_int(st, c++);
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}
int nvr_settings_email_set(nvr_settings_t *s, const nvr_email_cfg_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO email_alert(id,enable,receiver1,receiver2,receiver3,receiver4,receiver5,sender,"
        "smtp_port,smtp_server,username,password,use_ssl,title,interval)"
        " VALUES(1,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET enable=excluded.enable,receiver1=excluded.receiver1,"
        " receiver2=excluded.receiver2,receiver3=excluded.receiver3,receiver4=excluded.receiver4,"
        " receiver5=excluded.receiver5,sender=excluded.sender,smtp_port=excluded.smtp_port,"
        " smtp_server=excluded.smtp_server,username=excluded.username,password=excluded.password,"
        " use_ssl=excluded.use_ssl,title=excluded.title,interval=excluded.interval;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    int c = 1;
    sqlite3_bind_int(st, c++, r->enable);
    for (int i = 0; i < 5; i++) bind_txt(st, c++, r->receiver[i]);
    bind_txt(st, c++, r->sender);
    sqlite3_bind_int(st, c++, r->smtp_port);
    bind_txt(st, c++, r->smtp_server);
    bind_txt(st, c++, r->username);
    bind_txt(st, c++, r->password);
    sqlite3_bind_int(st, c++, r->use_ssl);
    bind_txt(st, c++, r->title);
    sqlite3_bind_int(st, c++, r->interval);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "email_alert."); return 0;
}

/* ---------------- ftp ---------------- */
int nvr_settings_ftp_get(nvr_settings_t *s, nvr_ftp_cfg_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out)); out->port = 21; out->max_file_len = 100;
    sqlite3_stmt *st = NULL; int found = -1;
    if (sqlite3_prepare_v2(s->db,
        "SELECT enable,anonymous,max_file_len,password,port,remote_dir,server,username FROM ftp WHERE id=1;",
        -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            out->enable = sqlite3_column_int(st, 0);
            out->anonymous = sqlite3_column_int(st, 1);
            out->max_file_len = sqlite3_column_int(st, 2);
            col_txt(st, 3, out->password, sizeof(out->password));
            out->port = sqlite3_column_int(st, 4);
            col_txt(st, 5, out->remote_dir, sizeof(out->remote_dir));
            col_txt(st, 6, out->server, sizeof(out->server));
            col_txt(st, 7, out->username, sizeof(out->username));
            found = 0;
        }
        sqlite3_finalize(st);
    }
    return found;
}
int nvr_settings_ftp_set(nvr_settings_t *s, const nvr_ftp_cfg_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO ftp(id,enable,anonymous,max_file_len,password,port,remote_dir,server,username)"
        " VALUES(1,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET enable=excluded.enable,anonymous=excluded.anonymous,"
        " max_file_len=excluded.max_file_len,password=excluded.password,port=excluded.port,"
        " remote_dir=excluded.remote_dir,server=excluded.server,username=excluded.username;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, r->enable);
    sqlite3_bind_int(st, 2, r->anonymous);
    sqlite3_bind_int(st, 3, r->max_file_len);
    bind_txt(st, 4, r->password);
    sqlite3_bind_int(st, 5, r->port);
    bind_txt(st, 6, r->remote_dir);
    bind_txt(st, 7, r->server);
    bind_txt(st, 8, r->username);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "ftp."); return 0;
}

/* ---------------- ddns ---------------- */
int nvr_settings_ddns_replace(nvr_settings_t *s, const nvr_ddns_row_t *rows, int n)
{
    if (!s) return -1;
    if (exec_sql(s->db, "BEGIN;") != 0) return -1;
    if (exec_sql(s->db, "DELETE FROM ddns;") != 0) { exec_sql(s->db, "ROLLBACK;"); return -1; }
    sqlite3_stmt *st = NULL;
    for (int i = 0; i < n && rows; i++) {
        const nvr_ddns_row_t *r = &rows[i];
        if (sqlite3_prepare_v2(s->db,
            "INSERT INTO ddns(idx,domain,enable,password,hostname,ddns_key,username)"
            " VALUES(?,?,?,?,?,?,?);", -1, &st, NULL) != SQLITE_OK) { exec_sql(s->db, "ROLLBACK;"); return -1; }
        sqlite3_bind_int(st, 1, r->idx ? r->idx : i);
        bind_txt(st, 2, r->domain);
        sqlite3_bind_int(st, 3, r->enable);
        bind_txt(st, 4, r->password);
        bind_txt(st, 5, r->hostname);
        bind_txt(st, 6, r->ddns_key);
        bind_txt(st, 7, r->username);
        int rc = sqlite3_step(st); sqlite3_finalize(st);
        if (rc != SQLITE_DONE) { exec_sql(s->db, "ROLLBACK;"); return -1; }
    }
    int rc = exec_sql(s->db, "COMMIT;");
    if (rc == 0) notify(s, "ddns.");
    return rc;
}
int nvr_settings_ddns_list(nvr_settings_t *s, nvr_ddns_row_t *out, int cap)
{
    if (!s || !out || cap <= 0) return -1;
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db,
        "SELECT idx,domain,enable,password,hostname,ddns_key,username FROM ddns ORDER BY idx;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(st) == SQLITE_ROW && n < cap) {
        nvr_ddns_row_t *r = &out[n]; memset(r, 0, sizeof(*r));
        r->idx = sqlite3_column_int(st, 0);
        col_txt(st, 1, r->domain, sizeof(r->domain));
        r->enable = sqlite3_column_int(st, 2);
        col_txt(st, 3, r->password, sizeof(r->password));
        col_txt(st, 4, r->hostname, sizeof(r->hostname));
        col_txt(st, 5, r->ddns_key, sizeof(r->ddns_key));
        col_txt(st, 6, r->username, sizeof(r->username));
        n++;
    }
    sqlite3_finalize(st);
    return n;
}
