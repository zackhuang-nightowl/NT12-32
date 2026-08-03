/**
 * @file onvif_map_utils.c
 * @brief Shared non-geometry conversions for the NOP->ONVIF mappers. Pure C, no
 *        ONVIF dependency — always compiled (no NOP_ONVIF_MAP gate needed).
 */
#include "onvif/mapping/onvif_map_utils.h"
#include "onvif/mapping/nop_onvif_map.h"

#include <stdio.h>
#include <string.h>

double onvif_map_clampd(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float onvif_map_level_to_unit(double level, double scale_max)
{
    if (level <= 0.0 || scale_max <= 0.0)
        return 0.0f;
    return (float)onvif_map_clampd(level / scale_max, 0.0, 1.0);
}

void onvif_map_dir_to_velocity(const char *dir, float mag,
                               float *pan, float *tilt, float *zoom)
{
    float p = 0.0f, t = 0.0f, z = 0.0f;
    if (dir) {
        if      (!strcmp(dir, "up"))        { t =  mag; }
        else if (!strcmp(dir, "down"))      { t = -mag; }
        else if (!strcmp(dir, "left"))      { p = -mag; }
        else if (!strcmp(dir, "right"))     { p =  mag; }
        else if (!strcmp(dir, "leftUp"))    { p = -mag; t =  mag; }
        else if (!strcmp(dir, "leftDown"))  { p = -mag; t = -mag; }
        else if (!strcmp(dir, "rightUp"))   { p =  mag; t =  mag; }
        else if (!strcmp(dir, "rightDown")) { p =  mag; t = -mag; }
        else if (!strcmp(dir, "zoomIn"))    { z =  mag; }
        else if (!strcmp(dir, "zoomOut"))   { z = -mag; }
    }
    if (pan)  *pan  = p;
    if (tilt) *tilt = t;
    if (zoom) *zoom = z;
}

nop_status_t onvif_map_rc(int onvif_rc)
{
    return onvif_rc == 0 ? NOP_OK : NOP_ERR_IO;
}

const char *onvif_map_int_token(int id, char *buf, unsigned size)
{
    if (buf && size > 0)
        snprintf(buf, size, "%d", id);
    return buf;
}

nop_camera_backend_t nop_onvif_map_classify_backend(const char *scopes)
{
    /* A NightOwl-native camera advertises a "nopVersion" scope item; drive it
     * natively over NOP. Anything else is a third-party ONVIF camera. */
    if (scopes && strstr(scopes, "nopVersion"))
        return NOP_BACKEND_NOP;
    return NOP_BACKEND_ONVIF;
}
