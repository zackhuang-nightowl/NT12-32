# 来源追溯 — SOURCE_MAP

每个模块的原始出处，便于回原 SDK 对照、升级、或补齐。
原始根：`NT12-SDK/`、`NT98633/na51090_linux_sdk/`。

## components/ （自研 + glue）

| 模块 | 抽取自 | 处理 | 备注 |
|------|--------|------|------|
| `nop/` | `NT12-SDK/nop_sdk/` | 复制 src/include/ports/tests/cmake；**剥离** `third_party/`(onvif/onvif_server/tutk 去重到顶层) 与所有 `build-*/` | 见 `components/nop/NOTE_dedup.md` |
| `recorder/` | `NT12-SDK/recorder_sdk/` | 整包，去 `build/` | librsdk，独立可编译 |
| `onvif/` | —（glue） | ✅ 已实现 | `nvr_onvif.c` + `nvr_dev_classify.c`；协议在 happytime |
| `streaming/` | —（glue） | ✅ 已实现 | 主+子常拉；目标机 `NopRtspClient`（NT12-SDK/OnvifClientLibrary），happytime 仍作依赖 |
| `cloud_tutk/` | — | ✅ | `nvr_rtsp_live.c` + `nvr_tutk_cgi.c`；P2P 走 ODC `AVAPIs_Server_CLI` |
| `storage/` | —（glue） | ✅ 已实现 | 裸盘方案 + 盘发现/装配/热插拔 |
| `config/` | — | ✅ 已实现 | SQLite 设置库 `nvr_settings` |
| `crypto/` | — | ✅ 已实现 | MD5/SHA/AES256 |
| `cloud_uploader/` | — | ✅ 已实现 | TS 封装 + VSaaS HTTPS 上传 |

## third_party/ （intact 上游）

| 目录 | 抽取自 | 处理 |
|------|--------|------|
| `happytime_onvif_rtsp/` | `NT12-SDK/rtsp-server/` | 整包，**剥离** `source/ffmpeg`(→下)、`source/openssl`(用系统 openssl) |
| `ffmpeg/` | `NT12-SDK/rtsp-server/source/ffmpeg/` | 只留 `include/` + `lib/arm64-v8a/`（丢 armeabi-v7a/linux-x86，非目标架构） |
| `tutk_sdk/` | `NT12-SDK/TUTK_SDK_classic_.../` | 只留 `Include/` + `Lib/Linux/ArmCortexA53_NT98633_8.4.0/`（本机 SoC，弃其余 ~200 个平台变体 1.8G） |
| `cJSON/` | `NT12-SDK/nop_sdk/third_party/cJSON/` | 提升为顶层共享 |

## platform/ （硬件适配）

| 项 | 对应 |
|----|------|
| `media_hal/` | 封装 `na51090_linux_sdk/code/hdal/include/hd_videodec.h / hd_videoout.h / hd_videoenc.h` |
| `bsp_ref.txt` | 指向 `NT98633/na51090_linux_sdk`（BSP 1.6G + hdal 307M，**不复制**，交叉编译时引用） |

## 重要去重说明

`nop_sdk` 原本在自己的 `third_party/` 里 vendored 了三份东西，**已在本工程去重**：

| nop_sdk 原 vendored | 本工程对应 | 说明 |
|--------------------|-----------|------|
| `third_party/onvif/`（ONVIF 客户端库 8.3M） | `third_party/happytime_onvif_rtsp` | 同源 Happytime |
| `third_party/onvif_server/`（23M） | `third_party/happytime_onvif_rtsp` | **就是** `rtsp-server` 整包 |
| `third_party/tutk/`（glue 8K） | `components/cloud_tutk/`（glue） | glue 归模块 |
| `third_party/cJSON/` | `third_party/cJSON/` | 提升共享 |

→ `components/nop` 的 `CMakeLists.txt` 里对 `third_party/*` 的相对路径**需要重指到顶层 `third_party/`**（Review 后随 glue 一起改）。详见 `components/nop/NOTE_dedup.md`。

## 未纳入本工程的原始资产（按需回取）

- na51090 BSP 全量（kernel/u-boot/atf/toolchain/hdal 源码）：留在原处，交叉编译时引用。
- TUTK 其余平台库、Sample 65M：非本机架构，未取。
- Happytime 的 openssl/libsrt 预编译：用系统库或按需回取。
- `SDK_NEW/nop_client`：**另一套更贴设备的 NOP 实现**（含 `bind_ipc/liveview/playback/smart_detect` 业务文件）；与 `components/nop`(SDK 版) 二选一或互补，Review 后定。
