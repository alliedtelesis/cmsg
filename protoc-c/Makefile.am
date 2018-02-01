bin_PROGRAMS = protoc-cmsg

http.pb.cc \
http.pb.h: $(PROTOC_PATH)$(EXEEXT) http.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) --cpp_out=. http.proto

validation.pb.cc \
validation.pb.h: $(PROTOC_PATH)$(EXEEXT) validation.proto
	$(AM_V_GEN)$(PROTOC_PATH)$(EXEEXT) --cpp_out=. validation.proto

protoc_cmsg_SOURCES = \
	http.pb.cc \
	http.pb.h \
	validation.pb.cc \
	validation.pb.h \
	c_atl_generator.cc \
	c_atl_generator.h \
	c_bytes_field.cc \
	c_bytes_field.h \
	c_enum.cc \
	c_enum.h \
	c_enum_field.cc \
	c_enum_field.h \
	c_extension.cc \
	c_extension.h \
	c_field.cc \
	c_field.h \
	c_file.cc \
	c_file.h \
	c_generator.cc \
	c_generator.h \
	c_helpers.cc \
	c_helpers.h \
	c_message.cc \
	c_message.h \
	c_message_field.cc \
	c_message_field.h \
	c_primitive_field.cc \
	c_primitive_field.h \
	c_service.cc \
	c_service.h \
	c_string_field.cc \
	c_string_field.h \
	main.cc

protoc_cmsg_CXXFLAGS = \
	-Werror \
	$(protobuf_CFLAGS)

protoc_cmsg_LDADD = \
	$(protobuf_LIBS) \
	-lprotoc \
	-lprotobuf

protoc_cmsg_CPPFLAGS = \
	-include $(top_builddir)/config.h

BUILT_SOURCES = http.pb.cc http.pb.h validation.pb.cc validation.pb.h
CLEANFILES = $(BUILT_SOURCES)