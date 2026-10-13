#include "native_device_grid_v1.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define QP_NATIVE_GRID_SUBPIXELS ((size_t)16u)

/* The old detached IR stores float points. When the source's exact decimal
 * precision was not retained, reject float-neighbor ambiguity at ceil edges.
 * A future precise geometry producer may pass explicitly proven double input. */
static int qp_float_extent_unambiguous(double points, unsigned dpi, double extent)
{
    float reduced = (float)points;
    double before, after;
    if (!isfinite(reduced) || (double)reduced != points)
        return 0;
    before = (double)nextafterf(reduced, -INFINITY) * ((double)dpi / 72.0);
    after = (double)nextafterf(reduced, INFINITY) * ((double)dpi / 72.0);
    return isfinite(before) && isfinite(after) &&
           ceil(before) == extent && ceil(after) == extent;
}

quantapdf_native_grid_result quantapdf_native_device_grid_v1_create(
    const quantapdf_native_grid_request *request,
    const quantapdf_native_grid_budget *budget,
    quantapdf_native_device_grid_v1 *out)
{
    double factor, w, h;
    size_t width, height, pixels, bytes, checks;
    quantapdf_native_device_grid_v1 grid = {0};
    if (out == NULL)
        return QUANTAPDF_NATIVE_GRID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (request == NULL || budget == NULL || budget->max_pixels == 0u ||
        budget->max_rgba_bytes == 0u || budget->max_edges == 0u ||
        budget->max_sample_tests == 0u || budget->admitted_edges == 0u)
        return QUANTAPDF_NATIVE_GRID_ARGUMENT;
    if (!isfinite(request->width_points) ||
        !isfinite(request->height_points) ||
        !isfinite(request->display_x0) || !isfinite(request->display_y0) ||
        !isfinite(request->user_unit) || request->width_points <= 0.0 ||
        request->height_points <= 0.0 || request->user_unit <= 0.0)
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    if ((request->dpi != 72u && request->dpi != 96u && request->dpi != 144u) ||
        request->intrinsic_rotation != 0u || request->requested_rotation != 0u ||
        request->display_x0 != 0.0 || request->display_y0 != 0.0 ||
        request->user_unit != 1.0 ||
        (request->precision != QUANTAPDF_NATIVE_GRID_SOURCE_F32 &&
         request->precision != QUANTAPDF_NATIVE_GRID_SOURCE_EXACT_DOUBLE))
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    factor = (double)request->dpi / 72.0;
    w = ceil(request->width_points * factor);
    h = ceil(request->height_points * factor);
    if (!isfinite(w) || !isfinite(h) || w <= 0.0 || h <= 0.0 ||
        w > (double)(INT_MAX / 4) || h > (double)INT_MAX)
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    if (request->precision == QUANTAPDF_NATIVE_GRID_SOURCE_F32 &&
        (!qp_float_extent_unambiguous(request->width_points, request->dpi, w) ||
         !qp_float_extent_unambiguous(request->height_points, request->dpi, h)))
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    width = (size_t)w;
    height = (size_t)h;
    if (width > budget->max_pixels / height ||
        budget->admitted_edges > budget->max_edges)
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    pixels = width * height;
    /* Budget and overflow checks precede ALL pixel, edge and sample work. */
    if (pixels > SIZE_MAX / 4u || pixels > budget->max_rgba_bytes / 4u ||
        pixels > budget->max_sample_tests / QP_NATIVE_GRID_SUBPIXELS)
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    checks = pixels * QP_NATIVE_GRID_SUBPIXELS;
    if (budget->admitted_edges > budget->max_sample_tests / checks)
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    checks *= budget->admitted_edges;
    bytes = pixels * 4u;
    grid.width = width;
    grid.height = height;
    grid.stride = width * 4u;
    grid.rgba_bytes = bytes;
    grid.pixel_count = pixels;
    grid.sample_edge_tests = checks;
    grid.width_points = request->width_points;
    grid.height_points = request->height_points;
    grid.device_per_point_x = w / request->width_points;
    grid.device_per_point_y = h / request->height_points;
    grid.points_per_device_x = request->width_points / w;
    grid.points_per_device_y = request->height_points / h;
    if (!isfinite(grid.device_per_point_x) ||
        !isfinite(grid.device_per_point_y) ||
        !isfinite(grid.points_per_device_x) ||
        !isfinite(grid.points_per_device_y) ||
        grid.device_per_point_x <= 0.0 || grid.device_per_point_y <= 0.0 ||
        grid.points_per_device_x <= 0.0 || grid.points_per_device_y <= 0.0)
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    *out = grid;
    return QUANTAPDF_NATIVE_GRID_OK;
}

quantapdf_native_grid_result quantapdf_native_device_grid_v1_sample_point(
    const quantapdf_native_device_grid_v1 *grid,
    size_t pixel_x, size_t pixel_y, unsigned sample_x, unsigned sample_y,
    double *out_x, double *out_y)
{
    if (out_x == NULL || out_y == NULL || out_x == out_y)
        return QUANTAPDF_NATIVE_GRID_ARGUMENT;
    *out_x = 0.0;
    *out_y = 0.0;
    if (grid == NULL || grid->width == 0u || grid->height == 0u ||
        pixel_x >= grid->width || pixel_y >= grid->height ||
        sample_x >= 4u || sample_y >= 4u ||
        !isfinite(grid->points_per_device_x) ||
        !isfinite(grid->points_per_device_y) ||
        grid->points_per_device_x <= 0.0 ||
        grid->points_per_device_y <= 0.0)
        return QUANTAPDF_NATIVE_GRID_UNSUPPORTED;
    *out_x = ((double)pixel_x + ((double)sample_x + 0.5) / 4.0) *
        grid->points_per_device_x;
    *out_y = ((double)pixel_y + ((double)sample_y + 0.5) / 4.0) *
        grid->points_per_device_y;
    return QUANTAPDF_NATIVE_GRID_OK;
}
