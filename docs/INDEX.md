# 文档索引 — NT12-32 NVR Firmware

> 按主题分类，便于快速定位。
> 活文档（对照当前代码）：[ARCHITECTURE.md](ARCHITECTURE.md) · [STATUS.md](STATUS.md) · [CODE_GAP_AUDIT.md](CODE_GAP_AUDIT.md)
> 带日期的审计/实现记录是当时快照，以活文档为准。记忆同步：2026-08-18。

---

## 架构与工程

| 文档 | 说明 |
|------|------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | **分层架构、数据流、模块契约、启动时序**（2026-08-18 对照实码） |
| [STATUS.md](STATUS.md) | **当前实现状态**（✅/🟡/❌，2026-08-20 更新） |
| [CODE_GAP_AUDIT.md](CODE_GAP_AUDIT.md) | **代码级缺口审计**（501 / 桩 / 仅配置 / 部分 / 真机待核） |
| [8-22完成内容.md](8-22完成内容.md) | **8-22 里程碑**：cap 清理、硬编码字段清单、AI/活动区改造 |
| [API_HARDCODED_DEFAULTS.md](API_HARDCODED_DEFAULTS.md) | **接口硬编码默认值**（应仅来自 DB 或 IPC） |
| [DB_SEED_DEFAULTS.md](DB_SEED_DEFAULTS.md) | **数据库出厂默认值**（16 表 seed · schema v9 · stream_type 策略） |
| [NVR_DATA_MAP.html](NVR_DATA_MAP.html) | **数据与配置可视化**（DB · 裸盘 · 录像码流策略 · JSON · 路径树） |
| [待完成功能.md](待完成功能.md) | **待办填写表**（做/缓/不做 + 优先级 + 备注） |
| [SOURCE_MAP.md](SOURCE_MAP.md) | 代码来源追溯（NT12-SDK → 本工程） |
| [固件系统分布.md](固件系统分布.md) | Flash 分区、Linux 挂载、库依赖、进程端口 |
| [固件系统分布.html](固件系统分布.html) | 上述内容的可视化版 |
| [../BUILD.md](../BUILD.md) | 构建命令（主机 / 交叉编译） |
| [../deploy/README.md](../deploy/README.md) | 部署与看护脚本 |
| [../README.md](../README.md) | 项目入口、功能清单 |

---

## 协议与设备接入

| 文档 | 说明 |
|------|------|
| [BIND_IPC_FLOW.md](BIND_IPC_FLOW.md) | 即插即用绑定流程（发现→分类→绑定→出图） |
| [NOP_NONPASSTHROUGH_APIS.md](NOP_NONPASSTHROUGH_APIS.md) | NOP 命令三档路由（本地/透传/ONVIF 翻译） |
| [功能对照_nopdoc.md](功能对照_nopdoc.md) | NOP 协议功能对照 |
| [getDeviceCapabilities样式.txt](getDeviceCapabilities样式.txt) | 能力查询响应样例 |

---

## 出图 / 显示

| 文档 | 说明 |
|------|------|
| [出图流程_显示管线.md](出图流程_显示管线.md) | 显示管线文字版 |
| [出图流程_显示管线.html](出图流程_显示管线.html) | 显示管线可视化 |
| [出图实现_2026-08-05.md](出图实现_2026-08-05.md) | 出图实现记录 |
| [spec_出图_display_2026-08-03.md](spec_出图_display_2026-08-03.md) | 出图 display spec |
| [PREVIEW_32CH.md](PREVIEW_32CH.md) | 32 通道预览设计 |
| [superpowers/plans/2026-08-03-出图-display.md](superpowers/plans/2026-08-03-出图-display.md) | 出图实现计划 |

---

## 录像 / 存储 / 回放

| 文档 | 说明 |
|------|------|
| [videoRecorder功能对照.md](videoRecorder功能对照.md) | 录像功能对照 |
| [回归_录像与NOP响应_2026-08-07.md](回归_录像与NOP响应_2026-08-07.md) | 录像回归测试 |
| [需你确认_回放与能力_2026-08-10.md](需你确认_回放与能力_2026-08-10.md) | 回放与能力确认项 |
| [实现_回放顺畅与双轨录像_2026-08-11.md](实现_回放顺畅与双轨录像_2026-08-11.md) | 墙钟回放 + 主/子双 writer 实现说明 |
| [实现_Playback协议对齐_2026-08-12.md](实现_Playback协议对齐_2026-08-12.md) | 事件检索 / Control / Audio / USB Backup |
| [../components/recorder/README.md](../components/recorder/README.md) | librsdk API 文档 |

---

## 云存 / 远程

| 文档 | 说明 |
|------|------|
| [云存储_说明.md](云存储_说明.md) | 云存架构与 VSaaS 接口 |
| [待办_TUTK_P2P_远程回放.md](待办_TUTK_P2P_远程回放.md) | TUTK：live 隧道已通，远程回放 RTSP 待办 |

---

## 审计 / 进度 / 交付

| 文档 | 说明 |
|------|------|
| [实际进度_代码审计_2026-08-03.md](实际进度_代码审计_2026-08-03.md) | 代码级审计（真实接线 vs 待接线） |
| [进度_2026-08-10.md](进度_2026-08-10.md) | 2026-08-10 进度快照 |
| [修改日志.md](修改日志.md) | 变更日志 |
| [交付说明.md](交付说明.md) | 交付说明 |
| [交付说明.html](交付说明.html) | 交付说明可视化 |
| [NVR功能清单.md](NVR功能清单.md) | 完整功能清单 |
| [功能对照_逐项.md](功能对照_逐项.md) | 逐项功能对照 |
| [死代码清理清单.md](死代码清理清单.md) | 死代码清理 |

---

## 设计 spec / 计划

| 文档 | 说明 |
|------|------|
| [superpowers/specs/2026-08-05-cmd-router-table-design.md](superpowers/specs/2026-08-05-cmd-router-table-design.md) | 8089 命令路由表设计 |
| [烧录分区数据.md](烧录分区数据.md) | 烧录分区说明 |

---

## 组件内部文档

| 文档 | 说明 |
|------|------|
| [../app/README.md](../app/README.md) | app 集成层说明 |
| [../platform/README.md](../platform/README.md) | media_hal 平台层 |
| [../components/nop/docs/](../components/nop/docs/) | NOP SDK 内部文档 |
| [../components/recorder/README.md](../components/recorder/README.md) | 录像引擎 API |
