#ifndef QUANTAPDF_IR_RASTER_V1_H
#define QUANTAPDF_IR_RASTER_V1_H

#include "render/ir_raster.h"
#include "render/native_device_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Additional explicit limits; every sampled edge test is charged before
 * publishing pixels. Sampling uses a fixed 4 x 4 grid. Exact text may
 * classify into a private temporary image which is discarded on failure.
 * Exactly one opaque clipped Fill can conservatively bound its work
 * to the intersection of the actual flattened draw and Clip edges.
 * This is a visited-sample optimization INSIDE this same C11 raster:
 * the numeric cap, sample grid, untouched paths and atomic refusal
 * contract are unchanged. */
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

/* Explicit PRIVATE Source V10 visible-work admission: the same
 * V1 winding/4x4 raster as above, but one fully admitted clipped Fill
 * may conservatively bound VISITED sample work by flattened draw and
 * Clip envelopes. This named entry is not a second raster or a
 * public SDK export. All legacy V1 functions above retain their
 * complete full-page budget/refusal contract. */
quantapdf_status quantapdf_ir_raster_render_v1_multi_clipped_visible_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_ir_raster_image *out_image);

/* Explicit PRIVATE 1px axis-aligned open butt Stroke under exactly
 * two Clip Path leaves, within the original V1 4x4 sampler and cap.
 * Its visit envelope is the flattened segment endpoints expanded by
 * 0.5 device pixel (BOTH axes), then intersected with each frozen
 * Clip envelope. Counts every visited stroke and Clip edge test.
 * Fully separate from the prior clipped Fill work entry; not public
 * Source admission, general Stroke or additional raster. */
quantapdf_status quantapdf_ir_raster_render_v1_multi_clipped_stroke_visible_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_ir_raster_image *out_image);

/* Exact original TeXample 0.3985px stroke WIDTH isolated as a
 * private, bounded *single* open horizontal/vertical butt-capped
 * line under two independently owned Clip leaves. Uses the SAME
 * 4x4 V1 distance/coverage sampler and per-visited-edge quota.
 * Its bounding box expands by HALF the actual source width in both
 * device axes. Not a curved ring Stroke or Source admission, and
 * previous 1px signatures retain their strict width policy. */
quantapdf_status quantapdf_ir_raster_render_v1_multi_clipped_fractional_stroke_visible_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_ir_raster_image *out_image);

/* PRIVATE M4a: one original black 0.3985pt closed TWO-RING Bézier
 * Stroke after Source M3 has owned its complete original
 * 32-cubic W-star Clip / even-odd Fill / two Stroke paint order.
 * Source's actual closed
 * 12-command stroke PATH is borrowed without any replacement curves.
 * Only a 238x181 unscaled *private physical sample grid* qualifies;
 * this is NOT the public fractional-page device plan. Exactly two
 * smooth cardinal rings, 4 cubic segments per ring, miter join,
 * solid butt cap, opaque black and original width are mandatory.
 * Uses the SAME V1 4x4 kernel, per-segment inflated pixel domains and
 * the unchanged global max_sample_tests cap. Unqualified generalized
 * joins/curves/public original RGB remain independent gates. */
quantapdf_status quantapdf_ir_raster_render_v1_texample_closed_cubic_stroke_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* PRIVATE M4b physical-map qualification: same original closed Stroke
 * shape and same candidate-edge budget as M4a, retaining the real decimal
 * page and an explicit physical 72-DPI plan. No public Source cutover or
 * claim of full miter/three-layer parity is implied. */
quantapdf_status quantapdf_ir_raster_render_v1_texample_physical_cubic_stroke_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    quantapdf_ir_raster_image *out_image);

/* Same V1 Fill/Clip sample kernel, with DPI-defined geometry and the
 * physical page rectangle intersected at each 4x4 sample. Input coordinates
 * are already Source-staged Y-down around the exact decimal height. Samples beyond
 * the right/bottom decimal edge retain the initialized background. FIT
 * plans and every Stroke refuse; the special original above is separate. */
quantapdf_status quantapdf_ir_raster_render_v1_physical_planned(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_ir_raster_image *out_image);

/* PRIVATE full original Source M3 graph, physical 72-DPI grid: orange
 * even-odd Fill under its actual W* Clip, then both Q-restored black S.
 * Exact smooth authored tangents; interval cubic distance/ray classification
 * introduces no subdivision joins. One aggregate <=8M lease covers all
 * classification nodes and sample/ray tests before output publication.
 * On success out_sample_tests (optional) reports the exact deterministic
 * charged work; all failures reset it and the complete output. No public
 * Source cutover or alternate renderer. Inputs/outputs must not alias. */
quantapdf_status quantapdf_ir_raster_render_v1_texample_physical_three_layer(
    const quantapdf_ir *source,const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,size_t *out_sample_tests,
    quantapdf_ir_raster_image *out_image);

/* Same complete original proof as above, preserving Clip/paint identities
 * and sharing immutable geometry only after exact equality checks. The
 * repeated groups require 384 unique edges. The historical entry above
 * retains its 768-edge contract. Public V10 uses this entry under the
 * unchanged 512-edge/8M caller budgets; no sampler or pixel fallback. */
quantapdf_status quantapdf_ir_raster_render_v1_texample_shared_three_layer(
    const quantapdf_ir *source,const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,size_t *out_sample_tests,
    quantapdf_ir_raster_image *out_image);

/* Complete original at physical 96/144 DPI. Same cubic classifier,
 * actual DPI-scaled width, bounded certified Fill/Clip row intersection,
 * shared immutable admission geometry, unchanged public 512 edges/8M cap.
 * Historical 72-DPI entries and their exact work accounting stay unchanged. */
quantapdf_status quantapdf_ir_raster_render_v1_texample_physical_dpi_three_layer(
    const quantapdf_ir *source,const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,size_t *out_sample_tests,
    quantapdf_ir_raster_image *out_image);

/* Source-owned TrueType glyph contours and axis rectangles, preserved in
 * original paint/glyph order. Four source paints, 64 glyphs, 4096 commands, and the
 * existing 512-edge/8M/1M-pixel limits. Atomic output even on a late glyph. */
quantapdf_status quantapdf_ir_raster_render_v1_text_outlines(
    const quantapdf_ir *source,const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,size_t *out_sample_tests,quantapdf_ir_raster_image *out_image);

/* PRIVATE original TeXample-type two-ring Bézier W*: exactly
 * two nested cardinal ring contours, each MOVE+4 CUBIC+CLOSE,
 * with no discarded source edges or unbounded extra subpaths.
 * One borrowed, FULLY admitted opaque PATH Fill; the SAME C11 V1
 * 4x4 winding engine and strict visible-sample/flattened-edge
 * limits supply every output pixel. Not general Source admission. */
quantapdf_status quantapdf_ir_raster_render_v1_two_cubic_ring_wstar_visible_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *clip,
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

