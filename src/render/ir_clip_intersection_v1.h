#ifndef QUANTAPDF_IR_CLIP_INTERSECTION_V1_H
#define QUANTAPDF_IR_CLIP_INTERSECTION_V1_H

#include "render/ir_clip_v0.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUANTAPDF_IR_CLIP_INTERSECTION_V1_MAX_MEMBERS ((size_t)4u)

/* Strict private V1: one normal graphics state, one opaque PATH paint and one
 * flat intersection of 2..4 distinct integer axis-aligned Clip Path leaves.
 * Every member must be a root-owned resource, with canonical identity CTM.
 * The immutable selected IR is borrowed only during this call. Output is a
 * detached malloc-owned RGBA8 image, disposed with ir_raster_image_dispose.
 *
 * Unsupported graphs, empty intersections, transforms, extra resources or
 * operations return UNSUPPORTED before publishing pixels. No PDFium or qpdf
 * linkage, public ABI changes, fallback or general Clip DAG support. */
quantapdf_status quantapdf_ir_raster_clip_intersection_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
