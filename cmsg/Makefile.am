lib_LTLIBRARIES = libcmsg.la
bin_PROGRAMS =

pkgconfiglibdir = $(libdir)/pkgconfig
pkgconfiglib_DATA = cmsg.pc

src/cmsg_sub_service.pb-c.c src/cmsg_sub_service.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/cmsg_sub_service.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I. --c_out=. src/cmsg_sub_service.proto

libcmsg_la_SOURCES = \
	src/cmsg_sub_service.pb-c.c \
	src/cmsg_sub_service.pb-c.h \
	src/transport/cmsg_transport_loopback.c \
	src/transport/cmsg_transport_tcp.c \
	src/transport/cmsg_transport_tipc.c \
	src/transport/cmsg_transport_udt.c \
	src/transport/cmsg_transport_unix.c \
	src/transport/cmsg_transport_tipc_broadcast.c \
	src/transport/cmsg_transport.c \
	src/broadcast_client/cmsg_broadcast_client_private.h \
	src/broadcast_client/cmsg_broadcast_client.c \
	src/broadcast_client/cmsg_broadcast_client_conn_mgmt.c \
	src/cmsg.c \
	src/cmsg_client.c \
	src/cmsg_composite_client.c \
	src/cmsg_composite_client_private.h \
	src/cmsg_debug.c \
	src/cmsg_pub.c \
	src/cmsg_queue.c \
	src/cmsg_server.c \
	src/cmsg_sub.c \
	src/cmsg_mesh.c \
	src/cmsg_glib_helpers.c \
	src/cmsg_pthread_helpers.c \
	include/cmsg/cmsg.h \
	include/cmsg/cmsg_private.h \
	include/cmsg/cmsg_error.h \
	include/cmsg/cmsg_client.h \
	include/cmsg/cmsg_composite_client.h \
	include/cmsg_debug.h \
	include/cmsg/cmsg_pub.h \
	include/cmsg/cmsg_queue.h \
	include/cmsg/cmsg_server.h \
	include/cmsg/cmsg_sub.h \
	include/cmsg/cmsg_transport.h \
	include/cmsg/cmsg_broadcast_client_server.h \
	include/cmsg/cmsg_mesh.h \
	include/cmsg/cmsg_glib_helpers.h


libcmsg_la_LIBADD = $(GLIB_LIBS) $(GMEM_DIAG_LIBS) -lutility -lprotobuf-c

if HAVE_COUNTERD
libcmsg_la_LIBADD += -lcntr
endif

libcmsg_la_CFLAGS = -Werror -Wall $(GLIB_CFLAGS) -Iinclude/cmsg

BUILT_SOURCES = src/cmsg_sub_service.pb-c.c src/cmsg_sub_service.pb-c.h
CLEANFILES = $(BUILT_SOURCES)

if BUILD_UNITTEST
bin_PROGRAMS += cmsg_unit_tests
cmsg_unit_tests_SOURCES = \
	test/unit/cmsg_pub_unit_tests.c \
	test/unit/cmsg_composite_client_unit_tests.c
cmsg_unit_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS) -Iinclude -Iinclude/cmsg
cmsg_unit_tests_LDFLAGS = -static
cmsg_unit_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) -ldebug_awplus -lcmsg -lprotobuf-c

test/functional/cmsg_functional_tests_api_auto.c \
test/functional/cmsg_functional_tests_api_auto.h \
test/functional/cmsg_functional_tests_impl_auto.c \
test/functional/cmsg_functional_tests_impl_auto.h \
test/functional/cmsg_functional_tests.pb-c.c \
test/functional/cmsg_functional_tests.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) test/functional/cmsg_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) --proto_path=. --proto_path=src --c_out=. test/functional/cmsg_functional_tests.proto

bin_PROGRAMS += cmsg_functional_tests
cmsg_functional_tests_SOURCES = \
	test/functional/cmsg_functional_tests_api_auto.c \
	test/functional/cmsg_functional_tests_api_auto.h \
	test/functional/cmsg_functional_tests_impl_auto.c \
	test/functional/cmsg_functional_tests_impl_auto.h \
	test/functional/cmsg_functional_tests.pb-c.c \
	test/functional/cmsg_functional_tests.pb-c.h \
	test/functional/client_server_rpc_tests.c \
	test/functional/client_server_oneway_tests.c \
	test/functional/client_server_echo_tests.c \
	test/functional/publisher_subscriber_tests.c \
	test/functional/client_queuing_tests.c \
	test/functional/publisher_queuing_tests.c \
	test/functional/broadcast_client_tests.c \
	test/functional/pthread_helpers_tests.c
cmsg_functional_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS) -Iinclude -Iinclude/cmsg
cmsg_functional_tests_LDFLAGS = -static
cmsg_functional_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) -ldebug_awplus -lcmsg -lprotobuf-c
endif # BUILD_UNITTEST
