/**
 * @file onvif_map_json.c
 * @brief NOP JSON <-> onvif_coord geometry marshalling (see onvif_map_json.h).
 *        Pure C, no ONVIF dependency — always compiled.
 */
#include "onvif/mapping/onvif_map_json.h"

int onvif_map_json_to_cells(const nop_json_t *arr, nop_coord_cell_t *out, int max)
{
    int n = 0, size, i;

    if (!arr || !nop_json_is_arr(arr) || !out || max <= 0)
        return 0;
    size = nop_json_arr_size(arr);
    for (i = 0; i < size && n < max; i++) {
        const nop_json_t *pt = nop_json_arr_at(arr, i);
        if (pt && nop_json_is_arr(pt) && nop_json_arr_size(pt) >= 2) {
            out[n].col = (int)nop_json_as_num(nop_json_arr_at(pt, 0), 0);
            out[n].row = (int)nop_json_as_num(nop_json_arr_at(pt, 1), 0);
            n++;
        }
    }
    return n;
}

nop_json_t *onvif_map_cells_to_json(const nop_coord_cell_t *cells, int count)
{
    nop_json_t *arr = nop_json_arr();
    int         i;

    if (!arr)
        return NULL;
    for (i = 0; i < count; i++) {
        nop_json_t *pt = nop_json_arr();
        if (!pt)
            break;
        nop_json_arr_push_int(pt, cells[i].col);
        nop_json_arr_push_int(pt, cells[i].row);
        nop_json_arr_push(arr, pt);
    }
    return arr;
}
