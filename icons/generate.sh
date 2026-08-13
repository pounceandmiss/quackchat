#!/usr/bin/env bash
# Regenerates every raster from the two SVGs beside this script; run by hand and
# commit the result. At build time it would put rsvg-convert and ImageMagick on
# the critical path of the Android and Windows cross builds.
#
# The figure is anotherim-desktop's, GPL-3.0, recoloured.
#
# Needs rsvg-convert (librsvg) and magick (ImageMagick 7).
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(dirname "$here")

for tool in rsvg-convert magick; do
    command -v "$tool" >/dev/null ||
        { echo "$0: need $tool on PATH" >&2; exit 1; }
done

render() { # svg size out
    mkdir -p "$(dirname "$3")"
    rsvg-convert -w "$2" -h "$2" "$1" -o "$3"
}

for size in 48 128 256; do
    render "$here/quack.svg" "$size" "$here/quack-$size.png"
done

# Rasterised per size rather than resampled from one master; the small frames
# need it.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
frames=()
for size in 16 24 32 48 64 128 256; do
    render "$here/quack.svg" "$size" "$tmp/$size.png"
    frames+=("$tmp/$size.png")
done
magick "${frames[@]}" "$here/quack.ico"

# A 108dp adaptive layer at each density's scale factor.
declare -A densities=([mdpi]=108 [hdpi]=162 [xhdpi]=216 [xxhdpi]=324 [xxxhdpi]=432)
for density in "${!densities[@]}"; do
    render "$here/quack-foreground.svg" "${densities[$density]}" \
        "$root/android/res/drawable-$density/ic_launcher_foreground.png"
done

echo "regenerated from quack.svg and quack-foreground.svg"
