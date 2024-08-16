#!/bin/sh
set -eu

mkdir -p build/cmake

arch=$(uname -m)
case "$arch" in
  x86_64|aarch64)
    cmake_arch=${arch};;
  *)
    echo "Bootstrap script does not support arch: ${arch}."; exit 1;;
esac

cmake_version=3.30.2
cmake_name=cmake-${cmake_version}-linux-${cmake_arch}

cmake_url=https://github.com/Kitware/CMake/releases/download/v${cmake_version}/${cmake_name}.tar.gz

curl -s -L "${cmake_url}" | tar xz -C build/cmake/

"./build/cmake/${cmake_name}/bin/cmake" -B build -D CMAKE_BUILD_TYPE=MinSizeRel
