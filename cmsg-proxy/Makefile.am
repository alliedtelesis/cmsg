lib_LTLIBRARIES = libcmsgproxy.la

common.pb-c.c common.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) common.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I. --c_out=. common.proto

libcmsgproxy_la_SOURCES = \
	common.pb-c.c \
	common.pb-c.h \
	cmsg_proxy.c \
	cmsg_proxy.h \
	cmsg_proxy_mem.c \
	cmsg_proxy_mem.h

libcmsgproxy_la_LIBADD = \
	$(GLIB_LIBS) \
	-lcmsg \
	-ljansson \
	-lprotobuf2json-c

libcmsgproxy_la_CFLAGS = -Werror -Wall $(GLIB_CFLAGS)

BUILT_SOURCES = common.pb-c.c common.pb-c.h
CLEANFILES = $(BUILT_SOURCES)

