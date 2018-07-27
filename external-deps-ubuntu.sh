#!/bin/bash

sudo apt-get update
sudo apt-get install -yq software-properties-common
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update

# Some extra dependencies for Ubuntu 13.10 and 14.04
sudo apt-get install -yq \
  g++-7 \
  pkg-config \
  libssl-dev \
  libjemalloc-dev \
  wget \
  libtool \
  autoconf \
  automake

sudo apt-get remove -yq \
  libgflags2v5 \
  libboost-all-dev \
  libc-ares-dev \
  cmake

sudo update-alternatives --install /usr/bin/cpp cpp /usr/bin/cpp-7 70 && \
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-7 70 && \
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 70 && \
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 70 && \
sudo update-alternatives --install /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-7 70 && \
sudo update-alternatives --install /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-7 70 && \
sudo update-alternatives --install /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-7 70 && \
sudo update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-7 70

set -e

BOOST_MAJOR=1
BOOST_MINOR=66
BOOST_PATCH=0

BOOST_RELEASE_TAG=${BOOST_MAJOR}_${BOOST_MINOR}_${BOOST_PATCH}
# install boost
pushd /tmp
wget -O /tmp/boost_${BOOST_RELEASE_TAG}.tar.gz https://dl.bintray.com/boostorg/release/${BOOST_MAJOR}.${BOOST_MINOR}.${BOOST_PATCH}/source/boost_${BOOST_RELEASE_TAG}.tar.gz
tar -xvzf boost_${BOOST_RELEASE_TAG}.tar.gz
cd boost_${BOOST_RELEASE_TAG}
./bootstrap.sh --with-libraries=system,filesystem,thread,stacktrace,date_time,regex,serialization
sudo ./b2 install
popd
sudo rm -rf /tmp/boost_${BOOST_RELEASE_TAG}*

# install cmake
version=3.10.1
cd /tmp
wget -O CMake-$version.tar.gz https://github.com/Kitware/CMake/archive/v${version}.tar.gz
tar -xzvf CMake-$version.tar.gz
cd CMake-$version/
./bootstrap
make -j4
sudo make install

#popd
rm -rf /tmp/CMake*


