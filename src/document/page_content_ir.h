#ifndef QUANTAPDF_PAGE_CONTENT_IR_H
#define QUANTAPDF_PAGE_CONTENT_IR_H

#include "snapshot/graph_snapshot.h"
#include "ir/pdf_graphics_state.h"

typedef struct quantapdf_pdf_page_semantics {
    quantapdf_pdf_content_ir *content;
    quantapdf_pdf_graphics_ir *graphics;
} quantapdf_pdf_page_semantics;

#ifdef __cplusplus
extern "C" {
#endif
/* Private whole-page path-only admission. /Contents stream or array is
 * decoded from the selected immutable graph version in PDF array order.
 * A newline separates streams; operands, graphics state and path state
 * may span stream boundaries. All budgets, including max_streams, are
 * explicit. Unsupported resources/text/images fail atomically.
 * Detached result retains no graph, stream, parser or context pointers. */
quantapdf_status quantapdf_graph_snapshot_page_semantics(
    const quantapdf_graph_snapshot *snapshot, size_t page_index,
    const quantapdf_graph_stream_limits *decode_limits,
    const quantapdf_pdf_content_limits *parse_limits,
    size_t max_streams, quantapdf_pdf_page_semantics **out_page);
void quantapdf_pdf_page_semantics_dispose(quantapdf_pdf_page_semantics *page);
#ifdef __cplusplus
}
#endif
#endif
