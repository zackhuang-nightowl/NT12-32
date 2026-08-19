# ④ cloud_tutk glue — TUTK P2P(P2PTunnel 映射 + 本地 RTSP live)
cmake_minimum_required(VERSION 3.13)
project(nvr_cloud_tutk C)

# TUTK SDK 根：默认用外部完整 SDK(含 P2PTunnelServer/TUTKGlobal 全量符号);
# 可用 -DNVR_TUTK_ROOT=... 覆盖。旧 third_party/tutk_sdk 内的库缺 P2PTunnelServer_* 符号。
set(NVR_TUTK_ROOT "/home/zack/NT12-32/tutk" CACHE PATH "TUTK SDK root")
set(TUTK ${NVR_TUTK_ROOT})

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
        ${TUTK_LIB}/libIOTCAPIs.so
        ${TUTK_LIB}/libP2PTunnelAPIs.so
        ${TUTK_LIB}/libTUTKGlobalAPIs.so
        pthread)
    target_link_options(nvr_cloud_tutk PUBLIC -Wl,--allow-shlib-undefined)
    message(STATUS "cloud_tutk: TUTK P2PTunnel+IOTC enabled")
else()
    target_compile_definitions(nvr_cloud_tutk PRIVATE NVR_HAVE_TUTK=0)
    message(STATUS "cloud_tutk: TUTK lib 未找到,编 stub(主机构建)")
endif()

set_target_properties(nvr_cloud_tutk PROPERTIES C_STANDARD 11)
