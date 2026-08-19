# 构建说明 — nvr_firmware（自包含工程）

本工程**自包含**：所有自研代码与第三方依赖（cJSON / ffmpeg / happytime / tutk）都在
`nvr_firmware/` 内，`third_party/cJSON` 为**唯一** cJSON 副本，各组件统一 `link cjson`。
一条命令即可在任意装好先决条件的机器上编译主机侧全部库。

## 先决条件（主机构建）
- CMake ≥ 3.13、gcc/g++（C11 / C++14）
- `libsqlite3-dev`（config 设置库 / recorder 元数据）
- `libssl-dev`（crypto: MD5/SHA/AES256）
- `python3`（recorder 编译期生成 rsdk_config.h）

Debian/Ubuntu：
```
sudo apt-get install -y build-essential cmake libsqlite3-dev libssl-dev python3
```

## 主机构建（编译自研库 + app 静态库）
```
cmake -S . -B build
cmake --build build -j
```
产出（`build/**/*.a`）：`cjson, sqlite3, nopcore, rsdk, nvr_settings, nvr_crypto,
nvr_dev_classify, nvr_onvif, nvr_storage, nvr_streaming, nvr_cloud_tutk,
nvr_cloud_uploader, nvr_app_core`。

> 主机单测已移除（2026-08-06，改为实机验证）。`nvr_app_core` 主机可编，作语法校验。
> `platform/media_hal` 与整机 `nvr_app` 仅目标机（需 na51090 hdal/BSP）。
> `components/nop` 的 demo 在聚合构建里默认关闭。

## 云环境（ServeDomainV2）
默认 **production** 域名。编 stage 固件加 `-DNVR_STAGE=ON`（两套独立固件，地址表在 `app/config/nvr_urls.h`）。
```
cmake -S . -B build_stage -DNVR_STAGE=ON
```

## 目标机交叉构建（aarch64，出整机固件 nvr_app）✅ 已跑通
工具链：`aarch64-ca53-linux-gnueabihf-8.4.01`（gcc 8.4，Novatek/Buildroot）。
BSP：na51090_linux_sdk（约 1.6G，含 aarch64 `libhdal.so`）。
```
cmake -S . -B build_arm \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-ca53.cmake \
  -DNVR_WITH_ONBOARD=ON \
  -DBSP_ROOT=/home/zack/NT12-32/NT98633/na51090_linux_sdk
cmake --build build_arm -j
# 产物：build_arm/app/nvr_app  (ELF aarch64, GNU/Linux 4.19)
```
- 工具链根可用 `-DNVR_TOOLCHAIN_ROOT=` 覆盖；`BSP_ROOT` 亦可用环境变量或 `platform/media_hal/bsp_ref.txt`。
- 第三方依赖已**工程内自带**：sqlite3(合并源) / openssl+curl 头 / happytime(源码编 `libhappytime_rtsp.a`) / TUTK+hdal(aarch64 预编译库)。交叉构建自动关掉主机自测/示例。
- **运行期依赖**（设备 rootfs 需提供）：`libssl.so.3 libcrypto.so.3 libcurl.so.4 libhdal.so` + 标准 C/C++ 运行库（原厂固件均已带）。
- ⚠️ 已知：本工程 openssl **头是 1.1.1**、设备 libcrypto 是 **3.x**——当前 EVP 用法两版兼容；若设备上有符号问题，换 openssl-3 头重编。
- 蓝牙(BLE)按要求本期未纳入。

## 目录（自包含边界）
```
nvr_firmware/
├── components/   nop · recorder · storage · streaming · onvif · cloud_tutk · config · crypto
├── platform/     media_hal（仅目标机；需 BSP）
├── app/          整机集成层（仅目标机）
├── third_party/  cJSON(唯一) · ffmpeg · happytime_onvif_rtsp · tutk_sdk
├── config/       运行期 JSON 默认值（首启种子）
└── docs/         ARCHITECTURE / SOURCE_MAP / BIND_IPC_FLOW / NOP_NONPASSTHROUGH_APIS
```
外部依赖仅两类：**系统库**（sqlite3/openssl，各机器安装）与 **na51090 BSP**（仅目标机）。
