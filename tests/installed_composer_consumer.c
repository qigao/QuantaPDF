#define _CRT_SECURE_NO_WARNINGS 1

#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(call)                                                            \
    do {                                                                       \
        quantapdf_status const status_ = (call);                               \
        if (status_ != QUANTAPDF_OK) {                                         \
            fprintf(stderr, "%s failed: %s\\n", #call,                       \
                    quantapdf_status_string(status_));                         \
            goto fail;                                                         \
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

static quantapdf_status installed_form_builder(
    quantapdf_composer *composer,
    size_t page_index,
    void *user_data)
{
    quantapdf_composer_text_options text = {0};
    quantapdf_rect bounds = {4.0f, 4.0f, 76.0f, 28.0f};

    (void)user_data;
    text.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    text.font = QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD;
    text.font_size = 12.0f;
    text.argb = UINT32_C(0xff202020);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text.wrap = 0;
    return quantapdf_composer_draw_text(
        composer, page_index, "Installed Form", &bounds, &text);
}

int main(int argc, char **argv)
{
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    unsigned char *font_data = NULL;
    size_t font_size = 0u;
    size_t page0 = SIZE_MAX;
    size_t page1 = SIZE_MAX;
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_composer_outline_id outline_id = 0u;

    quantapdf_composer_page_options page = {0};
    quantapdf_composer_form_options form_options = {0};
    quantapdf_composer_form_draw_options form_draw = {0};
    quantapdf_composer_form_id form_id = 0u;
    quantapdf_composer_font_options font_options = {0};
    quantapdf_composer_graphics_state_options graphics_state = {0};
    quantapdf_composer_graphics_state_id graphics_state_id = 0u;
    quantapdf_composer_clip_options clip_options = {0};
    quantapdf_composer_clip_id clip_id = 0u;
    quantapdf_composer_path_command clip_rectangle[5] = {{0}};
    quantapdf_composer_linear_gradient_options linear_gradient = {0};
    quantapdf_composer_radial_gradient_options radial_gradient = {0};
    quantapdf_composer_gradient_stop gradient_stops[2] = {
        {0.0f, UINT32_C(0xffff0000)},
        {1.0f, UINT32_C(0xff0000ff)}
    };
    quantapdf_composer_paint_id linear_paint_id = 0u;
    quantapdf_composer_paint_id radial_paint_id = 0u;
    quantapdf_composer_raster raster = {0};
    quantapdf_composer_image_options raster_draw = {0};
    quantapdf_composer_image_id raster_id = 0u;
    unsigned char raster_pixels[6] = {
        255u, 0u, 0u,
        0u, 0u, 255u
    };
    quantapdf_composer_text_options base_text = {0};
    quantapdf_composer_embedded_text_options text = {0};
    quantapdf_composer_text_measurement measurement = {0};
    quantapdf_affine_transform identity = {
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    quantapdf_affine_transform rotate90 = {
        0.0f, 1.0f, -1.0f, 0.0f, 290.0f, 70.0f
    };
    quantapdf_composer_glyph_run_options glyph_run = {0};
    quantapdf_composer_glyph shaped_glyph = {0};
    quantapdf_composer_path_options path = {0};
    quantapdf_composer_dash_pattern dash = {0};
    float dash_lengths[2] = {6.0f, 3.0f};
    quantapdf_composer_svg_options svg_options = {0};
    quantapdf_barcode_options barcode = {0};
    quantapdf_composer_outline_options outline = {0};

    quantapdf_composer_path_command rectangle[5] = {{0}};
    quantapdf_rect text_box = {24.0f, 24.0f, 280.0f, 70.0f};
    quantapdf_rect raster_box = {180.0f, 30.0f, 280.0f, 65.0f};
    quantapdf_rect transformed_text_box = {0.0f, 0.0f, 90.0f, 30.0f};
    quantapdf_rect barcode_box = {24.0f, 90.0f, 180.0f, 130.0f};
    quantapdf_rect uri_box = {24.0f, 140.0f, 120.0f, 160.0f};
    quantapdf_rect page_link_box = {140.0f, 140.0f, 260.0f, 160.0f};
    quantapdf_rect svg_box = {180.0f, 80.0f, 280.0f, 160.0f};
    quantapdf_point target = {24.0f, 24.0f};
    static const unsigned char svg_data[] =
        "<svg viewBox=\"0 0 10 8\">"
        "<g fill=\"#00a0ff\" stroke=\"#202020\" stroke-width=\"0.5\">"
        "<rect x=\"1\" y=\"1\" width=\"8\" height=\"6\"/>"
        "<path d=\"M2 6 Q5 1 8 6\" fill=\"none\"/>"
        "</g></svg>";

    if (argc != 3) {
        fprintf(stderr, "usage: %s <font.ttf> <output.pdf>\\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!read_file(argv[1], &font_data, &font_size)) {
        fprintf(stderr, "unable to read font: %s\\n", argv[1]);
        return EXIT_FAILURE;
    }

    CHECK(quantapdf_composer_create(NULL, &composer));

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 300.0f;
    page.height_points = 180.0f;
    page.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(composer, &page, &page0));
    CHECK(quantapdf_composer_add_page(composer, &page, &page1));

    form_options.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    form_options.width_points = 80.0f;
    form_options.height_points = 32.0f;
    CHECK(quantapdf_composer_add_form(
        composer,
        &form_options,
        installed_form_builder,
        NULL,
        &form_id));
    if (form_id == 0u) {
        fprintf(stderr, "form ID was not published\n");
        goto fail;
    }
    form_draw.struct_size =
        QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
    {
        quantapdf_affine_transform form_transform = {
            1.0f, 0.0f, 0.0f, 1.0f, 200.0f, 135.0f
        };
        CHECK(quantapdf_composer_draw_form(
            composer,
            page1,
            form_id,
            &form_transform,
            &form_draw));
    }

    clip_rectangle[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    clip_rectangle[0].point1 = (quantapdf_point){0.0f, 0.0f};
    clip_rectangle[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    clip_rectangle[1].point1 = (quantapdf_point){300.0f, 0.0f};
    clip_rectangle[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    clip_rectangle[2].point1 = (quantapdf_point){300.0f, 180.0f};
    clip_rectangle[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    clip_rectangle[3].point1 = (quantapdf_point){0.0f, 180.0f};
    clip_rectangle[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    clip_options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    clip_options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    CHECK(quantapdf_composer_add_clip_path(
        composer, clip_rectangle, 5u, &clip_options, &clip_id));
    if (clip_id == 0u) {
        fprintf(stderr, "clip ID was not published\n");
        goto fail;
    }

    graphics_state.struct_size =
        QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
    graphics_state.fill_alpha = 0.75f;
    graphics_state.stroke_alpha = 0.75f;
    graphics_state.blend_mode = QUANTAPDF_COMPOSER_BLEND_MULTIPLY;
    graphics_state.clip_id = clip_id;
    CHECK(quantapdf_composer_add_graphics_state(
        composer, &graphics_state, &graphics_state_id));
    if (graphics_state_id == 0u) {
        fprintf(stderr, "graphics-state ID was not published\n");
        goto fail;
    }

    linear_gradient.struct_size =
        QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE;
    linear_gradient.start = (quantapdf_point){20.0f, 20.0f};
    linear_gradient.end = (quantapdf_point){280.0f, 20.0f};
    linear_gradient.stops = gradient_stops;
    linear_gradient.stop_count = 2u;
    CHECK(quantapdf_composer_add_linear_gradient(
        composer, &linear_gradient, &linear_paint_id));
    if (linear_paint_id == 0u) {
        fprintf(stderr, "linear paint ID was not published\n");
        goto fail;
    }

    radial_gradient.struct_size =
        QUANTAPDF_COMPOSER_RADIAL_GRADIENT_OPTIONS_V1_SIZE;
    radial_gradient.start_center = (quantapdf_point){150.0f, 90.0f};
    radial_gradient.start_radius = 0.0f;
    radial_gradient.end_center = (quantapdf_point){150.0f, 90.0f};
    radial_gradient.end_radius = 40.0f;
    radial_gradient.stops = gradient_stops;
    radial_gradient.stop_count = 2u;
    CHECK(quantapdf_composer_add_radial_gradient(
        composer, &radial_gradient, &radial_paint_id));
    if (radial_paint_id == 0u) {
        fprintf(stderr, "radial paint ID was not published\n");
        goto fail;
    }

    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
        composer, font_data, font_size, &font_options, &font_id));

    raster.struct_size = QUANTAPDF_COMPOSER_RASTER_V1_SIZE;
    raster.format = QUANTAPDF_COMPOSER_RASTER_RGB24;
    raster.width = 2u;
    raster.height = 1u;
    raster.stride = 6u;
    raster.pixels = raster_pixels;
    raster.size = sizeof(raster_pixels);
    CHECK(quantapdf_composer_add_raster(
        composer, &raster, &raster_id));
    raster_draw.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    raster_draw.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    raster_draw.graphics_state_id = graphics_state_id;
    CHECK(quantapdf_composer_draw_image(
        composer, page1, raster_id, &raster_box, &raster_draw));

    base_text.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    base_text.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    base_text.font_size = 12.0f;
    base_text.argb = UINT32_C(0xff202020);
    base_text.line_height_multiplier = 1.2f;
    base_text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    base_text.wrap = 1;
    measurement.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
        composer,
        "Installed package measurement",
        120.0f,
        &base_text,
        &measurement));
    if (measurement.line_count == 0u ||
        measurement.width <= 0.0f ||
        measurement.height <= 0.0f) {
        fprintf(stderr, "Base-14 measurement was empty\n");
        goto fail;
    }

    CHECK(quantapdf_composer_draw_text_transformed(
        composer,
        page1,
        "Affine",
        &transformed_text_box,
        &rotate90,
        &base_text));

    text.struct_size = QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE;
    text.font_id = font_id;
    text.font_size = 22.0f;
    text.argb = UINT32_C(0xff202020);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text.wrap = 1;
    measurement.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_embedded_text(
        composer,
        "Installed Caf\xC3\xA9 \xCE\xA9",
        text_box.x1 - text_box.x0,
        &text,
        &measurement));
    if (measurement.line_count == 0u ||
        measurement.width <= 0.0f ||
        measurement.height <= 0.0f) {
        fprintf(stderr, "embedded measurement was empty\n");
        goto fail;
    }

    CHECK(quantapdf_composer_draw_embedded_text_transformed(
        composer,
        page0,
        "Installed Caf\xC3\xA9 \xCE\xA9",
        &text_box,
        &identity,
        &text));

    glyph_run.struct_size = QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_SIZE;
    glyph_run.font_id = font_id;
    glyph_run.font_size = 18.0f;
    glyph_run.argb = UINT32_C(0xff404040);
    shaped_glyph.glyph_id = 0u;
    shaped_glyph.x_advance = 500.0f;
    CHECK(quantapdf_composer_draw_glyph_run_transformed(
        composer,
        page1,
        (quantapdf_point){24.0f, 60.0f},
        &shaped_glyph,
        1u,
        NULL,
        0u,
        &identity,
        &glyph_run));

    rectangle[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    rectangle[0].point1 = (quantapdf_point){20.0f, 20.0f};
    rectangle[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    rectangle[1].point1 = (quantapdf_point){280.0f, 20.0f};
    rectangle[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    rectangle[2].point1 = (quantapdf_point){280.0f, 170.0f};
    rectangle[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    rectangle[3].point1 = (quantapdf_point){20.0f, 170.0f};
    rectangle[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    path.stroke = 1;
    path.fill = 1;
    path.fill_argb = UINT32_C(0xff000000);
    path.fill_paint_id = linear_paint_id;
    path.stroke_argb = UINT32_C(0xff004080);
    path.stroke_width = 1.0f;
    path.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    path.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    path.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    path.miter_limit = 10.0f;
    dash.struct_size = QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE;
    dash.lengths = dash_lengths;
    dash.length_count = 2u;
    dash.phase = 1.0f;
    CHECK(quantapdf_composer_draw_path_dashed(
        composer, page0, rectangle, 5u, &path, &dash));

    svg_options.struct_size = QUANTAPDF_COMPOSER_SVG_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_draw_svg(
        composer,
        page1,
        svg_data,
        sizeof(svg_data) - 1u,
        &svg_box,
        &svg_options));

    barcode.struct_size = QUANTAPDF_BARCODE_OPTIONS_V1_SIZE;
    barcode.argb = UINT32_C(0xff000000);
    CHECK(quantapdf_composer_draw_barcode(
        composer,
        page0,
        QUANTAPDF_BARCODE_CODE_128B,
        "QUANTAPDF",
        &barcode_box,
        &barcode));

    CHECK(quantapdf_composer_add_uri_link(
        composer, page0, &uri_box, "https://example.com/quantapdf"));
    CHECK(quantapdf_composer_add_page_link(
        composer, page0, &page_link_box, page1, target));

    outline.struct_size = QUANTAPDF_COMPOSER_OUTLINE_OPTIONS_V1_SIZE;
    outline.target_page_index = page0;
    outline.target = target;
    outline.is_open = 1;
    CHECK(quantapdf_composer_add_outline(
        composer, "Installed package smoke", &outline, &outline_id));
    if (outline_id == 0u) {
        fprintf(stderr, "outline ID was not published\\n");
        goto fail;
    }

    CHECK(quantapdf_composer_finish(composer, &output));
    CHECK(quantapdf_output_save_file(output, argv[2]));

    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    free(font_data);
    return EXIT_SUCCESS;

fail:
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    free(font_data);
    return EXIT_FAILURE;
}
