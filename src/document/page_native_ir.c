#include "document/page_native_ir.h"

#include "graph/graph_page_geometry.h"

#include <string.h>

quantapdf_status quantapdf_graph_snapshot_page_native_ir(
    const quantapdf_graph_snapshot *snapshot, size_t page_index,
    size_t max_parent_steps,
    const quantapdf_graph_stream_limits *decode_limits,
    const quantapdf_pdf_content_limits *content_limits,
    size_t max_streams,
    const quantapdf_pdf_path_lower_limits *lower_limits,
    unsigned dpi, size_t max_pixels,
    quantapdf_ir *out_ir, quantapdf_native_device_plan *out_plan)
{
    quantapdf_graph_page_source_numbers numbers = {0};
    quantapdf_native_device_plan plan = {0};
    quantapdf_ir ir = {0};
    quantapdf_pdf_page_semantics *semantics = NULL;
    quantapdf_status status;

    if (out_ir != NULL) memset(out_ir, 0, sizeof(*out_ir));
    if (out_plan != NULL) memset(out_plan, 0, sizeof(*out_plan));
    if (out_ir == NULL || out_plan == NULL || snapshot == NULL ||
        decode_limits == NULL || content_limits == NULL ||
        lower_limits == NULL || max_parent_steps == 0u || max_streams == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_graph_query_page_source_numbers(snapshot->graph,
        page_index, max_parent_steps, &numbers);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_native_device_plan_create(numbers.media_box,
        numbers.crop_box, numbers.user_unit, numbers.rotation,
        dpi, max_pixels, &plan);
    if (status != QUANTAPDF_OK) return status;

    /* Bounds and page-wide transparency admission precede content decoding.
     * No device matrix is encoded as Composer float/four-decimal geometry. */
    status = quantapdf_graph_admit_native_page_group(snapshot->graph, page_index);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_graph_snapshot_page_semantics(snapshot, page_index,
        decode_limits, content_limits, max_streams, &semantics);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_pdf_path_lower_to_composer_mapped(semantics,
        plan.width_points, plan.height_points, plan.source_to_display,
        lower_limits, &ir);
    quantapdf_pdf_page_semantics_dispose(semantics);
    if (status != QUANTAPDF_OK) return status;
    if (ir.page_count != 1u || ir.pages == NULL ||
        !quantapdf_native_device_plan_matches_page(&plan,
            ir.pages[0].width_points, ir.pages[0].height_points, max_pixels)) {
        quantapdf_ir_dispose(&ir);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    *out_ir = ir;
    *out_plan = plan;
    return QUANTAPDF_OK;
}
