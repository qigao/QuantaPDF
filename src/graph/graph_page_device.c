#include "graph_page_device.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* No platform-dependent epsilon or premature integer conversion. */
static int bounded_ceil_dimension(double value, uint32_t *out)
{
    double rounded;
    if (!isfinite(value) || !(value > 0.0) ||
        value > (double)UINT32_MAX) return 0;
    rounded = ceil(value);
    if (!(rounded >= 1.0) || rounded > (double)UINT32_MAX) return 0;
    *out = (uint32_t)rounded;
    return 1;
}

quantapdf_status quantapdf_graph_page_device_grid(
    const quantapdf_graph_page_geometry *geometry,
    unsigned int dpi, size_t max_pixels, size_t max_output_bytes,
    quantapdf_graph_device_grid *out_grid)
{
    quantapdf_graph_page_display display;
    quantapdf_graph_device_grid grid = {0};
    quantapdf_status status;
    double nominal_scale, scale_x, scale_y, w, h;
    size_t width, height, pixels;
    if (out_grid != NULL) memset(out_grid, 0, sizeof(*out_grid));
    if (geometry == NULL || out_grid == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (max_pixels == 0 || max_output_bytes == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (dpi != 72u && dpi != 96u && dpi != 144u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status = quantapdf_graph_page_display_map(geometry, &display);
    if (status != QUANTAPDF_OK) return status;
    /* DPI decides the integer bitmap extent, never the later sample phase. */
    nominal_scale = (double)dpi / 72.0;
    w = display.width_points * nominal_scale;
    h = display.height_points * nominal_scale;
    if (!bounded_ceil_dimension(w, &grid.width) ||
        !bounded_ceil_dimension(h, &grid.height))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    width = (size_t)grid.width;
    height = (size_t)grid.height;
    if (width > max_pixels / height ||
        width > SIZE_MAX / 3u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels = width * height;
    grid.stride_bytes = width * 3u;
    if (grid.stride_bytes > max_output_bytes / height ||
        pixels > max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grid.output_bytes = grid.stride_bytes * height;

    /* PDFium and the pre-existing native grid map the visible page to the
     * ENTIRE integer draw rectangle after ceil(DPI/72 * page extent).
     * Fractional extents yield independent actual pixel-to-point ratios.
     * Using nominal DPI for the affine would leave an artificial clear fringe
     * at the right/bottom and shift all pixel-center sampling locations. */
    scale_x = (double)grid.width / display.width_points;
    scale_y = (double)grid.height / display.height_points;
    if (!isfinite(scale_x) || !isfinite(scale_y) ||
        !(scale_x > 0.0) || !(scale_y > 0.0))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Source -> visible PDF Y-up -> device Y-down. The crop edge (not
     * its first pixel center) defines the phase/origin. */
    grid.from_source[0] = scale_x * display.from_source[0];
    grid.from_source[1] = -scale_y * display.from_source[1];
    grid.from_source[2] = scale_x * display.from_source[2];
    grid.from_source[3] = -scale_y * display.from_source[3];
    grid.from_source[4] = scale_x * display.from_source[4];
    grid.from_source[5] = scale_y *
        (display.height_points - display.from_source[5]);
    for (size_t i = 0; i < 6; ++i)
        if (!isfinite(grid.from_source[i]))
            return QUANTAPDF_ERROR_UNSUPPORTED;

    *out_grid = grid;
    return QUANTAPDF_OK;
}
