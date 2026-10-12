#include "graph_edit.h"

#include <stdlib.h>
#include <string.h>

typedef struct graph_lineage {
    size_t references;
    uint64_t token;
    quantapdf_identity_sequence versions;
} graph_lineage;

typedef struct graph_origin_range {
    uint64_t origin;
    quantapdf_local_id last;
} graph_origin_range;

struct quantapdf_graph {
    quantapdf_graph_model model;
    /* Mutable owners; model pointers are derived const aliases for readers. */
    quantapdf_graph_node *nodes;
    quantapdf_graph_edge *edges;
    unsigned char *bytes;
    quantapdf_graph_limits limits;
    size_t node_capacity, edge_capacity, byte_capacity, storage;
    size_t value_bytes, stream_bytes, defined_count;
    graph_lineage *lineage;
    uint64_t version;
    graph_origin_range *origins;
    size_t origin_count, inherited_count;
    quantapdf_local_id active;
    int sealed;
};

static int valid_limits(const quantapdf_graph_limits *limits)
{
    return limits != NULL && limits->max_nodes != 0 && limits->max_nodes <= UINT32_MAX &&
        limits->max_nodes <= SIZE_MAX / sizeof(quantapdf_graph_node) &&
        limits->max_edges <= SIZE_MAX / sizeof(quantapdf_graph_edge) &&
        limits->max_storage_bytes >= sizeof(quantapdf_graph) + sizeof(graph_lineage) &&
        limits->max_depth != 0;
}

quantapdf_status quantapdf_graph_set_source(quantapdf_graph *g,
    const quantapdf_graph_source *source)
{
    if (g == NULL || source == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    if (g->sealed) return QUANTAPDF_ERROR_STATE;
    if (source->pdf_version[0] < '1' || source->pdf_version[0] > '9' ||
        source->pdf_version[1] != '.' || source->pdf_version[2] < '0' ||
        source->pdf_version[2] > '9' || source->pdf_version[3] != 0 ||
        (source->encrypted != 0 && source->encrypted != 1))
        return QUANTAPDF_ERROR_ARGUMENT;
    g->model.source = *source;
    return QUANTAPDF_OK;
}

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
    quantapdf_status status;
    if (out_graph == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_graph = NULL;
    if (!valid_limits(limits))
        return QUANTAPDF_ERROR_ARGUMENT;
    g = (quantapdf_graph *)calloc(1, sizeof(*g));
    if (g == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    g->lineage = (graph_lineage *)calloc(1, sizeof(*g->lineage));
    if (g->lineage == NULL) {
        free(g);
        return QUANTAPDF_ERROR_NOMEM;
    }
    status = quantapdf_reference_scope_create(&g->lineage->token);
    if (status != QUANTAPDF_OK) {
        free(g->lineage);
        free(g);
        return status;
    }
    g->lineage->references = 1;
    g->lineage->versions.next = 2;
    g->version = 1;
    g->limits = *limits;
    g->storage = sizeof(*g) + sizeof(*g->lineage);
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
    free(g->origins);
    if (--g->lineage->references == 0)
        free(g->lineage);
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

static quantapdf_status finish_graph(quantapdf_graph *g,
    quantapdf_local_id trailer, quantapdf_local_id catalog,
    const quantapdf_local_id *pages, const quantapdf_graph_edge *page_edges,
    size_t page_count, quantapdf_validation_report *report)
{
    quantapdf_status status;
    size_t i, original;
    if (report != NULL) memset(report, 0, sizeof(*report));
    if (g == NULL || report == NULL || (page_count != 0 && pages == NULL && page_edges == NULL))
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
        quantapdf_graph_edge edge = {pages != NULL ? pages[i] : page_edges[i].target, 0, 0};
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

quantapdf_status quantapdf_graph_finish(quantapdf_graph *g,
    quantapdf_local_id trailer, quantapdf_local_id catalog,
    const quantapdf_local_id *pages, size_t page_count, quantapdf_validation_report *report)
{
    return finish_graph(g, trailer, catalog, pages, NULL, page_count, report);
}

const quantapdf_graph_model *quantapdf_graph_view(const quantapdf_graph *g)
{
    return g != NULL && g->sealed ? &g->model : NULL;
}

size_t quantapdf_graph_storage_bytes(const quantapdf_graph *g)
{
    return g == NULL ? 0 : g->storage;
}

uint64_t quantapdf_graph_lineage(const quantapdf_graph *g)
{
    return g != NULL && g->sealed ? g->lineage->token : 0;
}

uint64_t quantapdf_graph_version(const quantapdf_graph *g)
{
    return g != NULL && g->sealed ? g->version : 0;
}

static uint64_t node_origin(const quantapdf_graph *g, quantapdf_local_id id)
{
    size_t first = 0, last = g->origin_count;
    if (id > g->inherited_count)
        return g->version;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (id > g->origins[middle].last) first = middle + 1;
        else last = middle;
    }
    return g->origins[first].origin;
}

quantapdf_status quantapdf_graph_reference_at(const quantapdf_graph *g,
    quantapdf_local_id id, quantapdf_graph_reference *out_reference)
{
    if (out_reference == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_reference, 0, sizeof(*out_reference));
    if (g == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    if (!g->sealed) return QUANTAPDF_ERROR_STATE;
    if (id == 0 || id > g->model.node_count) return QUANTAPDF_ERROR_ARGUMENT;
    out_reference->lineage = g->lineage->token;
    out_reference->origin = node_origin(g, id);
    out_reference->id = id;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_resolve(const quantapdf_graph *g,
    const quantapdf_graph_reference *reference, quantapdf_local_id *out_id)
{
    if (out_id == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    *out_id = 0;
    if (g == NULL || reference == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    if (!g->sealed) return QUANTAPDF_ERROR_STATE;
    if (reference->lineage != g->lineage->token || reference->id == 0 ||
        reference->id > g->model.node_count || reference->origin != node_origin(g, reference->id))
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_id = reference->id;
    return QUANTAPDF_OK;
}

static quantapdf_status create_candidate(const quantapdf_graph *base,
    const quantapdf_graph_limits *limits, quantapdf_graph **out_graph)
{
    quantapdf_graph *g;
    quantapdf_status status;
    size_t count, bytes;
    *out_graph = NULL;
    if (base->lineage->references == SIZE_MAX) return QUANTAPDF_ERROR_NOMEM;
    g = (quantapdf_graph *)calloc(1, sizeof(*g));
    if (g == NULL) return QUANTAPDF_ERROR_NOMEM;
    status = quantapdf_identity_next(&base->lineage->versions, &g->version);
    if (status != QUANTAPDF_OK) { free(g); return status; }
    g->lineage = base->lineage;
    ++g->lineage->references;
    g->limits = *limits;
    g->storage = sizeof(*g) + sizeof(*g->lineage);
    count = base->origin_count + (base->model.node_count > base->inherited_count ? 1u : 0u);
    g->model.source = base->model.source;
    if (count > SIZE_MAX / sizeof(*g->origins)) {
        quantapdf_graph_drop(g);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    bytes = count * sizeof(*g->origins);
    if (bytes > limits->max_storage_bytes - g->storage) {
        quantapdf_graph_drop(g);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (count != 0) {
        g->origins = (graph_origin_range *)malloc(bytes);
        if (g->origins == NULL) { quantapdf_graph_drop(g); return QUANTAPDF_ERROR_NOMEM; }
        if (base->origin_count != 0)
            memcpy(g->origins, base->origins, base->origin_count * sizeof(*g->origins));
        if (count > base->origin_count) {
            g->origins[count - 1].origin = base->version;
            g->origins[count - 1].last = (quantapdf_local_id)base->model.node_count;
        }
    }
    g->storage += bytes;
    g->origin_count = count;
    g->inherited_count = base->model.node_count;
    *out_graph = g;
    return QUANTAPDF_OK;
}

static int valid_input(const quantapdf_graph_node_input *input)
{
    int bytes = input->kind == QUANTAPDF_GRAPH_REAL || input->kind == QUANTAPDF_GRAPH_NAME ||
        input->kind == QUANTAPDF_GRAPH_STRING || input->kind == QUANTAPDF_GRAPH_STREAM;
    int container = input->kind == QUANTAPDF_GRAPH_ARRAY || input->kind == QUANTAPDF_GRAPH_DICTIONARY ||
        input->kind == QUANTAPDF_GRAPH_STREAM;
    return input->kind >= QUANTAPDF_GRAPH_NULL && input->kind <= QUANTAPDF_GRAPH_STREAM &&
        (input->indirect == 0 || input->indirect == 1) &&
        (bytes || input->data_size == 0) && (container || input->edge_count == 0) &&
        (input->data_size == 0 || input->data != NULL) &&
        (input->edge_count == 0 || input->edges != NULL) &&
        input->edge_count <= SIZE_MAX / sizeof(*input->edges);
}

static quantapdf_status copy_input(quantapdf_graph *g, quantapdf_local_id id,
    const quantapdf_graph_node_input *input)
{
    size_t i;
    quantapdf_status status = quantapdf_graph_begin_node(g, id, input->kind, input->integer);
    if (status == QUANTAPDF_OK && input->data_size != 0)
        status = quantapdf_graph_append_data(g, input->data, input->data_size);
    for (i = 0; status == QUANTAPDF_OK && i < input->edge_count; ++i)
        status = quantapdf_graph_append_edge(g, input->edges[i].target,
            input->edges[i].key, input->edges[i].key_size);
    return status == QUANTAPDF_OK ? quantapdf_graph_end_node(g) : status;
}

static quantapdf_status copy_node(quantapdf_graph *g, const quantapdf_graph_model *base,
    quantapdf_local_id id)
{
    const quantapdf_graph_node *node = &base->nodes[id - 1];
    size_t i;
    quantapdf_status status = quantapdf_graph_begin_node(g, id, node->kind, node->integer);
    if (status == QUANTAPDF_OK && node->data_size != 0)
        status = quantapdf_graph_append_data(g, base->bytes + node->data_offset, node->data_size);
    for (i = 0; status == QUANTAPDF_OK && i < node->edge_count; ++i) {
        const quantapdf_graph_edge *edge = &base->edges[node->edge_offset + i];
        status = quantapdf_graph_append_edge(g, edge->target,
            edge->key_size != 0 ? base->bytes + edge->key_offset : NULL, edge->key_size);
    }
    return status == QUANTAPDF_OK ? quantapdf_graph_end_node(g) : status;
}

/* Validation owns the page-tree rules; the candidate owns bounded scratch.
 * Generic graph cycles remain legal. Only the page-tree view is constrained. */
static quantapdf_status validate_page_index(const quantapdf_graph *g,
    quantapdf_validation_report *report)
{
    const quantapdf_graph_model *model = &g->model;
    size_t capacity = model->node_count < g->limits.max_depth ? model->node_count : g->limits.max_depth;
    unsigned char *seen;
    quantapdf_validation_page_frame *stack;
    quantapdf_status status;
    if (capacity > SIZE_MAX / sizeof(*stack)) return QUANTAPDF_ERROR_UNSUPPORTED;
    seen = (unsigned char *)malloc(model->node_count);
    if (seen == NULL) return QUANTAPDF_ERROR_NOMEM;
    stack = (quantapdf_validation_page_frame *)malloc(capacity * sizeof(*stack));
    if (stack == NULL) { free(seen); return QUANTAPDF_ERROR_NOMEM; }
    status = quantapdf_validate_graph_pages(model, seen, model->node_count, stack, capacity, report);
    free(stack); free(seen);
    return status;
}

quantapdf_status quantapdf_graph_commit(const quantapdf_graph *base,
    const quantapdf_graph_change_set *changes, const quantapdf_graph_limits *limits,
    quantapdf_graph **out_graph, quantapdf_validation_report *report)
{
    quantapdf_graph *g = NULL;
    const quantapdf_graph_model *model;
    size_t i, replacement, count;
    quantapdf_status status;
    if (report != NULL) memset(report, 0, sizeof(*report));
    if (out_graph == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    *out_graph = NULL;
    if (base == NULL || changes == NULL || report == NULL || !valid_limits(limits))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (!base->sealed || changes->lineage != base->lineage->token ||
        changes->base_version != base->version)
        return QUANTAPDF_ERROR_STATE;
    model = &base->model;
    if ((changes->replacement_count != 0 && changes->replacements == NULL) ||
        changes->replacement_count > model->node_count ||
        changes->replacement_count > SIZE_MAX / sizeof(*changes->replacements) ||
        (changes->addition_count != 0 && changes->additions == NULL) ||
        changes->addition_count > SIZE_MAX / sizeof(*changes->additions) ||
        (changes->replace_roots != 0 && changes->replace_roots != 1) ||
        (changes->page_count != 0 && changes->pages == NULL) ||
        changes->page_count > SIZE_MAX / sizeof(*changes->pages) ||
        (!changes->replace_roots && (changes->trailer != 0 || changes->catalog != 0 ||
            changes->pages != NULL || changes->page_count != 0)))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (model->node_count > limits->max_nodes ||
        changes->addition_count > limits->max_nodes - model->node_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    count = model->node_count + changes->addition_count;
    for (i = 0; i < changes->replacement_count; ++i) {
        if (changes->replacements[i].id == 0 || changes->replacements[i].id > model->node_count ||
            (i != 0 && changes->replacements[i - 1].id >= changes->replacements[i].id) ||
            !valid_input(&changes->replacements[i].value))
            return QUANTAPDF_ERROR_ARGUMENT;
    }
    for (i = 0; i < changes->addition_count; ++i)
        if (!valid_input(&changes->additions[i])) return QUANTAPDF_ERROR_ARGUMENT;
    status = create_candidate(base, limits, &g);
    for (i = 0, replacement = 0; status == QUANTAPDF_OK && i < count; ++i) {
        quantapdf_local_id id;
        const quantapdf_graph_node_input *input = NULL;
        if (i >= model->node_count) input = &changes->additions[i - model->node_count];
        else if (replacement < changes->replacement_count && changes->replacements[replacement].id == i + 1)
            input = &changes->replacements[replacement++].value;
        status = quantapdf_graph_reserve_node(g,
            input != NULL ? input->indirect : model->nodes[i].indirect, &id);
    }
    for (i = 0, replacement = 0; status == QUANTAPDF_OK && i < count; ++i) {
        quantapdf_local_id id = (quantapdf_local_id)(i + 1);
        if (i >= model->node_count) status = copy_input(g, id, &changes->additions[i - model->node_count]);
        else if (replacement < changes->replacement_count && changes->replacements[replacement].id == id)
            status = copy_input(g, id, &changes->replacements[replacement++].value);
        else status = copy_node(g, model, id);
    }
    if (status == QUANTAPDF_OK) {
        if (changes->replace_roots)
            status = quantapdf_graph_finish(g, changes->trailer, changes->catalog,
                changes->pages, changes->page_count, report);
        else
            status = finish_graph(g, model->trailer, model->catalog, NULL,
                model->page_count != 0 ? model->edges + model->page_offset : NULL,
                model->page_count, report);
    }
    if (status == QUANTAPDF_OK) status = validate_page_index(g, report);
    if (status != QUANTAPDF_OK) { quantapdf_graph_drop(g); return status; }
    *out_graph = g;
    return QUANTAPDF_OK;
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
