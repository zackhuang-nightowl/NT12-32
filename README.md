# NT12-32 — NightOwl 16 路 PoE NVR 固件

> SoC：Novatek **NT98633 / NA51090**（4×Cortex-A53，aarch64，Linux 4.19，Buildroot）
> 规格：**32 通道**（16× PoE + 16× 数字/IP）· SPI-NAND 128MB · SATA HDD · HDMI 输出

NVR 作为**服务端**：对接相机（拉流/控制/事件）、驱动视频输出、录像存储、远程访问；
本机界面由独立 **LVGL** 进程经 NOP 接口（`127.0.0.1:8089 /APPJsonCmd`）驱动。

---

## 支持的功能

### 通道接入
- **16 路 PoE 即插即用**：eth1 内建 VLAN + DHCP（按口分 `198.18.<口>.100`），ONVIF 自动发现→分类→绑定→出图
- **16 路数字/LAN 通道**：独立 IP 相机（手动填 IP/URL 或 ONVIF 发现）
- **单设备多视频源**：一台设备多路 profile → 占多通道（鱼眼展开 / 多目 / 多 URL）
- **设备三分类**：NightOwl NOP · nopOnvif · 标准 ONVIF（按 scopes/MAC 自动识别）

### 实时预览出图
- RTSP 拉流 → **NA51090 硬件 VPU 硬解** → HDMI 输出（16 路硬解，不走软解）
- 多宫格 **1 / 4 / 9 / 16** 分屏、翻页、**宫格↔通道映射**、**视频悬浮块 (PIP)**
- 主/子码流自动切换（多宫格子码流、单画面全屏主码流）
- 显示布局由 LVGL 经 `GUI_setDeviceDisplayMode` / `DisplayExt` / `ChannelMapping` 驱动

### 录像
- 连续录像；**裸盘直写 (no-FS)** + **AES-256-CTR 加密** + 10MB 环形块 + 索引
- 多盘负载均衡、索引检索、跨盘回放
- **导出标准 MP4**（自研 muxer：H.264 avc1 / H.265 hvc1，moov 在尾）、抓拍 JPEG

### 存储管理
- 多盘发现 / 格式化（按容量自动布局）/ 装配 / 健康监测 / 热插拔 / 满盘策略

### 事件
- AI 事件接入：**移动 · 人形 · 人脸 · 车辆 · 动物 · 包裹 · 越线 · 区域入侵 · 门铃**
- 经 **longPolling** 状态位图（按类型 × 32 通道）主动上报 LVGL 界面；事件录像联动

### 云存
- 事件切片上传：取段 → MPEG-TS 封装 → VSaaS HTTPS 上传 → 回写状态

### 远程访问 & 协议
- **NOP 2.0** 服务端（8089 `/APPJsonCmd`，envelope → router → capability → handler）
- **TUTK P2P** 远程访问（IOTC / AVAPI）
- **ONVIF**：客户端（取流 / 发现 / PTZ 控制）+ 服务端（对 App 发流 RTSP）
- **NOP↔ONVIF 映射** 9 域：PTZ · OSD · 隐私遮挡 · 媒体编码 · AI 越线/入侵/物体检测 · 移动侦测 · 固件 等（含坐标系转译）

### 系统
- 网络：eth0 管理 · eth1 PoE 汇聚；时间 / NTP；账户鉴权 / 锁定
- OTA：A/B 双 rootfs（squashfs 只读，升级切分区）

---

## 目录

```
app/         整机集成层（通道 / 预览 / 录像调度 / 事件 / 配置 / 路由 / 网络时间）
components/  六大功能：nop · onvif · streaming · cloud_tutk · recorder · storage（+ config/crypto/cloud_uploader）
platform/    media_hal —— 封装 na51090 hdal（硬解 / 上屏）
third_party/ vendored 依赖：happytime(ONVIF/RTSP) · tutk_sdk · cJSON · sqlite3
config/      运行期 JSON（首启种子）；channels.json = 通道模型 + 显示映射
docs/        架构 / 来源追溯 / 现状 / 接口对照 / spec 与实现计划
```

## 构建

```bash
# 主机（编库 + 自测）
cmake -S . -B build && cmake --build build -j && ctest --test-dir build

# 目标机（aarch64 整机固件 nvr_app）
cmake -S . -B build_arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-ca53.cmake \
  -DNVR_WITH_ONBOARD=ON -DBSP_ROOT=<na51090_linux_sdk>
cmake --build build_arm -j
```
详见 [BUILD.md](BUILD.md)。部署见 [deploy/README.md](deploy/README.md)。

## 文档

- 架构与数据流：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- 代码级现状（哪些真实接线 / 待接线）：[docs/实际进度_代码审计_2026-08-03.md](docs/实际进度_代码审计_2026-08-03.md)
- 来源追溯：[docs/SOURCE_MAP.md](docs/SOURCE_MAP.md)
