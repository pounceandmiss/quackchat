cmake_minimum_required(VERSION 3.21)
# C is only for the Android JNI shim; without it CMake drops the .c source from
# the target silently rather than failing.
project(quack_qml VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED
    COMPONENTS Core Gui Network Qml Quick QuickControls2 QuickDialogs2 Test)
# Desktop notifications on Linux go over the session bus. Optional: without it
# the app builds and runs exactly as before, just silently. Android posts its
# own from the backend service (android/.../Notifications.java), so it is never
# wanted there. Gated on the platform rather than on the package being findable,
# because the Windows and macOS kits do ship Qt6DBus: QUIET alone would pick the
# FreeDesktop notifier there and address a session bus that is not running.
if(NOT ANDROID AND NOT WIN32 AND NOT APPLE)
    find_package(Qt6 6.5 QUIET COMPONENTS DBus)
endif()
qt_standard_project_setup(REQUIRES 6.5)

# tacky has its own (non-CMake) make, so we find what it drops in <root>/dist
# instead of building it. Point at a checkout with -DTACKY_ROOT=<dir> or
# $TACKY_ROOT, or name the archive itself with -DTACKY_LIB=<file>.
if(ANDROID)
    # bionic arm64 archive; tacky builds it in Docker via `make android-lib`
    set(_tacky_lib_file libtacky-android.a)
    set(_tacky_make android-lib)
elseif(WIN32)
    # MinGW PE archive, cross-built from Linux. `make win-lib` tracks whichever
    # mingw is installed, which is the one doing the final link here; the
    # DOCKER=1 variant pins gcc 12 for reproducibility and links just as well,
    # since the runtime only has to be no older than the objects.
    set(_tacky_lib_file libtacky-win.a)
    set(_tacky_make win-lib)
else()
    set(_tacky_lib_file libtacky.a)
    set(_tacky_make lib)
endif()

set(TACKY_ROOT "" CACHE PATH "tacky checkout holding embed/tacky.h and dist/${_tacky_lib_file}")

# A stated root first, then the usual side-by-side layout. The siblings are
# source-relative, so they miss from a git worktree; set $TACKY_ROOT there.
set(_tacky_roots ${TACKY_ROOT} $ENV{TACKY_ROOT}
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tacky"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tacky_t"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tacky")

# HINTS skips a candidate that misses, which would let a typo'd root fall through
# to a sibling, or pair one checkout's header with another's archive.
foreach(_r IN ITEMS "${TACKY_ROOT}" "$ENV{TACKY_ROOT}")
    if(NOT _r STREQUAL "" AND NOT (EXISTS "${_r}/embed/tacky.h"
                                   AND EXISTS "${_r}/dist/${_tacky_lib_file}"))
        message(FATAL_ERROR
            "TACKY_ROOT=${_r} is not a built tacky checkout "
            "(want embed/tacky.h and dist/${_tacky_lib_file} under it).")
    endif()
endforeach()

# NO_CMAKE_FIND_ROOT_PATH because the NDK toolchain confines find_* to the
# sysroot, and tacky is a host-side prebuilt that cross-compiled for the target.
find_path(TACKY_INCLUDE
    NAMES tacky.h
    HINTS ${_tacky_roots}
    PATH_SUFFIXES embed
    NO_CMAKE_FIND_ROOT_PATH
    DOC "directory holding tacky.h")
find_library(TACKY_LIB
    NAMES ${_tacky_lib_file}
    HINTS ${_tacky_roots}
    PATH_SUFFIXES dist
    NO_CMAKE_FIND_ROOT_PATH
    DOC "libtacky static archive")

# find_* trusts a cached hit without re-checking, so EXISTS is what catches a
# stale cache or a bad -DTACKY_LIB.
if(NOT TACKY_LIB OR NOT EXISTS "${TACKY_LIB}"
        OR NOT TACKY_INCLUDE OR NOT EXISTS "${TACKY_INCLUDE}/tacky.h")
    list(JOIN _tacky_roots "\n    " _tacky_searched)
    message(FATAL_ERROR
        "tacky not found (need embed/tacky.h and dist/${_tacky_lib_file}).\n"
        "Searched:\n    ${_tacky_searched}\n"
        "Build it with `make ${_tacky_make}` in a tacky checkout, then either "
        "pass -DTACKY_ROOT=<checkout> or set $TACKY_ROOT.")
endif()

# Rebuilding tacky without leaving the IDE. Nothing depends on this.
cmake_path(GET TACKY_INCLUDE PARENT_PATH _tacky_root)
add_custom_target(tacky-lib
    COMMAND make ${_tacky_make}
    WORKING_DIRECTORY "${_tacky_root}"
    USES_TERMINAL
    COMMENT "make ${_tacky_make} in ${_tacky_root}")

# Imported as a real target so dependents relink when the archive changes.
add_library(tacky STATIC IMPORTED GLOBAL)
set_target_properties(tacky PROPERTIES
    IMPORTED_LOCATION "${TACKY_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${TACKY_INCLUDE}")
if(WIN32)
    # No libdl on Windows, and no -lz either: tacky's `ar -M` merge folds zlib
    # into the archive. What is left is import libs - the subset of tacky's own
    # KITSH_SYSLIBS (zippy/windows.mk) this archive still reaches for, found by
    # diffing its undefined symbols against what each lib defines:
    #   x86_64-w64-mingw32-nm --undefined-only dist/libtacky-win.a
    # The Tk half (gdi32, comdlg32, comctl32, uxtheme, ...) is deliberately
    # absent - libtacky is the tclsh flavour and resolves nothing from it. So is
    # -static: Qt's kit ships shared libgcc/libstdc++ and the app has to keep
    # using them.
    target_link_libraries(tacky INTERFACE
        ws2_32 crypt32 netapi32 userenv iphlpapi bcrypt ncrypt winpthread ssp)
else()
    target_link_libraries(tacky INTERFACE dl z m)
    if(NOT ANDROID)
        # bionic has pthreads in libc
        target_link_libraries(tacky INTERFACE pthread)
    endif()
endif()

# The embedded transport is the only thing that calls into libtacky directly,
# so on Android - where the interpreter lives in the service - it is left out
# entirely rather than linked against an archive this process must not open.
if(ANDROID)
    set(_embedded_transport "")
else()
    set(_embedded_transport
        backend/EmbeddedTransport.cpp backend/EmbeddedTransport.h)
endif()

# Same shape as the transport above: one Notifier implementation per platform,
# picked here. Windows, macOS and a Linux build without Qt6::DBus get the null
# factory, which disables alerts and nothing else. So does Android, which posts
# its own from the service rather than from this process.
if(TARGET Qt6::DBus)
    set(_notifier backend/FdoNotifier.cpp backend/FdoNotifier.h)
else()
    set(_notifier backend/NullNotifier.cpp)
endif()

qt_add_library(quack STATIC)
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(Jid.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(AppWindows.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(quack
    URI Quack
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Quack"
    QML_FILES
        Main.qml
        ShellWindow.qml
        ChatWindow.qml
        AccountSettingsWindow.qml
        OmemoKeysWindow.qml
        MessageXmlWindow.qml
        AppShell.qml
        AccountRail.qml
        SheetDialog.qml
        PageHeader.qml
        AddAccountSheet.qml
        AccountSettingsPage.qml
        TrustPicker.qml
        TrustList.qml
        Card.qml
        Caption.qml
        OmemoKeysPage.qml
        MessageXmlPage.qml
        Fingerprint.qml
        ConversationsPage.qml
        ChatRowMenu.qml
        AppMenu.qml
        MenuEntry.qml
        NewChatSheet.qml
        Jid.qml
        JoinRoomSheet.qml
        TextPromptDialog.qml
        ConfirmDialog.qml
        SearchPage.qml
        ChatPage.qml
        ChatBubble.qml
        Avatar.qml
        IconButton.qml
        CallWindow.qml
        IncomingCallDialog.qml
        CallButton.qml
        AudioLevelRow.qml
        AppWindows.qml
        Glyph.qml
        Icons.qml
        Theme.qml
    SOURCES
        backend/AppController.cpp backend/AppController.h
        backend/TackyBackend.cpp backend/TackyBackend.h
        backend/TackyTransport.h
        ${_embedded_transport}
        backend/SocketTransport.cpp backend/SocketTransport.h
        backend/ChatModel.cpp backend/ChatModel.h
        backend/ChatSession.cpp backend/ChatSession.h
        backend/MessageMarkup.cpp backend/MessageMarkup.h
        backend/MessageXml.cpp backend/MessageXml.h
        backend/AuthorNames.cpp backend/AuthorNames.h
        backend/ChatListModel.cpp backend/ChatListModel.h
        backend/SearchModel.cpp backend/SearchModel.h
        backend/MapListModel.cpp backend/MapListModel.h
        backend/MucRoomsModel.cpp backend/MucRoomsModel.h
        backend/ChatListFilter.cpp backend/ChatListFilter.h
        backend/AccountsModel.cpp backend/AccountsModel.h
        backend/AccountSettings.cpp backend/AccountSettings.h
        backend/OmemoDevicesModel.cpp backend/OmemoDevicesModel.h
        backend/OmemoChat.cpp backend/OmemoChat.h
        backend/AvatarController.cpp backend/AvatarController.h
        backend/Clipboard.cpp backend/Clipboard.h
        backend/NotificationController.cpp backend/NotificationController.h
        backend/Notifier.h
        ${_notifier}
        backend/CallsModel.cpp backend/CallsModel.h
        backend/AudioDevices.cpp backend/AudioDevices.h
        backend/AppSettings.cpp backend/AppSettings.h
        backend/AvatarEncoder.h
        backend/AvatarSink.h)
target_include_directories(quack PUBLIC backend "${TACKY_INCLUDE}")
# Core/Qml PUBLIC so the headless tests get the headers without linking QtGui.
target_link_libraries(quack
    PUBLIC Qt6::Core Qt6::Network Qt6::Qml
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
if(ANDROID)
    # The interpreter belongs to the backend service's process, reached over a
    # socket. Linking it here too would put a second writer on tacky's store.
    target_link_libraries(quack PRIVATE Qt6::Network)
else()
    target_link_libraries(quack PRIVATE tacky)
endif()
if(TARGET Qt6::DBus)
    target_link_libraries(quack PRIVATE Qt6::DBus)
endif()

# The QImage half of avatar publishing stays out of `quack`, like the image
# provider, so the headless tests keep their QtGui-free include path.
qt_add_executable(quackchat backend/main.cpp backend/AvatarImageProvider.cpp
                            backend/QImageAvatarEncoder.cpp)
target_link_libraries(quackchat PRIVATE
    quack Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

# BASE is what makes the resource path :/icons/quack-48.png rather than
# :/icons/icons/quack-48.png.
qt_add_resources(quackchat "appicon"
    PREFIX "/icons"
    BASE icons
    FILES icons/quack-48.png icons/quack-128.png icons/quack-256.png)

if(WIN32)
    # Mark the PE as GUI-subsystem, or Windows opens a terminal behind the app.
    # Deliberately not WIN32_EXECUTABLE, which routes the entry through
    # Qt6::EntryPoint: its prebuilt qtentrypoint_win.cpp.obj wants __argc as a
    # dllimport (`__imp___argc`) that a newer MinGW's CRT no longer exports.
    # Keeping mainCRTStartup never pulls that object in, and the CRT builds
    # argc/argv the same way under either subsystem.
    target_link_options(quackchat PRIVATE
        -Wl,--subsystem,windows -Wl,--entry=mainCRTStartup)

    # The icon on the .exe itself. RC is enabled here rather than in
    # project(LANGUAGES) so other configures never look for windres.
    enable_language(RC)
    set(QUACK_ICO "${CMAKE_CURRENT_SOURCE_DIR}/icons/quack.ico")
    configure_file(icons/quack.rc.in "${CMAKE_CURRENT_BINARY_DIR}/quack.rc" @ONLY)
    target_sources(quackchat PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/quack.rc")
endif()
if(ANDROID)
    # libtacky-android.a is built against API 30 bionic
    set_target_properties(quackchat PROPERTIES QT_ANDROID_MIN_SDK_VERSION 30)

    # The interpreter, loaded by the Java service in the :backend process. This
    # is the only thing that links libtacky on Android, and it is why the NDK
    # r29 / android-30 pinning still applies to the build as a whole.
    add_library(tackyjni SHARED backend/jni/tacky_jni.c)
    target_link_libraries(tackyjni PRIVATE tacky)
    set_target_properties(tackyjni PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

    # Deliberately NOT QT_ANDROID_EXTRA_LIBS: that lists the library in
    # libs.xml's bundled_libs, and QtLoader dlopens every entry in whichever
    # process it initialises - so the UI process would map the interpreter it
    # must never run. Staged into the package's libs/ instead, which gradle
    # packages as a plain jniLib that only the service's System.loadLibrary
    # resolves.
    set(_android_pkg "${CMAKE_CURRENT_BINARY_DIR}/android-package")
    add_custom_command(TARGET quackchat POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/android" "${_android_pkg}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:tackyjni>"
                "${_android_pkg}/libs/${CMAKE_ANDROID_ARCH_ABI}/libtackyjni.so"
        COMMENT "Staging the Android package source dir")
    set_target_properties(quackchat PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${_android_pkg}")
    add_dependencies(quackchat tackyjni)
endif()

# A cross build emits tests this host cannot run, and ctest reports those as
# failures rather than skips, so they are off by default there.
# -DQUACK_BUILD_TESTS=ON opts back in for a runner that can execute them (a
# Windows host, or wine via CMAKE_CROSSCOMPILING_EMULATOR).
if(CMAKE_CROSSCOMPILING)
    set(_tests_default OFF)
else()
    set(_tests_default ON)
endif()
option(QUACK_BUILD_TESTS "Build the test executables" ${_tests_default})

# Android stays a hard exclusion rather than a default: there the app reaches
# the interpreter over a socket and `quack` links no tacky at all, so the tests
# have nothing to drive even if asked for.
if(QUACK_BUILD_TESTS AND NOT ANDROID)
enable_testing()

foreach(t tst_backend tst_chatlistmodel tst_chatmodel tst_authornames
          tst_accountsmodel tst_omemodevices tst_omemochat tst_accountsettings
          tst_searchmodel tst_mucrooms tst_chatlistfilter
          tst_integration tst_transport
          tst_calls tst_audiodevices tst_appsettings tst_notifications)
    qt_add_executable(${t} backend/${t}.cpp)
    target_link_libraries(${t} PRIVATE quack Qt6::Test)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# The real FdoNotifier against a stand-in daemon. dbus-run-session gives it a
# private bus to own org.freedesktop.Notifications on - on the developer's own
# session the desktop's daemon already has that name, so without it the test
# could not run at all.
find_program(DBUS_RUN_SESSION dbus-run-session)
if(TARGET Qt6::DBus AND DBUS_RUN_SESSION)
    qt_add_executable(tst_fdonotifier backend/tst_fdonotifier.cpp)
    target_link_libraries(tst_fdonotifier PRIVATE quack Qt6::Test Qt6::DBus)
    add_test(NAME tst_fdonotifier
             COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_fdonotifier>)
elseif(TARGET Qt6::DBus)
    message(STATUS "dbus-run-session not found; skipping tst_fdonotifier")
endif()

# needs QtGui/QtQuick for the image provider response
qt_add_executable(tst_avatar backend/tst_avatar.cpp backend/AvatarImageProvider.cpp)
target_link_libraries(tst_avatar PRIVATE
    quack Qt6::Test Qt6::Gui Qt6::Quick)
add_test(NAME tst_avatar COMMAND tst_avatar)
set_tests_properties(tst_avatar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# needs QtGui for QImage
qt_add_executable(tst_avatarencoder
    backend/tst_avatarencoder.cpp backend/QImageAvatarEncoder.cpp)
target_link_libraries(tst_avatarencoder PRIVATE quack Qt6::Test Qt6::Gui)
add_test(NAME tst_avatarencoder COMMAND tst_avatarencoder)
set_tests_properties(tst_avatarencoder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_qmlload backend/tst_qmlload.cpp)
target_link_libraries(tst_qmlload PRIVATE
    quack Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
add_test(NAME tst_qmlload COMMAND tst_qmlload)
set_tests_properties(tst_qmlload PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# real window + real backend: the feed has to lay out before it can page
qt_add_executable(tst_chatpage backend/tst_chatpage.cpp)
target_link_libraries(tst_chatpage PRIVATE
    quack Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
add_test(NAME tst_chatpage COMMAND tst_chatpage)
set_tests_properties(tst_chatpage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Packaging. Neither half of it is an option to set: whether Qt is static comes
# from the kit CMAKE_PREFIX_PATH points at, and the format is picked at
# `cpack -G` time. A shared Qt is the case that needs work, because the DLLs,
# the qwindows platform plugin and the QML modules are not link-time
# dependencies the exe records - windeployqt has to go and find them, and
# without them the app dies at startup saying nothing. Android is excluded
# outright: there androiddeployqt and gradle own packaging, and the deliverable
# is an apk rather than a staged directory.
if(NOT ANDROID)
install(TARGETS quackchat RUNTIME DESTINATION . BUNDLE DESTINATION .)

# Where a desktop entry and a hicolor icon mean something. By name rather than
# the usual `UNIX AND NOT APPLE`, which is also true of Android and Cygwin.
if(CMAKE_SYSTEM_NAME MATCHES "Linux|.*BSD|DragonFly|GNU")
    install(FILES icons/quackchat.desktop DESTINATION share/applications)
    install(FILES icons/quack.svg DESTINATION share/icons/hicolor/scalable/apps
            RENAME quackchat.svg)
endif()

get_target_property(_qt_core_type Qt6::Core TYPE)
if(WIN32 AND NOT _qt_core_type STREQUAL "STATIC_LIBRARY")
    # windeployqt ships as a .exe even in a kit meant for cross-building, so a
    # cross build has to route it through an emulator.
    if(CMAKE_CROSSCOMPILING)
        find_program(WINE_EXECUTABLE NAMES wine
            DOC "runs the Windows-only windeployqt when cross-compiling")
        set(_deploy_launcher "${WINE_EXECUTABLE}")
        # By path, not find_program: the .exe comes out of Qt's archive without
        # the executable bit, so find_program will never match it. Not a cache
        # entry either - set(CACHE) will not overwrite one, so a NOTFOUND from an
        # earlier configure would stick and silently skip deployment.
        if(NOT WINDEPLOYQT_EXECUTABLE)
            set(WINDEPLOYQT_EXECUTABLE "${QT6_INSTALL_PREFIX}/bin/windeployqt.exe")
        endif()
    else()
        set(_deploy_launcher "")
        find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/bin" PATH_SUFFIXES bin
            DOC "Qt's Windows deployment tool")
    endif()

    if(EXISTS "${WINDEPLOYQT_EXECUTABLE}"
            AND (NOT CMAKE_CROSSCOMPILING OR WINE_EXECUTABLE))
        message(STATUS "Staging Qt's runtime with ${WINDEPLOYQT_EXECUTABLE}")
        install(CODE "
            execute_process(COMMAND ${_deploy_launcher} \"${WINDEPLOYQT_EXECUTABLE}\"
                    --qmldir \"${CMAKE_CURRENT_SOURCE_DIR}\"
                    --no-translations --no-system-d3d-compiler --no-opengl-sw
                    \"\${CMAKE_INSTALL_PREFIX}/quackchat.exe\"
                RESULT_VARIABLE _rc)
            if(NOT _rc EQUAL 0)
                message(FATAL_ERROR \"windeployqt failed (\${_rc}); the staged \"
                    \"tree would be missing Qt's DLLs and plugins\")
            endif()")
    else()
        message(WARNING
            "windeployqt not usable, so `cpack` will produce a tree without Qt's "
            "runtime. Install wine (cross builds) or pass -DWINDEPLOYQT_EXECUTABLE.")
    endif()

    # windeployqt stages the gcc runtime *Qt* was built with, but the exe is
    # linked by this toolchain and imports its symbols - three of them
    # (__cxa_thread_atexit, std::__get_once_call{,able}) are absent from a
    # libstdc++-6.dll that old, and the app would fail to load. libssp-0.dll is
    # not staged at all, since only tacky's -lssp wants it. Copying ours in
    # afterwards covers both: install() runs in declaration order, so this lands
    # after windeployqt and overwrites it, and newer is the safe direction - Qt's
    # own DLLs resolve fine against a runtime later than the one they were built
    # against. To re-check the staged tree after a toolchain change, diff
    # `objdump -p` imports against exports for every binary in it.
    set(_mingw_runtime_dir "${MINGW_SYSROOT}/bin")
    foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libssp-0.dll)
        if(EXISTS "${_mingw_runtime_dir}/${_dll}")
            install(FILES "${_mingw_runtime_dir}/${_dll}" DESTINATION .)
        else()
            message(WARNING "${_dll} not found in ${_mingw_runtime_dir}; the "
                "package will carry Qt's copy, which the exe may outrun.")
        endif()
    endforeach()
endif()

set(CPACK_PACKAGE_NAME quackchat)
set(CPACK_PACKAGE_VENDOR quackchat)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "An XMPP chat client")
set(CPACK_PACKAGE_INSTALL_DIRECTORY quackchat)
set(CPACK_PACKAGE_EXECUTABLES quackchat quackchat)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
# ZIP is the portable-folder build and needs no host tooling, so it stays the
# default; NSIS wants makensis on PATH and is opt-in via `cpack -G NSIS`.
set(CPACK_GENERATOR ZIP)
set(CPACK_NSIS_PACKAGE_NAME quackchat)
set(CPACK_NSIS_DISPLAY_NAME quackchat)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
include(CPack)
endif()
