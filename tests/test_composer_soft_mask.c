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

static quantapdf_status draw_rect(
    quantapdf_composer *composer,
    size_t page_index,
    float x0,
    float y0,
    float x1,
    float y1,
    uint32_t argb,
    quantapdf_composer_graphics_state_id graphics_state_id)
{
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_options path = fill_options(argb);

    rectangle_commands(rect, x0, y0, x1, y1);
    if (graphics_state_id != 0u) {
        path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
        path.graphics_state_id = graphics_state_id;
    }
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

static quantapdf_status half_alpha_content_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id state_id = 0u;
    quantapdf_status status;

    (void)user_data;
    state.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE;
    state.fill_alpha = 0.5f;
    state.stroke_alpha = 0.5f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    status = quantapdf_composer_add_graphics_state(
        composer, &state, &state_id);
    if (status != QUANTAPDF_OK)
        return status;
    return draw_rect(
        composer,
        page_index,
        0.0f,
        0.0f,
        40.0f,
        60.0f,
        UINT32_C(0xffffffff),
        state_id);
}

static quantapdf_status nested_alpha_mask_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_draw_options draw = {0};
    quantapdf_composer_form_id form_id = 0u;
    quantapdf_affine_transform identity = {
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    quantapdf_status status;

    (void)user_data;
    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE;
    form.width_points = 80.0f;
    form.height_points = 60.0f;
    form.flags =
        QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
        QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
    status = quantapdf_composer_add_form(
        composer,
        &form,
        half_alpha_content_builder,
        NULL,
        &form_id);
    if (status != QUANTAPDF_OK)
        return status;
    draw.struct_size = QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    return quantapdf_composer_draw_form(
        composer, page_index, form_id, &identity, &draw);
}

static quantapdf_status luminosity_mask_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_status status;

    (void)user_data;
    status = draw_rect(
        composer,
        page_index,
        0.0f,
        0.0f,
        40.0f,
        60.0f,
        UINT32_C(0xffffffff),
        0u);
    if (status != QUANTAPDF_OK)
        return status;
    return draw_rect(
        composer,
        page_index,
        40.0f,
        0.0f,
        80.0f,
        60.0f,
        UINT32_C(0xff000000),
        0u);
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

static int make_zero_mask_output(
    int v3,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id state_id = 0u;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0u;
    size_t page_index = SIZE_MAX;

    *out_data = NULL;
    *out_size = 0u;
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 100.0f, 80.0f, &page_index));
    state.struct_size = v3
        ? QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V3_SIZE
        : QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
    state.fill_alpha = 0.75f;
    state.stroke_alpha = 0.75f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    state.clip_id = 0u;
    state.soft_mask_id = 0u;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &state_id) == QUANTAPDF_OK);
    CHECK(draw_rect(
              composer,
              page_index,
              10.0f,
              10.0f,
              90.0f,
              70.0f,
              UINT32_C(0xffff0000),
              state_id) == QUANTAPDF_OK);
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

static int test_validation_dedup_and_v2_identity(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_form_options ordinary = {0};
    quantapdf_composer_form_options group = {0};
    quantapdf_composer_form_id ordinary_id = 0u;
    quantapdf_composer_form_id group_id = 0u;
    quantapdf_composer_soft_mask_options options = {0};
    quantapdf_composer_soft_mask_id id = 99u;
    quantapdf_composer_soft_mask_id duplicate = 0u;
    quantapdf_composer_soft_mask_id distinct = 0u;
    unsigned char *v2 = NULL;
    unsigned char *v3 = NULL;
    size_t v2_size = 0u;
    size_t v3_size = 0u;

    CHECK(make_zero_mask_output(0, &v2, &v2_size) == 0);
    CHECK(make_zero_mask_output(1, &v3, &v3_size) == 0);
    CHECK(v2_size == v3_size);
    CHECK(memcmp(v2, v3, v2_size) == 0);
    free(v3);
    free(v2);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);

    ordinary.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    ordinary.width_points = 80.0f;
    ordinary.height_points = 60.0f;
    CHECK(quantapdf_composer_add_form(
              composer,
              &ordinary,
              empty_builder,
              NULL,
              &ordinary_id) == QUANTAPDF_OK);

    group.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE;
    group.width_points = 80.0f;
    group.height_points = 60.0f;
    group.flags =
        QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
        QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
    CHECK(quantapdf_composer_add_form(
              composer,
              &group,
              empty_builder,
              NULL,
              &group_id) == QUANTAPDF_OK);

    options.struct_size = QUANTAPDF_COMPOSER_SOFT_MASK_OPTIONS_V1_SIZE;
    options.mode = QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA;

    id = 99u;
    CHECK(quantapdf_composer_add_soft_mask(
              NULL, group_id, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);
    CHECK(quantapdf_composer_add_soft_mask(
              composer, 0u, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id + 100u, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, NULL, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, &options, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    options.struct_size = 0u;
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options.struct_size = QUANTAPDF_COMPOSER_SOFT_MASK_OPTIONS_V1_SIZE;

    options.mode = (quantapdf_composer_soft_mask_mode)99;
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    options.mode = QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA;

    CHECK(quantapdf_composer_add_soft_mask(
              composer, ordinary_id, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);

    options.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    memset(&options.transform, 0, sizeof(options.transform));

    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, &options, &id) == QUANTAPDF_OK);
    CHECK(id != 0u);
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, &options, &duplicate) == QUANTAPDF_OK);
    CHECK(duplicate == id);

    options.mode = QUANTAPDF_COMPOSER_SOFT_MASK_LUMINOSITY;
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, &options, &distinct) == QUANTAPDF_OK);
    CHECK(distinct != id);

    options.mode = QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA;
    options.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 0.0f};
    CHECK(quantapdf_composer_add_soft_mask(
              composer, group_id, &options, &duplicate) == QUANTAPDF_OK);
    CHECK(duplicate != id);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_alpha_luminosity_render_and_dictionary(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_id alpha_form_id = 0u;
    quantapdf_composer_form_id luminosity_form_id = 0u;
    quantapdf_composer_soft_mask_options mask = {0};
    quantapdf_composer_soft_mask_id alpha_mask_id = 0u;
    quantapdf_composer_soft_mask_id luminosity_mask_id = 0u;
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id alpha_state_id = 0u;
    quantapdf_composer_graphics_state_id luminosity_state_id = 0u;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t page0 = SIZE_MAX;
    size_t page1 = SIZE_MAX;
    int mode = 0;
    int isolated = 0;
    int has_source_form = 0;
    double bbox[4] = {0};

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 160.0f, 120.0f, &page0));
    CHECK(add_page(composer, 160.0f, 120.0f, &page1));

    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE;
    form.width_points = 80.0f;
    form.height_points = 60.0f;
    form.flags =
        QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
        QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
    CHECK(quantapdf_composer_add_form(
              composer,
              &form,
              nested_alpha_mask_builder,
              NULL,
              &alpha_form_id) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_form(
              composer,
              &form,
              luminosity_mask_builder,
              NULL,
              &luminosity_form_id) == QUANTAPDF_OK);

    mask.struct_size = QUANTAPDF_COMPOSER_SOFT_MASK_OPTIONS_V1_SIZE;
    mask.mode = QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA;
    mask.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 1.0f, 20.0f, 20.0f};
    CHECK(quantapdf_composer_add_soft_mask(
              composer,
              alpha_form_id,
              &mask,
              &alpha_mask_id) == QUANTAPDF_OK);

    mask.mode = QUANTAPDF_COMPOSER_SOFT_MASK_LUMINOSITY;
    CHECK(quantapdf_composer_add_soft_mask(
              composer,
              luminosity_form_id,
              &mask,
              &luminosity_mask_id) == QUANTAPDF_OK);

    state.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V3_SIZE;
    state.fill_alpha = 0.5f;
    state.stroke_alpha = 0.5f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    state.soft_mask_id = alpha_mask_id;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &alpha_state_id) == QUANTAPDF_OK);

    state.fill_alpha = 1.0f;
    state.stroke_alpha = 1.0f;
    state.soft_mask_id = luminosity_mask_id;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &luminosity_state_id) == QUANTAPDF_OK);

    CHECK(draw_rect(
              composer,
              page0,
              0.0f,
              0.0f,
              160.0f,
              120.0f,
              UINT32_C(0xff0000ff),
              alpha_state_id) == QUANTAPDF_OK);
    CHECK(draw_rect(
              composer,
              page1,
              0.0f,
              0.0f,
              160.0f,
              120.0f,
              UINT32_C(0xffff0000),
              luminosity_state_id) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_soft_mask_info(
        first_data,
        first_size,
        page0,
        alpha_state_id,
        &mode,
        bbox,
        &isolated,
        &has_source_form));
    CHECK(mode == 1 && isolated == 1 && has_source_form == 1);
    CHECK(fabs(bbox[0]) < 0.001 && fabs(bbox[1]) < 0.001);
    CHECK(fabs(bbox[2] - 160.0) < 0.001);
    CHECK(fabs(bbox[3] - 120.0) < 0.001);

    CHECK(quantapdf_test_pdf_soft_mask_info(
        first_data,
        first_size,
        page1,
        luminosity_state_id,
        &mode,
        bbox,
        &isolated,
        &has_source_form));
    CHECK(mode == 2 && isolated == 1 && has_source_form == 1);

    CHECK(quantapdf_output_save_file(
              first, COMPOSER_SOFT_MASK_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_SOFT_MASK_OUTPUT_PDF,
              NULL,
              &document) == QUANTAPDF_OK);

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
        CHECK(pixel_near(pixels, stride, 30, 30, 191, 191, 255, 35));
        CHECK(pixel_near(pixels, stride, 70, 30, 255, 255, 255, 20));
        CHECK(pixel_near(pixels, stride, 10, 30, 255, 255, 255, 20));
        CHECK(pixel_near(pixels, stride, 30, 90, 255, 255, 255, 20));
        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
    }

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

        CHECK(quantapdf_load_page(document, 1, &page) == QUANTAPDF_OK);
        render.struct_size = sizeof(render);
        render.dpi = 72.0f;
        CHECK(quantapdf_render_page_with_options(
                  page, &render, &bitmap) == QUANTAPDF_OK);
        CHECK(quantapdf_bitmap_dimensions(
                  bitmap, &width, &height, &stride, &components) ==
              QUANTAPDF_OK);
        CHECK(quantapdf_bitmap_data(
                  bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
        CHECK(pixel_near(pixels, stride, 30, 30, 255, 0, 0, 25));
        CHECK(pixel_near(pixels, stride, 70, 30, 255, 255, 255, 25));
        CHECK(pixel_near(pixels, stride, 10, 30, 255, 255, 255, 20));
        CHECK(pixel_near(pixels, stride, 30, 90, 255, 255, 255, 20));
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
    CHECK(test_validation_dedup_and_v2_identity() == 0);
    CHECK(test_alpha_luminosity_render_and_dictionary() == 0);
    return 0;
}
