# happytime RTSP 客户端库（CRtspClient）— 从源码交叉编 aarch64 静态库，供 ③streaming 链接。
# 只取客户端相关源集（bm/rtsp/rtp/media/http），排除服务端 main/onvif-server。
cmake_minimum_required(VERSION 3.13)

set(HT ${CMAKE_CURRENT_LIST_DIR}/source)

file(GLOB HT_SRC
    ${HT}/bm/*.cpp
    ${HT}/rtsp/*.cpp
    ${HT}/rtp/*.cpp
    ${HT}/http/*.cpp
)
# media/ 仅取格式定义（客户端需要）；其余为服务端编码/采集(依赖 alsa/ffmpeg)，排除
file(GLOB HT_MEDIA ${HT}/media/media_format.cpp)
list(APPEND HT_SRC ${HT_MEDIA})
# 排除服务端/采集/编码/示例入口（客户端不需要）
list(FILTER HT_SRC EXCLUDE REGEX "/(main|test|.*_srv|.*server|.*capture.*|.*encoder.*|.*alsa.*|.*android.*|.*_v4l.*)\\.cpp$")

add_library(happytime_rtsp STATIC ${HT_SRC})
target_include_directories(happytime_rtsp PUBLIC
    ${HT} ${HT}/bm ${HT}/http ${HT}/rtsp ${HT}/rtp ${HT}/media ${HT}/onvif
    ${CMAKE_CURRENT_LIST_DIR}/../openssl/include        # 工程内 vendored openssl 头
)
target_compile_definitions(happytime_rtsp PRIVATE
    EPOLL HTTPS MEDIA_SUPPORT MPEG4_SUPPORT AUDIO_SUPPORT)
target_compile_options(happytime_rtsp PRIVATE -O2 -w -fpermissive)
set_target_properties(happytime_rtsp PROPERTIES CXX_STANDARD 14)
