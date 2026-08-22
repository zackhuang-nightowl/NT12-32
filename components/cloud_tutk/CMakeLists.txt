# ④ cloud_tutk — 本机 live+playback RTSP(:8554) + ODC agent cgi
# P2P 会话由 /dvr/tutk_cloud_agent/AVAPIs_Server_CLI 负责,本库不再链 IOTC/P2PTunnel。
cmake_minimum_required(VERSION 3.13)
project(nvr_cloud_tutk C CXX)

# 隧道直播 RTSP 服务端改用 SDK 标准 nop::NopRtspServer(有挂钟媒体时钟,修冻结 bug)。
# nvr_rtsp_srv.cpp 是 C++ shim,nvr_rtsp_live.c 走 C API 调用它。
add_library(nvr_cloud_tutk STATIC
    src/nvr_rtsp_live.c
    src/nvr_rtsp_srv.cpp)
target_include_directories(nvr_cloud_tutk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../recorder/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/OnvifClientLibrary)   # nop_rtsp.h
# onvifclient(PUBLIC 用法要求)带来 media/ + bm/ 头(live_video.h / sys_inc.h),
# 并把 onvifclient 追加到最终可执行链接行(在 nvr_cloud_tutk 之后)→ 修 NopRtspServer
# 符号的链接顺序,无需改 app/CMakeLists.txt。
target_link_libraries(nvr_cloud_tutk PUBLIC pthread onvifclient)
set_target_properties(nvr_cloud_tutk PROPERTIES C_STANDARD 11 CXX_STANDARD 14)
if(TARGET rsdk)
    add_dependencies(nvr_cloud_tutk rsdk)
endif()

# ODC agent cgi:AVAPIs_Server_CLI popen 本程序。POSIX socket → :6061 + nvr_identity。
if(UNIX)
    set(_id ${CMAKE_CURRENT_SOURCE_DIR}/../config)
    add_executable(nvr_tutk_cgi
        cgi/nvr_tutk_cgi.c
        ${_id}/src/nvr_identity.c)
    target_include_directories(nvr_tutk_cgi PRIVATE
        ${_id}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include)
    target_link_libraries(nvr_tutk_cgi PRIVATE cjson pthread)
    set_target_properties(nvr_tutk_cgi PROPERTIES C_STANDARD 11)
    add_custom_command(TARGET nvr_tutk_cgi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:nvr_tutk_cgi>
            ${CMAKE_CURRENT_SOURCE_DIR}/../../deploy/rootfs/dvr/tutk_cloud_agent/nvr_tutk_cgi
        COMMENT "install nvr_tutk_cgi → deploy/rootfs/dvr/tutk_cloud_agent")
endif()
