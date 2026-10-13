#ifndef QUANTAPDF_IR_DEVICE_POLYLINE_STROKE_V0_H
#define QUANTAPDF_IR_DEVICE_POLYLINE_STROKE_V0_H

#include "render/ir_raster_v1.h"
#include "render/native_device_grid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Explicit private C11 stroke-only pixel sampler. Only an open polyline,
 * single opaque Stroke Path, default butt cap and bounded miter join (including
 * bevel fallback). No PDFium/qpdf, Path filling, Clip, general affine
 * transforms, fallback, or automatic call from existing raster entrypoints.
 *
 * Source-space half-width is sampled through the previously qualified
 * double page -> nonunit device grid; repeated edges contribute coverage only
 * once per subpixel. Input IR is borrowed and never mutated; result is an
 * owned RGBA8 layer, transparent outside the stroke and released with the
 * standard quantapdf_ir_raster_image_dispose().
 *
 * All failures reset out_image and publish zero pixels. */
quantapdf_status quantapdf_ir_raster_device_polyline_stroke_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);


/* Experimental explicitly-invoked private preview, NEVER the default.
 * Tests a 1pt horizontal (<2 projected device pixels) strip interior full
 * row-cover alternative while leaving Path, Clip, vertical lines, joins,
 * caps, ownership and negative source admission unchanged. Pixel parity
 * of this candidate must be measured against the untouched original PDF. */
quantapdf_status
quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);

/* Second EXPERIMENTAL private A/B. Unlike the #284 repeat-only
 * interior preview, this counts device pixels at geometrically repeated
 * source horizontal endpoints. It changes NEITHER old private entrypoint,
 * nor the prior preview, and may be rejected by full original-PDF parity.
 * No PDFium fallback, no public ABI or SDK change. */
quantapdf_status quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);


/* Third explicit/private stroke-only A/B: at a repeated horizontal-to-
 * downward-vertical right-angle corner, compare bounded repeated
 * straight-alpha source-over with the #286 full-row endpoint hypothesis.
 * No default raster/public SDK/cap/clip behavior is changed. */
quantapdf_status
quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);


/* Fourth private A/B: 1pt single-pass axis-aligned vertical Stroke interiors
 * use analytical device-pixel strip coverage only if all other source
 * segments and joins are excluded. Modes 0..3 stay unchanged. */
quantapdf_status
quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);


/* Fifth explicit/private A/B: source-geometry 1pt single-pass horizontal
 * interior coverage, retaining the mode4 vertical, repeated top and
 * corner preview. No default entrypoint, SDK, ABI or fallback change. */
quantapdf_status
quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
