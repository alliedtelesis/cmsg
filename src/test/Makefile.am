AM_CFLAGS = -Wall -Werror

check_PROGRAMS = test-generated-code test-generated-code2 test-rpc
noinst_PROGRAMS = cxx-generate-packed-data cmsg-server cmsg-client
INCLUDES = -I$(srcdir)/..
test_generated_code_SOURCES = \
test-generated-code.c \
generated-code/test.pb-c.c
test_generated_code_LDADD = \
../libprotobuf-c.la
test_generated_code2_SOURCES = \
test-generated-code2.c \
generated-code/test-full.pb-c.c
test_generated_code2_LDADD = \
../libprotobuf-c.la
test_rpc_SOURCES = \
test-rpc.c \
generated-code/test.pb-c.c
test_rpc_LDADD = \
../libprotobuf-c.la
cxx_generate_packed_data_SOURCES = \
cxx-generate-packed-data.cc generated-code/test-full.pb.cc
cxx-generate-packed-data.$(OBJEXT): generated-code/test-full.pb.h
cxx_generate_packed_data_LDADD = -lprotobuf
cxx_generate_packed_data_LDFLAGS = -pthread
cmsg_server_SOURCES = \
cmsg-server.c generated-code/test-cmsg.pb-c.c generated-code/test-cmsg_impl_auto.c
cmsg_server_LDADD = ../libprotobuf-c.la
cmsg_client_SOURCES = \
cmsg-client.c generated-code/test-cmsg.pb-c.c generated-code/test-cmsg_api_auto.c generated-code/test-cmsg_impl_auto.c
cmsg_client_LDADD = ../libprotobuf-c.la

if HAVE_VCSTACK
cmsg_server_LDADD  += -lcpg -lcoroipcc
cmsg_server_LDFLAGS = -Wl,-rpath -Wl,$(srcdir)/../../../corosync/build/lib
cmsg_client_LDADD  += -lcpg -lcoroipcc
cmsg_client_LDFLAGS = -Wl,-rpath -Wl,$(srcdir)/../../../corosync/build/lib
endif

generated-code/test.pb-c.c generated-code/test.pb-c.h: ../protoc-c$(EXEEXT) $(srcdir)/test.proto
	mkdir -p generated-code
	../protoc-c -I$(srcdir) --c_out=generated-code $(srcdir)/test.proto
generated-code/test-cmsg.pb-c.c generated-code/test-cmsg.pb-c.h: ../protoc-c$(EXEEXT) $(srcdir)/test-cmsg.proto
	mkdir -p generated-code
	../protoc-c -I$(srcdir) --c_out=generated-code $(srcdir)/test-cmsg.proto
generated-code/test-full.pb-c.c generated-code/test-full.pb-c.h: ../protoc-c $(srcdir)/test-full.proto
	mkdir -p generated-code
	../protoc-c -I$(srcdir) --c_out=generated-code $(srcdir)/test-full.proto
generated-code/test-full.pb.cc generated-code/test-full.pb.h: $(srcdir)/test-full.proto @PROTOC@
	mkdir -p generated-code
	@PROTOC@ -I$(srcdir) --cpp_out=generated-code $(srcdir)/test-full.proto
generated-code/test-full-cxx-output.inc: cxx-generate-packed-data$(EXEEXT)
	./cxx-generate-packed-data > generated-code/test-full-cxx-output.inc


BUILT_SOURCES = generated-code/test.pb-c.c generated-code/test.pb-c.h \
                generated-code/test-cmsg.pb-c.c generated-code/test-cmsg.pb-c.h \
                generated-code/test-full.pb-c.c generated-code/test-full.pb-c.h \
                generated-code/test-full.pb.cc generated-code/test-full.pb.h \
                generated-code/test-full-cxx-output.inc
DISTCLEANFILES = $(BUILT_SOURCES)
TESTS = test-generated-code test-generated-code2 test-rpc
EXTRA_DIST = test.proto test-cmg.proto test-full.proto common-test-arrays.h
