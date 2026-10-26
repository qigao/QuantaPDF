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

#ifdef __cplusplus
}
#endif

#endif
