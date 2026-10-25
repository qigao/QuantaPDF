#include "page_composer_ir.h"

#include "document/page_content_ir.h"
#include "graph/graph_page_geometry.h"

#include <string.h>

quantapdf_status quantapdf_graph_snapshot_page_composer_ir(
    const quantapdf_graph_snapshot *snapshot, size_t page_index,
    size_t max_parent_steps,
    const quantapdf_graph_stream_limits *decode_limits,
    const quantapdf_pdf_content_limits *content_limits,
    size_t max_streams,
    const quantapdf_pdf_path_lower_limits *lower_limits,
    quantapdf_ir *out_ir)
{
    quantapdf_graph_page_geometry geometry;
    quantapdf_pdf_page_semantics *semantics = NULL;
    quantapdf_status status;
    quantapdf_graph_page_display display = {0};

    if (out_ir != NULL) memset(out_ir, 0, sizeof(*out_ir));
    if (out_ir == NULL || snapshot == NULL || decode_limits == NULL ||
        content_limits == NULL || lower_limits == NULL ||
        max_parent_steps == 0 || max_streams == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_graph_query_page_geometry(snapshot->graph,
        page_index, max_parent_steps, &geometry);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_graph_page_display_map(&geometry, &display);
    if (status != QUANTAPDF_OK) return status;

    /* A source page /Group may alter every pixel; reject unsupported values
     * before parsing content, allocating any Composer IR or invoking raster. */
    status = quantapdf_graph_admit_native_page_group(snapshot->graph,
        page_index);
    if (status != QUANTAPDF_OK) return status;

    status = quantapdf_graph_snapshot_page_semantics(snapshot, page_index,
        max_parent_steps, decode_limits, content_limits, max_streams, &semantics);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_pdf_path_lower_to_composer_mapped(semantics,
        display.width_points, display.height_points, display.from_source,
        lower_limits, out_ir);
    quantapdf_pdf_page_semantics_dispose(semantics);
    return status;
}
