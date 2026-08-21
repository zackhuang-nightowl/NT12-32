/***************************************************************************************
 *  nvr_storage.h — ⑥ 文件存储（裸盘方案）盘运维管理层
 *
 *  职责：发现/识别/格式化编排/盘组装配/健康监控/热插拔/防误挂载。
 *  边界：盘上机制(格式化原语/chunk分配/索引/录制/回放/覆盖/均衡)全在 components/recorder。
 *        本层产出并监护 rsdk_group_t*，交给 recorder 使用。见 DESIGN_rawdisk.md。
 ***************************************************************************************/
#ifndef NVR_STORAGE_H
#define NVR_STORAGE_H

#include <stdint.h>
#include "rsdk.h"          /* rsdk_group_t / rsdk_dev_t / rsdk_format_opt_t / rsdk_err_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 盘状态 ---------------- */
typedef enum {
    NVR_DISK_UNKNOWN = 0,
    NVR_DISK_BLANK,        /* 空盘（无 RSDK 魔数，也无已知 FS） */
    NVR_DISK_FOREIGN,      /* 外来盘（有其它 FS 魔数），格式化需用户确认 */
    NVR_DISK_OURS,         /* 本组 RSDK 盘（group_uuid 匹配） */
    NVR_DISK_OURS_OTHER,   /* RSDK 盘但属于别的盘组 */
    NVR_DISK_ACTIVE,       /* 已纳入盘组、可读写 */
    NVR_DISK_FAILED,       /* SMART 坏 / 只读 / IO 错 */
    NVR_DISK_OFFLINE,      /* 已掉线（热拔） */
} nvr_disk_state_t;

/* ---------------- 健康 ---------------- */
typedef struct {
    int      ok;                 /* 1=健康(可写)  0=不健康 */
    int      read_only;          /* 内核标记只读 */
    int      temp_celsius;       /* 温度; <0=未知 */
    uint32_t reallocated;        /* SMART 5  重映射扇区数 */
    uint32_t pending;            /* SMART 197 待映射扇区 */
    uint32_t power_on_hours;     /* SMART 9 */
    uint64_t io_errors;          /* 累计 IO 错误 */
} nvr_disk_health_t;

/* ---------------- 单盘信息 ---------------- */
typedef struct {
    char             path[32];       /* "/dev/sda" */
    char             model[64];
    char             serial[64];
    uint64_t         capacity_bytes;
    nvr_disk_state_t state;
    /* 若为 RSDK 盘: 从 SuperBlock 读到的组信息 */
    uint8_t          group_uuid[16];
    uint16_t         group_disk_index;
    uint16_t         group_disk_count;
    uint32_t         feature_mask;
    uint8_t          enc_algo;
    nvr_disk_health_t health;
} nvr_disk_t;

/* ---------------- 事件回调（→ app/event → nop/tutk 推送） ---------------- */
typedef enum {
    NVR_STG_EVT_DISK_ADDED,      /* 热插入 */
    NVR_STG_EVT_DISK_REMOVED,    /* 热拔出/掉线 */
    NVR_STG_EVT_DISK_FAILED,     /* SMART/只读/IO 故障 */
    NVR_STG_EVT_FULL,            /* 盘组满（stop 模式将停录；overwrite 仅告警） */
    NVR_STG_EVT_NEED_FORMAT,     /* 发现空盘/外来盘，等待格式化决策 */
} nvr_stg_evt_t;

typedef void (*nvr_stg_cb)(nvr_stg_evt_t evt, const nvr_disk_t *disk, void *user);

/* ---------------- 管理器 ---------------- */
typedef struct nvr_storage nvr_storage_t;

typedef struct {
    const char *device_sn;        /* 派生 KEK；组内所有盘一致（NULL=RSDK_DEFAULT_SN） */
    uint8_t     want_encryption;  /* 1=格式化时开 AES-256-CTR */
    uint8_t     hdd_full;         /* RSDK_HDDFULL_OVERWRITE / _STOP */
    uint8_t     want_metadata;    /* 1=启用元数据配额 */
    nvr_stg_cb  cb;               /* 事件回调 */
    void       *cb_user;
} nvr_storage_cfg_t;

/* 生命周期 */
rsdk_err_t nvr_storage_init  (const nvr_storage_cfg_t *cfg, nvr_storage_t **out);
void       nvr_storage_deinit(nvr_storage_t *s);

/* 1) 发现 + 识别：扫描 /dev/sd*，填盘表（不改盘） */
int        nvr_storage_scan  (nvr_storage_t *s);                 /* 返回发现盘数 */
int        nvr_storage_list  (nvr_storage_t *s, nvr_disk_t *out, int cap);

/* 2) 格式化编排：对指定盘（BLANK/FOREIGN/或强制重建）执行 rsdk_format 并校验 */
rsdk_err_t nvr_storage_format(nvr_storage_t *s, const char *path,
                              uint16_t group_index, uint16_t group_count);

/* 3) 盘组装配：把所有 OURS 盘按 group_index 有序组装为 rsdk_group（缺盘会报告） */
rsdk_err_t nvr_storage_assemble(nvr_storage_t *s, rsdk_group_t **group_out);
rsdk_group_t *nvr_storage_group(nvr_storage_t *s);              /* 取已装配盘组 */

/* 热插拔:把已扫描到、尚未入组的本系统盘(OURS)原地并入现有盘组(group 指针不变)。返回新并入盘数。 */
int  nvr_storage_integrate(nvr_storage_t *s, rsdk_group_t *group);
/* 掉盘:把该 path 盘在组内标记不健康(balance 跳过)并置 OFFLINE。 */
void nvr_storage_disk_offline(nvr_storage_t *s, rsdk_group_t *group, const char *path);

/* 4) 周期维护：健康轮询 + 满盘检测 + 处理热插拔事件（app 定时调，如每 5s） */
void       nvr_storage_tick  (nvr_storage_t *s);

/* 供 recorder 均衡选盘查询：该盘当前是否健康可写（rsdk_balance 的"健康"输入） */
int        nvr_storage_disk_healthy(nvr_storage_t *s, uint16_t group_index);

/* ---------------- 子能力（也可单独用） ---------------- */
/* 身份识别：读 /dev/sdX 首扇区判定状态（不改盘） */
nvr_disk_state_t nvr_storage_identify(const char *path, nvr_disk_t *info,
                                      const uint8_t *want_group_uuid /*可NULL*/);
/* 健康采集：SMART/只读/温度 */
int  nvr_storage_read_health(const char *path, nvr_disk_health_t *out);
/* 防误挂载：检查录像盘是否被内核挂载；返回被挂载的盘数 */
int  nvr_storage_guard_check(nvr_storage_t *s);

#ifdef __cplusplus
}
#endif
#endif /* NVR_STORAGE_H */
