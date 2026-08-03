# NOP SDK

A self-contained, cross-platform C SDK that implements the **NOP protocol**
(envelope → router → capability gating → business handlers) for NightOwl
IPC/NVR devices. It builds with any toolchain via CMake — like the TUTK SDK,
the same sources target x86_64, ARM uClibc (Fullhan), Windows, and more.

The protocol core does **no networking**: transports (8089 HTTP server, TUTK
P2P, in-memory mock) are injected through `nop_transport_if`, so the whole core
is fully testable on the host with zero hardware.

> Status: **N0 milestone complete** — protocol skeleton (base + envelope +
> router + capability registry + mock transport + HAL stub + first handler
> batches), buildable and unit-tested on x86. See
> [docs/实现流程报告.md](docs/实现流程报告.md).

## Layout

```
nop_sdk/
├── include/nop_sdk/        Public ABI (stable C API; the only headers customers include)
│   ├── nop_sdk.h           Single aggregate entry header
│   ├── nop_app.h           One-stop façade (create / dispatch / destroy)
│   ├── nop_err.h nop_log.h nop_caps.h nop_types.h nop_transport.h nop_version.h
│   ├── hal/                Hardware abstraction interfaces (firmware implements)
│   └── osal/               OS porting contract (mutex / time)
├── src/                    Private implementation (not installed)
│   ├── base/               err · log · mem · json (cJSON facade) · map
│   ├── nop/                envelope · router · longpoll
│   ├── capability/         cap_registry (single source of truth)
│   ├── transport/          mock backend
│   ├── hal/                hal_registry
│   ├── business/caps/      command handlers, one file per capability group
│   └── app/                nop_app façade
├── ports/                  Porting implementations
│   ├── posix/ windows/     OSAL ports (linux_uclibc reuses posix)
│   └── stub/               Print-stub HAL (runs with no hardware)
├── third_party/cJSON/      Vendored JSON library (isolated behind base/nop_json)
├── tests/                  ctest unit + contract tests
├── examples/ipc_demo.c     Minimal embed example
├── cmake/                  Cross-compile toolchain files
└── docs/                   Porting guide, command matrix, reports
```

## Build

```sh
./build.sh             # native host: configure + build + run tests
./build.sh arm         # cross-compile for Fullhan arm-uClibc
./build.sh windows     # cross-compile for Windows (MinGW-w64)
```

Or drive CMake directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
(cd build && ctest --output-on-failure)
cmake --install build --prefix /your/staging   # -> include/ + lib/libnopcore.{a,so}
```

### Key options

| Option | Default | Meaning |
|---|---|---|
| `NOP_OSAL_PORT` | `posix` | `posix` \| `linux_uclibc` \| `windows` |
| `NOP_BUILD_STATIC` / `NOP_BUILD_SHARED` | ON / ON | which library forms to build |
| `NOP_BUILD_TESTS` / `NOP_BUILD_EXAMPLES` | ON / ON | host test + example builds |
| `NOP_WITH_HAL_STUB` | ON | bundle the print-stub HAL (no hardware needed) |
| `NOP_WITH_ONVIF` | OFF | built-in ONVIF client (vendored Happytimesoft lib; needs a C++ compiler + OpenSSL) |
| `NOP_WITH_TUTK` | OFF | built-in TUTK P2P backend (requires the TUTK SDK) |

> **ONVIF**: with `-DNOP_WITH_ONVIF=ON` the SDK also builds the ONVIF client
> (LAN discovery + RTSP stream-URI fetch) behind the C API `nop_sdk/nop_onvif.h`,
> plus the `onvif_client_demo` example. The vendored library is C++, so a C++
> toolchain (`g++`/MSVC) and OpenSSL are required for that option only — the
> default C-only build is unaffected. See [docs/使用报告.md](docs/使用报告.md) §8.

## Use it

```c
#include "nop_sdk/nop_sdk.h"

/* 1. Firmware registers its hardware tables (or use the stub). */
hal_register(HAL_SYSTEM, &my_system_interface);
hal_register(HAL_VIDEO,  &my_video_interface);

/* 2. Create the app — capabilities light up from the registered HALs. */
nop_app_config_t config = { .role = NOP_ROLE_IPC, .auto_caps = 1 };
nop_app_t *app = nop_app_create(&config);

/* 3. Feed request envelopes from any transport; get response envelopes. */
char *response = NULL;
nop_app_dispatch(app, "{\"func\":\"getDeviceInfo\",\"args\":{}}", &response);
printf("%s\n", response);          /* {"statusCode":200,...} */
nop_app_free_response(response);

nop_app_destroy(app);
```

See [docs/使用报告.md](docs/使用报告.md) for the full integration guide and
[docs/PORTING.md](docs/PORTING.md) for porting to a new chip/toolchain.
