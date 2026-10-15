#include "graph_page_geometry.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int is_type(const quantapdf_graph_model *model,
    quantapdf_local_id id, const char *name)
{
    const quantapdf_graph_edge *entry = quantapdf_graph_find(model, id, "/Type", 5);
    const quantapdf_graph_node *node = entry == NULL ? NULL :
        quantapdf_graph_node_at(model, entry->target);
    size_t length = strlen(name);
    return node != NULL && node->kind == QUANTAPDF_GRAPH_NAME &&
        node->data_size == length &&
        memcmp(model->bytes + node->data_offset, name, length) == 0;
}

/* The already validated graph stores PDF reals in their original decimal
 * spelling. Decode without strtod's process-locale dependence or exponent
 * acceptance. Resource consumption is explicitly bounded. */
static quantapdf_status decimal_number(const unsigned char *data,
    size_t length, double *out)
{
    size_t i = 0;
    double value = 0.0, fraction = 0.1;
    int negative = 0, dot = 0, digit = 0;
    if (length > 64) return QUANTAPDF_ERROR_UNSUPPORTED;
    if (length == 0) return QUANTAPDF_ERROR_FORMAT;
    if (data[0] == '+' || data[0] == '-') {
        negative = data[0] == '-';
        ++i;
    }
    for (; i < length; ++i) {
        unsigned char c = data[i];
        if (c == '.' && !dot) { dot = 1; continue; }
        if (c < '0' || c > '9') return QUANTAPDF_ERROR_FORMAT;
        digit = 1;
        if (dot) {
            value += (double)(c - '0') * fraction;
            fraction *= 0.1;
        } else value = value * 10.0 + (double)(c - '0');
        if (!isfinite(value)) return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (!digit) return QUANTAPDF_ERROR_FORMAT;
    *out = negative ? -value : value;
    return QUANTAPDF_OK;
}

static quantapdf_status numeric_value(const quantapdf_graph_model *model,
    quantapdf_local_id id, double *out)
{
    const quantapdf_graph_node *node = quantapdf_graph_node_at(model, id);
    if (node == NULL) return QUANTAPDF_ERROR_BACKEND;
    if (node->kind == QUANTAPDF_GRAPH_INTEGER) {
        /* Do not pretend a double holds every 64-bit PDF integer exactly. */
        if (node->integer > INT64_C(9007199254740991) ||
            node->integer < -INT64_C(9007199254740991))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        *out = (double)node->integer;
        return QUANTAPDF_OK;
    }
    if (node->kind != QUANTAPDF_GRAPH_REAL) return QUANTAPDF_ERROR_FORMAT;
    return decimal_number(model->bytes + node->data_offset, node->data_size, out);
}

static quantapdf_status box_value(const quantapdf_graph_model *model,
    quantapdf_local_id id, double out[4])
{
    const quantapdf_graph_node *node = quantapdf_graph_node_at(model, id);
    quantapdf_status status;
    if (node == NULL) return QUANTAPDF_ERROR_BACKEND;
    if (node->kind != QUANTAPDF_GRAPH_ARRAY || node->edge_count != 4)
        return QUANTAPDF_ERROR_FORMAT;
    for (size_t i = 0; i < 4; ++i) {
        status = numeric_value(model,
            model->edges[node->edge_offset + i].target, &out[i]);
        if (status != QUANTAPDF_OK) return status;
    }
    if (!(out[2] > out[0] && out[3] > out[1]) ||
        !isfinite(out[2] - out[0]) || !isfinite(out[3] - out[1]))
        return QUANTAPDF_ERROR_FORMAT;
    return QUANTAPDF_OK;
}

static quantapdf_status rotation_value(const quantapdf_graph_model *model,
    quantapdf_local_id id, int *out)
{
    const quantapdf_graph_node *node = quantapdf_graph_node_at(model, id);
    int rotation;
    if (node == NULL) return QUANTAPDF_ERROR_BACKEND;
    if (node->kind != QUANTAPDF_GRAPH_INTEGER ||
        node->integer % INT64_C(90) != 0)
        return QUANTAPDF_ERROR_FORMAT;
    rotation = (int)(node->integer % INT64_C(360));
    if (rotation < 0) rotation += 360;
    *out = rotation;
    return QUANTAPDF_OK;
}

static quantapdf_status parent_contains(const quantapdf_graph_model *model,
    quantapdf_local_id parent, quantapdf_local_id child)
{
    const quantapdf_graph_edge *entry = quantapdf_graph_find(model, parent, "/Kids", 5);
    const quantapdf_graph_node *array;
    size_t matches = 0;
    if (entry == NULL) return QUANTAPDF_ERROR_FORMAT;
    array = quantapdf_graph_node_at(model, entry->target);
    if (array == NULL || array->kind != QUANTAPDF_GRAPH_ARRAY)
        return QUANTAPDF_ERROR_FORMAT;
    for (size_t i = 0; i < array->edge_count; ++i)
        if (model->edges[array->edge_offset + i].target == child) ++matches;
    return matches == 1 ? QUANTAPDF_OK : QUANTAPDF_ERROR_FORMAT;
}

quantapdf_status quantapdf_graph_query_page_geometry(const quantapdf_graph *graph,
    size_t page_index, size_t max_parent_steps,
    quantapdf_graph_page_geometry *out_geometry)
{
    const quantapdf_graph_model *model;
    const quantapdf_graph_edge *root_edge, *entry;
    quantapdf_graph_page_geometry result = {0};
    quantapdf_local_id root, current;
    size_t limit;
    int have_media = 0, have_crop = 0, have_rotation = 0;
    quantapdf_status status;
    if (out_geometry != NULL) memset(out_geometry, 0, sizeof(*out_geometry));
    if (out_geometry == NULL || graph == NULL || max_parent_steps == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    model = quantapdf_graph_view(graph);
    if (model == NULL) return QUANTAPDF_ERROR_STATE;
    if (page_index >= model->page_count) return QUANTAPDF_ERROR_ARGUMENT;
    root_edge = quantapdf_graph_find(model, model->catalog, "/Pages", 6);
    if (root_edge == NULL) return QUANTAPDF_ERROR_FORMAT;
    root = root_edge->target;
    if (!is_type(model, root, "/Pages") ||
        !model->nodes[root - 1].indirect) return QUANTAPDF_ERROR_FORMAT;
    current = model->edges[model->page_offset + page_index].target;
    result.user_unit = 1.0;
    limit = max_parent_steps < model->node_count ?
        max_parent_steps : model->node_count;
    for (size_t step = 0; step < limit; ++step) {
        const quantapdf_graph_node *node = quantapdf_graph_node_at(model, current);
        if (node == NULL || node->kind != QUANTAPDF_GRAPH_DICTIONARY ||
            !node->indirect ||
            !is_type(model, current, step == 0 ? "/Page" : "/Pages"))
            return QUANTAPDF_ERROR_FORMAT;

        if (step == 0) {
            entry = quantapdf_graph_find(model, current, "/UserUnit", 9);
            if (entry != NULL) {
                status = numeric_value(model, entry->target, &result.user_unit);
                if (status != QUANTAPDF_OK) return status;
                if (!(result.user_unit > 0.0) || !isfinite(result.user_unit))
                    return QUANTAPDF_ERROR_FORMAT;
            }
        }
        if (!have_media) {
            entry = quantapdf_graph_find(model, current, "/MediaBox", 9);
            if (entry != NULL) {
                status = box_value(model, entry->target, result.media_box);
                if (status != QUANTAPDF_OK) return status;
                have_media = 1;
            }
        }
        if (!have_crop) {
            entry = quantapdf_graph_find(model, current, "/CropBox", 8);
            if (entry != NULL) {
                status = box_value(model, entry->target, result.crop_box);
                if (status != QUANTAPDF_OK) return status;
                have_crop = 1;
            }
        }
        if (!have_rotation) {
            entry = quantapdf_graph_find(model, current, "/Rotate", 7);
            if (entry != NULL) {
                status = rotation_value(model, entry->target, &result.rotation);
                if (status != QUANTAPDF_OK) return status;
                have_rotation = 1;
            }
        }
        if (current == root) {
            /* A root page-tree node must not have a parent. */
            if (quantapdf_graph_find(model, current, "/Parent", 7) != NULL ||
                !have_media) return QUANTAPDF_ERROR_FORMAT;
            if (!have_crop)
                memcpy(result.crop_box, result.media_box, sizeof(result.media_box));
            *out_geometry = result;
            return QUANTAPDF_OK;
        }
        entry = quantapdf_graph_find(model, current, "/Parent", 7);
        if (entry == NULL) return QUANTAPDF_ERROR_FORMAT;
        {
            quantapdf_local_id parent = entry->target;
            const quantapdf_graph_node *ancestor =
                quantapdf_graph_node_at(model, parent);
            if (ancestor == NULL || ancestor->kind != QUANTAPDF_GRAPH_DICTIONARY ||
                !ancestor->indirect || !is_type(model, parent, "/Pages"))
                return QUANTAPDF_ERROR_FORMAT;
            status = parent_contains(model, parent, current);
            if (status != QUANTAPDF_OK) return status;
            current = parent;
        }
    }
    return limit == model->node_count ?
        QUANTAPDF_ERROR_FORMAT : QUANTAPDF_ERROR_UNSUPPORTED;
}

quantapdf_status quantapdf_graph_canonical_page_dimensions(
    const quantapdf_graph_page_geometry *geometry,
    double *out_width, double *out_height)
{
    if (out_width != NULL) *out_width = 0.0;
    if (out_height != NULL) *out_height = 0.0;
    if (geometry == NULL || out_width == NULL || out_height == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (geometry->rotation != 0 || geometry->user_unit != 1.0 ||
        geometry->media_box[0] != 0.0 || geometry->media_box[1] != 0.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    for (size_t i = 0; i < 4; ++i) {
        if (!isfinite(geometry->media_box[i]) ||
            !isfinite(geometry->crop_box[i]) ||
            geometry->media_box[i] != geometry->crop_box[i])
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (geometry->media_box[2] <= 0.0 || geometry->media_box[3] <= 0.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    *out_width = geometry->media_box[2];
    *out_height = geometry->media_box[3];
    return QUANTAPDF_OK;
}



/* Native Path/Clip supports no transparency compositing model. A narrowly
 * isolated, non-knockout DeviceRGB page Group is pixel-equivalent while the
 * downstream native renderer admits only opaque Normal paints. Never drop
 * unknown group keys or silently accept non-isolated/knockout groups. */
static int group_name_is(const quantapdf_graph_model *model,
    quantapdf_local_id id, const char *value)
{
    const quantapdf_graph_node *node = quantapdf_graph_node_at(model, id);
    size_t size = strlen(value);
    return node != NULL && node->kind == QUANTAPDF_GRAPH_NAME &&
        node->data_size == size &&
        memcmp(model->bytes + node->data_offset, value, size) == 0;
}

quantapdf_status quantapdf_graph_admit_native_page_group(
    const quantapdf_graph *graph, size_t page_index)
{
    const quantapdf_graph_model *model;
    const quantapdf_graph_edge *group_edge;
    const quantapdf_graph_node *page, *group;
    quantapdf_local_id leaf;
    int have_subtype = 0, have_isolated = 0, have_space = 0;
    if (graph == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    model = quantapdf_graph_view(graph);
    if (model == NULL) return QUANTAPDF_ERROR_STATE;
    if (page_index >= model->page_count) return QUANTAPDF_ERROR_ARGUMENT;
    leaf = model->edges[model->page_offset + page_index].target;
    page = quantapdf_graph_node_at(model, leaf);
    if (page == NULL || page->kind != QUANTAPDF_GRAPH_DICTIONARY ||
        !is_type(model, leaf, "/Page"))
        return QUANTAPDF_ERROR_FORMAT;
    /* /Group is a PAGE property, not inherited from /Pages ancestors. */
    group_edge = quantapdf_graph_find(model, leaf, "/Group", 6);
    if (group_edge == NULL) return QUANTAPDF_OK;
    group = quantapdf_graph_node_at(model, group_edge->target);
    if (group == NULL) return QUANTAPDF_ERROR_BACKEND;
    /* A null dictionary value is semantically an omitted /Group key. */
    if (group->kind == QUANTAPDF_GRAPH_NULL) return QUANTAPDF_OK;
    if (group->kind != QUANTAPDF_GRAPH_DICTIONARY)
        return QUANTAPDF_ERROR_FORMAT;
    /* A group with no explicitly known isolation/space is not admitted,
     * even when its rendered contents happen to look opaque. */
    if (group->edge_count > 5u) return QUANTAPDF_ERROR_UNSUPPORTED;
    for (size_t i = 0u; i < group->edge_count; ++i) {
        const quantapdf_graph_edge *entry =
            &model->edges[group->edge_offset + i];
        const unsigned char *key = model->bytes + entry->key_offset;
        const quantapdf_graph_node *value =
            quantapdf_graph_node_at(model, entry->target);
        if (value == NULL) return QUANTAPDF_ERROR_BACKEND;
        if (entry->key_size == 5u && memcmp(key, "/Type", 5u) == 0) {
            if (!group_name_is(model, entry->target, "/Group"))
                return QUANTAPDF_ERROR_FORMAT;
        } else if (entry->key_size == 2u && memcmp(key, "/S", 2u) == 0) {
            if (value->kind != QUANTAPDF_GRAPH_NAME)
                return QUANTAPDF_ERROR_FORMAT;
            if (!group_name_is(model, entry->target, "/Transparency"))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            have_subtype = 1;
        } else if (entry->key_size == 2u && memcmp(key, "/I", 2u) == 0) {
            if (value->kind != QUANTAPDF_GRAPH_BOOL)
                return QUANTAPDF_ERROR_FORMAT;
            if (value->integer != 1)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            have_isolated = 1;
        } else if (entry->key_size == 2u && memcmp(key, "/K", 2u) == 0) {
            if (value->kind != QUANTAPDF_GRAPH_BOOL)
                return QUANTAPDF_ERROR_FORMAT;
            if (value->integer != 0)
                return QUANTAPDF_ERROR_UNSUPPORTED;
        } else if (entry->key_size == 3u && memcmp(key, "/CS", 3u) == 0) {
            if (value->kind != QUANTAPDF_GRAPH_NAME)
                return QUANTAPDF_ERROR_FORMAT;
            if (!group_name_is(model, entry->target, "/DeviceRGB"))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            have_space = 1;
        } else {
            /* Nonstandard backdrop, color space, transfer or compositing
             * keys require actual group support, not an implicit ignore. */
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    return have_subtype && have_isolated && have_space ?
        QUANTAPDF_OK : QUANTAPDF_ERROR_UNSUPPORTED;
}
