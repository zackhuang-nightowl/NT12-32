cmake_minimum_required(VERSION 3.10)

project(nop_sdk
    VERSION 0.1.0
    DESCRIPTION "NOP protocol SDK for NightOwl IPC/NVR (cross-platform, C11)"
    LANGUAGES C)

# ---------------------------------------------------------------------------
# Options (component trimming, lwIP lwipopts.h style)
# ---------------------------------------------------------------------------
set(NOP_OSAL_PORT "posix" CACHE STRING "OSAL port: posix | linux_uclibc | windows")
option(NOP_BUILD_SHARED   "Build shared library"            ON)
option(NOP_BUILD_STATIC   "Build static library"            ON)
option(NOP_BUILD_EXAMPLES "Build example programs"          ON)
option(NOP_WITH_HAL_STUB  "Include print-stub HAL (runs with no hardware)" ON)
option(NOP_WITH_TUTK      "Enable built-in TUTK P2P backend (requires SDK)" OFF)
option(NOP_WITH_ONVIF_SERVER "Enable device-side ONVIF server (vendored Happytimesoft)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(NOP_SRC
    src/base/nop_err.c
    src/base/nop_log.c
    src/base/nop_mem.c
    src/base/nop_json.c
    src/base/nop_map.c
    src/nop/nop_envelope.c
    src/nop/nop_router.c
    src/nop/nop_longpoll.c
    src/capability/cap_registry.c
    src/transport/transport_mock.c
    src/hal/hal_registry.c
    src/business/business_register.c
    src/business/nop_detect_types.c
    src/business/cap_helpers.c
    src/business/caps/cap_device.c
    src/business/caps/cap_auth.c
    src/business/caps/cap_system.c
    src/business/caps/cap_stream.c
    src/business/caps/cap_ptz.c
    src/business/caps/cap_light.c
    src/business/caps/cap_storage.c
    src/business/caps/cap_record.c
    src/business/caps/cap_audio.c
    src/business/caps/cap_ota.c
    src/business/caps/cap_floodlight.c
    src/business/caps/cap_video.c
    src/business/caps/cap_network.c
    src/business/caps/cap_push.c
    src/business/caps/cap_event.c
    src/business/caps/cap_ai.c
    src/business/caps/cap_cloud.c
    src/business/caps/cap_doorbell.c
    src/business/caps/cap_misc.c
    src/business/caps/cap_gui_account.c
    src/business/caps/cap_gui_network.c
    src/business/caps/cap_gui_system.c
    src/business/caps/cap_gui_lan.c
    src/business/caps/cap_gui_playback.c
    src/business/caps/cap_ptz_patrol.c
    src/business/caps/cap_ai_advanced.c
    src/business/caps/cap_agent.c
    src/business/caps/cap_misc_ext.c
    src/app/nop_app.c
    src/client/nop_client.c
    src/client/channel_loopback.c
    src/transport/transport_unix.c
    src/transport/http_server.c
    src/media/nop_media.c
    src/services/svc_event.c
    src/services/svc_push.c
    src/services/svc_record.c
    src/services/svc_ptz_patrol.c
    src/services/svc_chime.c
    src/services/camera/svc_snapshot.c
    src/services/camera/svc_light_linkage.c
    src/services/camera/svc_sdcard_upgrade.c
    src/services/camera/svc_system_events.c
    src/services/nvr/svc_event8012_client.c
    src/services/nvr/svc_nvr_recorder.c
    src/services/nvr/svc_nvr_channels.c
    src/services/nvr/svc_nvr_addcamera.c
    src/services/nvr/svc_wizard.c
    src/services/nvr/svc_analog_camera.c
    src/config/nop_config.c
    src/services/server/server_request.c
    # ONVIF↔NOP 映射层（纯 C，始终编入；NOP_ONVIF_MAP=1 时真正翻译，否则透传）
    src/onvif/mapping/onvif_coord.c
    src/onvif/mapping/onvif_map_utils.c
    src/onvif/mapping/onvif_map_json.c
    src/onvif/mapping/onvif_map_table.c
    src/onvif/mapping/onvif_map_dispatch.c
    src/onvif/mapping/onvif_map_session.c
    src/onvif/mapping/onvif_map_ptz.c
    src/onvif/mapping/onvif_map_ptz_ext.c
    src/onvif/mapping/onvif_map_privacy.c
    src/onvif/mapping/onvif_map_osd.c
    src/onvif/mapping/onvif_map_media.c
    src/onvif/mapping/onvif_map_ai.c
    src/onvif/mapping/onvif_map_motion.c
    src/onvif/mapping/onvif_map_firmware.c)

# cJSON: 统一用共享 cjson 静态库（工程内 third_party/cJSON，唯一副本）。
# 独立构建(本目录不含 cJSON)时回退到顶层 third_party/cJSON 并按需创建 cjson 目标。
if(NOT DEFINED NOP_CJSON_DIR)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON/cJSON.c)
        set(NOP_CJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON)
    else()
        set(NOP_CJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cJSON)
    endif()
endif()
if(NOT TARGET cjson)
    add_library(cjson STATIC ${NOP_CJSON_DIR}/cJSON.c)
    target_include_directories(cjson PUBLIC ${NOP_CJSON_DIR})
    set_target_properties(cjson PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
endif()

# OSAL port selection
if(NOP_OSAL_PORT STREQUAL "windows")
    list(APPEND NOP_SRC ports/windows/osal_windows.c)
else() # posix and linux_uclibc share the POSIX implementation
    list(APPEND NOP_SRC ports/posix/osal_posix.c)
endif()

if(NOP_WITH_HAL_STUB)
    list(APPEND NOP_SRC ports/stub/hal_stub.c)
endif()

# ---------------------------------------------------------------------------
# Object library shared by static + shared targets
# ---------------------------------------------------------------------------
add_library(nopcore_obj OBJECT ${NOP_SRC})
target_include_directories(nopcore_obj
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/onvif/mapping   # onvif_map_internal.h / nop_onvif_map.h
            ${NOP_CJSON_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/ports/stub)

if(MSVC)
    target_compile_options(nopcore_obj PRIVATE /W3)
else()
    target_compile_options(nopcore_obj PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

set(NOP_LINK_LIBS "")
if(NOT NOP_OSAL_PORT STREQUAL "windows")
    list(APPEND NOP_LINK_LIBS pthread m)   # m: 映射层坐标变换用 <math.h>
endif()

# 映射层始终编译；NOP_WITH_ONVIF 时点亮 NOP_ONVIF_MAP=1 让 dispatch 真正翻译成 ONVIF
if(NOP_WITH_ONVIF)
    target_compile_definitions(nopcore_obj PRIVATE NOP_ONVIF_MAP=1)
endif()

# ---------------------------------------------------------------------------
# ONVIF client (vendored Happytimesoft library + thin C++ adapter)
# Vendored sources in third_party/onvif/ are compiled UNMODIFIED. The adapter
# (src/onvif/onvif_adapter.cpp) is the only ONVIF contact point. Scoped in its
# own object library so the C core never sees ONVIF includes/defines.
# ---------------------------------------------------------------------------
set(NOP_ONVIF_OBJECTS "")
if(NOP_WITH_ONVIF)
    enable_language(CXX)
    set(NOP_ONVIF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onvif)
    set(NOP_ONVIF_SRC
        ${NOP_ONVIF_DIR}/bm/base64.cpp     ${NOP_ONVIF_DIR}/bm/hqueue.cpp
        ${NOP_ONVIF_DIR}/bm/hxml.cpp       ${NOP_ONVIF_DIR}/bm/linked_list.cpp
        ${NOP_ONVIF_DIR}/bm/net_util.cpp   ${NOP_ONVIF_DIR}/bm/ppstack.cpp
        ${NOP_ONVIF_DIR}/bm/rfc_md5.cpp    ${NOP_ONVIF_DIR}/bm/sha1.cpp
        ${NOP_ONVIF_DIR}/bm/sha256.cpp     ${NOP_ONVIF_DIR}/bm/sys_buf.cpp
        ${NOP_ONVIF_DIR}/bm/sys_log.cpp    ${NOP_ONVIF_DIR}/bm/sys_os.cpp
        ${NOP_ONVIF_DIR}/bm/util.cpp       ${NOP_ONVIF_DIR}/bm/word_analyse.cpp
        ${NOP_ONVIF_DIR}/bm/ws.cpp         ${NOP_ONVIF_DIR}/bm/xml_node.cpp
        ${NOP_ONVIF_DIR}/http/http_cln.cpp ${NOP_ONVIF_DIR}/http/http_parse.cpp
        ${NOP_ONVIF_DIR}/http/http_srv.cpp
        ${NOP_ONVIF_DIR}/onvif/onvif.cpp        ${NOP_ONVIF_DIR}/onvif/onvif_act.cpp
        ${NOP_ONVIF_DIR}/onvif/onvif_api.cpp    ${NOP_ONVIF_DIR}/onvif/onvif_cln.cpp
        ${NOP_ONVIF_DIR}/onvif/onvif_cm.cpp     ${NOP_ONVIF_DIR}/onvif/onvif_event.cpp
        ${NOP_ONVIF_DIR}/onvif/onvif_pkt.cpp    ${NOP_ONVIF_DIR}/onvif/onvif_probe.cpp
        ${NOP_ONVIF_DIR}/onvif/onvif_utils.cpp  ${NOP_ONVIF_DIR}/onvif/onvif_http.cpp
        ${NOP_ONVIF_DIR}/onvif/soap.cpp         ${NOP_ONVIF_DIR}/onvif/soap_parser.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/onvif/onvif_adapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/onvif/onvif_adapter_ext.cpp)  # 映射用扩展 ABI(PTZ/mask/OSD/venc CRUD)

    add_library(nopcore_onvif_obj OBJECT ${NOP_ONVIF_SRC})
    set_target_properties(nopcore_onvif_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(nopcore_onvif_obj PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${NOP_ONVIF_DIR} ${NOP_ONVIF_DIR}/bm ${NOP_ONVIF_DIR}/http
        ${NOP_ONVIF_DIR}/onvif ${NOP_ONVIF_DIR}/openssl/include)
    target_compile_definitions(nopcore_onvif_obj PRIVATE
        EPOLL HTTPS PROFILE_C_SUPPORT PROFILE_G_SUPPORT THERMAL_SUPPORT
        CREDENTIAL_SUPPORT ACCESS_RULES SCHEDULE_SUPPORT RECEIVER_SUPPORT
        IPFILTER_SUPPORT DEVICEIO_SUPPORT PROVISIONING_SUPPORT)
    if(NOT MSVC)
        target_compile_options(nopcore_onvif_obj PRIVATE -w)  # silence vendored code
    endif()
    set(NOP_ONVIF_OBJECTS $<TARGET_OBJECTS:nopcore_onvif_obj>)

    # OpenSSL：主机用系统；交叉(NVR_CROSS)用 TUTK 随包 aarch64 libssl/libcrypto(.so.3)。
    # ONVIF 客户端编译用工程内 openssl 头(third_party/onvif/openssl/include, 1.1.1)。
    if(NVR_CROSS AND DEFINED NVR_TUTK_AARCH64_LIB)
        list(APPEND NOP_LINK_LIBS ${NVR_TUTK_AARCH64_LIB}/libssl.so ${NVR_TUTK_AARCH64_LIB}/libcrypto.so)
    else()
        find_package(OpenSSL)
        if(OpenSSL_FOUND)
            list(APPEND NOP_LINK_LIBS OpenSSL::SSL OpenSSL::Crypto)
        else()
            list(APPEND NOP_LINK_LIBS ssl crypto)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# ONVIF server (device-side: exposes this device as a standard ONVIF camera).
# Vendored Happytimesoft ONVIF server (Profile S subset) in
# third_party/onvif_server/, compiled UNMODIFIED with NO_RTSP_SERVER so only the
# control plane (Device/Media/Imaging/Events + WS-Discovery) is built — no
# RTSP/media/ffmpeg. The adapter (src/onvif/onvif_server_adapter.cpp) is the only
# contact point, behind its own object library and include scope.
# NOTE: the client and server vendor two distinct trees with identical filenames
# and different ONVIF_CFG layouts; do NOT enable NOP_WITH_ONVIF and
# NOP_WITH_ONVIF_SERVER together (their global symbols collide at link time).
# ---------------------------------------------------------------------------
if(NOP_WITH_ONVIF_SERVER)
    if(NOP_WITH_ONVIF)
        message(FATAL_ERROR "NOP_WITH_ONVIF and NOP_WITH_ONVIF_SERVER are mutually "
                            "exclusive (clashing vendored symbols). Enable only one.")
    endif()
    enable_language(CXX)
    set(NOP_ONVIF_SRV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onvif_server)
    file(GLOB NOP_ONVIF_SRV_BM  ${NOP_ONVIF_SRV_DIR}/bm/*.cpp)
    set(NOP_ONVIF_SRV_SRC
        ${NOP_ONVIF_SRV_BM}
        ${NOP_ONVIF_SRV_DIR}/http/http_auth.cpp  ${NOP_ONVIF_SRV_DIR}/http/http_cln.cpp
        ${NOP_ONVIF_SRV_DIR}/http/httpd.cpp       ${NOP_ONVIF_SRV_DIR}/http/http_parse.cpp
        ${NOP_ONVIF_SRV_DIR}/http/http_srv.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif.cpp            ${NOP_ONVIF_SRV_DIR}/onvif/onvif_accessrules.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_analytics.cpp  ${NOP_ONVIF_SRV_DIR}/onvif/onvif_cfg.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_cm.cpp         ${NOP_ONVIF_SRV_DIR}/onvif/onvif_credential.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_device.cpp     ${NOP_ONVIF_SRV_DIR}/onvif/onvif_deviceio.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_doorcontrol.cpp ${NOP_ONVIF_SRV_DIR}/onvif/onvif_event.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_image.cpp      ${NOP_ONVIF_SRV_DIR}/onvif/onvif_media.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_media2.cpp     ${NOP_ONVIF_SRV_DIR}/onvif/onvif_pkt.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_probe.cpp      ${NOP_ONVIF_SRV_DIR}/onvif/onvif_provisioning.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_ptz.cpp        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_receiver.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_recording.cpp  ${NOP_ONVIF_SRV_DIR}/onvif/onvif_schedule.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_security.cpp   ${NOP_ONVIF_SRV_DIR}/onvif/onvif_srv.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_thermal.cpp    ${NOP_ONVIF_SRV_DIR}/onvif/onvif_timer.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/onvif_utils.cpp      ${NOP_ONVIF_SRV_DIR}/onvif/soap.cpp
        ${NOP_ONVIF_SRV_DIR}/onvif/soap_parser.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/onvif/onvif_server_adapter.cpp)

    add_library(nopcore_onvif_server_obj OBJECT ${NOP_ONVIF_SRV_SRC})
    set_target_properties(nopcore_onvif_server_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(nopcore_onvif_server_obj PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${NOP_ONVIF_SRV_DIR} ${NOP_ONVIF_SRV_DIR}/bm ${NOP_ONVIF_SRV_DIR}/http
        ${NOP_ONVIF_SRV_DIR}/onvif ${NOP_ONVIF_SRV_DIR}/openssl/include)
    # FULL ONVIF profile surface (S/T/M/G/C/Q + analytics/security/etc.), RTSP
    # server cut out (NO_RTSP_SERVER). Safe because the onvif/ control plane has
    # zero unguarded av_/media_/rtsp_ references — all RTSP coupling sits behind
    # RTSP_OVER_HTTP/WEBSOCKET, left undefined. The vendored .cpp already build
    # all services; these macros light them up.
    target_compile_definitions(nopcore_onvif_server_obj PRIVATE
        HT_STATIC HTTPD HTTPS
        MEDIA_SUPPORT MEDIA2_SUPPORT IMAGE_SUPPORT AUDIO_SUPPORT
        DEVICEIO_SUPPORT PTZ_SUPPORT VIDEO_ANALYTICS
        PROFILE_G_SUPPORT PROFILE_C_SUPPORT PROFILE_Q_SUPPORT
        THERMAL_SUPPORT RECEIVER_SUPPORT CREDENTIAL_SUPPORT
        SCHEDULE_SUPPORT ACCESS_RULES IPFILTER_SUPPORT PROVISIONING_SUPPORT
        STORAGE_SUPPORT GEOLOCATION_SUPPORT SECURITY_SUPPORT
        DOT11_SUPPORT MPEG4_SUPPORT
        NO_RTSP_SERVER)
    if(NOT MSVC)
        target_compile_options(nopcore_onvif_server_obj PRIVATE -w)  # silence vendored code
    endif()
    set(NOP_ONVIF_OBJECTS $<TARGET_OBJECTS:nopcore_onvif_server_obj>)

    list(APPEND NOP_LINK_LIBS
        ${NOP_ONVIF_SRV_DIR}/openssl/lib/linux/libssl.so.1.1
        ${NOP_ONVIF_SRV_DIR}/openssl/lib/linux/libcrypto.so.1.1)
endif()

set(_nop_targets "")
if(NOP_BUILD_STATIC)
    add_library(nopcore_static STATIC $<TARGET_OBJECTS:nopcore_obj> ${NOP_ONVIF_OBJECTS})
    set_target_properties(nopcore_static PROPERTIES OUTPUT_NAME nopcore)
    list(APPEND _nop_targets nopcore_static)
endif()
if(NOP_BUILD_SHARED)
    add_library(nopcore_shared SHARED $<TARGET_OBJECTS:nopcore_obj> ${NOP_ONVIF_OBJECTS})
    set_target_properties(nopcore_shared PROPERTIES
        OUTPUT_NAME nopcore
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
    list(APPEND _nop_targets nopcore_shared)
endif()

foreach(t ${_nop_targets})
    target_include_directories(${t}
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
               $<INSTALL_INTERFACE:include>)
    target_link_libraries(${t} PUBLIC ${NOP_LINK_LIBS} cjson)
endforeach()

# A stable alias for downstream targets (prefers static).
if(NOP_BUILD_STATIC)
    add_library(nopcore ALIAS nopcore_static)
elseif(NOP_BUILD_SHARED)
    add_library(nopcore ALIAS nopcore_shared)
endif()

# Internal-include helper for tests/examples that reach into src/.
set(NOP_PRIVATE_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${NOP_CJSON_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/stub
    CACHE INTERNAL "private include dirs")

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(NOP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Install (TUTK-style layout: include/ + lib/)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS ${_nop_targets}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(NOP_WITH_ONVIF)
    # Ship the vendored OpenSSL runtime the shared lib depends on.
    install(FILES
        ${NOP_ONVIF_DIR}/openssl/lib/linux/libssl.so.1.1
        ${NOP_ONVIF_DIR}/openssl/lib/linux/libcrypto.so.1.1
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if(NOP_WITH_ONVIF_SERVER)
    install(FILES
        ${NOP_ONVIF_SRV_DIR}/openssl/lib/linux/libssl.so.1.1
        ${NOP_ONVIF_SRV_DIR}/openssl/lib/linux/libcrypto.so.1.1
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

message(STATUS "nop_sdk ${PROJECT_VERSION}  osal=${NOP_OSAL_PORT}  "
               "static=${NOP_BUILD_STATIC} shared=${NOP_BUILD_SHARED} "
               "stub=${NOP_WITH_HAL_STUB} onvif=${NOP_WITH_ONVIF} "
               "onvif_server=${NOP_WITH_ONVIF_SERVER} tutk=${NOP_WITH_TUTK}")
