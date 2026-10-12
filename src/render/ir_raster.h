#ifndef QUANTAPDF_IR_RASTER_H
#define QUANTAPDF_IR_RASTER_H

#include "ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A private, detached RGBA8 result: row-major, straight alpha, top-left origin.
 * Pixel storage is malloc-owned and must be released by image_dispose().
 * No PDFium, qpdf, runtime context or bitmap handle is retained. */
typedef struct quantapdf_ir_raster_image {
    unsigned char *pixels;
    size_t size;
    size_t width;
    size_t height;
    size_t stride;
} quantapdf_ir_raster_image;

/* Both bounds must be nonzero; max_pixels also caps allocation/iteration. */
typedef struct quantapdf_ir_raster_limits {
    size_t max_pixels;
    size_t max_operations;
} quantapdf_ir_raster_limits;

/* V0: integer-size page; opaque, solid, axis-aligned, integer-coordinate
 * rectangle fills only (MOVE, LINE, LINE, LINE, CLOSE), no stroke, resources,
 * transform or other operation kinds. All unsupported constructs fail closed
 * before any raster allocation. The selected IR is borrowed, never mutated.
 *
 * Every call resets out_image first; any failure publishes no pixels. Callers
 * must dispose prior successful output before passing that slot again.
 * Failure statuses: ARGUMENT for invalid calls/limits/index, UNSUPPORTED for
 * unimplemented page content, NOMEM for allocation failure. */
quantapdf_status quantapdf_ir_raster_render_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_limits *limits,
    quantapdf_ir_raster_image *out_image);

void quantapdf_ir_raster_image_dispose(quantapdf_ir_raster_image *image);

#ifdef __cplusplus
}
#endif

#endif
