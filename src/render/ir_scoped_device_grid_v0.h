#ifndef QUANTAPDF_IR_SCOPED_DEVICE_GRID_V0_H
#define QUANTAPDF_IR_SCOPED_DEVICE_GRID_V0_H

#include "render/ir_scoped_operations_v1.h"
#include "render/native_device_grid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Explicit private C11 opt-in; NEVER replaces the historical integer-only
 * scoped entry point. No PDFium, qpdf, source lifetime or fallback.
 *
 * V0 subset: float-backed detached IR with independently supplied precise
 * displayed-page geometry, zero origin/UserUnit=1/no rotation, solid opaque
 * fill-only Path operations, identity source transforms and bounded Clip DAGs.
 * Source ownership stays with the caller; the output is uniquely owned RGBA.
 * Unsupported stroke, raw CropBox mapping, nonidentity transforms and
 * imprecise geometry reject without publishing pixels. */
quantapdf_status quantapdf_ir_raster_scoped_device_grid_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
