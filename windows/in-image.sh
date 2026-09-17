#!/usr/bin/env bash
# The build proper, from inside the Windows cross-build container: it assumes
# that image's MinGW, Qt kits, wine and NSIS, and the checkout bind-mounted at
# /src. windows/build.sh is the entry point.
set -euo pipefail

build=/src/build-win
export CCACHE_DIR=$build/.ccache

export TZ=UTC
export LC_ALL=C.UTF-8
export QT_HASH_SEED=0
if [ -z "${SOURCE_DATE_EPOCH-}" ]; then
    echo "in-image.sh: SOURCE_DATE_EPOCH is unset; run windows/build.sh, which sets it" >&2
    exit 1
fi
export SOURCE_DATE_EPOCH
echo "==> SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH ($(date -u -d "@$SOURCE_DATE_EPOCH"))"

if [ -n "${QUACK_WIN_CLEAN-}" ]; then
    rm -rf "$build/cmake" "$build/pkg"
fi
mkdir -p "$build"

# tacky's MinGW archive, into our own tree rather than the submodule's
# build/windows - a host-native cross build leaves objects of another MinGW
# there, and the two must never meet.
#
# WIN_BUILD has to stay *relative*: the Makefile composes BASEDIR as
# $(WIN_ROOT)/$(WIN_BUILD), so an absolute path would be appended to the tacky
# checkout rather than replacing it. WIN_DEPS_DIR is passed straight through as
# DEPSDIR and is absolute. The archive still lands in the submodule's dist/,
# which is what TACKY_ROOT points at.
#
# That DEPSDIR is shared with the AppImage build - unpacked sources, not objects;
# see appimage/in-image.sh for why that is safe.
echo "==> tacky"
make -C /src/third_party/tacky win-lib \
    WIN_TCLSH="$QUACK_WIN_TCLSH" \
    WIN_BUILD=../../build-win/tacky \
    WIN_DEPS_DIR=/src/build-deps

# Optional webrtc media backend (see README.md), built before CMake looks for it.
echo "==> tacky webrtc backend"
if [ -z "${QUACK_WEBRTC_SRC-}" ]; then
    echo "    QUACK_WEBRTC_SRC is unset; rtc backend only"
else
    make -C /src/third_party/tacky win-webrtc-dll \
        WIN_TCLSH="$QUACK_WIN_TCLSH" \
        WIN_BUILD=../../build-win/tacky \
        WIN_DEPS_DIR=/src/build-deps \
        WEBRTC_SRC="$QUACK_WEBRTC_SRC"
fi

# QUACK_FAST_LINKER=OFF for the same reason as the AppImage: which linker ran is
# visible in the binary, so it must not depend on what happens to be installed.
#
# The MinGW runtime DLLs that go beside the exe need no naming here: CMake asks
# the compiler where they are, which is the only answer that survives this
# image and an Arch host having different layouts.
echo "==> quackchat"
cmake -S /src -B "$build/cmake" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/src/cmake/mingw-w64-toolchain.cmake \
    -DCMAKE_PREFIX_PATH="$QUACK_QT_MINGW" \
    -DQT_HOST_PATH="$QUACK_QT_HOST" \
    -DTACKY_ROOT=/src/third_party/tacky \
    -DCMAKE_BUILD_TYPE=Release \
    -DQUACK_BUILD_TESTS=OFF \
    -DQUACK_FAST_LINKER=OFF
cmake --build "$build/cmake"

# windeployqt ships only as a .exe, so cpack runs it under wine. The prefix has
# to be named: docker/run.sh points HOME at /tmp, which belongs to root, and
# wine refuses to build a configuration directory in a HOME it does not own.
# The build tree is bind-mounted and owned by the invoking user, so it goes
# there - and persists, which saves rebuilding the prefix on every run.
export WINEPREFIX=$build/.wine
mkdir -p "$WINEPREFIX"

# Both package shapes come from the one staged tree.
echo "==> package"
generators=ZIP
if [ -n "${QUACK_WIN_INSTALLER-}" ]; then
    generators="NSIS;ZIP"
fi
rm -rf "$build/pkg"
cpack --config "$build/cmake/CPackConfig.cmake" -G "$generators" -B "$build/pkg"

VERSION=$(sed -n 's/.*project(quack_qml VERSION \([0-9.]*\).*/\1/p' /src/CMakeLists.txt)
mkdir -p /src/dist
for f in "quackchat-$VERSION-win64.zip" "quackchat-$VERSION-win64.exe"; do
    if [ -f "$build/pkg/$f" ]; then
        mv -f "$build/pkg/$f" /src/dist/
        sha256sum "/src/dist/$f"
    fi
done
ls -l /src/dist/quackchat-"$VERSION"-win64.* 2>/dev/null || true
