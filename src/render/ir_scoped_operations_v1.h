#ifndef QUANTAPDF_IR_SCOPED_OPERATIONS_V1_H
#define QUANTAPDF_IR_SCOPED_OPERATIONS_V1_H
#include "render/ir_scoped_operations_v0.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private C11: 1..4 opaque normal-blend PATH operations, each with its own
 * Graphics State and optional Clip Path/Intersection DAG (<=8 total nodes,
 * depth <=4, <=4 unique leaves per operation). Shared leaves/root IDs are
 * borrowed. No state leaks, no partial output, no PDFium/qpdf fallback. */
quantapdf_status quantapdf_ir_raster_scoped_operations_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* Explicit device plan version: all operations and Clip leaves share one
 * checked FIT grid and device affine. IR/plan are borrowed synchronously;
 * neither is changed and no Source/Graph/provider ownership is retained. */
quantapdf_status quantapdf_ir_raster_scoped_operations_v1_planned(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    quantapdf_ir_raster_image *out_image);
/* Explicit expanded profile: opaque linear multi-segment strokes are
 * converted to a bounded nonzero outline before sampling. Butt/square caps,
 * miter/bevel joins, positive widths/dashes and affine transforms; other Stroke
 * features reject atomically. The original V1/M2 profile stays unchanged. */
quantapdf_status quantapdf_ir_raster_scoped_operations_v2(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,quantapdf_ir_raster_image *out_image);
quantapdf_status quantapdf_ir_raster_scoped_operations_v2_planned(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,const quantapdf_native_device_plan *plan,
    quantapdf_ir_raster_image *out_image);
#ifdef __cplusplus
}
#endif
#endif
