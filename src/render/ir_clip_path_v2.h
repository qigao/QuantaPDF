#ifndef QUANTAPDF_IR_CLIP_PATH_V2_H
#define QUANTAPDF_IR_CLIP_PATH_V2_H

#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One opaque PATH fill and one Clip Path resource on one normal full-alpha
 * graphics state. Winding and antialias coverage intersect at each 4x4
 * subpixel sample; clip and draw paths may contain cubic curves, holes,
 * fractions and finite nonsingular affine transforms.
 * Unsupported resource graphs, strokes, groups, multiple operations etc.
 * fail closed. Pure private C11, no qpdf/PDFium dependency or fallback. */
quantapdf_status quantapdf_ir_raster_clip_path_v2(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
