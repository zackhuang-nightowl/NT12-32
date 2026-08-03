/***************************************************************************************
 *  storage_mgr.c — 顶层管理器：生命周期 + 状态机推进 + 告警回调 + 周期维护
 *
 *  app/record_sched 用法：
 *      nvr_storage_init(&cfg, &s);
 *      nvr_storage_scan(s);                       // 发现+识别
 *      // 对 BLANK/FOREIGN 盘按需 nvr_storage_format(...)
 *      nvr_storage_assemble(s, &group);           // 组装 → 交 recorder
 *      每 5s: nvr_storage_tick(s);                // 健康/满盘/热插拔
 ***************************************************************************************/
#include "storage_internal.h"
#include <stdlib.h>
#include <string.h>

void stg_emit(struct nvr_storage *s, nvr_stg_evt_t e, const nvr_disk_t *d)
{
    if (s && s->cfg.cb) s->cfg.cb(e, d, s->cfg.cb_user);
}

rsdk_err_t nvr_storage_init(const nvr_storage_cfg_t *cfg, nvr_storage_t **out)
{
    if (!cfg || !out) return RSDK_E_PARAM;
    nvr_storage_t *s = calloc(1, sizeof(*s));
    if (!s) return RSDK_E_NOSPACE;
    s->cfg = *cfg;
    s->hotplug_fd = -1;
    stg_hotplug_open(s);              /* 失败不致命：退化为纯轮询 */
    *out = s;
    return RSDK_OK;
}

void nvr_storage_deinit(nvr_storage_t *s)
{
    if (!s) return;
    if (s->group) rsdk_group_close(s->group);
    stg_hotplug_close(s);
    free(s);
}

/* 周期维护：热插拔事件 + 健康轮询 + 满盘检测 */
void nvr_storage_tick(nvr_storage_t *s)
{
    if (!s) return;

    /* 1) 热插拔（有新盘/掉盘 → 事件已由回调抛出；add 由上层决定是否重扫/格式化） */
    stg_hotplug_poll(s);

    /* 2) 健康轮询 */
    for (int i = 0; i < s->ndisks; i++) {
        nvr_disk_t *dk = &s->disks[i];
        if (dk->state == NVR_DISK_OFFLINE) continue;
        nvr_disk_health_t h;
        nvr_storage_read_health(dk->path, &h);
        int was_ok = dk->health.ok;
        dk->health = h;
        if (was_ok && !h.ok && dk->state == NVR_DISK_ACTIVE) {
            dk->state = NVR_DISK_FAILED;
            stg_emit(s, NVR_STG_EVT_DISK_FAILED, dk);   /* → balance 会自动跳过本盘 */
        }
    }

    /* 3) 满盘检测：任一 ACTIVE 盘已绕盘一圈（覆盖模式）→ 告警；stop 模式上层停录 */
    if (s->group) {
        int n = rsdk_group_count(s->group);
        for (int i = 0; i < n; i++) {
            rsdk_dev_t *dev = rsdk_group_dev(s->group, i);
            if (dev && rsdk_dev_is_wrapped(dev)) {
                stg_emit(s, NVR_STG_EVT_FULL, NULL);
                break;
            }
        }
    }
}

/* 供 recorder 均衡选盘：按盘组内序号查健康 */
int nvr_storage_disk_healthy(nvr_storage_t *s, uint16_t group_index)
{
    if (!s) return 0;
    for (int i = 0; i < s->ndisks; i++)
        if (s->disks[i].group_disk_index == group_index)
            return s->disks[i].health.ok && s->disks[i].state == NVR_DISK_ACTIVE;
    return 0;
}
