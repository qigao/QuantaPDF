#include "bitmap_internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_bitmap_dimensions(
    quantapdf_bitmap *bitmap,
    int *out_width,
    int *out_height,
    int *out_stride,
    int *out_components)
{
    if (bitmap == NULL || out_width == NULL || out_height == NULL ||
        out_stride == NULL || out_components == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_width = bitmap->width;
    *out_height = bitmap->height;
    *out_stride = bitmap->stride;
    *out_components = bitmap->components;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_bitmap_data(
    quantapdf_bitmap *bitmap,
    const unsigned char **out_data,
    size_t *out_size)
{
    if (bitmap == NULL || out_data == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_data = bitmap->data;
    *out_size = bitmap->size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_bitmap(quantapdf_bitmap *bitmap)
{
    quantapdf_context *context;
    if (bitmap == NULL)
        return;
    context = bitmap->context;

    free(bitmap->data);
    free(bitmap);
    quantapdf_drop_context(context);
}
