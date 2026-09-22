#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int read_file(
    const char *path,
    unsigned char **out_data,
    size_t *out_size)
{
    FILE *file;
    long length;
    unsigned char *data;

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

static int add_page(quantapdf_composer *composer)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 300.0f;
    page.height_points = 180.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(composer, &page, &page_index) ==
            QUANTAPDF_OK &&
        page_index == 0u;
}

static size_t count_nonwhite(
    const unsigned char *pixels,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1)
{
    size_t count = 0u;
    int x;
    int y;

    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            const unsigned char *p =
                pixels + (size_t)y * (size_t)stride + (size_t)x * 3u;
            if (p[0] < 240u || p[1] < 240u || p[2] < 240u)
                ++count;
        }
    }
    return count;
}

static int test_registration_contract(
    const unsigned char *font_data,
    size_t font_size)
{
    unsigned char bad_font[12] = {0};
    unsigned char cff_font[12] = {
        'O', 'T', 'T', 'O', 0, 0, 0, 0, 0, 0, 0, 0
    };
    quantapdf_composer *composer = NULL;
    quantapdf_composer_font_options options = {0};
    quantapdf_composer_font_id id = 99u;

    options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;

    CHECK(quantapdf_composer_add_font(
              NULL, font_data, font_size, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);

    id = 99u;
    CHECK(quantapdf_composer_add_font(
              composer, NULL, font_size, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);

    options.struct_size = 0u;
    id = 99u;
    CHECK(quantapdf_composer_add_font(
              composer, font_data, font_size, &options, &id) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(id == 0u);
    options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;

    id = 99u;
    CHECK(quantapdf_composer_add_font(
              composer, bad_font, sizeof(bad_font), &options, &id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0u);

    id = 99u;
    CHECK(quantapdf_composer_add_font(
              composer, cff_font, sizeof(cff_font), &options, &id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0u);

    CHECK(quantapdf_composer_add_font(
              composer, font_data, font_size, &options, &id) ==
          QUANTAPDF_OK);
    CHECK(id == 1u);

    id = 99u;
    CHECK(quantapdf_composer_add_font(
              composer, font_data, font_size, &options, &id) ==
          QUANTAPDF_OK);
    CHECK(id == 1u);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_resource_limit(
    const unsigned char *font_data,
    size_t font_size)
{
    quantapdf_composer_options composer_options = {0};
    quantapdf_composer_font_options font_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_composer_font_id id = 99u;

    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_resource_bytes = font_size - 1u;
    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_create(&composer_options, &composer) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_font(
              composer, font_data, font_size, &font_options, &id) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0u);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_embedded_text(
    const unsigned char *font_data,
    size_t font_size)
{
    static const char expected[] = "Hello Caf\xC3\xA9 \xCE\xA9";
    static const char missing[] = "\xF0\x9F\x98\x80";
    quantapdf_composer_options composer_options = {0};
    quantapdf_composer_font_options font_options = {0};
    quantapdf_composer_embedded_text_options text_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render = {0};
    quantapdf_rect bounds = {20.0f, 20.0f, 280.0f, 100.0f};
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const unsigned char *pixels = NULL;
    char *extracted = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t extracted_size = 0u;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_resource_bytes = font_size + 4096u;
    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;

    CHECK(quantapdf_composer_create(&composer_options, &composer) ==
          QUANTAPDF_OK);
    CHECK(add_page(composer));
    CHECK(quantapdf_composer_add_font(
              composer, font_data, font_size, &font_options, &font_id) ==
          QUANTAPDF_OK);
    CHECK(font_id == 1u);

    text_options.struct_size =
        QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE;
    text_options.font_id = font_id;
    text_options.font_size = 24.0f;
    text_options.argb = UINT32_C(0xff102030);
    text_options.line_height_multiplier = 1.2f;
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text_options.wrap = 1;

    CHECK(quantapdf_composer_draw_embedded_text(
              NULL, 0u, expected, &bounds, &text_options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    text_options.font_id = 2u;
    CHECK(quantapdf_composer_draw_embedded_text(
              composer, 0u, expected, &bounds, &text_options) ==
          QUANTAPDF_ERROR_ARGUMENT);
    text_options.font_id = font_id;
    CHECK(quantapdf_composer_draw_embedded_text(
              composer, 0u, missing, &bounds, &text_options) ==
          QUANTAPDF_ERROR_FORMAT);

    CHECK(quantapdf_composer_draw_embedded_text(
              composer, 0u, expected, &bounds, &text_options) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);
    CHECK(first_size < font_size);

    CHECK(quantapdf_output_save_file(first, COMPOSER_FONT_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(COMPOSER_FONT_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(extracted != NULL);
    CHECK(extracted_size >= strlen(expected));
    CHECK(memcmp(extracted, expected, strlen(expected)) == 0);
    quantapdf_free(extracted);
    extracted = NULL;

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 300 && height == 180 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);
    CHECK(count_nonwhite(pixels, stride, 15, 15, 285, 110) > 100u);

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

    CHECK(read_file(ROBOTO_TTF, &font_data, &font_size));
    CHECK(font_size > 1000u);

    CHECK(test_registration_contract(font_data, font_size) == 0);
    CHECK(test_resource_limit(font_data, font_size) == 0);
    CHECK(test_embedded_text(font_data, font_size) == 0);

    free(font_data);
    return 0;
}
