#include "internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static quantapdf_status quantapdf_render_page_transformed(
    quantapdf_page *page,
    float dpi,
    float rotation_degrees,
    const quantapdf_rect *clip,
    int alpha,
    quantapdf_bitmap **out_bitmap)
{
    quantapdf_bitmap *bitmap;
    quantapdf_backend_bitmap rendered = {0};
    quantapdf_status status;
    double user_unit;

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL || !isfinite(dpi) || dpi <= 0.0f ||
        !isfinite(rotation_degrees) || (alpha != 0 && alpha != 1))
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_context_retain_internal(page->context);
    if (status != QUANTAPDF_OK)
        return status;
    bitmap = (quantapdf_bitmap *)quantapdf_context_allocate_internal(
        page->context, sizeof(*bitmap));
    if (bitmap == NULL) {
        quantapdf_drop_context(page->context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    bitmap->context = page->context;

    status = quantapdf_document_page_user_unit(
        page->document, page->page_index, &user_unit);
    if (status == QUANTAPDF_OK) {
        status = page->document->ops->render_page(
            page->backend, dpi, rotation_degrees, clip, user_unit,
            alpha, &rendered);
    }
    if (status != QUANTAPDF_OK) {
        free(bitmap);
        quantapdf_drop_context(page->context);
        return status;
    }

    bitmap->data = rendered.data;
    bitmap->size = rendered.size;
    bitmap->width = rendered.width;
    bitmap->height = rendered.height;
    bitmap->stride = rendered.stride;
    bitmap->components = rendered.components;

    *out_bitmap = bitmap;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_render_page(
    quantapdf_page *page,
    quantapdf_bitmap **out_bitmap)
{
    return quantapdf_render_page_transformed(
        page,
        72.0f,
        0.0f,
        NULL,
        0,
        out_bitmap);
}

quantapdf_status quantapdf_render_page_with_options(
    quantapdf_page *page,
    const quantapdf_render_options *options,
    quantapdf_bitmap **out_bitmap)
{
    size_t minimum_size;
    size_t rotation_size;
    size_t clip_enabled_size;
    size_t clip_size;
    size_t alpha_size;
    float rotation_degrees = 0.0f;
    int clip_enabled = 0;
    int alpha = 0;
    quantapdf_rect clip = { 0 };
    const quantapdf_rect *clip_ptr = NULL;

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL || options == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(quantapdf_render_options, dpi) + sizeof(options->dpi);
    if (options->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    rotation_size = offsetof(quantapdf_render_options, rotation_degrees) +
        sizeof(options->rotation_degrees);
    if (options->struct_size >= rotation_size)
        rotation_degrees = options->rotation_degrees;

    clip_enabled_size = offsetof(quantapdf_render_options, clip_enabled) +
        sizeof(options->clip_enabled);
    if (options->struct_size >= clip_enabled_size)
        clip_enabled = options->clip_enabled != 0;

    if (clip_enabled) {
        clip_size = offsetof(quantapdf_render_options, clip) + sizeof(options->clip);
        if (options->struct_size < clip_size)
            return QUANTAPDF_ERROR_ARGUMENT;

        clip = options->clip;
        if (!isfinite(clip.x0) || !isfinite(clip.y0) ||
            !isfinite(clip.x1) || !isfinite(clip.y1) ||
            clip.x1 <= clip.x0 || clip.y1 <= clip.y0)
            return QUANTAPDF_ERROR_ARGUMENT;
        clip_ptr = &clip;
    }

    alpha_size = offsetof(quantapdf_render_options, alpha) + sizeof(options->alpha);
    if (options->struct_size >= alpha_size) {
        alpha = options->alpha;
        if (alpha != 0 && alpha != 1)
            return QUANTAPDF_ERROR_ARGUMENT;
    }

    return quantapdf_render_page_transformed(
        page,
        options->dpi,
        rotation_degrees,
        clip_ptr,
        alpha,
        out_bitmap);
}

quantapdf_status quantapdf_render_thumbnail(
    quantapdf_page *page,
    int max_width,
    int max_height,
    quantapdf_bitmap **out_bitmap)
{
    quantapdf_rect bounds;
    quantapdf_status status;
    float page_width;
    float page_height;
    float scale_x;
    float scale_y;
    float scale;

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL || max_width <= 0 || max_height <= 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_page_bounds(page, &bounds);
    if (status != QUANTAPDF_OK)
        return status;

    page_width = bounds.x1 - bounds.x0;
    page_height = bounds.y1 - bounds.y0;
    if (!isfinite(page_width) || !isfinite(page_height) ||
        page_width <= 0.0f || page_height <= 0.0f)
        return QUANTAPDF_ERROR_FORMAT;

    scale_x = (float)max_width / page_width;
    scale_y = (float)max_height / page_height;
    scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale > 1.0f)
        scale = 1.0f;

    return quantapdf_render_page_transformed(
        page,
        72.0f * scale,
        0.0f,
        NULL,
        0,
        out_bitmap);
}

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
