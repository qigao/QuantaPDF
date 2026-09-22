#include <quantapdf/quantapdf.h>

#include "composer_test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int add_page(quantapdf_composer *composer, size_t expected)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 200.0f;
    page.height_points = 200.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(composer, &page, &page_index) ==
            QUANTAPDF_OK &&
        page_index == expected;
}

static quantapdf_barcode_options black_options(void)
{
    quantapdf_barcode_options options = {0};
    options.struct_size = QUANTAPDF_BARCODE_OPTIONS_V1_SIZE;
    options.argb = UINT32_C(0xff000000);
    return options;
}

static int render_page(
    quantapdf_document *document,
    int page_index,
    quantapdf_bitmap **out_bitmap,
    const unsigned char **out_pixels,
    int *out_stride)
{
    quantapdf_page *page = NULL;
    quantapdf_render_options options = {0};
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
    return width == 200 && height == 200 && components == 3 &&
        size == (size_t)*out_stride * (size_t)height;
}

static int pixel_is_white(
    const unsigned char *pixels,
    int stride,
    int x,
    int y)
{
    const unsigned char *p =
        pixels + (size_t)y * (size_t)stride + (size_t)x * 3u;
    return p[0] > 245u && p[1] > 245u && p[2] > 245u;
}

static int pixel_is_black(
    const unsigned char *pixels,
    int stride,
    int x,
    int y)
{
    const unsigned char *p =
        pixels + (size_t)y * (size_t)stride + (size_t)x * 3u;
    return p[0] < 40u && p[1] < 40u && p[2] < 40u;
}

static int test_validation(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_barcode_options options = black_options();
    quantapdf_rect bounds = {10.0f, 10.0f, 110.0f, 50.0f};
    char bad_utf8[] = {(char)0xc0, '\0'};
    char long_qr[108];

    memset(long_qr, 'A', sizeof(long_qr));
    long_qr[107] = '\0';

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 0u));

    CHECK(quantapdf_composer_draw_barcode(
              NULL, 0u, QUANTAPDF_BARCODE_CODE_128B,
              "A", &bounds, &options) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, (quantapdf_barcode_kind)99,
              "A", &bounds, &options) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_CODE_128B,
              "", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);

    options.struct_size = QUANTAPDF_BARCODE_OPTIONS_V1_MIN_SIZE - 1u;
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_CODE_128B,
              "A", &bounds, &options) == QUANTAPDF_ERROR_ARGUMENT);
    options = black_options();
    options.argb = UINT32_C(0x80000000);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_CODE_128B,
              "A", &bounds, &options) == QUANTAPDF_ERROR_ARGUMENT);

    options = black_options();
    bounds.x1 = bounds.x0;
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_CODE_128B,
              "A", &bounds, &options) == QUANTAPDF_ERROR_ARGUMENT);
    bounds = (quantapdf_rect){10.0f, 10.0f, 110.0f, 50.0f};

    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_CODE_128B,
              "\n", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_CODE_39,
              "lower", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_CODE_39,
              "*", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_EAN_13,
              "4003994155480", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_UPC_A,
              "003994155481", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_EAN_8,
              "95012340", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_UPC_E,
              "01234550", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_UPC_E,
              "21234558", &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_QR,
              bad_utf8, &bounds, &options) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_QR,
              long_qr, &bounds, &options) == QUANTAPDF_ERROR_UNSUPPORTED);

    CHECK(quantapdf_composer_draw_barcode(
              composer, 1u, QUANTAPDF_BARCODE_CODE_39,
              "A", &bounds, &options) == QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_generation(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_barcode_options options = black_options();
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_bitmap *bitmap = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const unsigned char *pixels = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    int stride = 0;
    int page_count = 0;
    size_t page;

    const quantapdf_rect code128_bounds = {10.0f, 10.0f, 76.0f, 40.0f};
    const quantapdf_rect code39_bounds = {10.0f, 10.0f, 77.0f, 40.0f};
    const quantapdf_rect ean13_bounds = {10.0f, 50.0f, 123.0f, 90.0f};
    const quantapdf_rect upca_bounds = {10.0f, 50.0f, 123.0f, 90.0f};
    const quantapdf_rect ean8_bounds = {10.0f, 50.0f, 91.0f, 90.0f};
    const quantapdf_rect upce_bounds = {10.0f, 50.0f, 77.0f, 90.0f};
    const quantapdf_rect qr_bounds = {100.0f, 100.0f, 129.0f, 129.0f};

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    for (page = 0u; page < 7u; ++page)
        CHECK(add_page(composer, page));

    CHECK(quantapdf_composer_draw_barcode(
              composer, 0u, QUANTAPDF_BARCODE_CODE_128B,
              "A", &code128_bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 1u, QUANTAPDF_BARCODE_CODE_39,
              "A", &code39_bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 2u, QUANTAPDF_BARCODE_EAN_13,
              "4003994155486", &ean13_bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 3u, QUANTAPDF_BARCODE_UPC_A,
              "003994155480", &upca_bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 4u, QUANTAPDF_BARCODE_EAN_8,
              "95012346", &ean8_bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 5u, QUANTAPDF_BARCODE_UPC_E,
              "01234558", &upce_bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_barcode(
              composer, 6u, QUANTAPDF_BARCODE_QR,
              "A", &qr_bounds, &options) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u,
        "20 190 m 22 190 l 22 160 l 20 160 l h"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 2u,
        "21 150 m 22 150 l 22 110 l 21 110 l h"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 5u,
        "19 150 m 20 150 l 20 110 l 19 110 l h"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 6u,
        "104 96 m 111 96 l 111 95 l 104 95 l h"));

    CHECK(quantapdf_output_save_file(first, COMPOSER_BARCODE_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(COMPOSER_BARCODE_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 7);

    CHECK(render_page(document, 0, &bitmap, &pixels, &stride));
    CHECK(pixel_is_white(pixels, stride, 15, 20));
    CHECK(pixel_is_black(pixels, stride, 21, 20));
    quantapdf_drop_bitmap(bitmap);
    bitmap = NULL;

    CHECK(render_page(document, 6, &bitmap, &pixels, &stride));
    CHECK(pixel_is_white(pixels, stride, 101, 101));
    CHECK(pixel_is_black(pixels, stride, 105, 104));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_validation() == 0);
    CHECK(test_generation() == 0);
    return 0;
}
