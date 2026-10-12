#ifndef QUANTAPDF_IR_RASTER_V1_H
#define QUANTAPDF_IR_RASTER_V1_H

#include "render/ir_raster.h"

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
 * *closed triangle*, opaque, solid **one-device-pixel stroke** with butt cap,
 * miter join, default miter=10, no dashes and an isometric transform.
 * Unsupported widths/joins/transforms still fail closed. Optional canonical
 * Clip Path intersection is borrowed only by the _clipped entry points;
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

#ifdef __cplusplus
}
#endif

#endif
