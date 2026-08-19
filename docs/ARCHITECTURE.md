# 架构说明 — NVR Firmware

> SoC：Novatek **NT98633 / NA51090** · 32 通道（16 PoE + 16 LAN）· aarch64 Linux 4.19
> 状态快照见 [STATUS.md](STATUS.md) · 文档索引见 [INDEX.md](INDEX.md)
> 记忆同步：2026-08-19（对照 ODC TUTK agent cgi / 出厂 AuthKey 00000000 / 6061+8554+7000）

---

## 1. 设计原则

| # | 原则 | 说明 |
|---|------|------|
| 1 | **分层单向依赖** | `app → components → platform → (third_party/BSP)`，禁止反向或跨层 |
| 2 | **第三方 intact 隔离** | Happytime / TUTK / ffmpeg 原样放 `third_party/`，只经各模块 glue 使用 |
| 3 | **平台可移植** | 所有 na51090/hdal 调用**只出现在 `platform/media_hal`** |
| 4 | **功能内聚** | 六大功能各自独立目录；app 子模块不横向调用，由 `nvr_app.c` 编排 |
| 5 | **硬解优先** | 预览走 VPU 硬解（`hd_videodec`）；ffmpeg 仅作辅助/兜底，禁止软解多路上屏 |
| 6 | **入站端口最小** | GUI 命令 **8089**；App 经 ODC agent 打 **6061**（同 handler）；live **8554**、对讲 **7000** 仅本机，外网只走 TUTK agent |

---

## 2. 分层总览

```
┌─────────────────────────────────────────────────────────────────────────┐
│  LVGL GUI（独立进程）  ──HTTP──►  127.0.0.1:8089 /APPJsonCmd             │
│  手机 App ──ODC TUTK agent──►  本机 6061 命令 / 8089 缩略图 / 8554 live / 7000 对讲   │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │
┌────────────────────────────────────▼────────────────────────────────────┐
│ app/  整机集成层                                                          │
│   channel · preview · playback · record_sched · event · talk · router     │
│   config · nop8012 · netime · ble · ota                                   │
└───┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┘
    │         │         │         │         │         │         │
┌───▼───┐ ┌──▼───┐ ┌───▼───┐ ┌───▼───┐ ┌───▼───┐ ┌───▼───┐ ┌───▼───────┐
│ nop ①│ │onvif②│ │stream③│ │tutk ④│ │rec  ⑤│ │stor ⑥│ │config/crypto│
│       │ │      │ │双拉双写│ │liveRTSP│ │       │ │       │ │identity    │
└───┬───┘ └──┬───┘ └───┬───┘ └───┬───┘ └───┬───┘ └───┬───┘ └─────┬─────┘
    │        │         │         │         │         │           │
┌───▼────────▼─────────▼─────────▼─────────▼─────────▼───────────▼───────┐
│ platform/media_hal  (mhal_vdec · mhal_vout · mhal_aout · mhal_budget)  │
└───┬────────────────────────────────────────────────────────────────────┘
    │
┌───▼────────────────────────────────────────────────────────────────────┐
│ third_party: happytime_onvif_rtsp · tutk_sdk · cJSON · sqlite3         │
│ 目标机另链: NT12-SDK/OnvifClientLibrary (NopRtspClient)                 │
│ BSP(引用): na51090_linux_sdk  (kernel 4.19 + hdal + toolchain)         │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 3. 目录结构

```
nvr_firmware/
├── app/              整机集成（编排 + 8089 命令路由）
│   ├── src/          nvr_app.c 启动编排 + main.c
│   ├── channel/      通道表、在线状态机、PoE 绑定、LAN 增删、凭据候选、NOP 注册同步
│   ├── preview/      分屏 1/4/6/9/12/16、翻页、主/子显示、HDMI 热切
│   ├── playback/     本机 HDMI 回放（墙钟时钟 + 音频）
│   ├── record_sched/ 事件时窗 / 满盘策略编排
│   ├── event/        AI 事件中枢 → 录像/图标/云存/抓拍
│   ├── push/         推送策略 + 读事件图 + 图床/TPNS
│   ├── talk/         双向对讲（本机 127.0.0.1:7000；NOP:7000 / ONVIF backchannel）
│   ├── router/       8089/6061 中央路由表 g_nvr_cmd_table（含 Cognito/GraphQL/ResetCode）
│   ├── config/       JSON 种子 + SQLite overlay + channels.json；出站 URL 总表 nvr_urls.h
│   ├── nop8012/      逐 NOP 相机 8012 事件客户端
│   ├── netime/       eth0/eth1 + 时区/NTP
│   ├── ble/          BLE 配网命令桥（复用 router；板级 GATT 待接）
│   └── ota/          OTA（NVR 自升级 A/B + 查服务器 + IPC 下载下推）
├── components/
│   ├── nop/          ① NOP 协议核 + cap handler + ONVIF 映射
│   ├── onvif/        ② ONVIF 客户端 glue + 设备三分类 + UDP 34569
│   ├── streaming/    ③ 主+子常拉 → 硬解 / 双 writer / live 旁路
│   ├── cloud_tutk/   ④ nvr_rtsp_live（:8554）；P2P 走 ODC agent
│   ├── recorder/     ⑤ librsdk
│   ├── storage/      ⑥ 盘管理
│   ├── config/       SQLite 设置库 (nvr_settings) + 身份 (nvr_identity)
│   ├── crypto/       MD5/SHA/AES256
│   └── cloud_uploader/ 云存 TS + VSaaS
├── platform/media_hal/  hd_videodec / videoproc / videoout / audiodec / audioout
├── common/include/   跨模块常量（显示模式、日志）
├── third_party/      vendored 依赖（intact）
├── config/           运行期 JSON 种子
├── deploy/           部署脚本与 rootfs 模板
├── docs/             架构 / 状态 / 接口对照
└── cmake/            交叉编译工具链
```

---

## 4. 核心数据流

### 4.1 相机接入与出图

```
eth1 VLAN 2002~2017（口 P → VLAN 2001+P）/ NVR 198.18.<口>.100  （PoE：onvif_auto，tick 后台广播）
eth0 已添加 IP 相机                               （LAN：只连 LanAddDevice / 配置显式项）
         │
         ▼
ONVIF GetStreamUri → 主 URL + 子 URL
         │
         ▼
streaming 每通道两路常拉 (pmain / psub)
         │  Annex-B
         ├──────────────────┬──────────────────┐
         ▼                  ▼                  ▼
  可见窗: mhal_vdec     writer_main/sub    nvr_rtsp_live_feed
  → mhal_vout HDMI     slot.stream=0/1     (主/子 → 8554 RTP)
         ▲             音频挂主流
app/preview 宫格/全屏只改 decode_stream（不重连）
```

连接策略：

- **发现**：ONVIF WS-Discovery 为主；UDP 34569 LocalLAN 备用（应答带 MAC）。发现口 = 命令口。三类设备都走 ONVIF `GetStreamUri`。LAN 掉线按 MAC 找回（34569 + WS-Discovery）；PoE 按口，不做 MAC 钉死。NVR 本机听 34569 供 App 找回。
- **一机一 handle**：`host:port` 一只 `nop_onvif_device_t`（连接时建齐端点+token+caps；后续 mapping 用缓存，不重建）；mapping `retain`，不新建。
- **PoE 口**：`onvif_auto`；tick 对每口广播，扫到即首次试一轮凭据，失败 status **4** 等用户密码。
- **LAN**：只连已添加设备，**不在 eth0 上全网段自动绑定**；`GUI_LanAddDevice` 立刻入库（discovery 字段），tick 后台首次一轮。
- **LAN 扫描**：`GUI_LanSearch.status` 默认 1（active）；仅 scopes 含 `nopState/inactive` 才为 0。
- `NVR_MANUAL_ONLY`：不加载配置通道、不自动发现，只连手动添加。
- 凭据：用户密码（非 `123456`）→ 已开 digest 的 `P_enh` → `P_act` → 无鉴权。NOP digest 由 `GUI_setLanDevice.enhancedSecurity` 开/关（关=SET `random=""`）。random 入库至 NVR reset；401+`Random:` 重算，402/501 清本地。激活 AES-256-ECB（无 IV）。

### 4.2 NOP / 远程 / 界面

```
LVGL / 手机 App / BLE
        │
        ├── 本机 HTTP 8089
        ├── TUTK P2PTunnel → localhost:8089
        └── BLE GATT 组包 → ble_dispatch_bridge
                │
                ▼
         nvr_cmd_dispatch  (g_nvr_cmd_table)
                │
     ┌──────────┼──────────┐
     ▼          ▼          ▼
  本地 handler  NOP 透传            ONVIF 翻译
  (display/…)   POST 该路 /APPJsonCmd  按该路 host:port 发 SOAP
                (backend==0)         (backend!=0；nop_app_dispatch 仅翻译)
```

远程访问门控（`apply_remote_access`）：

| 账户态 | BLE + TUTK |
|--------|------------|
| 已绑 NOP owner | 常开 |
| 出厂（无本地账户） | 常开（供向导绑定） |
| 仅本地 admin | 默认关；`GUI_setRemoteAccessState` 运行时开关（启动复位为关） |

TUTK 实现是 **ODC agent**（`/dvr/tutk_cloud_agent` → `AVAPIs_Server_CLI`），不是 `nvr_tutk_init`、也不是 AV 裸推流。agent `popen nvr_tutk_cgi`：`-s` 读 `/User` 身份，`-f` POST 本机 **6061**（同 `nvr_cmd_dispatch`）。本机服务：8089 NOP（GUI + GET `/eventSnap`）、**6061** agent cgi、**8554** live/回放 RTSP（`rtsp://iotc-tunnel:8554/chN_0.264` 主 / `chN_1.264` 子；`/playback/<startTime>`）、**127.0.0.1:7000** 对讲。外网只经 agent，NVR 不接受直连。出厂 IotcAuthKey=`00000000`、AvPassword=`888888`。

身份权威源是数据分区 **`/User`**（`nvr_identity`）：SN/MAC 只读；UID / IOTCKey / AVKey / MODEL 可写回文件。勿把 sn/mac/uid 写进 settings 库。

### 4.3 录像与回放

```
写: pmain → writer_main (stream=0) + 音频(stream=2)
    psub  → writer_sub  (stream=1)
    连续录像排程: 主循环每 5s 评估 settings → nvr_stream_set_record

读(本机 HDMI): GUI_playbackControl
    → MasterClock(wall) + 每通道 feeder
    → rsdk_index_query / rsdk_play_*
    → mhal_vdec 上屏；1X 正放 AAC → mhal_aout

导出: GUI_ChannelBackup* → rsdk_backup_export → /mnt/usb
云存: 事件触发 → rsdk_cloud → TS → VSaaS HTTPS
```

远程回放：`startPlayback` → `rtsp://iotc-tunnel:8554/playback/<startTime>`（与 live 同口）。
App DESCRIBE/SETUP/PLAY 后按时间流推盘上帧；间隙推空白帧（RTP 拓展头 status=0）。
二次拖动：`SET_PARAMETER` `playback_ctrl: seek`（UTC y/m/d/h/min/sec）→ 停推清缓冲，等 PLAY 再推。
HDMI 本机回放仍走 `GUI_playbackControl`，两条路径不混。

### 4.4 事件链路

```
NOP 相机 8012 ──nvr_nop8012──┐
ONVIF 事件轮询 ──map_backend─┼──► nop_event_hub ──► nvr_evt_hub
                             │         │
                             │         ├─ record_sched 事件时窗
                             │         ├─ 异步抓拍 rsdk_pic（GetSnapshot / 8012 JPEG）
                             │         ├─ 后录结束：NOP AI_getEventExtInfo → meta_doc(AI_EVENT)
                             │         └─ cloud_uploader
                             ▼
                    longPolling 位图 → LVGL 自绘 OSD（通道名/时间/事件图标）
                    queryEventList.thumbnailUrl = http://iotc-tunnel:8089/eventSnap?eid=
                    AI_getEventExtInfo / Batch → meta.db（App 查 NVR，不查相机缓存）
```

---

## 5. 启动时序（`nvr_app_start`）

```
 1. nvr_config_load                         只读 JSON 种子
 2. nvr_settings_open(/flash/nvrcfg)        可写库 + overlay（不可写则回落 config_dir）
 2b. nvr_identity_ensure_provisioned        /User 身份兜底（tutkdata.json / SN/MAC/UID 日志）
 3. nvr_net_apply + nvr_time_apply          eth0/eth1 VLAN+DHCP、时区/NTP（须在发现前）
 3b. nvr_lan34569_server_start              本机 UDP 34569 应答
 4. nvr_storage_init → scan → assemble      盘组；失败则仅预览
 5. rsdk_meta_open(/flash/nvrcfg/meta.db)   云存/事件元数据
 6. mhal_vout_init(HDMI)                    按 display.resolution，不支持则降级回写
 7. nvr_stream_mgr_init                     拉流管理器（绑盘组）
 8. nop_event_hub + nop_app_create          协议核
 9. record_sched / preview / persist / event
10. nvr_chan_mgr + load_config + start_all  起流（MANUAL_ONLY 则跳过配置通道）
11. GUI_CONFIG.json → preview_set_mode      启动宫格/页（解码门控）
12. ONVIF 映射后端 + 事件轮询
13. nvr_nop8012_start                       逐 NOP 相机 8012
14. nvr_playback_create                     本机回放引擎
14b. nvr_talk_init(7000)                    对讲仅听 127.0.0.1
15. nvr_cmd_router + nop_http_server(8089)  GUI 入口
15b. nop_http_server(6061)                  ODC agent cgi（同 handler）
15c. nvr_rtsp_live_start(8554)              隧道 live+playback
16. maybe_start_uploader                    有盘+meta+UID（UID ← nvr_identity）
17. apply_remote_access                     门控 BLE + 拉起/杀掉 ODC agent
主循环: storage_tick + chan_tick + rec_tick + 排程(5s) + preview/evt + NTP(60s)
```

持久化约定：`nvr_settings.db` / `meta.db` / `channels.json` 落 **`/flash/nvrcfg`**（ubifs，OTA 跳过）。`config_dir`（常为 `/tmp/nvrcfg` 或 `/dvr/config`）只作 JSON 种子。

---

## 6. 模块间契约

| 调用方 → 被调方 | 接口 | 说明 |
|----------------|------|------|
| streaming → platform | `mhal_vdec_*` / `mhal_vout_*` | 可见窗送 Annex-B，硬解上屏 |
| playback → platform | `mhal_vdec_*` / `mhal_aout_*` | 录像帧上屏；AAC 硬解出声 |
| streaming → recorder | `rsdk_rec_write_frame` | 主/子各 writer |
| streaming → tutk | `nvr_rtsp_live_feed` / `_audio` | 主/子/音频旁路给 8554 |
| app → recorder | `rsdk_format/open/rec/index/play/backup` | 见 recorder README |
| app → nop | `nop_app_create` + `nop_http_server` | 8089 GUI + 6061 agent cgi |
| app → onvif | `nvr_onvif_get_url`（弱符号） | PoE 自动取流 |
| channel → nop | `nop_nvr_channels` + `nop_onvif_map_backend` | 控制翻译 + 事件轮询 |
| LVGL / TUTK / BLE → app | `nvr_cmd_dispatch` | 同一张路由表 |
| cloud_uploader → recorder | `rsdk_cloud_*` | 云存取段 |

app 子模块契约：preview / record_sched / event 不互相 include；上线/掉线/图标一律经 `nvr_app.c` 回调。

出站云 HTTP(S) URL 只写 [`app/config/nvr_urls.h`](../app/config/nvr_urls.h)，对齐 NOP_DOC **ServeDomainV2**：默认 production，`-DNVR_STAGE=ON` 为 stage。本机相机 `/APPJsonCmd`、IPC `upload.cgi`、`iotc-tunnel` 拼接不收录。

---

## 7. 8089 命令路由

权威表：`app/router/nvr_cmd_table.c` 的 `g_nvr_cmd_table[]`。新增本地接口 = **加一行 + 一个 `cmd_<func>`**。

| 文件 | 域 |
|------|----|
| `nvr_cmd_display.c` | 分屏 / 映射 / 分辨率 / longPolling / playbackControl / 能力 |
| `nvr_cmd_lan.c` | LAN 搜索 / 添加 / 删除 |
| `nvr_cmd_system.c` | 设备信息 / 时间 / owner / 远程访问 / TUTK key / UID |
| `nvr_cmd_account.c` | 登录 / 用户（本地 SHA256 + aws Cognito；空 owner 时 GraphQL addDevice） |
| `nvr_cognito.c` / `nvr_graphql.c` / `nvr_resetcode.c` | Cognito / addDevice / Admin 找回码 |
| `nvr_cmd_network.c` | eth / NTP / DDNS / UPnP / PoE / 端口 |
| `nvr_cmd_storage.c` | 盘信息 / 格式化 / 健康 |
| `nvr_cmd_cloud.c` | 云存开关 / 配置 |
| `nvr_cmd_record.c` | 录像开关 / 周计划 / 触发 / 推送 |
| `nvr_cmd_playback.c` | 回放音频 / 文件列表 / USB 备份 |
| `nvr_cmd_event.c` | 事件列表 / 日历 / thumbnailUrl |
| `nvr_cmd_p2p.c` | live / startSpeaker / stopSpeaker / tunnel |
| `nvr_cmd_ota.c` | 固件升级（本机 A/B + 查 OTA 服务器 + 通道下推） |
| `nvr_cmd_misc.c` | 通道聚合 / 增强安全 / 云统计 |

未命中本地表且带 `channel`：
- `backend==0` → 透传到**该通道物理设备** `/APPJsonCmd`（`args.channel` 改为该路 `dev_chn`，即启用的那路源）
- **例外** `X_NightOwl_getChannelActivityZoneTypes` / `get/setChannelTriggerActivityZone`：NOP 先透传；失败再 ONVIF CellMotion mapping（ModifyRules，不删建）。Types 仅当 GetRules 支持 Motion 时回 `triggers:["pixelChange"]`；TriggerActivityZone 读写 CellMotion ActiveCells。
- **nopOnvif 仅** `nightowl_protocol.md` 白灯 / 警笛 / Panic / DeviceActive → POST 发现口。其余 nopOnvif 命令仍 mapping SOAP。
- 其余 `backend!=0` → 本机 mapping 翻成 ONVIF，发到**该通道** `host:port`（协议 channel 改 0-based 只为查表）。

**OTA 在本地表**（不透传、不 mapping）：`GUI_checkServerFirmware` / `GUI_checkChannelServerFirmware` / `GUI_upgradeChannelFirmware` / `X_NightOwl_upgradeChannelFirmware` / `X_NightOwl_checkChannelUpgradeStatus`。NVR 查 OTA 服务器并下载，再按 NOP `upload.cgi` 或 ONVIF `StartFirmwareUpgrade` 下推。

mapping token：连接时写入 handle 缓存；PTZ=`ProfileToken`；对焦=`VideoSourceToken`；OSD/Mask=`VSC`；AI=`AnalyticsCfg`；编码=`venc`。详见 [BIND_IPC_FLOW.md](BIND_IPC_FLOW.md)。

---

## 8. 构建产物

| 目标 | 条件 | 产出 |
|------|------|------|
| 主机库 | 默认 cmake | 静态库 + `nvr_app_core`（语法/链接校验；主机单测已移除） |
| 整机 exe | `-DNVR_WITH_ONBOARD=ON -DBSP_ROOT=<sdk>` | `nvr_app` (aarch64) |

静态库：`nvr_app_core` · `nopcore_static` · `rsdk` · `nvr_streaming` · `nvr_onvif` · `nvr_dev_classify` · `nvr_storage` · `nvr_settings` · `nvr_crypto` · `nvr_cloud_uploader` · `nvr_cloud_tutk` · `mhal` · `cjson` · `sqlite3`

目标机另链：`onvifclient`（`NT12-SDK/OnvifClientLibrary`，`NopRtspClient`）· happytime · TUTK `.so` · hdal。

CMake 选项：`NVR_WITH_ONVIF` · `NVR_WITH_ONBOARD` · `NVR_STAGE`（stage 云域名，对齐 ServeDomainV2；默认 production）。onboard 默认打开 ONVIF。

---

## 9. 待接线 / 上真机调优

结构已就位，细节见 [STATUS.md](STATUS.md)：

- media_hal 4K 时序 / YUV 抓拍 / 回放音频真机出声
- BLE 板级 BlueZ GATT 0xFFF0
- 32 路并录 + ≤16 窗预览的 VPU/DDR 上限验证
- 云存 TS PCR 连续性；推送外发
- TUTK 远程回放真机对 App（Timeline Seek / 空白帧）回归

---

## 10. 相关文档

| 文档 | 内容 |
|------|------|
| [STATUS.md](STATUS.md) | 功能实现状态（✅/🟡/❌） |
| [待完成功能.md](待完成功能.md) | 待办填写表 |
| [SOURCE_MAP.md](SOURCE_MAP.md) | 代码来源追溯 |
| [固件系统分布.md](固件系统分布.md) | Flash 分区 / 挂载 / 进程端口 |
| [BIND_IPC_FLOW.md](BIND_IPC_FLOW.md) | 即插即用绑定流程 |
| [NOP_NONPASSTHROUGH_APIS.md](NOP_NONPASSTHROUGH_APIS.md) | NOP 命令路由三档 |
| [实现_回放顺畅与双轨录像_2026-08-11.md](实现_回放顺畅与双轨录像_2026-08-11.md) | 墙钟回放 + 主/子双 writer |
| [实现_Playback协议对齐_2026-08-12.md](实现_Playback协议对齐_2026-08-12.md) | 事件检索 / Control / Audio / USB Backup |
| [../BUILD.md](../BUILD.md) | 构建说明 |
