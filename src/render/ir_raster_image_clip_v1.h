#ifndef QUANTAPDF_IR_RASTER_IMAGE_CLIP_V1_H
#define QUANTAPDF_IR_RASTER_IMAGE_CLIP_V1_H

#include "render/ir_raster_image_v1.h"
#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private pure-C11 Image/Clip composition. Exactly one normalized RAW Image
 * operation with one graphics state referencing one Clip PATH resource.
 * The Clip is rasterized on the same 4x4 per-pixel grid as native Path V1,
 * using a one-page mask before integer source-over. There is no PDF writer,
 * PDFium/qpdf fallback, source/Graph ownership or public SDK ABI.
 *
 * Bounds must be page-contained integer STRETCH, and no unrelated resource,
 * page or operation can be ignored. Both limits independently gate native
 * image storage/drawn work and clip edges/subpixel sample tests.
 *
 * No caller payload is mutated. Output remains zero-initialized on every
 * unsupported, invalid, allocation or intermediate raster failure. */
quantapdf_status quantapdf_ir_raster_clipped_raw_image_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_ir_raster_image *out_image);

/* Same image/mask path; only the mask's existing V1 work is leased from
 * the shared page ledger. Source/destination pixel quotas are unchanged. */
quantapdf_status quantapdf_ir_raster_clipped_raw_image_v1_accounted(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_ir_raster_image *out_image);

/* Compile the SAME rectangle/Clip intersection without allocating a pixel
 * mask. Its owner copies/compiles every temporary command before returning. */
quantapdf_status quantapdf_native_image_mask_prepare_v1(
    const quantapdf_composer_page_state *page,const quantapdf_rect *bounds,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_geometry_v1 *out);
/* PRIVATE M2b.2c only: same prepared 4x4 Image-bounds/Clip geometry,
 * with visited-sample work instead of a full-viewport preflight. */
quantapdf_status quantapdf_native_image_mask_prepare_visible_clip_v1(
    const quantapdf_composer_page_state *page,const quantapdf_rect *bounds,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *clip,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_geometry_v1 *out);
quantapdf_status quantapdf_ir_raster_clipped_raw_image_v1_prepared(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_geometry_v1 *mask,
    quantapdf_ir_raster_image *out_image);

quantapdf_status quantapdf_native_clipped_raw_execute_v1(
    const quantapdf_composer_page_state *page,const quantapdf_composer_operation *operation,
    const quantapdf_composer_image_state *image,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *geometry_limits,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_geometry_v1 *mask,
    quantapdf_ir_raster_image *out);

#ifdef __cplusplus
}
#endif

#endif
