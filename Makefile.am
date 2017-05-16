SUBDIRS =
bin_PROGRAMS =
lib_LTLIBRARIES =
BUILT_SOURCES =
CLEANFILES =

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-I${top_srcdir}/protobuf-c \
	-I${top_builddir} \
	-I${top_srcdir}
AM_CFLAGS = -Wall ${my_CFLAGS} $(GLIB_CFLAGS)
AM_CXXFLAGS = -Werror
AM_LDFLAGS =


if BUILD_PROTOC_CMSG
SUBDIRS += protoc-c
endif # BUILD_PROTOC_CMSG

if BUILD_CMSG
SUBDIRS += cmsg
endif # BUILD_CMSG

if BUILD_CMSG_PROXY
SUBDIRS += cmsg-proxy
endif # BUILD_CMSG_PROXY

if BUILD_CMSG_TEST
SUBDIRS += cmsg-test
endif # BUILD_CMSG_TEST

#
#
#

CLEANFILES += $(BUILT_SOURCES)

dist-hook:
	rm -vf `find $(top_distdir) -name '*.pb-c.[ch]' -o -name '*.pb.cc' -o -name '*.pb.h'`

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(includedir)/google/protobuf-c
	cd $(DESTDIR)$(includedir)/google/protobuf-c && rm -vf protobuf-c.h
	cd $(DESTDIR)$(includedir)/google/protobuf-c && $(LN_S) ../../protobuf-c/protobuf-c.h protobuf-c.h
