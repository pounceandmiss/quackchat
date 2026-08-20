# quackchat

Qt gui around [tacky](https://github.com/pounceandmiss/tacky).

## Getting the source

tacky is a submodule:

    git clone --recurse-submodules https://github.com/pounceandmiss/quack_qml.git

After a plain clone, or when the pin moves:

    git submodule update --init --recursive

`--recursive` matters: tacky carries zippy, its build system, as a submodule of
its own.

## What you need

* Qt 6.8 or newer: Core, Gui, Network, Qml, Quick, QuickControls2,
  QuickDialogs2, Test and LinguistTools. On Linux, Qt6 DBus carries the desktop
  notifications if it is there.
* CMake 3.21+, Ninja, and a C++17 compiler.
* For tacky: a POSIX toolchain and `make`. Its own dependencies (Tcl, mbedTLS,
  libdatachannel, opus and the rest) are downloaded and built by its makefile
  at pinned versions, which costs a couple of gigabytes and a long first
  build. Later builds reuse it.

`ccache` and `mold` are picked up when installed.

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

Cross-built from Linux against a MinGW Qt kit, no Windows host is involved.
Windows installer is included.

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
        -DANDROID_PLATFORM=android-30 \
        -DCMAKE_BUILD_TYPE=Debug
    JAVA_HOME=<jdk21> cmake --build build-android --target apk

NDK r29 and `ANDROID_PLATFORM=android-30` are what the tacky archive is built
against: older NDKs lack `__cxa_init_primary_exception`, and API levels below
30 lack `pthread_cond_clockwait`.

JDK 21 has to be `JAVA_HOME` for the `apk` target: newer JDKs break gradle's
jlink step.

`CMAKE_BUILD_TYPE` is worth passing because `qt-cmake` leaves it empty, and an
empty build type compiles with no `-O` flags yet still packages as a release.
Debug is the one that installs: gradle signs it with the machine's debug
keystore. Release leaves the apk unsigned - the signing key is the packager's to
supply, and Android takes nothing unsigned - so for a release build that still
goes on a device, add `-DQT_ANDROID_DEPLOYMENT_TYPE=Debug`. Nothing the debug
key signed can be published.

## Flatpak

    cd flatpak
    flatpak-builder --user --ccache --force-clean --install \
        build-dir io.github.pounceandmiss.Quack.yml

flatpak-builder cannot read a submodule, so it builds tacky from the commit
pinned in the manifest instead. tacky and its dependencies are fetched from
pinned sources and built with no network of their own.

## AppImage

A single portable binary needing no Qt on the machine that runs it. Docker is
the only thing the host has to have:

    ./appimage/build.sh

The result is `dist/quackchat-<version>-x86_64.AppImage`, built in Rocky 9 for
its glibc 2.34 - the floor Qt's own binaries set - so it runs on Ubuntu 22.04,
Debian 12, RHEL 9 and newer. It carries Qt and tacky, and leaves the graphics
stack to the host: libGL, libEGL, libxkbcommon, fontconfig and dbus.

`--clean` rebuilds the app but keeps tacky's deps which takes long to compile -
delete `build-appimage/` for those too. `--no-aot` skips the ahead-of-time QML
compile, which dominates the build.

    ./appimage/smoke-test.sh

runs it on clean Ubuntu and Debian containers under a virtual X server, the only
real check that it works without Qt installed.

## Moving the tacky pin

    git -C third_party/tacky fetch origin
    git -C third_party/tacky checkout <commit>
    git add third_party/tacky
    ./flatpak/check-pin.sh --sync

The last line carries the commit into the manifest, which names it a second
time. `ctest` fails while the two disagree. If tacky's own dependency pins
moved, the manifest says how to regenerate its source list.

## Translations

Every string on screen goes through `qsTr()` or `tr()`, and the catalogues are
compiled into the binary. To add a language, list it in
`qt_standard_project_setup`:

    I18N_TRANSLATED_LANGUAGES de fr

then hand the catalogue it generates to a translator:

    cmake --build build --target update_translations

That target rewrites every `i18n/*.ts` from the sources, leaving finished
translations alone, so it is also what to run after changing a string.

The Android notification service does not read any of this: its strings are
Android resources, translated by adding `android/res/values-<lang>/strings.xml`
beside the existing `values/strings.xml`.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
