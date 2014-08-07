#!/bin/bash

CPPFLAGS=' -Wno-unused-but-set-variable -Wno-error=address -DLOCAL_INSTALL'

ENABLE_CMSG_PROF=0
ENABLE_VCSTACK=0
ENABLE_COUNTERD=0
SHOW_HELP=0
CONFIGURE_OPTIONS=""
CFLAGS_OPTIONS=""

# read the options
TEMP=`getopt -o pvh --long enable-cmsg-prof,enable-vcstack,enable-counterd,help -n 'test.sh' -- "$@"`
eval set -- "$TEMP"

while true ; do
    case "$1" in
        -p|--enable-cmsg-prof) ENABLE_CMSG_PROF=1 ; shift ;;
        -v|--enable-vcstack) ENABLE_VCSTACK=1 ; shift ;;
        -c|--enable-counterd) ENABLE_COUNTERD=1 ; shift ;;
        -h|--help) SHOW_HELP=1 ; shift ;;
        --) shift ; break ;;
        *) echo "Internal error!" ; exit 1 ;;
    esac
done

if [ $SHOW_HELP = 1 ]; then
    echo "options:"
    echo "-p or --enable-cmsg-prof"
    echo "-v or --enable-vcstack"
    echo "-c or --enable-counterd"
    echo "-h or --help"
    exit
fi

if [ $ENABLE_CMSG_PROF = 1 ]; then
    CONFIGURE_OPTIONS+="--enable-cmsg-prof "
    CFLAGS_OPTIONS+="-DHAVE_CMSG_PROFILING "
fi

if [ $ENABLE_VCSTACK = 1 ]; then
    CONFIGURE_OPTIONS+="--enable-vcstack "
    CFLAGS_OPTIONS+="-DHAVE_VCSTACK "
    CPPFLAGS+=" -I$(pwd)/../corosync/build/include "
    LDFLAGS+=" -L$(pwd)/../corosync/build/lib "
fi

if [ $ENABLE_COUNTERD = 1 ]; then
    CONFIGURE_OPTIONS+="--enable-counterd "
    CFLAGS_OPTIONS+="-DHAVE_COUNTERD "
fi

./autogen.sh

./configure --prefix=$(pwd)/build $CONFIGURE_OPTIONS \
    CPPFLAGS="$CPPFLAGS -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include/ -I$(pwd)/../../protobuf/build/include" \
    LDFLAGS="$LDFLAGS -L$(pwd)/../../protobuf/build/lib" \
    CFLAGS="$CFLAGS_OPTIONS"

make

make install
