/**
 * @file onvif_map_json.h  (internal)
 * @brief Shared bridge between NOP JSON point arrays and onvif_coord geometry
 *        types. Reused by the geometry domains (§7 privacy, §8 activity zone,
 *        §9 line/field) so the JSON<->cell/point marshalling is written once.
 *        Pure C: knows nop_json + onvif_coord, but no ONVIF headers. Keeps
 *        onvif_coord.* JSON-free and onvif_map_utils.* geometry-free.
 */
#ifndef NOP_ONVIF_MAP_JSON_H
#define NOP_ONVIF_MAP_JSON_H

#include "base/nop_json.h"
#include "onvif/mapping/onvif_coord.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a NOP "[[col,row],...]" array into cells. Skips malformed points.
 * @return the number of cells written (<= @p max).
 */
int onvif_map_json_to_cells(const nop_json_t *arr, nop_coord_cell_t *out, int max);

/** Build a new NOP "[[col,row],...]" array from @p count cells. */
nop_json_t *onvif_map_cells_to_json(const nop_coord_cell_t *cells, int count);

#ifdef __cplusplus
}
#endif

#endif /* NOP_ONVIF_MAP_JSON_H */
