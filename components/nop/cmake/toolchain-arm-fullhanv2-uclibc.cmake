# Toolchain file: Fullhan FH8852/FH8856 (arm-fullhanv2-linux-uclibcgnueabi).
# Matches the toolchain shipped in ../fuhan_sdk/board_support/toolchain.
#
# Usage:
#   cmake -S . -B build-arm \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-fullhanv2-uclibc.cmake \
#     -DNOP_OSAL_PORT=linux_uclibc \
#     -DTOOLCHAIN_ROOT=/opt/arm-fullhanv2-linux-uclibcgnueabi

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Override -DTOOLCHAIN_ROOT or set CROSS_COMPILE in the environment.
if(NOT DEFINED TOOLCHAIN_ROOT)
    set(TOOLCHAIN_ROOT "/opt/arm-fullhanv2-linux-uclibcgnueabi")
endif()
set(CROSS_PREFIX "arm-fullhanv2-linux-uclibcgnueabi-")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/bin/${CROSS_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/${CROSS_PREFIX}g++")
set(CMAKE_AR           "${TOOLCHAIN_ROOT}/bin/${CROSS_PREFIX}ar")

set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# uClibc targets are typically size-constrained.
set(CMAKE_C_FLAGS_INIT "-Os -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")
