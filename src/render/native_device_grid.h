#ifndef QUANTAPDF_NATIVE_DEVICE_GRID_H
#define QUANTAPDF_NATIVE_DEVICE_GRID_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private, provider-free C11 value. No ownership, allocation, public ABI,
 * PDFium, or implicit fallback. The v0 contract only admits a zero-origin,
 * unrotated, UserUnit=1 displayed page; geometry is not rewritten. */
typedef enum quantapdf_native_device_grid_status {
    QUANTAPDF_NATIVE_DEVICE_GRID_OK = 0,
    QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT = 1,
    QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED = 2
} quantapdf_native_device_grid_status;

typedef struct quantapdf_native_device_grid_geometry {
    double page_width_points;
    double page_height_points;
    double visible_origin_x_points;
    double visible_origin_y_points;
    double user_unit;
    int intrinsic_rotation_quarters;
    int requested_rotation_quarters;
    float dpi;
} quantapdf_native_device_grid_geometry;

typedef struct quantapdf_native_device_grid_limits {
    size_t max_pixels;
    size_t max_rgba_bytes;
    size_t max_edges;
    size_t max_sample_tests;
} quantapdf_native_device_grid_limits;

typedef struct quantapdf_native_device_grid {
    size_t width;
    size_t height;
    size_t pixel_count;
    size_t stride;
    size_t rgba_bytes;
    size_t sample_slots;
    double source_width_points;
    double source_height_points;
    double points_per_device_x;
    double points_per_device_y;
    float dpi;
} quantapdf_native_device_grid;

/* No raster admission is implied. Even success must not bypass the current
 * Scoped Raster UNSUPPORTED negative or publish pixels. Output cleared first. */
quantapdf_native_device_grid_status quantapdf_native_device_grid_create_v0(
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *limits,
    quantapdf_native_device_grid *out_grid);

/* Enforce sampled edge work in addition to the base 4x4 pixel budget. */
quantapdf_native_device_grid_status quantapdf_native_device_grid_admit_edges_v0(
    const quantapdf_native_device_grid *grid,
    const quantapdf_native_device_grid_limits *limits,
    size_t edge_count);

/* Guard a float-backed Detached IR extent against an original double PDF
 * geometry. Requires identical pixel extents AND <=1/4096-device-pixel width
 * and height narrowing error; does not validate individual IR commands. */
quantapdf_native_device_grid_status quantapdf_native_device_grid_check_ir_v0(
    const quantapdf_native_device_grid *grid,
    float ir_width_points, float ir_height_points);

/* Deterministic 4x4 subpixel inverse of PDFium's integer draw rectangle for
 * an already-normalized Y-down page. This is mathematical grid sampling,
 * NOT a PDFium anti-aliasing pixel-center observation or parity claim. */
quantapdf_native_device_grid_status quantapdf_native_device_grid_sample_v0(
    const quantapdf_native_device_grid *grid,
    size_t pixel_x, size_t pixel_y, unsigned sub_x, unsigned sub_y,
    double *out_source_x, double *out_source_y);

#ifdef __cplusplus
}
#endif

#endif
