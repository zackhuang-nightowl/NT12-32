# NVR Firmware — 顶层聚合构建（骨架）
# NT12-32 / Novatek NT98633 (NA51090, aarch64)
#
# 现状：聚合已成型的自研模块；app/ 与 storage/ 补齐后打通整机。
# 目标机交叉编译：-DCMAKE_TOOLCHAIN_FILE=<na51090 aarch64 toolchain>（见 platform/bsp_ref.txt）

cmake_minimum_required(VERSION 3.13)
project(nvr_firmware C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

# 主机构建先决条件（另见 BUILD.md）：cmake≥3.13, gcc/g++, libsqlite3-dev, libssl-dev, python3。
# 目标机(aarch64)构建 media_hal/app 另需 -DNVR_WITH_ONBOARD=ON -DBSP_ROOT=<na51090_linux_sdk>。

option(NVR_WITH_ONVIF   "启用 ONVIF（Happytime, 需 C++/openssl）" OFF)
option(NVR_WITH_ONBOARD "在目标机上构建（链接 hdal/platform）"   OFF)

# 云环境:默认 production；-DNVR_STAGE=ON 切 stage。域名表 app/config/nvr_urls.h（ServeDomainV2）。
option(NVR_STAGE        "使用预发(stage)云环境域名,默认生产(prod)" OFF)
if(NVR_STAGE)
    add_compile_definitions(NVR_BUILD_STAGE=1)
endif()

# --- 顶层第三方路径（各模块 glue 引用这里，替代原 nop_sdk/third_party）---
set(NVR_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party CACHE PATH "")
set(NVR_TUTK        ${NVR_THIRD_PARTY}/tutk_sdk)
set(NVR_FFMPEG      ${NVR_THIRD_PARTY}/ffmpeg)
set(NVR_CJSON       ${NVR_THIRD_PARTY}/cJSON)

# --- 共享 cJSON（工程内唯一副本，nop/config/app 统一 link cjson，避免重复符号）---
add_subdirectory(third_party/cJSON)

# --- vendored sqlite3（工程内合并源，主机/交叉统一；recorder/config link sqlite3）---
add_subdirectory(third_party/sqlite3)

# --- 运行期可写配置库（SQLite；叶子组件，app/nop 依赖）---
add_subdirectory(components/config)

# --- 密码/加密算法库（MD5/SHA/AES256；即插即用鉴权用）---
add_subdirectory(components/crypto)

# --- ① NOP 协议（自研，可独立编译）---
# 聚合构建里关掉 nop 的 demo/test；固件只需 nopcore 库。
set(NOP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NOP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
# 取流(GetStreamUri)与控制翻译对所有设备都走 ONVIF 客户端 → onboard 默认开 ONVIF。
if(NVR_WITH_ONBOARD AND NOT DEFINED NVR_WITH_ONVIF_SET)
    set(NVR_WITH_ONVIF ON CACHE BOOL "" FORCE)
endif()
if(NVR_WITH_ONVIF)
    set(NOP_WITH_ONVIF ON CACHE BOOL "" FORCE)   # 编 nop_onvif 客户端 + 点亮映射 NOP_ONVIF_MAP=1
endif()
add_subdirectory(components/nop)

# --- ⑤ 录像系统（自研，可独立编译）---
# 交叉构建里关掉 recorder 的 demo（meta_demo/cloud_demo 需链接 sqlite3；固件只需 librsdk）。
if(NVR_CROSS)
    set(RSDK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(components/recorder)

# --- ⑥ 文件存储（裸盘方案已定型，依赖 recorder）---
add_subdirectory(components/storage)

# --- ③ 拉流出图（glue 已实现，依赖 recorder + platform/media_hal + happytime）---
add_subdirectory(components/streaming)

# --- ② ONVIF glue（取流钩子 + 发现，依赖 nop 的 onvif 客户端）---
add_subdirectory(components/onvif)

# --- ④ TUTK glue（设备端 P2P，依赖 third_party/tutk_sdk）---
add_subdirectory(components/cloud_tutk)

# --- 云存上传引擎（事件切片→TS→VSaaS；依赖 recorder + libcurl）---
add_subdirectory(components/cloud_uploader)

# --- 平台层 + RTSP SDK（仅目标机构建）---
if(NVR_WITH_ONBOARD)
    add_subdirectory(platform/media_hal)                 # hd_videodec/videoproc/videoout 封装
    # 统一 RTSP SDK：仓库内 SDK/OnvifClientLibrary(nop::NopRtspClient facade + happytime 引擎源)。
    # 拉流(CRtspClient)与 server(NopRtspServer)统一在此;旧 third_party/happytime_onvif_rtsp 已移除。
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SDK/OnvifClientLibrary onvifclient_build)
endif()

# --- 集成主程序 ---
# 恒定加入：app 模块静态库(nvr_app_core)主机可编译（维护/CI 校验）；
# 整机可执行(nvr_app)在 app/CMakeLists 内由 NVR_WITH_ONBOARD 决定是否链接。
add_subdirectory(app)

message(STATUS "nvr_firmware: nop/recorder/storage/streaming/onvif/tutk/config/app + platform 已纳入")
