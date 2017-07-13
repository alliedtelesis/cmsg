lib_LTLIBRARIES = libcmsgproxy.la
bin_PROGRAMS =

src/common.pb-c.c \
src/common.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) src/common.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I. --c_out=. src/common.proto

libcmsgproxy_la_SOURCES = \
	src/common.pb-c.c \
	src/common.pb-c.h \
	src/cmsg_proxy.c \
	cmsg_proxy.h \
	src/cmsg_proxy_mem.c \
	src/cmsg_proxy_mem.h \
	src/cmsg_proxy_counters.c \
	src/cmsg_proxy_counters.h \
	src/cmsg_proxy_tree.c \
	src/cmsg_proxy_tree.h \
	src/cmsg_proxy_passthrough.c

libcmsgproxy_la_LIBADD = \
	$(GLIB_LIBS) \
	-lcmsg \
	-ljansson \
	-lprotobuf2json-c

libcmsgproxy_la_CFLAGS = -Werror -Wall $(GLIB_CFLAGS)

libcmsgproxy_la_CPPFLAGS = -Iinclude

BUILT_SOURCES = src/common.pb-c.c src/common.pb-c.h
CLEANFILES = $(BUILT_SOURCES)

if BUILD_UNITTEST
test/unit/cmsg_proxy_unit_tests_proxy_def.c \
test/unit/cmsg_proxy_unit_tests_proxy_def.h \
test/unit/cmsg_proxy_unit_tests_api_auto.c \
test/unit/cmsg_proxy_unit_tests_api_auto.h \
test/unit/cmsg_proxy_unit_tests.pb-c.c \
test/unit/cmsg_proxy_unit_tests.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) test/unit/cmsg_proxy_unit_tests.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) --proto_path=. --proto_path=src --c_out=. test/unit/cmsg_proxy_unit_tests.proto

bin_PROGRAMS += cmsg_proxy_unit_tests
cmsg_proxy_unit_tests_SOURCES = \
	src/common.pb-c.c \
	src/common.pb-c.h \
	test/unit/cmsg_proxy_unit_tests_proxy_def.c \
	test/unit/cmsg_proxy_unit_tests_proxy_def.h \
	test/unit/cmsg_proxy_unit_tests_api_auto.c \
	test/unit/cmsg_proxy_unit_tests_api_auto.h \
	test/unit/cmsg_proxy_unit_tests.pb-c.c \
	test/unit/cmsg_proxy_unit_tests.pb-c.h \
	test/unit/cmsg_proxy_mem_unit_tests.c \
	test/unit/cmsg_proxy_unit_tests.c
cmsg_proxy_unit_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS)
cmsg_proxy_unit_tests_CPPFLAGS  = -Isrc -Iinclude
cmsg_proxy_unit_tests_LDFLAGS = -static
cmsg_proxy_unit_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) \
                                -lcmsg -lprotobuf-c -lprotobuf2json-c \
                                -ljansson

test/functional/cmsg_proxy_functional_tests_proxy_def.c \
test/functional/cmsg_proxy_functional_tests_proxy_def.h \
test/functional/cmsg_proxy_functional_tests_api_auto.c \
test/functional/cmsg_proxy_functional_tests_api_auto.h \
test/functional/cmsg_proxy_functional_tests_impl_auto.c \
test/functional/cmsg_proxy_functional_tests_impl_auto.h \
test/functional/cmsg_proxy_functional_tests.pb-c.c \
test/functional/cmsg_proxy_functional_tests.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) test/functional/cmsg_proxy_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) --proto_path=. --proto_path=src --c_out=. test/functional/cmsg_proxy_functional_tests.proto

bin_PROGRAMS += cmsg_proxy_functional_tests
cmsg_proxy_functional_tests_SOURCES = \
	src/common.pb-c.c \
	src/common.pb-c.h \
	test/functional/cmsg_proxy_functional_tests_proxy_def.c \
	test/functional/cmsg_proxy_functional_tests_proxy_def.h \
	test/functional/cmsg_proxy_functional_tests_api_auto.c \
	test/functional/cmsg_proxy_functional_tests_api_auto.h \
	test/functional/cmsg_proxy_functional_tests_impl_auto.c \
	test/functional/cmsg_proxy_functional_tests_impl_auto.h \
	test/functional/cmsg_proxy_functional_tests.pb-c.c \
	test/functional/cmsg_proxy_functional_tests.pb-c.h \
	test/functional/cmsg_proxy_functional_tests.c
cmsg_proxy_functional_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS)
cmsg_proxy_functional_tests_CPPFLAGS  = -Isrc -Iinclude
cmsg_proxy_functional_tests_LDFLAGS = -static
cmsg_proxy_functional_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) \
                                -lcmsg -lprotobuf-c -lprotobuf2json-c \
                                -ljansson

test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.c \
test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.h \
test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.c \
test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.h \
test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.c \
test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.h \
test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.c \
test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) test/functional/cmsg_proxy_passthrough_functional_tests.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) --proto_path=. --proto_path=src --c_out=. test/functional/cmsg_proxy_passthrough_functional_tests.proto

bin_PROGRAMS += cmsg_proxy_passthrough_functional_tests
cmsg_proxy_passthrough_functional_tests_SOURCES = \
	src/common.pb-c.c \
	src/common.pb-c.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_proxy_def.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_api_auto.h \
	test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.c \
	test/functional/cmsg_proxy_passthrough_functional_tests_impl_auto.h \
	test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.c \
	test/functional/cmsg_proxy_passthrough_functional_tests.pb-c.h \
	test/functional/cmsg_proxy_passthrough_functional_tests.c
cmsg_proxy_passthrough_functional_tests_CFLAGS  = -Werror -Wall $(GLIB_CFLAGS) -g $(NOVAPROVA_CFLAGS)
cmsg_proxy_passthrough_functional_tests_CPPFLAGS  = -Isrc -Iinclude
cmsg_proxy_passthrough_functional_tests_LDFLAGS = -static
cmsg_proxy_passthrough_functional_tests_LDADD   = $(NOVAPROVA_LIBS) $(GLIB_LIBS) \
                                                  -lcmsg -lprotobuf-c
endif # BUILD_UNITTEST

