#include "render/ir_scoped_operations_v0.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int valid_clip_transform(const quantapdf_affine_transform *t)
{
    double a = (double)t->a, b = (double)t->b;
    double c = (double)t->c, d = (double)t->d;
    double e = (double)t->e, f = (double)t->f;
    double det;
    if (!isfinite(a) || !isfinite(b) || !isfinite(c) ||
        !isfinite(d) || !isfinite(e) || !isfinite(f) ||
        fabs(a) > 1000000.0 || fabs(b) > 1000000.0 ||
        fabs(c) > 1000000.0 || fabs(d) > 1000000.0 ||
        fabs(e) > 1000000.0 || fabs(f) > 1000000.0)
        return 0;
    det = a * d - b * c;
    return isfinite(det) && det != 0.0;
}

/* Straight-alpha source-over for a detached native V1 RGBA pixel.
 * Multiplications and numerator fit uint32 on every platform. */
static void over(unsigned char *dst, const unsigned char *src)
{
    unsigned sa = src[3], da = dst[3], keep = 255u - sa;
    unsigned denominator = 255u * sa + keep * da;
    size_t channel;
    if (sa == 0u)
        return;
    for (channel = 0u; channel < 3u; ++channel) {
        unsigned numerator = 255u * sa * (unsigned)src[channel] +
                             keep * da * (unsigned)dst[channel];
        dst[channel] =
            (unsigned char)((numerator + denominator / 2u) / denominator);
    }
    dst[3] = (unsigned char)((denominator + 127u) / 255u);
}

quantapdf_status quantapdf_ir_raster_scoped_operations_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_page_state *page;
    quantapdf_ir_raster_v1_limits per_operation;
    size_t width, height, count, bytes, i, k;
    unsigned states_used = 0u, clips_used = 0u;
    unsigned char *final_pixels = NULL;
    quantapdf_status status = QUANTAPDF_ERROR_ARGUMENT;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        ir->operations == NULL || ir->page_count != 1u || page_index != 0u ||
        ir->operation_count < 2u ||
        ir->operation_count > QUANTAPDF_IR_SCOPED_OPERATIONS_V0_MAX_OPERATIONS ||
        ir->operation_count > limits->max_operations ||
        limits->max_pixels == 0u || limits->max_pixels > SIZE_MAX / 4u ||
        limits->max_edges == 0u ||
        limits->max_sample_tests / ir->operation_count == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (ir->resources.graphics_state_count >
            QUANTAPDF_IR_SCOPED_OPERATIONS_V0_MAX_OPERATIONS ||
        ir->resources.clip_count >
            QUANTAPDF_IR_SCOPED_OPERATIONS_V0_MAX_OPERATIONS ||
        (ir->resources.graphics_state_count != 0u &&
         ir->resources.graphics_states == NULL) ||
        (ir->resources.clip_count != 0u &&
         ir->resources.clips == NULL) ||
        ir->resources.image_count != 0u || ir->resources.font_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.paint_count != 0u || ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    page = &ir->pages[0];
    if (!isfinite(page->width_points) || !isfinite(page->height_points) ||
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
    count = width * height;
    bytes = count * 4u;

    /* Admit all state/resource associations before raster allocation. Each
     * operation's immutable state is scoped to that operation only. */
    for (i = 0u; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *op = &ir->operations[i];
        const quantapdf_composer_graphics_state *gs;
        const quantapdf_composer_path_options *opt;
        size_t id, clip_id;
        if (op->page_index != 0u ||
            op->kind != QUANTAPDF_COMPOSER_OPERATION_PATH)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        opt = &op->value.path.options;
        id = (size_t)op->graphics_state_id;
        if (id != 0u &&
            (opt->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE ||
             opt->graphics_state_id != op->graphics_state_id))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (id == 0u &&
            opt->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
            opt->graphics_state_id != 0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (id == 0u)
            continue;
        if (id > ir->resources.graphics_state_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        states_used |= 1u << (unsigned)(id - 1u);
        gs = &ir->resources.graphics_states[id - 1u];
        if (gs->blend_mode != QUANTAPDF_COMPOSER_BLEND_NORMAL ||
            gs->fill_alpha != 1.0f || gs->stroke_alpha != 1.0f ||
            gs->soft_mask_id != 0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        clip_id = (size_t)gs->clip_id;
        if (clip_id == 0u)
            continue;
        if (clip_id > ir->resources.clip_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        clips_used |= 1u << (unsigned)(clip_id - 1u);
        {
            const quantapdf_composer_clip_state *clip =
                &ir->resources.clips[clip_id - 1u];
            if (clip->kind != QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
                clip->commands == NULL || clip->command_count == 0u ||
                clip->members != NULL || clip->member_count != 0u ||
                !valid_clip_transform(&clip->transform))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    if (states_used != ((1u << (unsigned)ir->resources.graphics_state_count) - 1u) ||
        clips_used != ((1u << (unsigned)ir->resources.clip_count) - 1u))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    final_pixels = (unsigned char *)malloc(bytes);
    if (final_pixels == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    for (k = 0u; k < count; ++k) {
        unsigned char *dst = final_pixels + k * 4u;
        if (page->suppress_background) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0u;
        } else {
            dst[0] = (unsigned char)(page->background_argb >> 16u);
            dst[1] = (unsigned char)(page->background_argb >> 8u);
            dst[2] = (unsigned char)page->background_argb;
            dst[3] = 255u;
        }
    }

    per_operation = *limits;
    per_operation.max_operations = 1u;
    /* Enforce global sampled-edge budget by dividing it across calls.
     * The V1 engine bills exact draw+clip work within each allotment. */
    per_operation.max_sample_tests =
        limits->max_sample_tests / ir->operation_count;

    for (i = 0u; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *source = &ir->operations[i];
        const quantapdf_composer_clip_state *clip = NULL;
        quantapdf_composer_operation operation = *source;
        quantapdf_composer_page_state transparent_page = *page;
        quantapdf_ir local = {0};
        quantapdf_ir_raster_image layer = {0};
        size_t gs_id = (size_t)source->graphics_state_id;
        if (gs_id != 0u) {
            size_t cid = (size_t)ir->resources.graphics_states[gs_id-1u].clip_id;
            if (cid != 0u)
                clip = &ir->resources.clips[cid-1u];
        }
        operation.graphics_state_id = 0u;
        operation.value.path.options.graphics_state_id = 0u;
        transparent_page.suppress_background = 1;
        local.pages = &transparent_page;
        local.page_count = 1u;
        local.operations = &operation;
        local.operation_count = 1u;
        if (clip == NULL) {
            status = quantapdf_ir_raster_render_v1(
                &local, 0u, &per_operation, &layer);
        } else {
            status = quantapdf_ir_raster_render_v1_clipped(
                &local, 0u, &per_operation, clip, &layer);
        }
        if (status != QUANTAPDF_OK)
            goto failure;
        if (layer.width != width || layer.height != height ||
            layer.stride != width * 4u || layer.size != bytes ||
            layer.pixels == NULL) {
            quantapdf_ir_raster_image_dispose(&layer);
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto failure;
        }
        for (k = 0u; k < count; ++k)
            over(final_pixels + 4u * k, layer.pixels + 4u * k);
        quantapdf_ir_raster_image_dispose(&layer);
    }

    out_image->pixels = final_pixels;
    out_image->width = width;
    out_image->height = height;
    out_image->stride = width * 4u;
    out_image->size = bytes;
    return QUANTAPDF_OK;

failure:
    free(final_pixels);
    return status;
}
