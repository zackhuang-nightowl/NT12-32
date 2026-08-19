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
    ${CMAKE_CURRENT_SOURCE_DIR}/playback
    ${CMAKE_CURRENT_SOURCE_DIR}/talk
    ${CMAKE_CURRENT_SOURCE_DIR}/nop8012
    ${NVR_ROOT}/third_party/curl/include        # curl.h（转发到设备用；host 用系统头）
    ${NVR_ROOT}/third_party/openssl/include     # nvr_ota.c 交叉编译用 vendored openssl 头(host 用系统头)
    ${NVR_ROOT}/components/streaming/include
    ${NVR_ROOT}/components/storage/include
    ${NVR_ROOT}/components/recorder/include
    ${NVR_ROOT}/components/config/include
    ${NVR_ROOT}/components/onvif/include
    ${NVR_ROOT}/components/crypto/include
    ${NVR_ROOT}/components/nop/include
    ${NVR_ROOT}/components/nop/src              # nop_onvif_map.h(公共)转引 nop/nop_envelope.h(私有 src)
    ${NVR_ROOT}/components/cloud_uploader/include
    ${NVR_ROOT}/components/cloud_tutk/include
    ${NVR_ROOT}/platform/media_hal
    ${NVR_ROOT}/third_party/cJSON
)

# --- app 模块静态库（主机可编译；静态库不需解析平台符号）---
add_library(nvr_app_core STATIC
    src/nvr_app.c
    config/nvr_config.c
    config/nvr_gui_config.c
    channel/nvr_channel.c
    channel/nvr_chan_nop_sync.c
    channel/nvr_chan_bind.c
    preview/nvr_preview.c
    record_sched/nvr_record_sched.c
    event/nvr_event.c
    nop8012/nvr_nop8012.c
    nop8012/nvr_nop8012_proto.c
    router/nvr_cmd_router.c
    router/nvr_cmd_util.c
    router/nvr_cmd_table.c
    router/nvr_cmd_display.c
    router/nvr_cmd_lan.c
    router/nvr_cmd_system.c
    router/nvr_cmd_storage.c
    router/nvr_cmd_cloud.c
    router/nvr_cmd_record.c
    router/nvr_cmd_ota.c
    router/nvr_cmd_event.c
    router/nvr_cmd_account.c
    router/nvr_cognito.c
    router/nvr_graphql.c
    router/nvr_resetcode.c
    router/nvr_cmd_network.c
    router/nvr_cmd_misc.c
    config/nvr_chan_persist.c
    channel/nvr_chan_status.c
    ota/nvr_ota.c
    netime/nvr_netime.c
    ble/nvr_ble.c
    playback/nvr_playback.c
    talk/nvr_talk.c
    router/nvr_cmd_playback.c
    router/nvr_cmd_p2p.c
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
        nvr_streaming onvifclient
        nvr_storage rsdk nvr_settings nvr_crypto sqlite3
        nvr_onvif nvr_cloud_uploader mhal cjson
        nopcore_static nvr_cloud_tutk
        ${_tutk}/libssl.so ${_tutk}/libcrypto.so
        ${_ht}/libsrt/lib/arm64-v8a/libsrt.a ${_ht}/libsrtp/lib/arm64-v8a/libsrtp2.a
        pthread m dl z)
    # nop 的 ONVIF 客户端 与 happytime_rtsp 同为 Happytimesoft，共享 bm/http 工具符号(base64/md5/
    # http_onvif_trans 等)会重复定义。二者实现同源，允许重复定义(取先出现的)。
    target_link_options(nvr_app PRIVATE -Wl,--allow-multiple-definition)
endif()

