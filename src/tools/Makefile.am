AM_CFLAGS = -I=/usr/include/glib-2.0 -I=/usr/lib/glib-2.0/include -Wall -Werror

if HAVE_CMSG_PROFILING
bin_PROGRAMS = cmsg-prof
INCLUDES = -I$(srcdir)/..
cmsg_prof_AM_CFLAGS = libtool --mode=link

cmsg_prof_SOURCES = cmsg-prof.c cmsg-prof.pb-c.c cmsg-prof_api_auto.c cmsg-prof_impl_auto.c

cmsg_prof_LDADD = ../libprotobuf-c.la -lpthread -lglib-2.0 -lrt

if HAVE_VCSTACK
cmsg_prof_LDADD  += -lcpg -lcoroipcc
endif

cmsg-prof.pb-c.c cmsg-prof.pb-c.h: ../protoc-c cmsg-prof.proto
	../protoc-c --c_out=. cmsg-prof.proto

cmsg-prof_api_auto.c cmsg-prof_api_auto.h: ../protoc-c cmsg-prof.proto
	../protoc-c --c_out=. cmsg-prof.proto

cmsg-prof_impl_auto.c cmsg-prof_impl_auto.h: ../protoc-c cmsg-prof.proto
	../protoc-c --c_out=. cmsg-prof.proto

cmsg-prof_types_auto.h: ../protoc-c cmsg-prof.proto
	../protoc-c --c_out=. cmsg-prof.proto

clean-local:
	rm -f *.pb-c.c *.pb-c.h *auto.c *auto.h

BUILT_SOURCES = cmsg-prof.pb-c.c cmsg-prof_api_auto.c cmsg-prof_impl_auto.c
endif
