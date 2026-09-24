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

static quantapdf_status draw_filled_rect(
    quantapdf_composer *composer,
    size_t page_index,
    float x0,
    float y0,
    float x1,
    float y1,
    uint32_t argb)
{
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_options path = {0};

    rectangle_commands(rect, x0, y0, x1, y1);
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    path.fill = 1;
    path.fill_argb = argb;
    path.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    return quantapdf_composer_draw_path(
        composer, page_index, rect, 5u, &path);
}

static quantapdf_status overlap_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_status status;

    (void)user_data;
    status = draw_filled_rect(
        composer, page_index, 0.0f, 0.0f, 60.0f, 60.0f,
        UINT32_C(0xffff0000));
    if (status != QUANTAPDF_OK)
        return status;
    return draw_filled_rect(
        composer, page_index, 40.0f, 0.0f, 100.0f, 60.0f,
        UINT32_C(0xff0000ff));
}

static quantapdf_status nested_group_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_draw_options draw = {0};
    quantapdf_composer_form_id inner_id = 0u;
    quantapdf_affine_transform identity = {
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    quantapdf_status status;

    (void)user_data;
    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE;
    form.width_points = 100.0f;
    form.height_points = 60.0f;
    form.flags = QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
        QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
    status = quantapdf_composer_add_form(
        composer, &form, overlap_builder, NULL, &inner_id);
    if (status != QUANTAPDF_OK)
        return status;

    draw.struct_size = QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    return quantapdf_composer_draw_form(
        composer, page_index, inner_id, &identity, &draw);
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

static int make_zero_flag_output(
    int v2,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_draw_options draw = {0};
    quantapdf_composer_form_id form_id = 0u;
    quantapdf_affine_transform transform = {
        1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 10.0f
    };
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0u;

    *out_data = NULL;
    *out_size = 0u;
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 120.0f, 80.0f));

    form.struct_size = v2
        ? QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE
        : QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    form.width_points = 100.0f;
    form.height_points = 60.0f;
    form.flags = 0u;
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &form_id) ==
          QUANTAPDF_OK);

    draw.struct_size = QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, form_id, &transform, &draw) == QUANTAPDF_OK);
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

static int test_validation_dedup_and_v1_identity(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_id id = 99u;
    quantapdf_composer_form_id duplicate = 0u;
    quantapdf_composer_form_id distinct = 0u;
    unsigned char *v1 = NULL;
    unsigned char *v2 = NULL;
    size_t v1_size = 0u;
    size_t v2_size = 0u;

    CHECK(make_zero_flag_output(0, &v1, &v1_size) == 0);
    CHECK(make_zero_flag_output(1, &v2, &v2_size) == 0);
    CHECK(v1_size == v2_size);
    CHECK(memcmp(v1, v2, v1_size) == 0);
    free(v2);
    free(v1);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);

    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE;
    form.width_points = 100.0f;
    form.height_points = 60.0f;

    form.flags = UINT32_C(0x80000000);
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    form.flags = QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    form.flags = QUANTAPDF_COMPOSER_FORM_FLAG_KNOCKOUT;
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);

    form.flags = QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
        QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &id) ==
          QUANTAPDF_OK);
    CHECK(id == 1u);
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &duplicate) ==
          QUANTAPDF_OK);
    CHECK(duplicate == id);

    form.flags |= QUANTAPDF_COMPOSER_FORM_FLAG_KNOCKOUT;
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &distinct) ==
          QUANTAPDF_OK);
    CHECK(distinct == 2u);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_group_dictionary_render_and_nested(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_draw_options draw = {0};
    quantapdf_composer_form_id ordinary_id = 0u;
    quantapdf_composer_form_id grouped_id = 0u;
    quantapdf_composer_form_id knockout_id = 0u;
    quantapdf_composer_form_id nested_id = 0u;
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id half_id = 0u;
    quantapdf_affine_transform ordinary_transform = {
        1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f
    };
    quantapdf_affine_transform grouped_transform = {
        1.0f, 0.0f, 0.0f, 1.0f, 130.0f, 20.0f
    };
    quantapdf_affine_transform knockout_transform = {
        0.5f, 0.0f, 0.0f, 0.5f, 250.0f, 20.0f
    };
    quantapdf_affine_transform nested_transform = {
        0.5f, 0.0f, 0.0f, 0.5f, 250.0f, 60.0f
    };
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
    int has_group = 0;
    int isolated = 0;
    int knockout = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 360.0f, 110.0f));

    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    form.width_points = 100.0f;
    form.height_points = 60.0f;
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &ordinary_id) ==
          QUANTAPDF_OK);

    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE;
    form.flags = QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
        QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &grouped_id) ==
          QUANTAPDF_OK);

    form.flags |= QUANTAPDF_COMPOSER_FORM_FLAG_KNOCKOUT;
    CHECK(quantapdf_composer_add_form(
              composer, &form, overlap_builder, NULL, &knockout_id) ==
          QUANTAPDF_OK);

    form.flags = QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
        QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
    CHECK(quantapdf_composer_add_form(
              composer, &form, nested_group_builder, NULL, &nested_id) ==
          QUANTAPDF_OK);

    state.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE;
    state.fill_alpha = 0.5f;
    state.stroke_alpha = 0.5f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &half_id) == QUANTAPDF_OK);

    draw.struct_size = QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    draw.graphics_state_id = half_id;
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, ordinary_id, &ordinary_transform, &draw) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, grouped_id, &grouped_transform, &draw) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, knockout_id, &knockout_transform, &draw) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, nested_id, &nested_transform, &draw) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);

    CHECK(quantapdf_test_pdf_form_group_info(
        data, size, 0u, ordinary_id,
        &has_group, &isolated, &knockout));
    CHECK(has_group == 0);

    CHECK(quantapdf_test_pdf_form_group_info(
        data, size, 0u, grouped_id,
        &has_group, &isolated, &knockout));
    CHECK(has_group == 1 && isolated == 1 && knockout == 0);

    CHECK(quantapdf_test_pdf_form_group_info(
        data, size, 0u, knockout_id,
        &has_group, &isolated, &knockout));
    CHECK(has_group == 1 && isolated == 1 && knockout == 1);

    CHECK(quantapdf_output_save_file(
              output, COMPOSER_FORM_GROUP_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_FORM_GROUP_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 360 && height == 110 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 30, 40, 255, 128, 128, 25));
    CHECK(pixel_near(pixels, stride, 60, 40, 128, 64, 191, 35));
    CHECK(pixel_near(pixels, stride, 150, 40, 255, 128, 128, 25));
    CHECK(pixel_near(pixels, stride, 180, 40, 128, 128, 255, 30));
    CHECK(pixel_near(pixels, stride, 275, 75, 255, 128, 128, 35));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_validation_dedup_and_v1_identity() == 0);
    CHECK(test_group_dictionary_render_and_nested() == 0);
    return 0;
}
