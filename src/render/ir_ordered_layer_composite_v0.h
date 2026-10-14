#ifndef QUANTAPDF_IR_ORDERED_LAYER_COMPOSITE_V0_H
#define QUANTAPDF_IR_ORDERED_LAYER_COMPOSITE_V0_H

#include "render/ir_raster.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private C11, provider-free. Strictly TWO pre-rendered straight-alpha RGBA
 * layers, composed in source paint order (earlier Stroke, later clipped Fill).
 * Does not reconstruct per-subpixel coverage correlations: whole-image
 * PDFium parity MUST be measured independently before any positive admission.
 *
 * Inputs are borrowed and immutable; returned RGBA8 is uniquely malloc-owned.
 * No source handles, PDFium, qpdf or SDK/public ABI. The caller must dispose
 * any prior result before reusing the out_image slot. */
typedef struct quantapdf_ir_ordered_composite_v0_limits {
    size_t max_pixels;
    size_t max_output_bytes;
} quantapdf_ir_ordered_composite_v0_limits;

quantapdf_status quantapdf_ir_ordered_layer_composite_v0(
    const quantapdf_ir_raster_image *earlier_stroke,
    const quantapdf_ir_raster_image *later_clipped_fill,
    const quantapdf_ir_ordered_composite_v0_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* PRIVATE A/B only: compose existing layers on opaque WHITE with floor
 * division after each paint. Returned RGBA8 has alpha=255 throughout.
 * Does not modify the original transparent composite or source layers. */
quantapdf_status
quantapdf_ir_ordered_layer_composite_opaque_white_floor_preview_v0(
    const quantapdf_ir_raster_image *earlier_stroke,
    const quantapdf_ir_raster_image *later_clipped_fill,
    const quantapdf_ir_ordered_composite_v0_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
