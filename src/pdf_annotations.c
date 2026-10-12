#include "internal.h"

#include <stddef.h>
#include <stdlib.h>

static void quantapdf_dispose_annotation_page(
    quantapdf_annotation_page *annotations)
{
    if (annotations == NULL)
        return;
    free(annotations->items);
    free(annotations->strings);
    free(annotations);
}

quantapdf_status quantapdf_extract_annotations(
    quantapdf_page *page,
    quantapdf_annotation_page **out_annotations)
{
    quantapdf_status status;
    double user_unit;

    if (out_annotations == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_annotations = NULL;
    if (page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_document_page_user_unit(
        page->document, page->page_index, &user_unit);
    if (status != QUANTAPDF_OK)
        return status;
    (void)user_unit;
    return page->document->ops->extract_annotations(
        page->document->backend,
        page->page_index,
        out_annotations);
}

quantapdf_status quantapdf_annotation_count(
    const quantapdf_annotation_page *annotations,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (annotations == NULL || out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = annotations->count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_annotation_get_info(
    const quantapdf_annotation_page *annotations,
    size_t index,
    quantapdf_annotation_info *out_info)
{
    const quantapdf_annotation_internal *item;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_annotation_info, flags) +
        sizeof(out_info->flags);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->type = QUANTAPDF_ANNOTATION_UNKNOWN;
    out_info->bounds = (quantapdf_rect){ 0 };
    out_info->flags = 0;
    if (annotations == NULL || index >= annotations->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    item = &annotations->items[index];
    out_info->type = item->type;
    out_info->bounds = item->bounds;
    out_info->flags = item->flags;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_annotation_contents(
    const quantapdf_annotation_page *annotations,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_annotation_internal *item;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (annotations == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= annotations->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    item = &annotations->items[index];
    if (!item->has_contents)
        return QUANTAPDF_OK;
    *out_utf8 = annotations->strings + item->contents_offset;
    *out_size = item->contents_size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_annotation_page(
    quantapdf_annotation_page *annotations)
{
    quantapdf_dispose_annotation_page(annotations);
}
