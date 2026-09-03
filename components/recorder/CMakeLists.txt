cmake_minimum_required(VERSION 3.10)
project(rsdk VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(RSDK_WITH_METADATA "Build metadata module (needs sqlite3); 应与 rsdk_features.conf 一致" ON)
option(RSDK_BUILD_EXAMPLES "Build example programs" ON)

# ---- 由配置文件生成 rsdk_config.h ----
find_package(Python3 COMPONENTS Interpreter)
set(RSDK_CONF   ${CMAKE_CURRENT_SOURCE_DIR}/rsdk_features.conf)
set(RSDK_CONFIG_H ${CMAKE_CURRENT_SOURCE_DIR}/include/rsdk_config.h)
add_custom_command(
  OUTPUT  ${RSDK_CONFIG_H}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_config.py ${RSDK_CONF} ${RSDK_CONFIG_H}
  DEPENDS ${RSDK_CONF} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_config.py
  COMMENT "gen_config: rsdk_features.conf -> include/rsdk_config.h")
add_custom_target(rsdk_config DEPENDS ${RSDK_CONFIG_H})

# ---- 静态库 ----
set(RSDK_SRC
  src/rsdk_core.c src/rsdk_util.c src/rsdk_feature.c src/rsdk_rawdev.c
  src/rsdk_crypto.c src/rsdk_storgedev.c src/rsdk_index.c src/rsdk_evtidx.c src/rsdk_rec.c
  src/rsdk_play.c src/rsdk_balance.c src/rsdk_backup.c src/rsdk_disk.c
  src/rsdk_scan.c    # rsdk_evtidx.c=事件索引区; rsdk_scan.c=自描述扫描重建(索引丢失可扫盘重建)
  src/rsdk_repair.c) # 开机自检 + 分级修复(Tier1 封口 / Tier2 后台重建)
if(RSDK_WITH_METADATA)
  list(APPEND RSDK_SRC src/rsdk_meta.c src/rsdk_pic.c)  # 元数据(可丢)/抓拍(盘上权威=事件槽)
endif()

add_library(rsdk STATIC ${RSDK_SRC})
add_dependencies(rsdk rsdk_config)
target_include_directories(rsdk PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
if(RSDK_WITH_METADATA)
  # vendored sqlite3（工程内合并源；主机/交叉统一，自包含）
  if(NOT TARGET sqlite3)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sqlite3
                     ${CMAKE_CURRENT_BINARY_DIR}/sqlite3_build)
  endif()
  target_link_libraries(rsdk PUBLIC sqlite3)
endif()

# ---- 示例 ----
if(RSDK_BUILD_EXAMPLES)
  add_executable(rec_demo examples/rec_demo.c)
  target_link_libraries(rec_demo rsdk)

  add_executable(multidisk_demo examples/multidisk_demo.c)
  target_link_libraries(multidisk_demo rsdk)

  add_executable(balance_demo examples/balance_demo.c)
  target_link_libraries(balance_demo rsdk)

  add_executable(backup_demo examples/backup_demo.c)
  target_link_libraries(backup_demo rsdk)

  add_executable(pic_demo examples/pic_demo.c)
  target_link_libraries(pic_demo rsdk)

  add_executable(overwrite_demo examples/overwrite_demo.c)
  target_link_libraries(overwrite_demo rsdk)

  add_executable(retention_demo examples/retention_demo.c)
  target_link_libraries(retention_demo rsdk)

  add_executable(meta_demo examples/meta_demo.c)   # 独立参考(vendored sqlite3)
  if(NOT TARGET sqlite3)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sqlite3
                     ${CMAKE_CURRENT_BINARY_DIR}/sqlite3_build_demo)
  endif()
  target_link_libraries(meta_demo sqlite3)
  target_include_directories(meta_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  if(RSDK_WITH_METADATA)
    add_executable(rebuild_demo examples/rebuild_demo.c)  # 统一扫盘重建: 抹事件区→rebuild→全链回来
    target_link_libraries(rebuild_demo rsdk)
  endif()

  add_executable(evtidx_demo examples/evtidx_demo.c)   # 事件索引区: 写/查/patch/失效
  target_link_libraries(evtidx_demo rsdk)

  add_executable(evtidx_newest_test examples/evtidx_newest_test.c)  # 事件查询倒叙(超 cap 取最新)
  target_link_libraries(evtidx_newest_test rsdk)
endif()

install(TARGETS rsdk ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(FILES rsdk_features.conf DESTINATION share/rsdk)
