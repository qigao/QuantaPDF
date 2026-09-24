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

static int contains_bytes(
    const char *haystack,
    size_t haystack_size,
    const char *needle,
    size_t needle_size)
{
    size_t i;

    if (needle_size == 0u)
        return 1;
    if (haystack == NULL || needle == NULL || needle_size > haystack_size)
        return 0;
    for (i = 0u; i <= haystack_size - needle_size; ++i) {
        if (memcmp(haystack + i, needle, needle_size) == 0)
            return 1;
    }
    return 0;
}

static int add_page(
    quantapdf_composer *composer,
    float width,
    float height,
    uint32_t background)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = width;
    page.height_points = height;
    page.background_argb = background;
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

static quantapdf_status simple_form_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_gradient_stop stops[2] = {
        {0.0f, UINT32_C(0xffff0000)},
        {1.0f, UINT32_C(0xffff0000)}
    };
    quantapdf_composer_linear_gradient_options gradient = {0};
    quantapdf_composer_paint_id paint_id = 0u;
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_options path = {0};
    quantapdf_composer_text_options text = {0};
    quantapdf_composer_raster raster = {0};
    quantapdf_composer_image_id image_id = 0u;
    quantapdf_composer_image_options image = {0};
    quantapdf_rect text_bounds = {4.0f, 42.0f, 48.0f, 59.0f};
    quantapdf_rect image_bounds = {80.0f, 42.0f, 100.0f, 60.0f};
    unsigned char green[3] = {0u, 255u, 0u};
    quantapdf_status status;

    (void)user_data;

    gradient.struct_size =
        QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE;
    gradient.start = (quantapdf_point){0.0f, 0.0f};
    gradient.end = (quantapdf_point){100.0f, 0.0f};
    gradient.stops = stops;
    gradient.stop_count = 2u;
    status = quantapdf_composer_add_linear_gradient(
        composer, &gradient, &paint_id);
    if (status != QUANTAPDF_OK)
        return status;

    rectangle_commands(rect, 0.0f, 0.0f, 100.0f, 40.0f);
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    path.fill = 1;
    path.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    path.fill_paint_id = paint_id;
    status = quantapdf_composer_draw_path(
        composer, page_index, rect, 5u, &path);
    if (status != QUANTAPDF_OK)
        return status;

    text.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    text.font = QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD;
    text.font_size = 12.0f;
    text.argb = UINT32_C(0xff000000);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text.wrap = 0;
    status = quantapdf_composer_draw_text(
        composer, page_index, "FORM", &text_bounds, &text);
    if (status != QUANTAPDF_OK)
        return status;

    raster.struct_size = QUANTAPDF_COMPOSER_RASTER_V1_SIZE;
    raster.format = QUANTAPDF_COMPOSER_RASTER_RGB24;
    raster.width = 1u;
    raster.height = 1u;
    raster.stride = 3u;
    raster.pixels = green;
    raster.size = sizeof(green);
    status = quantapdf_composer_add_raster(
        composer, &raster, &image_id);
    if (status != QUANTAPDF_OK)
        return status;

    image.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    image.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    return quantapdf_composer_draw_image(
        composer, page_index, image_id, &image_bounds, &image);
}

static quantapdf_status empty_form_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    (void)composer;
    (void)page_index;
    (void)user_data;
    return QUANTAPDF_OK;
}

static quantapdf_status failing_form_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    (void)composer;
    (void)page_index;
    (void)user_data;
    return QUANTAPDF_ERROR_STATE;
}

static quantapdf_status extra_page_form_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_page_options page = {0};
    size_t extra = SIZE_MAX;

    (void)page_index;
    (void)user_data;
    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 10.0f;
    page.height_points = 10.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(composer, &page, &extra);
}

static quantapdf_status nested_form_builder(
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
    quantapdf_composer_text_options text = {0};
    quantapdf_rect bounds = {50.0f, 42.0f, 98.0f, 59.0f};
    quantapdf_status status;

    (void)user_data;

    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    form.width_points = 100.0f;
    form.height_points = 60.0f;
    status = quantapdf_composer_add_form(
        composer, &form, simple_form_builder, NULL, &inner_id);
    if (status != QUANTAPDF_OK)
        return status;

    draw.struct_size = QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    status = quantapdf_composer_draw_form(
        composer, page_index, inner_id, &identity, &draw);
    if (status != QUANTAPDF_OK)
        return status;

    text.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    text.font = QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD;
    text.font_size = 10.0f;
    text.argb = UINT32_C(0xff000000);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text.wrap = 0;
    return quantapdf_composer_draw_text(
        composer, page_index, "NEST", &bounds, &text);
}

static int test_validation_dedup_and_budget(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_options limits = {0};
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_draw_options draw = {0};
    quantapdf_composer_form_id form_id = 99u;
    quantapdf_composer_form_id duplicate = 0u;
    quantapdf_affine_transform identity = {
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    quantapdf_affine_transform singular = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };

    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    form.width_points = 100.0f;
    form.height_points = 60.0f;

    CHECK(quantapdf_composer_add_form(
              NULL, &form, simple_form_builder, NULL, &form_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(form_id == 0u);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);

    form_id = 99u;
    CHECK(quantapdf_composer_add_form(
              composer, NULL, simple_form_builder, NULL, &form_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(form_id == 0u);
    CHECK(quantapdf_composer_add_form(
              composer, &form, NULL, NULL, &form_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_form(
              composer, &form, simple_form_builder, NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    form.struct_size = 0u;
    CHECK(quantapdf_composer_add_form(
              composer, &form, simple_form_builder, NULL, &form_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;

    form.width_points = NAN;
    CHECK(quantapdf_composer_add_form(
              composer, &form, simple_form_builder, NULL, &form_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    form.width_points = 100.0f;
    form.height_points = 0.0f;
    CHECK(quantapdf_composer_add_form(
              composer, &form, simple_form_builder, NULL, &form_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    form.height_points = 60.0f;

    CHECK(quantapdf_composer_add_form(
              composer, &form, failing_form_builder, NULL, &form_id) ==
          QUANTAPDF_ERROR_STATE);
    CHECK(form_id == 0u);
    CHECK(quantapdf_composer_add_form(
              composer, &form, extra_page_form_builder, NULL, &form_id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(form_id == 0u);

    CHECK(quantapdf_composer_add_form(
              composer, &form, simple_form_builder, NULL, &form_id) ==
          QUANTAPDF_OK);
    CHECK(form_id == 1u);
    CHECK(quantapdf_composer_add_form(
              composer, &form, simple_form_builder, NULL, &duplicate) ==
          QUANTAPDF_OK);
    CHECK(duplicate == form_id);

    CHECK(add_page(composer, 200.0f, 120.0f, UINT32_C(0xffffffff)));
    draw.struct_size = QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, form_id + 10u, &identity, &draw) ==
          QUANTAPDF_ERROR_ARGUMENT);
    draw.graphics_state_id = 999u;
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, form_id, &identity, &draw) ==
          QUANTAPDF_ERROR_ARGUMENT);
    draw.graphics_state_id = 0u;
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, form_id, &singular, &draw) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    composer = NULL;

    limits.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    limits.max_resource_bytes = 1u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    form_id = 99u;
    CHECK(quantapdf_composer_add_form(
              composer, &form, empty_form_builder, NULL, &form_id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(form_id == 0u);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_render_extract_nested_and_state(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_form_draw_options draw = {0};
    quantapdf_composer_form_id form_id = 0u;
    quantapdf_composer_form_id nested_id = 0u;
    quantapdf_affine_transform translated = {
        1.0f, 0.0f, 0.0f, 1.0f, 20.0f, 20.0f
    };
    quantapdf_affine_transform rotated = {
        0.0f, 1.0f, -1.0f, 0.0f, 260.0f, 20.0f
    };
    quantapdf_affine_transform clipped_translation = {
        1.0f, 0.0f, 0.0f, 1.0f, 20.0f, 140.0f
    };
    quantapdf_affine_transform nested_translation = {
        1.0f, 0.0f, 0.0f, 1.0f, 160.0f, 140.0f
    };
    quantapdf_composer_path_command clip_rect[5];
    quantapdf_composer_clip_options clip_options = {0};
    quantapdf_composer_clip_id clip_id = 0u;
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id state_id = 0u;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render = {0};
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const unsigned char *pixels = NULL;
    char *extracted = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t pixel_size = 0u;
    size_t extracted_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;
    double bbox[4] = {0};
    int has_resources = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 300.0f, 220.0f, UINT32_C(0xff0000ff)));

    form.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    form.width_points = 100.0f;
    form.height_points = 60.0f;
    CHECK(quantapdf_composer_add_form(
              composer, &form, simple_form_builder, NULL, &form_id) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_form(
              composer, &form, nested_form_builder, NULL, &nested_id) ==
          QUANTAPDF_OK);
    CHECK(form_id == 1u && nested_id == 2u);

    draw.struct_size = QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, form_id, &translated, &draw) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_form(
              composer, 0u, form_id, &rotated, &draw) == QUANTAPDF_OK);

    rectangle_commands(clip_rect, 20.0f, 140.0f, 70.0f, 200.0f);
    clip_options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    clip_options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    CHECK(quantapdf_composer_add_clip_path(
              composer, clip_rect, 5u, &clip_options, &clip_id) ==
          QUANTAPDF_OK);

    state.struct_size = QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
    state.fill_alpha = 0.5f;
    state.stroke_alpha = 1.0f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    state.clip_id = clip_id;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state, &state_id) == QUANTAPDF_OK);
    draw.graphics_state_id = state_id;
    CHECK(quantapdf_composer_draw_form(
              composer,
              0u,
              form_id,
              &clipped_translation,
              &draw) == QUANTAPDF_OK);

    draw.graphics_state_id = 0u;
    CHECK(quantapdf_composer_draw_form(
              composer,
              0u,
              nested_id,
              &nested_translation,
              &draw) == QUANTAPDF_OK);

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
        "1 0 0 1 20 140 cm /Fm1 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u,
        "0 -1 1 0 200 200 cm /Fm1 Do"));
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/Fm1 Do") == 3u);
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/Fm2 Do") == 1u);

    CHECK(quantapdf_test_pdf_form_xobject_info(
              first_data,
              first_size,
              0u,
              form_id,
              bbox,
              &has_resources));
    CHECK(fabs(bbox[0]) < 0.001);
    CHECK(fabs(bbox[1]) < 0.001);
    CHECK(fabs(bbox[2] - 100.0) < 0.001);
    CHECK(fabs(bbox[3] - 60.0) < 0.001);
    CHECK(has_resources == 1);

    CHECK(quantapdf_output_save_file(first, COMPOSER_FORM_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_FORM_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(contains_bytes(extracted, extracted_size, "FORM", 4u));
    CHECK(contains_bytes(extracted, extracted_size, "NEST", 4u));

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 300 && height == 220 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 30, 30, 255, 0, 0, 25));
    CHECK(pixel_near(pixels, stride, 80, 70, 0, 0, 255, 20));
    CHECK(pixel_near(pixels, stride, 240, 70, 255, 0, 0, 30));
    CHECK(pixel_near(pixels, stride, 30, 150, 128, 0, 128, 35));
    CHECK(pixel_near(pixels, stride, 90, 150, 0, 0, 255, 20));
    CHECK(pixel_near(pixels, stride, 170, 150, 255, 0, 0, 30));

    quantapdf_free(extracted);
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
    CHECK(test_render_extract_nested_and_state() == 0);
    return 0;
}
