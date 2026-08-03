# Porting Guide

Bringing the NOP SDK to a new chip/toolchain touches exactly three seams. The
protocol core never changes.

## 1. OSAL port (OS / platform)

The SDK calls only `include/nop_sdk/osal/osal.h` — never raw OS APIs. N0 needs a
recursive mutex and a monotonic clock.

- Reuse `ports/posix/osal_posix.c` for any POSIX libc (glibc **and** uClibc):
  build with `-DNOP_OSAL_PORT=posix` (or `linux_uclibc`, which maps to the same
  file).
- Windows: `ports/windows/osal_windows.c`, `-DNOP_OSAL_PORT=windows`.
- A bare-metal/RTOS target adds `ports/<rtos>/osal_<rtos>.c` implementing the
  same six functions, then a branch in the top `CMakeLists.txt`.

## 2. Cross-compile toolchain

Pick or add a file under `cmake/`:

```sh
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-fullhanv2-uclibc.cmake \
  -DTOOLCHAIN_ROOT=/opt/arm-fullhanv2-linux-uclibcgnueabi \
  -DNOP_OSAL_PORT=linux_uclibc -DNOP_BUILD_TESTS=OFF
cmake --build build-arm -j
```

To add a new SoC, copy a toolchain file and adjust `CMAKE_C_COMPILER` /
`CROSS_PREFIX`. The library has **no external link dependencies** beyond libc
(and pthread on POSIX), so cross-linking is trivial.

## 3. HAL implementation (hardware)

For each capability the device supports, fill the vtable from the matching
`include/nop_sdk/hal/hal_*.h` and register it **before** `nop_app_create`:

```c
static const hal_video_if my_video = { my_channel_count, my_start, my_stop, &my_ctx };
hal_register(HAL_VIDEO, &my_video);
```

Rules:

- A capability whose HAL is **not** registered automatically answers `501` for
  its commands — no crashes, no stubs required.
- With `auto_caps = 1`, registering `HAL_VIDEO/PTZ/LIGHT` lights up
  `CAP_STREAM/PTZ/LIGHT` respectively.
- Need to run before any hardware exists? Link `ports/stub` (default on) and
  call `hal_stub_register_all()` — see `examples/ipc_demo.c`.

## What you never touch

`src/nop/`, `src/capability/`, `src/base/` are platform-independent. They build
identically on every target and are covered by the host ctest suite, so a
porting bug is isolated to the OSAL port, the toolchain file, or a HAL table.
