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

#ifdef __cplusplus
}
#endif

#endif
