# ⑥ storage — 裸盘运维管理层。依赖 recorder(librsdk) 的裸设备原语。
cmake_minimum_required(VERSION 3.13)
project(nvr_storage C)

add_library(nvr_storage STATIC
    src/storage_mgr.c
    src/storage_disk.c
    src/storage_health.c
    src/storage_hotplug.c
    src/storage_guard.c
)

target_include_directories(nvr_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../recorder/include   # rsdk.h 及各模块头
)

# 链接 recorder 的 librsdk（提供 rsdk_rawdev/format/group_*）
# 由顶层聚合构建统一 target；独立构建时取消下面注释并指到 librsdk.a
# target_link_libraries(nvr_storage PUBLIC rsdk)

set_target_properties(nvr_storage PROPERTIES C_STANDARD 11)
