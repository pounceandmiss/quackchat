#!/usr/bin/env bash
# The build proper, from inside the Android container: it assumes that image's
# NDK, SDK, JDK and Qt kits, and the checkout bind-mounted at /src.
# android/build.sh is the entry point.
set -euo pipefail

build=/src/build-android
export CCACHE_DIR=$build/.ccache

export TZ=UTC
export LC_ALL=C.UTF-8
export QT_HASH_SEED=0
if [ -z "${SOURCE_DATE_EPOCH-}" ]; then
    echo "in-image.sh: SOURCE_DATE_EPOCH is unset; run android/build.sh, which sets it" >&2
    exit 1
fi
export SOURCE_DATE_EPOCH

# The NDK's toolchain directory goes on PATH here rather than in the image.
# android.mk invokes the API-versioned clang wrappers by bare name, so it has to
# be somewhere - but that directory also carries ld, clang and the llvm binutils,
# which would shadow the native and MinGW toolchains the common image provides
# and quietly break the other two targets. Confining it to this script keeps it
# where it is wanted.
export PATH="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"

if [ -n "${QUACK_ANDROID_CLEAN-}" ]; then
    rm -rf "$build"
fi
mkdir -p "$build"

# tacky's bionic archive. ANDROID_DOCKER=0 because this image already carries
# the NDK: left at its default the Makefile would call zippy/in_docker.sh and
# try to start the ndk container from inside this one.
#
# ANDROID_BUILD stays relative for the same reason WIN_BUILD does - the Makefile
# composes BASEDIR as $(ANDROID_ROOT)/$(ANDROID_BUILD), and ANDROID_ROOT is the
# tacky checkout under ANDROID_DOCKER=0. The archive lands in the submodule's
# dist/ as libtacky-android.a, which is what TACKY_ROOT points at.
echo "==> tacky"
make -C /src/third_party/tacky android-lib \
    ANDROID_DOCKER=0 \
    ANDROID_BUILD=../../build-android/tacky

# qt-cmake rather than cmake: it is the Android kit's wrapper, and it sets the
# toolchain file, the ABI and the deployment plumbing that androiddeployqt later
# reads out of the build tree.
#
# CMAKE_BUILD_TYPE is worth stating because qt-cmake leaves it empty, and an
# empty build type compiles with no -O flags yet still packages as a release.
# QT_ANDROID_DEPLOYMENT_TYPE decides the *package* type independently of it, so
# it is stated too rather than left to whatever the tree was last configured for.
# Optional webrtc media backend (see README.md), built before CMake looks for it.
echo "==> tacky webrtc backend"
if [ -z "${QUACK_WEBRTC_SRC-}" ]; then
    echo "    QUACK_WEBRTC_SRC is unset; rtc backend only"
else
    make -C /src/third_party/tacky android-webrtc-so \
        ANDROID_BUILD=../../build-android/tacky \
        WEBRTC_SRC="$QUACK_WEBRTC_SRC"
fi

echo "==> quackchat"
if [ -n "${QUACK_ANDROID_DEBUG-}" ]; then
    deployment=Debug
else
    deployment=Release
fi
"$QUACK_QT_ANDROID/bin/qt-cmake" -S /src -B "$build/cmake" -G Ninja \
    -DQT_HOST_PATH="$QUACK_QT_HOST" \
    -DANDROID_SDK_ROOT="$QUACK_ANDROID_SDK" \
    -DANDROID_NDK_ROOT="$QUACK_ANDROID_NDK" \
    -DANDROID_PLATFORM=android-30 \
    -DTACKY_ROOT=/src/third_party/tacky \
    -DCMAKE_BUILD_TYPE=Release \
    -DQUACK_QML_AOT=ON \
    -DQUACK_BUILD_TESTS=OFF \
    -DQT_ANDROID_DEPLOYMENT_TYPE="$deployment"

# Qt's apk target is add_custom_command(OUTPUT <final>), so ninja skips
# androiddeployqt whenever that file is newer than the binary it packages -
# including when the last build was a different deployment type entirely. Both
# are deleted so this run has to produce them: everything downstream passes just
# as well on a stale apk as on a fresh one.
final=$build/cmake/android-build/quackchat.apk
unsigned=$build/cmake/android-build/build/outputs/apk/release/android-build-release-unsigned.apk
debug_apk=$build/cmake/android-build/build/outputs/apk/debug/android-build-debug.apk
rm -f "$final" "$unsigned" "$debug_apk"

cmake --build "$build/cmake" --target apk

VERSION=$(sed -n 's/.*project(quack_qml VERSION \([0-9.]*\).*/\1/p' /src/CMakeLists.txt)
mkdir -p /src/dist
if [ -n "${QUACK_ANDROID_DEBUG-}" ]; then
    out=/src/dist/quackchat-$VERSION-debug.apk
    src=$debug_apk
else
    out=/src/dist/quackchat-$VERSION-unsigned.apk
    src=$unsigned
fi
[ -f "$src" ] || {
    echo "in-image.sh: the build did not produce $src" >&2
    exit 1
}
cp -f "$src" "$out"

# Signing, when a key was named. Android takes nothing unsigned, so a release
# apk is not installable until this happens - and it has to be the same key for
# every version ever published, since Android ties an app's identity to its
# certificate.
#
# Aligning has to come before signing, not after: apksigner signs the file as it
# lies, so realigning afterwards would invalidate the signature.
if [ -z "${QUACK_ANDROID_KEYSTORE-}" ] || [ -n "${QUACK_ANDROID_DEBUG-}" ]; then
    sha256sum "$out"
    ls -l "$out"
    exit 0
fi

tools=$(ls -d "$QUACK_ANDROID_SDK"/build-tools/* | sort -V | tail -1)
[ -x "$tools/apksigner" ] || {
    echo "in-image.sh: no apksigner under $QUACK_ANDROID_SDK/build-tools" >&2
    exit 1
}

echo "==> signing"
aligned=$build/quackchat-$VERSION-aligned.apk
signed=/src/dist/quackchat-$VERSION.apk
"$tools/zipalign" -p -f 4 "$out" "$aligned"

pass_args=()
if [ -n "${QUACK_ANDROID_KEYSTORE_PASS-}" ]; then
    pass_args=(--ks-pass env:QUACK_ANDROID_KEYSTORE_PASS
               --key-pass env:QUACK_ANDROID_KEYSTORE_PASS)
fi
"$tools/apksigner" sign \
    --ks "$QUACK_ANDROID_KEYSTORE" --ks-key-alias "$QUACK_ANDROID_KEY_ALIAS" \
    "${pass_args[@]}" \
    --out "$signed" "$aligned"
"$tools/apksigner" verify --print-certs "$signed"

rm -f "$out"
sha256sum "$signed"
ls -l "$signed"
