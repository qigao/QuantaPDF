#include "internal.h"

#include <stdlib.h>
#include <string.h>

quantapdf_status quantapdf_image_render(
    const quantapdf_image_page *images,
    size_t index,
    quantapdf_bitmap **out_bitmap)
{
    const quantapdf_image_occurrence_internal *occurrence;
    quantapdf_bitmap *bitmap;
    quantapdf_status status;

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;
    if (images == NULL || index >= images->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    occurrence = &images->items[index];
    status = quantapdf_context_retain_internal(images->context);
    if (status != QUANTAPDF_OK)
        return status;
    bitmap = (quantapdf_bitmap *)quantapdf_context_allocate_internal(
        images->context, sizeof(*bitmap));
    if (bitmap == NULL) {
        quantapdf_drop_context(images->context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    bitmap->context = images->context;
    bitmap->data = (unsigned char *)malloc(occurrence->pixel_size);
    if (bitmap->data == NULL) {
        free(bitmap);
        quantapdf_drop_context(images->context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    memcpy(bitmap->data, occurrence->pixels, occurrence->pixel_size);
    bitmap->size = occurrence->pixel_size;
    bitmap->width = occurrence->pixel_width;
    bitmap->height = occurrence->pixel_height;
    bitmap->stride = occurrence->pixel_stride;
    bitmap->components = occurrence->pixel_components;
    *out_bitmap = bitmap;
    return QUANTAPDF_OK;
}
