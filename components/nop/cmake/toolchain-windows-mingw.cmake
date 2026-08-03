# Toolchain file: Windows x64 cross-build via MinGW-w64 (x86_64-w64-mingw32).
#
# Usage:
#   cmake -S . -B build-win \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows-mingw.cmake \
#     -DNOP_OSAL_PORT=windows

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CROSS_PREFIX "x86_64-w64-mingw32-")
set(CMAKE_C_COMPILER   "${CROSS_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${CROSS_PREFIX}g++")
set(CMAKE_RC_COMPILER  "${CROSS_PREFIX}windres")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
