################OPTION###################
OUTPUT = onvifrtspserver
CCOMPILE = gcc
CPPCOMPILE = g++
COMPILEOPTION += -c -O3 -Wall
COMPILEOPTION += -DEPOLL
COMPILEOPTION += -DHTTPD
COMPILEOPTION += -DMEDIA_SUPPORT
COMPILEOPTION += -DIMAGE_SUPPORT
COMPILEOPTION += -DDEVICEIO_SUPPORT

COMPILEOPTION += -DMEDIA_LIVE
COMPILEOPTION += -DRTSP_OVER_HTTP
COMPILEOPTION += -DRTSP_RTCP
COMPILEOPTION += -DRTSP_METADATA
COMPILEOPTION += -DRTSP_REPLAY

ifneq ($(findstring SECURITY_SUPPORT, $(COMPILEOPTION)),)
COMPILEOPTION += -DHTTPS
endif

ifneq ($(findstring RTSP_OVER_WEBSOCKET, $(COMPILEOPTION)),)
COMPILEOPTION += -DRTSP_OVER_HTTP
endif

LINK = g++
LINKOPTION += -o $(OUTPUT)
INCLUDEDIR += -I.
INCLUDEDIR += -I./bm
INCLUDEDIR += -I./http
INCLUDEDIR += -I./librtmp
INCLUDEDIR += -I./media
INCLUDEDIR += -I./onvif
INCLUDEDIR += -I./rtmp
INCLUDEDIR += -I./rtp
INCLUDEDIR += -I./rtsp
INCLUDEDIR += -I./srt
INCLUDEDIR += -I./libsrt/include
INCLUDEDIR += -I./libsrtp/include
INCLUDEDIR += -I./ffmpeg/include
INCLUDEDIR += -I./openssl/include
INCLUDEDIR += -I./zlib/include
LIBDIRS += -L./libsrt/lib/linux
LIBDIRS += -L./libsrtp/lib/linux
LIBDIRS += -L./ffmpeg/lib/linux
LIBDIRS += -L./openssl/lib/linux
LIBDIRS += -L./zlib/lib/linux
OBJS += bm/base64.o
OBJS += bm/hqueue.o
OBJS += bm/hxml.o
OBJS += bm/linked_list.o
OBJS += bm/net_util.o
OBJS += bm/ppstack.o
OBJS += bm/rfc_md5.o
OBJS += bm/sha1.o
OBJS += bm/sha256.o
OBJS += bm/sys_buf.o
OBJS += bm/sys_log.o
OBJS += bm/sys_os.o
OBJS += bm/util.o
OBJS += bm/word_analyse.o
OBJS += bm/ws.o
OBJS += bm/xml_node.o
OBJS += http/http_auth.o
OBJS += http/http_cln.o
OBJS += http/http_parse.o
OBJS += http/http_srv.o
OBJS += media/media_codec.o
OBJS += media/media_parse.o
OBJS += media/media_util.o
OBJS += onvif/onvif.o
OBJS += onvif/onvif_cfg.o
OBJS += onvif/onvif_cm.o
OBJS += onvif/onvif_device.o
OBJS += onvif/onvif_event.o
OBJS += onvif/onvif_pkt.o
OBJS += onvif/onvif_probe.o
OBJS += onvif/onvif_srv.o
OBJS += onvif/onvif_timer.o
OBJS += onvif/onvif_utils.o
OBJS += onvif/soap.o
OBJS += onvif/soap_parser.o
OBJS += rtp/aac_rtp_rx.o
OBJS += rtp/h264_rtp_rx.o
OBJS += rtp/h264_util.o
OBJS += rtp/h265_rtp_rx.o
OBJS += rtp/h265_util.o
OBJS += rtp/mjpeg_rtp_rx.o
OBJS += rtp/mjpeg_tables.o
OBJS += rtp/mpeg4.o
OBJS += rtp/mpeg4_rtp_rx.o
OBJS += rtp/pcm_rtp_rx.o
OBJS += rtp/rtp_rx.o
OBJS += rtp/rtp_tx.o
OBJS += rtsp/rtsp_cfg.o
OBJS += rtsp/rtsp_mc.o
OBJS += rtsp/rtsp_media.o
OBJS += rtsp/rtsp_parse.o
OBJS += rtsp/rtsp_rsua.o
OBJS += rtsp/rtsp_srv.o
OBJS += rtsp/rtsp_stream.o
OBJS += rtsp/rtsp_timer.o
OBJS += rtsp/rtsp_util.o
OBJS += getopt.o
OBJS += main.o

ifneq ($(findstring HTTPD, $(COMPILEOPTION)),)
OBJS += http/httpd.o
endif

ifneq ($(findstring PROFILE_C_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_doorcontrol.o
endif

ifneq ($(findstring PROFILE_G_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_recording.o
endif

ifneq ($(findstring ACCESS_RULES, $(COMPILEOPTION)),)
OBJS += onvif/onvif_accessrules.o
endif

ifneq ($(findstring CREDENTIAL_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_credential.o
endif

ifneq ($(findstring DEVICEIO_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_deviceio.o
endif

ifneq ($(findstring MEDIA_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_media.o
endif

ifneq ($(findstring IMAGE_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_image.o
endif

ifneq ($(findstring SCHEDULE_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_schedule.o
endif

ifneq ($(findstring MEDIA2_SUPPORT, $(COMPILEOPTION)),)
ifeq ($(findstring onvif_media, $(OBJS)),)
OBJS += onvif/onvif_media.o
endif
OBJS += onvif/onvif_media2.o
endif

ifneq ($(findstring PTZ_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_ptz.o
endif

ifneq ($(findstring VIDEO_ANALYTICS, $(COMPILEOPTION)),)
OBJS += onvif/onvif_analytics.o
endif

ifneq ($(findstring THERMAL_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_thermal.o
endif

ifneq ($(findstring RECEIVER_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_receiver.o
endif

ifneq ($(findstring PROVISIONING_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_provisioning.o
endif

ifneq ($(findstring SECURITY_SUPPORT, $(COMPILEOPTION)),)
OBJS += onvif/onvif_security.o
endif

ffmpeg := 0

ifneq ($(findstring MEDIA_FILE, $(COMPILEOPTION)),)
ffmpeg := 1
endif

ifneq ($(findstring MEDIA_DEVICE, $(COMPILEOPTION)),)
ffmpeg := 1
endif

ifeq ($(ffmpeg), 1)
OBJS += media/audio_decoder.o
OBJS += media/audio_encoder.o
OBJS += media/avcodec_mutex.o
OBJS += media/video_decoder.o
OBJS += media/video_encoder.o
endif

ifneq ($(findstring MEDIA_FILE, $(COMPILEOPTION)),)
OBJS += media/file_demux.o
endif

ifneq ($(findstring MEDIA_DEVICE, $(COMPILEOPTION)),)
OBJS += media/audio_capture.o
OBJS += media/screen_capture.o
OBJS += media/video_capture.o
OBJS += media/window_capture.o

ifeq ($(findstring IOS, $(COMPILEOPTION)),)
OBJS += media/alsa.o
OBJS += media/v4l2.o
OBJS += media/v4l2_comm.o
OBJS += media/xcb_util.o
OBJS += media/audio_capture_linux.o
OBJS += media/screen_capture_linux.o
OBJS += media/video_capture_linux.o
OBJS += media/window_capture_linux.o
else
OBJS += media/audio_capture_avf.o
OBJS += media/audio_capture_mac.o
OBJS += media/screen_capture_avf.o
OBJS += media/screen_capture_mac.o
OBJS += media/video_capture_avf.o
OBJS += media/video_capture_mac.o
OBJS += media/window_capture_avf.o
OBJS += media/window_capture_mac.o
endif
endif

ifneq ($(findstring MEDIA_LIVE, $(COMPILEOPTION)),)
OBJS += media/live_audio.o
OBJS += media/live_video.o
endif

ifneq ($(findstring MEDIA_PUSHER, $(COMPILEOPTION)),)
OBJS += media/media_pusher.o
endif

ifneq ($(findstring MEDIA_PROXY, $(COMPILEOPTION)),)
OBJS += http/http_mjpeg_cln.o
OBJS += http/http_test.o
OBJS += media/media_proxy.o
OBJS += rtsp/rtsp_cln.o
OBJS += rtsp/rtsp_rcua.o
endif

ifneq ($(findstring RTMP_PROXY, $(COMPILEOPTION)),)
OBJS += librtmp/amf.o
OBJS += librtmp/hashswf.o
OBJS += librtmp/log.o
OBJS += librtmp/parseurl.o
OBJS += librtmp/rtmp.o
OBJS += rtmp/rtmp_cln.o
endif

ifneq ($(findstring SRT_PROXY, $(COMPILEOPTION)),)
OBJS += rtp/ts_parser.o
OBJS += srt/srt_cln.o
endif

ifneq ($(findstring RTSP_BACKCHANNEL, $(COMPILEOPTION)),)
OBJS += rtsp/rtsp_srv_backchannel.o

ifneq ($(findstring MEDIA_DEVICE, $(COMPILEOPTION)),)
OBJS += media/audio_play.o

ifeq ($(findstring IOS, $(COMPILEOPTION)),)
OBJS += media/audio_play_linux.o
else
OBJS += media/audio_play_avf.o
OBJS += media/audio_play_mac.o
endif
endif
endif

ifneq ($(findstring RTSP_OVER_HTTP, $(COMPILEOPTION)),)
OBJS += rtsp/rtsp_http.o
endif

ifneq ($(findstring RTSP_OVER_WEBSOCKET, $(COMPILEOPTION)),)
OBJS += rtsp/rtsp_srv_ws.o
endif

ifneq ($(findstring RTSP_RTCP, $(COMPILEOPTION)),)
OBJS += rtp/rtcp.o
endif

ifneq ($(findstring HTTP_NOTIFY, $(COMPILEOPTION)),)
OBJS += http/http_notify.o
endif

ifeq ($(ffmpeg), 1)
SHAREDLIB += -lavcodec
SHAREDLIB += -lavformat
SHAREDLIB += -lavutil
SHAREDLIB += -lswresample
SHAREDLIB += -lswscale
SHAREDLIB += -lopus
SHAREDLIB += -lx264
SHAREDLIB += -lx265
endif

ifneq ($(findstring MEDIA_DEVICE, $(COMPILEOPTION)),)
ifeq ($(findstring IOS, $(COMPILEOPTION)),)
SHAREDLIB += -lasound
SHAREDLIB += -lxcb
else
SHAREDLIB += -framework AudioToolbox
SHAREDLIB += -framework AVFoundation
SHAREDLIB += -framework CoreAudio
SHAREDLIB += -framework CoreFoundation
SHAREDLIB += -framework CoreGraphics
SHAREDLIB += -framework CoreMedia
SHAREDLIB += -framework CoreVideo
SHAREDLIB += -framework Foundation
SHAREDLIB += -framework ScreenCapturekit
endif
endif

ifeq ($(findstring IOS, $(COMPILEOPTION)),)
SHAREDLIB += -lrt
endif

ifneq ($(findstring SRT_PROXY, $(COMPILEOPTION)),)
SHAREDLIB += -lsrt
endif

ifneq ($(findstring SRTP, $(COMPILEOPTION)),)
SHAREDLIB += -lsrtp2
endif

openssl := 0

ifneq ($(findstring HTTPS, $(COMPILEOPTION)),)
openssl := 1
endif

ifneq ($(findstring RTSPS, $(COMPILEOPTION)),)
openssl := 1
endif

ifneq ($(findstring RTMP_PROXY, $(COMPILEOPTION)),)
openssl := 1
endif

ifneq ($(findstring SRT_PROXY, $(COMPILEOPTION)),)
openssl := 1
endif

ifeq ($(openssl), 1)
SHAREDLIB += -lcrypto
SHAREDLIB += -lssl
SHAREDLIB += -lz
endif

SHAREDLIB += -lpthread

APPENDLIB = 

################OPTION END################

$(OUTPUT):$(OBJS) $(APPENDLIB)
	./mklinks.sh
	$(LINK) $(LINKOPTION) $(LIBDIRS) $(OBJS) $(SHAREDLIB) $(APPENDLIB) 

clean: 
	rm -f $(OBJS)
	rm -f $(OUTPUT)
all: clean $(OUTPUT)
.PRECIOUS:%.cpp %.cc %.cxx %.c %.m %.mm
.SUFFIXES:
.SUFFIXES: .cpp .cc .cxx .c .m .mm .o

.cpp.o:
	$(CPPCOMPILE) -c -o $*.o $(COMPILEOPTION) $(INCLUDEDIR)  $*.cpp
	
.cc.o:
	$(CCOMPILE) -c -o $*.o $(COMPILEOPTION) $(INCLUDEDIR)  $*.cc

.cxx.o:
	$(CPPCOMPILE) -c -o $*.o $(COMPILEOPTION) $(INCLUDEDIR)  $*.cxx

.c.o:
	$(CCOMPILE) -c -o $*.o $(COMPILEOPTION) $(INCLUDEDIR) $*.c

.m.o:
	$(CCOMPILE) -c -o $*.o $(COMPILEOPTION) $(INCLUDEDIR) $*.m

.mm.o:
	$(CPPCOMPILE) -c -o $*.o $(COMPILEOPTION) $(INCLUDEDIR) $*.mm
	
