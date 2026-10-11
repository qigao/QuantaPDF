#include "graph_query.h"

#include <string.h>

const char *quantapdf_metadata_key(quantapdf_metadata_field field)
{
    switch (field) {
    case QUANTAPDF_METADATA_TITLE: return "/Title";
    case QUANTAPDF_METADATA_AUTHOR: return "/Author";
    case QUANTAPDF_METADATA_SUBJECT: return "/Subject";
    case QUANTAPDF_METADATA_KEYWORDS: return "/Keywords";
    case QUANTAPDF_METADATA_CREATOR: return "/Creator";
    case QUANTAPDF_METADATA_PRODUCER: return "/Producer";
    case QUANTAPDF_METADATA_CREATION_DATE: return "/CreationDate";
    case QUANTAPDF_METADATA_MODIFICATION_DATE: return "/ModDate";
    default: return NULL;
    }
}

quantapdf_status quantapdf_graph_query_metadata(const quantapdf_graph *graph,
    quantapdf_metadata_field field, quantapdf_graph_text_view *out_text)
{
    const quantapdf_graph_model *model;
    const quantapdf_graph_edge *edge;
    const quantapdf_graph_node *node;
    const char *key = quantapdf_metadata_key(field);
    if (out_text == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_text, 0, sizeof(*out_text));
    if (graph == NULL || key == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    model = quantapdf_graph_view(graph);
    if (model == NULL) return QUANTAPDF_ERROR_STATE;
    edge = quantapdf_graph_find(model, model->trailer, "/Info", 5);
    if (edge == NULL) return QUANTAPDF_OK;
    node = quantapdf_graph_node_at(model, edge->target);
    if (node->kind == QUANTAPDF_GRAPH_NULL) return QUANTAPDF_OK;
    if (node->kind != QUANTAPDF_GRAPH_DICTIONARY) return QUANTAPDF_ERROR_FORMAT;
    edge = quantapdf_graph_find(model, edge->target, key, strlen(key));
    if (edge == NULL) return QUANTAPDF_OK;
    node = quantapdf_graph_node_at(model, edge->target);
    if (node->kind == QUANTAPDF_GRAPH_NULL) return QUANTAPDF_OK;
    if (node->kind != QUANTAPDF_GRAPH_STRING) return QUANTAPDF_ERROR_FORMAT;
    out_text->data = node->data_size == 0 ? NULL : model->bytes + node->data_offset;
    out_text->size = node->data_size;
    out_text->present = 1;
    return QUANTAPDF_OK;
}
