# components/cloud_uploader — 云存异步上传引擎。计划 §B6。
cmake_minimum_required(VERSION 3.13)
project(nvr_cloud_uploader C)

set(CMAKE_C_STANDARD 11)

set(NVR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

if(NOT TARGET cjson)
  add_subdirectory(${NVR_ROOT}/third_party/cJSON ${CMAKE_CURRENT_BINARY_DIR}/cjson_build)
endif()

add_library(nvr_cloud_uploader STATIC
  src/uploader.c
  src/http_vsaas.c
  src/ts_mux.c
)
target_include_directories(nvr_cloud_uploader PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${NVR_ROOT}/components/recorder/include      # rsdk.h / rsdk_cloud.h
  ${NVR_ROOT}/common/include                   # nvr_log.h
)
target_include_directories(nvr_cloud_uploader PRIVATE
  ${NVR_ROOT}/app/config                       # nvr_urls.h（出站 URL 总表）
)
target_link_libraries(nvr_cloud_uploader PUBLIC cjson)

# recorder 生成 rsdk_config.h
if(TARGET rsdk)
  add_dependencies(nvr_cloud_uploader rsdk)
  target_link_libraries(nvr_cloud_uploader PUBLIC rsdk)
endif()

# libcurl：主机用系统；交叉用工程内 vendored 头(8.5.0) + TUTK aarch64 libcurl.so
if(NVR_CROSS)
  target_include_directories(nvr_cloud_uploader PRIVATE ${NVR_ROOT}/third_party/curl/include)
  target_link_libraries(nvr_cloud_uploader PUBLIC ${NVR_TUTK_AARCH64_LIB}/libcurl.so)
  message(STATUS "nvr_cloud_uploader: 交叉=vendored curl 头 + TUTK aarch64 libcurl")
else()
  find_package(CURL)
  if(CURL_FOUND)
    target_link_libraries(nvr_cloud_uploader PUBLIC CURL::libcurl)
  else()
    find_library(CURL_LIB curl)
    if(CURL_LIB)
      target_link_libraries(nvr_cloud_uploader PUBLIC ${CURL_LIB})
    else()
      message(FATAL_ERROR "libcurl not found (需要 libcurl4-openssl-dev)")
    endif()
  endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(nvr_cloud_uploader PUBLIC Threads::Threads)
