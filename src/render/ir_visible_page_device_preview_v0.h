#ifndef QUANTAPDF_IR_VISIBLE_PAGE_DEVICE_PREVIEW_V0_H
#define QUANTAPDF_IR_VISIBLE_PAGE_DEVICE_PREVIEW_V0_H

#include "graph/graph_page_device.h"
#include "render/ir_scoped_operations_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Explicit PRIVATE test-only fill + W Clip positive path.
 * The Graph->Composer lowering has ALREADY applied MediaBox/CropBox/Rotate/
 * UserUnit to Path and Clip transforms. This adapter only projects those
 * immutable VISIBLE Composer transforms to the bounded integer device grid.
 *
 * First qualified subset: exactly one opaque Normal nonzero-Fill PATH,
 * one normal Graphics State, one nonzero W Clip Path, and the source's
 * default identity content CTM (recognised by the precise page-only
 * Composer affine). No stroke, cubic, additional operators, clip DAG,
 * requested rotation, nonopaque state, automatic fallback, or public SDK.
 *
 * The output is uniquely owned RGBA8; caller uses the existing dispose.
 * Failures reset output and publish no pixels/dimensions. The source IR and
 * every Path/Clip command remain borrowed and unchanged.
 */
quantapdf_status quantapdf_ir_raster_visible_page_fillclip_preview_v0(
    const quantapdf_ir *normalized_ir, size_t page_index,
    const quantapdf_graph_page_geometry *source_geometry,
    unsigned int dpi,
    const quantapdf_ir_raster_v1_limits *limits,
    size_t max_rgba_bytes,
    quantapdf_ir_raster_image *out_image);

/* #360 private, explicit opt-in ONLY. The sealed Graph must have computed
 * exact_grid via quantapdf_graph_page_decimal_handoff_v0 BEFORE it was dropped.
 * The caller retains a detached owned IR and copied page_geometry. Both must
 * outlive this synchronous call. No source/Graph/provider pointer escapes.
 *
 * Reuses the same strict one-Fill + one nonzero-W-Clip subset and raster as
 * preview_v0; does NOT recompute integer extents from binary64 or add a
 * fallback. Checks the supplied value grid's byte budgets and six binary64
 * affine coefficients against the existing page-display projection.
 * No general Path/Clip/FIll PDFium pixel parity is asserted by this API.
 * Not exported through include/quantapdf or used by installed SDK.
 */
quantapdf_status quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
    const quantapdf_ir *normalized_ir, size_t page_index,
    const quantapdf_graph_page_geometry *source_geometry,
    const quantapdf_graph_device_grid *exact_grid, unsigned int dpi,
    const quantapdf_ir_raster_v1_limits *limits,
    size_t max_rgba_bytes,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
