# 固件功能 × videoRecorder 文档 对照

> 对象：当前编译出的 `nvr_app`（aarch64）。按 `videoRecorder/` 文档目录逐域说明实现程度。
> 口径：**✅ 已实现并接线**（本二进制在跑）· **🟡 结构/handler 在，未完全接线/需真机或 DG 算法** · **❌ 未实现**。
> 前置：`nvr_app` 起 8089 NOP 服务端，界面命令进 nop_sdk `cap_*` handler；「能应答」≠「反映真实状态」。
> 真正接到真实子系统的是：预览/拉流/录像/云存上传/配置/事件；其余多为 nop_sdk 骨架，待接线。

## AddCamera 相机接入
| 能力 | 状态 | 说明 / 代码 |
|---|---|---|
| ONVIF 发现三分类(NOP/nopOnvif/onvif) | ✅ | `components/onvif/nvr_dev_classify`（16 自测）；WS-Discovery 扫描结构 `nvr_onvif_discover` |
| 取流 URL(ONVIF GetStreamUri) | ✅ | `nvr_onvif_get_url` |
| 通道在线状态机 + 断线退避重连 | ✅ | `app/channel/nvr_channel.c`（本二进制在跑） |
| PoE 口→通道 即插即用绑定 | 🟡 | 函数 `nvr_chan_bind_poe` 已实现；**"检测到即绑定"的发现循环 + eth1 VLAN/DHCP 组网(系统级)未接** |
| 3 种连接出图方案(增强/激活/通用) | 🟡 | 分类+连接编排结构在；**增强模式/激活密码握手待 NightOwl-DG 算法**(`nvr_pw_*` 占位) |
| LAN Add 命令集(GUI_LanSearch/Add/Del/…) | 🟡 | nop handler 在，未接通道管理 |

## GUI 本地界面（实时预览 / 回放 / 菜单 / Admin鉴权）
| 能力 | 状态 | 说明 |
|---|---|---|
| 界面↔NVR(8089 /APPJsonCmd) | ✅ | 界面独立进程；固件起 NOP 8089 后端 `app/src/nvr_app.c` |
| 实时预览 1/4/6/9/12/16 分屏 + OSD | ✅ | `app/preview/nvr_preview.c`(mhal_vout) |
| 拉流出图(RTSP→硬解→上屏) | ✅ | `components/streaming` + `platform/media_hal`(hdal) |
| 回放(检索/跨盘连续/导出MP4) | ✅ | recorder `rsdk_group_query/play/backup`(界面经 NOP 调) |
| Admin 鉴权/锁定/找回 | 🟡 | 设置库 `auth` 表在；`GUI_login` handler 在 nop_sdk，未接口令校验/锁定逻辑 |

## Storage 存储
| 能力 | 状态 | 说明 |
|---|---|---|
| 录像引擎(裸盘/环形/索引/AES加密/多盘均衡/导出/元数据/抓拍) | ✅ | `components/recorder` librsdk(8 example 实测 PASS) |
| 盘管理(发现/识别/格式化编排/装配/热插拔/防挂载) | ✅ | `components/storage`(骨架；SMART 明细待接平台) |
| getStorageInfo/formatStorage/getAllDisksHealth(NOP) | 🟡 | cap_storage handler 在，接真实盘状态需接线 |

## cloudRec 云存
| 能力 | 状态 | 说明 |
|---|---|---|
| 事件切片上传引擎(取段→TS封装→VSaaS GET url→multipart POST→回写状态) | ✅ | `components/cloud_uploader`；在 `nvr_app` 里按设置库门控启动 |
| 录像云存状态内置 recorder | ✅ | `rsdk_cloud`(20 自测) |
| 多通道 starttime 埋通道 + tags + -1002/3/4 强制关 | ✅ | `uploader.c` |
| setOwner/stoken 持久化 + 云存开关/配置 | 🟡 | 设置库表在、上传器读设置；**NOP cap_cloud/cap_misc 仍用静态量，待接** |
| 同步(无盘 BaseStation)实时上传 | ❌ | v2 延后 |

## RemoteClients 远程访问（TUTK P2P）
| 能力 | 状态 | 说明 |
|---|---|---|
| 设备端 P2P(登录/监听/推流) | 🟡 | glue `components/cloud_tutk` 已实现；**未在 nvr_app 启动**(exe 链接里注释掉)，接线后可远程 |

## Admin_NOP 账户
| owner/账户/stoken | 🟡 | 设置库 `nop_owner`/`auth` 表在；NOP handler 在，未接线 |

## pushNotification 推送
| 事件→推送 | 🟡 | 事件中枢 `nvr_event` publish 扇出到 nop push 引擎(结构在)；实际推手机需接 push server 配置 |

## OTA
| NVR .rom 自升级 / IPC 固件下推 | ❌ | cap_ota handler 在，升级流程未实现 |

## Wizard 向导（本地/APP/BLE 四场景）
| 首启配置/激活 | 🟡 | `svc_wizard` handler 在；向导 UI 由界面驱动，激活密码待 DG |

## 其余（后续里程碑）
| 域 | 状态 |
|---|---|
| talk 双向对讲 | ❌(cap_audio 在，未接) |
| BLE | ❌(按要求本期不做) |
| Chromecast | ❌ |
| ProductionTest 产测 | ❌ |
| BaseStation 无盘形态 | ❌ |

## NOP↔ONVIF 透传/翻译（核心机制，跨全部相机控制）
| 能力 | 状态 | 说明 |
|---|---|---|
| NOP→ONVIF 映射 9 域(PTZ/OSD/隐私/媒体/AI/事件/设备…) | ✅ | `nop_sdk` 映射层完成、双 build 通过 |
| 三档路由(非透传本地 / NOP 透传 / ONVIF 翻译) | 🟡 | 结构在；非透传活文档 `NOP_NONPASSTHROUGH_APIS.md` 边实现边补 |

---
## 一句话总结
**这个二进制已能：** 起 8089 给界面用 → 加载 32 路配置 → ONVIF/PoE 通道拉流出图 + 分屏预览 + 连续/事件录像 →
录像回放/导出 → AI 事件联动录像 → 事件切片上传云存 → 配置持久化(SQLite) → 断线重连 → 全程串口日志。
**待真机接线的主要是：** PoE 自动发现绑定循环、3 方案连接的密码握手(DG 算法)、TUTK P2P 启动、
NOP cap_cloud/cap_misc→设置库、OTA/对讲/推送落地。
