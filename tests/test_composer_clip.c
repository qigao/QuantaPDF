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

static int test_validation_budget_and_dedup(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_composer_options limits = {0};
    quantapdf_composer_path_command rect[5];
    quantapdf_composer_path_command negative_zero[5];
    quantapdf_composer_clip_options clip_options = {0};
    quantapdf_composer_clip_id clip_id = 99u;
    quantapdf_composer_clip_id duplicate_id = 0u;
    quantapdf_composer_graphics_state_options state_v1 = {0};
    quantapdf_composer_graphics_state_options state_v2 = {0};
    quantapdf_composer_graphics_state_id state_id = 0u;
    quantapdf_composer_graphics_state_id state_duplicate = 0u;
    size_t command_bytes = sizeof(rect);

    rectangle_commands(rect, 0.0f, 0.0f, 80.0f, 80.0f);
    memcpy(negative_zero, rect, sizeof(rect));
    negative_zero[0].point1.x = -0.0f;
    negative_zero[0].point1.y = -0.0f;

    clip_options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    clip_options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;

    CHECK(quantapdf_composer_add_clip_path(
              NULL, rect, 5u, &clip_options, &clip_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(clip_id == 0u);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);

    clip_id = 99u;
    CHECK(quantapdf_composer_add_clip_path(
              composer, NULL, 5u, &clip_options, &clip_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(clip_id == 0u);
    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 0u, &clip_options, &clip_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 5u, NULL, &clip_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 5u, &clip_options, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    clip_options.struct_size = 0u;
    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 5u, &clip_options, &clip_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    clip_options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;

    clip_options.fill_rule = (quantapdf_composer_fill_rule)99;
    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 5u, &clip_options, &clip_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    clip_options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;

    clip_options.transform.a = 1.0f;
    clip_options.transform.d = 0.0f;
    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 5u, &clip_options, &clip_id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    memset(&clip_options.transform, 0, sizeof(clip_options.transform));

    {
        quantapdf_composer_path_command invalid[1] = {{0}};
        invalid[0].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
        invalid[0].point1 = (quantapdf_point){1.0f, 1.0f};
        CHECK(quantapdf_composer_add_clip_path(
                  composer, invalid, 1u, &clip_options, &clip_id) ==
              QUANTAPDF_ERROR_ARGUMENT);
    }

    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 5u, &clip_options, &clip_id) == QUANTAPDF_OK);
    CHECK(clip_id == 1u);
    CHECK(quantapdf_composer_add_clip_path(
              composer,
              negative_zero,
              5u,
              &clip_options,
              &duplicate_id) == QUANTAPDF_OK);
    CHECK(duplicate_id == clip_id);

    state_v1.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE;
    state_v1.fill_alpha = 1.0f;
    state_v1.stroke_alpha = 1.0f;
    state_v1.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state_v1, &state_id) == QUANTAPDF_OK);

    state_v2.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
    state_v2.fill_alpha = 1.0f;
    state_v2.stroke_alpha = 1.0f;
    state_v2.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    state_v2.clip_id = 0u;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state_v2, &state_duplicate) == QUANTAPDF_OK);
    CHECK(state_duplicate == state_id);

    state_v2.clip_id = clip_id + 10u;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state_v2, &state_duplicate) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    composer = NULL;

    limits.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    limits.max_resource_bytes = command_bytes - 1u;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    clip_id = 99u;
    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 5u, &clip_options, &clip_id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(clip_id == 0u);
    quantapdf_drop_composer(composer);

    limits.max_resource_bytes = command_bytes;
    CHECK(quantapdf_composer_create(&limits, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_clip_path(
              composer, rect, 5u, &clip_options, &clip_id) == QUANTAPDF_OK);
    CHECK(clip_id == 1u);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_render_transform_scope_and_consumers(void)
{
    unsigned char green_pixel[3] = {0u, 255u, 0u};
    quantapdf_composer *composer = NULL;
    quantapdf_composer_path_command clip_rect[5];
    quantapdf_composer_path_command evenodd[10];
    quantapdf_composer_path_command fill_rect[5];
    quantapdf_composer_clip_options clip_options = {0};
    quantapdf_composer_clip_id clip_id = 0u;
    quantapdf_composer_clip_id evenodd_id = 0u;
    quantapdf_composer_graphics_state_options state_options = {0};
    quantapdf_composer_graphics_state_id clipped_half_id = 0u;
    quantapdf_composer_graphics_state_id evenodd_state_id = 0u;
    quantapdf_composer_path_options path = {0};
    quantapdf_composer_text_options text = {0};
    quantapdf_composer_raster raster = {0};
    quantapdf_composer_image_id image_id = 0u;
    quantapdf_composer_image_options image = {0};
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
    CHECK(add_page(composer, 300.0f, 220.0f));

    rectangle_commands(clip_rect, 0.0f, 0.0f, 80.0f, 80.0f);
    clip_options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    clip_options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    clip_options.transform =
        (quantapdf_affine_transform){1.0f, 0.0f, 0.0f, 1.0f, 20.0f, 20.0f};
    CHECK(quantapdf_composer_add_clip_path(
              composer,
              clip_rect,
              5u,
              &clip_options,
              &clip_id) == QUANTAPDF_OK);
    CHECK(clip_id == 1u);

    memset(clip_rect, 0, sizeof(clip_rect));

    state_options.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
    state_options.fill_alpha = 0.5f;
    state_options.stroke_alpha = 1.0f;
    state_options.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    state_options.clip_id = clip_id;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state_options, &clipped_half_id) == QUANTAPDF_OK);

    rectangle_commands(&evenodd[0], 210.0f, 20.0f, 290.0f, 100.0f);
    rectangle_commands(&evenodd[5], 230.0f, 40.0f, 270.0f, 80.0f);
    clip_options.fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    memset(&clip_options.transform, 0, sizeof(clip_options.transform));
    CHECK(quantapdf_composer_add_clip_path(
              composer,
              evenodd,
              10u,
              &clip_options,
              &evenodd_id) == QUANTAPDF_OK);
    state_options.fill_alpha = 1.0f;
    state_options.clip_id = evenodd_id;
    CHECK(quantapdf_composer_add_graphics_state(
              composer, &state_options, &evenodd_state_id) == QUANTAPDF_OK);

    rectangle_commands(fill_rect, 0.0f, 0.0f, 110.0f, 110.0f);
    path = fill_options(UINT32_C(0xffff0000));
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    path.graphics_state_id = clipped_half_id;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, fill_rect, 5u, &path) == QUANTAPDF_OK);

    rectangle_commands(fill_rect, 120.0f, 20.0f, 200.0f, 100.0f);
    path = fill_options(UINT32_C(0xff0000ff));
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, fill_rect, 5u, &path) == QUANTAPDF_OK);

    rectangle_commands(fill_rect, 200.0f, 10.0f, 300.0f, 110.0f);
    path = fill_options(UINT32_C(0xff00ff00));
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE;
    path.graphics_state_id = evenodd_state_id;
    CHECK(quantapdf_composer_draw_path(
              composer, 0u, fill_rect, 5u, &path) == QUANTAPDF_OK);

    text.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V2_SIZE;
    text.font = QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD;
    text.font_size = 16.0f;
    text.argb = UINT32_C(0xff000000);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text.wrap = 0;
    text.graphics_state_id = clipped_half_id;
    {
        quantapdf_rect bounds = {40.0f, 60.0f, 140.0f, 90.0f};
        CHECK(quantapdf_composer_draw_text(
                  composer, 0u, "CLIP", &bounds, &text) == QUANTAPDF_OK);
    }

    raster.struct_size = QUANTAPDF_COMPOSER_RASTER_V1_SIZE;
    raster.format = QUANTAPDF_COMPOSER_RASTER_RGB24;
    raster.width = 1u;
    raster.height = 1u;
    raster.stride = 3u;
    raster.pixels = green_pixel;
    raster.size = sizeof(green_pixel);
    CHECK(quantapdf_composer_add_raster(
              composer, &raster, &image_id) == QUANTAPDF_OK);
    image.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    image.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    image.graphics_state_id = clipped_half_id;
    {
        quantapdf_rect bounds = {20.0f, 130.0f, 100.0f, 200.0f};
        CHECK(quantapdf_composer_draw_image(
                  composer, 0u, image_id, &bounds, &image) == QUANTAPDF_OK);
    }

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "20 200 m 100 200 l 100 120 l 20 120 l h W n"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "W* n"));
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/GS1 gs") == 3u);
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/GS2 gs") == 1u);

    CHECK(quantapdf_output_save_file(first, COMPOSER_CLIP_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_CLIP_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 300 && height == 220 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);

    CHECK(pixel_near(pixels, stride, 30, 30, 255, 128, 128, 25));
    CHECK(pixel_near(pixels, stride, 10, 30, 255, 255, 255, 15));
    CHECK(pixel_near(pixels, stride, 150, 50, 0, 0, 255, 15));
    CHECK(pixel_near(pixels, stride, 215, 30, 0, 255, 0, 20));
    CHECK(pixel_near(pixels, stride, 250, 60, 255, 255, 255, 20));
    CHECK(pixel_near(pixels, stride, 50, 160, 255, 255, 255, 20));

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
    CHECK(test_validation_budget_and_dedup() == 0);
    CHECK(test_render_transform_scope_and_consumers() == 0);
    return 0;
}
