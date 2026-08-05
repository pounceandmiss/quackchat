cmake_minimum_required(VERSION 3.21)
project(quack_qml VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED
    COMPONENTS Core Gui Qml Quick QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.5)

# tacky has its own (non-CMake) make, so we find what it drops in <root>/dist
# instead of building it. Point at a checkout with -DTACKY_ROOT=<dir> or
# $TACKY_ROOT, or name the archive itself with -DTACKY_LIB=<file>.
if(ANDROID)
    # bionic arm64 archive; tacky builds it in Docker via `make android-lib`
    set(_tacky_lib_file libtacky-android.a)
    set(_tacky_make android-lib)
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
target_link_libraries(tacky INTERFACE dl z m)
if(NOT ANDROID)
    # bionic has pthreads in libc
    target_link_libraries(tacky INTERFACE pthread)
endif()

qt_add_library(quack STATIC)
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
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
        AppShell.qml
        AccountRail.qml
        AddAccountSheet.qml
        AccountSettingsPage.qml
        TrustPicker.qml
        ConversationsPage.qml
        ChatPage.qml
        ChatBubble.qml
        Avatar.qml
        IconButton.qml
        AppWindows.qml
        Theme.qml
    SOURCES
        backend/AppController.cpp backend/AppController.h
        backend/TackyBackend.cpp backend/TackyBackend.h
        backend/ChatModel.cpp backend/ChatModel.h
        backend/ChatListModel.cpp backend/ChatListModel.h
        backend/AccountsModel.cpp backend/AccountsModel.h
        backend/AccountSettings.cpp backend/AccountSettings.h
        backend/OmemoDevicesModel.cpp backend/OmemoDevicesModel.h
        backend/AvatarController.cpp backend/AvatarController.h
        backend/AvatarSink.h)
target_include_directories(quack PUBLIC backend "${TACKY_INCLUDE}")
# Core/Qml PUBLIC so the headless tests get the headers without linking QtGui.
target_link_libraries(quack
    PUBLIC Qt6::Core Qt6::Qml
    PRIVATE Qt6::Quick Qt6::QuickControls2 tacky)

qt_add_executable(quackchat backend/main.cpp backend/AvatarImageProvider.cpp)
target_link_libraries(quackchat PRIVATE
    quack Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
if(ANDROID)
    # libtacky-android.a is built against API 30 bionic
    set_target_properties(quackchat PROPERTIES QT_ANDROID_MIN_SDK_VERSION 30)
endif()

if(NOT ANDROID)
enable_testing()

foreach(t tst_backend tst_chatlistmodel tst_chatmodel tst_accountsmodel
          tst_omemodevices tst_accountsettings tst_integration)
    qt_add_executable(${t} backend/${t}.cpp)
    target_link_libraries(${t} PRIVATE quack Qt6::Test)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# needs QtGui/QtQuick for the image provider response
qt_add_executable(tst_avatar backend/tst_avatar.cpp backend/AvatarImageProvider.cpp)
target_link_libraries(tst_avatar PRIVATE
    quack Qt6::Test Qt6::Gui Qt6::Quick)
add_test(NAME tst_avatar COMMAND tst_avatar)
set_tests_properties(tst_avatar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_qmlload backend/tst_qmlload.cpp)
target_link_libraries(tst_qmlload PRIVATE
    quack Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
add_test(NAME tst_qmlload COMMAND tst_qmlload)
set_tests_properties(tst_qmlload PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
