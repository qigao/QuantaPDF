#ifndef QUANTAPDF_IR_RASTER_IMAGE_CLIP_V3_H
#define QUANTAPDF_IR_RASTER_IMAGE_CLIP_V3_H

#include "render/ir_raster_image_clip_v2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private opt-in q/Q-restored image profile:
 * - Exactly two source-ordered owned RAW Image operations and resources;
 * - Exactly one frozen W/W* Clip Path leaf and one Graphics State;
 * - First IMAGE has graphics_state_id=1 (Clip active), second has id=0
 *   (the original PDF restored its previous unclipped state with Q);
 * - The first layer's coverage is sampled with the existing 4x4 Clip
 *   mask, the second is unmasked. Global image/draw budgets are charged
 *   for both layers and Clip sample/edge budgets for only the first.
 * - Both render to detached temporary alpha layers, which are combined
 *   in original paint order and published only after full success.
 *
 * No Image/Path mixing, generic Clip DAG, PDFium/qpdf dependency, public
 * ABI or production renderer cutover is implied. The earlier shared-Clip
 * V2 signature continues to require BOTH images clipped. */
quantapdf_status quantapdf_ir_raster_restored_raw_images_v3(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
