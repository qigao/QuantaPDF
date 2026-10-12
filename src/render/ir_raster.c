#include "render/ir_raster.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* V0 deliberately admits a complete, tiny shape language instead of
 * attempting approximate rendering of PDF paths. Extend admission and
 * rasterization together; never accept a shape that is not actually drawn. */
typedef struct raster_rect {
    size_t x0, y0, x1, y1;
    uint32_t argb;
} raster_rect;

static int integer_coord(float v, size_t bound, size_t *result)
{
    size_t n;
    if (!isfinite(v) || v < 0.0f || (double)v > (double)bound)
        return 0;
    n = (size_t)v;
    if ((double)n != (double)v)
        return 0;
    *result = n;
    return 1;
}

static int identity_or_default(const quantapdf_affine_transform *t)
{
    int zero = t->a == 0.0f && t->b == 0.0f &&
               t->c == 0.0f && t->d == 0.0f &&
               t->e == 0.0f && t->f == 0.0f;
    int identity = t->a == 1.0f && t->b == 0.0f &&
                   t->c == 0.0f && t->d == 1.0f &&
                   t->e == 0.0f && t->f == 0.0f;
    return zero || identity;
}

static quantapdf_status admit_path(
    const quantapdf_composer_operation *op,
    size_t width, size_t height, raster_rect *out)
{
    const quantapdf_composer_path_operation *path = &op->value.path;
    const quantapdf_composer_path_options *options = &path->options;
    size_t x[4], y[4], x0, x1, y0, y1, i;
    unsigned corners = 0u;

    if (op->graphics_state_id != 0u ||
        options->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
        options->stroke != 0 || options->fill != 1 ||
        (options->fill_argb >> 24u) != 255u ||
        (options->fill_rule != QUANTAPDF_COMPOSER_FILL_NONZERO &&
         options->fill_rule != QUANTAPDF_COMPOSER_FILL_EVEN_ODD) ||
        (options->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
         options->graphics_state_id != 0u) ||
        (options->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE &&
         (options->fill_paint_id != 0u || options->stroke_paint_id != 0u)) ||
        (options->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE &&
         !identity_or_default(&options->transform)) ||
        path->dash_count != 0u ||
        path->command_count != 5u || path->commands == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if (path->commands[0].kind != QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
        path->commands[1].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        path->commands[2].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        path->commands[3].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        path->commands[4].kind != QUANTAPDF_COMPOSER_PATH_CLOSE)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    for (i = 0u; i < 4u; ++i) {
        const quantapdf_point *p = &path->commands[i].point1;
        if (!integer_coord(p->x, width, &x[i]) ||
            !integer_coord(p->y, height, &y[i]))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    x0 = x1 = x[0];
    y0 = y1 = y[0];
    for (i = 1u; i < 4u; ++i) {
        if (x[i] < x0) x0 = x[i];
        if (x[i] > x1) x1 = x[i];
        if (y[i] < y0) y0 = y[i];
        if (y[i] > y1) y1 = y[i];
    }
    if (x0 == x1 || y0 == y1)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    for (i = 0u; i < 4u; ++i) {
        size_t j = (i + 1u) % 4u;
        unsigned bit;
        /* Each side is nonzero and axis-aligned, including the close side. */
        if ((x[i] == x[j]) == (y[i] == y[j]))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if ((x[i] != x0 && x[i] != x1) ||
            (y[i] != y0 && y[i] != y1))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        bit = 1u << ((x[i] == x1 ? 1u : 0u) +
                     (y[i] == y1 ? 2u : 0u));
        if ((corners & bit) != 0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        corners |= bit;
    }
    if (corners != 15u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    out->x0 = x0; out->x1 = x1;
    out->y0 = y0; out->y1 = y1;
    out->argb = options->fill_argb;
    return QUANTAPDF_OK;
}

void quantapdf_ir_raster_image_dispose(quantapdf_ir_raster_image *image)
{
    if (image == NULL)
        return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}

static void write_rgba(unsigned char *pixel, uint32_t argb)
{
    pixel[0] = (unsigned char)((argb >> 16u) & 255u);
    pixel[1] = (unsigned char)((argb >> 8u) & 255u);
    pixel[2] = (unsigned char)(argb & 255u);
    pixel[3] = (unsigned char)((argb >> 24u) & 255u);
}

quantapdf_status quantapdf_ir_raster_render_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    size_t width, height, stride, bytes, i, x, y;
    unsigned char *pixels;
    const quantapdf_composer_page_state *page;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL ||
        limits->max_pixels == 0u || limits->max_operations == 0u ||
        limits->max_pixels > SIZE_MAX / 4u ||
        page_index >= ir->page_count || ir->pages == NULL ||
        ir->operation_count > limits->max_operations ||
        (ir->operation_count != 0u && ir->operations == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;

    page = &ir->pages[page_index];
    if (!isfinite(page->width_points) ||
        !isfinite(page->height_points) ||
        page->width_points <= 0.0f || page->height_points <= 0.0f ||
        (double)page->width_points > (double)limits->max_pixels ||
        (double)page->height_points > (double)limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    width = (size_t)page->width_points;
    height = (size_t)page->height_points;
    if (width == 0u || height == 0u ||
        (double)width != (double)page->width_points ||
        (double)height != (double)page->height_points ||
        width > limits->max_pixels / height ||
        (page->suppress_background != 0 && page->suppress_background != 1) ||
        (!page->suppress_background && (page->background_argb >> 24u) != 255u))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* V0 must not silently ignore complex paints, glyph resources, clips,
     * forms or document navigation. This is a strict capability gate. */
    if (ir->resources.image_count != 0u || ir->resources.font_count != 0u ||
        ir->resources.graphics_state_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.clip_count != 0u ||
        ir->resources.paint_count != 0u ||
        ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Full preflight, before allocation or any pixels are published. */
    for (i = 0u; i < ir->operation_count; ++i) {
        raster_rect rect;
        const quantapdf_composer_operation *op = &ir->operations[i];
        if (op->page_index >= ir->page_count)
            return QUANTAPDF_ERROR_ARGUMENT;
        if (op->page_index != page_index)
            continue;
        if (op->kind != QUANTAPDF_COMPOSER_OPERATION_PATH ||
            admit_path(op, width, height, &rect) != QUANTAPDF_OK)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    stride = width * 4u;
    bytes = stride * height;
    pixels = (unsigned char *)malloc(bytes);
    if (pixels == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    /* Transparent canvas, or fully opaque background. */
    if (page->suppress_background) {
        memset(pixels, 0, bytes);
    } else {
        for (i = 0u; i < width * height; ++i)
            write_rgba(pixels + 4u * i, page->background_argb);
    }

    for (i = 0u; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *op = &ir->operations[i];
        raster_rect rect;
        if (op->page_index != page_index)
            continue;
        /* Already admitted above: cannot fail after publication begins. */
        (void)admit_path(op, width, height, &rect);
        for (y = rect.y0; y < rect.y1; ++y)
            for (x = rect.x0; x < rect.x1; ++x)
                write_rgba(pixels + y * stride + x * 4u, rect.argb);
    }

    out_image->pixels = pixels;
    out_image->size = bytes;
    out_image->width = width;
    out_image->height = height;
    out_image->stride = stride;
    return QUANTAPDF_OK;
}
