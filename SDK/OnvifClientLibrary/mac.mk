################OPTION###################
OUTPUT = libonvifclient.so
CCOMPILE = gcc
CPPCOMPILE = g++
COMPILEOPTION += -c -O3 -fPIC -Wall
COMPILEOPTION += -DIOS
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
LINK = g++
LINKOPTION = -shared -o $(OUTPUT)
INCLUDEDIR += -I.
INCLUDEDIR += -I./bm
INCLUDEDIR += -I./http
INCLUDEDIR += -I./onvif
INCLUDEDIR += -I/usr/local/include
LIBDIRS += -L/usr/local/lib
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

ifneq ($(findstring HTTPS, $(COMPILEOPTION)),)
SHAREDLIB += -lcrypto
SHAREDLIB += -lssl
endif

SHAREDLIB += -lpthread

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
	
