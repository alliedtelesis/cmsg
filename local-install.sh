#!/bin/bash

./bootstrap

./configure --prefix=$(pwd)/build \
    CPPFLAGS="-I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include/ -I$(pwd)/../torus/corosync/build/include" \
    LDFLAGS=-L$(pwd)/../torus/corosync/build/lib

make

make install
