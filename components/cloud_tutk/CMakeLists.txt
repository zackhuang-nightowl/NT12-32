# ④ cloud_tutk glue — TUTK 设备端 P2P（IOTC + AVAPI）。
cmake_minimum_required(VERSION 3.13)
project(nvr_cloud_tutk C)

set(TUTK ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/tutk_sdk)

add_library(nvr_cloud_tutk STATIC src/nvr_tutk.c)

target_include_directories(nvr_cloud_tutk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(nvr_cloud_tutk PRIVATE
    ${TUTK}/Include ${TUTK}/Include/P2PCam
)

# 链接本机 SoC 变体的 TUTK aarch64 预编译库（NT98633）。
set(TUTK_LIB ${TUTK}/Lib/Linux/ArmCortexA53_NT98633_8.4.0)
if(EXISTS ${TUTK_LIB}/libIOTCAPIs.so)
    # 只链 nvr_tutk 用到的 IOTC + AVAPI；其它 tutk .so 的内部依赖(RDT 等)运行期由设备 rootfs 解析。
    target_link_libraries(nvr_cloud_tutk PUBLIC
        ${TUTK_LIB}/libIOTCAPIs.so ${TUTK_LIB}/libAVAPIs.so pthread)
    target_link_options(nvr_cloud_tutk PUBLIC -Wl,--allow-shlib-undefined)
else()
    message(STATUS "cloud_tutk: TUTK aarch64 库未找到(非目标机构建?)，仅编 glue")
endif()

set_target_properties(nvr_cloud_tutk PROPERTIES C_STANDARD 11)
