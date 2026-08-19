# ④ cloud_tutk — 本机 live+playback RTSP(:8554) + ODC agent cgi
# P2P 会话由 /dvr/tutk_cloud_agent/AVAPIs_Server_CLI 负责,本库不再链 IOTC/P2PTunnel。
cmake_minimum_required(VERSION 3.13)
project(nvr_cloud_tutk C)

add_library(nvr_cloud_tutk STATIC src/nvr_rtsp_live.c)
target_include_directories(nvr_cloud_tutk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../recorder/include)
target_link_libraries(nvr_cloud_tutk PUBLIC pthread)
set_target_properties(nvr_cloud_tutk PROPERTIES C_STANDARD 11)
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
