#ifndef QUANTAPDF_NATIVE_DEVICE_GRID_V1_H
#define QUANTAPDF_NATIVE_DEVICE_GRID_V1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private, allocation-free C11 page-to-device candidate. Does not authorize
 * native pixel output. Input points use top-left DISPLAY coordinates; raw
 * MediaBox/CropBox, intrinsic Rotate and UserUnit belong to the producer. */
typedef enum quantapdf_native_grid_result {
    QUANTAPDF_NATIVE_GRID_OK = 0,
    QUANTAPDF_NATIVE_GRID_ARGUMENT = 1,
    QUANTAPDF_NATIVE_GRID_UNSUPPORTED = 2
} quantapdf_native_grid_result;

typedef enum quantapdf_native_grid_precision {
    QUANTAPDF_NATIVE_GRID_SOURCE_F32 = 1,
    QUANTAPDF_NATIVE_GRID_SOURCE_EXACT_DOUBLE = 2
} quantapdf_native_grid_precision;

typedef struct quantapdf_native_grid_request {
    double width_points, height_points;
    double display_x0, display_y0;
    double user_unit;
    unsigned dpi, intrinsic_rotation, requested_rotation;
    quantapdf_native_grid_precision precision;
} quantapdf_native_grid_request;

typedef struct quantapdf_native_grid_budget {
    size_t max_pixels, max_rgba_bytes, max_edges, max_sample_tests;
    size_t admitted_edges; /* exact bounded edge count across all admitted ops */
} quantapdf_native_grid_budget;

typedef struct quantapdf_native_device_grid_v1 {
    size_t width, height, stride, rgba_bytes, pixel_count, sample_edge_tests;
    double width_points, height_points;
    double device_per_point_x, device_per_point_y;
    double points_per_device_x, points_per_device_y;
} quantapdf_native_device_grid_v1;

/* Reset output even on refusal. This stage never allocates/publishes RGBA.
 * First implementation deliberately supports zero display offset, UserUnit=1,
 * no intrinsic/requested rotation, and 72/96/144 DPI only. A float-origin
 * grid near an integer-device boundary is conservatively UNSUPPORTED. */
quantapdf_native_grid_result quantapdf_native_device_grid_v1_create(
    const quantapdf_native_grid_request *request,
    const quantapdf_native_grid_budget *budget,
    quantapdf_native_device_grid_v1 *out);

/* Analytical 4x4 subpixel sampling in DISPLAY points, NOT independent proof
 * of PDFium antialiasing centers or original-PDF pixel parity. */
quantapdf_native_grid_result quantapdf_native_device_grid_v1_sample_point(
    const quantapdf_native_device_grid_v1 *grid,
    size_t pixel_x, size_t pixel_y, unsigned sample_x, unsigned sample_y,
    double *out_x, double *out_y);

#ifdef __cplusplus
}
#endif
#endif
