# quackchat

Qt gui around [tacky](https://github.com/pounceandmiss/tacky).

## Getting the source

tacky is a submodule:

    git clone --recurse-submodules https://github.com/pounceandmiss/quack_qml.git

After a plain clone, or when the pin moves:

    git submodule update --init --recursive

`--recursive` matters: tacky carries zippy, its build system, as a submodule of
its own.

There are two ways to build from here, and they need entirely different things.
The packaging builds - the AppImage, the Windows package, the apk and the
Flatpak - each run in a pinned container that carries its own toolchain, so they
need almost nothing installed. A host build uses the Qt and the compiler this
machine already has. It is the quick one, and the only one that builds the
tests. They are described in that order below.

## What you need to build the packages

`docker`, and `flatpak-builder` for the Flatpak. That is the whole list: no Qt
kit, no MinGW, no Android SDK, etc etc, and nothing to configure. Each target
builds in a pinned container that carries its own toolchain, so the artifacts do
not depend on this machine and anyone with docker can produce them.

## Building every package at once

    ./release.sh [--check] [<version>] [target...]

builds every artifact into `dist/` and writes a `SHA256SUMS` over them. Targets
are `windows`, `android`, `flatpak` and `appimage` - all four run when none are
named. Each leg is logged to `dist/logs/<target>.log` as well as the terminal.

`<version>` is checked rather than applied: the version lives in
`project(quack_qml VERSION)`, which is what CPack, the Android version code and
the AppImage all read, so naming it here only asserts that the bump happened.

Everything the selected targets need is checked before the first one builds -
`--check` runs just that and stops, which is how to find out whether this
machine can make a release without waiting for one.

The per-target sections below are for building one on its own. `release.sh`
calls exactly those scripts.

## How the build containers work

    docker/run.sh <profile> <command...>

builds the image named by `<profile>` and runs the command with the checkout
bind-mounted at `/src` as the invoking user, so nothing lands root-owned. There
are two profiles: `common` for the AppImage and the Windows cross-build, and
`android`, which derives from it and adds an NDK and an SDK.

The per-target scripts below call it, so `./appimage/build.sh` is the normal way
in. `docker/run.sh common bash` gives a shell in the same toolchain, which is how
to work out why a build behaves differently there than here. The Dockerfiles say
why the two are split the way they are.

## The Linux AppImage

A single portable binary needing no Qt on the machine that runs it:

    ./appimage/build.sh

The result is `dist/quackchat-<version>-x86_64.AppImage`, built in Rocky 9 for
its glibc 2.34 - the floor Qt's own binaries set - so it runs on Ubuntu 22.04,
Debian 12, RHEL 9 and newer. It carries Qt and tacky, and leaves the graphics
stack to the host: libGL, libEGL, libxkbcommon, fontconfig and dbus.

`--clean` rebuilds the app but keeps tacky's deps which takes long to compile -
delete `build-appimage/` for those too. `--no-aot` skips the ahead-of-time QML
compile, which dominates the build.

Calls run on tacky's rtc backend unless the AppImage carries the webrtc one,
built from the rtc-webrtc repo:

    QUACK_RUN_MOUNTS=$HOME/dev/tacky_calls/rtc-webrtc:/webrtc:ro \
    QUACK_WEBRTC_SRC=/webrtc ./appimage/build.sh

Pick the backend in Preferences, or with `--media-backend`.

    ./appimage/smoke-test.sh

runs it on a clean Ubuntu container under a virtual X server, the only real
check that it works without Qt installed.

## The Windows package

Cross-built from Linux, with no Windows host anywhere in the picture:

    ./windows/build.sh

leaving `dist/quackchat-<version>-win64.zip` and the NSIS installer beside it.
Both come from one staged tree: the installer writes under Program Files and so
asks for administrator, and the ZIP is what someone without it unpacks and runs
in place. `--no-installer` skips NSIS. `--clean` drops the app's half of
`build-win/` and keeps tacky's dependencies.

NSIS is not packaged for EL9 at all - not in the vault repos and not in EPEL -
so `docker/common.Dockerfile` builds it from source, along with the scons that
builds it. That is more pinning than a distro package gives, not less: the
compiler and the prebuilt stubs are each fixed by checksum.

## The Android apk

    ./android/build.sh

leaves `dist/quackchat-<version>-unsigned.apk`. Android installs nothing
unsigned, so that file is an input to signing rather than something to hand out.
`release.sh` signs it with the key named in `release.env`. `--debug` builds the
debug-signed apk instead, which does install - for testing only, since nothing
the debug key signed can ever be published.

Signing happens in the container too, when `QUACK_ANDROID_KEYSTORE` and
`QUACK_ANDROID_KEY_ALIAS` are set - `release.sh` reads them from `release.env`.
The keystore is bind-mounted read-only, never copied into the checkout or into
an image, and the password is passed by variable name rather than value so it
stays out of the process table. Left unset, `apksigner` prompts.

The image carries its own NDK, so tacky's Android targets are driven with
`ANDROID_DOCKER=0`. Left at the default they would start a second container from
inside this one. `docker/android.Dockerfile` pins the NDK, the JDK and the
command-line tools by checksum, but what `sdkmanager` fetches cannot be pinned -
those packages publish none - and the versions it installs have to satisfy the
Android Gradle Plugin that Qt chose, not one we pick.

## The Flatpak

    ./flatpak/build.sh

leaves `dist/quackchat-<version>-x86_64.flatpak`, after checking both pins and
dating the build by the commit rather than by the manifest's mtime.
flatpak-builder cannot read a submodule, so it builds tacky from the commit
pinned in the manifest instead. tacky and its dependencies are fetched from
pinned sources and built with no network of their own.

While working on the app, the form that installs what it builds is more useful:

    cd flatpak
    flatpak-builder --user --ccache --force-clean --install \
        build-dir io.github.pounceandmiss.Quack.yml

## Reproducible builds

The AppImage is reproducible: the same commit gives the same bytes, so a binary
from a release page is one anybody can rebuild and check against the source. The
clock and the checkout's file mtimes are replaced by the commit date, the uid and
the linker are fixed rather than inherited from the build host, and the whole
toolchain is pinned by digest and checksum in `docker/common.Dockerfile`.

Checked from a fresh clone rather than a rebuild in place: a rebuild reuses its
build tree and can agree with itself for reasons that would not survive
somebody else's checkout.

    tools/repro-check.sh

builds a second copy from a fresh clone of HEAD and compares. `diffoscope` is
worth having installed before a mismatch turns up. Both builds run on one
machine, so it catches the clock, the path and the mtimes. Only a rebuild
somewhere else proves the rest.

The other three are not held to it. All of them now build from the same pinned
toolchain, so the ingredients are fixed, but nothing checks that their output
settles: `tools/repro-check.sh flatpak` would settle the Flatpak, whose remaining
unknown is the KDE sdk it builds against - a binary runtime nobody here compiles,
recorded in `flatpak/runtime.pin` rather than built. Windows and Android have no
such check at all, and the apk in particular goes through gradle, which would
have to be made deterministic first.

## What a host build needs

* Qt 6.10 or newer: Core, Gui, Network, Qml, Quick, QuickControls2,
  QuickDialogs2, Test and LinguistTools. On Linux, Qt6 DBus carries the desktop
  notifications if it is there. A floor, not a preference: ChatListFilter uses
  endFilterChange() (6.10) and the QML uses the SafeArea attached type (6.9).
* CMake 3.21+, Ninja, and a C++17 compiler.
* For tacky: a POSIX toolchain and `make`. Its own dependencies (Tcl, mbedTLS,
  libdatachannel, opus and the rest) are downloaded and built by its makefile
  at pinned versions, which costs a couple of gigabytes and a long first
  build. Later builds reuse it.

`ccache` and `mold` are picked up when installed. `-DQUACK_FAST_LINKER=OFF`
leaves mold alone, which is what the release builds do so that their output does
not depend on having it.

## Building and testing on the host

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
`dist/libtacky.a` under it. A `dist/libtacky_webrtc.so` there (tacky's
`make webrtc-so`) is staged next to the binary. `cmake --build build --target tacky-lib` re-runs
tacky's make without leaving the build tree, which is the short way round after
moving the pin.

Debug builds compile QML to bytecode and Release builds compile it ahead of
time to C++, trading build time for startup and binding speed. Override with
`-DQUACK_QML_AOT=ON|OFF`.

## Building a package without the containers

Both cross builds can be driven against a host toolchain instead, which is what
this repository did before the containers. Nothing below is reproducible - the
output depends on which compiler, kit and linker the machine happens to carry,
which is the reason the containers exist - so it is for working on the packaging
itself rather than for producing anything to hand out.

Windows wants a MinGW Qt kit, `wine`, `makensis` and a native tclsh 9.0
installed:

    make -C third_party/tacky win-lib
    cmake -B build-win -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake \
        -DCMAKE_PREFIX_PATH=<qt>/mingw_64 \
        -DQT_HOST_PATH=<qt>/gcc_64
    cmake --build build-win

`cpack -G ZIP` or `cpack -G NSIS` from the build directory packages it, with
`windeployqt` running under wine. NSIS is only needed for the installer.

Android wants the SDK, NDK r29, JDK 21 and the Android Qt kit:

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
