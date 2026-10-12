#ifndef QUANTAPDF_IR_SCOPED_OPERATIONS_V0_H
#define QUANTAPDF_IR_SCOPED_OPERATIONS_V0_H

#include "render/ir_raster_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUANTAPDF_IR_SCOPED_OPERATIONS_V0_MAX_OPERATIONS ((size_t)4u)

/* Private, provider-free C11 renderer of 2..4 opaque solid PATH operations.
 * Each operation has its OWN independent normal graphics-state/clip scope:
 * no clip or one directly referenced Clip Path. Paint is evaluated with
 * shared 4x4 per-sample coverage and composited in IR operation order.
 *
 * Only one page, no stroke, no groups/soft-mask/intersection DAG, and
 * no unsupported resources. Entirely borrowed IR, detached malloc-owned
 * RGBA8 output. On any failure, output is reset and no partial pixels
 * escape. No PDFium/qpdf fallback or public ABI change. */
quantapdf_status quantapdf_ir_raster_scoped_operations_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
