#ifndef QUANTAPDF_GRAPH_EXTGSTATE_H
#define QUANTAPDF_GRAPH_EXTGSTATE_H

#include "graph/graph.h"
#include "ir/pdf_graphics_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve every gs operator from a selected sealed Graph /Resources snapshot.
 * Nearest /Page or /Pages /Resources wins as a whole dictionary; lookup then
 * follows /ExtGState and the exact, slash-prefixed resource name. No QPDF
 * object handle, provider, parser or borrowed graph pointer escapes.
 * Only /Type, /ca, /CA, and a supported name-valued /BM are admitted.
 * Other ExtGState keys, blend lists and soft masks fail closed. The
 * returned index-aligned delta vector belongs to caller; free() it.
 * No GS operators: returns OK with NULL out_changes/count=0. */
quantapdf_status quantapdf_graph_resolve_page_extgstates(
    const quantapdf_graph *graph, size_t page_index, size_t max_parent_steps,
    const quantapdf_pdf_content_ir *content,
    quantapdf_pdf_extgstate_delta **out_changes, size_t *out_count);

#ifdef __cplusplus
}
#endif
#endif
