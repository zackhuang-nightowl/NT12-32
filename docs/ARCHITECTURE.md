# 架构说明 — NVR Firmware

> SoC：Novatek **NT98633 / NA51090** · 32 通道 · aarch64 Linux 4.19
> 状态快照见 [STATUS.md](STATUS.md) · 文档索引见 [INDEX.md](INDEX.md)

---

## 1. 设计原则

| # | 原则 | 说明 |
|---|------|------|
| 1 | **分层单向依赖** | `app → components → platform → (third_party/BSP)`，禁止反向或跨层 |
| 2 | **第三方 intact 隔离** | Happytime / TUTK / ffmpeg 原样放 `third_party/`，只经各模块 glue 使用 |
| 3 | **平台可移植** | 所有 na51090/hdal 调用**只出现在 `platform/media_hal`** |
| 4 | **功能内聚** | 六大功能各自独立目录，模块间不横向调用，由 `app/` 编排 |
| 5 | **硬解优先** | 16 路预览走 VPU 硬解（`hd_videodec`），ffmpeg 仅作辅助/兜底 |

---

## 2. 分层总览

```
┌─────────────────────────────────────────────────────────────────────────┐
│  LVGL GUI（独立进程）  ──HTTP──►  127.0.0.1:8089 /APPJsonCmd             │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │
┌────────────────────────────────────▼────────────────────────────────────┐
│ app/  整机集成层                                                          │
│   channel · preview · record_sched · event · playback · router · config  │
│   nop8012 · netime · ble · ota                                            │
└───┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┘
    │         │         │         │         │         │         │
┌───▼───┐ ┌──▼───┐ ┌───▼───┐ ┌───▼───┐ ┌───▼───┐ ┌───▼───┐ ┌───▼───────┐
│ nop ①│ │onvif②│ │stream③│ │tutk ④│ │rec  ⑤│ │stor ⑥│ │config/crypto│
│       │ │      │ │       │ │       │ │       │ │       │ │cloud_upload│
└───┬───┘ └──┬───┘ └───┬───┘ └───┬───┘ └───┬───┘ └───┬───┘ └─────┬─────┘
    │        │         │         │         │         │           │
┌───▼────────▼─────────▼─────────▼─────────▼─────────▼───────────▼───────┐
│ platform/media_hal  (mhal_vdec · mhal_vout · mhal_budget)              │
└───┬────────────────────────────────────────────────────────────────────┘
    │
┌───▼────────────────────────────────────────────────────────────────────┐
│ third_party: happytime_onvif_rtsp · tutk_sdk · cJSON · sqlite3       │
│ BSP(引用): na51090_linux_sdk  (kernel 4.19 + hdal + toolchain)       │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 3. 目录结构

```
nvr_firmware/
├── app/              整机集成（编排六大模块 + 8089 命令路由）
│   ├── channel/      通道管理、在线状态机、PoE 绑定
│   ├── preview/      分屏布局 1/4/9/16、主/子码流切换
│   ├── record_sched/ 录像计划、满盘策略、事件时窗
│   ├── event/        AI 事件中枢 → 录像/抓拍/云存联动
│   ├── playback/     本机回放引擎
│   ├── router/       8089 命令路由（display/lan/storage/cloud/...）
│   ├── config/       JSON 配置 + SQLite overlay
│   └── src/          nvr_app.c 启动编排 + main.c
├── components/
│   ├── nop/          ① NOP 协议核 + cap handler + ONVIF 映射（9 域）
│   ├── onvif/        ② ONVIF 客户端 glue + 设备三分类
│   ├── streaming/    ③ RTSP 拉流 → 硬解/录像
│   ├── cloud_tutk/   ④ TUTK P2P glue
│   ├── recorder/     ⑤ librsdk 录像引擎
│   ├── storage/      ⑥ 盘管理
│   ├── config/       SQLite 设置库 (nvr_settings)
│   ├── crypto/       MD5/SHA/AES256
│   └── cloud_uploader/ 云存 TS 封装 + VSaaS 上传
├── platform/
│   └── media_hal/    hd_videodec / hd_videoproc / hd_videoout 封装
├── third_party/      vendored 依赖（intact）
├── config/           运行期 JSON 种子（channels.json 等）
├── deploy/           部署脚本与 rootfs 模板
├── docs/             架构/状态/接口对照文档
└── cmake/            交叉编译工具链
```

---

## 4. 核心数据流

### 4.1 相机接入与出图（最关键路径）

```
eth1 VLAN 2001~2016 / DHCP → 198.18.<口>.100
         │
         ▼
ONVIF 发现(②) → GetStreamUri → rtsp://198.18.N.100/...
         │
         ▼
CRtspClient (third_party/happytime)
         │  DESCRIBE/SETUP/PLAY → video_cb(H.264/265 Annex-B)
         ├──────────────────────────────┐
         ▼                              ▼
platform/media_hal                 components/recorder(⑤)
  mhal_vdec 硬解                     rsdk_rec_write_frame
  mhal_vout → HDMI 多分屏            裸盘落盘(AES-256-CTR)
         ▲
app/preview 编排布局(1/4/9/16) + 主/子码流切换
```

### 4.2 NOP 协议与远程访问

```
手机 App ──┬── 局域网 8089 HTTP ──┐
           └── TUTK P2P ──────────┤
                                 ▼
              nop_http_server → nvr_cmd_router
                                 │
                    ┌────────────┼────────────┐
                    ▼            ▼            ▼
              本地处理      NOP 透传     ONVIF 翻译
           (display/storage) (8089→相机)  (cap→SOAP)
                    │
                    ▼
              app 编排 → streaming/recorder/platform
```

NVR 作为 ONVIF **服务端**对 App 发流：`components/nop/src/media/rtsp_server.c`

### 4.3 录像与回放

```
写: video_cb → rsdk_rec_write_frame → storage(裸盘/ext4)
读: cap_playback / GUI → rsdk_index_query → rsdk_play_* → 硬解上屏
导出: rsdk_backup_export → MP4(fMP4) → U盘
云存: 事件触发 → rsdk_cloud → TS 封装 → VSaaS HTTPS 上传
```

### 4.4 事件链路

```
相机 AI 事件 ──8012/NOP──► nop_event_hub
                              │
                              ▼
                         app/event (nvr_evt_hub)
                              │
                    ┌─────────┼─────────┐
                    ▼         ▼         ▼
              事件录像   预览图标   云存上传
           (record_sched) (preview) (cloud_uploader)
```

---

## 5. 启动时序（nvr_app_start）

```
1. nvr_config_load + nvr_settings overlay     配置 → 通道表
2. nvr_storage_init → scan → assemble          发现盘 → rsdk_group
3. mhal_vout_init(HDMI) + set_layout           显示 + 默认分屏
4. nvr_stream_mgr_init                         拉流管理器
5. 逐通道 add_channel + start_all              起流 → 出图 + 录像
6. nop_http_server(8089) + nvr_cmd_router      协议入口
7. nvr_record_sched / nvr_event / cloud_uploader  调度与联动
主循环: storage_tick + 通道重连 + 信号退出
```

---

## 6. 模块间契约

| 调用方 → 被调方 | 接口 | 说明 |
|----------------|------|------|
| streaming → platform | `mhal_vdec_*` / `mhal_vout_*` | 送 Annex-B 帧，硬解上屏 |
| streaming → recorder | `rsdk_rec_write_frame` | 同一帧旁路录像 |
| app → recorder | `rsdk_format/open/rec/index/play/backup` | 见 `components/recorder/README.md` |
| app → nop | `nop_app_create` + `nop_http_server` | 8089 协议入口 |
| app → onvif | `nvr_onvif_get_url`（弱符号钩子） | PoE 自动取流 |
| LVGL → app | `nvr_cmd_dispatch` via 8089 | 显示/通道/存储/回放命令 |
| cloud_uploader → recorder | `rsdk_cloud_*` | 云存状态与取段 |

---

## 7. 构建产物

| 目标 | 条件 | 产出 |
|------|------|------|
| 主机库 | 默认 cmake | 12 静态库 + ctest 4/4 |
| 整机 exe | `-DNVR_WITH_ONBOARD=ON -DBSP_ROOT=<sdk>` | `nvr_app` (aarch64) |

静态库清单：`nvr_app_core` · `nopcore_static` · `rsdk` · `nvr_streaming` · `nvr_onvif` · `nvr_dev_classify` · `nvr_storage` · `nvr_settings` · `nvr_crypto` · `nvr_cloud_uploader` · `nvr_cloud_tutk` · `mhal` · `cjson` · `sqlite3`

---

## 8. 待接线 / 上真机调优

> 结构已就位，详见 [STATUS.md §9](STATUS.md)

- media_hal 内存池/4K 时序/OSD 真实叠加
- NOP cap_cloud/cap_misc → 设置库持久化
- 相机 8012 客户端 app 侧 attach
- TUTK 帧路径与 streaming 联动
- DG 私有密码算法（增强模式/激活 AES256）
- 32 路并录 + ≤16 窗预览 VPU/DDR 上限验证

---

## 9. 相关文档

| 文档 | 内容 |
|------|------|
| [STATUS.md](STATUS.md) | 功能实现状态（✅/🟡/❌） |
| [SOURCE_MAP.md](SOURCE_MAP.md) | 代码来源追溯 |
| [固件系统分布.md](固件系统分布.md) | Flash 分区 / 挂载 / 进程端口 |
| [BIND_IPC_FLOW.md](BIND_IPC_FLOW.md) | 即插即用绑定流程 |
| [NOP_NONPASSTHROUGH_APIS.md](NOP_NONPASSTHROUGH_APIS.md) | NOP 命令路由三档 |
| [BUILD.md](../BUILD.md) | 构建说明 |
