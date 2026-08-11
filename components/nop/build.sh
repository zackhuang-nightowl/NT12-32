#!/bin/sh
# Convenience build script for Linux/macOS hosts and cross builds.
#
#   ./build.sh                      # native x86_64 host build + run tests
#   ./build.sh arm                  # cross build for Fullhan arm-uClibc
#   ./build.sh windows              # cross build for Windows via MinGW-w64
#
# Environment overrides:
#   BUILD_DIR   (default: build)
#   TOOLCHAIN_ROOT  (arm only; path to the cross toolchain)
set -e

TARGET="${1:-host}"
BUILD_DIR="${BUILD_DIR:-build}"
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo"

case "$TARGET" in
    host)
        ;;
    arm)
        BUILD_DIR="${BUILD_DIR:-build-arm}"
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-fullhanv2-uclibc.cmake -DNOP_OSAL_PORT=linux_uclibc -DNOP_BUILD_TESTS=OFF"
        ;;
    windows)
        BUILD_DIR="${BUILD_DIR:-build-win}"
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows-mingw.cmake -DNOP_OSAL_PORT=windows -DNOP_BUILD_TESTS=OFF"
        ;;
    *)
        echo "unknown target: $TARGET (use: host | arm | windows)" >&2
        exit 1
        ;;
esac

echo "==> configuring ($TARGET) in $BUILD_DIR"
cmake -S . -B "$BUILD_DIR" $CMAKE_ARGS
echo "==> building"
cmake --build "$BUILD_DIR" -j

if [ "$TARGET" = "host" ]; then
    echo "==> running tests"
    ( cd "$BUILD_DIR" && ctest --output-on-failure )
fi
echo "==> done: artifacts in $BUILD_DIR"
