# NVR Firmware — NT12-32 (Novatek NT98633 / NA51090)

把实现这台 16 路 PoE NVR 固件所需的**全部代码**收拢到一个可维护工程里。
按 **分层 + 六大功能模块** 组织；第三方库 intact 隔离在 `third_party/`，平台强相关（hdal/BSP）隔离在 `platform/`，本项目自研与集成代码在 `components/` 与 `app/`。

> 设备：NightOwl **NT12-32**，SoC **Novatek NT98633 = NA51090**（4×Cortex-A53，Linux 4.19，aarch64/Buildroot）。
> 本工程只做**抽取与架构定型**，业务集成层（`app/`）与文件存储（`components/storage/`）为**待补充**骨架。

---

## 六大功能 → 模块映射

| # | 功能 | 模块 | 实现来源 | 状态 |
|---|------|------|----------|------|
| ① | **NOP 协议** | [`components/nop/`](components/nop/) | 自研 `nop_sdk`（协议核+能力网关+业务 handler） | ✅ 可编译，已单测 |
| ② | **ONVIF 协议** | [`components/onvif/`](components/onvif/) | glue（`nop_onvif_*` 客户端）+ `third_party/happytime_onvif_rtsp` | ✅ **glue 已实现**（取流钩子点亮 PoE 自动取流）|
| ③ | **拉流出图** | [`components/streaming/`](components/streaming/) | glue：Happytime `CRtspClient` 拉流 → `platform/media_hal` 硬解 → 录像 | ✅ **glue 已实现**（对真实 CRtspClient 头编译通过）|
| ④ | **TUTK P2P** | [`components/cloud_tutk/`](components/cloud_tutk/) | glue（设备端 IOTC+AV）+ `third_party/tutk_sdk`（NT98633 库） | ✅ **glue 已实现**（登录/监听/推流）|
| ⑤ | **录像系统** | [`components/recorder/`](components/recorder/) | 自研 `recorder_sdk`（librsdk） | ✅ 8 example 全实测 PASS |
| ⑥ | **文件存储** | [`components/storage/`](components/storage/) | 自研盘管理层（发现/识别/格式化编排/装配/健康/热插拔/防挂载），裸盘路线复用 recorder | ✅ **裸盘方案已定型**，骨架编译通过 |

---

## 目录结构

```
nvr_firmware/
├── app/                      整机集成层（相当于原厂 LocalHMI）—— 自研，骨架/待补充
│   ├── channel/              通道管理：IPC 增删、绑定、在线状态机
│   ├── preview/              预览分屏布局编排（1/4/9/16 分屏）
│   ├── record_sched/         录像调度 + 满盘策略编排
│   ├── event/                事件联动：AI → 录像/抓拍/推送
│   └── config/               配置管理（INI/JSON）
├── components/               六大功能模块（见上表）
│   ├── nop/  onvif/  streaming/  cloud_tutk/  recorder/  storage/
├── platform/                NA51090/NT98633 硬件适配层
│   ├── media_hal/            hd_videodec / hd_videoout / hd_videoenc 薄封装
│   └── bsp_ref.txt           → 指向 na51090_linux_sdk（BSP 1.6G 不复制）
├── third_party/             第三方 intact 依赖（可整体替换/升级）
│   ├── happytime_onvif_rtsp/ ONVIF+RTSP 全套（含 CRtspClient 拉流）
│   ├── tutk_sdk/             TUTK Include + NT98633 库
│   ├── ffmpeg/              arm64-v8a 预编译（软解/封装兜底）
│   └── cJSON/
├── config/                  各模块运行期配置（JSON）+ 通道模型
│   ├── channels.json         ⭐ PoE + 数字通道 + 单设备多视频源 → 通道映射
│   ├── system/streaming/onvif/nop/cloud_tutk/storage/recorder.json
│   └── README.md             通道模型（device→source→channel）+ 加载顺序
├── docs/
│   ├── ARCHITECTURE.md       分层、数据流、依赖图
│   └── SOURCE_MAP.md         每个文件/模块的来源追溯
└── CMakeLists.txt            顶层聚合构建
```

## 分层（自顶向下）

```
app/                 业务/整机集成（通道、预览、录像调度、事件、配置）
  │  只调下层稳定接口
components/           功能模块（NOP / ONVIF / 拉流 / TUTK / 录像 / 存储）
  │  只依赖 platform 抽象，不直接触碰 hdal 私有 API
platform/            硬件抽象（media_hal 封装 na51090 hdal）
  │
third_party/ + BSP   Happytime / TUTK / ffmpeg / na51090 BSP(kernel+hdal)
```

依赖只允许**自上而下**；模块之间通过 `app/` 编排，不横向硬耦合。详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 构建（现状）

- 各自研模块自带 CMake：`components/nop`、`components/recorder` 可独立 `cmake -S . -B build && cmake --build build`。
- 顶层 `CMakeLists.txt` 为聚合骨架，`app/` 与 `storage/` 补齐后打通整机构建。
- 目标机交叉编译用 `platform/bsp_ref.txt` 指向的 na51090 工具链。

## 给 Review 的三条主线

1. **拉流出图链路**（③）是整机唯一"跨了三个模块+平台"的关键路径：
   `CRtspClient.video_cb → platform/media_hal(hd_videodec) → hd_videoout(HDMI预览) + recorder(录像)`。
   见 [components/streaming/README.md](components/streaming/README.md)。
2. **NOP 已内含 onvif/tutk 适配器**（`components/nop/src/onvif`、`src/media`、`cap_cloud`），
   与 ②④ 模块是"协议入口↔具体实现"的关系，别重复造。见 [docs/SOURCE_MAP.md](docs/SOURCE_MAP.md)。
3. **文件存储 ⑥** 待你定方向：走 recorder_sdk 的裸盘直写，还是标准 ext4 文件（原机方案）。
   见 [components/storage/README.md](components/storage/README.md)。
