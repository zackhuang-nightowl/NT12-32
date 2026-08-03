# ⑥ 文件存储 — 裸盘方案设计 (raw-disk / no-FS)

选定**路线 A：裸盘直写**。媒体/索引/元数据的**盘上机制**由 `components/recorder`(librsdk) 全包，
本层只做 recorder **明确留白**的「盘运维管理」。二者边界如下。

## 1. 职责边界（关键：不重造轮子）

| 能力 | 归属 | 说明 |
|------|------|------|
| 盘上格式（SuperBlock/SysTab/位图） | **recorder** | 已冻结 v1，见 `../recorder/盘上格式冻结_v1.md` |
| chunk 环形分配 / 写指针 / 覆盖回收 | **recorder** | `rsdk_dev_alloc_chunk` / `is_wrapped` |
| 索引 / 录制 / 回放 / 导出 / 加密 / 元数据 | **recorder** | rec/play/index/backup/crypto/meta |
| 多盘负载均衡 / 跨盘归并回放 | **recorder** | `rsdk_balance_pick` / `rsdk_group_*` |
| **盘发现 / 容量 / 型号 / 序列号** | **storage** ✅ | recorder 未做（`rawdev` 只认路径） |
| **身份识别**（本组盘 / 空盘 / 外来盘） | **storage** ✅ | 读 SuperBlock magic 判定 |
| **格式化编排**（决策参数→调 rsdk_format→校验） | **storage** ✅ | recorder 只提供 `rsdk_format` 原语 |
| **盘组装配**（按 group_uuid/index 组装 rsdk_group） | **storage** ✅ | recorder 需要"有序且齐全"的盘路径 |
| **SMART / 健康 / 只读检测 / 温度 / 坏道** | **storage** ✅ | 设计 §238/§383 点名待做 |
| **热插拔**（SATA 上/下线→在线集合增删） | **storage** ✅ | 设计 §243 点名待做 |
| **防 OS 误挂载 / 误 fsck / 误格式化** | **storage** ✅ | 设计 §72：裸盘必须挡住内核自动挂载 |
| **满盘 / 掉盘告警**（→ event/nop 推送） | **storage** ✅ | 类原机 `HDDFullAct` |

> 一句话：**storage 产出并监护 `rsdk_dev_t*` / `rsdk_group_t*`，交给 recorder 用**；
> 它是"盘管理器"，不是"存储引擎"。

## 2. 盘生命周期状态机

```
         插入/扫描
   ┌───────────────► DISCOVERED (拿到 /dev/sdX + 容量/型号/SN)
   │                     │ 读 SuperBlock magic="RSDK01"
   │        ┌────────────┼────────────┐
   │        ▼            ▼            ▼
   │   OURS(本组)    BLANK(空盘)   FOREIGN(外来FS)
   │        │            │            │ 需用户确认
   │        │            └──►格式化◄──┘
   │        │              rsdk_format
   │        ▼                 │
   │   ACTIVE ◄───────────────┘  纳入盘组, recorder 可写
   │        │ SMART坏/只读/掉线
   │        ▼
   └─── FAILED/OFFLINE ──告警──► event/nop ; 写入转其余盘(balance)
              │ 盘重新上线 + group_uuid 校验通过
              └──► 重新纳入 ACTIVE
```

## 3. 数据流位置

```
app/record_sched  ──init/scan──►  storage(disk_mgr)
                                     │ 1. 发现 /dev/sd*
                                     │ 2. 识别(读SB) → OURS/BLANK/FOREIGN
                                     │ 3. (需要时)格式化 rsdk_format
                                     │ 4. 组装 rsdk_group_open(有序盘路径)
                                     ▼
                            rsdk_group_t*  ──►  recorder(录像/回放)
                                     ▲
              周期 tick: SMART/满盘/热插拔 ──► 回调 on_full/on_fail/on_hotplug
                                                     └──► app/event → nop/tutk 推送
```

## 4. 防误挂载（裸盘关键）

裸盘无文件系统，但内核/udev 可能：自动 fsck、自动挂载、`ntfs`/`ext` 探测误判、甚至 GUI 弹"格式化"。
对策（storage 落地）：
1. **udev 规则**：对录像盘 `ENV{UDISKS_IGNORE}="1"`、`ENV{ID_FS_TYPE}=""`，禁止自动挂载/探测。
2. 启动时 `nvr_storage_guard_check()` 扫 `/proc/mounts`，发现录像盘被挂载则告警/卸载。
3. SuperBlock 首扇区不放任何已知 FS 魔数（RSDK01 自有魔数），降低误判。

## 5. 与 recorder 的调用契约

```c
/* storage 内部最终产出给 recorder 的就是这两个句柄： */
rsdk_group_t *g;                          // storage 组装
rsdk_group_open(ordered_paths, n, &g);    // 有序、齐全、group_uuid 一致

/* recorder 侧照常用： */
rsdk_rec_open_group(g, ch, RSDK_REC_CONTINUOUS, &w);
rsdk_balance_pick(g, ch, &picked);        // storage 的 SMART 结果影响"健康"评分(见 §6)
```

## 6. 健康结果如何反馈给均衡

`rsdk_balance_pick` 选盘时要求"健康(SMART ok / 未只读)"（设计 §238）。
storage 维护每盘 `health` 状态，通过一个回调/查询接口暴露给 balance 的选盘评分：
坏盘/只读盘 → 标记不健康 → balance 自动跳过 → 写入转其余盘。

## 7. 落地文件

```
storage/
├── include/nvr_storage.h     对外 API（disk_mgr + health + hotplug + guard）
├── src/
│   ├── storage_mgr.c         顶层管理器 + 状态机 + 告警回调         【核心, 已实现骨架】
│   ├── storage_disk.c        发现/识别/格式化编排/盘组装配          【已实现骨架, 复用 rsdk_rawdev/format】
│   ├── storage_health.c      SMART/只读/温度/坏道                   【结构就绪, SMART 采集待接平台】
│   ├── storage_hotplug.c     netlink uevent 监听 → 上/下线           【结构就绪, netlink 骨架】
│   └── storage_guard.c       防误挂载检查 + udev 规则模板            【已实现骨架】
└── CMakeLists.txt
```

## 8. 待你确认的取舍

1. **单盘多分区 vs 多盘**：本设备 1×SATA。裸盘方案下"多分区"无意义（无 FS），
   建议**整盘单设备**；多盘扩展直接用 recorder 的盘组（未来加盘即插即用）。
2. **加密**：recorder 支持 AES-256-CTR + KEK(设备SN派生)。默认建议**开**（拔盘无法读）。
3. **满盘策略**：`overwrite`(环形覆盖，监控级) vs `stop`。默认 `overwrite`。
4. **SMART 采集方式**：`libatasmart` / ATA passthrough ioctl / 读 `/sys` —— 取决于 BSP 带哪个。
