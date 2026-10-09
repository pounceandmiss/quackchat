#!/usr/bin/env bash
# The test run proper, from inside the toolchain container. tools/test.sh is the
# entry point.
set -euo pipefail

build=/src/build-test
export CCACHE_DIR=$build/.ccache
mkdir -p "$build"

# Shares build-appimage's tacky tree, and the hour its deps take.
echo "==> tacky"
make -C /src/third_party/tacky lib \
    LINUX_BUILD=/src/build-appimage/tacky \
    DEPS_DIR=/src/build-deps

echo "==> quackchat and its tests"
cmake -S /src -B "$build/cmake" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$QTDIR" \
    -DQUACK_BUILD_TESTS=ON \
    -DTACKY_ROOT=/src/third_party/tacky
cmake --build "$build/cmake"

echo "==> ctest"
cd "$build/cmake"
# shellcheck disable=SC2086 # the arguments were joined for the trip, split them
ctest --output-on-failure -j"$(nproc)" ${QUACK_CTEST_ARGS-}
