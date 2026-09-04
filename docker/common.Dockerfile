# Toolchain image for the AppImage and the Windows cross-build, and the base
# docker/android.Dockerfile derives from. Driven by docker/run.sh, which the
# per-target build.sh scripts call.
#
# One image for those two: they differ only by a MinGW toolchain and wine, and
# share the distro, the compiler, tacky's build tools, the native tcl and Qt's
# Linux kit - which a cross build needs anyway for moc and rcc. Android is
# separate because its NDK is ~5 GB unpacked.
#
# Rocky 9 for its glibc 2.34, which is the floor Qt itself sets - the official
# Qt binaries reference GLIBC_2.34, and are built on RHEL 9 (their archives are
# tagged RHEL_9_6), so this is the matching base.
#
# Not Debian bookworm, the obvious first try: its glibc 2.36 leaves
# arc4random_buf@2.36 (from tacky's mbedtls, which has its own fallback when
# libc lacks it) and hypot@2.35 in the binary, putting the AppImage out of reach
# of Ubuntu 22.04 LTS for nothing.
#
# Everything below is pinned: rebuilding this image must not move the compiler,
# the libraries linuxdeploy bundles into the AppImage, or the runtime prepended
# to it, and an unpinned `dnf update` could raise the glibc floor unnoticed.
#
# The digest is the pin, the tag only says what it is; a new one comes from
# `docker buildx imagetools inspect rockylinux:9`. It names a stale Rocky 9.3,
# which matters little when the userspace comes from the vault snapshot below.
FROM rockylinux:9@sha256:d7be1c094cc5845ee815d4632fe377514ee6ebcf8efaed6892889657e5ddaaa6

# Rocky's normal repos track the live 9 stream, so a package resolves to whatever
# is current on the day the image is built. The vault holds frozen per-point-
# release snapshots instead, which is what makes the installed set a function of
# this Dockerfile alone. Named by baseurl rather than mirrorlist for the same
# reason: a mirror is free to serve a different snapshot.
#
# Every dnf below names the repos it wants rather than the shipped rocky.repo
# being deleted up front, and it has to be that way round: `dnf update` updates
# rocky-repos along with everything else, and writing /etc/yum.repos.d/rocky*.repo
# is that package's whole job, so a file deleted before the transaction is back,
# and enabled, by the end of it. Mixing the two at least fails loudly - the live
# stream runs ahead of any vault snapshot and dnf refuses the split dependency.
#
# The snapshot always lags by a point release: Rocky vaults 9.x once 9.x+1 ships.
# glibc does not move across them - it is 2.34 throughout Rocky 9 - but check the
# floor after moving the number, reading the extracted tree rather than the
# AppImage, whose leading runtime stub makes objdump report no glibc dependency
# at all:
#
#   ./dist/quackchat-*.AppImage --appimage-extract >/dev/null
#   find squashfs-root -type f \( -name '*.so*' -o -perm -u+x \) \
#     -exec objdump -T {} + 2>/dev/null \
#     | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -3
ARG ROCKY_VAULT=9.7
RUN vault=https://dl.rockylinux.org/vault/rocky/${ROCKY_VAULT} \
    && for r in BaseOS AppStream CRB extras; do \
        printf '[vault-%s]\nname=Rocky %s - %s (vault)\nbaseurl=%s/%s/$basearch/os/\ngpgcheck=1\nenabled=1\ngpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-Rocky-9\n\n' \
            "$r" "${ROCKY_VAULT}" "$r" "$vault" "$r" \
            >> /etc/yum.repos.d/vault.repo; \
       done \
    && dnf -y --disablerepo='*' --enablerepo='vault-*' update \
    && dnf clean all

# ninja, ccache and wine come from EPEL, everything else from the vault. EPEL
# publishes no frozen snapshot, so it is the one floating repo here - acceptable
# only because none of the three reaches an artifact: two drive the compiler,
# and wine only runs windeployqt, which copies Qt's own DLLs.
RUN dnf -y --disablerepo='*' --enablerepo='vault-*' install epel-release \
    && dnf clean all

# One transaction for both targets. The ones that are not obvious:
#
#   patch, libstdc++-static  zippy patches its dep trees and links libstdc++
#                            statically. Debian bundles both with
#                            build-essential; RHEL splits them out.
#   zip, unzip, bzip2        Tk's configure shells out to zip; the NDK and NSIS
#                            arrive as zip and bzip2.
#   speexdsp-devel           rtc-ma vendors speexdsp's jitter.c but includes
#                            <speex/speex_jitter.h> from the system.
#   mesa-lib*-devel          Qt6Gui resolves WrapOpenGL at find_package time and
#                            fails find_package(Qt6) without the headers and
#                            unversioned .so symlinks.
#   xcb-*, libxkbcommon-x11  linuxdeploy resolves Qt's prebuilt xcb plugin
#                            against the filesystem, and will ship it with holes
#                            in it if these are missing.
#   mingw64-*                the Windows cross toolchain, from CRB. posix
#                            threads, which libdatachannel needs, and it carries
#                            libssp, which tacky's kitsh link wants.
#   wine                     windeployqt ships only as a .exe. Without it cpack
#                            packages a tree with no Qt in it at all.
# curl is absent on purpose: the base image's curl-minimal already provides
# /usr/bin/curl and conflicts with the full package, failing the transaction.
RUN dnf -y --disablerepo='*' --enablerepo='vault-*' --enablerepo=epel install \
        gcc gcc-c++ libstdc++-static make patch cmake ninja-build \
        git file zip unzip xz bzip2 tar ca-certificates pkgconf-pkg-config ccache \
        python3 python3-pip \
        zlib-devel speexdsp-devel \
        libX11-devel libXext-devel libXScrnSaver-devel libXft-devel \
        libXcursor-devel fontconfig-devel \
        mesa-libGL-devel mesa-libEGL-devel libglvnd-devel \
        libxkbcommon-x11 libxkbcommon-x11-devel \
        libxcb libX11-xcb xcb-util xcb-util-image xcb-util-keysyms \
        xcb-util-renderutil xcb-util-wm xcb-util-cursor \
        libSM libICE glib2 freetype dbus-libs \
        mingw64-gcc mingw64-gcc-c++ mingw64-winpthreads-static \
        wine \
    && dnf clean all

# No dnf runs after this point, so nothing will rewrite these. Switching the live
# repos off means a `dnf install` in a derived image or a debugging session
# cannot silently reach past the vault snapshot. The vault's own file is named
# vault.repo so this glob cannot turn it off, and epel.repo is likewise left.
RUN sed -i 's/^enabled=1/enabled=0/' /etc/yum.repos.d/rocky*.repo

# Native tcl9.0, built once and used by both cross builds: zippy runs a host
# interp as a build tool (install-tzdata, thread's zipfs mkzip) and neither the
# cross-built PE nor the aarch64 one can run here. Rocky ships only 8.6. This is
# what QUACK_WIN_TCLSH names.
ARG TCL_VERSION=9.0.3
ARG TCL_SHA256=2537ba0c86112c8c953f7c09d33f134dd45c0fb3a71f2d7f7691fd301d2c33a6
RUN curl -fsSL -o /tmp/tcl.tar.gz \
        http://prdownloads.sourceforge.net/tcl/tcl${TCL_VERSION}-src.tar.gz \
    && printf '%s  %s\n' "${TCL_SHA256}" /tmp/tcl.tar.gz | sha256sum -c - \
    && tar xzf /tmp/tcl.tar.gz -C /tmp \
    && cd /tmp/tcl${TCL_VERSION}/unix \
    && ./configure --prefix=/usr/local --disable-shared \
    && make -j"$(nproc)" && make install \
    && rm -rf /tmp/tcl.tar.gz /tmp/tcl${TCL_VERSION}
ENV QUACK_WIN_TCLSH=/usr/local/bin/tclsh9.0

# Both desktop Qt kits. The distro has no Qt 6.11, so they come from the official
# installer archives; aqt is the unattended front end to the same downloads, and
# is pinned as well as Qt because which files it lays down and where is its own
# business, not Qt's.
#
# aqt needs a patch to fetch the mingw kit at all. Its extension_for_arch maps
# only wasm and android, so every Windows arch falls through to "" and it asks
# the repository for qt6_6111 - which does not exist. The real layout is
# per-compiler, qt6_6111_mingw / _llvm_mingw / _msvc2022_64: the arch with
# win64_ taken off the front. Patched here rather than in a throwaway venv, so
# the fix is recorded and pinned with the aqt version it applies to. The grep
# fails the build if the line it edits has moved; the import fails it if the
# result will not parse, before the downloads rather than after them.
ARG QT_VERSION=6.11.1
ARG AQTINSTALL_VERSION=3.3.0
ENV QUACK_QT_HOST=/opt/Qt/${QT_VERSION}/gcc_64
ENV QUACK_QT_MINGW=/opt/Qt/${QT_VERSION}/mingw_64
RUN python3 -m venv /opt/aqt \
    && /opt/aqt/bin/pip install --no-cache-dir aqtinstall==${AQTINSTALL_VERSION} \
    && meta=$(/opt/aqt/bin/python -c 'import aqt.metadata as m; print(m.__file__)') \
    && grep -q '^        elif architecture.startswith("android_") and is_version_ge_6:$' "$meta" \
    && sed -i 's|^        elif architecture.startswith("android_") and is_version_ge_6:$|        elif architecture.startswith("win64_") and is_version_ge_6:\n            return architecture[len("win64_") :]\n&|' "$meta" \
    && /opt/aqt/bin/python -c 'import aqt.metadata'
RUN /opt/aqt/bin/aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
        --outputdir /opt/Qt \
    && /opt/aqt/bin/aqt install-qt windows desktop ${QT_VERSION} win64_mingw \
        --outputdir /opt/Qt \
    && rm -rf /root/.cache

# Three SQL drivers and the GTK platform theme, none of which the app uses and
# every one of which linuxdeploy would die on: it treats a dependency it cannot
# resolve as fatal, so keeping them means carrying libpq, libmysqlclient and the
# whole of GTK 3 for the sake of bundling them. Only the Linux kit is pruned -
# it is the only one linuxdeploy ever reads.
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
RUN cd ${QUACK_QT_HOST} \
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

# Each tool is pinned by release and by checksum. The release stops the version
# moving; the checksum catches it moving anyway, which upstream does do -
# appimagetool's 1.9.1 assets were replaced weeks after the tag was cut. And
# linuxdeploy-plugin-qt has no tagged release recent enough to know about Qt
# 6.11, so it is pinned at `continuous` by checksum alone. Take a new checksum
# from the release rather than from the file just downloaded:
#
#   curl -s https://api.github.com/repos/AppImage/appimagetool/releases/tags/1.9.1 \
#     | jq -r '.assets[]|select(.name|test("x86_64.AppImage$"))|.digest'
ARG LINUXDEPLOY_RELEASE=1-alpha-20251107-1
ARG LINUXDEPLOY_SHA256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
ARG LINUXDEPLOY_QT_RELEASE=continuous
ARG LINUXDEPLOY_QT_SHA256=cfc1055b2b9dbc08412b579f20990b7b41a17b61beaa5847dc9477c96c9e9617
ARG APPIMAGETOOL_RELEASE=1.9.1
ARG APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUN base=https://github.com/linuxdeploy \
    && curl -sSLf -o /usr/local/bin/linuxdeploy \
        $base/linuxdeploy/releases/download/${LINUXDEPLOY_RELEASE}/linuxdeploy-x86_64.AppImage \
    && curl -sSLf -o /usr/local/bin/linuxdeploy-plugin-qt \
        $base/linuxdeploy-plugin-qt/releases/download/${LINUXDEPLOY_QT_RELEASE}/linuxdeploy-plugin-qt-x86_64.AppImage \
    && curl -sSLf -o /usr/local/bin/appimagetool \
        https://github.com/AppImage/appimagetool/releases/download/${APPIMAGETOOL_RELEASE}/appimagetool-x86_64.AppImage \
    && printf '%s  %s\n' \
        "${LINUXDEPLOY_SHA256}"    /usr/local/bin/linuxdeploy \
        "${LINUXDEPLOY_QT_SHA256}" /usr/local/bin/linuxdeploy-plugin-qt \
        "${APPIMAGETOOL_SHA256}"   /usr/local/bin/appimagetool \
        | sha256sum -c - \
    && chmod +x /usr/local/bin/linuxdeploy /usr/local/bin/linuxdeploy-plugin-qt \
        /usr/local/bin/appimagetool

# The type-2 runtime, prepended to the finished AppImage and so part of it. Left
# to itself appimagetool fetches this from the type2-runtime repo's `continuous`
# release at packaging time: a floating input in the middle of a release build,
# and a network dependency in a step that otherwise needs none. Staged here from
# a dated release instead, and handed to appimagetool with --runtime-file by
# appimage/in-image.sh.
ARG APPIMAGE_RUNTIME_RELEASE=20251108
ARG APPIMAGE_RUNTIME_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
ENV APPIMAGE_RUNTIME_FILE=/usr/local/share/appimage/runtime-x86_64
RUN mkdir -p "$(dirname "${APPIMAGE_RUNTIME_FILE}")" \
    && curl -sSLf -o "${APPIMAGE_RUNTIME_FILE}" \
        https://github.com/AppImage/type2-runtime/releases/download/${APPIMAGE_RUNTIME_RELEASE}/runtime-x86_64 \
    && printf '%s  %s\n' "${APPIMAGE_RUNTIME_SHA256}" "${APPIMAGE_RUNTIME_FILE}" \
        | sha256sum -c -

# NSIS builds the Windows installer. It is not packaged for EL9 at all - not in
# the vault repos, not in EPEL - and neither is scons, so both come from source.
#
# makensis is built from the source tarball; the stubs and plugins it prepends to
# an installer are prebuilt Windows binaries taken from the official zip, which
# is what SKIPSTUBS/SKIPPLUGINS leave out of the build.
ARG SCONS_VERSION=4.9.1
ARG NSIS_VERSION=3.11
ARG NSIS_SRC_SHA256=19e72062676ebdc67c11dc032ba80b979cdbffd3886c60b04bb442cdd401ff4b
ARG NSIS_BIN_SHA256=c7d27f780ddb6cffb4730138cd1591e841f4b7edb155856901cdf5f214394fa1
RUN python3 -m venv /opt/scons \
    && /opt/scons/bin/pip install --no-cache-dir scons==${SCONS_VERSION} \
    && base=https://downloads.sourceforge.net/project/nsis/NSIS%203/${NSIS_VERSION} \
    && curl -fsSL -o /tmp/nsis-src.tar.bz2 "$base/nsis-${NSIS_VERSION}-src.tar.bz2" \
    && curl -fsSL -o /tmp/nsis-bin.zip "$base/nsis-${NSIS_VERSION}.zip" \
    && printf '%s  %s\n%s  %s\n' \
        "${NSIS_SRC_SHA256}" /tmp/nsis-src.tar.bz2 \
        "${NSIS_BIN_SHA256}" /tmp/nsis-bin.zip | sha256sum -c - \
    && tar xjf /tmp/nsis-src.tar.bz2 -C /tmp \
    && cd /tmp/nsis-${NSIS_VERSION}-src \
    && /opt/scons/bin/scons -j"$(nproc)" \
        VERSION=${NSIS_VERSION} \
        SKIPSTUBS=all SKIPPLUGINS=all SKIPUTILS=all SKIPMISC=all \
        NSIS_CONFIG_CONST_DATA_PATH=no PREFIX=/usr/local install-compiler \
    && mkdir -p /usr/local/bin /usr/local/share/nsis \
    && mv /usr/local/makensis /usr/local/bin/makensis \
    && unzip -q /tmp/nsis-bin.zip -d /tmp/nsis-bin \
    && cp -r /tmp/nsis-bin/nsis-${NSIS_VERSION}/. /usr/local/share/nsis/ \
    && rm -rf /tmp/nsis-src.tar.bz2 /tmp/nsis-bin.zip /tmp/nsis-bin \
        /tmp/nsis-${NSIS_VERSION}-src \
    && NSISDIR=/usr/local/share/nsis makensis -VERSION
# install-compiler leaves the binary at $PREFIX/makensis, and where it sits
# matters beyond PATH: NSIS_CONFIG_CONST_DATA_PATH=no makes makensis derive its
# data directory from its own location, so from bin/ it finds ../share/nsis.
# NSISDIR is set outright rather than relying on that.
ENV NSISDIR=/usr/local/share/nsis

# QTDIR is what appimage/in-image.sh and linuxdeploy's Qt plugin read; it is the
# Linux kit, the only one either of them ever looks at. ccache stays first on
# PATH so its shims shadow the native compilers.
ENV QTDIR=${QUACK_QT_HOST}
ENV WINEDEBUG=-all
ENV PATH=/usr/lib64/ccache:${QTDIR}/bin:$PATH
WORKDIR /src
