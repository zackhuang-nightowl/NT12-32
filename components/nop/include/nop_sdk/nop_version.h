/**
 * @file nop_version.h
 * @brief NOP SDK semantic version. ABI break => bump MAJOR.
 */
#ifndef NOP_SDK_VERSION_H
#define NOP_SDK_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define NOP_SDK_VERSION_MAJOR 0
#define NOP_SDK_VERSION_MINOR 1
#define NOP_SDK_VERSION_PATCH 0

#define NOP_SDK_VERSION_STR "0.1.0"

/** API version advertised over the NOP protocol (X_NightOwl_getAPIVersion). */
#define NOP_API_VERSION_STR "1.56.0912"

/** @return human-readable SDK version string, e.g. "0.1.0". */
const char *nop_sdk_version(void);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_VERSION_H */
