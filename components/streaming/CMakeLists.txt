# ③ streaming — 拉流出图。CRtspClient(happytime) → media_hal(硬解) + recorder(录像)
cmake_minimum_required(VERSION 3.13)
project(nvr_streaming C CXX)

# 统一 RTSP SDK：NT12-SDK/OnvifClientLibrary(nop::NopRtspClient facade)。puller 仅需 nop_rtsp.h。
set(OCL ${CMAKE_CURRENT_SOURCE_DIR}/../../../NT12-SDK/OnvifClientLibrary)

add_library(nvr_streaming STATIC
    src/stream_nal.c        # 纯 C：NAL 分类
    src/stream_hub.c        # 纯 C：Live/Record 队列
    src/stream_record_worker.c  # 纯 C：Record Worker 写盘
    src/stream_router.c     # 纯 C：一帧 → hub → vdec + recorder
    src/stream_puller.cpp   # C++：CRtspClient 封装
    src/stream_mgr.cpp      # C++：16 路生命周期，实现 C API
)

find_package(Threads REQUIRED)
target_link_libraries(nvr_streaming PUBLIC Threads::Threads)

target_include_directories(nvr_streaming PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../recorder/include     # rsdk.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/media_hal # mhal_vdec.h / mhal_vout.h
)
target_include_directories(nvr_streaming PRIVATE
    ${OCL}                                                # nop_rtsp.h (puller.cpp 用)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include      # nvr_log.h
)

# 链接：recorder(librsdk) + platform media_hal + happytime rtsp 客户端库
# target_link_libraries(nvr_streaming PUBLIC rsdk mhal happytime_rtsp)

set_target_properties(nvr_streaming PROPERTIES C_STANDARD 11 CXX_STANDARD 14)
