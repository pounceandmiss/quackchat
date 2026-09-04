#!/usr/bin/env bash
# Build the Flatpak bundle into dist/.
#
#   flatpak/build.sh [--keep-repo]
#
# Settled here rather than left to the caller, so that anyone building from a
# clean clone gets the same bundle: both pins are checked first, and
# SOURCE_DATE_EPOCH is taken from the commit date and handed to
# flatpak-builder. Left to itself flatpak-builder dates the build by the
# manifest's *file mtime*, which is whenever the tree happened to be checked
# out - so the same commit gives a different bundle on every machine.
#
# --keep-repo leaves flatpak/repo alone. By default it is rebuilt from scratch,
# because an ostree repo that already holds a commit for this ref is a second
# input nobody records.
#
# Companion to appimage/build.sh; both are called by release.sh and by
# tools/repro-check.sh.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)
cd "$project"

manifest=flatpak/io.github.pounceandmiss.Quack.yml
app_id=io.github.pounceandmiss.Quack

keep_repo=0
while [ "$#" -gt 0 ]; do
    case $1 in
        --keep-repo) keep_repo=1 ;;
        *) echo "usage: flatpak/build.sh [--keep-repo]" >&2; exit 2 ;;
    esac
    shift
done

die() { printf 'flatpak/build.sh: %s\n' "$*" >&2; exit 1; }

command -v flatpak-builder >/dev/null || die "flatpak-builder is not on PATH"
command -v flatpak >/dev/null || die "flatpak is not on PATH"

version=$(sed -n 's/^project(quack_qml VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$version" ] || die "no project(quack_qml VERSION ...) in CMakeLists.txt"

# The two inputs this build does not compile from sources it can name: tacky's
# commit, repeated in the manifest because flatpak-builder cannot read a
# submodule, and the KDE sdk, which the manifest can only name by branch. Both
# checked before the hour-long build rather than after.
./flatpak/check-pin.sh
./flatpak/check-runtime-pin.sh

# The commit date, matching appimage/build.sh. An explicit value in the
# environment wins, which is what tools/repro-check.sh uses to hold two builds to one
# value.
if [ -z "${SOURCE_DATE_EPOCH-}" ]; then
    SOURCE_DATE_EPOCH=$(git -C "$project" log -1 --format=%ct 2>/dev/null || true)
    [ -n "$SOURCE_DATE_EPOCH" ] || die "no git commit to date the build by; set SOURCE_DATE_EPOCH"
fi
export SOURCE_DATE_EPOCH
printf '==> SOURCE_DATE_EPOCH=%s (%s)\n' \
    "$SOURCE_DATE_EPOCH" "$(date -u -d "@$SOURCE_DATE_EPOCH")" >&2

if [ "$keep_repo" -eq 0 ]; then
    rm -rf flatpak/repo
fi

# No --disable-updates, whatever tacky's own Makefile does: it stops
# flatpak-builder refreshing an already-mirrored git repo, so a stale mirror
# gets built while the right sources sit unread.
flatpak-builder --force-clean --ccache \
    --override-source-date-epoch="$SOURCE_DATE_EPOCH" \
    --repo=flatpak/repo \
    flatpak/build-dir "$manifest"

# --runtime-repo is what lets `flatpak install` on the bundle go and fetch
# org.kde.Platform; without it the install fails on a machine that has no KDE
# runtime, which is most of them.
mkdir -p dist
out=dist/quackchat-$version-x86_64.flatpak
rm -f "$out"
flatpak build-bundle flatpak/repo "$out" "$app_id" \
    --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo

sha256sum "$out"
ls -l "$out"
