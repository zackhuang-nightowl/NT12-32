# src/services/server/ — 设备→服务器（出站）交互

本文件夹集中放置**所有与外部服务器交互**的实现，对应 `nop_doc/APIs/Server_*`。
它们都是设备主动发起的出站请求（HTTPS，TLS1.2，端口 443），共享同一套出站原语，
与入站服务（8012/RTSP/HTTP 命令服务端）分离。

## 共享原语
- `server_request.c` / `include/nop_sdk/nop_server_request.h`
  - `nop_server_http_get()` —— 明文 HTTP GET（原始 socket）。
  - `nop_http_fetch_fn` —— 可插拔 fetch；生产环境注入 TLS 实现（保持内核无 TLS 依赖）。
  - 所有 Server 模块复用它做实际网络收发。

## Server 模块（对应 nop_doc/APIs/Server_*.md）
| 模块 | 文件 | 状态 | 关键端点/参数 |
|------|------|------|------|
| **Server_OTA** | `server_request.c`（nop_ota_*） | ✅ 已实现 | `<urlBase>/<company>/<product>/<model>`；版本号 `Model_x.y.z` 比对 |
| **Server_PushNotification** | `server_push.c`（待加） | ⏳ | 图片上传 `https://smart-notifications.nowlsp.com/filestorage/api/v1/upload`（stg: `asia-upload-tutk-stg.kalay.us`）；push `https://push-staging.kalay.us/tpns`；带 UID；发送间隔 ≥1s。**由 `svc_push` 策略引擎的 `nop_push_send_fn` 回调驱动** |
| **Server_NOP_Account** | `server_account.c`（待加） | ⏳ | AWS Cognito / GraphQL：prod `https://protect.nowlsp.com/graphql`（pool oX4P9V0Ig, region us-east-1, client 7a7t4ds667njvemo0e6aobotce）；stg `protect-staging.nowlsp.com` |
| **Server_GoogleHome** | `server_googlehome.c`（待加） | ⏳ | Google Home / Chromecast 对接（多为云侧/固件侧） |

## 约定
- 每个 Server 模块一个 `server_<name>.c` + 公开头 `include/nop_sdk/nop_server_<name>.h`。
- 网络收发统一走 `nop_server_http_get` / `nop_http_fetch_fn`；不要在本层直接内联 TLS。
- 出站策略（静默/限频/去重）在 `svc_push` 等事件侧引擎，本层只负责"发到服务器"。
- 环境区分 stage/production 由 provisioning（`nop_config`）与烧录的 UID 决定。

> 分工：出站"服务器请求类"由用户实现；本文件夹为其统一落点，OTA 已作为范例落地。
