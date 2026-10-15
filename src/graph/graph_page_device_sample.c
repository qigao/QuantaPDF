#include "graph_page_device_sample.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* Reject fabricated/corrupt grids. The base constructor allocates nothing;
 * these checks also make the sampling helper standalone and fail-closed. */
static int grid_valid(const quantapdf_graph_device_grid *grid)
{
    if (grid == NULL || grid->width == 0u || grid->height == 0u)
        return 0;
    /* uint32_t width*RGB3 is automatically safe on 64-bit size_t.
     * Only 32-bit hosts require the explicit width multiplication guard.
     * Avoid a permanently-false comparison rejected by -Wtype-limits. */
#if SIZE_MAX <= UINT32_MAX
    if ((size_t)grid->width > SIZE_MAX / 3u)
        return 0;
#endif
    if (grid->stride_bytes != (size_t)grid->width * 3u ||
        (size_t)grid->height > SIZE_MAX / grid->stride_bytes ||
        grid->output_bytes != grid->stride_bytes * (size_t)grid->height)
        return 0;
    for (size_t k = 0; k < 6; ++k)
        if (!isfinite(grid->from_source[k]))
            return 0;
    return 1;
}

quantapdf_status quantapdf_graph_device_grid_sample_q4(
    const quantapdf_graph_device_grid *grid,
    uint32_t pixel_x, uint32_t pixel_y,
    unsigned int sub_x, unsigned int sub_y,
    double *out_source_x, double *out_source_y)
{
    const double *t;
    double dx, dy, x, y;
    if (out_source_x != NULL) *out_source_x = 0.0;
    if (out_source_y != NULL) *out_source_y = 0.0;
    if (out_source_x == NULL || out_source_y == NULL ||
        out_source_x == out_source_y || !grid_valid(grid))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (pixel_x >= grid->width || pixel_y >= grid->height ||
        sub_x >= 4u || sub_y >= 4u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    dx = (double)pixel_x + ((double)sub_x + 0.5) / 4.0;
    dy = (double)pixel_y + ((double)sub_y + 0.5) / 4.0;
    t = grid->from_source;
    /* a c
     * b d : original source Y-up -> bitmap Y-down; only exact
     * quarter-turn affine forms admitted. Avoid determinant overflow
     * by dividing by the one coefficient on each orthogonal axis. */
    if (t[0] != 0.0 && t[3] != 0.0 &&
        t[1] == 0.0 && t[2] == 0.0) {
        x = (dx - t[4]) / t[0];
        y = (dy - t[5]) / t[3];
    } else if (t[0] == 0.0 && t[3] == 0.0 &&
        t[1] != 0.0 && t[2] != 0.0) {
        x = (dy - t[5]) / t[1];
        y = (dx - t[4]) / t[2];
    } else
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (!isfinite(x) || !isfinite(y))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    *out_source_x = x;
    *out_source_y = y;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_device_grid_admit_q4_work(
    const quantapdf_graph_device_grid *grid,
    size_t edge_count, size_t max_edges, size_t max_sample_tests)
{
    size_t pixels, sample_slots;
    if (!grid_valid(grid) || max_edges == 0u || max_sample_tests == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (edge_count == 0u || edge_count > max_edges)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if ((size_t)grid->width > SIZE_MAX / (size_t)grid->height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels = (size_t)grid->width * (size_t)grid->height;
    if (pixels > max_sample_tests / 16u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    sample_slots = pixels * 16u;
    if (edge_count > max_sample_tests / sample_slots)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    return QUANTAPDF_OK;
}
