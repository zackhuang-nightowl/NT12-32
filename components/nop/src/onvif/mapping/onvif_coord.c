/**
 * @file onvif_coord.c
 * @brief NOP <-> ONVIF coordinate transforms (see onvif_coord.h). Pure C, no
 *        ONVIF dependency; math only.
 */
#include "onvif/mapping/onvif_coord.h"

#include <math.h>
#include <string.h>

/* Clamp helpers (kept local to avoid a shared dependency). */
static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ------------------------------------------------------------------------ */
/* Line / Area points                                                       */
/* ------------------------------------------------------------------------ */

void nop_coord_norm_to_thousandths(float onx, float ony, int *nx, int *ny)
{
    /* +0.5 rounding on non-negative products, then clamp to [0,1000]. */
    if (nx) *nx = clampi((int)((onx + 1.0) * 500.0 + 0.5), 0, 1000);
    if (ny) *ny = clampi((int)((1.0 - ony) * 500.0 + 0.5), 0, 1000);
}

void nop_coord_thousandths_to_norm(int nx, int ny, float *onx, float *ony)
{
    if (onx) *onx = (float)(nx / 500.0 - 1.0);
    if (ony) *ony = (float)(1.0 - ny / 500.0);
}

int nop_coord_norm_points_unconfigured(const float *xs, const float *ys, int count)
{
    int i;
    if (count <= 0)
        return 1;
    if (!xs || !ys)
        return 1;
    for (i = 0; i < count; i++) {
        if (fabsf(xs[i] - NOP_COORD_UNCONFIGURED_X) > NOP_COORD_UNCONFIGURED_EPS ||
            fabsf(ys[i] - NOP_COORD_UNCONFIGURED_Y) > NOP_COORD_UNCONFIGURED_EPS)
            return 0;
    }
    return 1;
}

int nop_coord_points_unconfigured(const nop_coord_pointf_t *pts, int count)
{
    int i;
    if (count <= 0 || !pts)
        return 1;
    for (i = 0; i < count; i++) {
        if (fabsf(pts[i].x - NOP_COORD_UNCONFIGURED_X) > NOP_COORD_UNCONFIGURED_EPS ||
            fabsf(pts[i].y - NOP_COORD_UNCONFIGURED_Y) > NOP_COORD_UNCONFIGURED_EPS)
            return 0;
    }
    return 1;
}

void nop_coord_fill_unconfigured(float *xs, float *ys, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        if (xs) xs[i] = NOP_COORD_UNCONFIGURED_X;
        if (ys) ys[i] = NOP_COORD_UNCONFIGURED_Y;
    }
}

int nop_coord_packbits_decode(const unsigned char *in, int in_len,
                              unsigned char *out, int out_max)
{
    int i = 0, o = 0;
    if (!in || in_len <= 0)
        return 0;
    if (!out || out_max <= 0)
        return -1;
    while (i < in_len) {
        int n = (signed char)in[i];
        if (n == -128) /* 0x80: EOD in some PackBits variants; ONVIF streams omit it */
            break;
        if (n >= 0) {
            int cnt = n + 1;
            if (i + cnt >= in_len)
                return -2;
            if (o + cnt > out_max)
                return -3;
            memcpy(out + o, in + i + 1, (size_t)cnt);
            o += cnt;
            i += 1 + cnt;
        } else {
            int cnt = 257 - n;
            if (i + 1 >= in_len)
                return -2;
            if (o + cnt > out_max)
                return -3;
            memset(out + o, in[i + 1], (size_t)cnt);
            o += cnt;
            i += 2;
        }
    }
    return o;
}

int nop_coord_packbits_encode(const unsigned char *in, int in_len,
                              unsigned char *out, int out_max)
{
    int i = 0, o = 0;
    if (!in || in_len <= 0)
        return 0;
    if (!out || out_max <= 0)
        return -1;
    while (i < in_len) {
        if (i + 2 < in_len && in[i] == in[i + 1] && in[i + 1] == in[i + 2]) {
            int j = i + 1;
            while (j < in_len && in[j] == in[i] && (j - i) < 128)
                j++;
            if (o + 2 > out_max)
                return -3;
            out[o++] = (unsigned char)(257 - (j - i));
            out[o++] = in[i];
            i = j;
        } else {
            int lit = i;
            i++;
            while (i < in_len) {
                if (i + 2 < in_len && in[i] == in[i + 1] && in[i + 1] == in[i + 2])
                    break;
                if ((i - lit) >= 128)
                    break;
                i++;
            }
            if (o + 1 + (i - lit) > out_max)
                return -3;
            out[o++] = (unsigned char)((i - lit) - 1);
            memcpy(out + o, in + lit, (size_t)(i - lit));
            o += (i - lit);
        }
    }
    return o;
}

/* ------------------------------------------------------------------------ */
/* Privacy / Activity zones                                                 */
/* ------------------------------------------------------------------------ */

int nop_coord_cells_bounds(const nop_coord_cell_t *cells, int count,
                           int *c0, int *c1, int *r0, int *r1)
{
    int i, cmin, cmax, rmin, rmax;

    if (!cells || count <= 0)
        return NOP_COORD_EMPTY;

    cmin = cmax = cells[0].col;
    rmin = rmax = cells[0].row;
    for (i = 1; i < count; i++) {
        if (cells[i].col < cmin) cmin = cells[i].col;
        if (cells[i].col > cmax) cmax = cells[i].col;
        if (cells[i].row < rmin) rmin = cells[i].row;
        if (cells[i].row > rmax) rmax = cells[i].row;
    }
    if (c0) *c0 = cmin;
    if (c1) *c1 = cmax;
    if (r0) *r0 = rmin;
    if (r1) *r1 = rmax;
    return 0;
}

void nop_coord_grid_to_aabb(int c0, int c1, int r0, int r1, int W, int H,
                            nop_coord_pointf_t out_pts[4])
{
    double x_left, x_right, y_top, y_bottom;

    if (!out_pts || W <= 0 || H <= 0)
        return;

    x_left   = -1.0 + c0 * 2.0 / W;
    x_right  = -1.0 + (c1 + 1) * 2.0 / W;
    y_top    =  1.0 - r0 * 2.0 / H;
    y_bottom =  1.0 - (r1 + 1) * 2.0 / H;

    /* Order: 左上 -> 左下 -> 右下 -> 右上 (TL, BL, BR, TR). */
    out_pts[0].x = (float)x_left;  out_pts[0].y = (float)y_top;
    out_pts[1].x = (float)x_left;  out_pts[1].y = (float)y_bottom;
    out_pts[2].x = (float)x_right; out_pts[2].y = (float)y_bottom;
    out_pts[3].x = (float)x_right; out_pts[3].y = (float)y_top;
}

void nop_coord_aabb_to_grid(float xmin, float xmax, float ymin, float ymax,
                            int W, int H, int *c0, int *c1, int *r0, int *r1)
{
    double halfW, halfH;
    int    a, b;

    if (W <= 0 || H <= 0)
        return;
    halfW = W / 2.0;
    halfH = H / 2.0;

    a = (int)ceil((xmin + 1.0) * halfW - 0.5);
    b = (int)floor((xmax + 1.0) * halfW - 0.5);
    if (c0) *c0 = clampi(a, 0, W - 1);
    if (c1) *c1 = clampi(b, 0, W - 1);

    a = (int)ceil((1.0 - ymax) * halfH - 0.5);
    b = (int)floor((1.0 - ymin) * halfH - 0.5);
    if (r0) *r0 = clampi(a, 0, H - 1);
    if (r1) *r1 = clampi(b, 0, H - 1);
}

int nop_coord_points_bounds(const nop_coord_pointf_t *pts, int count,
                            float *xmin, float *xmax, float *ymin, float *ymax)
{
    int   i;
    float xlo, xhi, ylo, yhi;

    if (!pts || count <= 0)
        return NOP_COORD_EMPTY;

    xlo = xhi = pts[0].x;
    ylo = yhi = pts[0].y;
    for (i = 1; i < count; i++) {
        if (pts[i].x < xlo) xlo = pts[i].x;
        if (pts[i].x > xhi) xhi = pts[i].x;
        if (pts[i].y < ylo) ylo = pts[i].y;
        if (pts[i].y > yhi) yhi = pts[i].y;
    }
    if (xmin) *xmin = xlo;
    if (xmax) *xmax = xhi;
    if (ymin) *ymin = ylo;
    if (ymax) *ymax = yhi;
    return 0;
}

int nop_coord_grid_rect_expand(int c0, int c1, int r0, int r1,
                               nop_coord_cell_t *out, int max)
{
    int r, c, n = 0;

    if (!out || max <= 0)
        return 0;
    for (r = r0; r <= r1; r++) {
        for (c = c0; c <= c1; c++) {
            if (n >= max)
                return n;
            out[n].col = c;
            out[n].row = r;
            n++;
        }
    }
    return n;
}
