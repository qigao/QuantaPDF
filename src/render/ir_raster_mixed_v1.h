#ifndef QUANTAPDF_IR_RASTER_MIXED_V1_H
#define QUANTAPDF_IR_RASTER_MIXED_V1_H

#include "render/ir_raster_v1.h"
#include "render/ir_raster_image_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private pure-C11 source-order composition, independent of qpdf/PDFium.
 * This intentionally tiny V1 profile admits EXACTLY two operations:
 * one opaque solid-filled general PATH and one normalized RAW Gray/RGB
 * IMAGE, in either order. No Clip, graphics state, font, Form, paint, soft
 * mask, link or outline is silently ignored. IMAGE uses integer STRETCH
 * bounds and an optional owned alpha plane. Path may use its existing
 * finite affine geometry. No public render API or PDFium fallback.
 *
 * Limits apply to BOTH layer operations: output pixels, Path edges and
 * subpixel tests, normalized source pixels and image destination pixels.
 * The source-owned IR is borrowed; success returns a detached RGBA image.
 * Any unsupported or insufficient-budget layer fails with no output. */
typedef struct quantapdf_ir_raster_mixed_v1_limits {
    size_t max_pixels;
    size_t max_edges;
    size_t max_sample_tests;
    size_t max_source_pixels;
    size_t max_drawn_pixels;
} quantapdf_ir_raster_mixed_v1_limits;

quantapdf_status quantapdf_ir_raster_mixed_path_image_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_mixed_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* V2 private C11 extension: exactly one opaque solid PATH plus two
 * uniquely referenced RAW images in ANY source order (3 operations total).
 * Every resource must be referenced exactly once in this profile. Both
 * image source-pixel and painted-destination-pixel budgets are charged
 * cumulatively across the two Image operations. Unsupported late paints
 * produce no partial output; V1's 1+1 contract remains unchanged.
 * The caller still owns the detached RGBA output. */
quantapdf_status quantapdf_ir_raster_mixed_path_two_images_v2(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_mixed_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* V3 private C11: EXACTLY TWO opaque PATH fills plus TWO uniquely owned RAW
 * images, in any of the six source-order interleavings (four operations).
 * Sum of Path edges/sample work is conservatively bounded by splitting the
 * provided budgets into two per-Path shares; cumulative Image input and
 * painted destination work remain charged. No state, Clip, alpha Path, font,
 * Form, special paint or PDFium fallback is admitted. V1/V2 counts remain
 * unchanged; all layers are prevalidated and rasterized before publication. */
quantapdf_status quantapdf_ir_raster_mixed_two_paths_two_images_v3(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_mixed_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
