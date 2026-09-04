#!/usr/bin/env bash
# Run a build command inside one of the pinned toolchain containers.
#
#   docker/run.sh <profile> <command...>
#
# <profile> names an image, docker/<profile>.Dockerfile:
#
#   common    the AppImage and the Windows cross-build
#   android   the same plus an NDK and an SDK, derived from common
#
# The image is built as quack-build:<profile>, preceded by common when the
# profile's Dockerfile says FROM quack-build:common. Layer caching makes both
# near-instant once neither has moved.
#
# The checkout is bind-mounted at /src and the command runs there as the
# invoking user, so nothing lands root-owned and the build tree stays where a
# host-native build would leave it. /src is also why build paths need no
# -ffile-prefix-map of their own: the tree is at the same path on every machine,
# which is otherwise the commonest reason two builds of one commit differ.
#
# SOURCE_DATE_EPOCH and every QUACK_* variable in the environment are passed
# through, which is how the per-target build.sh scripts hand their knobs to the
# in-image half without this script knowing what any of them mean. Anything a
# target needs mounted beyond the checkout goes in QUACK_RUN_MOUNTS.
#
# Examples:
#   docker/run.sh common appimage/in-image.sh
#   docker/run.sh common bash          # an interactive toolchain shell
set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: docker/run.sh <profile> <command...>" >&2
    exit 2
fi

profile=$1
shift

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)
dockerfile=$here/$profile.Dockerfile

if [ ! -f "$dockerfile" ]; then
    echo "docker/run.sh: no such profile '$profile' ($dockerfile)" >&2
    echo "available:" >&2
    ls -1 "$here"/*.Dockerfile 2>/dev/null \
        | sed 's#.*/##; s#\.Dockerfile$##' | sed 's/^/  /' >&2
    exit 2
fi

command -v docker >/dev/null 2>&1 || {
    echo "docker/run.sh: docker is not on PATH" >&2
    exit 1
}

if [ ! -e "$project/third_party/tacky/embed/tacky.h" ]; then
    echo "docker/run.sh: third_party/tacky is empty." >&2
    echo "    git submodule update --init --recursive third_party/tacky" >&2
    exit 1
fi

# Build an image, unless the one already here is the one its Dockerfile
# describes. The condition matters: docker stamps a fresh config on every export,
# so rebuilding gives an image a new digest even when every layer was cached, and
# a derived image's FROM resolves to that digest - so rebuilding unconditionally
# invalidates everything below it every run, `aqt install-qt` and its two
# gigabytes included. The Dockerfile's hash rides along as a label, so
# the check needs nothing kept outside docker, and a runner that pulled a
# prebuilt image and tagged it skips the build entirely.
#
# Build chatter to stderr, leaving stdout to the build's own progress.
build_image() {
    local tag=$1 file=$2 want have
    want=$(sha256sum "$file" | cut -d' ' -f1)
    have=$(docker image inspect -f '{{index .Config.Labels "quack.dockerfile"}}' \
        "$tag" 2>/dev/null || true)
    [ "$want" = "$have" ] && return 0
    DOCKER_BUILDKIT=1 docker build -t "$tag" \
        --label "quack.dockerfile=$want" -f "$file" "$here" >&2
}

# The common image first, when this profile derives from it: docker resolves a
# FROM against images it already has and will not go and build that itself.
if grep -q '^FROM quack-build:common' "$dockerfile"; then
    build_image quack-build:common "$here/common.Dockerfile"
fi
build_image "quack-build:$profile" "$dockerfile"

# -e NAME with no value passes the host's through. Names only: a value on the
# command line is readable from /proc for as long as docker runs, and one of
# these carries the signing password.
env_args=()
for name in $(compgen -e); do
    case $name in
        SOURCE_DATE_EPOCH|QUACK_*) env_args+=(-e "$name") ;;
    esac
done

# Extra bind mounts, space-separated "host:container[:opts]" entries. The one
# thing this exists for is the Android signing key: it cannot live in an image
# that gets shared, and copying it into the checkout to get it under /src would
# be worse. Mounted read-only by the caller that asks for it.
mount_args=()
for m in ${QUACK_RUN_MOUNTS-}; do
    mount_args+=(-v "$m")
done

# Interactive only when attached to a real terminal, so piped and CI use are
# unaffected but `docker/run.sh common bash` still gives a usable shell.
tty_flags=()
if [ -t 0 ] && [ -t 1 ]; then
    tty_flags=(-t -i)
fi

# The invoking uid has no passwd entry in the image, so HOME has to be named
# outright or every tool that looks one up falls over.
exec docker run --rm "${tty_flags[@]}" \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    "${env_args[@]}" \
    -v "$project:/src" \
    "${mount_args[@]+"${mount_args[@]}"}" \
    -w /src \
    "quack-build:$profile" "$@"
