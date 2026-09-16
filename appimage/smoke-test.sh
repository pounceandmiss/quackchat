#!/usr/bin/env bash
# Run the built AppImage on a distro other than the one it was built in.
#
#   appimage/smoke-test.sh [image ...]     (default: ubuntu:22.04)
#
# This is the check the build cannot make about itself: that the app starts on a
# machine with no Qt - that the bundled platform plugin loads, that QML resolves,
# and that the graphics stack linuxdeploy deliberately leaves to the host is
# satisfied. The image starts clean, gets the packages below and nothing else,
# and runs the AppImage under a virtual X server.
#
# Not the glibc floor: appimage/in-image.sh checks that statically against the
# AppDir, and every image runnable here is newer than the 2.34 the build sits on,
# so none could fail for that reason. One image rather than several for the same
# reason - a second apt distro tests nearly the same thing. Ubuntu 22.04 is the
# one kept because it is the oldest the README claims, which makes its *host*
# libraries the binding case, not just its libc.
#
# The dependency install below is apt, so an image named here has to be
# Debian-family. The README's RHEL 9 claim is not testable this way.
#
# Still running when the timeout fires is a pass. A Qt that cannot find its
# platform plugin, or QML that fails to load, exits at once - main.cpp returns
# -1 on an empty rootObjects().
#
# A webrtc backend in the AppImage must also resolve on this distro.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)

images=("$@")
if [ "${#images[@]}" -eq 0 ]; then
    images=(ubuntu:22.04)
fi

shopt -s nullglob
built=("$project"/dist/*.AppImage)
if [ "${#built[@]}" -eq 0 ]; then
    echo "smoke-test: nothing in dist/ - run appimage/build.sh first" >&2
    exit 1
fi
appimage=${built[0]}

# Everything the app needs is inside the AppImage bar the graphics stack, which
# linuxdeploy leaves out on purpose: it has to match the host's driver.
# libOpenGL.so.0 and libGLX.so.0 are packaged separately on Debian and Ubuntu,
# so libgl1 alone leaves the app dying at load, and mesa's dri driver is what
# answers for GL where there is no GPU - which is every image here.
#
# xauth is xvfb-run's own, not the app's: without it xvfb-run exits 3 before the
# app is ever started.
runtime_deps="libgl1 libopengl0 libglx0 libegl1 libgl1-mesa-dri \
libxkbcommon0 libxkbcommon-x11-0 libfontconfig1 libdbus-1-3 xvfb xauth"

status=0
for image in "${images[@]}"; do
    echo "==> $image"
    if docker run --rm \
        -v "$appimage:/tmp/quack.AppImage:ro" \
        -e APPIMAGE_EXTRACT_AND_RUN=1 \
        -e QT_QPA_PLATFORM=xcb \
        "$image" bash -c "
            set -e
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq
            apt-get install -y -qq --no-install-recommends $runtime_deps >/dev/null
            echo '--- glibc:' \$(ldd --version | head -1)
            set +e
            timeout 20 xvfb-run -a /tmp/quack.AppImage 2>&1 | tail -20
            rc=\${PIPESTATUS[0]}
            echo \"--- exit \$rc\"
            [ \$rc -eq 124 ] || exit 1

            cd /tmp
            /tmp/quack.AppImage --appimage-extract >/dev/null 2>&1 || exit 1
            so=/tmp/squashfs-root/usr/bin/libtacky_webrtc.so
            if [ ! -f \$so ]; then
                echo '--- webrtc backend: not in this AppImage (rtc only)'
                exit 0
            fi
            echo '--- webrtc backend: beside the executable'
            ldd -r \$so 2>&1 | tail -5
            if ldd -r \$so 2>&1 | grep -qE 'not found|undefined symbol'; then
                echo '--- webrtc backend: will not load here'
                exit 1
            fi
            exit 0
        "; then
        echo "==> $image: PASS (still running at the timeout)"
    else
        echo "==> $image: FAIL"
        status=1
    fi
done
exit $status
