# ④ cloud_tutk glue — TUTK P2P(P2PTunnel 映射 + 本地 RTSP live)
cmake_minimum_required(VERSION 3.13)
project(nvr_cloud_tutk C)

set(TUTK ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/tutk_sdk)

add_library(nvr_cloud_tutk STATIC
    src/nvr_tutk.c
    src/nvr_rtsp_live.c
)

target_include_directories(nvr_cloud_tutk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(nvr_cloud_tutk PRIVATE
    ${TUTK}/Include ${TUTK}/Include/P2PCam
)

set(TUTK_LIB ${TUTK}/Lib/Linux/ArmCortexA53_NT98633_8.4.0)
if(EXISTS ${TUTK_LIB}/libIOTCAPIs.so)
    target_compile_definitions(nvr_cloud_tutk PRIVATE NVR_HAVE_TUTK=1)
    target_link_libraries(nvr_cloud_tutk PUBLIC
        ${TUTK_LIB}/libIOTCAPIs.so pthread)
    target_link_options(nvr_cloud_tutk PUBLIC -Wl,--allow-shlib-undefined)
    message(STATUS "cloud_tutk: TUTK P2PTunnel+IOTC enabled")
else()
    target_compile_definitions(nvr_cloud_tutk PRIVATE NVR_HAVE_TUTK=0)
    message(STATUS "cloud_tutk: TUTK lib 未找到,编 stub(主机构建)")
endif()

set_target_properties(nvr_cloud_tutk PROPERTIES C_STANDARD 11)
