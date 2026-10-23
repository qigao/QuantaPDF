#ifndef QUANTAPDF_IR_RASTER_V1_H
#define QUANTAPDF_IR_RASTER_V1_H

#include "render/ir_raster.h"
#include "render/native_device_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Additional explicit limits; every sampled edge test is charged before
 * allocating the output buffer. Sampling uses a fixed 4 x 4 grid. */
typedef struct quantapdf_ir_raster_v1_limits {
    size_t max_pixels;
    size_t max_operations;
    size_t max_edges;
    size_t max_sample_tests;
} quantapdf_ir_raster_v1_limits;

/* Private C11 API: general opaque solid-filled paths with MOVE/LINE/CUBIC/
 * CLOSE, affine transforms, implicit closing, even-odd/nonzero winding and
 * fixed deterministic 4x4 coverage. Also supports a narrowly bounded
 * open butt-capped segment or *closed triangle*, opaque, solid one-device-pixel
 * stroke under an isometry, and a closed axis-aligned rectangular miter stroke converted to a bounded
 * even-odd ring with transform-aware X/Y widths. The scoped renderer supports
 * the original Stroke, W Clip, Fill sequence using each operation's immutable
 * graphics state. An explicit Clip passed to this raster applies to all draws.
 * Unsupported widths/joins/dashes/general stroke transforms fail closed.
 * Optional canonical Clip Path intersection is borrowed only by the _clipped entry points;
 * there is no general stroke, alpha, resource or PDFium fallback.
 * The IR is borrowed; pixels are detached.
 * Output is reset on error, and callers dispose previous successful output
 * before reusing a result slot. */
quantapdf_status quantapdf_ir_raster_render_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* Private shared native sample-intersection consumer. 'ir' must already be
 * a single-operation resource-free local projection, and clip is a borrowed
 * canonical Clip Path. Both shapes are flattened under the same bounded work
 * budget. A sample paints only when inside BOTH shapes. */
quantapdf_status quantapdf_ir_raster_render_v1_clipped(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *clip,
    quantapdf_ir_raster_image *out_image);

/* Strict private multi-Clip sample intersection consumer. Each canonical
 * path is borrowed synchronously and flattened under the same total edge and
 * sampled-edge budget as the selected opaque paint operation. All clips must
 * contain the subpixel point for it to be painted; duplicate/no Clip input
 * is not a valid call. No provider dependency or fallback. */
#define QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES ((size_t)4u)
quantapdf_status quantapdf_ir_raster_render_v1_multi_clipped(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips, size_t clip_count,
    quantapdf_ir_raster_image *out_image);

/* Explicit private FIT device-grid consumer. The immutable plan is appended
 * AFTER the semantic operation transform, identically for paint and frozen
 * Clip paths, before device-space curve flattening. Zero clips is valid.
 * Fractional semantic page dimensions remain unchanged. A missing/mismatched
 * plan fails closed; the legacy entry points retain integer-page admission.
 * Stroke admission checks the combined device transform as well. */
quantapdf_status quantapdf_ir_raster_render_v1_planned(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    const quantapdf_composer_clip_state *const *clips, size_t clip_count,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
