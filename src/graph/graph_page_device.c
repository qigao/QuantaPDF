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
    double scale, w, h;
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
    scale = (double)dpi / 72.0;
    w = display.width_points * scale;
    h = display.height_points * scale;
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

    /* Source -> visible PDF Y-up -> device Y-down. The crop edge (not
     * its first pixel center) defines the phase/origin. */
    grid.from_source[0] = scale * display.from_source[0];
    grid.from_source[1] = -scale * display.from_source[1];
    grid.from_source[2] = scale * display.from_source[2];
    grid.from_source[3] = -scale * display.from_source[3];
    grid.from_source[4] = scale * display.from_source[4];
    grid.from_source[5] = scale *
        (display.height_points - display.from_source[5]);
    for (size_t i = 0; i < 6; ++i)
        if (!isfinite(grid.from_source[i]))
            return QUANTAPDF_ERROR_UNSUPPORTED;

    *out_grid = grid;
    return QUANTAPDF_OK;
}
