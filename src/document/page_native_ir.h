#ifndef QUANTAPDF_PAGE_NATIVE_IR_H
#define QUANTAPDF_PAGE_NATIVE_IR_H

#include "document/page_composer_ir.h"
#include "render/native_device_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private, transactional selected-page projection for the explicit Native
 * FIT device path. Exact selected Graph numeric spellings determine the
 * checked device grid; semantic source coordinates are lowered unchanged
 * under the existing Composer admission contract. No provider handles or
 * Graph pointers escape. Dispose the successful IR with quantapdf_ir_dispose;
 * the plan is a plain owned value. Both outputs are cleared on every error.
 * This does not select a public SDK backend or add a raster fallback. */
quantapdf_status quantapdf_graph_snapshot_page_native_ir(
    const quantapdf_graph_snapshot *snapshot, size_t page_index,
    size_t max_parent_steps,
    const quantapdf_graph_stream_limits *decode_limits,
    const quantapdf_pdf_content_limits *content_limits,
    size_t max_streams,
    const quantapdf_pdf_path_lower_limits *lower_limits,
    unsigned dpi, size_t max_pixels,
    quantapdf_ir *out_ir, quantapdf_native_device_plan *out_plan);

#ifdef __cplusplus
}
#endif
#endif
