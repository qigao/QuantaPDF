#ifndef QUANTAPDF_IR_SCOPED_OPERATIONS_V1_H
#define QUANTAPDF_IR_SCOPED_OPERATIONS_V1_H
#include "render/ir_scoped_operations_v0.h"
#ifdef __cplusplus
extern "C" {
#endif
#define QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS ((size_t)6u)

/* Private C11: 1..6 opaque normal-blend PATH operations, each with its own
 * Graphics State and optional Clip Path/Intersection DAG (<=9 total nodes,
 * depth <=4, <=4 unique leaves per operation). Only unused prefix intersections
 * proven as ordered suffixes of later used roots are permitted. Unrelated or
 * malformed resources fail closed. No partial output or PDFium/qpdf fallback. */
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
/* Explicit expanded profile: opaque linear/solid cubic multi-segment strokes
 * become a bounded nonzero outline before sampling. Butt/square/round caps,
 * miter/bevel/round joins, positive widths, nonnegative linear dashes and
 * authored/FIT transforms are supported; other Stroke
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
