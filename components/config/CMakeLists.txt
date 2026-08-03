# components/config — 运行期可写配置库（SQLite）。计划 §B7。
cmake_minimum_required(VERSION 3.13)
project(nvr_settings C)

set(CMAKE_C_STANDARD 11)

# 共享 cJSON / vendored sqlite3：优先复用已定义目标；独立构建时从工程内 third_party 创建。
set(NVR_TP ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party)
if(NOT TARGET cjson)
  add_subdirectory(${NVR_TP}/cJSON ${CMAKE_CURRENT_BINARY_DIR}/cjson_build)
endif()
if(NOT TARGET sqlite3)
  add_subdirectory(${NVR_TP}/sqlite3 ${CMAKE_CURRENT_BINARY_DIR}/sqlite3_build)
endif()

add_library(nvr_settings STATIC src/nvr_settings.c)
target_include_directories(nvr_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nvr_settings PUBLIC cjson sqlite3)

# 自测（host 验证：种子/回写/订阅/结构化表）
option(NVR_SETTINGS_TEST "构建 nvr_settings 自测" ON)
if(NVR_SETTINGS_TEST AND NOT NVR_CROSS)
  enable_testing()
  add_executable(test_nvr_settings tests/test_nvr_settings.c)
  target_link_libraries(test_nvr_settings nvr_settings)
  add_test(NAME nvr_settings COMMAND test_nvr_settings)
endif()
