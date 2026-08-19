#-------------------------------------------------
#
# Project created by QtCreator 2019-06-21T09:49:18
#
#-------------------------------------------------

QT += core gui widgets multimedia

TARGET = onvifrtspserver
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += EPOLL
DEFINES += MEDIA_DEVICE
DEFINES += RTSP_RTCP
DEFINES += RTSP_BACKCHANNEL
DEFINES += RTSP_METADATA
DEFINES += RTSP_REPLAY
DEFINES += RTSP_SRTP
DEFINES += RTSP_OVER_HTTP
DEFINES += DEMO
DEFINES += MEDIA_SUPPORT
DEFINES += MEDIA2_SUPPORT
DEFINES += IMAGE_SUPPORT
DEFINES += DEVICEIO_SUPPORT

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH +=  \
    android     \
    bm          \
    http        \
    media       \
    onvif       \
    rtp         \
    rtsp        \
    ffmpeg/include  \
    libsrtp/include \

SOURCES += \
    android/About.cpp \
    android/main.cpp \
    android/OnvifRtspServerDlg.cpp \
    android/utils.cpp \
    android/video_encoder_android.cpp \
    bm/base64.cpp \
    bm/hqueue.cpp \
    bm/hxml.cpp \
    bm/linked_list.cpp \
    bm/net_util.cpp \
    bm/ppstack.cpp \
    bm/rfc_md5.cpp \
    bm/sha256.cpp \
    bm/sha1.cpp \
    bm/sys_buf.cpp \
    bm/sys_log.cpp \
    bm/sys_os.cpp \
    bm/util.cpp \
    bm/word_analyse.cpp \
    bm/xml_node.cpp \
    http/http_auth.cpp \
    http/http_cln.cpp \
    http/http_parse.cpp \
    http/http_srv.cpp \
    media/audio_capture.cpp \
    media/audio_capture_android.cpp \
    media/audio_decoder.cpp \
    media/audio_encoder.cpp \
    media/audio_play.cpp \
    media/audio_play_qt.cpp \
    media/avcodec_mutex.cpp \
    media/gles_engine.cpp \
    media/gles_input.cpp \
    media/media_codec.cpp \
    media/media_util.cpp \
    media/video_capture.cpp \
    media/video_capture_qt.cpp \
    onvif/onvif.cpp \
    onvif/onvif_accessrules.cpp \
    onvif/onvif_analytics.cpp \
    onvif/onvif_cfg.cpp \
    onvif/onvif_cm.cpp \
    onvif/onvif_credential.cpp \
    onvif/onvif_device.cpp \
    onvif/onvif_deviceio.cpp \
    onvif/onvif_doorcontrol.cpp \
    onvif/onvif_event.cpp \
    onvif/onvif_image.cpp \
    onvif/onvif_media.cpp \
    onvif/onvif_media2.cpp \
    onvif/onvif_pkt.cpp \
    onvif/onvif_probe.cpp \
    onvif/onvif_provisioning.cpp \
    onvif/onvif_ptz.cpp \
    onvif/onvif_receiver.cpp \
    onvif/onvif_recording.cpp \
    onvif/onvif_schedule.cpp \
    onvif/onvif_srv.cpp \
    onvif/onvif_thermal.cpp \
    onvif/onvif_timer.cpp \
    onvif/onvif_utils.cpp \
    onvif/soap.cpp \
    onvif/soap_parser.cpp \
    rtp/aac_rtp_rx.cpp \
    rtp/mjpeg_tables.cpp \
    rtp/pcm_rtp_rx.cpp \
    rtp/rtcp.cpp \
    rtp/rtp_rx.cpp \
    rtp/rtp_tx.cpp \
    rtsp/rtsp_cfg.cpp \
    rtsp/rtsp_http.cpp \
    rtsp/rtsp_mc.cpp \
    rtsp/rtsp_media.cpp \
    rtsp/rtsp_parse.cpp \
    rtsp/rtsp_rsua.cpp \
    rtsp/rtsp_srv.cpp \
    rtsp/rtsp_srv_backchannel.cpp \
    rtsp/rtsp_stream.cpp \
    rtsp/rtsp_timer.cpp \
    rtsp/rtsp_util.cpp

HEADERS += \
    android/About.h \
    android/OnvifRtspServerDlg.h \
    android/video_encoder_android.h \
    media/audio_capture_android.h \
    media/audio_play_qt.h \
    media/video_capture_qt.h

FORMS += \
    android/About.ui \
    android/OnvifRtspServerDlg.ui

LIBS += -L$$PWD/ffmpeg/lib/$$ANDROID_TARGET_ARCH
LIBS += -L$$PWD/libsrtp/lib/$$ANDROID_TARGET_ARCH

LIBS += -lavformat
LIBS += -lswscale
LIBS += -lavcodec
LIBS += -lswresample
LIBS += -lavutil
LIBS += -lopus
LIBS += -lx264
LIBS += -lx265
LIBS += -lsrtp2
LIBS += -lOpenSLES

DISTFILES += \
    android/AndroidManifest.xml \
    android/build.gradle \
    android/gradle.properties \
    android/gradle/wrapper/gradle-wrapper.jar \
    android/gradle/wrapper/gradle-wrapper.properties \
    android/gradlew \
    android/gradlew.bat \
    android/res/values/libs.xml

ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android

