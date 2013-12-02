#!/bin/bash

./bootstrap

CPPFLAGS=' -Wno-unused-but-set-variable -Wno-error=address -DLOCAL_INSTALL'

if test "$1" == "--disable-vcstack"
then
    ./configure --prefix=$(pwd)/build --enable-cmsg-prof \
        CPPFLAGS="$CPPFLAGS -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include/ -I$(pwd)/../../protobuf/build/include" \
        LDFLAGS="-L$(pwd)/../../protobuf/build/lib" \
        CFLAGS=-DHAVE_CMSG_PROFILING
else
    ./configure --prefix=$(pwd)/build --enable-cmsg-prof --enable-vcstack \
        CPPFLAGS="$CPPFLAGS -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include/ -I$(pwd)/../corosync/build/include -I$(pwd)/../../protobuf/build/include" \
        LDFLAGS="-L$(pwd)/../corosync/build/lib -L$(pwd)/../../protobuf/build/lib" \
        CFLAGS=-DHAVE_CMSG_PROFILING -DHAVE_VCSTACK
fi

make

make install
