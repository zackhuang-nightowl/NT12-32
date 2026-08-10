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
    EPOLL HTTPS MEDIA_SUPPORT MPEG4_SUPPORT AUDIO_SUPPORT
    # 本库只做 RTSP 客户端；http_cln.cpp 里的 ONVIF 事务函数 http_onvif_trans 是死代码
    # (RTSP 侧无人调用)，且与 nop ONVIF-client 的同名 6 参版本冲突(--allow-multiple-definition
    # 会误取本库的 4 参版本，导致 NVR 侧所有 ONVIF 请求 ABI 错位失败)。改名让它不再导出，
    # 全局 http_onvif_trans 只剩 nop client 的正确版本。
    http_onvif_trans=ht_rtsp_onvif_trans_unused)
target_compile_options(happytime_rtsp PRIVATE -O2 -w -fpermissive)
set_target_properties(happytime_rtsp PROPERTIES CXX_STANDARD 14)
