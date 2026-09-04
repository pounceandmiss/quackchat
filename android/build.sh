#!/usr/bin/env bash
# Build the Android apk.
#
#   android/build.sh [--clean] [--debug]
#
# Runs entirely in the Android toolchain image (docker/android.Dockerfile), so
# docker is all the host needs - no SDK, no NDK r29, no JDK 21, no Qt kit.
#
# A release build leaves dist/quackchat-<version>-unsigned.apk. Android installs
# nothing unsigned, so that file is an input to signing rather than something to
# hand out; release.sh signs it with the key named in release.env. --debug builds
# the debug-signed apk instead, which gradle signs with the throwaway key in the
# image and which does install - for testing only, since nothing that key signed
# can ever be published.
#
# build-android/ holds the build tree and persists, so a rebuild is incremental.
# --clean drops it. tacky's Android dependency tree stays inside it rather than
# joining the one the Linux and Windows builds share: android.mk builds those
# dependencies in place, in their own source directories, so that tree belongs to
# this target alone.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)

clean=
debug=
while [ "$#" -gt 0 ]; do
    case $1 in
        --clean) clean=1 ;;
        --debug) debug=1 ;;
        *) echo "usage: android/build.sh [--clean] [--debug]" >&2; exit 2 ;;
    esac
    shift
done

# The commit date, as the other targets do. The apk is not reproducible - gradle
# would have to be made deterministic first - but zippy reads this for tacky's
# half either way.
if [ -z "${SOURCE_DATE_EPOCH-}" ]; then
    SOURCE_DATE_EPOCH=$(git -C "$project" log -1 --format=%ct 2>/dev/null || true)
    [ -n "$SOURCE_DATE_EPOCH" ] || {
        echo "android/build.sh: no git commit to date the build by." >&2
        echo "    set SOURCE_DATE_EPOCH to a unix timestamp and re-run" >&2
        exit 1
    }
fi
export SOURCE_DATE_EPOCH

export QUACK_ANDROID_CLEAN=$clean
export QUACK_ANDROID_DEBUG=$debug

# The signing key, if there is one. It is mounted read-only rather than copied
# anywhere: it must not end up in the checkout, in a layer, or in an image. The
# container path replaces the host one in the environment the build sees, so
# nothing inside has to know where it came from.
#
# QUACK_ANDROID_KEYSTORE_PASS is forwarded by name, not value, so the password
# never appears in a command line and so never in /proc. Left unset, apksigner
# prompts - which needs a terminal, and docker/run.sh gives the container one
# when this script has one.
if [ -n "${QUACK_ANDROID_KEYSTORE-}" ]; then
    [ -f "$QUACK_ANDROID_KEYSTORE" ] || {
        echo "android/build.sh: no keystore at $QUACK_ANDROID_KEYSTORE" >&2
        exit 1
    }
    [ -n "${QUACK_ANDROID_KEY_ALIAS-}" ] || {
        echo "android/build.sh: QUACK_ANDROID_KEY_ALIAS is unset but a keystore is named" >&2
        exit 1
    }
    export QUACK_RUN_MOUNTS="$(cd "$(dirname "$QUACK_ANDROID_KEYSTORE")" && pwd)/$(basename "$QUACK_ANDROID_KEYSTORE"):/keystore:ro"
    export QUACK_ANDROID_KEYSTORE=/keystore
    export QUACK_ANDROID_KEY_ALIAS
fi

exec "$project/docker/run.sh" android android/in-image.sh
