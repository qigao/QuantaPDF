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
    quantapdf_composer_font_options font_options = {0};
    quantapdf_composer_embedded_text_options text = {0};
    quantapdf_composer_glyph_run_options glyph_run = {0};
    quantapdf_composer_glyph shaped_glyph = {0};
    quantapdf_composer_path_options path = {0};
    quantapdf_composer_svg_options svg_options = {0};
    quantapdf_barcode_options barcode = {0};
    quantapdf_composer_outline_options outline = {0};

    quantapdf_composer_path_command rectangle[5] = {{0}};
    quantapdf_rect text_box = {24.0f, 24.0f, 280.0f, 70.0f};
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

    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
        composer, font_data, font_size, &font_options, &font_id));

    text.struct_size = QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE;
    text.font_id = font_id;
    text.font_size = 22.0f;
    text.argb = UINT32_C(0xff202020);
    text.line_height_multiplier = 1.2f;
    text.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text.wrap = 1;
    CHECK(quantapdf_composer_draw_embedded_text(
        composer,
        page0,
        "Installed Caf\xC3\xA9 \xCE\xA9",
        &text_box,
        &text));

    glyph_run.struct_size = QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_SIZE;
    glyph_run.font_id = font_id;
    glyph_run.font_size = 18.0f;
    glyph_run.argb = UINT32_C(0xff404040);
    shaped_glyph.glyph_id = 0u;
    shaped_glyph.x_advance = 500.0f;
    CHECK(quantapdf_composer_draw_glyph_run(
        composer,
        page1,
        (quantapdf_point){24.0f, 60.0f},
        &shaped_glyph,
        1u,
        NULL,
        0u,
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
    path.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    path.stroke = 1;
    path.stroke_argb = UINT32_C(0xff004080);
    path.stroke_width = 1.0f;
    path.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    path.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    path.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    path.miter_limit = 10.0f;
    CHECK(quantapdf_composer_draw_path(
        composer, page0, rectangle, 5u, &path));

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
