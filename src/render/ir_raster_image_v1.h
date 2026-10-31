#ifndef QUANTAPDF_IR_RASTER_IMAGE_V1_H
#define QUANTAPDF_IR_RASTER_IMAGE_V1_H

#include "render/ir_raster.h"
#include "render/native_raster_memory_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private pure-C11 image painting admission, not the public render backend.
 * Exactly one integer-size page, 1..4 IMAGE draw operations in source order.
 * All referenced images must be uniquely owned RAW Gray8/RGB24 resources;
 * optional alpha lives in the independent normalized alpha plane. Source
 * rows and output rows have top-left origin. Only integer-coordinate,
 * page-contained STRETCH rectangles and default graphics state are admitted.
 *
 * Sampling is deterministic center-nearest. Each layer uses straight-alpha
 * source-over; page background may be opaque or explicitly transparent.
 * Limits independently bound the output pixel count, cumulative source image
 * pixels, and cumulative destination sampling work (overlaps count again).
 *
 * No source/Graph, qpdf, PDFium, font/image decoder or backend is consulted.
 * Output is always reset on any error; no partial pixels are published.
 * The caller owns and disposes the result with image_dispose(). */
typedef struct quantapdf_ir_raster_image_v1_limits {
    size_t max_pixels;
    size_t max_operations;
    size_t max_source_pixels;
    size_t max_drawn_pixels;
} quantapdf_ir_raster_image_v1_limits;

quantapdf_status quantapdf_ir_raster_raw_images_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* V5 child: the complete parent's unique-source/placement inventory already
 * owns the pixel bill. Revalidate the same local caps, but charge only this
 * malloc-owned output's bytes to the parent. NULL retains legacy allocation. */
quantapdf_status quantapdf_ir_raster_raw_images_v1_borrowed(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *limits,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
