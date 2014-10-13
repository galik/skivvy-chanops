#!/bin/bash

top_dir=$(pwd)

DEV_HOME=$HOME/dev

export CXX=g++
#export CXX=clang++

CXXFLAGS="$CXXFLAGS -g0"
CXXFLAGS="$CXXFLAGS -O3"
export CXXFLAGS

rm -fr $top_dir/build-release
mkdir -p $top_dir/build-release
cd $top_dir/build-release

$top_dir/configure --prefix=$HOME --enable-silent-rules



