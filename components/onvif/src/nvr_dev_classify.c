/* nvr_dev_classify — Scopes → 三分类。见 nvr_dev_classify.h / docs/BIND_IPC_FLOW.md §1。 */
#include "nvr_dev_classify.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* 大小写不敏感子串查找 */
static const char *ci_strstr(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return NULL;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return p;
    }
    return NULL;
}

/* 从 ".../mac/54:2b:57:70:98:10" 提取 MAC 到 out */
static void extract_mac(const char *scopes, char *out, size_t cap)
{
    out[0] = 0;
    const char *m = ci_strstr(scopes, "/mac/");
    if (!m) return;
    m += 5;
    size_t i = 0;
    while (m[i] && i < cap - 1 &&
           (isxdigit((unsigned char)m[i]) || m[i] == ':' || m[i] == '-')) {
        out[i] = m[i]; i++;
    }
    out[i] = 0;
}

/* 从 scopes 的 /serial/ 或 /sn/ 后取设备 SN（到分隔符/空白为止） */
static void extract_serial(const char *scopes, char *out, size_t cap)
{
    out[0] = 0;
    const char *m = ci_strstr(scopes, "/serial/");
    size_t skip = 8;
    if (!m) { m = ci_strstr(scopes, "/sn/"); skip = 4; }
    if (!m) return;
    m += skip;
    size_t i = 0;
    while (m[i] && i < cap - 1 && m[i] != ' ' && m[i] != '\t' &&
           m[i] != '\r' && m[i] != '\n' && m[i] != '/') {
        out[i] = m[i]; i++;
    }
    out[i] = 0;
}

nvr_backend_t nvr_dev_backend_of(nvr_dev_kind_t kind)
{
    return (kind == NVR_DEV_KIND_NOP) ? NVR_BACKEND_NOP : NVR_BACKEND_ONVIF;
}

const char *nvr_dev_kind_name(nvr_dev_kind_t kind)
{
    switch (kind) {
        case NVR_DEV_KIND_NOP:      return "NOP";
        case NVR_DEV_KIND_NOPONVIF: return "NOPONVIF";
        case NVR_DEV_KIND_ONVIF:    return "ONVIF";
        default:                    return "?";
    }
}

int nvr_dev_classify(const char *scopes, nvr_dev_class_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!scopes) scopes = "";

    /* MAC + SN + NightOwl 判定 */
    extract_mac(scopes, out->mac, sizeof(out->mac));
    extract_serial(scopes, out->serial, sizeof(out->serial));
    int mac_owl = (out->mac[0] && ci_strstr(out->mac, "54:2b:57") == out->mac);
    int name_owl = (ci_strstr(scopes, "nightowl") != NULL) || (ci_strstr(scopes, "night_owl") != NULL);
    out->is_nightowl = mac_owl || name_owl;

    /* nopVersion 主版本 */
    const char *nv = ci_strstr(scopes, "nopversion/");
    if (nv) { int x = 0; if (sscanf(nv + strlen("nopversion/"), "%d", &x) == 1) out->nop_version_x = x; }

    int has_nopver  = (nv != NULL);
    int has_a1c2b3  = (ci_strstr(scopes, "a1c2b3") != NULL);
    int has_noponvif= (ci_strstr(scopes, "noponvif") != NULL);

    out->active = (ci_strstr(scopes, "nopstate/active") != NULL);
    out->bound  = (ci_strstr(scopes, "/bound") != NULL);

    /* 分类（标识互斥；nopVersion/A1C2B3 → NOP；nopOnvif → NOPONVIF；否则 ONVIF） */
    if (has_nopver || has_a1c2b3)      out->kind = NVR_DEV_KIND_NOP;
    else if (has_noponvif)             out->kind = NVR_DEV_KIND_NOPONVIF;
    else                               out->kind = NVR_DEV_KIND_ONVIF;

    out->backend = nvr_dev_backend_of(out->kind);
    return 0;
}
