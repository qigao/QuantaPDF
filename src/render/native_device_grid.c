#include "render/native_device_grid.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum { QP_NATIVE_SAMPLES_PER_AXIS = 4, QP_NATIVE_SAMPLES_PER_PIXEL = 16 };

static int qp_grid_valid(const quantapdf_native_device_grid *grid)
{
    return grid != NULL && grid->width != 0u && grid->height != 0u &&
           grid->pixel_count != 0u && grid->stride != 0u &&
           grid->sample_slots != 0u && grid->rgba_bytes != 0u &&
           isfinite(grid->source_width_points) &&
           isfinite(grid->source_height_points) &&
           isfinite(grid->points_per_device_x) &&
           isfinite(grid->points_per_device_y) &&
           grid->source_width_points > 0.0 &&
           grid->source_height_points > 0.0 &&
           grid->points_per_device_x > 0.0 &&
           grid->points_per_device_y > 0.0;
}

quantapdf_native_device_grid_status quantapdf_native_device_grid_create_v0(
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *limits,
    quantapdf_native_device_grid *out_grid)
{
    double projected_x, projected_y, width_d, height_d, scale;
    size_t width, height, pixels;
    quantapdf_native_device_grid grid;

    if (out_grid == NULL)
        return QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT;
    memset(out_grid, 0, sizeof(*out_grid));
    memset(&grid, 0, sizeof(grid));
    if (geometry == NULL || limits == NULL || limits->max_pixels == 0u ||
        limits->max_pixels > SIZE_MAX / 4u || limits->max_rgba_bytes < 4u ||
        limits->max_edges == 0u || limits->max_edges > 65536u ||
        limits->max_sample_tests == 0u)
        return QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT;

    /* Don't silently treat raw /CropBox, /Rotate, or UserUnit as ordinary
     * pixel coordinates. These require separately qualified normalization. */
    if (geometry->visible_origin_x_points != 0.0 ||
        geometry->visible_origin_y_points != 0.0 ||
        geometry->user_unit != 1.0 ||
        geometry->intrinsic_rotation_quarters != 0 ||
        geometry->requested_rotation_quarters != 0 ||
        (geometry->dpi != 72.0f && geometry->dpi != 96.0f &&
         geometry->dpi != 144.0f) ||
        !isfinite(geometry->page_width_points) ||
        !isfinite(geometry->page_height_points) ||
        geometry->page_width_points <= 0.0 ||
        geometry->page_height_points <= 0.0 ||
        geometry->page_width_points > 1000000.0 ||
        geometry->page_height_points > 1000000.0)
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;

    scale = (double)geometry->dpi / 72.0;
    projected_x = geometry->page_width_points * scale;
    projected_y = geometry->page_height_points * scale;
    width_d = ceil(projected_x);
    height_d = ceil(projected_y);
    /* The PDFium page/device rectangle API uses signed int dimensions. */
    if (!isfinite(width_d) || !isfinite(height_d) || width_d < 1.0 ||
        height_d < 1.0 || width_d > (double)INT_MAX ||
        height_d > (double)INT_MAX)
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;

    width = (size_t)width_d;
    height = (size_t)height_d;
    if (width > limits->max_pixels / height)
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;
    pixels = width * height;
    if (pixels > limits->max_rgba_bytes / 4u ||
        pixels > limits->max_sample_tests / QP_NATIVE_SAMPLES_PER_PIXEL ||
        width > SIZE_MAX / 4u)
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;

    grid.width = width;
    grid.height = height;
    grid.pixel_count = pixels;
    grid.stride = width * 4u;
    grid.rgba_bytes = pixels * 4u;
    grid.sample_slots = pixels * QP_NATIVE_SAMPLES_PER_PIXEL;
    grid.source_width_points = geometry->page_width_points;
    grid.source_height_points = geometry->page_height_points;
    grid.points_per_device_x = geometry->page_width_points / width_d;
    grid.points_per_device_y = geometry->page_height_points / height_d;
    grid.dpi = geometry->dpi;
    if (!qp_grid_valid(&grid))
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;

    *out_grid = grid;
    return QUANTAPDF_NATIVE_DEVICE_GRID_OK;
}

quantapdf_native_device_grid_status quantapdf_native_device_grid_admit_edges_v0(
    const quantapdf_native_device_grid *grid,
    const quantapdf_native_device_grid_limits *limits,
    size_t edge_count)
{
    if (!qp_grid_valid(grid) || limits == NULL || limits->max_edges == 0u ||
        limits->max_sample_tests == 0u)
        return QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT;
    if (edge_count == 0u || edge_count > limits->max_edges ||
        grid->pixel_count > limits->max_pixels ||
        grid->rgba_bytes > limits->max_rgba_bytes ||
        grid->sample_slots > limits->max_sample_tests ||
        edge_count > limits->max_sample_tests / grid->sample_slots)
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;
    return QUANTAPDF_NATIVE_DEVICE_GRID_OK;
}

quantapdf_native_device_grid_status quantapdf_native_device_grid_check_ir_v0(
    const quantapdf_native_device_grid *grid,
    float ir_width_points, float ir_height_points)
{
    double x, y, dx, dy;
    if (!qp_grid_valid(grid))
        return QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT;
    x = (double)ir_width_points;
    y = (double)ir_height_points;
    if (!isfinite(x) || !isfinite(y) || x <= 0.0 || y <= 0.0)
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;
    dx = fabs(x - grid->source_width_points) / grid->points_per_device_x;
    dy = fabs(y - grid->source_height_points) / grid->points_per_device_y;
    if (!isfinite(dx) || !isfinite(dy) || dx > 1.0 / 4096.0 ||
        dy > 1.0 / 4096.0 ||
        ceil(x * ((double)grid->dpi / 72.0)) != (double)grid->width ||
        ceil(y * ((double)grid->dpi / 72.0)) != (double)grid->height)
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;
    return QUANTAPDF_NATIVE_DEVICE_GRID_OK;
}

quantapdf_native_device_grid_status quantapdf_native_device_grid_sample_v0(
    const quantapdf_native_device_grid *grid,
    size_t pixel_x, size_t pixel_y, unsigned sub_x, unsigned sub_y,
    double *out_source_x, double *out_source_y)
{
    if (out_source_x != NULL) *out_source_x = 0.0;
    if (out_source_y != NULL) *out_source_y = 0.0;
    if (out_source_x == NULL || out_source_y == NULL ||
        out_source_x == out_source_y || !qp_grid_valid(grid))
        return QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT;
    if (pixel_x >= grid->width || pixel_y >= grid->height ||
        sub_x >= QP_NATIVE_SAMPLES_PER_AXIS ||
        sub_y >= QP_NATIVE_SAMPLES_PER_AXIS)
        return QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED;
    *out_source_x = ((double)pixel_x + ((double)sub_x + 0.5) / 4.0) *
        grid->points_per_device_x;
    *out_source_y = ((double)pixel_y + ((double)sub_y + 0.5) / 4.0) *
        grid->points_per_device_y;
    return QUANTAPDF_NATIVE_DEVICE_GRID_OK;
}
