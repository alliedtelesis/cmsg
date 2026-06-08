AC_PREREQ(2.64)

AC_INIT([cmsg],
        [1.0.0])
PACKAGE_DESCRIPTION="CMSG library implementation in C"
AC_SUBST(PACKAGE_DESCRIPTION)

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules subdir-objects])
AC_PROG_CC_STDC
AC_PROG_CXX
AC_PROG_LN_S
AC_PROG_MKDIR_P
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])
LT_INIT

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES([Makefile
  cmsg/Makefile
  cmsg-proxy/Makefile
  protoc-c/Makefile
  cmsg/cmsg.pc
  cmsg-proxy/cmsgproxyconfig.pc
  cmsg-proxy/cmsgproxyhttpstreamingapi.pc
  cmsg-proxy/file_upload_protobuf.pc])

PKG_CHECK_MODULES([GLIB],[glib-2.0])
PKG_CHECK_MODULES([OOP],[liboop],[AC_DEFINE([HAVE_LIBOOP],[1],[liboop is available])],[cmsg_prereq=no])
PKG_CHECK_MODULES([GMEM_DIAG],[gmem_diag],[AC_DEFINE([HAVE_GMEM_DIAG],[1],[Glib Memory Diagnostics are available])],[unittest_prereq=no])
PKG_CHECK_MODULES([NOVAPROVA],[novaprova],[],[unittest_prereq=no])
PKG_CHECK_MODULES([HEALTHCHECK],[healthcheck],[AC_DEFINE([HAVE_HEALTHCHECK],[1],[Healthcheck is available])],[cmsg_prereq=no])
PKG_CHECK_MODULES([SIMPLE_SHM],[simple_shm],[AC_DEFINE([HAVE_SIMPLE_SHM],[1],[Simple Shared Memory is available])],[cmsg_prereq=no])
PKG_CHECK_MODULES([OPENSSL],[openssl],[],[cmsg_prereq=no])
PKG_CHECK_MODULES([EVENTWATCH],[eventwatch],[AC_DEFINE([HAVE_EVENTWATCH],[1],[Eventwatch is available])],[cmsg_prereq=no])
AX_PTHREAD([],[unittest_prereq=no])

AC_ARG_VAR(PROTOC_PATH, [  Path to the protoc compiler])
AC_ARG_VAR(PROTOC_CMSG_PATH, [  Path to the protoc-cmsg compiler])
AC_ARG_VAR(PROTOC_C_PATH, [  Path to the protoc-c compiler])
AC_ARG_VAR(CMSG_PROTO_PATH, [  Path to the CMSG .proto files])
AC_ARG_VAR(GOOGLEAPIS_PROTO_PATH, [  Path to the googleapis .proto files])

BUILD_PROTOC_CMSG=1
AC_ARG_ENABLE(protoc-cmsg, [  --disable-protoc-cmsg   Suppress build of protoc-cmsg],
if test "x$enableval" = xno ; then
  BUILD_PROTOC_CMSG=0
fi)
AM_CONDITIONAL(BUILD_PROTOC_CMSG, test $BUILD_PROTOC_CMSG = 1)

BUILD_CMSG=1
AC_ARG_ENABLE(cmsg, [  --disable-cmsg          Suppress build of the cmsg library],
if test "x$enableval" = xno ; then
  BUILD_CMSG=0
fi)
AM_CONDITIONAL(BUILD_CMSG, test $BUILD_CMSG = 1)

BUILD_CMSG_PROXY=1
AC_ARG_ENABLE(cmsg-proxy, [  --disable-cmsg-proxy          Suppress build of the cmsg proxy library],
if test "x$enableval" = xno ; then
  BUILD_CMSG_PROXY=0
fi)
AM_CONDITIONAL(BUILD_CMSG_PROXY, test $BUILD_CMSG_PROXY = 1)

BUILD_UNITTEST=0
AC_ARG_ENABLE(unittest, [  --enable-unittest      Enable build of cmsg-proxy unit tests],
if test "x$enableval" = xyes ; then
  BUILD_UNITTEST=1
  AC_DEFINE(HAVE_UNITTEST, 1, [enable unittest code])
fi)
AM_CONDITIONAL(BUILD_UNITTEST, test $BUILD_UNITTEST = 1)

# --- Check if counterd is enabled
HAVE_COUNTERD=0
AC_ARG_ENABLE(counterd, [  --enable-counterd       Enable counterd],
if test "x$enableval" = xyes ; then
  HAVE_COUNTERD=1
  AC_DEFINE(HAVE_COUNTERD, 1, [enable counterd code])
fi)
AM_CONDITIONAL(HAVE_COUNTERD, test $HAVE_COUNTERD = 1)

# --- Check if IPC message tracing is enabled
HAVE_TRACE=0
AC_ARG_ENABLE(tracing, [  --enable-tracing        Enable IPC message tracing],
if test "x$enableval" = xyes ; then
  HAVE_TRACE=1
  AC_DEFINE(HAVE_TRACE, 1, [enable IPC message tracing])
fi)
AM_CONDITIONAL(HAVE_TRACE, test $HAVE_TRACE = 1)

AC_ARG_ENABLE([pidfd], [AS_HELP_STRING([--with-pidfd], [Use pidfd or pidpoll in cmsg])])
if test "${with_pidfd}" = "yes"; then
  AC_DEFINE([HAVE_PIDFD],1,[pidfd])
else
  AC_DEFINE([HAVE_PIDPOLL],1,[pidpoll])
fi
AM_CONDITIONAL([HAVE_PIDFD],[test "$with_pidfd" = yes])

gl_LD_VERSION_SCRIPT

gl_VALGRIND_TESTS

MY_CODE_COVERAGE

AC_C_BIGENDIAN

AC_OUTPUT
AC_MSG_RESULT([
    $PACKAGE $VERSION

        CC:                     ${CC}
        CFLAGS:                 ${CFLAGS}
        CXX:                    ${CXX}
        CXXFLAGS:               ${CXXFLAGS}
        LDFLAGS:                ${LDFLAGS}
        LIBS:                   ${LIBS}

        prefix:                 ${prefix}
        sysconfdir:             ${sysconfdir}
        libdir:                 ${libdir}
        includedir:             ${includedir}
        pkgconfigdir:           ${pkgconfigdir}

        bigendian:              ${ac_cv_c_bigendian}
        protobuf version:       ${PROTOBUF_VERSION}

    cmsg options
        protoc-cmsg:            ${BUILD_PROTOC_CMSG}
        protoc-path:            ${PROTOC_PATH}
        protoc-c-path:          ${PROTOC_C_PATH}
        protoc-cmsg-path:       ${PROTOC_CMSG_PATH}
        cmsg:                   ${BUILD_CMSG}
        counterd:               ${HAVE_COUNTERD}
        tracing:                ${HAVE_TRACE}
])
