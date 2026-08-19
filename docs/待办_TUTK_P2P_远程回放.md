# TUTK / P2P — 已做与待办

> 2026-08-16 对照：`nvr_tutk.c` + `nvr_rtsp_live.c` 已接线。
> live 隧道已通；**远程回放 RTSP** 仍未做。

## 已实现

- IOTC 登录（authkey）+ `P2PTunnelServer_Listen`
- 端口白名单：`nop_port`(8089) + `rtsp_port`(554)
- `nvr_rtsp_live`：本机 RTSP，streaming 旁路子码流 Annex-B → RTP
- 账户门控：`apply_remote_access`（owner / 出厂 / 本地 admin）
- `get/setIotcAuthKey`、`GUI_getUID`；authkey 可热更新

## 仍待办

- 远程回放推流：`startPlayback` 返回 `rtsp://iotc-tunnel:...`
  - 需回放 RTSP 服务（`rsdk_play` 帧 → RTP/RTSP → 经隧道给 App）
  - 固件当前只有 **live** RTSP（`nvr_rtsp_live`），没有 playback RTSP server
- `getPlaybackCapabilities` 的 `rtsp-iotc-tunnel` / `hls-lan` 协议字段（现报空）

## 本机回放（不依赖 TUTK，已做）

- `GUI_playbackControl` → HDMI 墙钟回放
- `GUI_get/setPlaybackAudio` → `mhal_aout`
- `GUI_ChannelBackup*` → `rsdk_backup_export`
- 事件列表/日历 → meta + 连续轨
