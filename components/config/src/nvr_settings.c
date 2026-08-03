/***************************************************************************************
 *  nvr_settings.c — 运行期可写配置库（SQLite 实现）。见 nvr_settings.h / 计划 §B7。
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
    "CREATE TABLE IF NOT EXISTS channel("
    "  chn INTEGER PRIMARY KEY, name TEXT, url TEXT, user TEXT, pass TEXT,"
    "  codec INTEGER, stream INTEGER, record INTEGER,"
    "  onvif_auto INTEGER, onvif_ip TEXT, onvif_port INTEGER, poe_port INTEGER,"
    "  enabled INTEGER DEFAULT 1, kind INTEGER DEFAULT 0, source TEXT DEFAULT 'json');"
    "CREATE TABLE IF NOT EXISTS cloud_channel("
    "  chn INTEGER PRIMARY KEY, stream_type TEXT DEFAULT 'main',"
    "  triggers TEXT DEFAULT 'human,face,vehicle');"
    "CREATE TABLE IF NOT EXISTS rec_schedule("
    "  chn INTEGER, dow INTEGER, start_min INTEGER, end_min INTEGER, mode TEXT,"
    "  PRIMARY KEY(chn,dow,start_min));"
    "CREATE TABLE IF NOT EXISTS rec_triggers("
    "  chn INTEGER PRIMARY KEY, triggers TEXT);"
    "CREATE TABLE IF NOT EXISTS netif("
    "  name TEXT PRIMARY KEY, dhcp INTEGER, ip TEXT, mask TEXT, gw TEXT, dns1 TEXT, dns2 TEXT);"
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
        /* 网络：eth0 DHCP(默认)/静态 + eth1 PoE VLAN 基值 → 供 nvr_net_apply 落地 */
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

    /* 云存默认关；无 owner */
    nvr_settings_set_int(s, "cloud.switch", 0);

    /* channels.json 的 devices 里的显式 IP 源播种为 channel 行（PoE 口留给运行期发现绑定） */
    cJSON *cj = load_json_dir(dir, "channels.json");
    if (cj) {
        cJSON *devs = cJSON_GetObjectItem(cj, "devices");
        cJSON *dev;
        if (cJSON_IsArray(devs)) cJSON_ArrayForEach(dev, devs) {
            cJSON *sources = cJSON_GetObjectItem(dev, "sources");
            if (!cJSON_IsArray(sources)) continue;
            cJSON *src;
            cJSON_ArrayForEach(src, sources) {
                int chn = jint(src, "channel", -1);
                if (chn < 0) continue;
                nvr_chan_row_t r; memset(&r, 0, sizeof(r));
                r.chn = chn; r.enabled = 1; r.record = 1; r.kind = 2 /*ONVIF*/;
                snprintf(r.name, sizeof(r.name), "%s", jstr(src, "name", "Camera"));
                snprintf(r.url,  sizeof(r.url),  "%s", jstr(src, "url", ""));
                snprintf(r.source, sizeof(r.source), "json");
                cJSON *onvif = cJSON_GetObjectItem(dev, "onvif");
                if (onvif) { snprintf(r.onvif_ip, sizeof(r.onvif_ip), "%s", jstr(onvif, "ip", ""));
                             r.onvif_port = jint(onvif, "port", 80); r.onvif_auto = 1; }
                nvr_settings_channel_upsert(s, &r);
            }
        }
        cJSON_Delete(cj);
    }

    exec_sql(s->db, "INSERT OR REPLACE INTO meta_kv(key,val) VALUES('schema_version','1');");
    exec_sql(s->db, "INSERT OR REPLACE INTO meta_kv(key,val) VALUES('seeded','1');");
    exec_sql(s->db, "COMMIT;");
}

/* ---------------- 生命周期 ---------------- */
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
    if (exec_sql(s->db, DDL) != 0) { nvr_settings_close(s); return -1; }
    chmod(db_path, 0600);

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

/* ---------------- KV ---------------- */
int nvr_settings_get_int(nvr_settings_t *s, const char *key, int def)
{
    if (!s || !key) return def;
    sqlite3_stmt *st = NULL; int v = def;
    if (sqlite3_prepare_v2(s->db, "SELECT ival FROM setting WHERE key=?;", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
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
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
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
        sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
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
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, val ? val : "", -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, key); return 0;
}
int nvr_settings_get_blob(nvr_settings_t *s, const char *key, void *out, int cap)
{
    if (!s || !key || !out || cap <= 0) return -1;
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db, "SELECT bval FROM setting WHERE key=?;", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
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
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
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

/* ---------------- channel ---------------- */
int nvr_settings_channel_upsert(nvr_settings_t *s, const nvr_chan_row_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO channel(chn,name,url,user,pass,codec,stream,record,onvif_auto,onvif_ip,onvif_port,poe_port,enabled,kind,source)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(chn) DO UPDATE SET name=excluded.name,url=excluded.url,user=excluded.user,pass=excluded.pass,"
        "  codec=excluded.codec,stream=excluded.stream,record=excluded.record,onvif_auto=excluded.onvif_auto,"
        "  onvif_ip=excluded.onvif_ip,onvif_port=excluded.onvif_port,poe_port=excluded.poe_port,"
        "  enabled=excluded.enabled,kind=excluded.kind,source=excluded.source;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    int c = 1;
    sqlite3_bind_int (st, c++, r->chn);
    sqlite3_bind_text(st, c++, r->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, c++, r->url,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, c++, r->user, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, c++, r->pass, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, c++, r->codec);
    sqlite3_bind_int (st, c++, r->stream);
    sqlite3_bind_int (st, c++, r->record);
    sqlite3_bind_int (st, c++, r->onvif_auto);
    sqlite3_bind_text(st, c++, r->onvif_ip, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, c++, r->onvif_port);
    sqlite3_bind_int (st, c++, r->poe_port);
    sqlite3_bind_int (st, c++, r->enabled);
    sqlite3_bind_int (st, c++, r->kind);
    sqlite3_bind_text(st, c++, r->source[0] ? r->source : "user", -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "channel."); return 0;
}
int nvr_settings_channel_delete(nvr_settings_t *s, int chn)
{
    if (!s) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db, "DELETE FROM channel WHERE chn=?;", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, chn);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "channel."); return 0;
}
int nvr_settings_channel_list(nvr_settings_t *s, nvr_chan_row_t *out, int cap)
{
    if (!s || !out || cap <= 0) return -1;
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db,
        "SELECT chn,name,url,user,pass,codec,stream,record,onvif_auto,onvif_ip,onvif_port,poe_port,enabled,kind,source"
        " FROM channel ORDER BY chn;", -1, &st, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(st) == SQLITE_ROW && n < cap) {
        nvr_chan_row_t *r = &out[n]; memset(r, 0, sizeof(*r)); int c = 0;
        r->chn = sqlite3_column_int(st, c++);
        #define CPY(field) do{ const char *t=(const char*)sqlite3_column_text(st,c++); snprintf(r->field,sizeof(r->field),"%s",t?t:""); }while(0)
        CPY(name); CPY(url); CPY(user); CPY(pass);
        r->codec = sqlite3_column_int(st, c++);
        r->stream = sqlite3_column_int(st, c++);
        r->record = sqlite3_column_int(st, c++);
        r->onvif_auto = sqlite3_column_int(st, c++);
        CPY(onvif_ip);
        r->onvif_port = sqlite3_column_int(st, c++);
        r->poe_port = sqlite3_column_int(st, c++);
        r->enabled = sqlite3_column_int(st, c++);
        r->kind = sqlite3_column_int(st, c++);
        CPY(source);
        #undef CPY
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
    sqlite3_bind_text(st, 1, r->pw_algo, -1, SQLITE_TRANSIENT);
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
            const char *a = (const char*)sqlite3_column_text(st,0);
            const char *b = (const char*)sqlite3_column_text(st,1);
            const char *c = (const char*)sqlite3_column_text(st,2);
            snprintf(out->owner_id, sizeof(out->owner_id), "%s", a?a:"");
            snprintf(out->username, sizeof(out->username), "%s", b?b:"");
            snprintf(out->stoken,   sizeof(out->stoken),   "%s", c?c:"");
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
    sqlite3_bind_text(st, 1, r->owner_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, r->username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, r->stoken,   -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "nop_owner."); return 0;
}

/* ---------------- cloud_channel ---------------- */
int nvr_settings_cloud_ch_upsert(nvr_settings_t *s, const nvr_cloud_ch_row_t *r)
{
    if (!s || !r) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db,
        "INSERT INTO cloud_channel(chn,stream_type,triggers) VALUES(?,?,?)"
        " ON CONFLICT(chn) DO UPDATE SET stream_type=excluded.stream_type,triggers=excluded.triggers;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int (st, 1, r->chn);
    sqlite3_bind_text(st, 2, r->stream_type[0] ? r->stream_type : "main", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, r->triggers, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    notify(s, "cloud_channel."); return 0;
}
int nvr_settings_cloud_ch_list(nvr_settings_t *s, nvr_cloud_ch_row_t *out, int cap)
{
    if (!s || !out || cap <= 0) return -1;
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(s->db, "SELECT chn,stream_type,triggers FROM cloud_channel ORDER BY chn;",
        -1, &st, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(st) == SQLITE_ROW && n < cap) {
        nvr_cloud_ch_row_t *r = &out[n]; memset(r, 0, sizeof(*r));
        r->chn = sqlite3_column_int(st, 0);
        const char *stp = (const char*)sqlite3_column_text(st, 1);
        const char *tg  = (const char*)sqlite3_column_text(st, 2);
        snprintf(r->stream_type, sizeof(r->stream_type), "%s", stp?stp:"main");
        snprintf(r->triggers, sizeof(r->triggers), "%s", tg?tg:"");
        n++;
    }
    sqlite3_finalize(st);
    return n;
}
