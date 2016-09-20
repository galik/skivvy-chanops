#!/bin/bash

source configure.conf 2> /dev/null
source ${0%*.sh}.conf 2> /dev/null

top_dir=$(pwd)

PREFIX=${PREFIX:-$HOME/dev}
LIBDIR=$PREFIX/lib

export PKG_CONFIG_PATH="$LIBDIR/pkgconfig"
# TODO: Fix need for -std=c++14 flag here

export CPPFLAGS="$CPPFLAGS"
export CXXFLAGS="$CXXFLAGS -std=c++14 -g3 -O0 -D DEBUG -U NDEBUG"
export LDFLAGS="$LDFLAGS"

rm -fr $top_dir/build-debug
mkdir -p $top_dir/build-debug

cd $top_dir/build-debug
$top_dir/configure --prefix=$PREFIX



