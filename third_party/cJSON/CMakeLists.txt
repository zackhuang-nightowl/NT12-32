# cJSON — 单一共享静态库（工程内唯一副本，去重；各组件 link cjson 而非各自编译）。
# 用 include guard 允许被多处 add_subdirectory 而只定义一次。
cmake_minimum_required(VERSION 3.13)

if(NOT TARGET cjson)
  add_library(cjson STATIC ${CMAKE_CURRENT_LIST_DIR}/cJSON.c)
  target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_LIST_DIR})
  set_target_properties(cjson PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
endif()
