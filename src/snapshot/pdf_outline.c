#include "document/document_internal.h"
#include "snapshot/outline_snapshot.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static void quantapdf_dispose_outline(quantapdf_snapshot *snapshot)
{
    quantapdf_outline *outline = (quantapdf_outline *)snapshot;
    if (outline == NULL)
        return;
    free(outline->nodes);
    free(outline->strings);
    free(outline);
}

quantapdf_status quantapdf_document_outline(
    quantapdf_document *document,
    quantapdf_outline **out_outline)
{
    quantapdf_status status;
    if (out_outline == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_outline = NULL;
    if (document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_context_retain_internal(document->context);
    if (status != QUANTAPDF_OK)
        return status;
    status = document->ops->outline(document->backend, out_outline);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_context(document->context);
        return status;
    }
    quantapdf_snapshot_bind(&(*out_outline)->snapshot,
        document->context, quantapdf_dispose_outline);
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_outline_count(
    const quantapdf_outline *outline,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (outline == NULL || out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = outline->count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_outline_get_info(
    const quantapdf_outline *outline,
    size_t index,
    quantapdf_outline_info *out_info)
{
    const quantapdf_outline_node_internal *node;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_outline_info, is_open) +
        sizeof(out_info->is_open);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->parent_index = SIZE_MAX;
    out_info->first_child_index = SIZE_MAX;
    out_info->next_sibling_index = SIZE_MAX;
    out_info->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_NONE;
    out_info->target_page = -1;
    out_info->target = (quantapdf_point){ 0 };
    out_info->is_open = 0;
    if (outline == NULL || index >= outline->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    node = &outline->nodes[index];
    out_info->parent_index = node->parent_index;
    out_info->first_child_index = node->first_child_index;
    out_info->next_sibling_index = node->next_sibling_index;
    out_info->destination_kind = node->destination_kind;
    out_info->target_page = node->target_page;
    out_info->target = node->target;
    out_info->is_open = node->is_open;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_outline_title(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_outline_node_internal *node;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (outline == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= outline->count)
        return QUANTAPDF_ERROR_ARGUMENT;
    node = &outline->nodes[index];
    if (!node->has_title)
        return QUANTAPDF_OK;
    *out_utf8 = outline->strings + node->title_offset;
    *out_size = node->title_size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_outline_uri(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_outline_node_internal *node;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (outline == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= outline->count)
        return QUANTAPDF_ERROR_ARGUMENT;
    node = &outline->nodes[index];
    if (node->destination_kind != QUANTAPDF_OUTLINE_DESTINATION_URI)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_utf8 = outline->strings + node->uri_offset;
    *out_size = node->uri_size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_outline(quantapdf_outline *outline)
{
    quantapdf_snapshot_drop(outline == NULL ? NULL : &outline->snapshot);
}
