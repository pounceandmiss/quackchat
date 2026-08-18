# quackchat

A Qt Quick chat client for XMPP, with OMEMO encryption, group chats, file
sharing and voice calls.

The protocol work is [tacky](https://github.com/pounceandmiss/tacky)'s, linked
in as a static archive, so one process is the whole application. This
repository is the GUI; anything that is not presentation belongs in tacky.

## Getting the source

tacky is a submodule, pinned at the commit this GUI is built and tested
against:

    git clone --recurse-submodules https://github.com/pounceandmiss/quack_qml.git

After a plain clone, or when the pin moves:

    git submodule update --init --recursive

`--recursive` matters: tacky carries zippy, its build system, as a submodule of
its own, and without it `make lib` stops at a missing `zippy/zippy.mk`.

## What you need

* Qt 6.5 or newer: Core, Gui, Network, Qml, Quick, QuickControls2,
  QuickDialogs2 and Test. On Linux, Qt6 DBus carries the desktop notifications
  if it is there; without it the app builds and runs the same, just silently.
* CMake 3.21+, Ninja, and a C++17 compiler.
* For tacky: a POSIX toolchain and `make`. Its own dependencies (Tcl, mbedTLS,
  libdatachannel, opus and the rest) are downloaded and built by its makefile
  at pinned versions, which costs a couple of gigabytes and a long first
  build. Later builds reuse it.

`ccache` and `mold` are picked up when installed. The build is link-bound
rather than compile-bound, since the tests link one binary per suite.

## Building

tacky is a separate build system and nothing here drives it, so its archive
comes first:

    make -C third_party/tacky lib

Then the app:

    cmake --preset default
    cmake --build build
    ctest --preset default

The binary is `build/quackchat`.

CMake finds `third_party/tacky` on its own. A checkout elsewhere is named with
`-DTACKY_ROOT=<dir>` or `$TACKY_ROOT`, and wants both `embed/tacky.h` and
`dist/libtacky.a` under it. `cmake --build build --target tacky-lib` re-runs
tacky's make without leaving the build tree, which is the short way round after
moving the pin.

Debug builds compile QML to bytecode and Release builds compile it ahead of
time to C++, trading build time for startup and binding speed. Override with
`-DQUACK_QML_AOT=ON|OFF`.

## Windows

Cross-built from Linux against a MinGW Qt kit; no Windows host is involved,
packaging included.

    make -C third_party/tacky win-lib
    cmake -B build-win -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake \
        -DCMAKE_PREFIX_PATH=<qt>/mingw_64 \
        -DQT_HOST_PATH=<qt>/gcc_64
    cmake --build build-win

`cpack -G ZIP` or `cpack -G NSIS` from the build directory packages it, with
`windeployqt` running under wine. NSIS is only needed for the installer.

## Android

tacky's archive is cross-compiled in Docker, so Docker is needed here:

    make -C third_party/tacky android-lib
    <qt>/android_arm64_v8a/bin/qt-cmake -S . -B build-android -G Ninja \
        -DQT_HOST_PATH=<qt>/gcc_64 \
        -DANDROID_SDK_ROOT=<sdk> \
        -DANDROID_NDK_ROOT=<sdk>/ndk/<r29> \
        -DANDROID_PLATFORM=android-30
    JAVA_HOME=<jdk21> cmake --build build-android --target apk

NDK r29 and `ANDROID_PLATFORM=android-30` are what the tacky archive is built
against: older NDKs lack `__cxa_init_primary_exception`, and API levels below
30 lack `pthread_cond_clockwait`.

JDK 21 has to be `JAVA_HOME` for the `apk` target; newer JDKs break gradle's
jlink step.

`qt-cmake` leaves `CMAKE_BUILD_TYPE` empty and Qt derives the package type from
it, so a Release configure emits an unsigned, non-debuggable apk. Pass
`-DQT_ANDROID_DEPLOYMENT_TYPE=Debug` for anything that will be installed.

## Flatpak

    cd flatpak
    flatpak-builder --user --ccache --force-clean --install \
        build-dir io.github.pounceandmiss.Quack.yml

flatpak-builder cannot read a submodule, so it builds tacky from the commit
pinned in the manifest instead. The two have to agree:

    ./flatpak/check-pin.sh

tacky and its dependencies are fetched from pinned sources and built with no
network of their own.

## Moving the tacky pin

    git -C third_party/tacky fetch origin
    git -C third_party/tacky checkout <commit>
    git add third_party/tacky

Then set the manifest's `commit:` to the same value. If tacky's own dependency
pins moved, regenerate the manifest's source list wholesale rather than editing
it, in a tacky checkout:

    make -f zippy/zippy.mk flatpak-sources FLATPAK_DEPS_DIR=build/deps

and finish with `./flatpak/check-pin.sh`.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
