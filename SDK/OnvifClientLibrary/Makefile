################OPTION###################
OUTPUT = libonvifclient.so
CCOMPILE = gcc
CPPCOMPILE = g++
COMPILEOPTION += -c -O3 -fPIC -Wall
COMPILEOPTION += -DEPOLL
COMPILEOPTION += -DHTTPS
COMPILEOPTION += -DPROFILE_C_SUPPORT
COMPILEOPTION += -DPROFILE_G_SUPPORT
COMPILEOPTION += -DTHERMAL_SUPPORT
COMPILEOPTION += -DCREDENTIAL_SUPPORT
COMPILEOPTION += -DACCESS_RULES
COMPILEOPTION += -DSCHEDULE_SUPPORT
COMPILEOPTION += -DRECEIVER_SUPPORT
COMPILEOPTION += -DIPFILTER_SUPPORT
COMPILEOPTION += -DDEVICEIO_SUPPORT
COMPILEOPTION += -DPROVISIONING_SUPPORT
# ---- nop_rtsp media component (RTSP/RTP) ----
COMPILEOPTION += -DMEDIA_LIVE
COMPILEOPTION += -DRTSP_BACKCHANNEL
COMPILEOPTION += -DBACKCHANNEL
COMPILEOPTION += -DRTSP_OVER_HTTP
COMPILEOPTION += -DOVER_HTTP
COMPILEOPTION += -DRTSP_OVER_WEBSOCKET
COMPILEOPTION += -DOVER_WEBSOCKET
COMPILEOPTION += -DRTSPS
COMPILEOPTION += -DRTSP_SRTP
COMPILEOPTION += -DSRTP
LINK = g++
LINKOPTION = -shared -o $(OUTPUT)
INCLUDEDIR += -I.
INCLUDEDIR += -I./bm
INCLUDEDIR += -I./http
INCLUDEDIR += -I./onvif
INCLUDEDIR += -I./openssl/include
INCLUDEDIR += -I./rtp
INCLUDEDIR += -I./rtsp
INCLUDEDIR += -I./media
INCLUDEDIR += -I./libsrtp/include
LIBDIRS += -L./openssl/lib/linux
LIBDIRS += -L./libsrtp/lib/linux
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
OBJS += onvif/onvif.o
OBJS += onvif/onvif_act.o
OBJS += onvif/onvif_api.o
OBJS += onvif/onvif_cln.o
OBJS += onvif/onvif_cm.o
OBJS += onvif/onvif_event.o
OBJS += onvif/onvif_pkt.o
OBJS += onvif/onvif_probe.o
OBJS += onvif/onvif_utils.o
OBJS += onvif/onvif_http.o
OBJS += onvif/soap.o
OBJS += onvif/soap_parser.o
# ---- rtp/ ----
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
OBJS += rtp/rtcp.o
OBJS += rtp/rtp_rx.o
OBJS += rtp/rtp_tx.o
OBJS += rtp/ts_parser.o
# ---- rtsp/ ----
OBJS += rtsp/rtsp_backchannel.o
OBJS += rtsp/rtsp_cfg.o
OBJS += rtsp/rtsp_cln.o
OBJS += rtsp/rtsp_http.o
OBJS += rtsp/rtsp_mc.o
OBJS += rtsp/rtsp_media.o
OBJS += rtsp/rtsp_parse.o
OBJS += rtsp/rtsp_rcua.o
OBJS += rtsp/rtsp_rsua.o
OBJS += rtsp/rtsp_srv.o
OBJS += rtsp/rtsp_srv_backchannel.o
OBJS += rtsp/rtsp_srv_ws.o
OBJS += rtsp/rtsp_stream.o
OBJS += rtsp/rtsp_timer.o
OBJS += rtsp/rtsp_util.o
# ---- media/ (ffmpeg-free live-injection slice) ----
OBJS += media/media_util.o
OBJS += media/live_video.o
OBJS += media/live_audio.o
# ---- nop_rtsp facade ----
OBJS += nop_rtsp.o

ifneq ($(findstring HTTPS, $(COMPILEOPTION)),)
SHAREDLIB += -lcrypto
SHAREDLIB += -lssl
endif

SHAREDLIB += -lpthread
SHAREDLIB += -lsrtp2

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
	
