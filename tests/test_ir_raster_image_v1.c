#include "render/ir_raster_image_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "%s:%d: raw image v1: %s\n", __FILE__, __LINE__, #e); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

static uint32_t rgba(const quantapdf_ir_raster_image *image,
                     size_t x, size_t y)
{
    const unsigned char *p = image->pixels + y * image->stride + 4u * x;
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) | p[3];
}

static void image(quantapdf_composer_image_state *out,
                  unsigned char *data, size_t size,
                  unsigned char *alpha, size_t alpha_size,
                  unsigned width, unsigned height, int components)
{
    memset(out, 0, sizeof(*out));
    out->data = data;
    out->size = size;
    out->width = width;
    out->height = height;
    out->components = components;
    out->format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    out->alpha_data = alpha;
    out->alpha_size = alpha_size;
    out->has_alpha = alpha != NULL;
}

static void draw(quantapdf_composer_operation *op,
                 quantapdf_composer_image_id image_id,
                 float x0, float y0, float x1, float y1)
{
    memset(op, 0, sizeof(*op));
    op->kind = QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    op->value.image.image_id = image_id;
    op->value.image.options.struct_size =
        QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    op->value.image.options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    op->bounds.x0 = x0;
    op->bounds.y0 = y0;
    op->bounds.x1 = x1;
    op->bounds.y1 = y1;
}

static int test_rgb_gray_alpha_layers(void)
{
    unsigned char rgb[] = {
        255, 0, 0,  0, 255, 0,
        0, 0, 255,  255, 255, 255
    };
    unsigned char gray[] = {128};
    unsigned char black[] = {0, 0, 0};
    unsigned char alpha[] = {128};
    quantapdf_composer_page_state page = {0};
    quantapdf_composer_operation ops[3] = {{0}};
    quantapdf_composer_image_state images[3] = {{0}};
    quantapdf_ir ir = {0};
    quantapdf_ir_raster_image result = {0}, repeated = {0};
    const quantapdf_ir_raster_image_v1_limits limits = {64u, 4u, 6u, 48u};
    unsigned char *save;
    page.width_points = 8.0f;
    page.height_points = 8.0f;
    page.background_argb = UINT32_C(0xffffffff);
    image(&images[0], rgb, sizeof(rgb), NULL, 0u, 2u, 2u, 3);
    image(&images[1], gray, sizeof(gray), NULL, 0u, 1u, 1u, 1);
    image(&images[2], black, sizeof(black), alpha, sizeof(alpha), 1u, 1u, 3);
    draw(&ops[0], 1u, 2.0f, 1.0f, 6.0f, 5.0f);
    draw(&ops[1], 2u, 0.0f, 5.0f, 4.0f, 8.0f);
    draw(&ops[2], 3u, 2.0f, 1.0f, 6.0f, 5.0f);
    ir.pages = &page;
    ir.page_count = 1u;
    ir.operations = ops;
    ir.operation_count = 3u;
    ir.resources.images = images;
    ir.resources.image_count = 3u;
    OK(quantapdf_ir_raster_raw_images_v1(&ir, 0u, &limits, &result));
    OK(quantapdf_ir_raster_raw_images_v1(&ir, 0u, &limits, &repeated));
    CHECK(result.pixels != NULL && result.width == 8u && result.height == 8u &&
          result.stride == 32u && result.size == 256u &&
          result.size == repeated.size &&
          memcmp(result.pixels, repeated.pixels, result.size) == 0);
    CHECK(rgba(&result, 0u, 0u) == UINT32_C(0xffffffff));
    CHECK(rgba(&result, 2u, 1u) == UINT32_C(0x7f0000ff));
    CHECK(rgba(&result, 5u, 1u) == UINT32_C(0x007f00ff));
    CHECK(rgba(&result, 2u, 4u) == UINT32_C(0x00007fff));
    CHECK(rgba(&result, 5u, 4u) == UINT32_C(0x7f7f7fff));
    CHECK(rgba(&result, 1u, 6u) == UINT32_C(0x808080ff));
    CHECK(rgba(&result, 7u, 7u) == UINT32_C(0xffffffff));

    save = (unsigned char *)malloc(result.size);
    CHECK(save != NULL);
    memcpy(save, result.pixels, result.size);
    memset(rgb, 0, sizeof(rgb));
    gray[0] = 0;
    alpha[0] = 0;
    /* No source/IR borrows escape the returned independent output bytes. */
    CHECK(memcmp(result.pixels, save, result.size) == 0);
    free(save);
    quantapdf_ir_raster_image_dispose(&result);
    quantapdf_ir_raster_image_dispose(&repeated);
    CHECK(result.pixels == NULL && result.size == 0u && result.width == 0u);
    return EXIT_SUCCESS;
}

static int test_transparent_source_over(void)
{
    unsigned char red[] = {255u, 0u, 0u};
    unsigned char blue[] = {0u, 0u, 255u};
    unsigned char opacity[] = {128u};
    quantapdf_composer_page_state page = {0};
    quantapdf_composer_operation ops[2] = {{0}};
    quantapdf_composer_image_state images[2] = {{0}};
    quantapdf_ir ir = {0};
    quantapdf_ir_raster_image result = {0};
    const quantapdf_ir_raster_image_v1_limits limits = {16u, 2u, 2u, 20u};
    page.width_points = 4.0f;
    page.height_points = 4.0f;
    page.suppress_background = 1;
    image(&images[0], red, sizeof(red), opacity, sizeof(opacity), 1u, 1u, 3);
    image(&images[1], blue, sizeof(blue), opacity, sizeof(opacity), 1u, 1u, 3);
    draw(&ops[0], 1u, 0.0f, 0.0f, 2.0f, 2.0f);
    draw(&ops[1], 2u, 1.0f, 1.0f, 3.0f, 3.0f);
    ir.pages = &page;
    ir.page_count = 1u;
    ir.operations = ops;
    ir.operation_count = 2u;
    ir.resources.images = images;
    ir.resources.image_count = 2u;
    OK(quantapdf_ir_raster_raw_images_v1(&ir, 0u, &limits, &result));
    CHECK(rgba(&result, 0u, 0u) == UINT32_C(0xff000080));
    CHECK(rgba(&result, 1u, 1u) == UINT32_C(0x5500aac0));
    CHECK(rgba(&result, 2u, 2u) == UINT32_C(0x0000ff80));
    CHECK(rgba(&result, 3u, 3u) == UINT32_C(0x00000000));
    quantapdf_ir_raster_image_dispose(&result);
    return EXIT_SUCCESS;
}

static int refused(quantapdf_ir *ir,
                   quantapdf_ir_raster_image_v1_limits *limits)
{
    quantapdf_ir_raster_image out;
    quantapdf_status status;
    memset(&out, 0xa5, sizeof(out));
    status = quantapdf_ir_raster_raw_images_v1(ir, 0u, limits, &out);
    CHECK(status == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(out.pixels == NULL && out.size == 0u && out.width == 0u &&
          out.height == 0u && out.stride == 0u);
    return EXIT_SUCCESS;
}

static int test_atomic_negative_matrix(void)
{
    unsigned char pixels[] = {30u, 40u, 50u};
    unsigned char alpha[] = {128u};
    quantapdf_composer_page_state page = {0};
    quantapdf_composer_operation ops[2] = {{0}};
    quantapdf_composer_image_state images[2] = {{0}};
    quantapdf_ir ir = {0};
    quantapdf_ir_raster_image_v1_limits limits = {16u, 2u, 2u, 32u};
    page.width_points = 4.0f;
    page.height_points = 4.0f;
    page.background_argb = UINT32_C(0xffffffff);
    image(&images[0], pixels, sizeof(pixels), NULL, 0u, 1u, 1u, 3);
    draw(&ops[0], 1u, 0.0f, 0.0f, 4.0f, 4.0f);
    ir.pages = &page;
    ir.page_count = 1u;
    ir.operations = ops;
    ir.operation_count = 1u;
    ir.resources.images = images;
    ir.resources.image_count = 1u;

    limits.max_pixels = 15u;
    CHECK(refused(&ir, &limits) == 0);
    limits.max_pixels = 16u;
    limits.max_drawn_pixels = 15u;
    CHECK(refused(&ir, &limits) == 0);
    limits.max_drawn_pixels = 32u;
    limits.max_source_pixels = 0u;
    {
        quantapdf_ir_raster_image out = {0};
        CHECK(quantapdf_ir_raster_raw_images_v1(&ir,0u,&limits,&out) ==
              QUANTAPDF_ERROR_ARGUMENT && out.pixels == NULL);
    }
    limits.max_source_pixels = 2u;
    images[0].format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_PNG;
    CHECK(refused(&ir, &limits) == 0);
    images[0].format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    images[0].size = 2u;
    CHECK(refused(&ir, &limits) == 0);
    images[0].size = sizeof(pixels);
    images[0].has_alpha = 1;
    CHECK(refused(&ir, &limits) == 0);
    images[0].alpha_data = alpha;
    images[0].alpha_size = sizeof(alpha);
    images[0].has_alpha = 0;
    CHECK(refused(&ir, &limits) == 0);
    images[0].has_alpha = 1;

    ops[0].bounds.x0 = 0.5f;
    CHECK(refused(&ir, &limits) == 0);
    ops[0].bounds.x0 = 0.0f;
    ops[0].bounds.x1 = 5.0f;
    CHECK(refused(&ir, &limits) == 0);
    ops[0].bounds.x1 = 4.0f;
    ops[0].value.image.options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_COVER;
    CHECK(refused(&ir, &limits) == 0);
    ops[0].value.image.options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    ops[0].value.image.image_id = 2u;
    CHECK(refused(&ir, &limits) == 0);
    ops[0].value.image.image_id = 1u;
    ir.resources.image_count = 2u;
    CHECK(refused(&ir, &limits) == 0);
    ir.resources.image_count = 1u;
    page.suppress_background = 2;
    CHECK(refused(&ir, &limits) == 0);
    page.suppress_background = 0;
    page.background_argb = UINT32_C(0x80ffffff);
    CHECK(refused(&ir, &limits) == 0);
    page.background_argb = UINT32_C(0xffffffff);
    ops[1].kind = QUANTAPDF_COMPOSER_OPERATION_TEXT;
    ir.operation_count = 2u;
    CHECK(refused(&ir, &limits) == 0);
    ir.operation_count = 1u;

    /* No invalid call may poison subsequent independent use. */
    {
        quantapdf_ir_raster_image positive = {0};
        OK(quantapdf_ir_raster_raw_images_v1(&ir,0u,&limits,&positive));
        CHECK(rgba(&positive,0u,0u) == UINT32_C(0x8e9398ff));
        quantapdf_ir_raster_image_dispose(&positive);
    }
    return EXIT_SUCCESS;
}

int main(void)
{
    if (test_rgb_gray_alpha_layers() != EXIT_SUCCESS ||
        test_transparent_source_over() != EXIT_SUCCESS ||
        test_atomic_negative_matrix() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    puts("Native RAW Image V1: source-owned Gray/RGB planes, alpha, "
         "layering, atomic refusal and detached RGBA PASS");
    return EXIT_SUCCESS;
}
