#!/bin/bash
# The only prerequisite should be homebrew.


brewget() {
    brew install $@ || brew upgrade $@
}

brewget \
    pkg-config \
    protobuf \
    gflags \
    c-ares \
    wget \
    cmake \
    scons \
    automake \
    boost \
    clang-format \
    https://raw.githubusercontent.com/DomT4/homebrew-crypto/master/Formula/boringssl.rb
#   jemalloc \
#   glib \
#    mysql-connector-c \
#    json-c \
