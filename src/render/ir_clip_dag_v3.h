#ifndef QUANTAPDF_IR_CLIP_DAG_V3_H
#define QUANTAPDF_IR_CLIP_DAG_V3_H

#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Preserve the original scoped V2 cap; the expanded V1 Path/Clip profile
 * opts into nine nodes explicitly. */
#define QUANTAPDF_NATIVE_CLIP_DAG_V0_MAX_NODES ((size_t)8u)
#define QUANTAPDF_NATIVE_CLIP_DAG_MAX_NODES ((size_t)9u)
#define QUANTAPDF_NATIVE_CLIP_DAG_MAX_DEPTH ((size_t)4u)

/* Reusable bounded, read-only Clip graph admission for scoped operations.
 * Resolves an arbitrary Path/Intersection root to unique leaf references.
 * visited_bits covers validated nodes, allowing callers to require complete
 * resource ownership across several independent operation scopes. */
quantapdf_status quantapdf_ir_clip_dag_collect_v3(
    const quantapdf_ir *ir, quantapdf_composer_clip_id root,
    const quantapdf_composer_clip_state **
        leaves, size_t *leaf_count, unsigned *visited_bits);

/* One opaque PATH paint, one Normal/full-alpha Graphics State and a Clip DAG
 * consisting only of Clip Paths and intersections. The root must contain a
 * nested intersection, with 3..9 total nodes, <=4 unique leaves, depth <=4.
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
