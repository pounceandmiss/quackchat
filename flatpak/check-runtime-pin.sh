#!/bin/sh
# The KDE runtime is the one build input the Flatpak does not compile from
# pinned sources: the manifest names a branch, and whatever commit that branch
# points at supplies Qt, gcc and the rest. A manifest has no key for a commit,
# so it is recorded in flatpak/runtime.pin instead and this holds the installed
# runtime to it. flatpak/build.sh checks it before building.
#
#   check-runtime-pin.sh          verify
#   check-runtime-pin.sh --sync   record what is installed, after retesting
#
# To go the other way and reproduce an older release, install its commits rather
# than the branch tip:
#
#   flatpak update --commit=<commit> org.kde.Sdk//<branch>
set -eu

case "${1-}" in
    ""|--sync) ;;
    *) echo "usage: check-runtime-pin.sh [--sync]" >&2; exit 2 ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pin=$root/flatpak/runtime.pin

# From the manifest rather than a copy here, which could drift from it.
branch=$(sed -n "s/^runtime-version: *'\{0,1\}\([^']*\)'\{0,1\}.*/\1/p" \
    "$root/flatpak/io.github.pounceandmiss.Quack.yml")
[ -n "$branch" ] || { echo "check-runtime-pin: no runtime-version in the manifest" >&2; exit 1; }

# The sdk builds the app and the platform ships under it, so both are recorded:
# a bundle made against one platform commit is a different artifact from one made
# against another, even when the sdk has not moved.
installed=$(
    printf 'branch %s\n' "$branch"
    for app in org.kde.Sdk org.kde.Platform; do
        commit=$(flatpak info --show-commit "$app//$branch" 2>/dev/null) || exit 1
        printf '%s %s\n' "$app" "$commit"
    done
) || {
    echo "check-runtime-pin: org.kde.Sdk//$branch and org.kde.Platform//$branch must both be installed" >&2
    echo "    flatpak install flathub org.kde.Sdk//$branch org.kde.Platform//$branch" >&2
    exit 1
}

if [ "${1-}" = "--sync" ]; then
    { echo "# The KDE runtime commits the Flatpak was last built and tested against."
      echo "# Written by flatpak/check-runtime-pin.sh --sync."
      echo "$installed"
    } > "$pin"
    echo "check-runtime-pin: recorded"
    echo "$installed" | sed 's/^/  /'
    exit 0
fi

[ -f "$pin" ] || { echo "check-runtime-pin: no $pin; run --sync" >&2; exit 1; }

if [ "$(grep -v '^#' "$pin")" != "$installed" ]; then
    echo "check-runtime-pin: the installed runtime does not match $pin" >&2
    echo "  pinned:" >&2;    grep -v '^#' "$pin" | sed 's/^/    /' >&2
    echo "  installed:" >&2; echo "$installed" | sed 's/^/    /' >&2
    echo "check-runtime-pin: install the pinned commits, or --sync after rebuilding" >&2
    exit 1
fi

echo "check-runtime-pin: the installed runtime matches the pin ($branch)"
