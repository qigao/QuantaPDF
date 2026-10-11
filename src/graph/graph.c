#include "graph.h"

#include <stdlib.h>
#include <string.h>

struct quantapdf_graph {
    quantapdf_graph_model model;
    /* Mutable owners; model pointers are derived const aliases for readers. */
    quantapdf_graph_node *nodes;
    quantapdf_graph_edge *edges;
    unsigned char *bytes;
    quantapdf_graph_limits limits;
    size_t node_capacity, edge_capacity, byte_capacity, storage;
    size_t value_bytes, stream_bytes, defined_count;
    quantapdf_local_id active;
    int sealed;
};

static quantapdf_status grow(quantapdf_graph *g, void *old_data, size_t *capacity,
    size_t needed, size_t maximum, size_t element, void **out_data)
{
    size_t available, next, increase;
    void *data;
    *out_data = old_data;
    if (needed > maximum || needed > SIZE_MAX / element)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (needed <= *capacity)
        return QUANTAPDF_OK;
    available = (g->limits.max_storage_bytes - g->storage) / element;
    if (needed - *capacity > available)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    increase = *capacity == 0 ? 8 : *capacity;
    if (increase > maximum - *capacity)
        increase = maximum - *capacity;
    if (increase > available)
        increase = available;
    next = *capacity + increase;
    if (next < needed)
        next = needed;
    data = realloc(old_data, next * element);
    if (data == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    g->storage += (next - *capacity) * element;
    *capacity = next;
    *out_data = data;
    return QUANTAPDF_OK;
}

static quantapdf_status grow_edges(quantapdf_graph *g, size_t needed)
{
    void *data;
    quantapdf_status status = grow(g, g->edges, &g->edge_capacity,
        needed, g->limits.max_edges, sizeof(*g->edges), &data);
    g->edges = (quantapdf_graph_edge *)data;
    g->model.edges = g->edges;
    return status;
}

static quantapdf_status grow_bytes(quantapdf_graph *g, size_t size, int stream)
{
    void *data;
    size_t used = stream ? g->stream_bytes : g->value_bytes;
    size_t maximum = stream ? g->limits.max_stream_bytes : g->limits.max_value_bytes;
    quantapdf_status status;
    if (size > maximum - used || size > SIZE_MAX - g->model.byte_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status = grow(g, g->bytes, &g->byte_capacity,
        g->model.byte_count + size, SIZE_MAX, 1, &data);
    g->bytes = (unsigned char *)data;
    g->model.bytes = g->bytes;
    return status;
}

quantapdf_status quantapdf_graph_create(
    const quantapdf_graph_limits *limits, quantapdf_graph **out_graph)
{
    quantapdf_graph *g;
    if (out_graph == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_graph = NULL;
    if (limits == NULL || limits->max_nodes == 0 || limits->max_nodes > UINT32_MAX ||
        limits->max_nodes > SIZE_MAX / sizeof(quantapdf_graph_node) ||
        limits->max_edges > SIZE_MAX / sizeof(quantapdf_graph_edge) ||
        limits->max_storage_bytes < sizeof(*g) || limits->max_depth == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    g = (quantapdf_graph *)calloc(1, sizeof(*g));
    if (g == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    g->limits = *limits;
    g->storage = sizeof(*g);
    *out_graph = g;
    return QUANTAPDF_OK;
}

void quantapdf_graph_drop(quantapdf_graph *g)
{
    if (g == NULL)
        return;
    free(g->nodes);
    free(g->edges);
    free(g->bytes);
    free(g);
}

quantapdf_status quantapdf_graph_reserve_node(
    quantapdf_graph *g, int indirect, quantapdf_local_id *out_id)
{
    void *data;
    quantapdf_status status;
    if (out_id == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_id = 0;
    if (g == NULL || (indirect != 0 && indirect != 1))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (g->sealed)
        return QUANTAPDF_ERROR_STATE;
    if (g->model.node_count == g->limits.max_nodes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status = grow(g, g->nodes, &g->node_capacity, g->model.node_count + 1,
        g->limits.max_nodes, sizeof(*g->nodes), &data);
    g->nodes = (quantapdf_graph_node *)data;
    g->model.nodes = g->nodes;
    if (status != QUANTAPDF_OK)
        return status;
    memset(&g->nodes[g->model.node_count], 0, sizeof(*g->nodes));
    g->nodes[g->model.node_count].indirect = indirect;
    status = quantapdf_local_id_from_index(g->model.node_count, out_id);
    if (status == QUANTAPDF_OK)
        ++g->model.node_count;
    return status;
}

quantapdf_status quantapdf_graph_begin_node(
    quantapdf_graph *g, quantapdf_local_id id, quantapdf_graph_kind kind, int64_t integer)
{
    quantapdf_graph_node *node;
    if (g == NULL || id == 0 || id > g->model.node_count ||
        kind < QUANTAPDF_GRAPH_NULL || kind > QUANTAPDF_GRAPH_STREAM)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (g->sealed || g->active != 0 || id != g->defined_count + 1)
        return QUANTAPDF_ERROR_STATE;
    node = &g->nodes[id - 1];
    node->kind = kind;
    node->integer = integer;
    node->data_offset = g->model.byte_count;
    node->edge_offset = g->model.edge_count;
    g->active = id;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_append_data(quantapdf_graph *g, const void *data, size_t size)
{
    quantapdf_graph_node *node;
    quantapdf_status status;
    int stream;
    if (g == NULL || (size != 0 && data == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (g->sealed || g->active == 0)
        return QUANTAPDF_ERROR_STATE;
    node = &g->nodes[g->active - 1];
    if (node->edge_count != 0 || (node->kind != QUANTAPDF_GRAPH_REAL &&
        node->kind != QUANTAPDF_GRAPH_NAME && node->kind != QUANTAPDF_GRAPH_STRING &&
        node->kind != QUANTAPDF_GRAPH_STREAM))
        return QUANTAPDF_ERROR_STATE;
    stream = node->kind == QUANTAPDF_GRAPH_STREAM;
    status = grow_bytes(g, size, stream);
    if (status != QUANTAPDF_OK)
        return status;
    if (size != 0)
        memcpy(g->bytes + g->model.byte_count, data, size);
    g->model.byte_count += size;
    node->data_size += size;
    if (stream) g->stream_bytes += size;
    else g->value_bytes += size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_append_edge(
    quantapdf_graph *g, quantapdf_local_id target, const void *key, size_t key_size)
{
    quantapdf_graph_node *node;
    quantapdf_graph_edge edge;
    quantapdf_status status;
    if (g == NULL || target == 0 || target > g->model.node_count || (key_size != 0 && key == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (g->sealed || g->active == 0)
        return QUANTAPDF_ERROR_STATE;
    node = &g->nodes[g->active - 1];
    if (node->kind != QUANTAPDF_GRAPH_ARRAY && node->kind != QUANTAPDF_GRAPH_DICTIONARY &&
        node->kind != QUANTAPDF_GRAPH_STREAM)
        return QUANTAPDF_ERROR_STATE;
    if ((node->kind == QUANTAPDF_GRAPH_ARRAY && key_size != 0) ||
        (node->kind != QUANTAPDF_GRAPH_ARRAY && (key_size == 0 || ((const char *)key)[0] != '/')))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (g->model.edge_count == g->limits.max_edges)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status = grow_edges(g, g->model.edge_count + 1);
    if (status == QUANTAPDF_OK)
        status = grow_bytes(g, key_size, 0);
    if (status != QUANTAPDF_OK)
        return status;
    edge.target = target;
    edge.key_offset = g->model.byte_count;
    edge.key_size = key_size;
    if (key_size != 0)
        memcpy(g->bytes + g->model.byte_count, key, key_size);
    g->model.byte_count += key_size;
    g->value_bytes += key_size;
    g->edges[g->model.edge_count++] = edge;
    ++node->edge_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_end_node(quantapdf_graph *g)
{
    if (g == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (g->sealed || g->active == 0)
        return QUANTAPDF_ERROR_STATE;
    g->active = 0;
    ++g->defined_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_finish(quantapdf_graph *g,
    quantapdf_local_id trailer, quantapdf_local_id catalog,
    const quantapdf_local_id *pages, size_t page_count, quantapdf_validation_report *report)
{
    quantapdf_status status;
    size_t i, original;
    if (report != NULL) memset(report, 0, sizeof(*report));
    if (g == NULL || report == NULL || (page_count != 0 && pages == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (g->sealed || g->active != 0 || g->defined_count != g->model.node_count)
        return QUANTAPDF_ERROR_STATE;
    if (page_count > g->limits.max_edges - g->model.edge_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    original = g->model.edge_count;
    status = grow_edges(g, original + page_count);
    if (status != QUANTAPDF_OK)
        return status;
    for (i = 0; i < page_count; ++i) {
        quantapdf_graph_edge edge = {pages[i], 0, 0};
        g->edges[original + i] = edge;
    }
    g->model.trailer = trailer; g->model.catalog = catalog;
    g->model.page_offset = original; g->model.page_count = page_count;
    g->model.edge_count += page_count;
    status = quantapdf_validate_graph(&g->model, report);
    if (status == QUANTAPDF_OK)
        g->sealed = 1;
    else {
        g->model.trailer = g->model.catalog = 0;
        g->model.edge_count = original;
        g->model.page_offset = g->model.page_count = 0;
    }
    return status;
}

const quantapdf_graph_model *quantapdf_graph_view(const quantapdf_graph *g)
{
    return g != NULL && g->sealed ? &g->model : NULL;
}

size_t quantapdf_graph_storage_bytes(const quantapdf_graph *g)
{
    return g == NULL ? 0 : g->storage;
}

const quantapdf_graph_node *quantapdf_graph_node_at(const quantapdf_graph_model *model, quantapdf_local_id id)
{
    return model != NULL && id != 0 && id <= model->node_count ? &model->nodes[id - 1] : NULL;
}

const quantapdf_graph_edge *quantapdf_graph_find(
    const quantapdf_graph_model *model, quantapdf_local_id id, const void *key, size_t size)
{
    const quantapdf_graph_node *node = quantapdf_graph_node_at(model, id);
    size_t first = 0, last;
    if (node == NULL || key == NULL || (node->kind != QUANTAPDF_GRAPH_DICTIONARY && node->kind != QUANTAPDF_GRAPH_STREAM))
        return NULL;
    last = node->edge_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        const quantapdf_graph_edge *edge = &model->edges[node->edge_offset + middle];
        int order = memcmp(model->bytes + edge->key_offset, key,
            edge->key_size < size ? edge->key_size : size);
        if (order == 0) {
            if (edge->key_size == size) return edge;
            order = edge->key_size < size ? -1 : 1;
        }
        if (order < 0) first = middle + 1;
        else last = middle;
    }
    return NULL;
}
