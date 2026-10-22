#ifndef QUANTAPDF_IR_STROKE_POLYLINE_V1_H
#define QUANTAPDF_IR_STROKE_POLYLINE_V1_H

#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Detached, bounded user-space outline for opaque linear strokes.
 * MOVE/LINE/CLOSE, positive width, butt/square/round caps, miter/bevel/round joins.
 * Nonnegative, not-all-zero dash arrays (<=64 items), odd repetition/phase.
 * Zero dashes use directed caps. No hairline, cubic, alpha or fallback.
 * The fill preserves the authored affine transform; callers borrow it
 * synchronously and dispose commands after sampling. Source/output must not
 * alias. Dispose earlier successful output before reuse; failure zeroes output. */
quantapdf_status quantapdf_ir_stroke_polyline_outline_v1(
    const quantapdf_composer_path_operation *source, size_t max_edges,
    quantapdf_composer_path_operation *out_fill);
/* Round arc subdivision accounts for the final FIT plan as well as the
 * authored transform. The returned fill still lives in user space; sample
 * with this same plan. Chord error <=1/64 px, float error <=1/128 px. */
quantapdf_status quantapdf_ir_stroke_polyline_outline_v1_planned(
    const quantapdf_composer_path_operation *source,size_t max_edges,
    const quantapdf_native_device_plan *plan,quantapdf_composer_path_operation *out_fill);
void quantapdf_ir_stroke_polyline_dispose_v1(
    quantapdf_composer_path_operation *fill);

#ifdef __cplusplus
}
#endif
#endif
