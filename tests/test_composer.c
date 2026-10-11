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

static quantapdf_composer *composer_sentinel(void)
{
    return (quantapdf_composer *)(uintptr_t)1;
}

static int pixel_is_white(
    const unsigned char *pixels,
    int stride,
    int x,
    int y)
{
    const unsigned char *pixel =
        pixels + (size_t)y * (size_t)stride + (size_t)x * 3u;
    return pixel[0] > 245u && pixel[1] > 245u && pixel[2] > 245u;
}

static size_t count_blue_pixels(
    const unsigned char *pixels,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1)
{
    size_t count = 0u;
    int x;
    int y;

    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            const unsigned char *pixel =
                pixels + (size_t)y * (size_t)stride + (size_t)x * 3u;
            if (pixel[2] > 80u && pixel[2] > pixel[0] * 2u &&
                pixel[2] > pixel[1] + 20u)
                ++count;
        }
    }
    return count;
}

static int render_page_pixels(
    quantapdf_document *document,
    int page_index,
    quantapdf_bitmap **out_bitmap,
    const unsigned char **out_pixels,
    int *out_stride)
{
    quantapdf_render_options options = {0};
    quantapdf_page *page = NULL;
    size_t size = 0u;
    int width = 0;
    int height = 0;
    int components = 0;

    *out_bitmap = NULL;
    *out_pixels = NULL;
    *out_stride = 0;
    options.struct_size = sizeof(options);
    options.dpi = 72.0f;
    if (quantapdf_load_page(document, page_index, &page) != QUANTAPDF_OK ||
        quantapdf_render_page_with_options(page, &options, out_bitmap) !=
            QUANTAPDF_OK ||
        quantapdf_bitmap_dimensions(
            *out_bitmap, &width, &height, out_stride, &components) !=
            QUANTAPDF_OK ||
        quantapdf_bitmap_data(*out_bitmap, out_pixels, &size) != QUANTAPDF_OK) {
        quantapdf_drop_bitmap(*out_bitmap);
        *out_bitmap = NULL;
        quantapdf_drop_page(page);
        return 0;
    }
    quantapdf_drop_page(page);
    return width == 200 && height == 400 && components == 3 &&
        size == (size_t)*out_stride * (size_t)height;
}

static int test_create_contract(void)
{
    quantapdf_composer *composer = composer_sentinel();
    quantapdf_composer_options options = {0};

    CHECK(quantapdf_composer_create(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(composer != NULL && composer != composer_sentinel());
    quantapdf_drop_composer(composer);
    quantapdf_drop_composer(NULL);

    options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_MIN_SIZE - 1u;
    composer = composer_sentinel();
    CHECK(quantapdf_composer_create(&options, &composer) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(composer == NULL);
    return 0;
}

static int test_page_validation_and_capacity(void)
{
    quantapdf_composer_options options = {0};
    quantapdf_composer_page_options page = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t output_size = 0u;
    size_t page_index = SIZE_MAX;

    options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    options.max_pages = 1u;
    CHECK(quantapdf_composer_create(&options, &composer) == QUANTAPDF_OK);

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 612.0f;
    page.height_points = 792.0f;
    page.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_OK);
    CHECK(page_index == 0u);

    page_index = 19u;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(page_index == SIZE_MAX);

    page.width_points = 0.0f;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_ARGUMENT);
    page.width_points = 612.0f;
    page.height_points = -1.0f;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);

    composer = NULL;
    page.width_points = 0.000001f;
    page.height_points = 0.000001f;
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &output_size) == QUANTAPDF_OK);
    CHECK(quantapdf_test_pdf_page_size_positive(
        data, output_size, 0u));
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_finish_text_document(void)
{
    static const char expected[] = "Hello copied text";
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_text_options text_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {36.0f, 48.0f, 360.0f, 120.0f};
    quantapdf_rect actual_bounds = {0};
    char text_input[] = "Hello copied text";
    char *extracted = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t extracted_size = 0u;
    size_t page_index = SIZE_MAX;
    int page_count = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 612.0f;
    page_options.height_points = 792.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);

    text_options.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    text_options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD;
    text_options.font_size = 18.0f;
    text_options.argb = UINT32_C(0xff204080);
    text_options.line_height_multiplier = 1.2f;
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text_options.wrap = 1;
    CHECK(quantapdf_composer_draw_text(
              composer, page_index, text_input, &bounds, &text_options) ==
          QUANTAPDF_OK);
    memset(text_input, 'X', sizeof(text_input) - 1u);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size > 8u);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);
    CHECK(memcmp(first_data, "%PDF-", 5u) == 0);
    CHECK(quantapdf_output_save_file(first, COMPOSER_OUTPUT_PDF) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_open(COMPOSER_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 1);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(page, &actual_bounds) == QUANTAPDF_OK);
    CHECK(actual_bounds.x1 == 612.0f);
    CHECK(actual_bounds.y1 == 792.0f);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(extracted_size >= sizeof(expected) - 1u);
    CHECK(memcmp(extracted, expected, sizeof(expected) - 1u) == 0);

    quantapdf_free(extracted);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_jpeg_resource_and_placement(void)
{
    static const unsigned char invalid_image[] = {0xffu, 0xd8u, 0xffu};
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_image_options image_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_composer_image_id image_id = UINT32_MAX;
    quantapdf_image_info info = {0};
    quantapdf_rect bounds = {36.0f, 200.0f, 236.0f, 300.0f};
    unsigned char *jpeg = NULL;
    size_t jpeg_size = 0u;
    size_t page_index = SIZE_MAX;
    size_t image_count = 0u;

    CHECK(quantapdf_test_make_jpeg(&jpeg, &jpeg_size));
    CHECK(jpeg != NULL && jpeg_size > 4u);
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 300.0f;
    page_options.height_points = 400.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_add_image(
              composer, invalid_image, sizeof(invalid_image), &image_id) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(image_id == 0u);
    CHECK(quantapdf_composer_add_image(
              composer, jpeg, jpeg_size, &image_id) == QUANTAPDF_OK);
    CHECK(image_id == 1u);
    memset(jpeg, 0, jpeg_size);
    free(jpeg);

    image_options.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_CONTAIN;
    CHECK(quantapdf_composer_draw_image(
              composer, page_index, image_id, &bounds, &image_options) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(output, COMPOSER_OUTPUT_PDF) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_open(COMPOSER_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(quantapdf_image_count(images, &image_count) == QUANTAPDF_OK);
    CHECK(image_count == 1u);
    info.struct_size = sizeof(info);
    CHECK(quantapdf_image_get_info(images, 0u, &info) == QUANTAPDF_OK);
    CHECK(info.pixel_width == 8);
    CHECK(info.pixel_height == 4);
    CHECK(info.components == 3);
    CHECK(info.quad.ul.x == 36.0f);
    CHECK(info.quad.ul.y == 200.0f);
    CHECK(info.quad.lr.x == 236.0f);
    CHECK(info.quad.lr.y == 300.0f);

    quantapdf_drop_image_page(images);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_png_and_alpha(void)
{
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_image_options image_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render_options = {0};
    quantapdf_composer_image_id opaque_id = 0u;
    quantapdf_composer_image_id alpha_id = 0u;
    quantapdf_composer_image_id corrupt_id = UINT32_MAX;
    quantapdf_image_info info = {0};
    quantapdf_rect opaque_bounds = {10.0f, 10.0f, 50.0f, 30.0f};
    quantapdf_rect alpha_bounds = {10.0f, 40.0f, 50.0f, 60.0f};
    unsigned char *opaque_png = NULL;
    unsigned char *alpha_png = NULL;
    const unsigned char *pdf_data = NULL;
    const unsigned char *pixels = NULL;
    size_t opaque_size = 0u;
    size_t alpha_size = 0u;
    size_t page_index = SIZE_MAX;
    size_t image_count = 0u;
    size_t pixel_size = 0u;
    size_t pdf_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_test_make_png(0, &opaque_png, &opaque_size));
    CHECK(quantapdf_test_make_png(1, &alpha_png, &alpha_size));
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 100.0f;
    page_options.height_points = 100.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(
              composer, opaque_png, opaque_size, &opaque_id) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(
              composer, alpha_png, alpha_size, &alpha_id) == QUANTAPDF_OK);
    alpha_png[alpha_size - 1u] ^= 1u;
    CHECK(quantapdf_composer_add_image(
              composer, alpha_png, alpha_size, &corrupt_id) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(corrupt_id == 0u);
    free(opaque_png);
    free(alpha_png);
    image_options.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    CHECK(quantapdf_composer_draw_image(
              composer, page_index, opaque_id, &opaque_bounds,
              &image_options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_image(
              composer, page_index, alpha_id, &alpha_bounds,
              &image_options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &pdf_data, &pdf_size) == QUANTAPDF_OK);
    CHECK(pdf_size > 8u && memcmp(pdf_data, "%PDF-1.4", 8u) == 0);
    CHECK(quantapdf_output_save_file(output, COMPOSER_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(COMPOSER_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(quantapdf_image_count(images, &image_count) == QUANTAPDF_OK);
    CHECK(image_count == 2u);
    info.struct_size = sizeof(info);
    CHECK(quantapdf_image_get_info(images, 1u, &info) == QUANTAPDF_OK);
    CHECK(info.pixel_width == 2);
    CHECK(info.pixel_height == 1);
    CHECK(info.has_alpha == 1);

    render_options.struct_size = sizeof(render_options);
    render_options.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(
              page, &render_options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 100 && height == 100 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);
    CHECK(pixels[50u * (size_t)stride + 15u * 3u] > 245u);
    CHECK(pixels[50u * (size_t)stride + 15u * 3u + 1u] > 245u);
    CHECK(pixels[50u * (size_t)stride + 15u * 3u + 2u] > 245u);
    CHECK(pixels[50u * (size_t)stride + 45u * 3u] < 80u);
    CHECK(pixels[50u * (size_t)stride + 45u * 3u + 2u] > 180u);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_image_page(images);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_limits_state_and_output_isolation(void)
{
    static const char truncated_utf8[] = {(char)0xe2, '\0'};
    static const char unmapped_winansi_gap[] = {
        (char)0xe2, (char)0x80, (char)0x95, '\0'};
    quantapdf_composer_options limits = {0};
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_text_options text_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *first = (quantapdf_output *)(uintptr_t)1;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_rect text_bounds = {10.0f, 10.0f, 90.0f, 30.0f};
    size_t page_index = SIZE_MAX;
    int page_count = 0;

    CHECK(quantapdf_composer_finish(NULL, &first) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(first == NULL);
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    first = (quantapdf_output *)(uintptr_t)1;
    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_ERROR_STATE);
    CHECK(first == NULL);
    quantapdf_drop_composer(composer);

    limits.struct_size = sizeof(limits);
    limits.max_operations = 1u;
    limits.max_resource_bytes = 5u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    page_options.struct_size = sizeof(page_options);
    page_options.width_points = 100.0f;
    page_options.height_points = 100.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);
    text_options.struct_size = sizeof(text_options);
    text_options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    text_options.font_size = 10.0f;
    text_options.argb = UINT32_C(0xff000000);
    text_options.line_height_multiplier = 1.0f;
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text_options.wrap = 0;
    CHECK(quantapdf_composer_draw_text(
              composer, page_index, truncated_utf8, &text_bounds,
              &text_options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_text(
              composer, page_index, unmapped_winansi_gap, &text_bounds,
              &text_options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_text(
              composer, page_index, "four", &text_bounds, &text_options) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_text(
              composer, page_index, "", &text_bounds, &text_options) ==
          QUANTAPDF_ERROR_UNSUPPORTED);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);
    CHECK(page_index == 1u);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(first, COMPOSER_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(second, COMPOSER_OUTPUT_SECOND_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(COMPOSER_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 1);
    quantapdf_close(document);
    document = NULL;
    CHECK(quantapdf_open(COMPOSER_OUTPUT_SECOND_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 2);

    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_encoded_image_reuse(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_image_id jpeg_id = 0, png_id = 0, repeated_id = 0;
    unsigned char *jpeg = NULL, *png = NULL;
    size_t jpeg_size = 0, png_size = 0;

    CHECK(quantapdf_test_make_jpeg(&jpeg, &jpeg_size));
    CHECK(quantapdf_test_make_png(1, &png, &png_size));
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(composer, jpeg, jpeg_size, &jpeg_id) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(composer, jpeg, jpeg_size, &repeated_id) ==
          QUANTAPDF_OK);
    CHECK(jpeg_id != 0 && repeated_id == jpeg_id);
    CHECK(quantapdf_composer_add_image(composer, png, png_size, &png_id) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(composer, png, png_size, &repeated_id) ==
          QUANTAPDF_OK);
    CHECK(png_id != 0 && png_id != jpeg_id && repeated_id == png_id);
    free(png);
    free(jpeg);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_decoder_resource_and_format_guards(void)
{
    quantapdf_composer_options limits = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_composer_image_id image_id = UINT32_MAX;
    unsigned char *jpeg = NULL;
    unsigned char *truncated = NULL;
    unsigned char *huge_progressive = NULL;
    unsigned char *oversized_png = NULL;
    size_t jpeg_size = 0u;
    size_t truncated_size = 0u;
    size_t huge_progressive_size = 0u;
    size_t oversized_size = 0u;

    CHECK(quantapdf_test_make_jpeg(&jpeg, &jpeg_size));
    CHECK(quantapdf_test_make_truncated_jpeg(
        jpeg, jpeg_size, &truncated, &truncated_size));
    CHECK(quantapdf_test_make_huge_progressive_jpeg(
        jpeg, jpeg_size, &huge_progressive, &huge_progressive_size));
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(
              composer, truncated, truncated_size, &image_id) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(image_id == 0u);
    quantapdf_drop_composer(composer);
    composer = NULL;

    limits.struct_size = sizeof(limits);
    limits.max_resource_bytes = jpeg_size + 8u * 4u * 3u - 1u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(
              composer, jpeg, jpeg_size, &image_id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(quantapdf_composer_add_image(
              composer, huge_progressive, huge_progressive_size, &image_id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_drop_composer(composer);
    composer = NULL;

    CHECK(quantapdf_test_make_oversized_png(
        &oversized_png, &oversized_size));
    limits.max_resource_bytes = 1024u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(
              composer, oversized_png, oversized_size, &image_id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    {
        int variant;
        for (variant = 0; variant <= 6; ++variant) {
            unsigned char *malformed = NULL;
            size_t malformed_size = 0u;
            CHECK(quantapdf_test_make_malformed_png(
                variant, &malformed, &malformed_size));
            CHECK(quantapdf_composer_add_image(
                      composer, malformed, malformed_size, &image_id) ==
                  QUANTAPDF_ERROR_FORMAT);
            CHECK(image_id == 0u);
            free(malformed);
        }
    }

    quantapdf_drop_composer(composer);
    free(oversized_png);
    free(huge_progressive);
    free(truncated);
    free(jpeg);
    return 0;
}

static int test_layout_order_fit_and_locale(void)
{
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_text_options text_options = {0};
    quantapdf_composer_image_options image_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *document = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_composer_image_id image_id = 0u;
    quantapdf_rect text_bounds = {0.0f, 0.0f, 100.0f, 20.0f};
    quantapdf_rect wrap_bounds = {0.0f, 30.0f, 18.0f, 80.0f};
    quantapdf_rect overlap = {20.0f, 100.0f, 120.0f, 150.0f};
    quantapdf_rect contain = {10.0f, 10.0f, 110.0f, 110.0f};
    quantapdf_rect cover = {10.0f, 120.0f, 110.0f, 220.0f};
    quantapdf_rect stretch = {10.0f, 230.0f, 110.0f, 330.0f};
    unsigned char *png = NULL;
    const unsigned char *data = NULL;
    const unsigned char *pixels = NULL;
    size_t png_size = 0u;
    size_t output_size = 0u;
    size_t page = SIZE_MAX;
    quantapdf_status finish_status;
    int stride = 0;

    CHECK(quantapdf_test_make_png(0, &png, &png_size));
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    page_options.struct_size = sizeof(page_options);
    page_options.width_points = 200.0f;
    page_options.height_points = 400.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page) == QUANTAPDF_OK);
    CHECK(page == 0u);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page) == QUANTAPDF_OK);
    CHECK(page == 1u);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page) == QUANTAPDF_OK);
    CHECK(page == 2u);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page) == QUANTAPDF_OK);
    CHECK(page == 3u);
    CHECK(quantapdf_composer_add_image(
              composer, png, png_size, &image_id) == QUANTAPDF_OK);
    free(png);

    text_options.struct_size = sizeof(text_options);
    text_options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    text_options.font_size = 10.0f;
    text_options.argb = UINT32_C(0xff204080);
    text_options.line_height_multiplier = 1.0f;
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER;
    text_options.wrap = 0;
    CHECK(quantapdf_composer_draw_text(
              composer, 0u, "WWW", &text_bounds, &text_options) ==
          QUANTAPDF_OK);
    text_bounds.y0 = 20.0f;
    text_bounds.y1 = 40.0f;
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT;
    CHECK(quantapdf_composer_draw_text(
              composer, 0u, "WWW", &text_bounds, &text_options) ==
          QUANTAPDF_OK);
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text_options.wrap = 1;
    CHECK(quantapdf_composer_draw_text(
              composer, 0u, "WW", &wrap_bounds, &text_options) ==
          QUANTAPDF_OK);

    image_options.struct_size = sizeof(image_options);
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    CHECK(quantapdf_composer_draw_image(
              composer, 1u, image_id, &overlap, &image_options) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_text(
              composer, 1u, "Z", &overlap, &text_options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_text(
              composer, 2u, "Z", &overlap, &text_options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_image(
              composer, 2u, image_id, &overlap, &image_options) ==
          QUANTAPDF_OK);

    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_CONTAIN;
    CHECK(quantapdf_composer_draw_image(
              composer, 3u, image_id, &contain, &image_options) ==
          QUANTAPDF_OK);
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_COVER;
    CHECK(quantapdf_composer_draw_image(
              composer, 3u, image_id, &cover, &image_options) ==
          QUANTAPDF_OK);
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    CHECK(quantapdf_composer_draw_image(
              composer, 3u, image_id, &stretch, &image_options) ==
          QUANTAPDF_OK);

    quantapdf_test_use_comma_locale(1);
    finish_status = quantapdf_composer_finish(composer, &output);
    quantapdf_test_use_comma_locale(0);
    CHECK(finish_status == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &output_size) == QUANTAPDF_OK);
    CHECK(output_size > 8u && memcmp(data, "%PDF-1.4", 8u) == 0);
    CHECK(quantapdf_test_pdf_content_contains(
        data, output_size, 0u, "35.84 390 Tm (WWW) Tj"));
    CHECK(quantapdf_test_pdf_content_contains(
        data, output_size, 0u, "71.68 370 Tm (WWW) Tj"));
    CHECK(quantapdf_test_pdf_content_contains(
        data, output_size, 0u, "0.1255 0.251 0.502 rg"));
    CHECK(quantapdf_test_pdf_content_count(
              data, output_size, 0u, "(W) Tj ET") == 2u);
    CHECK(quantapdf_test_pdf_content_order(
        data, output_size, 1u, "/Im1 Do", "(Z) Tj"));
    CHECK(quantapdf_test_pdf_content_order(
        data, output_size, 2u, "(Z) Tj", "/Im1 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        data, output_size, 3u, "100 0 0 50 10 315 cm /Im1 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        data, output_size, 3u,
        "10 180 100 100 re W n 200 0 0 100 -40 180 cm /Im1 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        data, output_size, 3u, "100 0 0 100 10 70 cm /Im1 Do"));
    CHECK(quantapdf_test_pdf_content_count(
              data, output_size, 3u, "/Im1 Do") == 3u);

    CHECK(quantapdf_output_save_file(output, COMPOSER_OUTPUT_SECOND_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(COMPOSER_OUTPUT_SECOND_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(render_page_pixels(document, 0, &bitmap, &pixels, &stride));
    CHECK(count_blue_pixels(pixels, stride, 0, 0, 110, 90) > 10u);
    quantapdf_drop_bitmap(bitmap);
    bitmap = NULL;
    CHECK(render_page_pixels(document, 1, &bitmap, &pixels, &stride));
    CHECK(count_blue_pixels(pixels, stride, 20, 100, 120, 150) > 0u);
    quantapdf_drop_bitmap(bitmap);
    bitmap = NULL;
    CHECK(render_page_pixels(document, 2, &bitmap, &pixels, &stride));
    CHECK(count_blue_pixels(pixels, stride, 20, 100, 120, 150) == 0u);
    quantapdf_drop_bitmap(bitmap);
    bitmap = NULL;
    CHECK(render_page_pixels(document, 3, &bitmap, &pixels, &stride));
    CHECK(pixel_is_white(pixels, stride, 20, 20));
    CHECK(!pixel_is_white(pixels, stride, 20, 50));
    CHECK(pixel_is_white(pixels, stride, 5, 170));
    CHECK(!pixel_is_white(pixels, stride, 20, 170));
    CHECK(!pixel_is_white(pixels, stride, 20, 250));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_close(document);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_create_contract() == 0);
    CHECK(test_page_validation_and_capacity() == 0);
    CHECK(test_finish_text_document() == 0);
    CHECK(test_jpeg_resource_and_placement() == 0);
    CHECK(test_png_and_alpha() == 0);
    CHECK(test_limits_state_and_output_isolation() == 0);
    CHECK(test_encoded_image_reuse() == 0);
    CHECK(test_decoder_resource_and_format_guards() == 0);
    CHECK(test_layout_order_fit_and_locale() == 0);
    return 0;
}
