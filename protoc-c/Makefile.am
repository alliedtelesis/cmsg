bin_PROGRAMS = protoc-gen-cmsg

google/api/http.pb.cc \
google/api/http.pb.h: $(PROTOC_PATH)$(EXEEXT) $(GOOGLEAPIS_PROTO_PATH)/google/api/http.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(GOOGLEAPIS_PROTO_PATH) --cpp_out=$(top_srcdir)/protoc-c $(GOOGLEAPIS_PROTO_PATH)/google/api/http.proto

google/api/annotations.pb.cc \
google/api/annotations.pb.h: $(PROTOC_PATH)$(EXEEXT) $(GOOGLEAPIS_PROTO_PATH)/google/api/annotations.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(GOOGLEAPIS_PROTO_PATH) --cpp_out=$(top_srcdir)/protoc-c $(GOOGLEAPIS_PROTO_PATH)/google/api/annotations.proto

validation.pb.cc \
validation.pb.h: $(PROTOC_PATH)$(EXEEXT) ../cmsg/src/validation.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src --cpp_out=$(top_srcdir)/protoc-c $(top_srcdir)/cmsg/src/validation.proto

supported_service.pb.cc \
supported_service.pb.h: $(PROTOC_PATH)$(EXEEXT) ../cmsg/src/supported_service.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) -I$(top_srcdir)/cmsg/src --cpp_out=$(top_srcdir)/protoc-c $(top_srcdir)/cmsg/src/supported_service.proto

protoc_gen_cmsg_SOURCES = \
	google/api/http.pb.cc \
	google/api/http.pb.h \
	google/api/annotations.pb.cc \
	google/api/annotations.pb.h \
	validation.pb.cc \
	validation.pb.h \
	supported_service.pb.cc \
	supported_service.pb.h \
	c_atl_generator.cc \
	c_atl_generator.h \
	c_enum.cc \
	c_enum.h \
	c_file.cc \
	c_file.h \
	c_generator.cc \
	c_generator.h \
	c_helpers.cc \
	c_helpers.h \
	c_helpers_cmsg.cc \
	c_helpers_cmsg.h \
	c_message.cc \
	c_message.h \
	c_service.cc \
	c_service.h \
	main.cc

protoc_gen_cmsgdir       = $(includedir)/cmsg
protoc_gen_cmsg_HEADERS  =

protoc_gen_cmsg_CXXFLAGS = \
	-Werror \
	$(protobuf_CFLAGS) \
	-std=c++11

protoc_gen_cmsg_LDADD = \
	$(protobuf_LIBS) \
	-lprotoc \
	-lprotobuf

protoc_gen_cmsg_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-I$(top_srcdir)

BUILT_SOURCES = google/api/http.pb.cc google/api/http.pb.h google/api/annotations.pb.cc google/api/annotations.pb.h \
                validation.pb.cc validation.pb.h supported_service.pb.cc supported_service.pb.h
CLEANFILES = $(BUILT_SOURCES)