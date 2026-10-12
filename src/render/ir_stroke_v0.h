#ifndef QUANTAPDF_IR_STROKE_V0_H
#define QUANTAPDF_IR_STROKE_V0_H

#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Strict private native stroke increment, not a complete PDF stroke engine.
 * Only one opaque, untransformed, open MOVE->LINE segment, positive nonzero
 * width, butt caps, with no fill/dash/resources. The segment is converted to
 * an owned-on-stack quadrilateral and sent to the backend-free Raster V1
 * consumer. Results are detached RGBA and use ir_raster_image_dispose().
 *
 * Other stroke semantics fail UNSUPPORTED and never render partial output.
 * No public SDK, PDFium/qpdf dependency, or silent provider fallback. */
quantapdf_status quantapdf_ir_raster_stroke_butt_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
