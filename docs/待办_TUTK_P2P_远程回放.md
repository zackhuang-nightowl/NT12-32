# TUTK / P2P — 已做与待办

> 2026-08-19：ODC agent + 本机 :8554 **直播和远程回放**已接线。P2P 会话仍由 `AVAPIs_Server_CLI` 负责。

## 已实现

- ODC agent：`device.sh` → `AVAPIs_Server_CLI`，cgi=`nvr_tutk_cgi`
- 隧道口：6061 命令 / 8089 GUI·缩略图 / **8554 live+playback** / 7000 对讲
- 直播：`startLiveStream` → `rtsp://iotc-tunnel:8554/ch{N}_{0|1}.264`
- 回放：`startPlayback` → `rtsp://iotc-tunnel:8554/playback/<startTime>`
  - RTP 拓展头 playbackStatus / playbackTimestamp
  - Seek：`SET_PARAMETER` `playback_ctrl: seek`（UTC year/month/day/hour/min/sec）后停推，等 PLAY
  - 无录像：2fps 空白帧（status=0），不自主 Seek
- 账户门控：`apply_remote_access`（owner / 出厂 / 本地 admin）
- `get/setIotcAuthKey`、`GUI_getUID`；出厂 AuthKey `00000000`

## 真机待回归

- App Timeline 首次 startPlayback、二次拖动 Seek、空白区间 No Record 叠图
- 主/子码流切换（streamType audioAndVideo vs audioAndSubVideo）
- 事件页按 duration 停、Timeline 忽略 duration 一直播

## 本机回放（不依赖 TUTK，已做）

- `GUI_playbackControl` → HDMI 墙钟回放
- `GUI_get/setPlaybackAudio` → `mhal_aout`
- `GUI_ChannelBackup*` → `rsdk_backup_export`
- 事件列表/日历 → meta + 连续轨
