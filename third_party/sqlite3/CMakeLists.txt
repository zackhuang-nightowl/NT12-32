# sqlite3 — 工程内 vendored 合并源（amalgamation 3.45.3），主机/交叉统一编译。
# 各组件 link sqlite3 目标（不再 find_library 系统 sqlite3），自包含 + 可交叉。
cmake_minimum_required(VERSION 3.13)

if(NOT TARGET sqlite3)
  add_library(sqlite3 STATIC ${CMAKE_CURRENT_LIST_DIR}/sqlite3.c)
  target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_LIST_DIR})
  # 常用精简开关：启用 JSON1（rsdk_meta / rsdk_cloud 依赖 json_extract）+ 线程安全
  target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_JSON1=1
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5=0
    SQLITE_OMIT_LOAD_EXTENSION=1)
  set_target_properties(sqlite3 PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
  find_package(Threads QUIET)
  if(TARGET Threads::Threads)
    target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  target_link_libraries(sqlite3 PUBLIC m)      # sqlite 数学函数(log 等)
endif()
