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

static int add_page(
    quantapdf_composer *composer,
    float width,
    float height)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = width;
    page.height_points = height;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(
               composer, &page, &page_index) == QUANTAPDF_OK &&
        page_index == 0u;
}

static void rectangle_commands(
    quantapdf_composer_path_command commands[5],
    float x0,
    float y0,
    float x1,
    float y1)
{
    memset(commands, 0, sizeof(*commands) * 5u);
    commands[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[0].point1 = (quantapdf_point){x0, y0};
    commands[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1 = (quantapdf_point){x1, y0};
    commands[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[2].point1 = (quantapdf_point){x1, y1};
    commands[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[3].point1 = (quantapdf_point){x0, y1};
    commands[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
}

static quantapdf_status tile_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_options path = {0};

    (void)user_data;
    rectangle_commands(rect, 0.0f, 0.0f, 5.0f, 8.0f);
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    path.fill = 1;
    path.fill_argb = UINT32_C(0xffff0000);
    path.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    return quantapdf_composer_draw_path(
        composer, page_index, rect, 5u, &path);
}

static quantapdf_status empty_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    (void)composer;
    (void)page_index;
    (void)user_data;
    return QUANTAPDF_OK;
}

static quantapdf_status failing_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    (void)composer;
    (void)page_index;
    (void)user_data;
    return QUANTAPDF_ERROR_STATE;
}

static quantapdf_status extra_page_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_page_options page = {0};
    size_t extra = SIZE_MAX;

    (void)page_index;
    (void)user_data;
    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 1.0f;
    page.height_points = 1.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(composer, &page, &extra);
}

static int near_channel(unsigned char actual, int expected, int tolerance)
{
    int delta = (int)actual - expected;
    if (delta < 0)
        delta = -delta;
    return delta <= tolerance;
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
    return near_channel(p[0], r, tolerance) &&
        near_channel(p[1], g, tolerance) &&
        near_channel(p[2], b, tolerance);
}

static int close_number(double left, double right)
{
    return fabs(left - right) < 0.001;
}

static int test_validation_dedup_and_budget(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_options limits = {0};
    quantapdf_composer_tiling_pattern_options options = {0};
    quantapdf_composer_paint_id id = 99u;
    quantapdf_composer_paint_id duplicate = 0u;
    quantapdf_composer_paint_id distinct = 0u;

    options.struct_size =
        QUANTAPDF_COMPOSER_TILING_PATTERN_OPTIONS_V1_SIZE;
    options.width_points = 10.0f;
    options.height_points = 8.0f;
    options.x_step = 12.0f;
    options.y_step = 10.0f;

    CHECK(quantapdf_composer_add_tiling_pattern(
              NULL, &options, tile_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);

    id = 99u;
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, NULL, tile_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, NULL, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    options.struct_size = 0u;
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options.struct_size =
        QUANTAPDF_COMPOSER_TILING_PATTERN_OPTIONS_V1_SIZE;

    options.width_points = 0.0f;
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options.width_points = 10.0f;
    options.height_points = NAN;
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options.height_points = 8.0f;
    options.x_step = 0.0f;
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options.x_step = 12.0f;
    options.y_step = 0.0f;
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options.y_step = 10.0f;

    options.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    memset(&options.transform, 0, sizeof(options.transform));

    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, failing_builder, NULL, &id) ==
          QUANTAPDF_ERROR_STATE);
    CHECK(id == 0u);
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, extra_page_builder, NULL, &id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0u);

    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &id) == QUANTAPDF_OK);
    CHECK(id == 1u);
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &duplicate) ==
          QUANTAPDF_OK);
    CHECK(duplicate == id);

    options.x_step = 13.0f;
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, tile_builder, NULL, &distinct) ==
          QUANTAPDF_OK);
    CHECK(distinct == 2u);

    quantapdf_drop_composer(composer);
    composer = NULL;

    limits.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    limits.max_resource_bytes = 1u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    options.x_step = 12.0f;
    id = 99u;
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &options, empty_builder, NULL, &id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0u);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_pattern_resources_fill_stroke_and_render(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_tiling_pattern_options pattern = {0};
    quantapdf_composer_tiling_pattern_options rotated = {0};
    quantapdf_composer_paint_id pattern_id = 0u;
    quantapdf_composer_paint_id rotated_id = 0u;
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_command line[2] = {{0}};
    quantapdf_composer_path_options fill = {0};
    quantapdf_composer_path_options stroke = {0};
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
    double bbox[4] = {0};
    double x_step = 0.0;
    double y_step = 0.0;
    double matrix[6] = {0};
    int has_tile_form = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 160.0f, 120.0f));

    pattern.struct_size =
        QUANTAPDF_COMPOSER_TILING_PATTERN_OPTIONS_V1_SIZE;
    pattern.width_points = 10.0f;
    pattern.height_points = 8.0f;
    pattern.x_step = 12.0f;
    pattern.y_step = 10.0f;
    pattern.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 1.0f, 3.0f, 4.0f};
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &pattern, tile_builder, NULL, &pattern_id) ==
          QUANTAPDF_OK);
    CHECK(pattern_id == 1u);

    rotated = pattern;
    rotated.transform =
        (quantapdf_affine_transform){0.0f, 1.0f, -1.0f, 0.0f, 100.0f, 20.0f};
    CHECK(quantapdf_composer_add_tiling_pattern(
              composer, &rotated, tile_builder, NULL, &rotated_id) ==
          QUANTAPDF_OK);
    CHECK(rotated_id == 2u);

    rectangle_commands(rect, 0.0f, 0.0f, 80.0f, 60.0f);
    fill.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    fill.fill = 1;
    fill.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    fill.fill_paint_id = pattern_id;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &fill) == QUANTAPDF_OK);

    line[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    line[0].point1 = (quantapdf_point){0.0f, 90.0f};
    line[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    line[1].point1 = (quantapdf_point){120.0f, 90.0f};
    stroke.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    stroke.stroke = 1;
    stroke.stroke_width = 6.0f;
    stroke.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    stroke.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    stroke.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    stroke.miter_limit = 10.0f;
    stroke.stroke_paint_id = pattern_id;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, line, 2u, &stroke) == QUANTAPDF_OK);

    rectangle_commands(rect, 100.0f, 0.0f, 150.0f, 50.0f);
    fill.fill_paint_id = rotated_id;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &fill) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_tiling_pattern_info(
        first_data,
        first_size,
        0u,
        pattern_id,
        bbox,
        &x_step,
        &y_step,
        matrix,
        &has_tile_form));
    CHECK(close_number(bbox[0], 0.0));
    CHECK(close_number(bbox[1], 0.0));
    CHECK(close_number(bbox[2], 10.0));
    CHECK(close_number(bbox[3], 8.0));
    CHECK(close_number(x_step, 12.0));
    CHECK(close_number(y_step, -10.0));
    CHECK(close_number(matrix[0], 1.0));
    CHECK(close_number(matrix[1], 0.0));
    CHECK(close_number(matrix[2], 0.0));
    CHECK(close_number(matrix[3], 1.0));
    CHECK(close_number(matrix[4], 3.0));
    CHECK(close_number(matrix[5], 108.0));
    CHECK(has_tile_form == 1);

    CHECK(quantapdf_test_pdf_tiling_pattern_info(
        first_data,
        first_size,
        0u,
        rotated_id,
        bbox,
        &x_step,
        &y_step,
        matrix,
        &has_tile_form));
    CHECK(close_number(matrix[0], 0.0));
    CHECK(close_number(matrix[1], -1.0));
    CHECK(close_number(matrix[2], 1.0));
    CHECK(close_number(matrix[3], 0.0));
    CHECK(close_number(matrix[4], 92.0));
    CHECK(close_number(matrix[5], 100.0));

    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "/Pattern cs /P1 scn"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "/Pattern CS /P1 SCN"));

    CHECK(quantapdf_output_save_file(
              first, COMPOSER_TILING_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_TILING_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 160 && height == 120 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 4, 5, 255, 0, 0, 25));
    CHECK(pixel_near(pixels, stride, 10, 5, 255, 255, 255, 20));
    CHECK(pixel_near(pixels, stride, 16, 5, 255, 0, 0, 25));
    CHECK(pixel_near(pixels, stride, 4, 15, 255, 0, 0, 25));
    CHECK(pixel_near(pixels, stride, 90, 20, 255, 255, 255, 20));

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
    CHECK(test_validation_dedup_and_budget() == 0);
    CHECK(test_pattern_resources_fill_stroke_and_render() == 0);
    return 0;
}
