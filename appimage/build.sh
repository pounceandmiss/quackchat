#!/usr/bin/env bash
# Build the portable Linux AppImage.
#
#   appimage/build.sh [--clean] [--no-aot]
#
# Runs entirely in the common toolchain image (docker/common.Dockerfile), so
# docker is all the host needs. The result lands in dist/ and runs on glibc 2.34
# or newer.
#
# The build is reproducible: one commit gives one set of bytes. SOURCE_DATE_EPOCH
# carries that - taken from the commit date below, it stands in for every real
# timestamp the build would otherwise pick up. Set it to override, which is what
# tools/repro-check.sh does to hold two builds to the same value.
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

# The commit date, not the wall clock: two builds of one commit have to agree,
# and a build of a later commit should differ. A dirty tree gets HEAD's date,
# an uncommitted change not being something anyone reproduces.
if [ -z "${SOURCE_DATE_EPOCH-}" ]; then
    SOURCE_DATE_EPOCH=$(git -C "$project" log -1 --format=%ct 2>/dev/null || true)
    [ -n "$SOURCE_DATE_EPOCH" ] || {
        echo "appimage/build.sh: no git commit to date the build by." >&2
        echo "    set SOURCE_DATE_EPOCH to a unix timestamp and re-run" >&2
        exit 1
    }
fi
export SOURCE_DATE_EPOCH

# Exported rather than passed: docker/run.sh forwards SOURCE_DATE_EPOCH and
# every QUACK_* variable it finds, which is how a target's knobs reach the
# in-image half without that script knowing what any of them mean.
export QUACK_APPIMAGE_CLEAN=$clean
export QUACK_APPIMAGE_AOT=$aot

exec "$project/docker/run.sh" common appimage/in-image.sh
