# NT12-32 NVR 固件 — 当前支持状态

> 本文记录固件**当前已实现/可编译**的能力，供团队对照与补充。
> 图例：✅ 已实现并主机自测/编译通过 · 🟡 结构就位，需上真机对接/调优 · ❌ 未开始（后续里程碑）
> 最近更新：2026-08（核心视频链路 + 云存 + 配置持久化打通）。补充请直接在对应表格追加行。

---

## 0. 一句话现状
六大底层子系统 + NOP↔ONVIF 映射 + **app 整机集成层** + **云存上传** + **配置持久化** 已串成
一台可编译的 NVR。主机 `cmake -S . -B build && cmake --build build` 干净产出 **12 个库**，
`ctest` **4/4 通过**。整机可执行 `nvr_app` 仅在目标机（`-DNVR_WITH_ONBOARD=ON` + na51090 BSP）链接。

---

## 1. 构建与工程结构 ✅
| 能力 | 状态 | 说明 |
|---|---|---|
| 自包含工程（一条命令编译，可移植到别的机器） | ✅ | 见 [BUILD.md](../BUILD.md)；third_party 全在工程内，唯一 `cjson` 共享库 |
| 主机侧全部库编译 + ctest | ✅ | 12 库；4 自测（settings/crypto/rsdk_cloud/dev_classify）|
| 目标机交叉构建整机 exe | 🟡 | 需 `-DNVR_WITH_ONBOARD=ON -DBSP_ROOT=<na51090_sdk>` + 交叉工具链 |
| 外部依赖 | ✅ | 仅系统库 sqlite3/openssl/libcurl + 目标机 na51090 BSP |

---

## 2. 协议与设备接入
| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| NOP 协议核 + 能力网关 + handler | ✅ | `components/nop`（SDK 完整、已单测） |
| App↔NVR 仅 NOP（NVR 为唯一 NOP 服务端） | ✅ | App 从不直接发 ONVIF |
| NOP 命令三档路由（非透传本地 / NOP 透传 / ONVIF 翻译） | 🟡 | 名单见 [NOP_NONPASSTHROUGH_APIS.md](NOP_NONPASSTHROUGH_APIS.md)（活文档，边实现边补） |
| NOP→ONVIF 映射（PTZ/OSD/隐私/媒体/AI/事件…9 域） | ✅ | `nop_sdk` 映射层完成、双 build 通过 |
| ONVIF 客户端（发现 / GetStreamUri） | ✅ | `components/onvif`（点亮 PoE 自动取流） |
| **设备三分类**（ONVIF-Discovery Scopes：NOP / nopOnvif / onvif） | ✅ | `components/onvif/nvr_dev_classify.c`（16 自测）|
| 即插即用「3 种连接出图方案」框架 | 🟡 | 流程见 [BIND_IPC_FLOW.md](BIND_IPC_FLOW.md)；分类/绑定就位，鉴权握手待 DG 算法 |
| 增强安全模式 / 设备激活流程 | 🟡 | 接口/流程已梳理；`nvr_pw_*` 密码算法待 NightOwl-DG |
| LAN Add 命令集（GUI_LanSearch/Add/Del/…） | 🟡 | 参考实现 `SDK_NEW/nop_client/nop_bind_ipc.*`；NVR 侧 handler 待接 |
| 34569 LocalLAN 发现 / MAC 找回 | ❌ | 结构预留（`nvr_onvif` 待补） |
| 电池机设备 | ❌ | 本期忽略 |

---

## 3. 拉流 / 预览 / 显示
| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 每路 RTSP 拉流（CRtspClient）→ 硬解 → 上屏 + 旁路录像 | ✅ | `components/streaming`（对真实头编译通过） |
| 通道管理 + 在线/掉线/退避重连 状态机 | ✅ | `app/channel/nvr_channel.c` |
| 动态增删 / PoE 口绑定 / 发现结果落地分类 | ✅ | `nvr_chan_add/remove/bind_poe/apply_discovery` |
| 分屏布局 1/4/6/9/12/16 + 翻页 + 窗口映射 | ✅ | `app/preview/nvr_preview.c`（6/12 用 9/16 网格绑 N 窗） |
| 多分屏子码流 / 单画面·全屏切主码流 | ✅ | `nvr_preview_fullscreen/single_zoom` |
| OSD（通道名/时间/事件图标） | 🟡 | 文本合成就位；真实叠加样式上真机调 `mhal_vout_osd` |
| 32 路并录 + ≤16 窗预览（录像免解码，仅可见窗解码） | 🟡 | 设计到位；VPU/DDR 上限需真机核 |
| 平台 media_hal（hd_videodec/videoout 对接 hdal） | 🟡 | 骨架对真实 hdal 头语法通过；内存池/4K 时序/OSD 待实现 |

---

## 4. 录像 / 存储 / 回放
| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 录像引擎（裸盘/环形/索引/加密/多盘均衡/导出 MP4/元数据/抓拍） | ✅ | `components/recorder` librsdk（8 example 实测 PASS） |
| 盘管理（发现/识别/格式化编排/装配/热插拔/防挂载） | 🟡 | `components/storage`；SMART 明细待接平台 |
| 连续录像（通道 record=1，帧路径直写） | ✅ | 由 `streaming` 负责 |
| 录像调度 / 事件时窗 / 满盘策略编排 | ✅ | `app/record_sched/nvr_record_sched.c` |
| 回放（时间检索 / 跨盘连续 / 导出） | ✅ | recorder API（`rsdk_group_query/play/backup`）；GUI 层未接 |
| 录像计划表（周计划/按事件类型） | 🟡 | 表结构在设置库；调度细节待补 |

---

## 5. 事件 / AI
| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 事件中枢（复用 nop 事件脊柱，发布即扇出） | ✅ | `app/event/nvr_event.c` |
| 相机 AI 事件 → 事件录像触发 + 预览图标 | ✅ | `nvr_evt_ingest`（type→RSDK_REC_* 映射） |
| 事件 → 抓拍(rsdk_pic)/推送/云存 联动 | 🟡 | 抓拍/推送钩子在 nop 侧；云存已接（见 §6） |
| 相机 8012 事件中心客户端（每相机接收 AI 事件） | 🟡 | nop 侧有 `svc_event8012_client`；app 侧 attach 待接线 |

---

## 6. 云存（cloudRec）
| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 录像云存状态内置 Recorder（不改冻结盘格式） | ✅ | `rsdk_cloud.*`（doc_type=CLOUD，20 自测） |
| 上传引擎：轮询待传→取段→读帧→TS 分片→上传→回写状态 | ✅ | `components/cloud_uploader/uploader.c` |
| VSaaS HTTP：GET stream_url + multipart POST + Update Tags | ✅ | `http_vsaas.c`（libcurl，URL/文件名/tags 按文档） |
| MPEG-TS 封装（H264/H265 + AAC） | 🟡 | `ts_mux.c` 极简 PAT/PMT/PES；PCR/连续性上真机调优 |
| 多通道 starttime 埋通道 + tags | 🟡 | 已实现；>8 通道单位数限制（同文档） |
| 错误 -1002/-1003/-1004 → 强制关开关 | ✅ | `uploader force_off` |
| 上传器接入整机（按设置库 switch/stoken/UID 门控 + 变更通知） | ✅ | `app/src/nvr_app.c` `maybe_start_uploader` |
| 同步（无盘 BaseStation）实时上传 | ❌ | v2 延后（tee 点已标注） |
| `X_NightOwl_setOwner/CloudRecordSwitch/Configs` 持久化并门控 | 🟡 | 设置库表已就位；NOP handler 仍用静态量，待接（见 §9） |

---

## 7. 远程访问（TUTK P2P）
| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 设备端 P2P（登录/监听/推流） | ✅ | `components/cloud_tutk`（glue 实现） |
| 直播/回放经 TUTK/NOP 回传 App | 🟡 | 帧推送接口就位；与 streaming 帧路径联动待接线 |

---

## 8. 配置 / 存储持久化
| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 只读 JSON 配置加载（device→source→channel 扁平化，32 路模型） | ✅ | `app/config/nvr_config.c` |
| **运行期可写设置库（SQLite）** | ✅ | `components/config/nvr_settings.*`（24 自测） |
| 表：setting / auth / nop_owner / channel / cloud_channel / rec_schedule / rec_triggers / netif | ✅ | 首启从 JSON 播种；工厂复位=删库重种 |
| JSON 默认 + 设置库 overlay 桥接 | ✅ | `nvr_config_overlay_from_settings` |
| 密码/加密算法库（MD5/SHA/AES256） | ✅ | `components/crypto`（10 自测，FIPS 向量） |
| 口令派生（增强模式/激活 AES256） | 🟡 | 占位实现，`nvr_pw_algo_ready()`==0，待 NightOwl-DG 算法 |
| 敏感项（密码 hash / stoken）落库 0600 | ✅ | v1 文件权限；静态加密为后续加固项 |

---

## 9. 待接线 / 上真机调（结构已就位）
| 项 | 说明 |
|---|---|
| NOP `cap_cloud`/`cap_misc` → 设置库 | owner/stoken 持久化、cloud 开关/配置门控上传器；需把 settings 句柄穿进 nop 业务上下文 |
| 分类器 2→3（nop_sdk） | 可选：app 已单独记 kind；2 类后端分派已够用 |
| DG 私有密码算法 | 增强模式 random→password、激活 AES256（对外阻塞项） |
| media_hal 对接真实 hdal | 内存池/4K 时序/OSD/YUV 抓拍 |
| streaming happytime + tutk 实链 | `NVR_WITH_ONBOARD` + BSP |
| 相机 8012 客户端 attach | app/event 侧逐相机接收 AI 事件 |

---

## 10. 产品外围（❌ 未开始，后续里程碑）
Wizard 四场景 · BLE GATT(0xFFF0) · LVGL GUI(预览/回放/菜单/HDMI 4K+LCD+CVBS) · OTA(NVR .rom 自升级 + IPC 固件下推) · 推送通知 · 双向对讲 talk · Chromecast · 产测 ProductionTest · Admin 鉴权/锁定/找回 UI · 周维护自动重启 · BaseStation 无盘形态。
> `nop_sdk` 已含大量对应 cap handler，接线时复用。

---

## 附：自测清单
| 测试 | 覆盖 | 结果 |
|---|---|---|
| `nvr_settings` | KV/结构化表/订阅/种子/持久化 | 24 ✅ |
| `nvr_crypto` | MD5/SHA1/SHA256/AES256(FIPS 向量) | 10 ✅ |
| `rsdk_cloud` | begin/set_state/enumerate/on_reclaim | 20 ✅ |
| `dev_classify` | 三分类/MAC/active/bound | 16 ✅ |
| recorder 8 example | 格式化/加密/索引/回放/多盘/导出/元数据/覆盖 | PASS |
