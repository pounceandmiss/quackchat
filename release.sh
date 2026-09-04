#!/usr/bin/env bash
# Build the release artifacts into dist/.
#
#   ./release.sh [--check] [--clean] [--allow-dirty] [--allow-debug-key] \
#                [<version>] [target...]
#
# Targets are `windows`, `android`, `flatpak` and `appimage`; all four run when
# none are named. Each leaves one or two files in dist/, and a SHA256SUMS over
# them is rewritten at the end. Nothing is uploaded - the release page is a
# separate, deliberate step.
#
# <version> is optional and checked rather than applied: the version lives in
# project(quack_qml VERSION), which is what CPack, the Android version code and
# the AppImage all read, so naming it here only asserts that the bump happened.
#
# Nothing here needs a toolchain installed: Windows, Android and the AppImage
# each build in a pinned container, and the Flatpak against a pinned runtime.
# The only host requirements are docker, and flatpak-builder for the Flatpak.
#
# What release.env still carries - untracked, made by copying
# release.env.sample - is the Android signing key, which cannot live in an image
# that gets shared and is the one input nobody else's copy of this repository
# should have.
#
# Everything each selected target needs is checked before the first one builds:
# the shortest leg here is minutes and the Flatpak is closer to an hour, so a
# missing keystore must not surface after three targets have already run.
#
# --check runs that preflight and stops, which is how to find out whether this
# machine can build a release without waiting for one.
#
# Per-target build trees (build-win/, build-android/, build-appimage/) persist
# and rebuild incrementally. --clean drops them first.
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
cd "$root"

all_targets="windows android flatpak appimage"
logdir=dist/logs

usage() {
    sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; $d'
}

die() { printf 'release.sh: %s\n' "$*" >&2; exit 1; }
note() { printf '\n== %s\n' "$*" >&2; }

# Everything long-running is logged to a file as well as the terminal, so a
# failure forty minutes in is still readable afterwards. pipefail is what makes
# that safe: without it tee's exit status stands in for the build's, and a
# failed build reports success.
run_logged() {
    local name=$1; shift
    note "$name  (log: $logdir/$name.log)"
    "$@" 2>&1 | tee "$logdir/$name.log"
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "$1 is not on PATH; needed for $2"
}

# Named rather than guessed. A default that points at one machine's layout is
# worse than an error: it half-works elsewhere and packages the wrong thing.
need_path() {
    local var=$1 val=$2 what=$3
    [ -n "$val" ] || die "$var is unset - needed for $what (see release.env.sample)"
    [ -e "$val" ] || die "$var=$val does not exist - needed for $what"
}

clean=0
allow_dirty=0
allow_debug_key=0
check_only=0
version=
targets=

while [ "$#" -gt 0 ]; do
    case $1 in
        --check)           check_only=1 ;;
        --clean)           clean=1 ;;
        --allow-dirty)     allow_dirty=1 ;;
        --allow-debug-key) allow_debug_key=1 ;;
        -h|--help)         usage; exit 0 ;;
        -*)                die "unknown option $1" ;;
        [0-9]*)            version=$1 ;;
        windows|android|flatpak|appimage) targets="$targets $1" ;;
        *)                 die "unknown target $1 (want one of: $all_targets)" ;;
    esac
    shift
done
targets=${targets# }
[ -n "$targets" ] || targets=$all_targets

# shellcheck source=/dev/null
[ -f release.env ] && . ./release.env

# The signing key is the whole of what release.env still carries: every
# toolchain each target needs now lives in that target's container.
: "${QUACK_ANDROID_KEYSTORE:=}"
: "${QUACK_ANDROID_KEY_ALIAS:=}"
: "${QUACK_ANDROID_KEYSTORE_PASS:=}"

selected() { case " $targets " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

# ---------------------------------------------------------------- preflight

preflight() {
    version_declared=$(sed -n 's/^project(quack_qml VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
    [ -n "$version_declared" ] || die "no project(quack_qml VERSION ...) in CMakeLists.txt"
    if [ -n "$version" ] && [ "$version" != "$version_declared" ]; then
        die "asked for $version but CMakeLists.txt declares $version_declared; bump project(VERSION) first"
    fi
    version=$version_declared

    # --ignore-submodules=dirty because the tacky archives below are built
    # inside third_party/tacky and leave object trees there. A moved submodule
    # *commit* is a different thing and still fails this.
    if [ "$allow_dirty" -eq 0 ]; then
        local dirty
        dirty=$(git status --porcelain --ignore-submodules=dirty)
        [ -z "$dirty" ] || die "the tree has uncommitted changes; commit them or pass --allow-dirty:
$dirty"
    fi

    # A warning, not an error: most runs of this script are rehearsals, and the
    # tag usually lands once the artifacts are known good.
    if ! git describe --exact-match --tags HEAD >/dev/null 2>&1; then
        note "HEAD is not tagged; the release page will want a v$version tag"
    fi

    [ -e third_party/tacky/embed/tacky.h ] ||
        die "third_party/tacky is empty; run  git submodule update --init --recursive"

    if selected windows; then
        # MinGW, the Qt kit, wine, NSIS and a native tclsh 9.0 all live in the
        # image (docker/common.Dockerfile), so there is nothing to check for
        # on the host but docker itself.
        need_cmd docker "the Windows build, which builds entirely in its own image"
    fi

    if selected android; then
        # The SDK, NDK r29, JDK 21 and the Qt kit are all in the image
        # (docker/android.Dockerfile), which is also where the version
        # constraints that used to be checked here now live: an r29 NDK because
        # libtacky-android.a is built against it, and a JDK 21 because gradle's
        # jlink step breaks on newer ones.
        need_cmd docker "the Android build, which builds entirely in its own image"
        preflight_signing
    fi

    if selected flatpak; then
        need_cmd flatpak-builder "the Flatpak"
        need_cmd flatpak "the Flatpak bundle"
        flatpak info org.kde.Sdk//6.11 >/dev/null 2>&1 ||
            die "org.kde.Sdk//6.11 is not installed; flatpak install flathub org.kde.Sdk//6.11"
        # The manifest names tacky's commit a second time, because
        # flatpak-builder cannot read a submodule. Disagreeing means shipping an
        # archive nothing was tested against, and it is an hour into the build
        # before anything else would notice.
        ./flatpak/check-pin.sh
        # The sdk commit, which the manifest cannot name. flatpak/build.sh
        # checks this too; doing it here means it fails in seconds rather than
        # after the other three targets have built.
        ./flatpak/check-runtime-pin.sh
    fi

    if selected appimage; then
        need_cmd docker "the AppImage, which builds entirely in its own image"
    fi
}

# Which key signs the apk is settled up front, because it is the one decision
# here that cannot be revisited: Android identifies an app by its signing
# certificate, so a version signed by a different key than the last cannot be
# installed over it - the user has to uninstall, losing the tacky store.
preflight_signing() {
    if [ -n "$QUACK_ANDROID_KEYSTORE" ]; then
        need_path QUACK_ANDROID_KEYSTORE "$QUACK_ANDROID_KEYSTORE" "signing the apk"
        [ -n "$QUACK_ANDROID_KEY_ALIAS" ] ||
            die "QUACK_ANDROID_KEY_ALIAS is unset but QUACK_ANDROID_KEYSTORE is set"
        return
    fi
    # gradle makes and uses its own debug key inside the container, so there is
    # no host keystore to check for here - only that this was asked for.
    if [ "$allow_debug_key" -eq 1 ]; then
        return
    fi
    die "no release keystore. Every future version must be signed by the same key
as the first one published, so make one now and keep it somewhere it cannot be
lost:

    keytool -genkeypair -v -keystore <path>.jks -alias quackchat \\
        -keyalg RSA -keysize 4096 -validity 10000

then set QUACK_ANDROID_KEYSTORE and QUACK_ANDROID_KEY_ALIAS in release.env.
--allow-debug-key builds a debug-signed apk for testing, which must not be the
one a release page hands out."
}

# ------------------------------------------------------------------ targets

build_windows() {
    local args=()
    if [ "$clean" -eq 1 ]; then args=(--clean); fi
    # Writes dist/quackchat-<version>-win64.{zip,exe} itself.
    run_logged windows ./windows/build.sh "${args[@]+"${args[@]}"}"
}

build_android() {
    local args=()
    if [ "$clean" -eq 1 ]; then args=(--clean); fi
    # --debug builds the debug-signed apk gradle can sign by itself, which is
    # what --allow-debug-key asks for. Otherwise android/build.sh signs with the
    # keystore named below, mounted into the container read-only.
    if [ -z "$QUACK_ANDROID_KEYSTORE" ]; then args+=(--debug); fi
    # Writes dist/quackchat-<version>.apk itself, or -debug.apk for the above.
    run_logged android env \
        QUACK_ANDROID_KEYSTORE="$QUACK_ANDROID_KEYSTORE" \
        QUACK_ANDROID_KEY_ALIAS="$QUACK_ANDROID_KEY_ALIAS" \
        ./android/build.sh "${args[@]+"${args[@]}"}"
}

build_flatpak() {
    # Writes dist/quackchat-<version>-x86_64.flatpak itself, after checking both
    # pins and dating the build by the commit rather than the manifest's mtime.
    run_logged flatpak ./flatpak/build.sh
}

build_appimage() {
    local args=()
    if [ "$clean" -eq 1 ]; then args=(--clean); fi
    # Writes dist/quackchat-<version>-x86_64.AppImage itself.
    run_logged appimage ./appimage/build.sh "${args[@]+"${args[@]}"}"
}

# -------------------------------------------------------------------- drive

mkdir -p "$logdir"
preflight

if [ "$check_only" -eq 1 ]; then
    note "quackchat $version: $targets - everything needed is in place"
    exit 0
fi

note "quackchat $version: $targets"
for t in $targets; do
    "build_$t"
done

# Only this version's files, so a dist/ that still holds the last release does
# not end up vouched for by this one's checksums.
( cd dist && find . -maxdepth 1 -type f -name "quackchat-$version*" -printf '%P\n' |
    sort | xargs -r sha256sum > SHA256SUMS )

note "dist/"
ls -lh dist/ | grep -v '^total'
printf '\nUpload these to the release page for v%s.\n' "$version" >&2
