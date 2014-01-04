#!/bin/sh
BIN=/var/tmp/build
test $BIN -nt build.cc -a $BIN -nt core/thread.cc  -a $BIN -nt core/memory.h || \
 clang++ -march=native -iquotecore -iquoteio -std=c++11 -DASSERT core/memory.cc core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread \
 -o $BIN
$BIN $TARGET $BUILD $*
