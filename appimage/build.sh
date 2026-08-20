#!/usr/bin/env bash
# Build the portable Linux AppImage.
#
#   appimage/build.sh [--clean] [--no-aot]
#
# Runs entirely in the toolchain image (appimage/Dockerfile), so docker is all
# the host needs. The result lands in dist/ and runs on glibc 2.34 or newer.
#
# build-appimage/ holds the build tree and persists, so a rebuild is
# incremental. --clean drops the app's half of it and keeps tacky's deps, which
# cost the better part of an hour to compile; delete build-appimage/ for those
# too. --no-aot skips the ahead-of-time QML compile, which dominates a release
# build and has no bearing on packaging.
#
# The tree is never shared with a host-native build: different toolchain,
# different glibc, and objects from one are no use to the other.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)
image=quack-appimage:build

clean=
aot=1
while [ "$#" -gt 0 ]; do
    case $1 in
        --clean)  clean=1 ;;
        --no-aot) aot= ;;
        *) echo "usage: appimage/build.sh [--clean] [--no-aot]" >&2; exit 2 ;;
    esac
    shift
done

if [ ! -e "$project/third_party/tacky/embed/tacky.h" ]; then
    echo "appimage/build.sh: third_party/tacky is empty." >&2
    echo "    git submodule update --init --recursive third_party/tacky" >&2
    exit 1
fi

# Layer-cached, so near-instant unless the Dockerfile moved. Build chatter to
# stderr, leaving stdout to the build's own progress.
DOCKER_BUILDKIT=1 docker build -t "$image" -f "$here/Dockerfile" "$here" >&2

# As the invoking user, so nothing is left root-owned in the checkout. That uid
# has no passwd entry in the image, so HOME has to be named outright.
exec docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e QUACK_APPIMAGE_CLEAN="$clean" \
    -e QUACK_APPIMAGE_AOT="$aot" \
    -v "$project:/src" \
    -w /src \
    "$image" appimage/in-image.sh
