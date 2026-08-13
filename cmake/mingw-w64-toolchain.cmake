# Cross-compiling to Windows with MinGW-w64. Kit locations differ per machine,
# so they come in on the command line and the sysroot stays overridable:
#
#   cmake -B build-win -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake \
#     -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/mingw_64" \
#     -DQT_HOST_PATH="$HOME/Qt/6.11.1/gcc_64"
#
# QT_HOST_PATH is not optional: moc, rcc, qmltyperegistrar and qmlcachegen all
# run during the build, and the ones in the Windows kit are .exe files this host
# cannot execute. It points at the native Qt of the *same* version.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TARGET x86_64-w64-mingw32 CACHE STRING "MinGW-w64 toolchain triple")
set(MINGW_SYSROOT "/usr/${MINGW_TARGET}" CACHE PATH "MinGW-w64 sysroot")

set(CMAKE_C_COMPILER ${MINGW_TARGET}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_TARGET}-g++)
set(CMAKE_RC_COMPILER ${MINGW_TARGET}-windres)

# CMAKE_PREFIX_PATH joins the root path so the Qt kit is searchable: with
# PACKAGE mode ONLY, a kit outside the roots is invisible to find_package.
# (libtacky is found with NO_CMAKE_FIND_ROOT_PATH instead - it is a prebuilt
# that lives in a tacky checkout, nowhere near either root.)
set(CMAKE_FIND_ROOT_PATH "${MINGW_SYSROOT}" ${CMAKE_PREFIX_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
