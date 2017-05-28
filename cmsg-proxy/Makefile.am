lib_LTLIBRARIES = libcmsgproxy.la
bin_PROGRAMS =

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

if BUILD_UNITTEST
cmsg_proxy_unit_tests_proxy_def.c cmsg_proxy_unit_tests_proxy_def.h: $(PROTOC_CMSG_PATH)$(EXEEXT) cmsg_proxy_unit_tests.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I. --c_out=. cmsg_proxy_unit_tests.proto

bin_PROGRAMS += cmsg_proxy_unit_tests
cmsg_proxy_unit_tests_SOURCES = \
	common.pb-c.c \
	common.pb-c.h \
	cmsg_proxy_unit_tests_proxy_def.c \
	cmsg_proxy_unit_tests_proxy_def.h \
	cmsg_proxy_unit_tests_api_auto.c \
	cmsg_proxy_unit_tests_api_auto.h \
	cmsg_proxy_unit_tests_impl_auto.c \
	cmsg_proxy_unit_tests_impl_auto.h \
	cmsg_proxy_unit_tests.pb-c.c \
	cmsg_proxy_unit_tests.pb-c.h \
	cmsg_proxy_unit_tests.c
cmsg_proxy_unit_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS)
cmsg_proxy_unit_tests_LDFLAGS = -static
cmsg_proxy_unit_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) \
                                -lcmsg -lprotobuf-c -lprotobuf2json-c \
                                -ljansson
endif # BUILD_UNITTEST

