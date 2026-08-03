# 架构说明 — NVR Firmware

## 1. 设计原则

1. **分层单向依赖**：`app → components → platform → (third_party/BSP)`，不允许反向或跨层。
2. **第三方 intact 隔离**：Happytime / TUTK / ffmpeg 原样放 `third_party/`，只通过各模块的 glue 使用，便于整体升级替换。
3. **平台可移植**：所有 na51090/hdal 私有调用**只出现在 `platform/media_hal`**；换 SoC 只改这一层。
4. **功能内聚**：六大功能各自独立目录，模块间不横向调用，由 `app/` 编排。

## 2. 分层与模块

```
┌──────────────────────────────────────────────────────────────────┐
│ app/  整机集成（自研，待补充）                                       │
│   channel  preview  record_sched  event  config                    │
└───────┬───────────────┬───────────────┬───────────────┬───────────┘
        │               │               │               │
┌───────▼──────┐ ┌──────▼──────┐ ┌──────▼──────┐ ┌──────▼───────┐
│ components/  │ │ components/ │ │ components/ │ │ components/   │
│ nop (①)      │ │ onvif (②)   │ │ streaming(③)│ │ cloud_tutk(④) │
│ recorder(⑤)  │ │             │ │             │ │ storage(⑥)    │
└───────┬──────┘ └──────┬──────┘ └──────┬──────┘ └──────┬───────┘
        │               │               │               │
┌───────▼───────────────▼───────────────▼───────────────▼───────────┐
│ platform/media_hal   (hd_videodec / hd_videoout / hd_videoenc)     │
└───────┬────────────────────────────────────────────────────────────┘
        │
┌───────▼────────────────────────────────────────────────────────────┐
│ third_party: happytime_onvif_rtsp · tutk_sdk · ffmpeg · cJSON       │
│ BSP(引用): na51090_linux_sdk  (kernel 4.19 + hdal + toolchain)      │
└─────────────────────────────────────────────────────────────────────┘
```

## 3. 核心数据流

### 3.1 相机接入与出图（③ 拉流，最关键）

```
ONVIF 发现(②)                       eth1.VLAN 2001~2016 / DHCP 分 198.18.x
   │  GetStreamUri → rtsp://198.18.N.100/...
   ▼
CRtspClient (third_party/happytime_onvif_rtsp/source/rtsp)
   │  DESCRIBE/SETUP/PLAY → video_cb(H.264/265 Annex-B 帧)
   ├─────────────────────────────┐
   ▼                             ▼
platform/media_hal            components/recorder (⑤)
  hd_videodec 硬解              rsdk_rec_write_frame  录像落盘
   ▼
  hd_videoout / HDMI  多分屏预览(preview 编排)
```

> ⚠️ **不要用 ffmpeg 软解 16 路上屏**——解码走 NA51090 硬件 VPU（`hd_videodec`）。
> ffmpeg 仅作 SDP/RTP 辅助、导出转封装、个别相机软解兜底。

### 3.2 远程访问（① NOP / ④ TUTK）

```
手机 App ──┬── 局域网 8089 HTTP ──┐
           └── TUTK P2P(40633) ───┤
                                  ▼
                    components/nop  NOP 协议（envelope→router→cap 网关→handler）
                                  │  cap_stream/cap_record/cap_playback/cap_ptz ...
                                  ▼
                    app/ 编排 → 调 streaming/recorder/... + platform
```
NVR 作为 ONVIF **服务端**对 App 发流：`components/nop/src/media/rtsp_server.c`。

### 3.3 录像与回放（⑤ + ⑥）

```
写: video_cb → recorder(rsdk_rec_write_frame) → storage(落盘 raw/ext4)
读: NOP cap_playback / GUI → recorder(rsdk_index_query → rsdk_play_*) → 硬解上屏
导出: rsdk_backup_export → MP4/fMP4 → U盘
```

## 4. 模块间契约（关键接口）

| 调用方 → 被调方 | 接口 | 说明 |
|----------------|------|------|
| streaming → platform | `mhal_vdec_*`（media_hal） | 送 H.264/265 帧，取解码后 YUV/上屏 |
| streaming → recorder | `rsdk_rec_write_frame` | 同一帧旁路录像 |
| app → recorder | `rsdk_format/open/rec/index/play/backup` | 见 `components/recorder/README.md` |
| app → nop | `nop_app_create/dispatch` | 协议入口 |
| nop → onvif/tutk | `src/onvif/*adapter`、`cap_cloud` | NOP 已内置适配器 |
| onvif/streaming → third_party | Happytime `CRtspClient`/`onvif_*` | intact 上游 |

## 5. 待补充清单（Review 后确定方向）

- [ ] `app/`：通道管理状态机、预览布局、录像调度、事件联动、配置读写。
- [ ] `components/storage/`：落盘方案（裸盘直写 vs ext4 文件）。
- [ ] `platform/media_hal/`：把骨架头对接 na51090 hdal 真实 API（现为接口占位）。
- [ ] `components/streaming/`：`video_cb → vdec → 录像/上屏` 的 glue 实现。
- [ ] 顶层 CMake：打通交叉编译（用 BSP 工具链）。
