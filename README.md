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

Two different answers, depending on what you are here for.

**To build and run it**, on this machine, against what the distro gives you:

* Qt 6.8 or newer: Core, Gui, Network, Qml, Quick, QuickControls2,
  QuickDialogs2, Test and LinguistTools. On Linux, Qt6 DBus carries the desktop
  notifications if it is there.
* CMake 3.21+, Ninja, and a C++17 compiler.
* For tacky: a POSIX toolchain and `make`. Its own dependencies (Tcl, mbedTLS,
  libdatachannel, opus and the rest) are downloaded and built by its makefile
  at pinned versions, which costs a couple of gigabytes and a long first
  build. Later builds reuse it.

**To build the release artifacts** - the AppImage, the Windows package, the apk:
just `docker`, plus `flatpak-builder` for the Flatpak. No Qt kit, no MinGW, no
Android SDK or NDK, no JDK, and nothing to configure; each target builds in a
pinned container. See [Build containers](#build-containers).

`ccache` and `mold` are picked up when installed. `-DQUACK_FAST_LINKER=OFF`
leaves mold alone, which is what the release builds do so that their output does
not depend on having it.

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

## Build containers

The packaging targets build in pinned containers rather than against whatever
the host happens to have, so the artifacts do not depend on this machine and
anyone with docker can produce them.

    docker/run.sh <profile> <command...>

builds the image named by `<profile>` and runs the command with the checkout
bind-mounted at `/src` as the invoking user, so nothing lands root-owned. There
are two:

    common    the AppImage and the Windows cross-build
    android   the same, plus an NDK and an SDK - derived from common

The AppImage and the Windows build share one image because they share nearly
everything: the distro, the compiler, tacky's build tools, the native tcl 9.0 and
Qt's Linux kit, which a cross build needs anyway to run moc and rcc as host
tools. Splitting them duplicated all of that for the sake of a MinGW toolchain
and wine.

Android stays separate, and derives from `common` with `FROM`, because its NDK
alone is ~5 GB unpacked - nobody building an AppImage should have to pull it.
`docker/run.sh android` builds `common` first when it needs to.

The per-target scripts below call it, so `./appimage/build.sh` is the normal way
in. `docker/run.sh common bash` gives an interactive shell in the same toolchain,
which is the way to work out why a build behaves differently there than here.

## Windows

Cross-built from Linux, with no Windows host anywhere in the picture. Docker is
the only thing this needs:

    ./windows/build.sh

leaving `dist/quackchat-<version>-win64.zip` and the NSIS installer beside it.
Both come from one staged tree: the installer writes under Program Files and so
asks for administrator, and the ZIP is what someone without it unpacks and runs
in place. `--no-installer` skips NSIS; `--clean` drops the app's half of
`build-win/` and keeps tacky's dependencies.

NSIS is not packaged for EL9 at all - not in the vault repos and not in EPEL -
so `docker/common.Dockerfile` builds it from source, along with the scons that
builds it. That is more pinning than a distro package gives, not less: the
compiler and the prebuilt stubs are each fixed by checksum.

To build against the host's own toolchain instead, which wants a MinGW Qt kit,
`wine`, `makensis` and a native tclsh 9.0 installed:

    make -C third_party/tacky win-lib
    cmake -B build-win -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake \
        -DCMAKE_PREFIX_PATH=<qt>/mingw_64 \
        -DQT_HOST_PATH=<qt>/gcc_64
    cmake --build build-win

`cpack -G ZIP` or `cpack -G NSIS` from the build directory packages it, with
`windeployqt` running under wine. NSIS is only needed for the installer.

## Android

Docker is the only thing the host needs - no SDK, no NDK, no JDK, no Qt kit:

    ./android/build.sh

leaves `dist/quackchat-<version>-unsigned.apk`. Android installs nothing
unsigned, so that file is an input to signing rather than something to hand out;
`release.sh` signs it with the key named in `release.env`. `--debug` builds the
debug-signed apk instead, which does install - for testing only, since nothing
the debug key signed can ever be published.

Signing happens in the container too, when `QUACK_ANDROID_KEYSTORE` and
`QUACK_ANDROID_KEY_ALIAS` are set - `release.sh` reads them from `release.env`.
The keystore is bind-mounted read-only, never copied into the checkout or into
an image, and the password is passed by variable name rather than value so it
stays out of the process table. Left unset, `apksigner` prompts.

The image carries its own NDK, so tacky's Android targets are driven with
`ANDROID_DOCKER=0`; left at the default they would start a second container from
inside this one. `docker/android.Dockerfile` pins the NDK, the JDK and the
command-line tools by checksum, but what `sdkmanager` fetches cannot be pinned -
those packages publish none - and the versions it installs have to satisfy the
Android Gradle Plugin that Qt chose, not one we pick.

To build against a host toolchain instead:

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

That is the form to use while working on the app - it installs what it builds.
The release bundle has its own script, which checks both pins first and dates the
build by the commit rather than by the manifest's mtime:

    ./flatpak/build.sh

leaving `dist/quackchat-<version>-x86_64.flatpak`.

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

runs it on a clean Ubuntu container under a virtual X server, the only real
check that it works without Qt installed.

## Making a release

    ./release.sh [--check] [<version>] [target...]

builds every artifact into `dist/` and writes a `SHA256SUMS` over them. Targets
are `windows`, `android`, `flatpak` and `appimage`; all four run when none are
named. Nothing is uploaded - the release page is a separate, deliberate step.

`<version>` is checked rather than applied: the version lives in
`project(quack_qml VERSION)`, which is what CPack, the Android version code and
the AppImage all read, so naming it here only asserts that the bump happened.

Everything the selected targets need is checked before the first one builds -
`--check` runs just that and stops, which is how to find out whether this
machine can make a release without waiting for one. Three of the four targets
need only docker; the Flatpak needs `flatpak-builder`. The one thing that cannot
live in an image is the Android signing key, which comes from `release.env`,
made by copying `release.env.sample`.

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
machine, so it catches the clock, the path and the mtimes; only a rebuild
somewhere else proves the rest.

The other three are not held to it. All of them now build from the same pinned
toolchain, so the ingredients are fixed, but nothing checks that their output
settles: `tools/repro-check.sh flatpak` would settle the Flatpak, whose remaining
unknown is the KDE sdk it builds against - a binary runtime nobody here compiles,
recorded in `flatpak/runtime.pin` rather than built. Windows and Android have no
such check at all, and the apk in particular goes through gradle, which would
have to be made deterministic first.

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
