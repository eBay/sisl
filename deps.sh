#!/bin/bash

PROTOBUF_RELEASE=3.4.1
GLOG_RELEASE=0.3.5
GTEST_RELEASE=1.8.0
LIBGFLAGS_VERSION=2.2.0
PROMETHEUS_CPP_CLIENT_TAG=v0.1.2

# Parse args
JOBS=8

uname -a | grep Darwin >> /dev/null && is_mac=1 || true
uname -a | grep Ubuntu >> /dev/null && is_ubuntu=1 || true

# Parse args
read -r -d '' USAGE << EOM
deps.sh [-j num_jobs] [-F]
    -j number of paralle jobs to use during the build (default: 8)
    -F destroy caches and rebuild all dependencies
EOM

while getopts "xFj:" opt; do
  case $opt in
    F)
        FORCE=1;;
    x)
        set -x;;
    *)
        echo "$USAGE"
        exit 1;;
   \?)
        echo "Invalid option: -$OPTARG" >&2
        exit 1;;
    j)
        JOBS=$opt;;
    :)
      echo "Option $OPTARG requires an argument." >&2
      exit 1
  esac
done

start_dir=`pwd`
trap "cd $start_dir" EXIT

set -eE #enable bash debug mode

error() {
   local sourcefile=$1
   local lineno=$2
   echo abnormal exit at $sourcefile, line $lineno
}
trap 'error "${BASH_SOURCE}" "${LINENO}"' ERR

# Must execute from the directory containing this script
cd "$(dirname "$0")"
project_dir=`pwd`
deps_prefix=$project_dir/deps_prefix
deps_build=$project_dir/deps_build

if [ -n "$FORCE" ]; then
    rm -rf $deps_prefix
    rm -rf $deps_build
fi


mkdir -p $deps_prefix
mkdir -p $deps_build
cd $deps_build

function library() {
    name=$1 # - library name; becomes directory prefix
    version=$2 # - library version or commit or tag; becomes directory suffix
    url=$3 # - URL; must have the name and version already expanded
    branch=${4:-master} #- parameter for git download method

    dirname=$name-$version
    if [ ! -e $dirname -o ! -e $dirname/build_success ]; then
        rm -rf $dirname
        echo "Fetching $dirname"

        case $url in
            *.tar.gz)
                if [ ! -f $dirname.tar.gz ]; then
                    wget --max-redirect=5 -O $dirname.tar.gz $url
                fi
                tar zxf $dirname.tar.gz
                test -d $dirname
                cd $dirname;;
            *.h|*.hpp )
                mkdir $dirname
                wget --max-redirect=5 --directory-prefix=$dirname $url
                cd $dirname;;
            *.git)
                git clone -b $branch $url $dirname
                cd $dirname
                git cat-file -e $version^{commit} && git checkout $version || true;;
            *)
              echo Unable to derive download method from url $url
              exit 1;;
        esac

        $name # invoke the build function

        cd $deps_build
        touch $dirname/build_success
    fi
}

do-cmake() {
    cmake -H. -Bcmake-build -DCMAKE_INSTALL_PREFIX:PATH=$deps_prefix -DCMAKE_PREFIX_PATH=$deps_prefix \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release} \
        -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" \
        "$@"
    cmake --build cmake-build -- -j$JOBS
    make_target=${make_target:-install}
    cmake --build cmake-build -- $make_target
}
do-configure () {
    ./configure --prefix=$deps_prefix "$@"
}
do-make() {
    make -j$JOBS "$@"
    make install
}

gflags() {
    do-cmake
}
library gflags ${LIBGFLAGS_VERSION} https://github.com/gflags/gflags/archive/v${LIBGFLAGS_VERSION}.tar.gz

#-------------------------------------------------
protobuf() {
    ./autogen.sh
    #CXXFLAGS=-std=c++11 DIST_LANG=cpp
    do-configure
    do-make
}

library protobuf ${PROTOBUF_RELEASE} https://github.com/google/protobuf/releases/download/v${PROTOBUF_RELEASE}/protobuf-cpp-${PROTOBUF_RELEASE}.tar.gz

glog () {
    aclocal
    automake --add-missing
    do-configure
    do-make
}
library glog ${GLOG_RELEASE} https://github.com/google/glog/archive/v${GLOG_RELEASE}.tar.gz

googletest-release() {
  do-cmake
}

library googletest-release ${GTEST_RELEASE} https://github.com/google/googletest/archive/release-${GTEST_RELEASE}.tar.gz

prometheus-cpp () {
    git submodule update --init
    git apply --whitespace=warn $project_dir/patches/prometheus-cpp/prometheus-cpp.patch
    do-cmake \
            -DProtobuf_LIBRARY=$deps_prefix/lib/libprotobuf.a \
            -DProtobuf_PROTOC_EXECUTABLE=$deps_prefix/bin/protoc
}
library prometheus-cpp ${PROMETHEUS_CPP_CLIENT_TAG} https://github.com/jupp0r/prometheus-cpp.git ${PROMETHEUS_CPP_CLIENT_TAG}


