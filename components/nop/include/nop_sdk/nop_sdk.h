/**
 * @file nop_sdk.h
 * @brief Single aggregate entry header for the NOP SDK public ABI.
 *
 * NOP SDK — a cross-platform, toolchain-agnostic C SDK implementing the NOP
 * protocol (envelope + router + capability gating) for NightOwl IPC/NVR
 * devices. The protocol core does no networking; transports (HTTP server, TUTK
 * P2P, mock) are injected via nop_transport_if.
 */
#ifndef NOP_SDK_H
#define NOP_SDK_H

#include "nop_sdk/nop_version.h"
#include "nop_sdk/nop_err.h"
#include "nop_sdk/nop_log.h"
#include "nop_sdk/nop_types.h"
#include "nop_sdk/nop_transport.h"
#include "nop_sdk/nop_app.h"

#include "nop_sdk/osal/osal.h"

#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_system.h"
#include "nop_sdk/hal/hal_video.h"
#include "nop_sdk/hal/hal_ptz.h"
#include "nop_sdk/hal/hal_light.h"

#endif /* NOP_SDK_H */
