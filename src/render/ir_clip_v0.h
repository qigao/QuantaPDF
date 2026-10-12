#ifndef QUANTAPDF_IR_CLIP_V0_H
#define QUANTAPDF_IR_CLIP_V0_H

#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A deliberately restricted private native clip consumer. Only one
 * rectangular, integer-aligned Clip Path linked by one normal graphics state
 * on one opaque PATH fill operation is admitted. Other clip semantics fail
 * UNSUPPORTED without published pixels. The IR is borrowed and unchanged;
 * output is detached RGBA8 and disposed with ir_raster_image_dispose().
 * No PDFium or qpdf call, no public SDK/ABI extension or fallback. */
quantapdf_status quantapdf_ir_raster_clip_rect_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
