#include "render/ir_raster_image_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_RAW_IMAGE_MAX_OPERATIONS 4u
#define QP_RAW_IMAGE_MAX_PIXELS ((size_t)4194304u)
#define QP_RAW_IMAGE_MAX_WORK ((size_t)16777216u)

typedef struct qp_image_layer {
    const quantapdf_composer_image_state *image;
    size_t x0, y0, width, height;
} qp_image_layer;

static int qp_integer_coord(float value, size_t bound, size_t *out)
{
    size_t integer;
    if (!isfinite(value) || value < 0.0f ||
        (double)value > (double)bound)
        return 0;
    integer = (size_t)value;
    if ((float)integer != value)
        return 0;
    *out = integer;
    return 1;
}

static quantapdf_status qp_admit_image(
    const quantapdf_composer_image_state *image,
    const quantapdf_ir_raster_image_v1_limits *limits,
    size_t *used_source_pixels)
{
    size_t pixels, bytes;
    if (image->format != QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW ||
        image->width == 0u || image->height == 0u ||
        (image->components != 1 && image->components != 3) ||
        (image->has_alpha != 0 && image->has_alpha != 1) ||
        image->data == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if ((size_t)image->width > QP_RAW_IMAGE_MAX_PIXELS /
        (size_t)image->height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels = (size_t)image->width * (size_t)image->height;
    if (pixels > limits->max_source_pixels ||
        *used_source_pixels > limits->max_source_pixels - pixels ||
        pixels > QP_RAW_IMAGE_MAX_PIXELS - *used_source_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (pixels > SIZE_MAX / (size_t)image->components)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    bytes = pixels * (size_t)image->components;
    if (image->size != bytes ||
        (image->has_alpha ?
            (image->alpha_data == NULL || image->alpha_size != pixels) :
            (image->alpha_data != NULL || image->alpha_size != 0u)))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    *used_source_pixels += pixels;
    return QUANTAPDF_OK;
}

static void qp_source_over(unsigned char *dst, const unsigned char *src,
                           unsigned alpha)
{
    unsigned da = dst[3];
    /* Retain exact straight alpha; postpone 8-bit quantization until the
     * final division. Intermediate products fit in 64 bits. */
    const uint64_t out_a = (uint64_t)alpha * 255u +
        (uint64_t)da * (255u - alpha);
    size_t channel;
    if (out_a == 0u) {
        memset(dst, 0, 4u);
        return;
    }
    for (channel = 0u; channel < 3u; ++channel) {
        const uint64_t numerator = (uint64_t)src[channel] * alpha * 255u +
            (uint64_t)dst[channel] * da * (255u - alpha);
        dst[channel] = (unsigned char)((numerator + out_a / 2u) / out_a);
    }
    dst[3] = (unsigned char)((out_a + 127u) / 255u);
}

static quantapdf_status qp_raw_images(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *limits,
    quantapdf_ir_raster_image *out_image,quantapdf_native_work_ledger_v1 *ledger)
{
    qp_image_layer layers[QP_RAW_IMAGE_MAX_OPERATIONS] = {{0}};
    const quantapdf_composer_page_state *page;
    unsigned char *pixels;
    size_t width, height, pixel_count, image_pixels = 0u, drawn = 0u;
    size_t i, x, y, component;
    unsigned used = 0u;
    quantapdf_status allocation_status=QUANTAPDF_OK;
    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        ir->operations == NULL || ir->page_count != 1u ||
        page_index != 0u || ir->operation_count == 0u ||
        limits->max_pixels == 0u || limits->max_operations == 0u ||
        limits->max_source_pixels == 0u || limits->max_drawn_pixels == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (ir->operation_count > QP_RAW_IMAGE_MAX_OPERATIONS ||
        ir->operation_count > limits->max_operations ||
        ir->resources.image_count == 0u ||
        ir->resources.image_count > QP_RAW_IMAGE_MAX_OPERATIONS ||
        ir->resources.images == NULL ||
        ir->resources.graphics_state_count != 0u ||
        ir->resources.font_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.clip_count != 0u ||
        ir->resources.paint_count != 0u ||
        ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    page = &ir->pages[0];
    if (!isfinite(page->width_points) || !isfinite(page->height_points) ||
        page->width_points <= 0.0f || page->height_points <= 0.0f ||
        page->width_points > (float)QP_RAW_IMAGE_MAX_PIXELS ||
        page->height_points > (float)QP_RAW_IMAGE_MAX_PIXELS ||
        (page->suppress_background != 0 && page->suppress_background != 1) ||
        (!page->suppress_background &&
         (page->background_argb >> 24u) != 255u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    width = (size_t)page->width_points;
    height = (size_t)page->height_points;
    if ((float)width != page->width_points ||
        (float)height != page->height_points || height == 0u ||
        width > QP_RAW_IMAGE_MAX_PIXELS / height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixel_count = width * height;
    if (pixel_count > limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Validate every source resource before looking at operations. Do not
     * dereference malformed resource IDs or publish partial paint. */
    for (i = 0u; i < ir->resources.image_count; ++i) {
        quantapdf_status status = qp_admit_image(
            &ir->resources.images[i], limits, &image_pixels);
        if (status != QUANTAPDF_OK)
            return status;
    }
    for (i = 0u; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *op = &ir->operations[i];
        size_t x0, x1, y0, y1, id, area;
        if (op->kind != QUANTAPDF_COMPOSER_OPERATION_IMAGE ||
            op->page_index != 0u || op->graphics_state_id != 0u ||
            op->value.image.options.struct_size <
                QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE ||
            op->value.image.options.fit !=
                QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH ||
            (op->value.image.options.struct_size >=
                QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE &&
             op->value.image.options.graphics_state_id != 0u))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        id = (size_t)op->value.image.image_id;
        if (id == 0u || id > ir->resources.image_count ||
            !qp_integer_coord(op->bounds.x0, width, &x0) ||
            !qp_integer_coord(op->bounds.x1, width, &x1) ||
            !qp_integer_coord(op->bounds.y0, height, &y0) ||
            !qp_integer_coord(op->bounds.y1, height, &y1) ||
            x1 <= x0 || y1 <= y0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        area = (x1 - x0) * (y1 - y0);
        if (area > limits->max_drawn_pixels ||
            drawn > limits->max_drawn_pixels - area ||
            area > QP_RAW_IMAGE_MAX_WORK - drawn)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        drawn += area;
        used |= 1u << (unsigned)(id - 1u);
        layers[i].image = &ir->resources.images[id - 1u];
        layers[i].x0 = x0;
        layers[i].y0 = y0;
        layers[i].width = x1 - x0;
        layers[i].height = y1 - y0;
    }
    if (used != ((1u << (unsigned)ir->resources.image_count) - 1u))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* All state/geometry/source budget checks precede output allocation. */
    pixels = (unsigned char *)quantapdf_native_raster_malloc_v1(ledger,pixel_count*4u,&allocation_status);
    if (pixels == NULL)
        return allocation_status;
    for (i = 0u; i < pixel_count; ++i) {
        unsigned char *dst = pixels + 4u * i;
        uint32_t argb = page->background_argb;
        dst[0] = page->suppress_background ? 0u :
            (unsigned char)((argb >> 16u) & 255u);
        dst[1] = page->suppress_background ? 0u :
            (unsigned char)((argb >> 8u) & 255u);
        dst[2] = page->suppress_background ? 0u :
            (unsigned char)(argb & 255u);
        dst[3] = page->suppress_background ? 0u : 255u;
    }

    for (i = 0u; i < ir->operation_count; ++i) {
        const qp_image_layer *layer = &layers[i];
        const quantapdf_composer_image_state *source = layer->image;
        for (y = 0u; y < layer->height; ++y) {
            size_t sy = (size_t)(((uint64_t)(2u * y + 1u) *
                (uint64_t)source->height) / (2u * (uint64_t)layer->height));
            for (x = 0u; x < layer->width; ++x) {
                size_t sx = (size_t)(((uint64_t)(2u * x + 1u) *
                    (uint64_t)source->width) / (2u * (uint64_t)layer->width));
                size_t source_index = sy * (size_t)source->width + sx;
                const unsigned char *src = source->data +
                    source_index * (size_t)source->components;
                unsigned char rgb[3];
                unsigned alpha = source->has_alpha ?
                    source->alpha_data[source_index] : 255u;
                unsigned char *dst = pixels + 4u *
                    ((layer->y0 + y) * width + layer->x0 + x);
                for (component = 0u; component < 3u; ++component)
                    rgb[component] = src[source->components == 1 ? 0u :
                        component];
                qp_source_over(dst, rgb, alpha);
            }
        }
    }
    out_image->pixels = pixels;
    out_image->size = pixel_count * 4u;
    out_image->width = width;
    out_image->height = height;
    out_image->stride = width * 4u;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_ir_raster_raw_images_v1(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *limits,quantapdf_ir_raster_image *out)
{
    return qp_raw_images(ir,page_index,limits,out,NULL);
}

quantapdf_status quantapdf_ir_raster_raw_images_v1_borrowed(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *limits,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_ir_raster_image *out)
{
    return qp_raw_images(ir,page_index,limits,out,ledger);
}
