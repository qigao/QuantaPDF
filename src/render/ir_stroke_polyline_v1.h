#ifndef QUANTAPDF_IR_STROKE_POLYLINE_V1_H
#define QUANTAPDF_IR_STROKE_POLYLINE_V1_H

#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Detached, bounded user-space outline for opaque linear strokes.
 * MOVE/LINE/CLOSE, positive width, butt/square caps, miter/bevel joins.
 * Positive dash arrays (<=64 items), odd repetition and nonnegative phase.
 * No zero dash entries, hairline, round, cubic, alpha or provider fallback.
 * The fill preserves the authored affine transform; callers borrow it
 * synchronously and dispose commands after sampling. Source/output must not
 * alias. Dispose earlier successful output before reuse; failure zeroes output. */
quantapdf_status quantapdf_ir_stroke_polyline_outline_v1(
    const quantapdf_composer_path_operation *source, size_t max_edges,
    quantapdf_composer_path_operation *out_fill);
void quantapdf_ir_stroke_polyline_dispose_v1(
    quantapdf_composer_path_operation *fill);

#ifdef __cplusplus
}
#endif
#endif
