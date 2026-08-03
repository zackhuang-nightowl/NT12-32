# na51090 (NT98633) aarch64 交叉工具链 — Novatek/Buildroot GCC 8.4.0 (glibc 2.30)
# 用法：cmake -S . -B build_arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-ca53.cmake \
#              -DNVR_WITH_ONBOARD=ON -DBSP_ROOT=/home/zack/NT12-32/NT98633/na51090_linux_sdk
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 工具链根（可用 -DNVR_TOOLCHAIN_ROOT= 覆盖）
if(NOT DEFINED NVR_TOOLCHAIN_ROOT)
    set(NVR_TOOLCHAIN_ROOT /home/zack/NT12-32/NT98633/toolchain/aarch64-ca53-linux-gnueabihf-8.4.01)
endif()
set(_tc ${NVR_TOOLCHAIN_ROOT}/bin/aarch64-ca53-linux-gnu-)

set(CMAKE_C_COMPILER   ${_tc}gcc)
set(CMAKE_CXX_COMPILER ${_tc}g++)
set(CMAKE_AR           ${_tc}ar CACHE FILEPATH "")
set(CMAKE_RANLIB       ${_tc}ranlib CACHE FILEPATH "")
set(CMAKE_STRIP        ${_tc}strip CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH ${NVR_TOOLCHAIN_ROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
# 头文件允许用主机端可移植头（如 sqlite3.h）——目标 sysroot 缺失时仍可编译静态库
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# 标记：交叉构建下，各组件对 sqlite3/curl/ssl 采用「有则链，无则仅用头，链接留给最终 exe」
set(NVR_CROSS 1 CACHE INTERNAL "cross build")
# 主机可移植头（sqlite3.h / curl/ / openssl/）——目标 sysroot 缺失时供交叉编译取声明。
# 只用于编译静态库；最终 exe 链接仍需目标 sysroot 的 .so（设备 rootfs 自带）。
set(NVR_CROSS_SYSINC /usr/include CACHE PATH "host portable headers for cross compile")
# TUTK 随包提供的 aarch64 预编译库（ssl/crypto/curl）——可直接给交叉链接用。
set(NVR_TUTK_AARCH64_LIB
    ${CMAKE_CURRENT_LIST_DIR}/../third_party/tutk_sdk/Lib/Linux/ArmCortexA53_NT98633_8.4.0
    CACHE PATH "tutk aarch64 prebuilt libs (ssl/crypto/curl)")
