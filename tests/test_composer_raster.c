#include <quantapdf/quantapdf.h>

#include "composer_test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int add_page(
    quantapdf_composer *composer,
    uint32_t background,
    size_t expected)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 120.0f;
    page.height_points = 100.0f;
    page.background_argb = background;
    return quantapdf_composer_add_page(
               composer, &page, &page_index) == QUANTAPDF_OK &&
        page_index == expected;
}

static quantapdf_composer_image_options stretch_options(void)
{
    quantapdf_composer_image_options options = {0};
    options.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    return options;
}

static int pixel_near(
    const unsigned char *pixels,
    int stride,
    int x,
    int y,
    int r,
    int g,
    int b,
    int tolerance)
{
    const unsigned char *p =
        pixels + (size_t)y * (size_t)stride + (size_t)x * 3u;
    int dr = (int)p[0] - r;
    int dg = (int)p[1] - g;
    int db = (int)p[2] - b;
    if (dr < 0)
        dr = -dr;
    if (dg < 0)
        dg = -dg;
    if (db < 0)
        db = -db;
    return dr <= tolerance && dg <= tolerance && db <= tolerance;
}

static int test_validation_and_budget(void)
{
    unsigned char pixels[32] = {0};
    quantapdf_composer_raster raster = {0};
    quantapdf_composer_image_id id = UINT32_MAX;
    quantapdf_composer *composer = NULL;
    quantapdf_composer_options limits = {0};

    raster.struct_size = QUANTAPDF_COMPOSER_RASTER_V1_SIZE;
    raster.format = QUANTAPDF_COMPOSER_RASTER_RGBA32;
    raster.width = 2u;
    raster.height = 2u;
    raster.stride = 12u;
    raster.pixels = pixels;
    raster.size = 24u;

    CHECK(quantapdf_composer_add_raster(
              NULL, &raster, &id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);

    id = UINT32_MAX;
    CHECK(quantapdf_composer_add_raster(
              composer, NULL, &id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    raster.struct_size = 0u;
    id = UINT32_MAX;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);
    raster.struct_size = QUANTAPDF_COMPOSER_RASTER_V1_SIZE;

    raster.format = (quantapdf_composer_raster_format)99;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_ARGUMENT);
    raster.format = QUANTAPDF_COMPOSER_RASTER_RGBA32;

    raster.width = 0u;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_ARGUMENT);
    raster.width = 2u;
    raster.height = 0u;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_ARGUMENT);
    raster.height = 2u;

    raster.pixels = NULL;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_ARGUMENT);
    raster.pixels = pixels;

    raster.stride = 7u;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_ARGUMENT);
    raster.stride = 12u;

    raster.size = 19u;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_ARGUMENT);
    raster.size = 20u;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_OK);
    CHECK(id == 1u);

    quantapdf_drop_composer(composer);
    composer = NULL;

    limits.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    limits.max_resource_bytes = 15u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    id = UINT32_MAX;
    raster.stride = 12u;
    raster.size = 20u;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0u);
    quantapdf_drop_composer(composer);
    composer = NULL;

    limits.max_resource_bytes = 16u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    raster.stride = 1024u;
    raster.size = 1032u;
    {
        unsigned char *padded =
            (unsigned char *)calloc(raster.size, 1u);
        CHECK(padded != NULL);
        raster.pixels = padded;
        id = UINT32_MAX;
        CHECK(quantapdf_composer_add_raster(
                  composer, &raster, &id) == QUANTAPDF_OK);
        CHECK(id == 1u);
        free(padded);
    }
    quantapdf_drop_composer(composer);
    composer = NULL;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    raster.pixels = pixels;
    raster.width = UINT32_MAX;
    raster.height = UINT32_MAX;
    raster.stride = SIZE_MAX;
    raster.size = SIZE_MAX;
    id = UINT32_MAX;
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &id) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0u);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_formats_stride_alpha_and_determinism(void)
{
    unsigned char gray_source[8] = {
        0u, 255u, 99u, 99u,
        128u, 64u, 99u, 99u
    };
    unsigned char rgb_source[8] = {
        255u, 0u, 0u,
        0u, 255u, 0u,
        77u, 77u
    };
    unsigned char rgba_source[10] = {
        255u, 0u, 0u, 0u,
        0u, 0u, 255u, 255u,
        66u, 66u
    };
    quantapdf_composer_raster raster = {0};
    quantapdf_composer_image_options image_options = stretch_options();
    quantapdf_composer_image_id gray_id = 0u;
    quantapdf_composer_image_id rgb_id = 0u;
    quantapdf_composer_image_id rgba_id = 0u;
    quantapdf_composer *composer = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render = {0};
    quantapdf_image_info info = {0};
    quantapdf_rect gray_bounds = {10.0f, 10.0f, 50.0f, 50.0f};
    quantapdf_rect rgb_bounds = {60.0f, 10.0f, 100.0f, 30.0f};
    quantapdf_rect rgba_bounds = {60.0f, 50.0f, 100.0f, 70.0f};
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const unsigned char *pixels = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t pixel_size = 0u;
    size_t image_count = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;
    int xobject_components = 0;
    int has_smask = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, UINT32_C(0xffffffff), 0u));

    raster.struct_size = QUANTAPDF_COMPOSER_RASTER_V1_SIZE;
    raster.format = QUANTAPDF_COMPOSER_RASTER_GRAY8;
    raster.width = 2u;
    raster.height = 2u;
    raster.stride = 4u;
    raster.pixels = gray_source;
    raster.size = sizeof(gray_source);
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &gray_id) == QUANTAPDF_OK);
    CHECK(gray_id == 1u);

    raster.format = QUANTAPDF_COMPOSER_RASTER_RGB24;
    raster.width = 2u;
    raster.height = 1u;
    raster.stride = 8u;
    raster.pixels = rgb_source;
    raster.size = sizeof(rgb_source);
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &rgb_id) == QUANTAPDF_OK);
    CHECK(rgb_id == 2u);

    raster.format = QUANTAPDF_COMPOSER_RASTER_RGBA32;
    raster.width = 2u;
    raster.height = 1u;
    raster.stride = 10u;
    raster.pixels = rgba_source;
    raster.size = sizeof(rgba_source);
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &rgba_id) == QUANTAPDF_OK);
    CHECK(rgba_id == 3u);

    memset(gray_source, 200, sizeof(gray_source));
    memset(rgb_source, 200, sizeof(rgb_source));
    memset(rgba_source, 200, sizeof(rgba_source));

    CHECK(quantapdf_composer_draw_image(
              composer, 0u, gray_id, &gray_bounds, &image_options) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_image(
              composer, 0u, rgb_id, &rgb_bounds, &image_options) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_image(
              composer, 0u, rgba_id, &rgba_bounds, &image_options) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_image_xobject_info(
              first_data, first_size, 0u, gray_id,
              &xobject_components, &has_smask));
    CHECK(xobject_components == 1 && has_smask == 0);
    CHECK(quantapdf_test_pdf_image_xobject_info(
              first_data, first_size, 0u, rgb_id,
              &xobject_components, &has_smask));
    CHECK(xobject_components == 3 && has_smask == 0);
    CHECK(quantapdf_test_pdf_image_xobject_info(
              first_data, first_size, 0u, rgba_id,
              &xobject_components, &has_smask));
    CHECK(xobject_components == 3 && has_smask == 1);

    CHECK(quantapdf_output_save_file(first, COMPOSER_RASTER_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_RASTER_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);

    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(quantapdf_image_count(images, &image_count) == QUANTAPDF_OK);
    CHECK(image_count == 3u);
    info.struct_size = sizeof(info);
    CHECK(quantapdf_image_get_info(images, 0u, &info) == QUANTAPDF_OK);
    CHECK(info.pixel_width == 2 && info.pixel_height == 2);
    CHECK(info.components == 1);
    CHECK(info.has_alpha == 0);
    info.struct_size = sizeof(info);
    CHECK(quantapdf_image_get_info(images, 2u, &info) == QUANTAPDF_OK);
    CHECK(info.pixel_width == 2 && info.pixel_height == 1);
    CHECK(info.has_alpha == 1);

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 120 && height == 100 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 20, 20, 0, 0, 0, 20));
    CHECK(pixel_near(pixels, stride, 40, 20, 255, 255, 255, 20));
    CHECK(pixel_near(pixels, stride, 70, 20, 255, 0, 0, 25));
    CHECK(pixel_near(pixels, stride, 90, 20, 0, 255, 0, 25));
    CHECK(pixel_near(pixels, stride, 70, 60, 255, 255, 255, 20));
    CHECK(pixel_near(pixels, stride, 90, 60, 0, 0, 255, 25));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_image_page(images);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_validation_and_budget() == 0);
    CHECK(test_formats_stride_alpha_and_determinism() == 0);
    return 0;
}
