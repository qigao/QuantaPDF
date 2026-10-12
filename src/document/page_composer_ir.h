#ifndef QUANTAPDF_PAGE_COMPOSER_IR_H
#define QUANTAPDF_PAGE_COMPOSER_IR_H

#include "snapshot/graph_snapshot.h"
#include "ir/pdf_path_lowering.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Selected immutable page -> fully detached, single-page Composer IR.
 * Resolve and validate page-tree ancestry, inherited boxes/rotation and the
 * leaf's UserUnit BEFORE decoding page content. An explicit source-to-canvas
 * mapping supports translated/cropped/rotated/scaled pages when exactly
 * representable by the strict path-only lowering. Output is
 * zeroed on all failures; successful ownership uses quantapdf_ir_dispose. */
quantapdf_status quantapdf_graph_snapshot_page_composer_ir(
    const quantapdf_graph_snapshot *snapshot, size_t page_index,
    size_t max_parent_steps,
    const quantapdf_graph_stream_limits *decode_limits,
    const quantapdf_pdf_content_limits *content_limits,
    size_t max_streams,
    const quantapdf_pdf_path_lower_limits *lower_limits,
    quantapdf_ir *out_ir);

#ifdef __cplusplus
}
#endif
#endif
