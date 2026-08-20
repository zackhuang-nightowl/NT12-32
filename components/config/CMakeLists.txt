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

add_library(nvr_settings STATIC src/nvr_settings.c src/nvr_identity.c src/nvr_record_policy.c)
target_include_directories(nvr_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# nvr_log.h(header-only)供 nvr_identity 排障日志用;仅头,不引入链接依赖,保持叶子性质。
target_include_directories(nvr_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include)
target_link_libraries(nvr_settings PUBLIC cjson sqlite3)

# host 单测:身份分区读写(NVR_USER_DIR 重定向到临时目录)
if(NVR_BUILD_TESTS)
  add_executable(test_nvr_identity test/test_nvr_identity.c src/nvr_identity.c)
  target_include_directories(test_nvr_identity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include)
  target_compile_definitions(test_nvr_identity PRIVATE NVR_USER_DIR="${CMAKE_CURRENT_BINARY_DIR}/id_test_user")
  target_link_libraries(test_nvr_identity PRIVATE cjson pthread)
  add_test(NAME nvr_identity COMMAND test_nvr_identity)
endif()
