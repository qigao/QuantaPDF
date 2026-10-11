#include "page_content_ir.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct bounded_content {
    unsigned char *data;
    size_t size, capacity;
} bounded_content;

static quantapdf_status append(bounded_content *output, const void *bytes,
    size_t size, size_t limit)
{
    size_t needed, next;
    unsigned char *buffer;
    if (output->size > limit || size > limit - output->size)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    needed = output->size + size;
    if (needed > output->capacity) {
        next = output->capacity == 0 ? 16 : output->capacity;
        if (next > limit) next = limit;
        while (next < needed) {
            if (next > limit - next) next = limit;
            else next *= 2;
        }
        if (next < needed) return QUANTAPDF_ERROR_UNSUPPORTED;
        buffer = (unsigned char *)realloc(output->data, next);
        if (buffer == NULL) return QUANTAPDF_ERROR_NOMEM;
        output->data = buffer;
        output->capacity = next;
    }
    if (size) memcpy(output->data + output->size, bytes, size);
    output->size = needed;
    return QUANTAPDF_OK;
}

void quantapdf_pdf_page_semantics_dispose(quantapdf_pdf_page_semantics *page)
{
    if (page == NULL) return;
    quantapdf_pdf_graphics_dispose(page->graphics);
    quantapdf_pdf_content_dispose(page->content);
    free(page);
}

quantapdf_status quantapdf_graph_snapshot_page_semantics(
    const quantapdf_graph_snapshot *snapshot, size_t page_index,
    const quantapdf_graph_stream_limits *decode_limits,
    const quantapdf_pdf_content_limits *parse_limits,
    size_t max_streams, quantapdf_pdf_page_semantics **out_page)
{
    const quantapdf_graph_model *model;
    const quantapdf_graph_node *contents;
    const quantapdf_graph_edge *entry;
    quantapdf_local_id root = 0;
    quantapdf_pdf_page_semantics *page = NULL;
    quantapdf_status status = QUANTAPDF_OK;
    bounded_content joined = {0};
    size_t count = 0;
    int array = 0;
    if (out_page != NULL) *out_page = NULL;
    if (out_page == NULL || snapshot == NULL || decode_limits == NULL ||
        parse_limits == NULL || max_streams == 0 ||
        decode_limits->max_decoded_bytes == 0 ||
        decode_limits->max_direct_depth == 0 ||
        decode_limits->max_direct_visits == 0 ||
        parse_limits->max_input_bytes == 0 ||
        parse_limits->max_operators == 0 ||
        parse_limits->max_graphics_depth == 0) return QUANTAPDF_ERROR_ARGUMENT;
    model = quantapdf_graph_view(snapshot->graph);
    if (model == NULL) return QUANTAPDF_ERROR_STATE;
    if (page_index >= model->page_count) return QUANTAPDF_ERROR_ARGUMENT;
    root = model->edges[model->page_offset + page_index].target;
    entry = quantapdf_graph_find(model, root, "/Contents", 9);
    if (entry != NULL) {
        root = entry->target;
        contents = quantapdf_graph_node_at(model, root);
        if (contents == NULL) return QUANTAPDF_ERROR_BACKEND;
        if (contents->kind == QUANTAPDF_GRAPH_STREAM) count = 1;
        else if (contents->kind == QUANTAPDF_GRAPH_ARRAY) {
            array = 1;
            count = contents->edge_count;
        } else if (contents->kind != QUANTAPDF_GRAPH_NULL)
            return QUANTAPDF_ERROR_FORMAT;
    }
    if (count > max_streams) return QUANTAPDF_ERROR_UNSUPPORTED;
    /* Validate every member before any potentially expensive provider replay. */
    for (size_t i = 0; i < count; ++i) {
        quantapdf_local_id id = array ?
            model->edges[model->nodes[root - 1].edge_offset + i].target : root;
        const quantapdf_graph_node *node = quantapdf_graph_node_at(model, id);
        if (node == NULL || node->kind != QUANTAPDF_GRAPH_STREAM)
            return QUANTAPDF_ERROR_FORMAT;
    }
    page = (quantapdf_pdf_page_semantics *)calloc(1, sizeof(*page));
    if (page == NULL) return QUANTAPDF_ERROR_NOMEM;
    for (size_t i = 0; i < count; ++i) {
        quantapdf_local_id id = array ?
            model->edges[model->nodes[root - 1].edge_offset + i].target : root;
        quantapdf_graph_reference reference;
        quantapdf_graph_stream_limits local_limits = *decode_limits;
        unsigned char *decoded = NULL;
        size_t size = 0, remaining;
        if (i != 0) {
            static const char separator = '\n';
            status = append(&joined, &separator, 1, parse_limits->max_input_bytes);
            if (status != QUANTAPDF_OK) goto fail;
        }
        remaining = parse_limits->max_input_bytes - joined.size;
        if (remaining == 0) { status = QUANTAPDF_ERROR_UNSUPPORTED; goto fail; }
        if (local_limits.max_decoded_bytes > remaining)
            local_limits.max_decoded_bytes = remaining;
        status = quantapdf_graph_reference_at(snapshot->graph, id, &reference);
        if (status != QUANTAPDF_OK) goto fail;
        status = quantapdf_graph_snapshot_stream(snapshot, &reference,
            &local_limits, &decoded, &size);
        if (status != QUANTAPDF_OK) goto fail;
        status = append(&joined, decoded, size, parse_limits->max_input_bytes);
        quantapdf_free(decoded);
        if (status != QUANTAPDF_OK) goto fail;
    }
    status = quantapdf_pdf_content_parse(joined.data, joined.size,
        parse_limits, &page->content);
    if (status != QUANTAPDF_OK) goto fail;
    status = quantapdf_pdf_graphics_evaluate(page->content,
        parse_limits, &page->graphics);
    if (status != QUANTAPDF_OK) goto fail;
    free(joined.data);
    *out_page = page;
    return QUANTAPDF_OK;
fail:
    free(joined.data);
    quantapdf_pdf_page_semantics_dispose(page);
    return status;
}
