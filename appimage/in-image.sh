#!/usr/bin/env bash
# The build proper, from inside the toolchain container: it assumes that image's
# Qt and AppImage tooling, and the checkout bind-mounted at /src.
# appimage/build.sh is the entry point.
set -euo pipefail

build=/src/build-appimage
appdir=$build/AppDir
export CCACHE_DIR=$build/.ccache

# Reproducibility. SOURCE_DATE_EPOCH is the commit date, set by
# appimage/build.sh: zippy reads it and turns on -ffile-prefix-map and fixed
# zipfs entry mtimes, Qt's rcc stamps it into the resource data in place of each
# file's real mtime, and the packaging step at the bottom passes the same value
# on to mksquashfs. Without it, all three record whenever the tree happened to
# be checked out.
#
# Build paths, the usual culprit, need no such handling: the checkout is
# bind-mounted at /src on every machine, so the tree is in the same place for
# everyone.
#
# TZ and LC_ALL so that a timestamp that does get formatted, and any sort order a
# build step depends on, cannot follow the host. QT_HASH_SEED because QHash seeds
# itself randomly per process, and moc, rcc and qmltyperegistrar all walk hashed
# containers on their way to generating code.
export TZ=UTC
export LC_ALL=C.UTF-8
export QT_HASH_SEED=0
if [ -z "${SOURCE_DATE_EPOCH-}" ]; then
    echo "in-image.sh: SOURCE_DATE_EPOCH is unset; run appimage/build.sh, which sets it" >&2
    exit 1
fi
export SOURCE_DATE_EPOCH
echo "==> SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH ($(date -u -d "@$SOURCE_DATE_EPOCH"))"

# The app's half only. tacky's deps and the compiler cache cost far more to
# rebuild than an app change can ever invalidate.
if [ -n "${QUACK_APPIMAGE_CLEAN-}" ]; then
    rm -rf "$build/cmake"
fi
mkdir -p "$build"

# zippy compiles Tcl and every C dep from source, so a cold run is the better
# part of an hour and later ones are seconds. Its build tree is named away from
# third_party/tacky/build/linux, where a host-native `make lib` leaves objects
# of another glibc; the archive still lands in the submodule's dist/, which is
# what TACKY_ROOT points at.
#
# The unpacked sources are shared with the Windows build at /src/build-deps,
# which the build trees deliberately are not. zippy makes that safe: most of its
# dependencies build out of tree, and the two that cannot - omemo and tclwuffs,
# which compile inside their own source directory - are copied to the cross
# build's BUILDDIR first, so neither build links the other's objects
# (zippy/zippy.mk). Two targets must not build at once, though: they would be
# unpacking into the same directory.
echo "==> tacky"
make -C /src/third_party/tacky lib \
    LINUX_BUILD="$build/tacky" \
    DEPS_DIR=/src/build-deps

# Prefix /usr with DESTDIR into the AppDir is the layout linuxdeploy expects: it
# takes the desktop entry and the hicolor icon out of usr/share itself.
#
# QUACK_FAST_LINKER=OFF so the binary does not depend on whether mold is
# installed - it is not, in this image, but relying on that is how a rebuild in a
# slightly altered image quietly produces different bytes.
echo "==> quackchat"
if [ -n "${QUACK_APPIMAGE_AOT-}" ]; then aot=ON; else aot=OFF; fi
cmake -S /src -B "$build/cmake" -G Ninja \
    -DQUACK_QML_AOT="$aot" \
    -DQUACK_FAST_LINKER=OFF \
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
echo "==> deploy"
export QMAKE=$QTDIR/bin/qmake
export QML_SOURCES_PATHS=/src
export LD_LIBRARY_PATH=$QTDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
VERSION=$(sed -n 's/.*project(quack_qml VERSION \([0-9.]*\).*/\1/p' /src/CMakeLists.txt)

# Deliberately no `--output appimage`: that hands packaging to linuxdeploy's
# bundled appimage plugin, which offers no way through to the mksquashfs
# arguments or the runtime file below. Populating the AppDir and packaging it are
# two separate steps here for that reason.
cd "$build"
linuxdeploy --appdir "$appdir" --plugin qt

# The glibc floor, checked rather than assumed. Everything in the AppDir was
# built or bundled by this image, so nothing should reference a symbol newer
# than the image's own glibc - but a moved vault snapshot, or a library that
# came from EPEL rather than the vault, could raise it without anyone noticing,
# and the AppImage would then refuse to start on the distros the README names.
#
# The AppDir rather than the finished file: an AppImage begins with a static
# runtime stub, so objdump on that reports no glibc dependency at all and looks
# like a pass whatever is inside.
floor_max=GLIBC_2.34
floor=$(find "$appdir" -type f \( -name '*.so*' -o -perm -u+x \) \
    -exec objdump -T {} + 2>/dev/null \
    | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)
echo "==> glibc floor: ${floor:-none found}"
if [ -n "$floor" ] &&
   [ "$(printf '%s\n%s\n' "$floor" "$floor_max" | sort -V | tail -1)" != "$floor_max" ]; then
    echo "in-image.sh: the AppImage needs $floor, above the $floor_max floor." >&2
    echo "    Something in docker/common.Dockerfile moved. The distros the" >&2
    echo "    README names would no longer run this." >&2
    exit 1
fi

# An AppImage is a squashfs, which records a timestamp and an owner for every
# entry, plus one for the filesystem itself. Left alone those come from the
# checkout's mtimes and from whichever uid docker was told to run as - so the
# same commit packaged on two machines differs, in thousands of places, for no
# reason to do with the software. -all-time and -mkfs-time replace them with the
# commit date; -all-root replaces the owner with 0:0.
#
# mksquashfs reads SOURCE_DATE_EPOCH itself and refuses to run when both it and
# the equivalent options are given - "SOURCE_DATE_EPOCH and command line options
# can't be used at the same time to set timestamp(s)" - so `env -u` takes it out
# of the way for this one call. The options are the half to keep: the variable
# alone dates the filesystem header and the entries mksquashfs synthesises, and
# leaves every regular file's real mtime in place. It stays exported everywhere
# above, where zippy and rcc are the ones reading it.
#
# --runtime-file is the other half. Left to itself appimagetool downloads the
# type-2 runtime from the `continuous` release at this point and prepends it,
# making the first ~900KB of every AppImage whatever upstream published most
# recently. The image stages a pinned copy instead.
#
# ARCH because appimagetool otherwise guesses from the AppDir's binaries and
# refuses when it cannot.
echo "==> package"
# Staged by the Dockerfile. Checked rather than assumed, because an image built
# before that step existed leaves it empty, and appimagetool's complaint about an
# unreadable runtime file names neither this variable nor the image.
if [ ! -s "${APPIMAGE_RUNTIME_FILE-}" ]; then
    echo "in-image.sh: no AppImage runtime at '${APPIMAGE_RUNTIME_FILE-}'." >&2
    echo "    the toolchain image predates it; docker/common.Dockerfile stages it," >&2
    echo "    and docker/run.sh rebuilds the image on every run." >&2
    exit 1
fi
out=quackchat-$VERSION-x86_64.AppImage
rm -f "$out"
env -u SOURCE_DATE_EPOCH ARCH=x86_64 appimagetool \
    --runtime-file "$APPIMAGE_RUNTIME_FILE" \
    --mksquashfs-opt -all-root \
    --mksquashfs-opt -all-time --mksquashfs-opt "$SOURCE_DATE_EPOCH" \
    --mksquashfs-opt -mkfs-time --mksquashfs-opt "$SOURCE_DATE_EPOCH" \
    "$appdir" "$out"

mkdir -p /src/dist
mv -f "$out" /src/dist/
sha256sum "/src/dist/$out"
ls -l "/src/dist/$out"
