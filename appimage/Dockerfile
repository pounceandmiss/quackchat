# Toolchain image for the portable Linux build: tacky's archive, the Qt app and
# the AppImage packaging all happen in here. Driven by appimage/build.sh.
#
# Rocky 9 for its glibc 2.34, which is the floor Qt itself sets - the official
# Qt binaries reference GLIBC_2.34, and are built on RHEL 9 (their archives are
# tagged RHEL_9_6), so this is the matching base.
#
# Not Debian bookworm, the obvious first try: its glibc 2.36 leaves
# arc4random_buf@2.36 (from tacky's mbedtls, which has its own fallback when
# libc lacks it) and hypot@2.35 in the binary, putting the AppImage out of reach
# of Ubuntu 22.04 LTS for nothing.
FROM rockylinux:9

# ninja and ccache come from EPEL, the -devel packages below from CRB.
RUN dnf -y install dnf-plugins-core epel-release \
    && dnf config-manager --set-enabled crb \
    && dnf -y update \
    && dnf clean all

# Mostly what zippy needs to build tacky and its deps from source. The ones that
# are not obvious:
#
#   patch, libstdc++-static  zippy patches its dep trees and links libstdc++
#                            statically. Debian bundles both with
#                            build-essential; RHEL splits them out.
#   speexdsp-devel           rtc-ma vendors speexdsp's jitter.c but includes
#                            <speex/speex_jitter.h> from the system.
#   zip                      Tk's configure shells out to it.
#   mesa-lib*-devel          Qt6Gui resolves WrapOpenGL at find_package time and
#                            fails find_package(Qt6) without the headers and
#                            unversioned .so symlinks.
#   xcb-*, libxkbcommon-x11  linuxdeploy resolves Qt's prebuilt xcb plugin
#                            against the filesystem, and will ship it with holes
#                            in it if these are missing.
#
# curl is absent on purpose: the base image's curl-minimal already provides
# /usr/bin/curl and conflicts with the full package, failing the transaction.
RUN dnf -y install \
        gcc gcc-c++ libstdc++-static make patch cmake ninja-build \
        git file zip xz tar ca-certificates pkgconf-pkg-config ccache \
        python3 python3-pip \
        zlib-devel speexdsp-devel \
        libX11-devel libXext-devel libXScrnSaver-devel libXft-devel \
        libXcursor-devel fontconfig-devel \
        mesa-libGL-devel mesa-libEGL-devel libglvnd-devel \
        libxkbcommon-x11 libxkbcommon-x11-devel \
        libxcb libX11-xcb xcb-util xcb-util-image xcb-util-keysyms \
        xcb-util-renderutil xcb-util-wm xcb-util-cursor \
        libSM libICE glib2 freetype dbus-libs \
    && dnf clean all

# The distro has no Qt 6.11, so it comes from the official installer archives;
# aqt is the unattended front end to the same downloads.
ARG QT_VERSION=6.11.1
ENV QTDIR=/opt/Qt/${QT_VERSION}/gcc_64
RUN python3 -m venv /opt/aqt \
    && /opt/aqt/bin/pip install --no-cache-dir aqtinstall \
    && /opt/aqt/bin/aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
        --outputdir /opt/Qt \
    && rm -rf /root/.cache

# Three SQL drivers and the GTK platform theme, none of which the app uses and
# every one of which linuxdeploy would die on: it treats a dependency it cannot
# resolve as fatal, so keeping them means carrying libpq, libmysqlclient and the
# whole of GTK 3 for the sake of bundling them.
#
# The drivers arrive indirectly. linuxdeploy's Qt plugin deploys a matched QML
# module by copying its entire directory, so `import QtQuick` brings the whole
# QtQuick.* tree - LocalStorage included, which links Qt6Sql. The app opens no
# SQL database of its own; tacky owns its store and links SQLite into its
# archive. The SQLite driver stays.
#
# Each .so's Config files go with it. Qt's CMake package globs its plugins'
# Qt6*PluginConfig.cmake and each asserts its .so exists, so removing the
# library alone breaks find_package(Qt6) rather than the deployment.
RUN cd ${QTDIR} \
    && rm -f plugins/sqldrivers/libqsqlpsql.so \
             plugins/sqldrivers/libqsqlmysql.so \
             plugins/sqldrivers/libqsqlodbc.so \
             plugins/platformthemes/libqgtk3.so \
    && rm -f lib/cmake/Qt6Sql/Qt6QPSQLDriverPlugin*.cmake \
             lib/cmake/Qt6Sql/Qt6QMYSQLDriverPlugin*.cmake \
             lib/cmake/Qt6Sql/Qt6QODBCDriverPlugin*.cmake \
             lib/cmake/Qt6Gui/Qt6QGtk3ThemePlugin*.cmake

# The AppImage tools are AppImages themselves and would want /dev/fuse to mount
# themselves; extract-and-run unpacks to a temp dir instead. Set in the image so
# it also covers the tools linuxdeploy invokes on its own.
ENV APPIMAGE_EXTRACT_AND_RUN=1
ARG LD_RELEASE=continuous
RUN base=https://github.com/linuxdeploy \
    && curl -sSL -o /usr/local/bin/linuxdeploy \
        $base/linuxdeploy/releases/download/$LD_RELEASE/linuxdeploy-x86_64.AppImage \
    && curl -sSL -o /usr/local/bin/linuxdeploy-plugin-qt \
        $base/linuxdeploy-plugin-qt/releases/download/$LD_RELEASE/linuxdeploy-plugin-qt-x86_64.AppImage \
    && curl -sSL -o /usr/local/bin/appimagetool \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
    && chmod +x /usr/local/bin/linuxdeploy /usr/local/bin/linuxdeploy-plugin-qt \
        /usr/local/bin/appimagetool

ENV PATH=/usr/lib64/ccache:${QTDIR}/bin:$PATH
WORKDIR /src
