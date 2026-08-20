#!/usr/bin/env bash
# The build proper, from inside the toolchain container: it assumes that image's
# Qt and AppImage tooling, and the checkout bind-mounted at /src.
# appimage/build.sh is the entry point.
set -euo pipefail

build=/src/build-appimage
appdir=$build/AppDir
export CCACHE_DIR=$build/.ccache

# The app's half only. tacky's deps and the compiler cache cost far more to
# rebuild than an app change can ever invalidate.
if [ -n "${QUACK_APPIMAGE_CLEAN-}" ]; then
    rm -rf "$build/cmake"
fi
mkdir -p "$build"

# zippy compiles Tcl and every C dep from source, so a cold run is the better
# part of an hour and later ones are seconds. Its build tree and dep cache are
# named away from third_party/tacky/build/linux, where a host-native `make lib`
# leaves objects of another glibc; the archive still lands in the submodule's
# dist/, which is what TACKY_ROOT points at.
echo "==> tacky"
make -C /src/third_party/tacky lib \
    LINUX_BUILD="$build/tacky" \
    DEPS_DIR="$build/tacky-deps"

# Prefix /usr with DESTDIR into the AppDir is the layout linuxdeploy expects: it
# takes the desktop entry and the hicolor icon out of usr/share itself.
echo "==> quackchat"
if [ -n "${QUACK_APPIMAGE_AOT-}" ]; then aot=ON; else aot=OFF; fi
cmake -S /src -B "$build/cmake" -G Ninja \
    -DQUACK_QML_AOT="$aot" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QTDIR" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DQUACK_BUILD_TESTS=OFF \
    -DTACKY_ROOT=/src/third_party/tacky
cmake --build "$build/cmake"

rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build/cmake"

# QML_SOURCES_PATHS is what gives the Qt plugin anything to scan, the app's own
# QML being compiled into the binary. It reports `Quack` as a missing module in
# consequence, which is right: there is nothing on disk to deploy for it.
#
# LD_LIBRARY_PATH because this Qt sits under /opt, off the loader's path.
# Without it the executable still resolves through its rpath and only the second
# pass over the deployed QML plugins fails, naming a Qt library that is plainly
# there.
#
# VERSION names the finished file.
echo "==> deploy"
export QMAKE=$QTDIR/bin/qmake
export QML_SOURCES_PATHS=/src
export LD_LIBRARY_PATH=$QTDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
VERSION=$(sed -n 's/.*project(quack_qml VERSION \([0-9.]*\).*/\1/p' /src/CMakeLists.txt)
export VERSION

cd "$build"
linuxdeploy --appdir "$appdir" --plugin qt --output appimage

mkdir -p /src/dist
mv -f ./*.AppImage /src/dist/
ls -l /src/dist/*.AppImage
