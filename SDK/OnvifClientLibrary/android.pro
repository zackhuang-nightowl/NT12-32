#-------------------------------------------------
#
# Project created by QtCreator 2014-02-17T15:51:28
#
#-------------------------------------------------

QT -= core gui
TARGET = onvifclient
TEMPLATE = lib
CONFIG += staticlib

DEFINES += ANDROID
DEFINES += EPOLL
DEFINES += PROFILE_C_SUPPORT
DEFINES += PROFILE_G_SUPPORT
DEFINES += THERMAL_SUPPORT
DEFINES += CREDENTIAL_SUPPORT
DEFINES += ACCESS_RULES
DEFINES += SCHEDULE_SUPPORT
DEFINES += RECEIVER_SUPPORT
DEFINES += IPFILTER_SUPPORT
DEFINES += DEVICEIO_SUPPORT
DEFINES += DPROVISIONING_SUPPORT

INCLUDEPATH += ./bm
INCLUDEPATH += ./http
INCLUDEPATH += ./onvif

SOURCES += \
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
    bm/ws.cpp \
    bm/xml_node.cpp \
    http/http_cln.cpp \
    http/http_parse.cpp \
    http/http_srv.cpp \
    onvif/onvif.cpp \
    onvif/onvif_act.cpp \
    onvif/onvif_api.cpp \
    onvif/onvif_cln.cpp \
    onvif/onvif_cm.cpp \
    onvif/onvif_event.cpp \
    onvif/onvif_pkt.cpp \
    onvif/onvif_probe.cpp \
    onvif/onvif_utils.cpp \
    onvif/onvif_http.cpp \
    onvif/soap.cpp \
    onvif/soap_parser.cpp \
