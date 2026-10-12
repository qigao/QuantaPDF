#include "document/document_internal.h"
#include "snapshot/image_snapshot.h"

#include <stddef.h>
#include <stdlib.h>

static void quantapdf_dispose_image_page(quantapdf_snapshot *snapshot)
{
    quantapdf_image_page *images = (quantapdf_image_page *)snapshot;
    size_t index;

    if (images == NULL)
        return;
    for (index = 0; index < images->count; ++index)
        free(images->items[index].pixels);
    free(images->items);
    free(images);
}

quantapdf_status quantapdf_extract_images(
    quantapdf_page *page,
    quantapdf_image_page **out_images)
{
    quantapdf_status status;
    if (out_images == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_images = NULL;
    if (page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_context_retain_internal(page->context);
    if (status != QUANTAPDF_OK)
        return status;
    status = page->document->ops->extract_images(page->backend, out_images);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_context(page->context);
        return status;
    }
    quantapdf_snapshot_bind(&(*out_images)->snapshot,
        page->context, quantapdf_dispose_image_page);
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_image_count(
    const quantapdf_image_page *images,
    size_t *out_count)
{
    if (out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = 0;
    if (images == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = images->count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_image_get_info(
    const quantapdf_image_page *images,
    size_t index,
    quantapdf_image_info *out_info)
{
    const quantapdf_image_occurrence_internal *item;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_image_info, has_alpha) +
        sizeof(out_info->has_alpha);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->quad = (quantapdf_quad){ 0 };
    out_info->pixel_width = 0;
    out_info->pixel_height = 0;
    out_info->components = 0;
    out_info->bits_per_component = 0;
    out_info->has_alpha = 0;
    if (images == NULL || index >= images->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    item = &images->items[index];
    out_info->quad = item->quad;
    out_info->pixel_width = item->pixel_width;
    out_info->pixel_height = item->pixel_height;
    out_info->components = item->components;
    out_info->bits_per_component = item->bits_per_component;
    out_info->has_alpha = item->has_alpha;
    return QUANTAPDF_OK;
}

void quantapdf_drop_image_page(quantapdf_image_page *images)
{
    quantapdf_snapshot_drop(images == NULL ? NULL : &images->snapshot);
}
