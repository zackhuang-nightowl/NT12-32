# ④ TUTK P2P (cloud_tutk)

手机 App 经 **P2PTunnel 端口映射** 远程访问 NVR（非 AV 裸推流）。

## 架构

```
App ──P2PTunnel(UID + IOTC authkey)──► nvr_tutk (IOTC_Listen + P2PTunnelServer_Listen)
         │ PortMapping(remote=8089) ──► localhost:8089  NOP 命令路由
         │ PortMapping(remote=554)  ──► localhost:554   nvr_rtsp_live (子码流 RTP)
```

## 文件

| 文件 | 职责 |
|------|------|
| `src/nvr_tutk.c` | IOTC 登录(authkey) + P2PTunnel 会话 + 端口白名单 |
| `src/nvr_rtsp_live.c` | 本地 RTSP/RTP 服务(供隧道映射推 live) |
| `include/nvr_tutk.h` | 设备端 API + `nvr_tutk_cfg_t` |

## 设置库 KV

| Key | 说明 |
|-----|------|
| `tutk.uid` | TUTK UID |
| `tutk.authkey` | 8 字符 IOTC key（`getIotcAuthKey` / `setIotcAuthKey`） |
| `tutk.license_key` | 可选 `TUTK_SDK_Set_License_Key` |
| `tutk.max_sessions` | 最大并发 P2P 会话，默认 8 |

## NOP 命令

- `getIotcAuthKey` → 读 `tutk.authkey`
- `setIotcAuthKey` → 写 `tutk.authkey`（8 位字母数字），触发 `nvr_app` 热更新或重启 TUTK
- `GUI_getUID` → `tutk.uid` + `system.sn` + `system.mac`

## 构建

目标机需 `third_party/tutk_sdk/Lib/.../libIOTCAPIs.so`；主机无库时编 stub（`NVR_HAVE_TUTK=0`）。
