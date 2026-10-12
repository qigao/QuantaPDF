#ifndef QUANTAPDF_IR_CLIP_DAG_V3_H
#define QUANTAPDF_IR_CLIP_DAG_V3_H

#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUANTAPDF_NATIVE_CLIP_DAG_MAX_NODES ((size_t)8u)
#define QUANTAPDF_NATIVE_CLIP_DAG_MAX_DEPTH ((size_t)4u)

/* One opaque PATH paint, one Normal/full-alpha Graphics State and a Clip DAG
 * consisting only of Clip Paths and intersections. The root must contain a
 * nested intersection, with 3..8 total nodes, <=4 unique leaves, depth <=4.
 * Every registered node must be reachable. Intersections are evaluated as
 * an AND at each shared raster subpixel; no mask averaging or PDFium fallback.
 * The IR is borrowed, result RGBA8 detached, error publication atomic. */
quantapdf_status quantapdf_ir_raster_clip_dag_v3(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
