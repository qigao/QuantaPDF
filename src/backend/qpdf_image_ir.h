#ifndef QUANTAPDF_BACKEND_QPDF_IMAGE_IR_H
#define QUANTAPDF_BACKEND_QPDF_IMAGE_IR_H

#include "backend/qpdf_document.h"
#include "ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private source-Image XObject admission. This is NOT public rendering.
 * A bounded qpdf structural source interpreter parses the entire content
 * stream, admitting only balanced q/Q, cm, and Image XObject Do.
 * Any other paint, text, Form, shading, mask or blend operator rejects
 * the whole page, never yielding an image-only partial rendering.
 *
 * Output owns all RAW Gray8/RGB24 planes and image operations, and outlives
 * the qpdf document. Native Raster borrows this owned IR synchronously.
 * The caller passes separate bounds on page and cumulative source pixels,
 * operation count, and inherited resource ancestry.
 * On every failure the output is reset to a zero model. */
typedef struct quantapdf_qpdf_source_image_ir_limits {
    size_t max_operations;
    size_t max_page_pixels;
    size_t max_source_pixels;
    size_t max_parent_steps;
} quantapdf_qpdf_source_image_ir_limits;

quantapdf_status quantapdf_qpdf_source_image_ir(
    quantapdf_qpdf_document *document, int page_index,
    const quantapdf_qpdf_source_image_ir_limits *limits,
    quantapdf_ir *out_ir);

#ifdef __cplusplus
}
#endif
#endif
