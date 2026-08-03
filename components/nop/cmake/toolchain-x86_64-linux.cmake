# Toolchain file: native x86_64 Linux (host development / CI baseline).
# This is the default when no toolchain file is supplied; provided explicitly so
# every target platform has a matching toolchain file in this directory.
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64-linux.cmake

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER gcc)
endif()
