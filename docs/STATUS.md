# NT12-32 NVR 固件 — 当前支持状态

> 本文记录固件**当前已实现/可编译**的能力，供团队对照与补充。
> 图例：✅ 已实现并主机自测/编译通过 · 🟡 结构就位，需上真机对接/调优 · ❌ 未开始（后续里程碑）
> 最近更新：2026-08-19（ODC TUTK agent cgi/device.sh/profile + 出厂 AuthKey 00000000）。

---

## 0. 一句话现状

六大子系统 + NOP↔ONVIF 映射 + **app 整机编排** 已串成一台可交叉编译的 NVR。
出图/双轨录像/本机回放/8089 三档路由/绑定握手（首次一轮等密码）/8012 事件/ODC TUTK agent（6061 命令 + 8554 live + 7000 对讲）/Cognito 绑 owner 已接线。
主机单测已按实机策略移除；整机 `nvr_app` 仅在目标机（`-DNVR_WITH_ONBOARD=ON` + na51090 BSP）链接。

---

## 1. 构建与工程结构 ✅

| 能力 | 状态 | 说明 |
|---|---|---|
| 自包含工程（一条命令编译） | ✅ | 见 [BUILD.md](../BUILD.md)；third_party 在工程内 |
| 主机侧库 + `nvr_app_core` | ✅ | 语法/维护校验；主机 ctest 已移除（2026-08-06） |
| 目标机交叉构建整机 exe | 🟡 | `-DNVR_WITH_ONBOARD=ON -DBSP_ROOT=<na51090_sdk>` |
| 外部依赖 | ✅ | sqlite3/openssl/libcurl 源或系统库 + 目标机 hdal / TUTK .so |

---

## 2. 协议与设备接入

| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| NOP 协议核 + 能力网关 + handler | ✅ | `components/nop` |
| App↔NVR 仅 NOP（NVR 为唯一 NOP 服务端） | ✅ | App 从不直接发 ONVIF |
| 入站本机口 | ✅ | GUI **8089**；agent cgi **6061**（同 handler）；live **8554**；对讲 **7000**；UDP **34569** |
| NOP 命令三档路由（本地 / 透传 / ONVIF 翻译） | ✅ | 中央表 `g_nvr_cmd_table`；nopOnvif 白灯/警笛/Panic/激活 POST 发现口；其余 `backend!=0` → SOAP |
| NOP→ONVIF 映射（9 域） | ✅ | `g_onvif_map_table`；token 连接时缓存，后续直接 SOAP |
| ONVIF 客户端（发现 / GetStreamUri） | ✅ | 一机一 `nop_onvif_device_t`（`retain`）；连接时建齐 token/caps |
| 设备三分类（NOP / nopOnvif / onvif） | ✅ | `nvr_dev_classify.c`；三类都走 ONVIF 取流 |
| PoE 即插即用（口级 onvif_auto + tick 广播） | ✅ | 首次添加试一轮凭据，失败 status **4** 等用户密码；见 [BIND_IPC_FLOW.md](BIND_IPC_FLOW.md) |
| LAN Add 命令集 | ✅ | `nvr_cmd_lan.c`；eth0 **不做**全网段自动绑定；首次同样一轮 |
| 增强安全 / 设备激活 | ✅ | NOP digest 由 `GUI_setLanDevice.enhancedSecurity` 开关；random 入库；401/402/501 自愈。连接只 GET 已开的 `P_enh`。激活 AES-256-ECB |
| 34569 LocalLAN 发现 / MAC 找回 | ✅ | 扫网+LAN 掉线召回走 `nvr_lan34569_discover`；本机听 `:34569` 供 App 找回 |
| 电池机设备 | ❌ | 本期忽略 |

---

## 3. 拉流 / 预览 / 显示

| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 每通道主+子**常拉**（不重连切显示） | ✅ | `stream_internal.h`：`pmain`/`psub`；`NopRtspClient` |
| 可见窗硬解上屏 + 隐藏窗只拉不解码 | ✅ | `nvr_stream_set_display` 门控 |
| 通道管理 + 掉线退避重连 | ✅ | `app/channel/nvr_channel.c` |
| 动态增删 / PoE 绑定 / 发现落地 | ✅ | `nvr_chan_add/remove/bind_poe` |
| 分屏 1/4/6/9/12/16 + 翻页 + 映射 | ✅ | `nvr_preview.c`；启动宫格读 `GUI_CONFIG.json` |
| 多分屏子码流 / 单画面主码流 | ✅ | 只改 `decode_stream` |
| HDMI 分辨率热切 + 重启 LVGL | ✅ | `setSysDisplay` → `mhal_vout` + `killall` GUI |
| OSD（通道名/时间/事件图标） | ✅ | 固件不叠 HDMI；GUI 根据 `GUI_longPolling` 自绘 |
| 32 路并录 + ≤16 窗预览 | 🟡 | 设计到位；VPU/DDR 上限需真机核 |
| media_hal（vdec/vout/aout） | 🟡 | 对真实 hdal 头通过；内存池/4K/YUV 抓拍待板级 |

---

## 4. 录像 / 存储 / 回放

| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 录像引擎（裸盘/环形/索引/加密/导出 MP4） | ✅ | `components/recorder` |
| 主/子双 writer（独立段索引） | ✅ | streaming `writer_main`/`writer_sub`；音频挂主流 |
| 连续录像排程（周计划 + 手动开关） | ✅ | GET 无库=开+7×24；保存后才录。保存立刻 `rec_schedule_apply` |
| 事件录像排程（按 sensor） | ✅ | GET 无库=7×24；无保存规则不触发落盘 |
| 事件时窗 / 满盘策略编排 | ✅ | `nvr_record_sched.c` |
| 盘管理（发现/格式化/装配/热插拔） | 🟡 | `components/storage`；SMART 已接 `rsdk_smart`（温度/重映射/通电时长），真机判定待核 |
| 本机 HDMI 回放（墙钟时钟、倍速、I 倒放、日界停） | ✅ | `nvr_playback.c` + `GUI_playbackControl` |
| 回放音频（AAC → `mhal_aout`，仅 1X 正放） | 🟡 | 已接线；真机出声待核 |
| 事件列表/日历 | ✅ | meta `DOC_CLOUD` + 连续轨 `RSDK_RK_EVENT` |
| USB 备份三件套 | ✅ | `rsdk_backup_export` → `/mnt/usb` |
| 远程回放 RTSP（TUTK 隧道 URL） | ✅ | `startPlayback` → `rtsp://iotc-tunnel:8554/playback/<startTime>`；Seek=`SET_PARAMETER playback_ctrl:seek`；RTP 拓展头 status/timestamp |

---

## 5. 事件 / AI

| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 事件中枢（nop hub → 扇出） | ✅ | `app/event/nvr_event.c` |
| 相机 AI → 事件录像 + longPolling 位图 | ✅ | `nvr_evt_ingest`；OSD 由 GUI 自绘 |
| 事件抓拍 → 列表缩略图 | ✅ | 异步 GetSnapshot/`rsdk_pic`；`thumbnailUrl=http://iotc-tunnel:8089/eventSnap?eid=`（仅隧道 GET） |
| NOP 8012 客户端（逐相机） | ✅ | `nvr_nop8012_start` 已在 `nvr_app_start` 启动 |
| ONVIF 事件轮询 → 同一 hub | ✅ | `nop_onvif_map_events_start` |
| 抓拍 / 推送联动 | ✅ | 事件 JPEG 落盘；推送附图仍可后续接云存 |

---

## 6. 云存（cloudRec）

| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 云存状态内置 Recorder | ✅ | `rsdk_cloud.*` |
| 上传引擎：待传→取段→TS→VSaaS | ✅ | `components/cloud_uploader` |
| 整机门控（UID / stoken / switch） | ✅ | `maybe_start_uploader` + settings 订阅 |
| MPEG-TS 封装 | 🟡 | 极简 PAT/PMT/PES；PCR 上真机调 |
| 错误 -1002/-1003/-1004 → 强制关 | ✅ | `uploader force_off` |
| 同步（无盘 BaseStation）实时上传 | ❌ | v2 延后 |

---

## 7. 远程访问（TUTK P2P）

| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| IOTC 登录 + P2P 会话 | ✅ | **ODC agent** `AVAPIs_Server_CLI`（`/dvr/tutk_cloud_agent/device.sh`）；不走 `nvr_tutk_init`。cgi=`nvr_tutk_cgi`（`-s` 读 `/User`，`-f` POST :6061） |
| 隧道映射 6061 命令 + 8554 live/回放 + 7000 对讲 | ✅ | App 命令 `iotc-tunnel:6061`；live `rtsp://iotc-tunnel:8554/chN_{0\|1}.264`；回放 `/playback/<ts>`；对讲 `iotc-tunnel:7000`。8089 仍给 GUI/`/eventSnap` |
| authkey 热更新 / UID 命令 | ✅ | `get/setIotcAuthKey` · `get/setAvPassword` · `get/setIotcUID` · `GUI_getUID`；出厂 `00000000`/`888888`；凭据权威源 `/User` |
| 账户门控（owner / 出厂 / 本地 admin） | ✅ | `apply_remote_access`：有 owner 常开；出厂常开；仅本地 admin 默认关 |
| aws 登录 + 向导绑定 | ✅ | Cognito `InitiateAuth`；owner 空则 GraphQL `addDevice` 写 stoken |
| 远程回放推流 | ✅ | `nvr_rtsp_live` 同口回放；空白帧 2fps；不自主 Seek |

---

## 8. 配置 / 存储持久化

| 能力 | 状态 | 实现位置 / 说明 |
|---|---|---|
| 只读 JSON 种子（32 路模型） | ✅ | `app/config/nvr_config.c` |
| 运行期 SQLite 设置库 | ✅ | `/flash/nvrcfg/nvr_settings.db`（不可写回落 config_dir） |
| 表：KV + auth / local_user / nop_owner / camera / rec_schedule / rec_triggers / 网络服务 | ✅ | 首启播种；工厂复位=删库重种 |
| 设备身份（SN/MAC/UID/TUTK/MODEL） | ✅ | `nvr_identity`：权威源 `/User` 文件（OTA 不覆盖）；settings 不存 sn/mac/uid |
| 通道映射/名称 `channels.json` | ✅ | 同目录 `/flash/nvrcfg` |
| 密码/加密算法库 | ✅ | `components/crypto`：增强 KEY_X/Y；激活 AES-256-ECB（无 IV） |
| 口令派生（增强模式/激活 AES256） | ✅ | `nvr_pw_algo_ready()`=1；key=`eT79Uo51sK` 补 0 |
| 账户（本地 + aws） | ✅ | 本地 SHA256 多用户；`GUI_login` aws → Cognito；`GUI_forgetPassword` ResetCode |
| 双向对讲 | ✅ | `app/talk`：本机 127.0.0.1:7000；NOP TCP :7000 / ONVIF backchannel。HDMI MIC 未接 |
| 电子放大 ZoomPan | ✅ | `mhal_vout_set_crop`；已 start 的 VPE 先 stop 再设 IN_CROP |
| 网络落地（eth0/eth1 VLAN+DHCP、NTP） | ✅ | `nvr_netime.c`；口 P → VLAN(2001+P)=2002..2017 |
| 周维护自动重启 | ✅ | `nvr_app.c` `auto_reboot_tick`；`GUI_get/setAutoRebootSetting` |
| OTA（MD5 + 版本 + A/B；查服务器；IPC 下推） | ✅ | NVR 自升级 `nvr_ota.c`。`GUI_checkServerFirmware` 查 NightOwl OTA。IPC：NVR 下载后 NOP `upload.cgi` / ONVIF `StartFirmwareUpgrade` |

---

## 9. 待接线 / 上真机调（结构已就位）

| 项 | 说明 |
|---|---|
| media_hal 板级 | 4K 时序 / YUV 抓拍（ddr_id 已按 dts）；回放 HDMI 音频真机出声 |
| BLE GATT | 协议桥已接 router；BlueZ 0xFFF0 待板级 |
| TUTK 远程回放 | 真机对 App 拖时间轴 / 事件回放回归 |
| TS PCR / 推送 | 云存封装调优；推送开关已落库、外发未做 |

---

## 10. 产品外围（❌ 未开始，后续里程碑）

Wizard 四场景 · LVGL GUI 本体（独立仓）· 推送通知 · Chromecast · 产测 · BaseStation 无盘形态。
> `nop_sdk` 已含大量对应 cap handler，接线时复用。

---

## 附：验证方式

主机单测目录已删除（2026-08-06，改为实机验证）。当前以目标机交叉编译 + 实机回归为准。
历史主机自测覆盖（settings/crypto/rsdk_cloud/dev_classify）仍可作为回归参考，但不再随默认 cmake 跑。
