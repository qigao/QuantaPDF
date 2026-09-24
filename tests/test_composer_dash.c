#include <quantapdf/quantapdf.h>

#include "composer_test_helpers.h"

#include <math.h>
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

static int add_page(quantapdf_composer *composer)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 220.0f;
    page.height_points = 180.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(composer, &page, &page_index) ==
            QUANTAPDF_OK &&
        page_index == 0u;
}

static quantapdf_composer_path_options stroke_options(void)
{
    quantapdf_composer_path_options options = {0};
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    options.stroke = 1;
    options.stroke_argb = UINT32_C(0xff000000);
    options.stroke_width = 3.0f;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    options.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    options.miter_limit = 10.0f;
    return options;
}

static void line_commands(
    quantapdf_composer_path_command commands[2],
    float y)
{
    memset(commands, 0, sizeof(*commands) * 2u);
    commands[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[0].point1 = (quantapdf_point){20.0f, y};
    commands[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1 = (quantapdf_point){200.0f, y};
}

static int pixel_is_dark(
    const unsigned char *pixels,
    int stride,
    int x,
    int y)
{
    const unsigned char *p =
        pixels + (size_t)y * (size_t)stride + (size_t)x * 3u;
    return p[0] < 100u && p[1] < 100u && p[2] < 100u;
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

static int test_validation(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_path_options options = stroke_options();
    quantapdf_composer_path_command commands[2];
    quantapdf_composer_dash_pattern dash = {0};
    float valid[] = {8.0f, 0.0f, 4.0f};
    float all_zero[] = {0.0f, 0.0f};
    float negative[] = {4.0f, -1.0f};
    float nan_values[] = {4.0f, NAN};
    float too_many[QUANTAPDF_COMPOSER_MAX_DASH_COUNT + 1u] = {0};

    line_commands(commands, 40.0f);
    dash.struct_size = QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE;
    dash.lengths = valid;
    dash.length_count = 3u;
    dash.phase = 0.0f;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    dash.struct_size = 0u;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);
    dash.struct_size = QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE;

    dash.lengths = NULL;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);

    dash.lengths = valid;
    dash.length_count = 0u;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);

    too_many[0] = 1.0f;
    dash.lengths = too_many;
    dash.length_count = QUANTAPDF_COMPOSER_MAX_DASH_COUNT + 1u;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);

    dash.lengths = all_zero;
    dash.length_count = 2u;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);

    dash.lengths = negative;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);

    dash.lengths = nan_values;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);

    dash.lengths = valid;
    dash.length_count = 3u;
    dash.phase = -1.0f;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);
    dash.phase = NAN;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);

    dash.phase = 0.0f;
    options.stroke = 0;
    options.fill = 1;
    options.fill_argb = UINT32_C(0xff000000);
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_ARGUMENT);

    options = stroke_options();
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_OK);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_resource_and_operation_atomicity(void)
{
    quantapdf_composer_options composer_options = {0};
    quantapdf_composer_path_options options = stroke_options();
    quantapdf_composer_path_command commands[2];
    quantapdf_composer_dash_pattern dash = {0};
    float lengths[] = {10.0f, 5.0f};
    size_t required =
        sizeof(commands) + sizeof(lengths);
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0u;

    line_commands(commands, 40.0f);
    dash.struct_size = QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE;
    dash.lengths = lengths;
    dash.length_count = 2u;
    dash.phase = 0.0f;

    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_resource_bytes = required - 1u;
    CHECK(quantapdf_composer_create(&composer_options, &composer) ==
          QUANTAPDF_OK);
    CHECK(add_page(composer));
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_test_pdf_content_count(
              data, size, 0u, " d ") == 0u);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    output = NULL;
    composer = NULL;

    composer_options = (quantapdf_composer_options){0};
    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_operations = 1u;
    CHECK(quantapdf_composer_create(&composer_options, &composer) ==
          QUANTAPDF_OK);
    CHECK(add_page(composer));
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_OK);
    line_commands(commands, 80.0f);
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &dash) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_test_pdf_content_count(
              data, size, 0u, " d ") == 1u);

    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_content_render_copy_and_determinism(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_path_options options = stroke_options();
    quantapdf_composer_path_command commands[2];
    quantapdf_composer_dash_pattern first_dash = {0};
    quantapdf_composer_dash_pattern second_dash = {0};
    float first_lengths[] = {10.0f, 5.0f};
    float second_lengths[] = {6.0f, 3.0f, 2.0f};
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render = {0};
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

    first_dash.struct_size = QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE;
    first_dash.lengths = first_lengths;
    first_dash.length_count = 2u;
    first_dash.phase = 0.0f;
    line_commands(commands, 40.0f);
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &first_dash) ==
          QUANTAPDF_OK);

    second_dash.struct_size = QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE;
    second_dash.lengths = second_lengths;
    second_dash.length_count = 3u;
    second_dash.phase = 2.0f;
    line_commands(commands, 90.0f);
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, commands, 2u, &options, &second_dash) ==
          QUANTAPDF_OK);

    line_commands(commands, 140.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, commands, 2u, &options) == QUANTAPDF_OK);

    first_lengths[0] = 99.0f;
    first_lengths[1] = 99.0f;
    second_lengths[0] = 99.0f;
    second_lengths[1] = 99.0f;
    second_lengths[2] = 99.0f;

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
        "[10 5] 0 d 20 140 m 200 140 l S"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u,
        "[6 3 2] 2 d 20 90 m 200 90 l S"));
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, " d ") == 2u);
    CHECK(!quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "[99"));

    CHECK(quantapdf_output_save_file(first, COMPOSER_DASH_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_DASH_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 220 && height == 180 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);
    CHECK(pixel_is_dark(pixels, stride, 24, 40));
    CHECK(pixel_is_white(pixels, stride, 32, 40));
    CHECK(pixel_is_dark(pixels, stride, 24, 140));

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
    CHECK(test_validation() == 0);
    CHECK(test_resource_and_operation_atomicity() == 0);
    CHECK(test_content_render_copy_and_determinism() == 0);
    return 0;
}
