#ifndef QUANTAPDF_IR_RECT_FILL_RIGHT_PREVIEW_V0_H
#define QUANTAPDF_IR_RECT_FILL_RIGHT_PREVIEW_V0_H

#include "render/ir_scoped_device_grid_v0.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental, explicitly invoked PRIVATE C11 A/B.
 * A single normal/opaque rectangular Fill clipped by exactly one
 * rectangular nonzero W Path may replace ONLY the isolated source Fill's
 * right-edge partial alpha using actual fractional device-pixel geometry.
 *
 * Repeated rectangle boundary retraces in the SAME source operation
 * remain one Fill/Clip; no source token coordinate or PDFium fallback.
 * All other source geometries are UNSUPPORTED with fully zero output.
 * Old C11 V0, Path/Clip, SDK/ABI and caller ownership are unchanged.
 * The image is uniquely owned; dispose with the normal raster disposer. */
quantapdf_status quantapdf_ir_raster_rect_fill_right_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);


/* Seventh EXPLICIT PRIVATE original clipped-Fill candidate: preserve
 * the right-Fill analytical boundary and optionally correct the single
 * W Clip LEFT boundary's interior subpixel coverage. No default/SDK
 * renderer switch, no second ownership path, no PDFium fallback. */
quantapdf_status quantapdf_ir_raster_rect_fill_left_clip_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);


/* Eighth, explicit PRIVATE C11 clipped-Fill bottom 1pt-source fringe.
 * This is only permitted after the exact, independently callable
 * Fill-right + nonzero W Clip-left preview has been admitted; add one
 * true source-bottom device-pixel Y intersection for totally clear,
 * corner-excluded old samples. No default/public SDK/ABI switch or
 * automatic AA fallback. Caller owns and disposes its one RGBA result. */
quantapdf_status quantapdf_ir_raster_rect_fill_bottom_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);

/* NINTH independently callable PRIVATE source Fill-right / W Clip-top
 * Q8 fractional corner A/B. The old clipped Fill-bottom preview remains
 * independently callable and byte-identical, including left, right and
 * bottom accepted interiors. One source-owned pixel may change; no
 * default SDK/ABI, new owner, fallback, or PDFium-assisted C11 drawing. */
quantapdf_status
quantapdf_ir_raster_rect_fill_upper_right_q8_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);

/* TENTH independently callable PRIVATE clipped-Fill bottom-fringe A/B.
 * Preserve Q8 W-top/Fill-right correction, extend source-bottom faint
 * fringe ONLY into previously clear, wholly X-contained device pixels
 * immediately adjacent to conservative old interior limits. No public
 * SDK/ABI, second owner, fallback, coordinate fixtures or source state. */
quantapdf_status
quantapdf_ir_raster_rect_fill_bottom_near_edges_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);

/* ELEVENTH independently invoked PRIVATE source Fill-right/bottom Q8
 * tiny corner A/B. Requires two genuine fractional source-owned edges,
 * an unoccluded W Clip rectangle and an originally CLEAR RGBA device
 * pixel. All old modes and public SDK/ABI remain byte-for-byte unchanged. */
quantapdf_status
quantapdf_ir_raster_rect_fill_bottom_right_q8_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);

/* TWELFTH explicitly callable PRIVATE source W Clip-left/top corner.
 * Uses the existing source-continuous left coverage and Q8-quantized
 * Clip-top coverage for one genuine two-edge source intersection only.
 * Reuses the independently qualified mode11 Fill image and modifies
 * only that corner's already-red Alpha. No SDK/public ABI or fallback. */
quantapdf_status
quantapdf_ir_raster_rect_clip_left_top_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
