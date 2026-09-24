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

static quantapdf_composer_path_options fill_options(void)
{
    quantapdf_composer_path_options options = {0};
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    options.fill = 1;
    options.fill_argb = UINT32_C(0xffff0000);
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    return options;
}

static quantapdf_composer_path_options stroke_options(void)
{
    quantapdf_composer_path_options options = {0};
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    options.stroke = 1;
    options.stroke_argb = UINT32_C(0xff000000);
    options.stroke_width = 4.0f;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    options.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL;
    options.miter_limit = 10.0f;
    return options;
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

static int make_identity_output(
    int v4,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_path_options path = fill_options();
    quantapdf_composer_path_command rect[5];
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0u;

    *out_data = NULL;
    *out_size = 0u;
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 120.0f, 100.0f));

    rectangle_commands(rect, 10.0f, 10.0f, 80.0f, 70.0f);
    if (v4) {
        path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        path.transform =
            (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    }
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

static int test_identity_and_validation(void)
{
    unsigned char *v3 = NULL;
    unsigned char *v4 = NULL;
    size_t v3_size = 0u;
    size_t v4_size = 0u;
    quantapdf_composer *composer = NULL;
    quantapdf_composer_path_options path = fill_options();
    quantapdf_composer_path_command rect[5];

    CHECK(make_identity_output(0, &v3, &v3_size) == 0);
    CHECK(make_identity_output(1, &v4, &v4_size) == 0);
    CHECK(v3_size == v4_size);
    CHECK(memcmp(v3, v4, v3_size) == 0);
    free(v4);
    free(v3);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 120.0f, 100.0f));
    rectangle_commands(rect, 10.0f, 10.0f, 80.0f, 70.0f);

    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    memset(&path.transform, 0, sizeof(path.transform));
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    path.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_ERROR_ARGUMENT);

    path.transform =
        (quantapdf_affine_transform){NAN, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_nonconformal_dash_gradient_and_render(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_command line[2] = {{0}};
    quantapdf_composer_path_options fill = fill_options();
    quantapdf_composer_path_options stroke = stroke_options();
    quantapdf_composer_dash_pattern dash = {0};
    float dash_lengths[2] = {10.0f, 5.0f};
    quantapdf_composer_gradient_stop stops[2] = {
        {0.0f, UINT32_C(0xffff0000)},
        {1.0f, UINT32_C(0xff0000ff)}
    };
    quantapdf_composer_linear_gradient_options gradient = {0};
    quantapdf_composer_paint_id paint_id = 0u;
    quantapdf_output *output = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render = {0};
    const unsigned char *data = NULL;
    const unsigned char *pixels = NULL;
    size_t size = 0u;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 300.0f, 240.0f));

    fill.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    fill.transform =
        (quantapdf_affine_transform){2.0f, 0.0f, 0.0f, 0.5f, 20.0f, 40.0f};
    rectangle_commands(rect, 10.0f, 20.0f, 50.0f, 60.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &fill) == QUANTAPDF_OK);

    stroke.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    stroke.transform =
        (quantapdf_affine_transform){2.0f, 0.0f, 0.0f, 0.5f, 20.0f, 40.0f};
    line[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    line[0].point1 = (quantapdf_point){10.0f, 80.0f};
    line[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    line[1].point1 = (quantapdf_point){50.0f, 80.0f};
    dash.struct_size = QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE;
    dash.lengths = dash_lengths;
    dash.length_count = 2u;
    dash.phase = 0.0f;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, line, 2u, &stroke, &dash) == QUANTAPDF_OK);

    gradient.struct_size =
        QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE;
    gradient.start = (quantapdf_point){60.0f, 20.0f};
    gradient.end = (quantapdf_point){100.0f, 20.0f};
    gradient.stops = stops;
    gradient.stop_count = 2u;
    CHECK(quantapdf_composer_add_linear_gradient(
              composer, &gradient, &paint_id) == QUANTAPDF_OK);

    fill = fill_options();
    fill.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    fill.fill_argb = 0u;
    fill.fill_paint_id = paint_id;
    fill.transform =
        (quantapdf_affine_transform){2.0f, 0.0f, 0.0f, 0.5f, 20.0f, 40.0f};
    rectangle_commands(rect, 60.0f, 20.0f, 100.0f, 60.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &fill) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);

    CHECK(quantapdf_test_pdf_content_contains(
        data, size, 0u, "q 2 0 0 0.5 20 80 cm"));
    CHECK(quantapdf_test_pdf_content_contains(
        data,
        size,
        0u,
        "q 2 0 0 0.5 20 80 cm 0 0 0 RG 4 w 1 J 2 j 10 M [10 5] 0 d"));
    CHECK(quantapdf_test_pdf_content_contains(
        data, size, 0u, "/Pattern cs /P1 scn"));

    CHECK(quantapdf_output_save_file(
              output, COMPOSER_PATH_AFFINE_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_PATH_AFFINE_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 300 && height == 240 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 60, 60, 255, 0, 0, 20));
    CHECK(pixel_near(pixels, stride, 30, 60, 255, 255, 255, 15));
    CHECK(!pixel_near(pixels, stride, 160, 60, 255, 255, 255, 15));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_identity_and_validation() == 0);
    CHECK(test_nonconformal_dash_gradient_and_render() == 0);
    return 0;
}
