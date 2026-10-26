#ifndef QUANTAPDF_IR_RASTER_IMAGE_CLIP_PATH_V4_H
#define QUANTAPDF_IR_RASTER_IMAGE_CLIP_PATH_V4_H

#include "render/ir_raster_image_clip_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private C11 image-then-path q/Q-restoration profile.
 *
 * Exactly two source-ordered operations:
 *  [0] one owned RAW IMAGE under one frozen W/W* Clip PATH leaf and GS #1;
 *  [1] one opaque PATH fill after PDF Q restored GS/Clip to ID zero.
 *
 * Image and Path both render to detached transparent layers. Clip V1's
 * original 4x4 subpixel mask is applied ONLY to Image #0; Path #1 is
 * rasterized without inheriting the old Clip. Edge/sample budgets are
 * conservatively divided between layers. No qpdf/PDFium/renderer backend,
 * public ABI change or implicit fallback. Failure resets all output fields.
 */
quantapdf_status quantapdf_ir_raster_clipped_image_then_path_v4(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *path_clip_limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
