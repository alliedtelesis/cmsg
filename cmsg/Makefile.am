lib_LTLIBRARIES = libcmsg.la
bin_PROGRAMS = cmsg cmsg_sld cmsg_pssd

pkgconfiglibdir = $(libdir)/pkgconfig
pkgconfiglib_DATA = cmsg.pc

src/cmsg_api_auto.c \
src/cmsg_api_auto.h \
src/cmsg_impl_auto.c \
src/cmsg_impl_auto.h \
src/cmsg_impl_types_auto.h \
src/cmsg.pb-c.c \
src/cmsg.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/cmsg.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src --c_out=$(top_srcdir)/cmsg/src $(top_srcdir)/cmsg/src/cmsg.proto

src/service_listener/configuration_api_auto.c \
src/service_listener/configuration_api_auto.h \
src/service_listener/configuration_impl_auto.c \
src/service_listener/configuration_impl_auto.h \
src/service_listener/configuration_impl_types_auto.h \
src/service_listener/configuration.pb-c.c \
src/service_listener/configuration.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/service_listener/configuration.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src/service_listener -I$(top_srcdir)/cmsg/src --c_out=$(top_srcdir)/cmsg/src/service_listener $(top_srcdir)/cmsg/src/service_listener/configuration.proto

src/service_listener/remote_sync_api_auto.c \
src/service_listener/remote_sync_api_auto.h \
src/service_listener/remote_sync_impl_auto.c \
src/service_listener/remote_sync_impl_auto.h \
src/service_listener/remote_sync_impl_types_auto.h \
src/service_listener/remote_sync.pb-c.c \
src/service_listener/remote_sync.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/service_listener/remote_sync.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src/service_listener -I$(top_srcdir)/cmsg/src --c_out=$(top_srcdir)/cmsg/src/service_listener $(top_srcdir)/cmsg/src/service_listener/remote_sync.proto

src/service_listener/events_api_auto.c \
src/service_listener/events_api_auto.h \
src/service_listener/events_impl_auto.c \
src/service_listener/events_impl_auto.h \
src/service_listener/events_impl_types_auto.h \
src/service_listener/events.pb-c.c \
src/service_listener/events.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/service_listener/events.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src/service_listener -I$(top_srcdir)/cmsg/src --c_out=$(top_srcdir)/cmsg/src/service_listener $(top_srcdir)/cmsg/src/service_listener/events.proto

src/publisher_subscriber/cmsg_sub_service.pb-c.c \
src/publisher_subscriber/cmsg_sub_service.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/publisher_subscriber/cmsg_sub_service.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src/publisher_subscriber --c_out=$(top_srcdir)/cmsg/src/publisher_subscriber/ $(top_srcdir)/cmsg/src/publisher_subscriber/cmsg_sub_service.proto

src/publisher_subscriber/configuration_api_auto.c \
src/publisher_subscriber/configuration_api_auto.h \
src/publisher_subscriber/configuration_impl_auto.c \
src/publisher_subscriber/configuration_impl_auto.h \
src/publisher_subscriber/configuration_impl_types_auto.h \
src/publisher_subscriber/configuration.pb-c.c \
src/publisher_subscriber/configuration.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/publisher_subscriber/configuration.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src/publisher_subscriber -I$(top_srcdir)/cmsg/src --c_out=$(top_srcdir)/cmsg/src/publisher_subscriber $(top_srcdir)/cmsg/src/publisher_subscriber/configuration.proto

src/publisher_subscriber/remote_sync_api_auto.c \
src/publisher_subscriber/remote_sync_api_auto.h \
src/publisher_subscriber/remote_sync_impl_auto.c \
src/publisher_subscriber/remote_sync_impl_auto.h \
src/publisher_subscriber/remote_sync_impl_types_auto.h \
src/publisher_subscriber/remote_sync.pb-c.c \
src/publisher_subscriber/remote_sync.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/publisher_subscriber/remote_sync.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src/publisher_subscriber -I$(top_srcdir)/cmsg/src --c_out=$(top_srcdir)/cmsg/src/publisher_subscriber $(top_srcdir)/cmsg/src/publisher_subscriber/remote_sync.proto

libcmsg_la_SOURCES = \
	src/cmsg.pb-c.c \
	src/cmsg.pb-c.h \
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
	src/publisher_subscriber/cmsg_sub_service.pb-c.c \
	src/publisher_subscriber/cmsg_sub_service.pb-c.h \
	src/publisher_subscriber/cmsg_pub.c \
	src/publisher_subscriber/cmsg_sub.c \
	src/cmsg_queue.c \
	src/cmsg_server.c \
	src/cmsg_mesh.c \
	src/cmsg_glib_helpers.c \
	src/validation/cmsg_validation.c \
	src/cmsg_pthread_helpers.c \
	src/service_listener/cmsg_sl_api.c \
	src/service_listener/configuration_api_auto.c \
	src/service_listener/configuration.pb-c.c \
	src/service_listener/events_impl_auto.c \
	src/service_listener/events.pb-c.c \
	src/publisher_subscriber/cmsg_pss_api.c \
	src/publisher_subscriber/configuration_api_auto.c \
	src/publisher_subscriber/configuration.pb-c.c

libcmsg_la_LIBADD = $(GLIB_LIBS) $(GMEM_DIAG_LIBS) $(DEBUG_AWPLUS_LIBS) $(UTILITY_LIBS) -lprotobuf-c

if HAVE_COUNTERD
libcmsg_la_LIBADD += -lcntr
endif

libcmsg_la_CFLAGS = -Werror -Wall $(GLIB_CFLAGS) -include $(top_builddir)/config.h
libcmsg_la_CPPFLAGS = -I$(top_srcdir)/cmsg/include/cmsg -I$(top_srcdir)/cmsg/include -I$(top_srcdir)/cmsg/src
libcmsg_ladir       = $(includedir)/cmsg
libcmsg_la_HEADERS  = \
	include/cmsg/cmsg.h \
	include/cmsg/cmsg_private.h \
	include/cmsg/cmsg_error.h \
	include/cmsg/cmsg_client.h \
	include/cmsg/cmsg_composite_client.h \
	include/cmsg/cmsg_debug.h \
	include/cmsg/cmsg_pub.h \
	include/cmsg/cmsg_queue.h \
	include/cmsg/cmsg_server.h \
	include/cmsg/cmsg_sub.h \
	include/cmsg/cmsg_transport.h \
	include/cmsg/cmsg_broadcast_client.h \
	include/cmsg/cmsg_mesh.h \
	include/cmsg/cmsg_glib_helpers.h \
	include/cmsg/cmsg_validation.h \
	include/cmsg/cmsg_pthread_helpers.h \
	include/cmsg/cmsg_sl_config.h \
	include/cmsg/cmsg_pss_config.h

BUILT_SOURCES =  \
	src/cmsg_api_auto.c \
	src/cmsg_api_auto.h \
	src/cmsg_impl_auto.c \
	src/cmsg_impl_auto.h \
	src/cmsg.pb-c.c \
	src/cmsg.pb-c.h \
	src/service_listener/configuration_api_auto.c \
	src/service_listener/configuration_api_auto.h \
	src/service_listener/configuration_impl_auto.c \
	src/service_listener/configuration_impl_auto.h \
	src/service_listener/configuration.pb-c.c \
	src/service_listener/configuration.pb-c.h \
	src/service_listener/remote_sync_api_auto.c \
	src/service_listener/remote_sync_api_auto.h \
	src/service_listener/remote_sync_impl_auto.c \
	src/service_listener/remote_sync_impl_auto.h \
	src/service_listener/remote_sync.pb-c.c \
	src/service_listener/remote_sync.pb-c.h \
	src/service_listener/events_api_auto.c \
	src/service_listener/events_api_auto.h \
	src/service_listener/events_impl_auto.c \
	src/service_listener/events_impl_auto.h \
	src/service_listener/events.pb-c.c \
	src/service_listener/events.pb-c.h \
	src/publisher_subscriber/cmsg_sub_service.pb-c.c \
	src/publisher_subscriber/cmsg_sub_service.pb-c.h \
	src/publisher_subscriber/configuration_api_auto.c \
	src/publisher_subscriber/configuration_api_auto.h \
	src/publisher_subscriber/configuration_impl_auto.c \
	src/publisher_subscriber/configuration_impl_auto.h \
	src/publisher_subscriber/configuration.pb-c.c \
	src/publisher_subscriber/configuration.pb-c.h \
	src/publisher_subscriber/remote_sync_api_auto.c \
	src/publisher_subscriber/remote_sync_api_auto.h \
	src/publisher_subscriber/remote_sync_impl_auto.c \
	src/publisher_subscriber/remote_sync_impl_auto.h \
	src/publisher_subscriber/remote_sync.pb-c.c \
	src/publisher_subscriber/remote_sync.pb-c.h

if BUILD_UNITTEST
BUILT_SOURCES += \
	test/functional/cmsg_functional_tests_api_auto.c \
	test/functional/cmsg_functional_tests_api_auto.h \
	test/functional/cmsg_functional_tests_impl_auto.c \
	test/functional/cmsg_functional_tests_impl_auto.h \
	test/functional/cmsg_functional_tests_types_auto.h \
	test/functional/cmsg_functional_tests.pb-c.c \
	test/functional/cmsg_functional_tests.pb-c.h \
	test/functional/ant_result_types_auto.h \
	test/functional/ant_result.pb-c.c \
	test/functional/ant_result.pb-c.h
endif

CLEANFILES = $(BUILT_SOURCES)

cmsg_SOURCES = src/shell_application/cmsg_shell_application.c
cmsg_LDADD   = libcmsg.la $(GLIB_LIBS) -ldl -ljansson -lprotobuf2json-c -lprotobuf-c
cmsg_CFLAGS  = -Werror -Wall -include $(top_builddir)/config.h $(GLIB_CFLAGS)
cmsg_CPPFLAGS = -I$(top_srcdir)/cmsg/include/cmsg

cmsg_sld_SOURCES = \
	src/service_listener/main.c \
	src/service_listener/configuration.c \
	src/service_listener/remote_sync.c \
	src/service_listener/data.c \
	src/service_listener/configuration_impl_auto.c \
	src/service_listener/configuration.pb-c.c \
	src/service_listener/remote_sync_api_auto.c \
	src/service_listener/remote_sync_impl_auto.c \
	src/service_listener/remote_sync.pb-c.c \
	src/service_listener/events_api_auto.c \
	src/service_listener/events.pb-c.c
cmsg_sld_LDADD   = libcmsg.la $(GLIB_LIBS) -lprotobuf-c
cmsg_sld_CFLAGS  = -Werror -Wall -include $(top_builddir)/config.h $(GLIB_CFLAGS)
cmsg_sld_CPPFLAGS = -I$(top_srcdir)/cmsg/include/cmsg -I$(top_srcdir)/cmsg/include -I$(top_srcdir)/cmsg/src

cmsg_pssd_SOURCES = \
	src/publisher_subscriber/main.c \
	src/publisher_subscriber/configuration.c \
	src/publisher_subscriber/data.c \
	src/publisher_subscriber/remote_sync.c \
	src/publisher_subscriber/configuration_impl_auto.c \
	src/publisher_subscriber/configuration.pb-c.c \
	src/publisher_subscriber/remote_sync_api_auto.c \
	src/publisher_subscriber/remote_sync_impl_auto.c \
	src/publisher_subscriber/remote_sync.pb-c.c
cmsg_pssd_LDADD   = libcmsg.la $(GLIB_LIBS) -lprotobuf-c
cmsg_pssd_CFLAGS  = -Werror -Wall -include $(top_builddir)/config.h $(GLIB_CFLAGS)
cmsg_pssd_CPPFLAGS = -I$(top_srcdir)/cmsg/include/cmsg -I$(top_srcdir)/cmsg/include -I$(top_srcdir)/cmsg/src

if BUILD_UNITTEST
test/unit/cmsg_unit_tests_api_auto.c \
test/unit/cmsg_unit_tests_api_auto.h \
test/unit/cmsg_unit_tests_impl_auto.c \
test/unit/cmsg_unit_tests_impl_auto.h \
test/unit/cmsg_unit_tests_types_auto.h \
test/unit/cmsg_unit_tests.pb-c.c \
test/unit/cmsg_unit_tests.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) test/unit/cmsg_unit_tests.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/test/unit --c_out=$(top_srcdir)/cmsg/test/unit $(top_srcdir)/cmsg/test/unit/cmsg_unit_tests.proto

bin_PROGRAMS += cmsg_unit_tests
cmsg_unit_tests_SOURCES = \
	test/unit/cmsg_unit_tests_api_auto.c \
	test/unit/cmsg_unit_tests_api_auto.h \
	test/unit/cmsg_unit_tests_impl_auto.c \
	test/unit/cmsg_unit_tests_impl_auto.h \
	test/unit/cmsg_unit_tests.pb-c.c \
	test/unit/cmsg_unit_tests.pb-c.h \
	test/unit/cmsg_pub_unit_tests.c \
	test/unit/cmsg_composite_client_unit_tests.c \
	test/unit/cmsg_unit_tests.c
cmsg_unit_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS) -include $(top_builddir)/config.h
cmsg_unit_tests_CPPFLAGS = -I$(top_srcdir)/cmsg/include -I$(top_srcdir)/cmsg/include/cmsg
cmsg_unit_tests_LDFLAGS = -static
cmsg_unit_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) $(DEBUG_AWPLUS_LIBS) libcmsg.la -lprotobuf-c

test/functional/cmsg_functional_tests_api_auto.c \
test/functional/cmsg_functional_tests_api_auto.h \
test/functional/cmsg_functional_tests_impl_auto.c \
test/functional/cmsg_functional_tests_impl_auto.h \
test/functional/cmsg_functional_tests_types_auto.h \
test/functional/cmsg_functional_tests.pb-c.c \
test/functional/cmsg_functional_tests.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) test/functional/cmsg_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/test/functional -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c --c_out=$(top_srcdir)/cmsg/test/functional $(top_srcdir)/cmsg/test/functional/cmsg_functional_tests.proto

test/functional/ant_result_types_auto.h \
test/functional/ant_result.pb-c.c \
test/functional/ant_result.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) $(top_srcdir)/cmsg-proxy/src/ant_result.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/src --c_out=$(top_srcdir)/cmsg/test/functional $(top_srcdir)/cmsg-proxy/src/ant_result.proto


bin_PROGRAMS += cmsg_functional_tests
cmsg_functional_tests_SOURCES = \
	test/functional/ant_result.pb-c.c \
	test/functional/ant_result.pb-c.h \
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
	test/functional/server_side_validation.c \
	test/functional/pthread_helpers_tests.c \
	test/functional/setup.c

cmsg_functional_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS) -include $(top_builddir)/config.h
cmsg_functional_tests_CPPFLAGS = -I$(top_srcdir)/cmsg/include -I$(top_srcdir)/cmsg/include/cmsg -I$(top_builddir) -I$(top_srcdir)/cmsg/src
cmsg_functional_tests_LDFLAGS = -static
cmsg_functional_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) $(DEBUG_AWPLUS_LIBS) libcmsg.la -lprotobuf-c
endif # BUILD_UNITTEST
