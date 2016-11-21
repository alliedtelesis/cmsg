AC_PREREQ(2.64)

AC_INIT([protobuf-cmsg],
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
AC_CONFIG_FILES([Makefile])

PKG_CHECK_MODULES(GLIB, glib-2.0 >=  2.2.0, HAVE_GLIB="yes", sweep_config_ok="no")
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
if test "x$HAVE_GLIB" != xyes ; then
  AC_CHECK_LIB(glib-2.0, g_list_append, HAVE_GLIB="maybe", sweep_config_ok="no")
fi

PKG_PROG_PKG_CONFIG
if test -n "$PKG_CONFIG"; then
    # Horrible hack for systems where the pkg-config install directory is simply wrong!
    if $PKG_CONFIG --variable=pc_path pkg-config 2>/dev/null | grep -q /libdata/; then
        PKG_INSTALLDIR(['${prefix}/libdata/pkgconfig'])
    else
        PKG_INSTALLDIR
    fi
fi

AC_ARG_VAR(PROTOC_CMSG_PATH, [  Path to the protoc-cmsg compiler])

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

BUILD_CMSG_TEST=0
AC_ARG_ENABLE(cmsg-test, [  --enable-cmsg-test      Enable build of cmsg-test],
if test "x$enableval" = xyes ; then
  BUILD_CMSG_TEST=1
fi)
AM_CONDITIONAL(BUILD_CMSG_TEST, test $BUILD_CMSG_TEST = 1)

# --- Check CMSG PERF is enabled
HAVE_CMSG_PROFILING=0
AC_ARG_ENABLE(cmsg_prof, [  --enable-cmsg-prof      Enable build of cmsg profiling],
if test "x$enableval" = xyes ; then
  HAVE_CMSG_PROFILING=1
fi)
AM_CONDITIONAL(HAVE_CMSG_PROFILING, test $HAVE_CMSG_PROFILING = 1)

# --- Check VCStack is enabled
HAVE_VCSTACK=0
AC_ARG_ENABLE(vcstack, [  --enable-vcstack        Enable VCStack],
if test "x$enableval" = xyes ; then
  HAVE_VCSTACK=1
fi)
AM_CONDITIONAL(HAVE_VCSTACK, test $HAVE_VCSTACK = 1)

# --- Check if counterd is enabled
HAVE_COUNTERD=0
AC_ARG_ENABLE(counterd, [  --enable-counterd       Enable counterd],
if test "x$enableval" = xyes ; then
  HAVE_COUNTERD=1
fi)
AM_CONDITIONAL(HAVE_COUNTERD, test $HAVE_COUNTERD = 1)

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
        protoc-cmsg-path:       ${PROTOC_CMSG_PATH}
        cmsg:                   ${BUILD_CMSG}
        cmsg-test:              ${BUILD_CMSG_TEST}
        cmsg-tools:             ${HAVE_CMSG_PROFILING}
        VCStack:                ${HAVE_VCSTACK}
        counterd:               ${HAVE_COUNTERD}
])
