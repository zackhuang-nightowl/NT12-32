# 待办(暂不实现,后续接入 P2P 时做)—— 2026-08-07

本轮聚焦**单机本地功能**。以下依赖 TUTK/P2P 的远程能力**暂不实现**,记录待办:

## 1. TUTK / P2P 相关(全部暂缓)
- 远程看画面(App 经 P2P 隧道拉 live)。
- 远程回放推流:`startPlayback` 返回 `rtsp://iotc-tunnel:...` —— 需从零建**回放 RTSP 服务器**
  (rsdk_play 帧 → RTP/RTSP 打包 → 经 TUTK 隧道给 App),固件当前**只有 RTSP client、无 server**。
- `getPlaybackCapabilities` 的 `rtsp-iotc-tunnel`/`hls-lan` 协议。
- 云存(cloud upload)相关远程链路。

## 2. 本地回放(本轮做)—— 不依赖 TUTK
- GUI 用本地命令 `GUI_playbackControl`(action=play/pause/stop/seek,speed,direction,startTime)
  在**本机 HDMI 屏**回放:NVR 用 `rsdk_index_query` 找段 → `rsdk_play_*` 取 Annex-B 帧 →
  `mhal_vdec_send` 解码上屏(复用 live 的解码+显示窗,源换成录像)。
- `GUI_getFileList` → 接 `rsdk_index_query` 返回真实录像时间段(时间轴)。
- 布局用 `GUI_setPlaybackMode`(displayMode + channels[])。

## 3. 未定/后续
- 倍速(2X/4X/…)+ 倒放(backward,需 GOP 逆序)——先正放 1X,再迭代。
- 多通道同步回放(共享时间轴)。
- 备份 `GUI_ChannelBackupFiles`/`StopChannelBackup` → 接 `rsdk_backup_export_seg`(导出到 U 盘/SD)。
