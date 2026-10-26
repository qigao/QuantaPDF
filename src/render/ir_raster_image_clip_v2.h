#ifndef QUANTAPDF_IR_RASTER_IMAGE_CLIP_V2_H
#define QUANTAPDF_IR_RASTER_IMAGE_CLIP_V2_H

#include "render/ir_raster_image_clip_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private, bounded all-or-nothing two-Image shared-Clip profile.
 * Exactly two source-ordered RAW IMAGE operations, two uniquely owned
 * resources, and ONE immutable graphics-state/Clip PATH root.
 * Both Image operations must bind the same live Clip.
 *
 * Each existing V1 image+Clip raster is invoked on its own transparent
 * page projection and composed in original order onto one final RGBA8
 * background. Their sample/edge budgets are conservatively split;
 * source-pixel and destination-draw work are preflighted IN AGGREGATE.
 * If the second layer fails, neither partial result is published.
 *
 * No PDFium/qpdf/Graph, no public ABI, no implicit renderer fallback. */
quantapdf_status quantapdf_ir_raster_two_clipped_raw_images_v2(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
