#!/bin/sh
# The tacky commit is named twice, by the third_party/tacky submodule and by the
# manifest, since flatpak-builder builds tacky from its own git source and
# cannot read a submodule. They have to agree, or the Flatpak ships an archive
# nothing was tested against. ctest runs this; --sync writes the submodule's
# HEAD into the manifest, which is what to do after moving the pin.
set -eu

sync=0
case "${1-}" in
    --sync) sync=1 ;;
    "") ;;
    *) echo "usage: check-pin.sh [--sync]" >&2; exit 2 ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
manifest="$root/flatpak/io.github.pounceandmiss.Quack.yml"

pinned=$(awk '
    $1 == "url:" && $2 == "https://github.com/pounceandmiss/tacky.git" { want = 1; next }
    want && $1 == "commit:" { print $2; exit }
    want { want = 0 }
' "$manifest")

if [ -z "$pinned" ]; then
    echo "check-pin: no pinned tacky commit in $manifest" >&2
    echo "check-pin: a branch: line instead of commit: makes the build unreproducible" >&2
    exit 1
fi

if [ ! -e "$root/third_party/tacky/.git" ]; then
    echo "check-pin: third_party/tacky is empty" >&2
    echo "check-pin: run  git submodule update --init --recursive" >&2
    exit 1
fi

checked_out=$(git -C "$root/third_party/tacky" rev-parse HEAD)

if [ "$pinned" = "$checked_out" ]; then
    echo "check-pin: tacky pinned at $pinned"
    exit 0
fi

if [ "$sync" -eq 0 ]; then
    echo "check-pin: the manifest and the submodule disagree about tacky" >&2
    echo "  manifest:  $pinned" >&2
    echo "  submodule: $checked_out" >&2
    echo "check-pin: run  ./flatpak/check-pin.sh --sync" >&2
    exit 1
fi

# Rewritten in place rather than by sed over the whole file: the same commit:
# key appears under every other pinned source, and only tacky's own follows its
# url.
tmp="$manifest.new"
awk -v commit="$checked_out" '
    $1 == "url:" && $2 == "https://github.com/pounceandmiss/tacky.git" { want = 1; print; next }
    want && $1 == "commit:" {
        match($0, /^[ \t]*/)
        printf "%scommit: %s\n", substr($0, 1, RLENGTH), commit
        want = 0
        next
    }
    want { want = 0 }
    { print }
' "$manifest" > "$tmp"
mv -- "$tmp" "$manifest"

echo "check-pin: manifest moved from $pinned to $checked_out"
