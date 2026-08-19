# ④ TUTK P2P — ODC command agent + 本机 live RTSP

手机 App 经 **NightOwl 定制的 TUTK 代理程序**（`AVAPIs_Server_CLI`）远程访问 NVR。
固件不自己调 `IOTC_Listen` / `nvr_tutk_init`。契约见 `APP_client_Agent.md`。

## 架构

```
App ──IOTC/AV + P2PTunnel──► AVAPIs_Server_CLI (/dvr/tutk_cloud_agent)
         │ popen nvr_tutk_cgi -s|-f <func> [-a json]
         │   -s getIotcAuthKey/...  → /User 身份
         │   -f startLiveStream/... → POST 127.0.0.1:6061/APPJsonCmd
         │                              └── nvr_cmd_dispatch
         │ PortMapping
         │   iotc-tunnel:6061 → localhost:6061  命令
         │   iotc-tunnel:8554 → localhost:8554  nvr_rtsp_live 直播+回放
         │   iotc-tunnel:7000 → localhost:7000  对讲
         │   iotc-tunnel:8089 → localhost:8089  GUI /eventSnap
```

`nvr_app` 按文档拉起：`device.sh <UID> <nvr_tutk_cgi> <profile>`（同一进程设 cgi/profile 再 `--start`；脚本自带看护）。

## 文件

| 路径 | 职责 |
|------|------|
| `deploy/rootfs/dvr/tutk_cloud_agent/device.sh` | `UID cgi profile` 同一进程 `--cgipath/--profilepath/--start`，掉线看护 |
| `deploy/rootfs/dvr/tutk_cloud_agent/profile.txt` | NVR 模板（`type=videoRecorder`）；运行期写 `/tmp/tutk_profile.txt` |
| `cgi/nvr_tutk_cgi.c` | agent 的 cgi。交叉编译产物 `nvr_tutk_cgi` |
| `src/nvr_rtsp_live.c` | 本机 :8554 RTSP（隧道 live + playback） |

`AVAPIs_Server_CLI` 必须是 NightOwl 用 **NA51090/aarch64 toolchain** 编出的二进制（MC6630 开发包不能上本机）。放到 `/dvr/tutk_cloud_agent/`。

## 出厂凭据（`/User/OWL/tutkdata.json`，OTA 不覆盖）

| 字段 | 出厂默认 |
|------|----------|
| IotcAuthKey | `00000000` |
| AvPassword | `888888` |
| UID | `/User/tutk_agent_udid`（产测写入） |

## NOP 命令（agent cgi / 6061）

- `get/setIotcAuthKey` · `get/setAvPassword` · `get/setIotcUID` · `getAvAccount`
- `getProfile` · `notifyLoginSuccess` · `notifySessionCount` · `getNotificationSetting`(501)
- `startLiveStream` → `rtsp://iotc-tunnel:8554/ch{N}_0.264`（主）/ `ch{N}_1.264`（子）
- `startPlayback` → `rtsp://iotc-tunnel:8554/playback/<startTime>` + duration
- `startSpeaker` → `tcp://iotc-tunnel:7000/speaker`（App POST `/cgi-bin/speaker`，无 HTTP 应答）
- `buildTunnel` → 随机 32 位 password + `iotc-channel:2`（与 agent 隧道联动）
