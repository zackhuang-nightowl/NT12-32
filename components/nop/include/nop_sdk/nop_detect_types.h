/**
 * @file nop_detect_types.h
 * @brief The single source of truth for the NOP detection / trigger / event
 *        type vocabulary. Handlers, config, and firmware must use this enum and
 *        its name<->id helpers instead of scattering string literals like
 *        "human" / "pixelChange" across files.
 *
 * The name strings are the exact NOP wire tokens.
 */
#ifndef NOP_SDK_DETECT_TYPES_H
#define NOP_SDK_DETECT_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/** Detection / trigger / event types. Values are an ABI (stored in config);
 *  append new types before NOP_DETECT_TYPE_MAX, never renumber. */
typedef enum nop_detect_type {
    NOP_DETECT_PIXEL_CHANGE = 0,   /**< "pixelChange" */
    NOP_DETECT_MOTION,             /**< "motion" */
    NOP_DETECT_HUMAN,              /**< "human" */
    NOP_DETECT_VEHICLE,            /**< "vehicle" */
    NOP_DETECT_ANIMAL,             /**< "animal" */
    NOP_DETECT_PACKAGE,            /**< "package" */
    NOP_DETECT_FACE,               /**< "face" */
    NOP_DETECT_PIR,                /**< "pir" */
    NOP_DETECT_DOORBELL_RING,      /**< "doorbellRing" */
    NOP_DETECT_LINE_CROSS,         /**< "lineCross" */
    NOP_DETECT_FIELD_INTRUSION,    /**< "fieldIntrusion" */
    NOP_DETECT_BABY_CRY,           /**< "babyCry" */
    NOP_DETECT_GUN_SHOT,           /**< "gunShot" */
    NOP_DETECT_FIRE_ALARM,         /**< "fireAlarm" */
    NOP_DETECT_FACIAL_RECOGNITION, /**< "facialRecognition" */
    NOP_DETECT_OBJECT_DETECTION,   /**< "objectDetection" */
    NOP_DETECT_TYPE_MAX
} nop_detect_type_t;

/** @return the wire token for @p type, or "" if out of range. */
const char *nop_detect_type_name(nop_detect_type_t type);

/** @return the type id for wire token @p name, or NOP_DETECT_TYPE_MAX if unknown. */
nop_detect_type_t nop_detect_type_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_DETECT_TYPES_H */
