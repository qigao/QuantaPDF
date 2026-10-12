#include "graph_extgstate.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int name_equals(const quantapdf_graph_model *model,
    const quantapdf_graph_node *node, const char *name)
{
    size_t len = strlen(name);
    return node != NULL && node->kind == QUANTAPDF_GRAPH_NAME &&
        node->data_size == len &&
        memcmp(model->bytes + node->data_offset, name, len) == 0;
}

static int typed(const quantapdf_graph_model *model,
    quantapdf_local_id id, const char *type)
{
    const quantapdf_graph_edge *edge = quantapdf_graph_find(
        model, id, "/Type", 5);
    return edge != NULL && name_equals(model,
        quantapdf_graph_node_at(model, edge->target), type);
}

static quantapdf_status resource_dictionary(
    const quantapdf_graph_model *model, size_t index,
    size_t max_steps, quantapdf_local_id *out_id)
{
    quantapdf_local_id current;
    size_t limit;
    *out_id = 0;
    if (index >= model->page_count || max_steps == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    current = model->edges[model->page_offset + index].target;
    limit = max_steps < model->node_count ? max_steps : model->node_count;
    for (size_t i = 0; i < limit; ++i) {
        const quantapdf_graph_edge *edge;
        const quantapdf_graph_node *node = quantapdf_graph_node_at(model, current);
        if (node == NULL || node->kind != QUANTAPDF_GRAPH_DICTIONARY ||
            !typed(model, current, i == 0 ? "/Page" : "/Pages"))
            return QUANTAPDF_ERROR_FORMAT;
        edge = quantapdf_graph_find(model, current, "/Resources", 10);
        if (edge != NULL) {
            const quantapdf_graph_node *value =
                quantapdf_graph_node_at(model, edge->target);
            if (value == NULL) return QUANTAPDF_ERROR_BACKEND;
            if (value->kind != QUANTAPDF_GRAPH_NULL) {
                if (value->kind != QUANTAPDF_GRAPH_DICTIONARY)
                    return QUANTAPDF_ERROR_FORMAT;
                *out_id = edge->target;
                return QUANTAPDF_OK;
            }
        }
        edge = quantapdf_graph_find(model, current, "/Parent", 7);
        if (edge == NULL ||
            quantapdf_graph_node_at(model, edge->target)->kind ==
                QUANTAPDF_GRAPH_NULL)
            return QUANTAPDF_OK;
        current = edge->target;
    }
    return limit == model->node_count ?
        QUANTAPDF_ERROR_FORMAT : QUANTAPDF_ERROR_UNSUPPORTED;
}

static quantapdf_status real_number(const quantapdf_graph_model *model,
    quantapdf_local_id id, double *out_value)
{
    const quantapdf_graph_node *node = quantapdf_graph_node_at(model, id);
    size_t i = 0;
    double value = 0.0, fraction = 0.1;
    int negative = 0, dot = 0, digits = 0;
    if (node == NULL) return QUANTAPDF_ERROR_BACKEND;
    if (node->kind == QUANTAPDF_GRAPH_INTEGER) {
        if (node->integer > INT64_C(9007199254740991) ||
            node->integer < -INT64_C(9007199254740991))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        *out_value = (double)node->integer;
        return QUANTAPDF_OK;
    }
    if (node->kind != QUANTAPDF_GRAPH_REAL)
        return QUANTAPDF_ERROR_FORMAT;
    if (node->data_size == 0) return QUANTAPDF_ERROR_FORMAT;
    if (node->data_size > 64) return QUANTAPDF_ERROR_UNSUPPORTED;
    const unsigned char *s = model->bytes + node->data_offset;
    if (s[0] == '+' || s[0] == '-') {
        negative = s[0] == '-';
        ++i;
    }
    for (; i < node->data_size; ++i) {
        if (s[i] == '.' && !dot) { dot = 1; continue; }
        if (s[i] < '0' || s[i] > '9') return QUANTAPDF_ERROR_FORMAT;
        digits = 1;
        if (dot) { value += (double)(s[i] - '0') * fraction; fraction *= 0.1; }
        else value = value * 10.0 + (double)(s[i] - '0');
        if (!isfinite(value)) return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (!digits) return QUANTAPDF_ERROR_FORMAT;
    *out_value = negative ? -value : value;
    return QUANTAPDF_OK;
}

static quantapdf_status blend_mode(const quantapdf_graph_model *model,
    quantapdf_local_id id, quantapdf_composer_blend_mode *out)
{
    static const struct {
        const char *name;
        quantapdf_composer_blend_mode value;
    } supported[] = {
        {"/Normal", QUANTAPDF_COMPOSER_BLEND_NORMAL},
        {"/Multiply", QUANTAPDF_COMPOSER_BLEND_MULTIPLY},
        {"/Screen", QUANTAPDF_COMPOSER_BLEND_SCREEN},
        {"/Overlay", QUANTAPDF_COMPOSER_BLEND_OVERLAY},
        {"/Darken", QUANTAPDF_COMPOSER_BLEND_DARKEN},
        {"/Lighten", QUANTAPDF_COMPOSER_BLEND_LIGHTEN}
    };
    const quantapdf_graph_node *node = quantapdf_graph_node_at(model, id);
    if (node == NULL) return QUANTAPDF_ERROR_BACKEND;
    if (node->kind != QUANTAPDF_GRAPH_NAME)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
        if (name_equals(model, node, supported[i].name)) {
            *out = supported[i].value;
            return QUANTAPDF_OK;
        }
    }
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

static quantapdf_status resolve_delta(
    const quantapdf_graph_model *model, quantapdf_local_id id,
    quantapdf_pdf_extgstate_delta *out)
{
    const quantapdf_graph_node *dict = quantapdf_graph_node_at(model, id);
    if (dict == NULL || dict->kind != QUANTAPDF_GRAPH_DICTIONARY)
        return QUANTAPDF_ERROR_FORMAT;
    for (size_t i = 0; i < dict->edge_count; ++i) {
        const quantapdf_graph_edge *edge =
            &model->edges[dict->edge_offset + i];
        const unsigned char *key = model->bytes + edge->key_offset;
        size_t length = edge->key_size;
        quantapdf_status status;
        if (length == 5 && memcmp(key, "/Type", 5) == 0) {
            if (!name_equals(model, quantapdf_graph_node_at(model,
                edge->target), "/ExtGState"))
                return QUANTAPDF_ERROR_FORMAT;
        } else if (length == 3 && memcmp(key, "/ca", 3) == 0) {
            status = real_number(model, edge->target, &out->fill_alpha);
            if (status != QUANTAPDF_OK) return status;
            if (out->fill_alpha < 0.0 || out->fill_alpha > 1.0)
                return QUANTAPDF_ERROR_FORMAT;
            out->presence |= QUANTAPDF_PDF_GS_FILL_ALPHA;
        } else if (length == 3 && memcmp(key, "/CA", 3) == 0) {
            status = real_number(model, edge->target, &out->stroke_alpha);
            if (status != QUANTAPDF_OK) return status;
            if (out->stroke_alpha < 0.0 || out->stroke_alpha > 1.0)
                return QUANTAPDF_ERROR_FORMAT;
            out->presence |= QUANTAPDF_PDF_GS_STROKE_ALPHA;
        } else if (length == 3 && memcmp(key, "/BM", 3) == 0) {
            status = blend_mode(model, edge->target, &out->blend_mode);
            if (status != QUANTAPDF_OK) return status;
            out->presence |= QUANTAPDF_PDF_GS_BLEND_MODE;
        } else {
            /* /SMask, /OP, /Font, /AIS, /TR, line overrides, etc. may change
             * pixels. Never discard fields or synthesize a partial state. */
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    out->resolved = 1;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_graph_resolve_page_extgstates(
    const quantapdf_graph *graph, size_t page_index, size_t max_parent_steps,
    const quantapdf_pdf_content_ir *content,
    quantapdf_pdf_extgstate_delta **out_changes, size_t *out_count)
{
    const quantapdf_graph_model *model;
    const quantapdf_graph_node *dictionary;
    const quantapdf_graph_edge *edge;
    quantapdf_pdf_extgstate_delta *changes = NULL;
    quantapdf_local_id resources = 0, extgstate;
    size_t gs_count = 0;
    quantapdf_status status;
    if (out_changes != NULL) *out_changes = NULL;
    if (out_count != NULL) *out_count = 0;
    if (graph == NULL || content == NULL || out_changes == NULL ||
        out_count == NULL || max_parent_steps == 0 ||
        (content->count != 0 && content->ops == NULL) ||
        (content->name_byte_count != 0 && content->name_bytes == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;
    model = quantapdf_graph_view(graph);
    if (model == NULL) return QUANTAPDF_ERROR_STATE;
    if (page_index >= model->page_count) return QUANTAPDF_ERROR_ARGUMENT;
    for (size_t i = 0; i < content->count; ++i)
        if (content->ops[i].opcode == QUANTAPDF_PDF_OP_EXT_GSTATE)
            ++gs_count;
    if (gs_count == 0) return QUANTAPDF_OK;
    status = resource_dictionary(model, page_index, max_parent_steps,
        &resources);
    if (status != QUANTAPDF_OK) return status;
    if (resources == 0) return QUANTAPDF_ERROR_FORMAT;
    edge = quantapdf_graph_find(model, resources, "/ExtGState", 10);
    if (edge == NULL) return QUANTAPDF_ERROR_FORMAT;
    extgstate = edge->target;
    dictionary = quantapdf_graph_node_at(model, extgstate);
    if (dictionary == NULL || dictionary->kind != QUANTAPDF_GRAPH_DICTIONARY)
        return QUANTAPDF_ERROR_FORMAT;
    if (content->count > SIZE_MAX / sizeof(*changes))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    changes = (quantapdf_pdf_extgstate_delta *)calloc(
        content->count, sizeof(*changes));
    if (changes == NULL) return QUANTAPDF_ERROR_NOMEM;
    for (size_t i = 0; i < content->count; ++i) {
        const quantapdf_pdf_content_op *op = &content->ops[i];
        if (op->opcode != QUANTAPDF_PDF_OP_EXT_GSTATE) continue;
        if (op->name_size <= 1 ||
            op->name_offset > content->name_byte_count ||
            op->name_size > content->name_byte_count - op->name_offset) {
            status = QUANTAPDF_ERROR_FORMAT; goto fail;
        }
        edge = quantapdf_graph_find(model, extgstate,
            content->name_bytes + op->name_offset, op->name_size);
        if (edge == NULL) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
        status = resolve_delta(model, edge->target, &changes[i]);
        if (status != QUANTAPDF_OK) goto fail;
    }
    *out_changes = changes;
    *out_count = content->count;
    return QUANTAPDF_OK;
fail:
    free(changes);
    return status;
}
