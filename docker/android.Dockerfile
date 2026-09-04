# The Android toolchain, derived from the common image: tacky's bionic archive,
# the Qt app, and the apk that androiddeployqt and gradle package. Driven by
# android/build.sh.
#
# The distro pin, the compiler, tacky's build tools, the native tcl and Qt's
# Linux kit - the host tools a cross build runs - come from
# docker/common.Dockerfile. Only what Android alone needs is added here, and it
# is a separate image rather than part of that one because the NDK is ~5 GB
# unpacked.
#
# This image carries its own NDK, so tacky's android targets are driven with
# ANDROID_DOCKER=0. Left at the default they would call zippy/in_docker.sh and
# try to start a second container from inside this one, which needs the docker
# socket mounted and hands back root-owned output.
FROM quack-build:common

# gradle's jlink step fails on newer JDKs, so the version is pinned by being
# whatever the vault snapshot carries.
RUN dnf -y --disablerepo='*' --enablerepo='vault-*' --enablerepo=epel install \
        java-21-openjdk-devel \
    && dnf clean all
ENV QUACK_ANDROID_JDK=/usr/lib/jvm/java-21-openjdk
ENV JAVA_HOME=${QUACK_ANDROID_JDK}

# The Android Qt kit. aqt is already installed and patched in the common image;
# only the kit differs. No patch is needed for this one - aqt maps the android
# architectures correctly, and it is Windows that it gets wrong.
ARG QT_VERSION=6.11.1
ENV QUACK_QT_ANDROID=/opt/Qt/${QT_VERSION}/android_arm64_v8a
RUN /opt/aqt/bin/aqt install-qt linux android ${QT_VERSION} android_arm64_v8a \
        --outputdir /opt/Qt \
    && rm -rf /root/.cache

# NDK r29. It has to be r29: libtacky-android.a is built against it, r27's
# libc++ has no __cxa_init_primary_exception, and API levels below 30 stub no
# pthread_cond_clockwait - an older kit links with undefined symbols a long way
# from their cause.
#
# ANDROID_NDK is the name zippy's android-toolchain.cmake reads out of the
# environment. Its toolchain bin/ is deliberately kept off PATH: it carries ld,
# clang and llvm binutils that would shadow the native and MinGW toolchains
# inherited from the common image. android/in-image.sh prepends it for its own
# build instead.
ARG NDK_VERSION=r29
ARG NDK_SHA256=4abbbcdc842f3d4879206e9695d52709603e52dd68d3c1fff04b3b5e7a308ecf
ENV ANDROID_NDK=/opt/android-ndk-${NDK_VERSION}
ENV QUACK_ANDROID_NDK=${ANDROID_NDK}
RUN curl -fsSL -o /tmp/ndk.zip \
        https://dl.google.com/android/repository/android-ndk-${NDK_VERSION}-linux.zip \
    && printf '%s  %s\n' "${NDK_SHA256}" /tmp/ndk.zip | sha256sum -c - \
    && unzip -q /tmp/ndk.zip -d /opt \
    && rm -f /tmp/ndk.zip

# The SDK, from the command-line tools bundle - the only part Google publishes
# without the IDE. The bundle is checksum-pinned; what sdkmanager fetches is not,
# and cannot be - those packages publish no checksum and the tool offers no way
# to state one. So this is the floating input in an otherwise pinned image, and
# it is last so that changing which components are installed does not drag the
# NDK and Qt layers along with it.
#
# The versions have to satisfy the Android Gradle Plugin that Qt chose, not one
# we pick: AGP 9.0.0 refuses build-tools below 36.0.0, and on a mismatch gradle
# tries to download what it wants and dies on a read-only SDK, the container
# running as the invoking user. Two platforms because androiddeployqt's target
# has moved between Qt releases and CMakeLists.txt pins only the minimum (30).
ARG CMDLINE_TOOLS=13114758
ARG CMDLINE_TOOLS_SHA256=7ec965280a073311c339e571cd5de778b9975026cfcbe79f2b1cdcb1e15317ee
ARG ANDROID_BUILD_TOOLS=36.0.0
ENV QUACK_ANDROID_SDK=/opt/android-sdk
RUN curl -fsSL -o /tmp/tools.zip \
        https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip \
    && printf '%s  %s\n' "${CMDLINE_TOOLS_SHA256}" /tmp/tools.zip | sha256sum -c - \
    && mkdir -p ${QUACK_ANDROID_SDK}/cmdline-tools \
    && unzip -q /tmp/tools.zip -d ${QUACK_ANDROID_SDK}/cmdline-tools \
    && mv ${QUACK_ANDROID_SDK}/cmdline-tools/cmdline-tools \
          ${QUACK_ANDROID_SDK}/cmdline-tools/latest \
    && rm -f /tmp/tools.zip \
    && yes | ${QUACK_ANDROID_SDK}/cmdline-tools/latest/bin/sdkmanager --licenses >/dev/null \
    && ${QUACK_ANDROID_SDK}/cmdline-tools/latest/bin/sdkmanager \
        "platform-tools" \
        "build-tools;${ANDROID_BUILD_TOOLS}" \
        "platforms;android-35" \
        "platforms;android-36" >/dev/null \
    && rm -rf /root/.android

ENV PATH=${QUACK_ANDROID_SDK}/cmdline-tools/latest/bin:${QUACK_ANDROID_SDK}/platform-tools:$PATH
WORKDIR /src
