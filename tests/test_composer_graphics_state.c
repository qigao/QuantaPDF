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

static int read_file(
    const char *path,
    unsigned char **out_data,
    size_t *out_size)
{
    FILE *file = NULL;
    unsigned char *data = NULL;
    long length;

    *out_data = NULL;
    *out_size = 0u;
    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    data = (unsigned char *)malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return 0;
    }
    if (fread(data, 1u, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    *out_data = data;
    *out_size = (size_t)length;
    return 1;
}

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

static quantapdf_composer_path_options fill_path_options(uint32_t argb)
{
    quantapdf_composer_path_options options = {0};
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    options.fill = 1;
    options.fill_argb = argb;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
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

static int channel_near(unsigned char value, int expected, int tolerance)
{
    int delta = (int)value - expected;
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
    return channel_near(p[0], r, tolerance) &&
        channel_near(p[1], g, tolerance) &&
        channel_near(p[2], b, tolerance);
}

static int make_identity_output(
    int v2,
    unsigned char **out_copy,
    size_t *out_size)
{
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_options options =
        fill_path_options(UINT32_C(0xff336699));
    const unsigned char *data = NULL;
    size_t size = 0u;

    *out_copy = NULL;
    *out_size = 0u;
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 100.0f, 100.0f));
    rectangle_commands(rect, 10.0f, 10.0f, 90.0f, 90.0f);
    if (v2) {
        options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
        options.graphics_state_id = 0u;
    }
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);

    *out_copy = (unsigned char *)malloc(size);
    CHECK(*out_copy != NULL);
    memcpy(*out_copy, data, size);
    *out_size = size;

    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_registration_validation_and_v1_identity(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id id = 99u;
    quantapdf_composer_graphics_state_id duplicate = 0u;
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_options path =
        fill_path_options(UINT32_C(0xff000000));
    quantapdf_composer_text_options text = {0};
    unsigned char *v1 = NULL;
    unsigned char *v2 = NULL;
    size_t v1_size = 0u;
    size_t v2_size = 0u;

    CHECK(make_identity_output(0, &v1, &v1_size) == 0);
    CHECK(make_identity_output(1, &v2, &v2_size) == 0);
    CHECK(v1_size == v2_size);
    CHECK(memcmp(v1, v2, v1_size) == 0);
    free(v2);
    free(v1);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 100.0f, 100.0f));

    CHECK(quantapdf_composer_add_graphics_state(
              composer, NULL, &id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    state.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE;
    state.fill_alpha = 0.5f;
    state.stroke_alpha = 0.25f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_MULTIPLY;

    id = 99u;
    CHECK(quantapdf_composer_add_graphics_state(
              NULL, &state, &id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    state.fill_alpha = NAN;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &id) == QUANTAPDF_ERROR_ARGUMENT);
    state.fill_alpha = -0.1f;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &id) == QUANTAPDF_ERROR_ARGUMENT);
    state.fill_alpha = 1.1f;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &id) == QUANTAPDF_ERROR_ARGUMENT);
    state.fill_alpha = 0.5f;

    state.stroke_alpha = NAN;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &id) == QUANTAPDF_ERROR_ARGUMENT);
    state.stroke_alpha = -0.1f;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &id) == QUANTAPDF_ERROR_ARGUMENT);
    state.stroke_alpha = 1.1f;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &id) == QUANTAPDF_ERROR_ARGUMENT);
    state.stroke_alpha = 0.25f;

    state.blend_mode = (quantapdf_composer_blend_mode)99;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &id) == QUANTAPDF_ERROR_ARGUMENT);
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_MULTIPLY;

    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &id) == QUANTAPDF_OK);
    CHECK(id == 1u);
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &duplicate) == QUANTAPDF_OK);
    CHECK(duplicate == id);

    rectangle_commands(rect, 10.0f, 10.0f, 90.0f, 90.0f);
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    path.graphics_state_id = id + 10u;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) ==
          QUANTAPDF_ERROR_ARGUMENT);

    text.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V2_SIZE;
    text.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    text.font_size = 12.0f;
    text.argb = UINT32_C(0xff000000);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text.wrap = 0;
    text.graphics_state_id = id + 10u;
    {
        quantapdf_rect bounds = {10.0f, 10.0f, 90.0f, 30.0f};
        CHECK(quantapdf_composer_draw_text(
                  composer, 0u, "bad state", &bounds, &text) ==
              QUANTAPDF_ERROR_ARGUMENT);
    }

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_render_resources_and_attachment(
    const unsigned char *font_data,
    size_t font_size)
{
    unsigned char rgba_pixel[4] = {0u, 0u, 255u, 128u};
    quantapdf_composer *composer = NULL;
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id half_id = 0u;
    quantapdf_composer_graphics_state_id multiply_id = 0u;
    quantapdf_composer_font_options font_options = {0};
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_composer_raster raster = {0};
    quantapdf_composer_image_id image_id = 0u;
    quantapdf_composer_image_options image_options = {0};
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_command line[2] = {{0}};
    quantapdf_composer_path_options path = {0};
    quantapdf_composer_dash_pattern dash = {0};
    float dash_lengths[2] = {5.0f, 3.0f};
    quantapdf_composer_text_options text = {0};
    quantapdf_composer_embedded_text_options embedded = {0};
    quantapdf_composer_glyph_run_options glyph_options = {0};
    quantapdf_composer_glyph glyph = {0};
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
    double fill_alpha = 0.0;
    double stroke_alpha = 0.0;
    char blend[32] = {0};

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 240.0f, 180.0f));

    state.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE;
    state.fill_alpha = 0.5f;
    state.stroke_alpha = 0.25f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &half_id) == QUANTAPDF_OK);
    CHECK(half_id == 1u);

    state.fill_alpha = 1.0f;
    state.stroke_alpha = 1.0f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_MULTIPLY;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &multiply_id) == QUANTAPDF_OK);
    CHECK(multiply_id == 2u);

    path = fill_path_options(UINT32_C(0xffff0000));
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    path.graphics_state_id = half_id;
    rectangle_commands(rect, 10.0f, 10.0f, 70.0f, 70.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    path = fill_path_options(UINT32_C(0xff0000ff));
    rectangle_commands(rect, 80.0f, 10.0f, 140.0f, 70.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    path = fill_path_options(UINT32_C(0xff00ff00));
    rectangle_commands(rect, 150.0f, 10.0f, 210.0f, 70.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);
    path = fill_path_options(UINT32_C(0xffff0000));
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    path.graphics_state_id = multiply_id;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    raster.struct_size = QUANTAPDF_COMPOSER_RASTER_V1_SIZE;
    raster.format = QUANTAPDF_COMPOSER_RASTER_RGBA32;
    raster.width = 1u;
    raster.height = 1u;
    raster.stride = 4u;
    raster.pixels = rgba_pixel;
    raster.size = sizeof(rgba_pixel);
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &image_id) == QUANTAPDF_OK);
    image_options.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    image_options.graphics_state_id = half_id;
    {
        quantapdf_rect image_bounds = {10.0f, 90.0f, 70.0f, 150.0f};
        CHECK(quantapdf_composer_draw_image(
                  composer, 0u, image_id, &image_bounds, &image_options) ==
              QUANTAPDF_OK);
    }

    text.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V2_SIZE;
    text.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    text.font_size = 14.0f;
    text.argb = UINT32_C(0xff000000);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text.wrap = 0;
    text.graphics_state_id = half_id;
    {
        quantapdf_rect text_bounds = {80.0f, 85.0f, 145.0f, 105.0f};
        CHECK(quantapdf_composer_draw_text(
                  composer, 0u, "Alpha", &text_bounds, &text) ==
              QUANTAPDF_OK);
    }

    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
              composer, font_data, font_size, &font_options, &font_id) ==
          QUANTAPDF_OK);

    embedded.struct_size =
        QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V2_SIZE;
    embedded.font_id = font_id;
    embedded.font_size = 14.0f;
    embedded.argb = UINT32_C(0xff000000);
    embedded.line_height_multiplier = 1.2f;
    embedded.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    embedded.wrap = 0;
    embedded.graphics_state_id = half_id;
    {
        quantapdf_rect embedded_bounds = {80.0f, 110.0f, 150.0f, 130.0f};
        CHECK(quantapdf_composer_draw_embedded_text(
                  composer, 0u, "State", &embedded_bounds, &embedded) ==
              QUANTAPDF_OK);
    }

    glyph_options.struct_size =
        QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V2_SIZE;
    glyph_options.font_id = font_id;
    glyph_options.font_size = 14.0f;
    glyph_options.argb = UINT32_C(0xff000000);
    glyph_options.graphics_state_id = half_id;
    glyph.glyph_id = 0u;
    glyph.x_advance = 600.0f;
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              (quantapdf_point){160.0f, 120.0f},
              &glyph,
              1u,
              NULL,
              0u,
              &glyph_options) == QUANTAPDF_OK);

    memset(line, 0, sizeof(line));
    line[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    line[0].point1 = (quantapdf_point){80.0f, 155.0f};
    line[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    line[1].point1 = (quantapdf_point){210.0f, 155.0f};
    path = (quantapdf_composer_path_options){0};
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    path.stroke = 1;
    path.stroke_argb = UINT32_C(0xff000000);
    path.stroke_width = 4.0f;
    path.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    path.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    path.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    path.miter_limit = 10.0f;
    path.graphics_state_id = half_id;
    dash.struct_size = QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE;
    dash.lengths = dash_lengths;
    dash.length_count = 2u;
    dash.phase = 0.0f;
    CHECK(quantapdf_composer_draw_path_dashed(
              composer, 0u, line, 2u, &path, &dash) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/GS1 gs") == 6u);
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/GS2 gs") == 1u);
    CHECK(quantapdf_test_pdf_extgstate_info(
              first_data, first_size, 0u, half_id,
              &fill_alpha, &stroke_alpha, blend, sizeof(blend)));
    CHECK(fabs(fill_alpha - 0.5) < 0.0001);
    CHECK(fabs(stroke_alpha - 0.25) < 0.0001);
    CHECK(strcmp(blend, "/Normal") == 0);
    CHECK(quantapdf_test_pdf_extgstate_info(
              first_data, first_size, 0u, multiply_id,
              &fill_alpha, &stroke_alpha, blend, sizeof(blend)));
    CHECK(fabs(fill_alpha - 1.0) < 0.0001);
    CHECK(fabs(stroke_alpha - 1.0) < 0.0001);
    CHECK(strcmp(blend, "/Multiply") == 0);

    CHECK(quantapdf_output_save_file(first, COMPOSER_GRAPHICS_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_GRAPHICS_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 240 && height == 180 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 40, 40, 255, 128, 128, 20));
    CHECK(pixel_near(pixels, stride, 110, 40, 0, 0, 255, 15));
    CHECK(pixel_near(pixels, stride, 180, 40, 0, 0, 0, 20));
    CHECK(pixel_near(pixels, stride, 40, 120, 191, 191, 255, 28));

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
    unsigned char *font_data = NULL;
    size_t font_size = 0u;

    CHECK(test_registration_validation_and_v1_identity() == 0);
    CHECK(read_file(ROBOTO_TTF, &font_data, &font_size));
    CHECK(test_render_resources_and_attachment(font_data, font_size) == 0);
    free(font_data);
    return 0;
}
