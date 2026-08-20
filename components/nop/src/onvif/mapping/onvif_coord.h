/**
 * @file onvif_coord.h  (internal)
 * @brief Pure coordinate transforms between the NOP and ONVIF coordinate
 *        systems. NO ONVIF/C++ dependency — this translation unit compiles and
 *        links standalone (see tests/test_onvif_coord.c), which is why it lives
 *        below the C ABI boundary and never includes a vendored ONVIF header.
 *
 * Two coordinate spaces, per NOPMappingONVIF.md §1:
 *   - ONVIF: normalized [-1, 1], origin = frame center, +x right, +y UP.
 *   - NOP "thousandths" (Line/Area): integer [0, 1000], origin = top-left,
 *     +x right, +y DOWN. Used by AI_*LineCrossDetect / *FieldIntrusionDetect.
 *   - NOP "macroblock grid" (Privacy/Activity): integer cell [col,row], origin =
 *     top-left, default W=22 columns, H=18 rows. Used by *PrivacyZone /
 *     *TriggerActivityZone.
 *
 * Empty geometry (NOPMappingONVIF.md §9): ONVIF stores every point as (-1,1);
 * GET mappers return NOP [] (line/area/privacyZonePoints group). SET with NOP []
 * writes (-1,1) for each required point.
 *
 * All formulas are the exact ones from the spec's "公式汇总表" (§1.3).
 */
#ifndef NOP_ONVIF_COORD_H
#define NOP_ONVIF_COORD_H

#ifdef __cplusplus
extern "C" {
#endif

/** Default macroblock grid dimensions (spec default). */
#define NOP_COORD_DEFAULT_W 22
#define NOP_COORD_DEFAULT_H 18

/** Sentinel returned by the bounds helpers when the input point set is empty.
 *  Mappers translate this to a domain-specific default (disable rule / full
 *  frame), never a degenerate zero-area polygon — see NOPMappingONVIF.md §9 ps. */
#define NOP_COORD_EMPTY (-1)

/** ONVIF placeholder for unconfigured point geometry (NOPMappingONVIF.md §9). */
#define NOP_COORD_UNCONFIGURED_X (-1.0f)
#define NOP_COORD_UNCONFIGURED_Y ( 1.0f)
#define NOP_COORD_UNCONFIGURED_EPS 0.001f

/** A normalized ONVIF point: x,y in [-1,1], center origin, +y up. */
typedef struct nop_coord_pointf {
    float x;
    float y;
} nop_coord_pointf_t;

/** A NOP macroblock cell [col,row], top-left origin. */
typedef struct nop_coord_cell {
    int col;
    int row;
} nop_coord_cell_t;

/* ======================================================================== */
/* Line / Area points:  thousandths [0,1000] (top-left, y-down)             */
/*                   <-> normalized [-1,1]   (center,   y-up)               */
/* ======================================================================== */

/**
 * ONVIF normalized point -> NOP thousandths. Rounds to nearest integer and
 * clamps each axis to [0, 1000].
 *   NOP_x = round((ONVIF_x + 1) * 500),  NOP_y = round((1 - ONVIF_y) * 500)
 */
void nop_coord_norm_to_thousandths(float onx, float ony, int *nx, int *ny);

/**
 * NOP thousandths point -> ONVIF normalized (exact inverse of the above).
 *   ONVIF_x = NOP_x/500 - 1,  ONVIF_y = 1 - NOP_y/500
 */
void nop_coord_thousandths_to_norm(int nx, int ny, float *onx, float *ony);

/**
 * True when @p count<=0 or every ONVIF point is the unconfigured sentinel (-1,1).
 * GET mappers use this to emit NOP [] instead of converted coordinates.
 */
int nop_coord_norm_points_unconfigured(const float *xs, const float *ys, int count);

/** Convenience wrapper for @ref nop_coord_pointf_t arrays. */
int nop_coord_points_unconfigured(const nop_coord_pointf_t *pts, int count);

/** Fill @p count points with the unconfigured sentinel (-1,1) for ONVIF SET. */
void nop_coord_fill_unconfigured(float *xs, float *ys, int count);

/* ======================================================================== */
/* ActiveCells / MotionInCells — PackBits (TIFF 6.0 / ONVIF Analytics)    */
/* ======================================================================== */

/**
 * Expand PackBits-compressed ONVIF cell bitmask into @p out (max @p out_max bytes).
 * @return unpacked length (>0), 0 if empty input, negative on error.
 */
int nop_coord_packbits_decode(const unsigned char *in, int in_len,
                              unsigned char *out, int out_max);

/**
 * Compress raw cell bitmask with PackBits (TIFF 6.0).
 * @return packed length (>0), negative on error.
 */
int nop_coord_packbits_encode(const unsigned char *in, int in_len,
                              unsigned char *out, int out_max);

/* ======================================================================== */
/* Privacy / Activity zones:  NOP macroblock grid <-> ONVIF normalized rect */
/* ======================================================================== */

/**
 * Axis-aligned bounds (min/max col,row) over a NOP cell list.
 * @return 0 on success (c0/c1/r0/r1 filled), NOP_COORD_EMPTY if @p count <= 0.
 */
int nop_coord_cells_bounds(const nop_coord_cell_t *cells, int count,
                           int *c0, int *c1, int *r0, int *r1);

/**
 * NOP cell rectangle [c0..c1] x [r0..r1] -> ONVIF axis-aligned 4-corner polygon.
 * Corner order matches the spec (左上→左下→右下→右上): TL, BL, BR, TR.
 *   x_left  = -1 + c0*2/W     x_right  = -1 + (c1+1)*2/W
 *   y_top   =  1 - r0*2/H     y_bottom =  1 - (r1+1)*2/H
 * @p W,@p H must be > 0 (pass NOP_COORD_DEFAULT_W/H by default).
 */
void nop_coord_grid_to_aabb(int c0, int c1, int r0, int r1, int W, int H,
                            nop_coord_pointf_t out_pts[4]);

/**
 * ONVIF AABB (min/max, normalized) -> NOP cell range, center-point method:
 *   c0 = ceil((xmin+1)*W/2 - 0.5)   c1 = floor((xmax+1)*W/2 - 0.5)
 *   r0 = ceil((1-ymax)*H/2 - 0.5)   r1 = floor((1-ymin)*H/2 - 0.5)
 * Results are clamped to the valid grid [0,W-1] x [0,H-1].
 */
void nop_coord_aabb_to_grid(float xmin, float xmax, float ymin, float ymax,
                            int W, int H, int *c0, int *c1, int *r0, int *r1);

/**
 * Axis-aligned bounds (min/max x,y) over an ONVIF point list.
 * @return 0 on success (xmin/xmax/ymin/ymax filled), NOP_COORD_EMPTY if empty.
 */
int nop_coord_points_bounds(const nop_coord_pointf_t *pts, int count,
                            float *xmin, float *xmax, float *ymin, float *ymax);

/**
 * Expand the filled cell rectangle [c0..c1] x [r0..r1] into a row-major
 * [col,row] list. Writes at most @p max cells.
 * @return the number of cells written (>= 0).
 */
int nop_coord_grid_rect_expand(int c0, int c1, int r0, int r1,
                               nop_coord_cell_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* NOP_ONVIF_COORD_H */
