# app — 整机集成层。
#  nvr_app_core: 各 app 模块的静态库 —— **主机可编译**（只编译不链平台），用于 CI/维护校验。
#  nvr_app     : 整机可执行 —— 仅目标机(NVR_WITH_ONBOARD)链接 mhal(hdal)+streaming(happytime)。
cmake_minimum_required(VERSION 3.13)
project(nvr_app C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

set(NVR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# 共享 cJSON（复用 cjson 目标；独立构建时创建）
if(NOT TARGET cjson)
    add_subdirectory(${NVR_ROOT}/third_party/cJSON ${CMAKE_CURRENT_BINARY_DIR}/cjson_build)
endif()

# --- app 模块公共包含目录（组件对外头）---
set(NVR_APP_INCLUDES
    ${NVR_ROOT}/common/include            # nvr_log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/channel
    ${CMAKE_CURRENT_SOURCE_DIR}/preview
    ${CMAKE_CURRENT_SOURCE_DIR}/record_sched
    ${CMAKE_CURRENT_SOURCE_DIR}/event
    ${CMAKE_CURRENT_SOURCE_DIR}/router
    ${CMAKE_CURRENT_SOURCE_DIR}/ota
    ${CMAKE_CURRENT_SOURCE_DIR}/netime
    ${CMAKE_CURRENT_SOURCE_DIR}/ble
    ${NVR_ROOT}/third_party/curl/include        # curl.h（转发到设备用；host 用系统头）
    ${NVR_ROOT}/components/streaming/include
    ${NVR_ROOT}/components/storage/include
    ${NVR_ROOT}/components/recorder/include
    ${NVR_ROOT}/components/config/include
    ${NVR_ROOT}/components/onvif/include
    ${NVR_ROOT}/components/nop/include
    ${NVR_ROOT}/components/cloud_uploader/include
    ${NVR_ROOT}/components/cloud_tutk/include
    ${NVR_ROOT}/platform/media_hal
)

# --- app 模块静态库（主机可编译；静态库不需解析平台符号）---
add_library(nvr_app_core STATIC
    src/nvr_app.c
    config/nvr_config.c
    channel/nvr_channel.c
    preview/nvr_preview.c
    record_sched/nvr_record_sched.c
    event/nvr_event.c
    router/nvr_cmd_router.c
    ota/nvr_ota.c
    netime/nvr_netime.c
    ble/nvr_ble.c
)
target_include_directories(nvr_app_core PUBLIC ${NVR_APP_INCLUDES})
target_link_libraries(nvr_app_core PUBLIC cjson)
# 依赖 recorder 生成 rsdk_config.h（rsdk.h 需要）
if(TARGET rsdk)
    add_dependencies(nvr_app_core rsdk)
endif()

# --- 整机可执行（仅目标机）---
if(NVR_WITH_ONBOARD)
    add_executable(nvr_app src/main.c)
    target_include_directories(nvr_app PRIVATE ${NVR_APP_INCLUDES})
    # happytime 依赖：openssl(HTTPS,用 TUTK aarch64 .so) + libsrt/libsrtp(arm64-v8a 预编译)
    set(_ht ${NVR_ROOT}/third_party/happytime_onvif_rtsp/source)
    set(_tutk ${NVR_ROOT}/third_party/tutk_sdk/Lib/Linux/ArmCortexA53_NT98633_8.4.0)
    target_link_libraries(nvr_app PRIVATE
        nvr_app_core
        nvr_streaming happytime_rtsp
        nvr_storage rsdk nvr_settings nvr_crypto sqlite3
        nvr_onvif nvr_cloud_uploader mhal cjson
        nopcore_static nvr_cloud_tutk
        ${_tutk}/libssl.so ${_tutk}/libcrypto.so
        ${_ht}/libsrt/lib/arm64-v8a/libsrt.a ${_ht}/libsrtp/lib/arm64-v8a/libsrtp2.a
        pthread m dl)
    # nop 的 ONVIF 客户端 与 happytime_rtsp 同为 Happytimesoft，共享 bm/http 工具符号(base64/md5/
    # http_onvif_trans 等)会重复定义。二者实现同源，允许重复定义(取先出现的)。
    target_link_options(nvr_app PRIVATE -Wl,--allow-multiple-definition)
endif()

# --- BLE 分包/组包单测（主机可运行；不需 BT 硬件）---
if(NOT NVR_CROSS)
    add_executable(test_nvr_ble ble/test_nvr_ble.c ble/nvr_ble.c)
    target_include_directories(test_nvr_ble PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ble ${NVR_ROOT}/common/include)
    add_test(NAME test_nvr_ble COMMAND test_nvr_ble)

    # 解码吞吐预算准入单测（纯 C，直接编 mhal_budget.c；无需 hdal）
    add_executable(test_mhal_budget ${NVR_ROOT}/platform/media_hal/test_mhal_budget.c
                                    ${NVR_ROOT}/platform/media_hal/mhal_budget.c)
    target_include_directories(test_mhal_budget PRIVATE ${NVR_ROOT}/platform/media_hal)
    target_link_libraries(test_mhal_budget PRIVATE pthread)
    add_test(NAME test_mhal_budget COMMAND test_mhal_budget)

    # channels.json 原子持久化单测（主机可运行；纯 C + cjson）
    add_executable(test_nvr_chan_persist
        config/test_nvr_chan_persist.c config/nvr_chan_persist.c)
    target_include_directories(test_nvr_chan_persist PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/config ${NVR_ROOT}/common/include
        ${NVR_ROOT}/third_party/cJSON)
    target_link_libraries(test_nvr_chan_persist PRIVATE cjson)
    add_test(NAME test_nvr_chan_persist COMMAND test_nvr_chan_persist)

    # 通道状态 0-7 纯映射单测（主机可运行；纯 C，无重依赖）
    add_executable(test_nvr_chan_status
        channel/test_nvr_chan_status.c channel/nvr_chan_status.c)
    target_include_directories(test_nvr_chan_status PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/channel)
    add_test(NAME test_nvr_chan_status COMMAND test_nvr_chan_status)

    # 预览布局/映射/悬浮块单测（主机可运行；mock mhal_vout + streaming）
    add_executable(test_nvr_preview
        preview/test_nvr_preview.c preview/nvr_preview.c
        preview/test/mock_mhal_vout.c preview/test/mock_streaming.c)
    target_include_directories(test_nvr_preview PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/preview ${CMAKE_CURRENT_SOURCE_DIR}/preview/test
        ${NVR_ROOT}/platform/media_hal ${NVR_ROOT}/components/streaming/include
        ${NVR_ROOT}/common/include ${NVR_ROOT}/components/recorder/include)
    add_test(NAME test_nvr_preview COMMAND test_nvr_preview)
endif()
