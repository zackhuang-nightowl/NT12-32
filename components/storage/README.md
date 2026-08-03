# ⑥ 文件存储 (storage) — 裸盘方案 ✅ 已定型

**已选路线 A：裸盘直写（no-FS）**。盘上机制全在 `components/recorder`(librsdk)；
本层只做 recorder 留白的**盘运维管理**。完整设计见 [DESIGN_rawdisk.md](DESIGN_rawdisk.md)。

## 边界（不重造）

| recorder 负责 | storage 负责 |
|--------------|-------------|
| 格式化原语/SuperBlock/chunk 环形分配/索引/录制/回放/覆盖回收/多盘均衡/加密 | **发现·识别·格式化编排·盘组装配·SMART健康·热插拔·防误挂载·满盘告警** |

> storage **产出并监护** `rsdk_group_t*` 交给 recorder；它是"盘管理器"，不是"存储引擎"。

## 落地文件（均通过 `-fsyntax-only` 对真实 recorder 头校验）

| 文件 | 内容 | 状态 |
|------|------|------|
| `include/nvr_storage.h` | 对外 API（disk_mgr / health / hotplug / guard） | ✅ |
| `src/storage_mgr.c` | 生命周期 + 状态机 + 告警回调 + `tick` 周期维护 | ✅ 骨架可用 |
| `src/storage_disk.c` | 发现(/sys/block)·识别(读SB魔数)·格式化编排(rsdk_format)·盘组装配(rsdk_group_open) | ✅ 复用 recorder 原语 |
| `src/storage_health.c` | 只读检测(sysfs 可靠) + SMART 明细(待接平台) | ⚠️ SMART 采集 TODO |
| `src/storage_hotplug.c` | netlink uevent 监听 → add/remove 上下线 | ✅ netlink 实现 |
| `src/storage_guard.c` | 扫 /proc/mounts 防误挂载 + udev 规则 | ✅ |
| `nvr-rawdisk.rules` | udev 规则模板（部署期投放） | ✅ |

## app 侧用法

```c
nvr_storage_cfg_t cfg = { .device_sn=sn, .want_encryption=1,
                          .hdd_full=RSDK_HDDFULL_OVERWRITE, .cb=on_stg_evt };
nvr_storage_t *s; nvr_storage_init(&cfg, &s);
nvr_storage_scan(s);                         /* 发现+识别 → 盘表 */
/* 对 BLANK/FOREIGN 盘按需： nvr_storage_format(s, "/dev/sda", 0, 1); */
rsdk_group_t *g; nvr_storage_assemble(s, &g);/* 组装 → 交 recorder */
rsdk_rec_open_group(g, ch, RSDK_REC_CONTINUOUS, &w);   /* recorder 照常录像 */
/* 主循环每 5s： */ nvr_storage_tick(s);      /* 健康/满盘/热插拔 */
```

## 剩余 TODO（接平台/联调时补）

- [ ] **SMART 明细采集**：`storage_health.c` 三选一（libatasmart / ATA passthrough ioctl / smartd 导出），取决于 na51090 BSP。当前只读检测已可靠。
- [ ] **FOREIGN 精判**：`storage_disk.c` 现保守当 BLANK；可接 libblkid 精判 ext/ntfs 后再要求用户确认。
- [ ] **group_index 落盘**：确认 `rsdk_format_opt_t` 是否已带组序号字段；多盘编号约定与装配对齐。
- [ ] **健康反馈균衡**：把 `nvr_storage_disk_healthy()` 接进 `rsdk_balance_pick` 的健康输入（设计 §238）。
