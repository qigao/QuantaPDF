#include "graph_query.h"

#include <string.h>

static quantapdf_status cursor_begin(const quantapdf_graph *graph,
    quantapdf_graph_cursor *cursor)
{
    if (cursor == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(cursor, 0, sizeof(*cursor));
    if (graph == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    if (quantapdf_graph_view(graph) == NULL) return QUANTAPDF_ERROR_STATE;
    cursor->graph = graph;
    return QUANTAPDF_OK;
}

static quantapdf_status match_reset(quantapdf_graph_match *match, int *found)
{
    if (match != NULL) memset(match, 0, sizeof(*match));
    if (found != NULL) *found = 0;
    return match != NULL && found != NULL ? QUANTAPDF_OK : QUANTAPDF_ERROR_ARGUMENT;
}

static quantapdf_status member_match(const quantapdf_graph *graph,
    quantapdf_local_id owner, size_t index, size_t ordinal, quantapdf_graph_match *match)
{
    const quantapdf_graph_model *model = quantapdf_graph_view(graph);
    const quantapdf_graph_edge *edge = &model->edges[index];
    quantapdf_status status = quantapdf_graph_reference_at(graph, edge->target, &match->reference);
    if (status == QUANTAPDF_OK && owner != 0)
        status = quantapdf_graph_reference_at(graph, owner, &match->owner);
    if (status != QUANTAPDF_OK) { memset(match, 0, sizeof(*match)); return status; }
    match->node = quantapdf_graph_node_at(model, edge->target);
    match->name = edge->key_size == 0 ? NULL : model->bytes + edge->key_offset;
    match->name_size = edge->key_size;
    match->ordinal = ordinal;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_query_objects(const quantapdf_graph *graph,
    quantapdf_graph_cursor *out_cursor)
{
    quantapdf_status status = cursor_begin(graph, out_cursor);
    if (status == QUANTAPDF_OK) {
        out_cursor->objects = 1;
        out_cursor->count = quantapdf_graph_view(graph)->node_count;
    }
    return status;
}

quantapdf_status quantapdf_graph_query_pages(const quantapdf_graph *graph,
    quantapdf_graph_cursor *out_cursor)
{
    quantapdf_status status = cursor_begin(graph, out_cursor);
    if (status == QUANTAPDF_OK) {
        const quantapdf_graph_model *model = quantapdf_graph_view(graph);
        out_cursor->offset = model->page_offset;
        out_cursor->count = model->page_count;
    }
    return status;
}

quantapdf_status quantapdf_graph_query_members(const quantapdf_graph *graph,
    const quantapdf_graph_reference *reference, quantapdf_graph_cursor *out_cursor)
{
    quantapdf_local_id id;
    const quantapdf_graph_node *node;
    quantapdf_status status = cursor_begin(graph, out_cursor);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_graph_resolve(graph, reference, &id);
    if (status == QUANTAPDF_OK) {
        node = quantapdf_graph_node_at(quantapdf_graph_view(graph), id);
        if (node->kind != QUANTAPDF_GRAPH_ARRAY && node->kind != QUANTAPDF_GRAPH_DICTIONARY &&
            node->kind != QUANTAPDF_GRAPH_STREAM) status = QUANTAPDF_ERROR_ARGUMENT;
        else {
            out_cursor->owner = id;
            out_cursor->offset = node->edge_offset;
            out_cursor->count = node->edge_count;
        }
    }
    if (status != QUANTAPDF_OK) memset(out_cursor, 0, sizeof(*out_cursor));
    return status;
}

quantapdf_status quantapdf_graph_query_next(quantapdf_graph_cursor *cursor,
    quantapdf_graph_match *out_match, int *out_found)
{
    quantapdf_status status = match_reset(out_match, out_found);
    const quantapdf_graph_model *model;
    if (status != QUANTAPDF_OK) return status;
    if (cursor == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    model = quantapdf_graph_view(cursor->graph);
    if (model == NULL) return QUANTAPDF_ERROR_STATE;
    if (cursor->next == cursor->count) return QUANTAPDF_OK;
    if (cursor->objects) {
        quantapdf_local_id id = (quantapdf_local_id)(cursor->next + 1);
        status = quantapdf_graph_reference_at(cursor->graph, id, &out_match->reference);
        if (status != QUANTAPDF_OK) return status;
        out_match->node = quantapdf_graph_node_at(model, id);
        out_match->ordinal = cursor->next;
    } else {
        status = member_match(cursor->graph, cursor->owner,
            cursor->offset + cursor->next, cursor->next, out_match);
        if (status != QUANTAPDF_OK) return status;
    }
    ++cursor->next; *out_found = 1;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_query_member(const quantapdf_graph *graph,
    const quantapdf_graph_reference *reference, const void *key, size_t key_size,
    quantapdf_graph_match *out_match, int *out_found)
{
    quantapdf_graph_reference selected = {0};
    quantapdf_local_id id;
    const quantapdf_graph_model *model;
    const quantapdf_graph_node *node;
    const quantapdf_graph_edge *edge;
    quantapdf_status status;
    if (reference != NULL) selected = *reference;
    status = match_reset(out_match, out_found);
    if (status != QUANTAPDF_OK) return status;
    if (key == NULL || key_size == 0 || ((const unsigned char *)key)[0] != '/')
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_graph_resolve(graph, reference == NULL ? NULL : &selected, &id);
    if (status != QUANTAPDF_OK) return status;
    model = quantapdf_graph_view(graph); node = quantapdf_graph_node_at(model, id);
    if (node->kind != QUANTAPDF_GRAPH_DICTIONARY && node->kind != QUANTAPDF_GRAPH_STREAM)
        return QUANTAPDF_ERROR_ARGUMENT;
    edge = quantapdf_graph_find(model, id, key, key_size);
    if (edge == NULL) return QUANTAPDF_OK;
    status = member_match(graph, id, (size_t)(edge - model->edges),
        (size_t)(edge - model->edges) - node->edge_offset, out_match);
    if (status == QUANTAPDF_OK) *out_found = 1;
    return status;
}

static int named_type(const quantapdf_graph_model *model, quantapdf_local_id id, const char *type)
{
    const quantapdf_graph_edge *edge = quantapdf_graph_find(model, id, "/Type", 5);
    const quantapdf_graph_node *node = edge == NULL ? NULL : quantapdf_graph_node_at(model, edge->target);
    return node != NULL && node->kind == QUANTAPDF_GRAPH_NAME &&
        node->data_size == strlen(type) && memcmp(model->bytes + node->data_offset, type, node->data_size) == 0;
}

quantapdf_status quantapdf_graph_query_resources(const quantapdf_graph *graph,
    size_t page_index, size_t max_parent_steps, quantapdf_graph_match *out_match, int *out_found)
{
    const quantapdf_graph_model *model;
    quantapdf_local_id current;
    size_t step, limit;
    quantapdf_status status = match_reset(out_match, out_found);
    if (status != QUANTAPDF_OK) return status;
    if (graph == NULL || max_parent_steps == 0) return QUANTAPDF_ERROR_ARGUMENT;
    model = quantapdf_graph_view(graph);
    if (model == NULL) return QUANTAPDF_ERROR_STATE;
    if (page_index >= model->page_count) return QUANTAPDF_ERROR_ARGUMENT;
    current = model->edges[model->page_offset + page_index].target;
    limit = max_parent_steps < model->node_count ? max_parent_steps : model->node_count;
    for (step = 0; step < limit; ++step) {
        const quantapdf_graph_node *node = &model->nodes[current - 1];
        const quantapdf_graph_edge *edge;
        if (node->kind != QUANTAPDF_GRAPH_DICTIONARY ||
            !named_type(model, current, step == 0 ? "/Page" : "/Pages")) return QUANTAPDF_ERROR_FORMAT;
        edge = quantapdf_graph_find(model, current, "/Resources", 10);
        if (edge != NULL && model->nodes[edge->target - 1].kind != QUANTAPDF_GRAPH_NULL) {
            if (model->nodes[edge->target - 1].kind != QUANTAPDF_GRAPH_DICTIONARY)
                return QUANTAPDF_ERROR_FORMAT;
            status = member_match(graph, current, (size_t)(edge - model->edges),
                (size_t)(edge - model->edges) - node->edge_offset, out_match);
            if (status == QUANTAPDF_OK) *out_found = 1;
            return status;
        }
        edge = quantapdf_graph_find(model, current, "/Parent", 7);
        if (edge == NULL || model->nodes[edge->target - 1].kind == QUANTAPDF_GRAPH_NULL) return QUANTAPDF_OK;
        current = edge->target;
    }
    return limit == model->node_count ? QUANTAPDF_ERROR_FORMAT : QUANTAPDF_ERROR_UNSUPPORTED;
}

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
    quantapdf_graph_reference root;
    quantapdf_graph_match match;
    quantapdf_status status;
    int found;
    const char *key = quantapdf_metadata_key(field);
    if (out_text == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_text, 0, sizeof(*out_text));
    if (graph == NULL || key == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    model = quantapdf_graph_view(graph);
    if (model == NULL) return QUANTAPDF_ERROR_STATE;
    status = quantapdf_graph_reference_at(graph, model->trailer, &root);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_graph_query_member(graph, &root, "/Info", 5, &match, &found);
    if (status != QUANTAPDF_OK || !found) return status;
    if (match.node->kind == QUANTAPDF_GRAPH_NULL) return QUANTAPDF_OK;
    if (match.node->kind != QUANTAPDF_GRAPH_DICTIONARY) return QUANTAPDF_ERROR_FORMAT;
    status = quantapdf_graph_query_member(graph, &match.reference, key, strlen(key), &match, &found);
    if (status != QUANTAPDF_OK || !found) return status;
    if (match.node->kind == QUANTAPDF_GRAPH_NULL) return QUANTAPDF_OK;
    if (match.node->kind != QUANTAPDF_GRAPH_STRING) return QUANTAPDF_ERROR_FORMAT;
    out_text->data = match.node->data_size == 0 ? NULL : model->bytes + match.node->data_offset;
    out_text->size = match.node->data_size;
    out_text->present = 1;
    return QUANTAPDF_OK;
}
