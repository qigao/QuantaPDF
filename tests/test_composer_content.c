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
    float height,
    uint32_t background,
    size_t expected)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = width;
    page.height_points = height;
    page.background_argb = background;
    return quantapdf_composer_add_page(
               composer, &page, &page_index) == QUANTAPDF_OK &&
        page_index == expected;
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

static int bytes_contains(
    const char *data,
    size_t size,
    const char *needle)
{
    size_t needle_size = strlen(needle);
    size_t i;

    if (needle_size == 0u)
        return 1;
    if (data == NULL || needle_size > size)
        return 0;
    for (i = 0u; i <= size - needle_size; ++i) {
        if (memcmp(data + i, needle, needle_size) == 0)
            return 1;
    }
    return 0;
}

static int build_fragment(quantapdf_composer **out_fragment)
{
    quantapdf_composer *fragment = NULL;
    quantapdf_composer_graphics_state_options state = {0};
    quantapdf_composer_graphics_state_id state_id = 0u;
    quantapdf_composer_gradient_stop stops[2] = {
        {0.0f, UINT32_C(0xffff0000)},
        {1.0f, UINT32_C(0xff0000ff)}
    };
    quantapdf_composer_linear_gradient_options gradient = {0};
    quantapdf_composer_paint_id paint_id = 0u;
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_options path = {0};
    quantapdf_composer_text_options text = {0};
    quantapdf_rect text_bounds = {18.0f, 18.0f, 82.0f, 42.0f};

    *out_fragment = NULL;
    CHECK(quantapdf_composer_create(NULL, &fragment) == QUANTAPDF_OK);
    CHECK(add_page(
        fragment,
        100.0f,
        60.0f,
        UINT32_C(0xffff0000),
        0u));

    state.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE;
    state.fill_alpha = 0.75f;
    state.stroke_alpha = 1.0f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    CHECK(quantapdf_composer_add_graphics_state(
              fragment, &state, &state_id) == QUANTAPDF_OK);

    gradient.struct_size =
        QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE;
    gradient.start = (quantapdf_point){10.0f, 0.0f};
    gradient.end = (quantapdf_point){90.0f, 0.0f};
    gradient.stops = stops;
    gradient.stop_count = 2u;
    CHECK(quantapdf_composer_add_linear_gradient(
              fragment, &gradient, &paint_id) == QUANTAPDF_OK);

    rectangle_commands(rect, 10.0f, 10.0f, 90.0f, 50.0f);
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    path.fill = 1;
    path.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    path.graphics_state_id = state_id;
    path.fill_paint_id = paint_id;
    CHECK(quantapdf_composer_draw_path(
              fragment, 0u, rect, 5u, &path) == QUANTAPDF_OK);

    text.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V2_SIZE;
    text.font = QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD;
    text.font_size = 14.0f;
    text.argb = UINT32_C(0xff000000);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER;
    text.wrap = 0;
    text.graphics_state_id = state_id;
    CHECK(quantapdf_composer_draw_text(
              fragment, 0u, "FORM", &text_bounds, &text) == QUANTAPDF_OK);

    *out_fragment = fragment;
    return 0;
}

static int test_validation_and_budget(void)
{
    quantapdf_composer *parent = NULL;
    quantapdf_composer *empty = NULL;
    quantapdf_composer *two_pages = NULL;
    quantapdf_composer *linked = NULL;
    quantapdf_composer *fragment = NULL;
    quantapdf_composer_options limits = {0};
    quantapdf_composer_content_id content_id = 99u;
    quantapdf_composer_content_options draw = {0};
    quantapdf_rect link_box = {0.0f, 0.0f, 20.0f, 20.0f};

    CHECK(quantapdf_composer_create(NULL, &parent) == QUANTAPDF_OK);
    CHECK(add_page(
        parent, 200.0f, 120.0f, UINT32_C(0xffffffff), 0u));
    CHECK(build_fragment(&fragment) == 0);

    CHECK(quantapdf_composer_add_content(
              NULL, fragment, &content_id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(content_id == 0u);
    content_id = 99u;
    CHECK(quantapdf_composer_add_content(
              parent, NULL, &content_id) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(content_id == 0u);
    CHECK(quantapdf_composer_add_content(
              parent, fragment, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_content(
              parent, parent, &content_id) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_composer_create(NULL, &empty) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_content(
              parent, empty, &content_id) == QUANTAPDF_ERROR_UNSUPPORTED);

    CHECK(quantapdf_composer_create(NULL, &two_pages) == QUANTAPDF_OK);
    CHECK(add_page(
        two_pages, 10.0f, 10.0f, UINT32_C(0xffffffff), 0u));
    CHECK(add_page(
        two_pages, 10.0f, 10.0f, UINT32_C(0xffffffff), 1u));
    CHECK(quantapdf_composer_add_content(
              parent, two_pages, &content_id) == QUANTAPDF_ERROR_UNSUPPORTED);

    CHECK(quantapdf_composer_create(NULL, &linked) == QUANTAPDF_OK);
    CHECK(add_page(
        linked, 40.0f, 40.0f, UINT32_C(0xffffffff), 0u));
    CHECK(quantapdf_composer_add_uri_link(
              linked,
              0u,
              &link_box,
              "https://example.com/content") == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_content(
              parent, linked, &content_id) == QUANTAPDF_ERROR_UNSUPPORTED);

    CHECK(quantapdf_composer_add_content(
              parent, fragment, &content_id) == QUANTAPDF_OK);
    CHECK(content_id == 1u);

    draw.struct_size = QUANTAPDF_COMPOSER_CONTENT_OPTIONS_V1_SIZE;
    draw.graphics_state_id = 999u;
    CHECK(quantapdf_composer_draw_content(
              parent, 0u, content_id, &draw) == QUANTAPDF_ERROR_ARGUMENT);
    draw.graphics_state_id = 0u;
    draw.transform.a = 1.0f;
    CHECK(quantapdf_composer_draw_content(
              parent, 0u, content_id, &draw) == QUANTAPDF_ERROR_ARGUMENT);
    memset(&draw.transform, 0, sizeof(draw.transform));
    CHECK(quantapdf_composer_draw_content(
              parent, 0u, content_id + 10u, &draw) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(linked);
    quantapdf_drop_composer(two_pages);
    quantapdf_drop_composer(empty);
    quantapdf_drop_composer(fragment);
    quantapdf_drop_composer(parent);

    limits.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    limits.max_resource_bytes = 1u;
    CHECK(quantapdf_composer_create(&limits, &parent) == QUANTAPDF_OK);
    CHECK(build_fragment(&fragment) == 0);
    content_id = 99u;
    CHECK(quantapdf_composer_add_content(
              parent, fragment, &content_id) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(content_id == 0u);

    quantapdf_drop_composer(fragment);
    quantapdf_drop_composer(parent);
    return 0;
}

static int test_snapshot_nested_placement_and_render(void)
{
    quantapdf_composer *fragment = NULL;
    quantapdf_composer *wrapper = NULL;
    quantapdf_composer *parent = NULL;
    quantapdf_composer_content_id content_id = 0u;
    quantapdf_composer_content_id duplicate_id = 0u;
    quantapdf_composer_content_id nested_id = 0u;
    quantapdf_composer_content_id changed_id = 0u;
    quantapdf_composer_content_options content = {0};
    quantapdf_composer_path_command mutation[5];
    quantapdf_composer_path_options mutation_path = {0};
    quantapdf_composer_path_command clip_rect[5];
    quantapdf_composer_clip_options clip_options = {0};
    quantapdf_composer_clip_id clip_id = 0u;
    quantapdf_composer_graphics_state_options outer_state = {0};
    quantapdf_composer_graphics_state_id outer_state_id = 0u;
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
    double form_width = 0.0;
    double form_height = 0.0;
    int has_font = 0;
    int has_pattern = 0;
    int has_extgstate = 0;

    CHECK(build_fragment(&fragment) == 0);

    CHECK(quantapdf_composer_create(NULL, &wrapper) == QUANTAPDF_OK);
    CHECK(add_page(
        wrapper, 100.0f, 60.0f, UINT32_C(0xff00ff00), 0u));
    CHECK(quantapdf_composer_add_content(
              wrapper, fragment, &content_id) == QUANTAPDF_OK);
    content.struct_size = QUANTAPDF_COMPOSER_CONTENT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_draw_content(
              wrapper, 0u, content_id, &content) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_create(NULL, &parent) == QUANTAPDF_OK);
    CHECK(add_page(
        parent, 300.0f, 180.0f, UINT32_C(0xffffffff), 0u));

    CHECK(quantapdf_composer_add_content(
              parent, fragment, &content_id) == QUANTAPDF_OK);
    CHECK(content_id == 1u);
    CHECK(quantapdf_composer_add_content(
              parent, fragment, &duplicate_id) == QUANTAPDF_OK);
    CHECK(duplicate_id == content_id);

    CHECK(quantapdf_composer_add_content(
              parent, wrapper, &nested_id) == QUANTAPDF_OK);
    CHECK(nested_id == 2u);

    rectangle_commands(mutation, 0.0f, 0.0f, 100.0f, 60.0f);
    mutation_path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    mutation_path.fill = 1;
    mutation_path.fill_argb = UINT32_C(0xff000000);
    mutation_path.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    CHECK(quantapdf_composer_draw_path(
              fragment, 0u, mutation, 5u, &mutation_path) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_content(
              parent, fragment, &changed_id) == QUANTAPDF_OK);
    CHECK(changed_id != content_id);

    quantapdf_drop_composer(wrapper);
    wrapper = NULL;
    quantapdf_drop_composer(fragment);
    fragment = NULL;

    memset(&content, 0, sizeof(content));
    content.struct_size = QUANTAPDF_COMPOSER_CONTENT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_draw_content(
              parent, 0u, content_id, &content) == QUANTAPDF_OK);

    content.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 1.0f, 120.0f, 0.0f};
    CHECK(quantapdf_composer_draw_content(
              parent, 0u, nested_id, &content) == QUANTAPDF_OK);

    rectangle_commands(clip_rect, 0.0f, 80.0f, 50.0f, 140.0f);
    clip_options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    clip_options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    CHECK(quantapdf_composer_add_clip_path(
              parent, clip_rect, 5u, &clip_options, &clip_id) == QUANTAPDF_OK);
    outer_state.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
    outer_state.fill_alpha = 0.5f;
    outer_state.stroke_alpha = 0.5f;
    outer_state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    outer_state.clip_id = clip_id;
    CHECK(quantapdf_composer_add_graphics_state(
              parent, &outer_state, &outer_state_id) == QUANTAPDF_OK);

    content.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 80.0f};
    content.graphics_state_id = outer_state_id;
    CHECK(quantapdf_composer_draw_content(
              parent, 0u, content_id, &content) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(parent, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(parent, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/C1 Do") == 2u);
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/C2 Do") == 1u);
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "1 0 0 1 0 120 cm /C1 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "1 0 0 1 120 120 cm /C2 Do"));
    CHECK(quantapdf_test_pdf_form_xobject_info(
              first_data,
              first_size,
              0u,
              content_id,
              &form_width,
              &form_height,
              &has_font,
              &has_pattern,
              &has_extgstate));
    CHECK(fabs(form_width - 100.0) < 0.01);
    CHECK(fabs(form_height - 60.0) < 0.01);
    CHECK(has_font && has_pattern && has_extgstate);

    CHECK(quantapdf_output_save_file(first, COMPOSER_CONTENT_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_CONTENT_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(bytes_contains(extracted, extracted_size, "FORM"));

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 300 && height == 180 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 5, 5, 255, 255, 255, 15));
    CHECK(!pixel_near(pixels, stride, 20, 30, 255, 255, 255, 20));
    CHECK(pixel_near(pixels, stride, 125, 5, 255, 255, 255, 15));
    CHECK(!pixel_near(pixels, stride, 140, 30, 255, 255, 255, 20));
    CHECK(!pixel_near(pixels, stride, 20, 110, 255, 255, 255, 20));
    CHECK(pixel_near(pixels, stride, 70, 110, 255, 255, 255, 20));

    quantapdf_free(extracted);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(parent);
    return 0;
}

int main(void)
{
    CHECK(test_validation_and_budget() == 0);
    CHECK(test_snapshot_nested_placement_and_render() == 0);
    return 0;
}
