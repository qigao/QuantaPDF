#ifndef QUANTAPDF_IR_RASTER_SCOPED_MIXED_V5_H
#define QUANTAPDF_IR_RASTER_SCOPED_MIXED_V5_H

#include "render/ir_raster_image_clip_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private, reusable bounded C11 Path/RAW Image compositor.
 *
 * Admits one integer-size page with 1..4 source-ordered opaque PATH
 * fills and/or normalized RAW Gray/RGB Images. Homogeneous IMAGE-only
 * pages may use four layers only without Clip or GS. The older clipped
 * IMAGE-only profile permits at most two paints under one frozen Clip
 * PATH leaf and a single GS (full alpha, Normal blend, no soft-mask).
 * Mixed PATH/IMAGE keeps its independent strict admission.
 * Per-operation GS identity is immutable; 0 means Q restored no Clip.
 *
 * Every resource must be reachable, all image input, destination sampling
 * and geometry work must fit explicit budgets. No text, Form, Stroke,
 * XObject decoding, implicit alpha group, other GS, or PDFium/qpdf fallback.
 * This is NOT the public page render API or an all-PDF compatibility mode.
 * Borrowed IR is never modified, and failure publishes a zeroed output. */
typedef struct quantapdf_ir_raster_scoped_mixed_v5_limits {
    size_t max_pixels;
    size_t max_operations;
    size_t max_edges;
    size_t max_sample_tests;
    size_t max_source_pixels;
    size_t max_drawn_pixels;
} quantapdf_ir_raster_scoped_mixed_v5_limits;

quantapdf_status quantapdf_ir_raster_scoped_mixed_v5(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* Explicit opt-in for device-projected Source V10 96-DPI IMAGE bounds.
 * Uses the same V5 compositor and 4x4 PATH/Clip sampler. Original
 * integer-only callers retain their exact rejection and ownership. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_device(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* V10 original fractional physical page width/height. Integer canvas
 * ceil is separately checked by the original native C11 device plan.
 * Internal Clip shares the existing 4x4 PATH/W Clip/RAW Image sample. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image);


/* PRIVATE opt-in: two owned bounded Clip Path leaves plus one flat
 * Intersection root, one Normal/full-alpha GS (clip_id=3). Same 4x4
 * C11 sampler for original PATH/RAW Image and physical viewport.
 * Native Source V10 selects this graph after bounded two-leaf admission;
 * all older V5 entrypoints retain their original one-Clip contract. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image);

/* Explicit bounded PATH-only profile: one..four opaque PATH paints,
 * no Image or hidden resources, and exactly one GS naming one frozen
 * Clip leaf or two leaves plus their flat Intersection root. All paints
 * start in GS=1; an optional Q-restored GS=0 suffix is final. PATH and
 * Clip commands contain four..nine elements. The same V1 sampler tests
 * PATH, Clip leaves and fractional viewport at each shared subpixel.
 * Legacy entrypoints retain their refusal of clipped PATH-only input. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_clipped_paths_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
