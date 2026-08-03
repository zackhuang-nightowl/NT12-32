/**
 * @file nop_types.h
 * @brief Common public types shared across the NOP SDK ABI.
 */
#ifndef NOP_SDK_TYPES_H
#define NOP_SDK_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Device role; drives default capability/build gating. */
typedef enum nop_role {
    NOP_ROLE_IPC = 0,   /**< standalone IP camera / doorbell */
    NOP_ROLE_NVR        /**< video recorder with channels    */
} nop_role_t;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_TYPES_H */
