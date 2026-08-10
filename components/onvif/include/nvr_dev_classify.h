/***************************************************************************************
 *  nvr_dev_classify.h — 由 ONVIF-Discovery 的 Scopes 判定设备三分类（计划 §B1.1）
 *
 *  三类（见 docs/BIND_IPC_FLOW.md §1）：
 *    NOP      : Scopes 含 nopVersion/x.y 或 A1C2B3（NightOwl NOP 私有协议）→ 控制面透传 NOP
 *    NOPONVIF : Scopes 含 nopOnvif/x.y（NightOwl，ONVIF digest + 激活 + 算法密码）→ ONVIF 后端
 *    ONVIF    : 无以上标识（通用第三方）→ ONVIF 后端
 *  （电池机较老，本期忽略，无法仅由 Scopes 判定）
 *
 *  纯 C，不依赖 happytime/nop，便于单测。
 ***************************************************************************************/
#ifndef NVR_DEV_CLASSIFY_H
#define NVR_DEV_CLASSIFY_H

#ifdef __cplusplus
extern "C" {
#endif

/* 与 nvr_settings 的 channel.kind 一致：0=NOP 1=NOPONVIF 2=ONVIF */
typedef enum {
    NVR_DEV_KIND_NOP      = 0,
    NVR_DEV_KIND_NOPONVIF = 1,
    NVR_DEV_KIND_ONVIF    = 2
} nvr_dev_kind_t;

/* 后端：NOP 透传 vs ONVIF 翻译 */
typedef enum { NVR_BACKEND_NOP = 0, NVR_BACKEND_ONVIF = 1 } nvr_backend_t;

typedef struct {
    nvr_dev_kind_t kind;
    nvr_backend_t  backend;         /* NOP→NOP 透传；NOPONVIF/ONVIF→ONVIF 翻译 */
    char           mac[24];         /* 从 scopes 的 /mac/ 提取（可空） */
    char           serial[64];      /* 设备 SN：从 scopes 的 /serial/ 或 /sn/ 提取（激活用，可空） */
    char           model[64];       /* 型号：从 scopes 的 /hardware/ 提取（ONVIF 标准；可空） */
    char           name[64];        /* 设备名：从 scopes 的 /name/ 提取（ONVIF 标准；可空） */
    int            is_nightowl;     /* manufacturer=NightOwl 或 MAC 前缀 54:2b:57 */
    int            nop_version_x;   /* nopVersion 主版本（无则 0） */
    int            active;          /* scopes 含 nopState/active */
    int            bound;           /* scopes 含 /bound（已被某 NVR 绑定） */
} nvr_dev_class_t;

/* 解析一段 Scopes 字符串（空格或换行分隔的多个 onvif:// scope）→ 分类结果。
 * scopes 可为多行/多段；大小写不敏感匹配关键标识。返回 0 成功。 */
int nvr_dev_classify(const char *scopes, nvr_dev_class_t *out);

/* 便捷：kind→后端 */
nvr_backend_t nvr_dev_backend_of(nvr_dev_kind_t kind);

/* kind 名称（日志/调试） */
const char *nvr_dev_kind_name(nvr_dev_kind_t kind);

#ifdef __cplusplus
}
#endif
#endif /* NVR_DEV_CLASSIFY_H */
