# OnvifClientLibrary — 固件(aarch64)交叉编译静态库。
# 统一的 RTSP/RTP client(+server 给APP拉流)+ media live + nop_rtsp facade。
# 本 target 只做 RTSP;ONVIF client 仍用 nop 树(不编 onvif/,避免双份 ONVIF)。
# 关掉 SRTP/RTSPS/WEBSOCKET/HTTPS 重依赖(PoE 相机走明文 RTSP over TCP);openssl/srtp 仅用头。
cmake_minimum_required(VERSION 3.13)

set(OCL ${CMAKE_CURRENT_LIST_DIR})

file(GLOB OCL_SRC
    ${OCL}/bm/*.cpp
    ${OCL}/http/*.cpp
    ${OCL}/rtsp/*.cpp
    ${OCL}/rtp/*.cpp
    ${OCL}/media/*.cpp
    ${OCL}/nop_rtsp.cpp
)
# 排除采集/编码/播放/示例(依赖 alsa/ffmpeg/v4l/gles),客户端不需要
list(FILTER OCL_SRC EXCLUDE REGEX "/(main|test|.*capture.*|.*encoder.*|.*decoder.*|.*alsa.*|.*android.*|.*_v4l.*|screen_.*|window_.*|audio_play.*|gles_.*|xcb_.*|media_codec|media_parse|media_proxy|media_pusher|file_demux|live_.*_qt)\\.cpp$")

add_library(onvifclient STATIC ${OCL_SRC})

target_include_directories(onvifclient PUBLIC
    ${OCL} ${OCL}/bm ${OCL}/http ${OCL}/rtsp ${OCL}/rtp ${OCL}/media ${OCL}/onvif
    ${OCL}/openssl/include ${OCL}/libsrtp/include
)

target_compile_definitions(onvifclient PRIVATE
    EPOLL MEDIA_LIVE MEDIA_SUPPORT MPEG4_SUPPORT AUDIO_SUPPORT
    BACKCHANNEL RTSP_BACKCHANNEL
    # 注:不定义 SRTP/RTSPS/RTSP_OVER_HTTP/WEBSOCKET(相机走明文RTSP),避免 openssl/libsrtp 链接依赖
    # 与 nop ONVIF-client 同名 4 参 http_onvif_trans 是本库死代码,改名避免 --allow-multiple-definition 误取
    http_onvif_trans=ht_ocl_onvif_trans_unused)

target_compile_options(onvifclient PRIVATE -O2 -w -fpermissive)
set_target_properties(onvifclient PROPERTIES CXX_STANDARD 14)
