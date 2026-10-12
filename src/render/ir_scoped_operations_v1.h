#ifndef QUANTAPDF_IR_SCOPED_OPERATIONS_V1_H
#define QUANTAPDF_IR_SCOPED_OPERATIONS_V1_H
#include "render/ir_scoped_operations_v0.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private C11: 2..4 opaque normal-blend PATH operations, each with its own
 * Graphics State and optional Clip Path/Intersection DAG (<=8 total nodes,
 * depth <=4, <=4 unique leaves per operation). Shared leaves/root IDs are
 * borrowed. No state leaks, no partial output, no PDFium/qpdf fallback. */
quantapdf_status quantapdf_ir_raster_scoped_operations_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);
#ifdef __cplusplus
}
#endif
#endif
