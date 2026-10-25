#include "render/ir_raster_mixed_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_MIXED_MAX_PIXELS ((size_t)1048576u)

/* All components remain straight alpha, matching the two existing native
 * layer rasterizers. One 64-bit integer division per channel is deterministic
 * across platforms and preserves the fully transparent black convention. */
static void qp_mixed_over(unsigned char *dst, const unsigned char *src)
{
    const unsigned sa = src[3], da = dst[3];
    const uint64_t a = (uint64_t)sa * 255u +
        (uint64_t)da * (255u - sa);
    size_t c;
    if (a == 0u) {
        memset(dst, 0, 4u);
        return;
    }
    for (c=0u; c<3u; ++c) {
        const uint64_t n = (uint64_t)src[c] * sa * 255u +
            (uint64_t)dst[c] * da * (255u - sa);
        dst[c] = (unsigned char)((n + a / 2u) / a);
    }
    dst[3] = (unsigned char)((a + 127u) / 255u);
}

quantapdf_status quantapdf_ir_raster_mixed_path_image_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_mixed_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_page_state *page;
    quantapdf_composer_page_state isolated_page;
    quantapdf_ir_raster_image layers[2] = {{0}};
    quantapdf_ir projected = {0};
    quantapdf_composer_operation operation;
    quantapdf_ir_raster_v1_limits path_limits;
    quantapdf_ir_raster_image_v1_limits image_limits;
    unsigned char *result = NULL;
    size_t width, height, n, bytes, i, x;
    unsigned path_count=0u, image_count=0u;
    quantapdf_status status = QUANTAPDF_ERROR_UNSUPPORTED;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        ir->operations == NULL || ir->page_count != 1u || page_index != 0u ||
        limits->max_pixels == 0u || limits->max_edges == 0u ||
        limits->max_sample_tests == 0u ||
        limits->max_source_pixels == 0u ||
        limits->max_drawn_pixels == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (ir->operation_count != 2u ||
        ir->resources.images == NULL || ir->resources.image_count != 1u ||
        ir->resources.graphics_state_count != 0u ||
        ir->resources.clip_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.paint_count != 0u ||
        ir->resources.font_count != 0u ||
        ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    page = &ir->pages[0];
    if (!isfinite(page->width_points) || !isfinite(page->height_points) ||
        page->width_points <= 0.0f || page->height_points <= 0.0f ||
        page->width_points > (float)QP_MIXED_MAX_PIXELS ||
        page->height_points > (float)QP_MIXED_MAX_PIXELS ||
        (page->suppress_background != 0 && page->suppress_background != 1) ||
        (!page->suppress_background &&
         (page->background_argb >> 24u) != 255u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    width = (size_t)page->width_points;
    height = (size_t)page->height_points;
    if ((float)width != page->width_points ||
        (float)height != page->height_points || height == 0u ||
        width > QP_MIXED_MAX_PIXELS / height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    n = width * height;
    if (n > limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    bytes = n * 4u;

    /* The whole selected operation sequence must be understood. No hidden
     * PostScript/Text operation, phantom GS, unused IMAGE resource or
     * undeclared paint can be silently elided by projecting one layer. */
    for(i=0u;i<2u;++i) {
        const quantapdf_composer_operation *op = &ir->operations[i];
        if (op->page_index != 0u || op->graphics_state_id != 0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        switch(op->kind) {
        case QUANTAPDF_COMPOSER_OPERATION_PATH: {
            const quantapdf_composer_path_operation *p = &op->value.path;
            const quantapdf_composer_path_options *opt = &p->options;
            if (++path_count != 1u ||
                opt->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
                opt->fill != 1 || opt->stroke != 0 ||
                (opt->fill_argb >> 24u) != 255u ||
                p->dash_count != 0u ||
                (opt->struct_size >=
                     QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
                 opt->graphics_state_id != 0u) ||
                (opt->struct_size >=
                     QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE &&
                 (opt->fill_paint_id != 0u || opt->stroke_paint_id != 0u)))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            break;
        }
        case QUANTAPDF_COMPOSER_OPERATION_IMAGE:
            if (++image_count != 1u ||
                op->value.image.image_id != 1u)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            break;
        default:
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    if(path_count != 1u || image_count != 1u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    isolated_page = *page;
    isolated_page.suppress_background = 1;
    isolated_page.background_argb = 0u;
    projected.pages = &isolated_page;
    projected.page_count = 1u;
    projected.operations = &operation;
    projected.operation_count = 1u;
    path_limits.max_pixels = limits->max_pixels;
    path_limits.max_operations = 1u;
    path_limits.max_edges = limits->max_edges;
    path_limits.max_sample_tests = limits->max_sample_tests;
    image_limits.max_pixels = limits->max_pixels;
    image_limits.max_operations = 1u;
    image_limits.max_source_pixels = limits->max_source_pixels;
    image_limits.max_drawn_pixels = limits->max_drawn_pixels;

    /* Each layer has exactly the resources needed by the matching already
     * admitted C11 raster. Both must succeed BEFORE the result is allocated
     * or published. The original immutable operations and resources are
     * never mutated or disposed here. */
    for(i=0u;i<2u;++i) {
        operation = ir->operations[i];
        memset(&projected.resources, 0, sizeof(projected.resources));
        if(operation.kind == QUANTAPDF_COMPOSER_OPERATION_PATH) {
            status = quantapdf_ir_raster_render_v1(
                &projected, 0u, &path_limits, &layers[i]);
        } else {
            projected.resources.images = ir->resources.images;
            projected.resources.image_count = 1u;
            status = quantapdf_ir_raster_raw_images_v1(
                &projected, 0u, &image_limits, &layers[i]);
        }
        if(status != QUANTAPDF_OK)
            goto cleanup;
        if(layers[i].width != width || layers[i].height != height ||
           layers[i].stride != width * 4u || layers[i].size != bytes ||
           layers[i].pixels == NULL) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
    }

    result = (unsigned char *)malloc(bytes);
    if(result == NULL) {
        status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }
    for(x=0u;x<n;++x) {
        const uint32_t bg = page->background_argb;
        unsigned char *d = result + 4u*x;
        d[0] = page->suppress_background ? 0u :
            (unsigned char)((bg >> 16u) & 255u);
        d[1] = page->suppress_background ? 0u :
            (unsigned char)((bg >> 8u) & 255u);
        d[2] = page->suppress_background ? 0u :
            (unsigned char)(bg & 255u);
        d[3] = page->suppress_background ? 0u : 255u;
    }
    for(i=0u;i<2u;++i)
        for(x=0u;x<n;++x)
            qp_mixed_over(result+4u*x,layers[i].pixels+4u*x);
    out_image->pixels = result;
    out_image->size = bytes;
    out_image->stride = width * 4u;
    out_image->width = width;
    out_image->height = height;
    result = NULL;
    status = QUANTAPDF_OK;
cleanup:
    free(result);
    quantapdf_ir_raster_image_dispose(&layers[0]);
    quantapdf_ir_raster_image_dispose(&layers[1]);
    return status;
}
