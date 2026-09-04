#!/usr/bin/env bash
# Check that a release artifact is reproducible: build it twice, the second time
# from a pristine clone of HEAD, and compare the two byte for byte.
#
#   tools/repro-check.sh [--keep] [appimage] [flatpak]
#
# The AppImage runs when no target is named, being the only one held to
# reproducing. The Flatpak is selectable, but no one has yet run it that way: it
# builds and installs, and whether it reproduces is unmeasured.
# --keep leaves the clone to poke at.
#
# A clone rather than a rebuild in place, because it differs from the checkout in
# new file mtimes, a different path, and a different time of day. It is also
# cold, so tacky's dependencies compile from scratch - the better part of an
# hour. Both sides are built, since an artifact already in dist/ records nothing
# about the SOURCE_DATE_EPOCH it was built with and would report a difference
# that is only the timestamp.
#
# A mismatch is reported with diffoscope when it is installed; without it, byte
# offsets, which is not much to go on for a squashfs.
#
# This cannot tell you a different *machine* gets the same bytes. Everything the
# container controls is pinned (docker/*.Dockerfile), but only a second machine
# proves it.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

die() { printf 'repro-check: %s\n' "$*" >&2; exit 1; }
note() { printf '\n== %s\n' "$*" >&2; }

keep=0
targets=

while [ "$#" -gt 0 ]; do
    case $1 in
        --keep)    keep=1 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; $d'; exit 0 ;;
        appimage|flatpak) targets="$targets $1" ;;
        *) die "unknown argument $1 (want --keep, appimage, flatpak)" ;;
    esac
    shift
done
targets=${targets# }
[ -n "$targets" ] || targets=appimage

version=$(sed -n 's/^project(quack_qml VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$version" ] || die "no project(quack_qml VERSION ...) in CMakeLists.txt"

# A dirty tree cannot be cloned, so the second build would be of something else
# and the comparison would say nothing.
dirty=$(git status --porcelain --ignore-submodules=dirty)
[ -z "$dirty" ] || die "the tree has uncommitted changes, which a clone of HEAD will not have:
$dirty"

# Both sides are held to HEAD's commit date. The build scripts derive the same
# value on their own; stating it here means the two cannot disagree even if that
# derivation later changes.
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)
export SOURCE_DATE_EPOCH
commit=$(git rev-parse HEAD)

artifact_for() {
    case $1 in
        appimage) echo "quackchat-$version-x86_64.AppImage" ;;
        flatpak)  echo "quackchat-$version-x86_64.flatpak" ;;
    esac
}

build_target() {
    case $1 in
        appimage) ./appimage/build.sh ;;
        flatpak)  ./flatpak/build.sh ;;
    esac
}

first=
clone=
cleanup() {
    if [ -n "$first" ]; then
        rm -rf "$first"
    fi
    if [ -n "$clone" ] && [ "$keep" -eq 0 ]; then
        # Written by the container as the invoking user, but tacky's dep
        # trees leave some directories read-only.
        chmod -R u+w "$clone" 2>/dev/null || true
        rm -rf "$clone"
    elif [ -n "$clone" ]; then
        printf '\nrepro-check: clone kept at %s\n' "$clone" >&2
    fi
}
trap cleanup EXIT

# ------------------------------------------------------------------ side one

note "quackchat $version at $commit"
note "SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH ($(date -u -d "@$SOURCE_DATE_EPOCH"))"

first=$(mktemp -d "${TMPDIR:-/tmp}/quack-repro-first.XXXXXX")

for t in $targets; do
    f=$(artifact_for "$t")
    note "side one: building $t"
    build_target "$t"
    [ -f "dist/$f" ] || die "the $t build did not produce dist/$f"
    cp -- "dist/$f" "$first/$f"
done

# ------------------------------------------------------------------ side two

# A clone rather than a copy: it takes only what is committed, and takes it with
# today's mtimes - one of the things being tested. The submodule comes from its
# own remote for the same reason; a local copy would carry this machine's build
# leftovers.
clone=$(mktemp -d "${TMPDIR:-/tmp}/quack-repro.XXXXXX")
note "side two: cloning $commit into $clone"
git clone --quiet "$root" "$clone/quack_qml"
git -C "$clone/quack_qml" checkout --quiet --detach "$commit"
git -C "$clone/quack_qml" submodule update --init --recursive --quiet

note "side two: building $targets (cold - tacky's deps compile from scratch)"
for t in $targets; do
    ( cd "$clone/quack_qml" && build_target "$t" )
done

# ------------------------------------------------------------------- compare

rc=0
for t in $targets; do
    f=$(artifact_for "$t")
    a=$first/$f
    b=$clone/quack_qml/dist/$f
    [ -f "$b" ] || die "the second build did not produce $f"

    ha=$(sha256sum < "$a" | cut -d' ' -f1)
    hb=$(sha256sum < "$b" | cut -d' ' -f1)

    if [ "$ha" = "$hb" ]; then
        printf '\nrepro-check: %s reproducible\n  %s\n' "$f" "$ha"
        continue
    fi

    rc=1
    printf '\nrepro-check: %s DIFFERS\n  side one: %s (%s bytes)\n  side two: %s (%s bytes)\n' \
        "$f" "$ha" "$(stat -c %s "$a")" "$hb" "$(stat -c %s "$b")" >&2

    if command -v diffoscope >/dev/null 2>&1; then
        report=$root/dist/logs/repro-$t.html
        mkdir -p "$(dirname "$report")"
        printf 'repro-check: writing %s\n' "$report" >&2
        # diffoscope exits nonzero because there is a difference, which is
        # expected here and must not end the loop.
        diffoscope --html "$report" --max-report-size 20000000 "$a" "$b" >/dev/null 2>&1 || true
    else
        printf 'repro-check: install diffoscope to see what differs. The\n' >&2
        printf '  first differing byte offsets:\n' >&2
        cmp -l -- "$a" "$b" 2>/dev/null | head -20 >&2 || true
    fi
done

exit $rc
