# NOTE — nop 模块的去重与重连

本模块 = `NT12-SDK/nop_sdk` 的协议核，**已剥离**它原本 vendored 的第三方，去重到顶层 `third_party/`。

## 剥离了什么

| 原 `nop_sdk/third_party/` | 去向 |
|--------------------------|------|
| `onvif/`（ONVIF 客户端 8.3M） | 顶层 `third_party/happytime_onvif_rtsp`（同源） |
| `onvif_server/`（23M，即 rtsp-server 整包） | 顶层 `third_party/happytime_onvif_rtsp` |
| `tutk/`（glue 8K） | `components/cloud_tutk`（glue 归模块） |
| `cJSON/` | 顶层 `third_party/cJSON`（共享） |

同时删除了 `nop_sdk` 的 `build-default/ build-onvif/ build-onvif-server/ build-both/`（构建产物）。

## 保留了什么（NOP 自身实现）

`src/` 下：`base`(err/log/mem/json/map) · `nop`(envelope/router/longpoll) · `capability`(cap_registry) ·
`business/caps`(30 个能力 handler：device/stream/record/playback/ptz/cloud/ai/storage...) ·
`app`(façade) · `hal`/`osal`(硬件与 OS 抽象) · `client`/`config`/`services` · `transport`(mock) ·
`onvif`(adapter→Happytime) · `media`(rtsp_server/nop_media)。

## 重连（补 glue 时要做）

`components/nop/CMakeLists.txt` 里对 `third_party/*` 的相对路径原本指向 `nop_sdk/third_party/`，
现需重指到工程顶层 `../../third_party/`：

- `-DNOP_WITH_ONVIF=ON` 的 onvif 源路径 → `third_party/happytime_onvif_rtsp/source/onvif`
- `-DNOP_WITH_TUTK=ON` 的 tutk 库路径 → `third_party/tutk_sdk`
- cJSON → `third_party/cJSON`

默认 C-only 构建（onvif/tutk 关）不受影响，可先独立编译验证协议核。
