lib_LTLIBRARIES = libcmsg.la
bin_PROGRAMS =

pkgconfiglibdir = $(libdir)/pkgconfig
pkgconfiglib_DATA = cmsg.pc

cmsg_sub_service.pb-c.c cmsg_sub_service.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) cmsg_sub_service.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I. --c_out=. cmsg_sub_service.proto

libcmsg_la_SOURCES = \
	cmsg_sub_service.pb-c.c \
	cmsg_sub_service.pb-c.h \
	cmsg.c \
	cmsg.h \
	cmsg_private.h \
	cmsg_error.h \
	cmsg_client.c \
	cmsg_client.h \
	cmsg_composite_client.c \
	cmsg_composite_client.h \
	cmsg_pub.c \
	cmsg_pub.h \
	cmsg_queue.c \
	cmsg_queue.h \
	cmsg_server.c \
	cmsg_server.h \
	cmsg_sub.c \
	cmsg_sub.h \
	cmsg_transport.c \
	cmsg_transport.h \
	cmsg_transport_loopback.c \
	cmsg_transport_tcp.c \
	cmsg_transport_tipc.c \
	cmsg_transport_udt.c \
	cmsg_transport_unix.c

libcmsg_la_LIBADD = $(GLIB_LIBS) $(GMEM_DIAG_LIBS) -lutility -lprotobuf-c

if HAVE_COUNTERD
libcmsg_la_LIBADD += -lcntr
endif

if HAVE_VCSTACK
libcmsg_la_SOURCES += \
	cmsg_transport_cpg.c \
	cmsg_transport_tipc_broadcast.c
libcmsg_la_LIBADD += -lcpg -lcoroipcc
endif

libcmsg_la_CFLAGS = -Werror -Wall $(GLIB_CFLAGS)

BUILT_SOURCES = cmsg_sub_service.pb-c.c cmsg_sub_service.pb-c.h
CLEANFILES = $(BUILT_SOURCES)

if BUILD_UNITTEST
bin_PROGRAMS += cmsg_unit_tests
cmsg_unit_tests_SOURCES = \
	cmsg_unit_tests.c
cmsg_unit_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS)
cmsg_unit_tests_LDFLAGS = -static
cmsg_unit_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS)
endif # BUILD_UNITTEST
