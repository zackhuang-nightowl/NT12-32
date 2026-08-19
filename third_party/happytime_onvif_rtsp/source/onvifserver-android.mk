################OPTION###################
OUTPUT = onvifserver
NDK=/home/android-ndk-r25c
API=23
PLATFORM=aarch64
TOOLCHAIN=$(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
SYSROOT=$(NDK)/toolchains/llvm/prebuilt/linux-x86_64/sysroot
ifneq ($(findstring armv7a, $(PLATFORM)),)
TARGET=$(PLATFORM)-linux-androideabi
RANLIB=$(TOOLCHAIN)/arm-linux-androideabi-ranlib
LINK = $(TOOLCHAIN)/clang++
endif
ifneq ($(findstring aarch64, $(PLATFORM)),)
TARGET=$(PLATFORM)-linux-android
RANLIB=$(TOOLCHAIN)/$(TARGET)-ranlib
LINK = $(TOOLCHAIN)/clang++
endif
CCOMPILE = $(TOOLCHAIN)/clang
CPPCOMPILE = $(TOOLCHAIN)/clang++
COMPILEOPTION += -fPIC -DANDROID --sysroot=$(SYSROOT)
COMPILEOPTION += -c -target $(TARGET)$(API) -O3 -Wall 
COMPILEOPTION += -DEPOLL
COMPILEOPTION += -DHTTPD
COMPILEOPTION += -DMEDIA_SUPPORT
COMPILEOPTION += -DMPEG4_SUPPORT
COMPILEOPTION += -DIMAGE_SUPPORT
COMPILEOPTION += -DDEVICEIO_SUPPORT
COMPILEOPTION += -DPROFILE_G_SUPPORT
COMPILEOPTION += -DPROFILE_C_SUPPORT
COMPILEOPTION += -DCREDENTIAL_SUPPORT
COMPILEOPTION += -DACCESS_RULES
COMPILEOPTION += -DSCHEDULE_SUPPORT
COMPILEOPTION += -DAUDIO_SUPPORT
COMPILEOPTION += -DMEDIA2_SUPPORT
COMPILEOPTION += -DPTZ_SUPPORT
COMPILEOPTION += -DVIDEO_ANALYTICS
COMPILEOPTION += -DTHERMAL_SUPPORT
COMPILEOPTION += -DRECEIVER_SUPPORT
COMPILEOPTION += -DIPFILTER_SUPPORT
COMPILEOPTION += -DSTORAGE_SUPPORT
COMPILEOPTION += -DPROVISIONING_SUPPORT
COMPILEOPTION += -DGEOLOCATION_SUPPORT
COMPILEOPTION += -DDOT11_SUPPORT
COMPILEOPTION += -DNO_RTSP_SERVER

ifneq ($(findstring SECURITY_SUPPORT, $(COMPILEOPTION)),)
COMPILEOPTION += -DHTTPS
endif

LINKOPTION += -target $(TARGET)$(API) -o $(OUTPUT)
INCLUDEDIR += -I./bm
INCLUDEDIR += -I./http
INCLUDEDIR += -I./onvif
INCLUDEDIR += -I./openssl/include
ifneq ($(findstring armv7a, $(PLATFORM)),)
LIBDIRS += -L./openssl/lib/armeabi-v7a
endif
ifneq ($(findstring aarch64, $(PLATFORM)),)
LIBDIRS += -L./openssl/lib/arm64-v8a
endif
OBJS += bm/base64.o
OBJS += bm/hqueue.o
OBJS += bm/hxml.o
OBJS += bm/linked_list.o
OBJS += bm/ppstack.o
OBJS += bm/rfc_md5.o
OBJS += bm/sha1.o
OBJS += bm/sha256.o
OBJS += bm/sys_buf.o
OBJS += bm/sys_log.o
OBJS += bm/sys_os.o
OBJS += bm/util.o
OBJS += bm/word_analyse.o
OBJS += bm/xml_node.o
OBJS += http/http_auth.o
OBJS += http/http_cln.o
OBJS += http/http_parse.o
OBJS += http/http_srv.o
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

ifneq ($(findstring HTTPS, $(COMPILEOPTION)),)
SHAREDLIB += -lcrypto
SHAREDLIB += -lssl
endif

SHAREDLIB +=

APPENDLIB = 

################OPTION END################

$(OUTPUT):$(OBJS) $(APPENDLIB)
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
	
