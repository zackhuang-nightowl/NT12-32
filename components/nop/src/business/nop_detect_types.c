/**
 * @file nop_detect_types.c
 * @brief Detection-type enum <-> wire-token table. One table, used everywhere.
 */
#include "nop_sdk/nop_detect_types.h"

#include <string.h>

/* Indexed by nop_detect_type_t — keep in lockstep with the enum. */
static const char *const k_detect_type_names[NOP_DETECT_TYPE_MAX] = {
    "pixelChange",
    "motion",
    "human",
    "vehicle",
    "animal",
    "package",
    "face",
    "pir",
    "doorbellRing",
    "lineCross",
    "fieldIntrusion",
    "babyCry",
    "gunShot",
    "fireAlarm",
    "facialRecognition",
    "objectDetection"
};

const char *nop_detect_type_name(nop_detect_type_t type)
{
    if (type < 0 || type >= NOP_DETECT_TYPE_MAX)
        return "";
    return k_detect_type_names[type];
}

nop_detect_type_t nop_detect_type_from_name(const char *name)
{
    int i;
    if (!name)
        return NOP_DETECT_TYPE_MAX;
    for (i = 0; i < NOP_DETECT_TYPE_MAX; i++)
        if (strcmp(name, k_detect_type_names[i]) == 0)
            return (nop_detect_type_t)i;
    return NOP_DETECT_TYPE_MAX;
}
