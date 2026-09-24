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
    float height,
    size_t *out_page_index)
{
    quantapdf_composer_page_options page = {0};

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = width;
    page.height_points = height;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(
               composer, &page, out_page_index) == QUANTAPDF_OK;
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

static quantapdf_composer_path_options fill_options(uint32_t argb)
{
    quantapdf_composer_path_options options = {0};
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    options.fill = 1;
    options.fill_argb = argb;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    return options;
}

static quantapdf_status full_green_form_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_options path =
        fill_options(UINT32_C(0xff00ff00));

    (void)user_data;
    rectangle_commands(rect, 0.0f, 0.0f, 160.0f, 120.0f);
    return quantapdf_composer_draw_path(
        composer, page_index, rect, 5u, &path);
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

static int test_validation_dedup_and_budget(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_options limits = {0};
    quantapdf_composer_path_command a[5];
    quantapdf_composer_path_command b[5];
    quantapdf_composer_clip_options options = {0};
    quantapdf_composer_clip_id a_id = 0u;
    quantapdf_composer_clip_id b_id = 0u;
    quantapdf_composer_clip_id compound = 99u;
    quantapdf_composer_clip_id pair[2];
    quantapdf_composer_clip_id invalid[2];
    size_t command_bytes = 2u * 5u * sizeof(quantapdf_composer_path_command);
    size_t member_bytes = 2u * sizeof(quantapdf_composer_clip_id);

    rectangle_commands(a, 0.0f, 0.0f, 100.0f, 100.0f);
    rectangle_commands(b, 20.0f, 20.0f, 80.0f, 80.0f);
    options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_clip_path(
              composer, a, 5u, &options, &a_id) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_clip_path(
              composer, b, 5u, &options, &b_id) == QUANTAPDF_OK);
    CHECK(a_id != 0u && b_id != 0u && a_id != b_id);

    pair[0] = a_id;
    pair[1] = b_id;

    compound = 99u;
    CHECK(quantapdf_composer_add_clip_intersection(
              NULL, pair, 2u, &compound) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(compound == 0u);

    compound = 99u;
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, NULL, 2u, &compound) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(compound == 0u);
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, pair, 1u, &compound) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_clip_intersection(
              composer,
              pair,
              QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS + 1u,
              &compound) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, pair, 2u, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    invalid[0] = 0u;
    invalid[1] = b_id;
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, invalid, 2u, &compound) ==
          QUANTAPDF_ERROR_ARGUMENT);
    invalid[0] = a_id;
    invalid[1] = b_id + 100u;
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, invalid, 2u, &compound) ==
          QUANTAPDF_ERROR_ARGUMENT);

    pair[0] = a_id;
    pair[1] = a_id;
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, pair, 2u, &compound) == QUANTAPDF_OK);
    CHECK(compound == a_id);

    pair[0] = a_id;
    pair[1] = b_id;
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, pair, 2u, &compound) == QUANTAPDF_OK);
    CHECK(compound != 0u && compound != a_id && compound != b_id);
    {
        quantapdf_composer_clip_id reverse[2] = {b_id, a_id};
        quantapdf_composer_clip_id reverse_id = 0u;
        CHECK(quantapdf_composer_add_clip_intersection(
                  composer, reverse, 2u, &reverse_id) == QUANTAPDF_OK);
        CHECK(reverse_id == compound);
    }

    quantapdf_drop_composer(composer);
    composer = NULL;

    limits.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    limits.max_resource_bytes = command_bytes + member_bytes - 1u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_clip_path(
              composer, a, 5u, &options, &a_id) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_clip_path(
              composer, b, 5u, &options, &b_id) == QUANTAPDF_OK);
    pair[0] = a_id;
    pair[1] = b_id;
    compound = 99u;
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, pair, 2u, &compound) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(compound == 0u);
    quantapdf_drop_composer(composer);

    limits.max_resource_bytes = command_bytes + member_bytes;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_clip_path(
              composer, a, 5u, &options, &a_id) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_clip_path(
              composer, b, 5u, &options, &b_id) == QUANTAPDF_OK);
    pair[0] = a_id;
    pair[1] = b_id;
    CHECK(quantapdf_composer_add_clip_intersection(
              composer, pair, 2u, &compound) == QUANTAPDF_OK);
    CHECK(compound != 0u);
    quantapdf_drop_composer(composer);
    return 0;
}

static int render_and_check_page(
    quantapdf_document *document,
    size_t page_index,
    int expected_r,
    int expected_g,
    int expected_b)
{
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render = {0};
    const unsigned char *pixels = NULL;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_load_page(
              document, (int)page_index, &page) == QUANTAPDF_OK);
    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(
              page, &render, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 160 && height == 120 && components == 3);
    CHECK(quantapdf_bitmap_data(
              bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(
        pixels, stride, 50, 30,
        expected_r, expected_g, expected_b, 20));
    CHECK(pixel_near(pixels, stride, 20, 30, 255, 255, 255, 15));
    CHECK(pixel_near(pixels, stride, 50, 80, 255, 255, 255, 15));
    CHECK(pixel_near(pixels, stride, 100, 30, 255, 255, 255, 15));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    return 0;
}

static int test_nested_intersection_render_and_scope(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_path_command a[5];
    quantapdf_composer_path_command b[5];
    quantapdf_composer_path_command c[5];
    quantapdf_composer_path_command full[5];
    quantapdf_composer_path_command blue[5];
    quantapdf_composer_clip_options options = {0};
    quantapdf_composer_clip_id a_id = 0u;
    quantapdf_composer_clip_id b_id = 0u;
    quantapdf_composer_clip_id c_id = 0u;
    quantapdf_composer_clip_id ab_id = 0u;
    quantapdf_composer_clip_id abc_id = 0u;
    quantapdf_composer_clip_id flat_id = 0u;
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id state_id = 0u;
    quantapdf_composer_path_options red =
        fill_options(UINT32_C(0xffff0000));
    quantapdf_composer_path_options blue_path =
        fill_options(UINT32_C(0xff0000ff));
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_draw_options form_draw = {0};
    quantapdf_composer_form_id form_id = 0u;
    quantapdf_affine_transform identity = {
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t page0 = SIZE_MAX;
    size_t page1 = SIZE_MAX;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 160.0f, 120.0f, &page0));
    CHECK(add_page(composer, 160.0f, 120.0f, &page1));
    CHECK(page0 == 0u && page1 == 1u);

    rectangle_commands(a, 10.0f, 10.0f, 110.0f, 90.0f);
    rectangle_commands(b, 20.0f, 0.0f, 70.0f, 100.0f);
    rectangle_commands(c, 0.0f, 20.0f, 120.0f, 70.0f);
    options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;

    CHECK(quantapdf_composer_add_clip_path(
              composer, a, 5u, &options, &a_id) == QUANTAPDF_OK);

    options.fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    options.transform = (quantapdf_affine_transform){
        1.0f, 0.0f, 0.0f, 1.0f, 20.0f, 0.0f};
    CHECK(quantapdf_composer_add_clip_path(
              composer, b, 5u, &options, &b_id) == QUANTAPDF_OK);

    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    memset(&options.transform, 0, sizeof(options.transform));
    CHECK(quantapdf_composer_add_clip_path(
              composer, c, 5u, &options, &c_id) == QUANTAPDF_OK);

    {
        quantapdf_composer_clip_id pair[2] = {a_id, b_id};
        CHECK(quantapdf_composer_add_clip_intersection(
                  composer, pair, 2u, &ab_id) == QUANTAPDF_OK);
    }
    {
        quantapdf_composer_clip_id pair[2] = {ab_id, c_id};
        CHECK(quantapdf_composer_add_clip_intersection(
                  composer, pair, 2u, &abc_id) == QUANTAPDF_OK);
    }
    {
        quantapdf_composer_clip_id leaves[3] = {c_id, b_id, a_id};
        CHECK(quantapdf_composer_add_clip_intersection(
                  composer, leaves, 3u, &flat_id) == QUANTAPDF_OK);
        CHECK(flat_id == abc_id);
    }

    state.struct_size = QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
    state.fill_alpha = 1.0f;
    state.stroke_alpha = 1.0f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    state.clip_id = abc_id;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &state_id) == QUANTAPDF_OK);
    CHECK(state_id != 0u);

    rectangle_commands(full, 0.0f, 0.0f, 160.0f, 120.0f);
    red.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    red.graphics_state_id = state_id;
    CHECK(quantapdf_composer_draw_path(
              composer, page0, full, 5u, &red) == QUANTAPDF_OK);

    rectangle_commands(blue, 120.0f, 10.0f, 150.0f, 40.0f);
    CHECK(quantapdf_composer_draw_path(
              composer, page0, blue, 5u, &blue_path) == QUANTAPDF_OK);

    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    form.width_points = 160.0f;
    form.height_points = 120.0f;
    CHECK(quantapdf_composer_add_form(
              composer,
              &form,
              full_green_form_builder,
              NULL,
              &form_id) == QUANTAPDF_OK);
    form_draw.struct_size = QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    form_draw.graphics_state_id = state_id;
    CHECK(quantapdf_composer_draw_form(
              composer,
              page1,
              form_id,
              &identity,
              &form_draw) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              first, &first_data, &first_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              second, &second_data, &second_size) == QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "W n") == 2u);
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "W* n") == 1u);
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "40 120 m 90 120 l 90 20 l 40 20 l h W* n"));

    CHECK(quantapdf_output_save_file(
              first, COMPOSER_COMPOUND_CLIP_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_COMPOUND_CLIP_OUTPUT_PDF,
              NULL,
              &document) == QUANTAPDF_OK);
    CHECK(render_and_check_page(document, 0u, 255, 0, 0) == 0);
    CHECK(render_and_check_page(document, 1u, 0, 255, 0) == 0);

    {
        quantapdf_page *page = NULL;
        quantapdf_bitmap *bitmap = NULL;
        quantapdf_render_options render = {0};
        const unsigned char *pixels = NULL;
        size_t pixel_size = 0u;
        int width = 0;
        int height = 0;
        int stride = 0;
        int components = 0;

        CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
        render.struct_size = sizeof(render);
        render.dpi = 72.0f;
        CHECK(quantapdf_render_page_with_options(
                  page, &render, &bitmap) == QUANTAPDF_OK);
        CHECK(quantapdf_bitmap_dimensions(
                  bitmap, &width, &height, &stride, &components) ==
              QUANTAPDF_OK);
        CHECK(quantapdf_bitmap_data(
                  bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
        CHECK(pixel_near(
            pixels, stride, 130, 20, 0, 0, 255, 20));
        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
    }

    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_validation_dedup_and_budget() == 0);
    CHECK(test_nested_intersection_render_and_scope() == 0);
    return 0;
}
