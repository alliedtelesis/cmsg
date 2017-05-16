bin_PROGRAMS = cmsg-test

cmsg_test_SOURCES = \
	cmsg-test.c \
	cmsg-test_api_auto.c \
	cmsg-test_impl_auto.c \
	cmsg-test.pb-c.c

cmsg_test_CFLAGS = -Werror $(GLIB_CFLAGS)

cmsg_test_LDADD = \
	-lpthread \
	-lcmsg \
	-lprotobuf-c

cmsg-test.pb-c.c cmsg-test.pb-c.h: $(PROTOC_CMSG_PATH)$(EXEEXT) cmsg-test.proto
	$(AM_V_GEN)$(PROTOC_CMSG_PATH)$(EXEEXT) -I. --c_out=. cmsg-test.proto

BUILT_SOURCES = \
	cmsg-test.pb-c.c cmsg-test.pb-c.h \
	cmsg-test_api_auto.c cmsg-test_api_auto.h \
	cmsg-test_impl_auto.c cmsg-test_impl_auto.h \
	cmsg-test.pb-c.c cmsg-test.pb-c.h \
	cmsg-test_impl_auto-tmp_stubs.c \
	cmsg-test_types_auto.h

CLEANFILES = $(BUILT_SOURCES)
