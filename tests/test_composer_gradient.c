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
            fprintf(stderr, "CHECK failed at %s:%d: %s\\n",                 \
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

static quantapdf_composer_path_options fill_options(void)
{
    quantapdf_composer_path_options options = {0};
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    options.fill = 1;
    options.fill_argb = UINT32_C(0xff000000);
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    return options;
}

static quantapdf_composer_path_options stroke_options(void)
{
    quantapdf_composer_path_options options = {0};
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    options.stroke = 1;
    options.stroke_argb = UINT32_C(0xff000000);
    options.stroke_width = 8.0f;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    options.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    options.miter_limit = 10.0f;
    return options;
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

static int make_solid_output(
    int v3,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    quantapdf_composer_path_options path = fill_options();
    quantapdf_composer_path_command rect[5];
    const unsigned char *data = NULL;
    size_t size = 0u;

    *out_data = NULL;
    *out_size = 0u;
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 100.0f, 100.0f));
    rectangle_commands(rect, 10.0f, 10.0f, 90.0f, 90.0f);
    if (v3)
        path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);

    *out_data = (unsigned char *)malloc(size);
    CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;

    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_validation_budget_and_v3_identity(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_options limits = {0};
    quantapdf_composer_linear_gradient_options linear = {0};
    quantapdf_composer_radial_gradient_options radial = {0};
    quantapdf_composer_gradient_stop stops[3] = {
        {0.0f, UINT32_C(0xffff0000)},
        {0.5f, UINT32_C(0xff00ff00)},
        {1.0f, UINT32_C(0xff0000ff)}
    };
    quantapdf_composer_gradient_stop bad[3];
    quantapdf_composer_gradient_stop budget_stops[2] = {
        {0.0f, UINT32_C(0xffff0000)},
        {1.0f, UINT32_C(0xff0000ff)}
    };
    quantapdf_composer_paint_id id = 99u;
    quantapdf_composer_paint_id duplicate = 0u;
    quantapdf_composer_path_options path = fill_options();
    quantapdf_composer_path_command rect[5];
    unsigned char *v2 = NULL;
    unsigned char *v3 = NULL;
    size_t v2_size = 0u;
    size_t v3_size = 0u;

    CHECK(make_solid_output(0, &v2, &v2_size) == 0);
    CHECK(make_solid_output(1, &v3, &v3_size) == 0);
    CHECK(v2_size == v3_size);
    CHECK(memcmp(v2, v3, v2_size) == 0);
    free(v3);
    free(v2);

    linear.struct_size =
        QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE;
    linear.start = (quantapdf_point){0.0f, 0.0f};
    linear.end = (quantapdf_point){100.0f, 0.0f};
    linear.stops = stops;
    linear.stop_count = 3u;

    CHECK(quantapdf_composer_add_linear_gradient(
              NULL, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);

    id = 99u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, NULL, &id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    linear.struct_size = 0u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    linear.struct_size =
        QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE;

    linear.stops = NULL;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    linear.stops = stops;

    linear.stop_count = 1u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    linear.stop_count = QUANTAPDF_COMPOSER_MAX_GRADIENT_STOPS + 1u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    linear.stop_count = 3u;

    memcpy(bad, stops, sizeof(stops));
    bad[0].offset = 0.1f;
    linear.stops = bad;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    memcpy(bad, stops, sizeof(stops));
    bad[2].offset = 0.9f;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    memcpy(bad, stops, sizeof(stops));
    bad[1].offset = 0.0f;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    memcpy(bad, stops, sizeof(stops));
    bad[1].offset = NAN;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    memcpy(bad, stops, sizeof(stops));
    bad[1].argb = UINT32_C(0x8000ff00);
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    linear.stops = stops;

    linear.end = linear.start;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    linear.end = (quantapdf_point){100.0f, 0.0f};
    linear.start.x = NAN;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    linear.start.x = 0.0f;

    linear.transform.a = 1.0f;
    linear.transform.d = 0.0f;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_ARGUMENT);
    memset(&linear.transform, 0, sizeof(linear.transform));

    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_OK);
    CHECK(id == 1u);
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &duplicate) == QUANTAPDF_OK);
    CHECK(duplicate == id);

    radial.struct_size =
        QUANTAPDF_COMPOSER_RADIAL_GRADIENT_OPTIONS_V1_SIZE;
    radial.start_center = (quantapdf_point){50.0f, 50.0f};
    radial.start_radius = 0.0f;
    radial.end_center = (quantapdf_point){50.0f, 50.0f};
    radial.end_radius = 50.0f;
    radial.stops = stops;
    radial.stop_count = 3u;
    CHECK(quantapdf_composer_add_radial_gradient(
              composer, &radial, &id) == QUANTAPDF_OK);
    radial.start_radius = -1.0f;
    CHECK(quantapdf_composer_add_radial_gradient(
              composer, &radial, &id) == QUANTAPDF_ERROR_ARGUMENT);
    radial.start_radius = 50.0f;
    radial.end_radius = 50.0f;
    CHECK(quantapdf_composer_add_radial_gradient(
              composer, &radial, &id) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(add_page(composer, 100.0f, 100.0f));
    quantapdf_drop_composer(composer);
    composer = NULL;

    limits.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    limits.max_resource_bytes =
        2u * sizeof(quantapdf_composer_gradient_stop) - 1u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    linear.stops = budget_stops;
    linear.stop_count = 2u;
    linear.end = (quantapdf_point){100.0f, 0.0f};
    memset(&linear.transform, 0, sizeof(linear.transform));
    id = 99u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0u);
    quantapdf_drop_composer(composer);

    limits.max_resource_bytes =
        2u * sizeof(quantapdf_composer_gradient_stop);
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &id) == QUANTAPDF_OK);
    CHECK(id == 1u);
    CHECK(add_page(composer, 100.0f, 100.0f));
    rectangle_commands(rect, 10.0f, 10.0f, 90.0f, 90.0f);
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    path.fill_paint_id = id + 10u;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) ==
          QUANTAPDF_ERROR_ARGUMENT);
    path.fill = 0;
    path.fill_paint_id = id;
    path.stroke = 1;
    path.stroke_argb = UINT32_C(0xff000000);
    path.stroke_width = 1.0f;
    path.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    path.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    path.miter_limit = 10.0f;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_render_resources_ownership_and_determinism(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_linear_gradient_options linear = {0};
    quantapdf_composer_radial_gradient_options radial = {0};
    quantapdf_composer_gradient_stop two[2] = {
        {0.0f, UINT32_C(0xffff0000)},
        {1.0f, UINT32_C(0xff0000ff)}
    };
    quantapdf_composer_gradient_stop three[3] = {
        {0.0f, UINT32_C(0xffff0000)},
        {0.5f, UINT32_C(0xff00ff00)},
        {1.0f, UINT32_C(0xff0000ff)}
    };
    quantapdf_composer_gradient_stop radial_stops[2] = {
        {0.0f, UINT32_C(0xffffffff)},
        {1.0f, UINT32_C(0xff000000)}
    };
    quantapdf_composer_gradient_stop vertical[2] = {
        {0.0f, UINT32_C(0xffffff00)},
        {1.0f, UINT32_C(0xff00ffff)}
    };
    quantapdf_composer_paint_id linear_id = 0u;
    quantapdf_composer_paint_id multi_id = 0u;
    quantapdf_composer_paint_id radial_id = 0u;
    quantapdf_composer_paint_id vertical_id = 0u;
    quantapdf_composer_path_options path = {0};
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_command line[2] = {{0}};
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
    int shading_type = 0;
    int function_type = 0;
    double matrix[6] = {0};

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 320.0f, 240.0f));

    linear.struct_size =
        QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE;
    linear.start = (quantapdf_point){20.0f, 0.0f};
    linear.end = (quantapdf_point){120.0f, 0.0f};
    linear.stops = two;
    linear.stop_count = 2u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &linear_id) == QUANTAPDF_OK);
    CHECK(linear_id == 1u);

    linear.start = (quantapdf_point){140.0f, 0.0f};
    linear.end = (quantapdf_point){240.0f, 0.0f};
    linear.stops = three;
    linear.stop_count = 3u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &multi_id) == QUANTAPDF_OK);
    CHECK(multi_id == 2u);

    radial.struct_size =
        QUANTAPDF_COMPOSER_RADIAL_GRADIENT_OPTIONS_V1_SIZE;
    radial.start_center = (quantapdf_point){70.0f, 150.0f};
    radial.start_radius = 0.0f;
    radial.end_center = (quantapdf_point){70.0f, 150.0f};
    radial.end_radius = 50.0f;
    radial.stops = radial_stops;
    radial.stop_count = 2u;
    CHECK(quantapdf_composer_add_radial_gradient(
              composer, &radial, &radial_id) == QUANTAPDF_OK);
    CHECK(radial_id == 3u);

    linear.start = (quantapdf_point){0.0f, 0.0f};
    linear.end = (quantapdf_point){100.0f, 0.0f};
    linear.transform =
        (quantapdf_affine_transform){0.0f, 1.0f, -1.0f, 0.0f, 280.0f, 20.0f};
    linear.stops = vertical;
    linear.stop_count = 2u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &linear, &vertical_id) == QUANTAPDF_OK);
    CHECK(vertical_id == 4u);

    memset(two, 0, sizeof(two));
    memset(three, 0, sizeof(three));
    memset(radial_stops, 0, sizeof(radial_stops));
    memset(vertical, 0, sizeof(vertical));

    path = fill_options();
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    path.fill_paint_id = linear_id;
    rectangle_commands(rect, 20.0f, 20.0f, 120.0f, 80.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    path.fill_paint_id = multi_id;
    rectangle_commands(rect, 140.0f, 20.0f, 240.0f, 80.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    path.fill_paint_id = radial_id;
    rectangle_commands(rect, 20.0f, 100.0f, 120.0f, 200.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    path.fill_paint_id = vertical_id;
    rectangle_commands(rect, 250.0f, 20.0f, 300.0f, 120.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    path = stroke_options();
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    path.stroke_paint_id = linear_id;
    line[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    line[0].point1 = (quantapdf_point){20.0f, 220.0f};
    line[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    line[1].point1 = (quantapdf_point){120.0f, 220.0f};
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, line, 2u, &path) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "/Pattern cs /P1 scn"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "/Pattern CS /P1 SCN"));
    CHECK(quantapdf_test_pdf_pattern_info(
              first_data, first_size, 0u, linear_id,
              &shading_type, &function_type, matrix));
    CHECK(shading_type == 2 && function_type == 2);
    CHECK(close_number(matrix[0], 1.0));
    CHECK(close_number(matrix[1], 0.0));
    CHECK(close_number(matrix[2], 0.0));
    CHECK(close_number(matrix[3], -1.0));
    CHECK(close_number(matrix[4], 0.0));
    CHECK(close_number(matrix[5], 240.0));

    CHECK(quantapdf_test_pdf_pattern_info(
              first_data, first_size, 0u, multi_id,
              &shading_type, &function_type, matrix));
    CHECK(shading_type == 2 && function_type == 3);
    CHECK(quantapdf_test_pdf_pattern_info(
              first_data, first_size, 0u, radial_id,
              &shading_type, &function_type, matrix));
    CHECK(shading_type == 3 && function_type == 2);
    CHECK(quantapdf_test_pdf_pattern_info(
              first_data, first_size, 0u, vertical_id,
              &shading_type, &function_type, matrix));
    CHECK(shading_type == 2 && function_type == 2);
    CHECK(close_number(matrix[0], 0.0));
    CHECK(close_number(matrix[1], -1.0));
    CHECK(close_number(matrix[2], -1.0));
    CHECK(close_number(matrix[3], 0.0));
    CHECK(close_number(matrix[4], 280.0));
    CHECK(close_number(matrix[5], 220.0));

    CHECK(quantapdf_output_save_file(first, COMPOSER_GRADIENT_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_GRADIENT_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 320 && height == 240 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 25, 50, 242, 0, 13, 35));
    CHECK(pixel_near(pixels, stride, 115, 50, 13, 0, 242, 35));
    CHECK(pixel_near(pixels, stride, 190, 50, 0, 255, 0, 40));
    CHECK(pixel_near(pixels, stride, 70, 150, 255, 255, 255, 25));
    CHECK(pixel_near(pixels, stride, 115, 150, 25, 25, 25, 45));
    CHECK(pixel_near(pixels, stride, 275, 25, 242, 255, 13, 40));
    CHECK(pixel_near(pixels, stride, 275, 115, 13, 255, 242, 40));
    CHECK(pixel_near(pixels, stride, 25, 220, 242, 0, 13, 50));
    CHECK(pixel_near(pixels, stride, 115, 220, 13, 0, 242, 50));

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
    CHECK(test_validation_budget_and_v3_identity() == 0);
    CHECK(test_render_resources_ownership_and_determinism() == 0);
    return 0;
}
