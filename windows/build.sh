#!/usr/bin/env bash
# Build the Windows package.
#
#   windows/build.sh [--clean] [--no-installer]
#
# Runs entirely in the common toolchain image (docker/common.Dockerfile), so
# docker is all the host needs - no MinGW, no Qt kit, no wine, no NSIS.
#
# Leaves dist/quackchat-<version>-win64.zip, and the NSIS installer beside it
# unless --no-installer. Both come from one staged tree: the installer asks for
# administrator because it writes under Program Files, so the ZIP is what
# someone without it unpacks and runs in place.
#
# build-win/ holds the build tree and persists, so a rebuild is incremental.
# --clean drops the app's half and keeps tacky's deps, which cost the better
# part of an hour to compile; delete build-win/ for those too.
#
# The tree is never shared with a host-native cross build: a different MinGW
# means objects wanting libgcc symbols the other runtime lacks, and nothing
# would report it.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)

clean=
installer=1
while [ "$#" -gt 0 ]; do
    case $1 in
        --clean)         clean=1 ;;
        --no-installer)  installer= ;;
        *) echo "usage: windows/build.sh [--clean] [--no-installer]" >&2; exit 2 ;;
    esac
    shift
done

# The commit date, as the AppImage does. The PE is not reproducible today - see
# the README - but zippy reads this for its own timestamps either way, and
# having it set is a precondition for ever getting there rather than a claim
# that we already have.
if [ -z "${SOURCE_DATE_EPOCH-}" ]; then
    SOURCE_DATE_EPOCH=$(git -C "$project" log -1 --format=%ct 2>/dev/null || true)
    [ -n "$SOURCE_DATE_EPOCH" ] || {
        echo "windows/build.sh: no git commit to date the build by." >&2
        echo "    set SOURCE_DATE_EPOCH to a unix timestamp and re-run" >&2
        exit 1
    }
fi
export SOURCE_DATE_EPOCH

# Exported rather than passed: docker/run.sh forwards SOURCE_DATE_EPOCH and
# every QUACK_* variable it finds.
export QUACK_WIN_CLEAN=$clean
export QUACK_WIN_INSTALLER=$installer

exec "$project/docker/run.sh" common windows/in-image.sh
