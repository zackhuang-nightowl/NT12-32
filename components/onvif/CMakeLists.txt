# ② onvif glue — 基于 nop_sdk 的 nop_onvif_* 客户端（包 Happytime）。
cmake_minimum_required(VERSION 3.13)
project(nvr_onvif C)

# 设备三分类：纯 C 叶子（不依赖 happytime/nop），可独立编译与单测
add_library(nvr_dev_classify STATIC src/nvr_dev_classify.c)
target_include_directories(nvr_dev_classify PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(nvr_dev_classify PROPERTIES C_STANDARD 11)

add_library(nvr_onvif STATIC src/nvr_onvif.c)

target_include_directories(nvr_onvif PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nop/include        # nop_sdk/nop_onvif.h
)
target_link_libraries(nvr_onvif PUBLIC nvr_dev_classify)
# 链接 nop 的 onvif 客户端（NOP_WITH_ONVIF=ON 时 nopcore 内含 nop_onvif_* 实现）
if(TARGET nopcore_static)
    target_link_libraries(nvr_onvif PUBLIC nopcore_static)
endif()

set_target_properties(nvr_onvif PROPERTIES C_STANDARD 11)

option(NVR_ONVIF_TEST "构建 onvif 分类自测" ON)
if(NVR_ONVIF_TEST AND NOT NVR_CROSS)
  enable_testing()
  add_executable(test_dev_classify tests/test_dev_classify.c)
  target_link_libraries(test_dev_classify nvr_dev_classify)
  add_test(NAME dev_classify COMMAND test_dev_classify)
endif()
