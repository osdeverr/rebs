#! /usr/bin/env bash

mkdir -p out
cd out
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=/usr/local/Cellar/vcpkg/2020.06_1/libexec/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_INSTALL_PREFIX="/usr/local/" \
    -DCMAKE_BUILD_TYPE="RelWithDebInfo"
