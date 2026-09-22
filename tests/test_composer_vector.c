#include <quantapdf/quantapdf.h>

#include "composer_test_helpers.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static quantapdf_composer_path_options path_options(void)
{
    quantapdf_composer_path_options options = {0};

    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    options.stroke = 1;
    options.fill = 0;
    options.stroke_argb = UINT32_C(0xff000000);
    options.fill_argb = UINT32_C(0xffffffff);
    options.stroke_width = 1.0f;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    options.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    options.miter_limit = 10.0f;
    return options;
}

static int add_page(quantapdf_composer *composer)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 200.0f;
    page.height_points = 200.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(composer, &page, &page_index) ==
            QUANTAPDF_OK &&
        page_index == 0u;
}

static int test_path_validation(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_options composer_options = {0};
    quantapdf_composer_path_options options = path_options();
    quantapdf_composer_path_command commands[2] = {0};

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    commands[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[0].point1.x = 10.0f;
    commands[0].point1.y = 10.0f;
    commands[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1.x = 20.0f;
    commands[1].point1.y = 20.0f;

    CHECK(quantapdf_composer_draw_path(
              NULL, 0u, commands, 2u, &options) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_path(
              composer, 1u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, NULL, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 0u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, SIZE_MAX, &options) ==
          QUANTAPDF_ERROR_UNSUPPORTED);

    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE - 1u;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options = path_options();
    options.stroke = 0;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options = path_options();
    options.stroke_width = -1.0f;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options = path_options();
    options.miter_limit = 0.5f;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options = path_options();
    options.stroke_argb = UINT32_C(0x80000000);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);

    options = path_options();
    commands[0].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    commands[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1.x = NAN;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);

    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_resource_bytes = sizeof(commands) - 1u;
    CHECK(quantapdf_composer_create(&composer_options, &composer) ==
          QUANTAPDF_OK);
    CHECK(add_page(composer));
    commands[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1.x = 20.0f;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_drop_composer(composer);
    return 0;
}

static int pixel_is_red(
    const unsigned char *pixels,
    int stride,
    int x,
    int y)
{
    const unsigned char *pixel =
        pixels + (size_t)y * (size_t)stride + (size_t)x * 3u;
    return pixel[0] > 180u && pixel[1] < 100u && pixel[2] < 100u;
}

static int test_path_content_render_and_determinism(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render = {0};
    quantapdf_composer_path_options options = path_options();
    quantapdf_composer_path_command rect[5] = {0};
    quantapdf_composer_path_command curve[2] = {0};
    quantapdf_composer_path_command triangle[4] = {0};
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const unsigned char *pixels = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    rect[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    rect[0].point1 = (quantapdf_point){20.0f, 20.0f};
    rect[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    rect[1].point1 = (quantapdf_point){100.0f, 20.0f};
    rect[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    rect[2].point1 = (quantapdf_point){100.0f, 100.0f};
    rect[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    rect[3].point1 = (quantapdf_point){20.0f, 100.0f};
    rect[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    options.stroke = 1;
    options.fill = 1;
    options.stroke_argb = UINT32_C(0xff0000ff);
    options.fill_argb = UINT32_C(0xffff0000);
    options.stroke_width = 2.0f;
    options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    options.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &options) == QUANTAPDF_OK);

    curve[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    curve[0].point1 = (quantapdf_point){120.0f, 20.0f};
    curve[1].kind = QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    curve[1].point1 = (quantapdf_point){180.0f, 20.0f};
    curve[1].point2 = (quantapdf_point){180.0f, 100.0f};
    curve[1].point3 = (quantapdf_point){120.0f, 100.0f};
    options = path_options();
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, curve, 2u, &options) == QUANTAPDF_OK);

    triangle[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    triangle[0].point1 = (quantapdf_point){120.0f, 120.0f};
    triangle[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    triangle[1].point1 = (quantapdf_point){180.0f, 120.0f};
    triangle[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    triangle[2].point1 = (quantapdf_point){150.0f, 180.0f};
    triangle[3].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    options = (quantapdf_composer_path_options){0};
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    options.fill = 1;
    options.fill_argb = UINT32_C(0xff00ff00);
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, triangle, 4u, &options) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "20 180 m 100 180 l 100 100 l 20 100 l h B"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u,
        "120 180 m 180 180 180 100 120 100 c S"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u,
        "120 80 m 180 80 l 150 20 l h f*"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u,
        "2 w 1 J 2 j 10 M 1 0 0 rg"));

    CHECK(quantapdf_output_save_file(first, COMPOSER_VECTOR_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(COMPOSER_VECTOR_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 200 && height == 200 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);
    CHECK(pixel_is_red(pixels, stride, 50, 50));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_path_validation() == 0);
    CHECK(test_path_content_render_and_determinism() == 0);
    return 0;
}
