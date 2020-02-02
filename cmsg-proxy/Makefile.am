lib_LTLIBRARIES = libfile_upload_protobuf.la libcmsgproxyconfig.la libcmsgproxy.la libcmsgproxyhttpstreamingapi.la
bin_PROGRAMS =

pkgconfiglibdir = $(libdir)/pkgconfig
pkgconfiglib_DATA = cmsgproxyconfig.pc cmsgproxyhttpstreamingapi.pc file_upload_protobuf.pc

src/http_streaming_api_auto.c \
src/http_streaming_api_auto.h \
src/http_streaming_impl_auto.c \
src/http_streaming_impl_auto.h \
src/http_streaming.pb-c.c \
src/http_streaming.pb-c.h: $(PROTOC_PATH)$(EXEEXT) src/http_streaming.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/src \
	--plugin=protoc-gen-c=$(PROTOC_C_PATH) --c_out=$(top_srcdir)/cmsg-proxy/src $(top_srcdir)/cmsg-proxy/src/http_streaming.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/src \
	--plugin=protoc-gen-cmsg=$(PROTOC_CMSG_PATH) --cmsg_out=$(top_srcdir)/cmsg-proxy/src $(top_srcdir)/cmsg-proxy/src/http_streaming.proto

src/file_upload.pb-c.c \
src/file_upload.pb-c.h \
src/file_upload_types_auto.h \
src/file_upload_validation_auto.h: $(PROTOC_PATH)$(EXEEXT) src/file_upload.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/src \
	--plugin=protoc-gen-c=$(PROTOC_C_PATH) --c_out=$(top_srcdir)/cmsg-proxy/src $(top_srcdir)/cmsg-proxy/src/file_upload.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/src \
	--plugin=protoc-gen-cmsg=$(PROTOC_CMSG_PATH) --cmsg_out=$(top_srcdir)/cmsg-proxy/src $(top_srcdir)/cmsg-proxy/src/file_upload.proto

## Library for file_upload.proto messages
libfile_upload_protobuf_la_LDFLAGS = -version-info 1:0:0 # version:revision:age
libfile_upload_protobuf_la_SOURCES = src/file_upload.pb-c.c
libfile_upload_protobuf_la_LIBADD =
libfile_upload_protobuf_la_CFLAGS  =

## Library for cmsg proxy configuration
libcmsgproxyconfig_la_LDFLAGS = -version-info 1:0:0 # version:revision:age
libcmsgproxyconfig_la_SOURCES = src/cmsg_proxy_config.c cmsg_proxy_config.h
libcmsgproxyconfig_la_LIBADD = -lsimple_shm
libcmsgproxyconfig_la_CFLAGS  = -include $(top_builddir)/config.h

## Library for using the cmsg proxy http streaming functionality
libcmsgproxyhttpstreamingapi_la_CFLAGS  = $(GLIB_CFLAGS) -include $(top_builddir)/config.h
libcmsgproxyhttpstreamingapi_la_CPPFLAGS = -I$(top_srcdir)/cmsg/include
libcmsgproxyhttpstreamingapi_la_LDFLAGS = -version-info 1:0:0 # version:revision:age
libcmsgproxyhttpstreamingapi_la_SOURCES = src/cmsg_proxy_http_streaming_api.c cmsg_proxy_http_streaming_api.h \
                                          src/http_streaming_api_auto.c src/http_streaming_api_auto.h \
                                          src/http_streaming.pb-c.c src/http_streaming.pb-c.h \
libcmsgproxyhttpstreamingapi_la_LIBADD = -lcmsg

libcmsgproxy_la_SOURCES = \
	src/http_streaming_impl_auto.c \
	src/http_streaming_impl_auto.h \
	src/http_streaming.pb-c.c \
	src/http_streaming.pb-c.h \
	src/cmsg_proxy.c \
	cmsg_proxy.h \
	src/cmsg_proxy_mem.c \
	src/cmsg_proxy_mem.h \
	src/cmsg_proxy_counters.c \
	src/cmsg_proxy_counters.h \
	src/cmsg_proxy_tree.c \
	src/cmsg_proxy_tree.h \
	src/cmsg_proxy_passthrough.c \
	src/cmsg_proxy_http_streaming.c \
	src/cmsg_proxy_index.c \
	src/cmsg_proxy_input.c \
	src/cmsg_proxy_output.c \
	src/cmsg_proxy_output.h

libcmsgproxy_la_LIBADD = \
	$(GLIB_LIBS) \
	$(GMEM_DIAG_LIBS) \
	-lcmsg \
	-ljansson \
	-lprotobuf2json-c \
	libfile_upload_protobuf.la

if HAVE_COUNTERD
libcmsgproxy_la_LIBADD += -lcntr
endif

libcmsgproxy_la_CFLAGS = -Werror -Wall $(GLIB_CFLAGS) -include $(top_builddir)/config.h

libcmsgproxy_la_CPPFLAGS = -I$(top_srcdir)/cmsg-proxy -I$(top_srcdir)/cmsg/include

libcmsgproxy_ladir       = $(includedir)/cmsg-proxy
libcmsgproxy_la_HEADERS  = \
	cmsg_proxy.h \
	cmsg_proxy_config.h \
	cmsg_proxy_http_streaming_api.h \
	src/file_upload.proto \
	src/file_upload.pb-c.h \
	src/file_upload_types_auto.h \
	src/file_upload_validation_auto.h

BUILT_SOURCES = \
	src/http_streaming_api_auto.c \
	src/http_streaming_api_auto.h \
	src/http_streaming_impl_auto.c \
	src/http_streaming_impl_auto.h \
	src/http_streaming.pb-c.c \
	src/http_streaming.pb-c.h \
	src/file_upload.pb-c.c \
	src/file_upload.pb-c.h

if BUILD_UNITTEST
BUILT_SOURCES += \
	test/unit/cmsg_proxy_unit_tests_proxy_def.c \
	test/unit/cmsg_proxy_unit_tests_proxy_def.h \
	test/unit/cmsg_proxy_unit_tests_api_auto.c \
	test/unit/cmsg_proxy_unit_tests_api_auto.h \
	test/unit/cmsg_proxy_unit_tests.pb-c.c \
	test/unit/cmsg_proxy_unit_tests.pb-c.h \
	test/unit/cmsg_proxy_unit_tests_types_auto.h \
	test/functional/cmsg_proxy_functional_tests_proxy_def.c \
	test/functional/cmsg_proxy_functional_tests_proxy_def.h \
	test/functional/cmsg_proxy_functional_tests_api_auto.c \
	test/functional/cmsg_proxy_functional_tests_api_auto.h \
	test/functional/cmsg_proxy_functional_tests_impl_auto.c \
	test/functional/cmsg_proxy_functional_tests_impl_auto.h \
	test/functional/cmsg_proxy_functional_tests.pb-c.c \
	test/functional/cmsg_proxy_functional_tests.pb-c.h \
	test/functional/cmsg_proxy_functional_tests_types_auto.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.h \
	test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.c \
	test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_types_auto.h
endif

CLEANFILES = $(BUILT_SOURCES)

if BUILD_UNITTEST
test/unit/cmsg_proxy_unit_tests_proxy_def.c \
test/unit/cmsg_proxy_unit_tests_proxy_def.h \
test/unit/cmsg_proxy_unit_tests_api_auto.c \
test/unit/cmsg_proxy_unit_tests_api_auto.h \
test/unit/cmsg_proxy_unit_tests_types_auto.h \
test/unit/cmsg_proxy_unit_tests.pb-c.c \
test/unit/cmsg_proxy_unit_tests.pb-c.h \
test/unit/http.pb-c.c \
test/unit/http.pb-c.h: $(PROTOC_PATH)$(EXEEXT) test/unit/cmsg_proxy_unit_tests.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/test/unit -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c -I$(CMSG_PROTO_PATH) \
	--plugin=protoc-gen-cmsg=$(PROTOC_CMSG_PATH) --cmsg_out=$(top_srcdir)/cmsg-proxy/test/unit $(top_srcdir)/cmsg-proxy/test/unit/cmsg_proxy_unit_tests.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/test/unit -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c -I$(CMSG_PROTO_PATH) \
	--plugin=protoc-gen-c=$(PROTOC_C_PATH) --c_out=$(top_srcdir)/cmsg-proxy/test/unit $(top_srcdir)/cmsg-proxy/test/unit/cmsg_proxy_unit_tests.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/test/unit -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c -I$(CMSG_PROTO_PATH) \
	--plugin=protoc-gen-c=$(PROTOC_C_PATH) --c_out=$(top_srcdir)/cmsg-proxy/test/unit $(top_srcdir)/protoc-c/http.proto

bin_PROGRAMS += cmsg_proxy_unit_tests
cmsg_proxy_unit_tests_SOURCES = \
	src/http_streaming_impl_auto.c \
	src/http_streaming_impl_auto.h \
	src/http_streaming.pb-c.c \
	src/http_streaming.pb-c.h \
	test/unit/http.pb-c.c \
	test/unit/http.pb-c.h \
	test/unit/cmsg_proxy_unit_tests_proxy_def.c \
	test/unit/cmsg_proxy_unit_tests_proxy_def.h \
	test/unit/cmsg_proxy_unit_tests_api_auto.c \
	test/unit/cmsg_proxy_unit_tests_api_auto.h \
	test/unit/cmsg_proxy_unit_tests.pb-c.c \
	test/unit/cmsg_proxy_unit_tests.pb-c.h \
	test/unit/cmsg_proxy_mem_unit_tests.c \
	test/unit/cmsg_proxy_unit_tests.c
cmsg_proxy_unit_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS) $(PTHREAD_CFLAGS) -include $(top_builddir)/config.h
cmsg_proxy_unit_tests_CPPFLAGS  = -I$(top_srcdir) -I$(top_srcdir)/cmsg-proxy -I$(top_srcdir)/cmsg-proxy/src
cmsg_proxy_unit_tests_LDFLAGS = -static
cmsg_proxy_unit_tests_LDADD   = $(PTHREAD_LIBS) $(NOVAPROVA_LIBS) $(GLIB_LIBS) $(GMEM_DIAG_LIBS) \
                                -lcmsg -lprotobuf-c -lprotobuf2json-c \
                                -ljansson libcmsgproxyconfig.la

if HAVE_COUNTERD
cmsg_proxy_unit_tests_LDADD += -lcntr
endif

test/functional/cmsg_proxy_functional_tests_proxy_def.c \
test/functional/cmsg_proxy_functional_tests_proxy_def.h \
test/functional/cmsg_proxy_functional_tests_api_auto.c \
test/functional/cmsg_proxy_functional_tests_api_auto.h \
test/functional/cmsg_proxy_functional_tests_impl_auto.c \
test/functional/cmsg_proxy_functional_tests_impl_auto.h \
test/functional/cmsg_proxy_functional_tests_types_auto.h \
test/functional/cmsg_proxy_functional_tests.pb-c.c \
test/functional/cmsg_proxy_functional_tests.pb-c.h \
test/functional/http.pb-c.c \
test/functional/http.pb-c.h: $(PROTOC_PATH)$(EXEEXT) test/functional/cmsg_proxy_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/test/functional -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c -I$(CMSG_PROTO_PATH) \
	--plugin=protoc-gen-cmsg=$(PROTOC_CMSG_PATH) --cmsg_out=$(top_srcdir)/cmsg-proxy/test/functional $(top_srcdir)/cmsg-proxy/test/functional/cmsg_proxy_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/test/functional -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c -I$(CMSG_PROTO_PATH) \
	--plugin=protoc-gen-c=$(PROTOC_C_PATH) --c_out=$(top_srcdir)/cmsg-proxy/test/functional $(top_srcdir)/cmsg-proxy/test/functional/cmsg_proxy_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/test/functional -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c -I$(CMSG_PROTO_PATH) \
	--plugin=protoc-gen-c=$(PROTOC_C_PATH) --c_out=$(top_srcdir)/cmsg-proxy/test/functional $(top_srcdir)/protoc-c/http.proto

bin_PROGRAMS += cmsg_proxy_functional_tests
cmsg_proxy_functional_tests_SOURCES = \
	test/functional/http.pb-c.c \
	test/functional/http.pb-c.h \
	test/functional/cmsg_proxy_functional_tests_proxy_def.c \
	test/functional/cmsg_proxy_functional_tests_proxy_def.h \
	test/functional/cmsg_proxy_functional_tests_api_auto.c \
	test/functional/cmsg_proxy_functional_tests_api_auto.h \
	test/functional/cmsg_proxy_functional_tests_impl_auto.c \
	test/functional/cmsg_proxy_functional_tests_impl_auto.h \
	test/functional/cmsg_proxy_functional_tests.pb-c.c \
	test/functional/cmsg_proxy_functional_tests.pb-c.h \
	src/http_streaming_api_auto.c \
	src/http_streaming_api_auto.h \
	src/http_streaming.pb-c.c \
	src/http_streaming.pb-c.h \
	test/functional/cmsg_proxy_functional_tests.c
cmsg_proxy_functional_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS) -include $(top_builddir)/config.h
cmsg_proxy_functional_tests_CPPFLAGS  = -I$(top_srcdir) -I$(top_srcdir)/cmsg-proxy -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/cmsg/include/cmsg -I$(top_srcdir)/cmsg/src
cmsg_proxy_functional_tests_LDFLAGS = -static
cmsg_proxy_functional_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) $(GMEM_DIAG_LIBS) \
                                -lcmsg -lprotobuf-c -lprotobuf2json-c \
                                -ljansson libcmsgproxy.la
if HAVE_COUNTERD
cmsg_proxy_functional_tests_LDADD += -lcntr
endif

test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.c \
test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.h \
test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.c \
test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.h \
test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.c \
test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.h \
test/functional/cmsg_proxy_passthrough_functional_tests_types_auto.h \
test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.c \
test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.h: $(PROTOC_PATH)$(EXEEXT) test/functional/cmsg_proxy_passthrough_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/test/functional -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c -I$(CMSG_PROTO_PATH) \
	--plugin=protoc-gen-cmsg=$(PROTOC_CMSG_PATH) --cmsg_out=$(top_srcdir)/cmsg-proxy/test/functional $(top_srcdir)/cmsg-proxy/test/functional/cmsg_proxy_passthrough_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg-proxy/test/functional -I$(top_srcdir)/cmsg-proxy/src -I$(top_srcdir)/protoc-c -I$(CMSG_PROTO_PATH) \
	--plugin=protoc-gen-c=$(PROTOC_C_PATH) --c_out=$(top_srcdir)/cmsg-proxy/test/functional $(top_srcdir)/cmsg-proxy/test/functional/cmsg_proxy_passthrough_functional_tests.proto

bin_PROGRAMS += cmsg_proxy_passthrough_functional_tests
cmsg_proxy_passthrough_functional_tests_SOURCES = \
	test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.h \
	test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.c \
	test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.h \
	test/functional/cmsg_proxy_passthrough_functional_tests.c
cmsg_proxy_passthrough_functional_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS) -include $(top_builddir)/config.h
cmsg_proxy_passthrough_functional_tests_CPPFLAGS  =-I$(top_srcdir) -I$(top_srcdir)/cmsg-proxy -I$(top_srcdir)/cmsg-proxy/src
cmsg_proxy_passthrough_functional_tests_LDFLAGS = -static
cmsg_proxy_passthrough_functional_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) \
                                                  -lcmsg -lprotobuf-c libcmsgproxy.la
if HAVE_COUNTERD
cmsg_proxy_passthrough_functional_tests_LDADD += -lcntr
endif
endif # BUILD_UNITTEST

