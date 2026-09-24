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
            fprintf(stderr, "CHECK failed at %s:%d: %s\\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.01f;
}

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

static int add_page(quantapdf_composer *composer)
{
    quantapdf_composer_page_options page = {0};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 300.0f;
    page.height_points = 400.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(composer, &page, &page_index) ==
            QUANTAPDF_OK &&
        page_index == 0u;
}

static int test_base14_measurement(void)
{
    static const char text[] = "alpha beta gamma delta";
    quantapdf_composer *composer = NULL;
    quantapdf_composer_text_options options = {0};
    quantapdf_composer_text_measurement one_line = {0};
    quantapdf_composer_text_measurement wrapped = {0};
    quantapdf_composer_text_measurement centered = {0};
    quantapdf_composer_text_measurement trimmed = {0};
    quantapdf_composer_text_measurement plain = {0};
    quantapdf_composer_text_measurement tabbed = {0};
    quantapdf_composer_text_measurement spaced = {0};
    quantapdf_composer_text_measurement no_wrap = {0};
    quantapdf_composer_text_measurement right = {0};
    quantapdf_rect bounds = {20.0f, 20.0f, 60.0f, 360.0f};
    quantapdf_output *measured_only = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0u;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    options.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    options.font_size = 12.0f;
    options.argb = UINT32_C(0xff202020);
    options.line_height_multiplier = 1.5f;
    options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    options.wrap = 1;

    one_line.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              composer, text, 1000.0f, &options, &one_line) == QUANTAPDF_OK);
    CHECK(one_line.line_count == 1u);
    CHECK(one_line.width > 0.0f);
    CHECK(close_float(one_line.height, options.font_size));

    wrapped.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              composer, text, 40.0f, &options, &wrapped) == QUANTAPDF_OK);
    CHECK(wrapped.line_count > 1u);
    CHECK(wrapped.width <= 40.01f);
    CHECK(close_float(
        wrapped.height,
        options.font_size +
            (float)(wrapped.line_count - 1u) *
                options.font_size * options.line_height_multiplier));

    options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER;
    centered.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              composer, text, 40.0f, &options, &centered) == QUANTAPDF_OK);
    CHECK(centered.line_count == wrapped.line_count);
    CHECK(close_float(centered.width, wrapped.width));
    CHECK(close_float(centered.height, wrapped.height));

    options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT;
    right.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              composer, text, 40.0f, &options, &right) == QUANTAPDF_OK);
    CHECK(right.line_count == wrapped.line_count);
    CHECK(close_float(right.width, wrapped.width));
    CHECK(close_float(right.height, wrapped.height));

    options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    plain.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    trimmed.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              composer, "word", 100.0f, &options, &plain) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_measure_text(
              composer, "word   ", 100.0f, &options, &trimmed) ==
          QUANTAPDF_OK);
    CHECK(close_float(plain.width, trimmed.width));

    tabbed.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    spaced.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              composer, "a\tb", 100.0f, &options, &tabbed) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_measure_text(
              composer, "a b", 100.0f, &options, &spaced) == QUANTAPDF_OK);
    CHECK(tabbed.line_count == spaced.line_count);
    CHECK(close_float(tabbed.width, spaced.width));
    CHECK(close_float(tabbed.height, spaced.height));

    options.wrap = 0;
    no_wrap.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              composer, text, 40.0f, &options, &no_wrap) == QUANTAPDF_OK);
    CHECK(no_wrap.line_count == 1u);
    CHECK(no_wrap.width > 40.0f);
    options.wrap = 1;

    CHECK(quantapdf_composer_finish(composer, &measured_only) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(measured_only, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_test_pdf_content_count(
              data, size, 0u, " Tj ET") == 0u);
    quantapdf_drop_output(measured_only);
    measured_only = NULL;

    wrapped.width = 123.0f;
    wrapped.height = 456.0f;
    wrapped.line_count = 789u;
    CHECK(quantapdf_composer_measure_text(
              composer, text, 0.0f, &options, &wrapped) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(wrapped.width == 0.0f && wrapped.height == 0.0f &&
          wrapped.line_count == 0u);

    wrapped.struct_size =
        QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_MIN_SIZE - 1u;
    wrapped.width = 123.0f;
    CHECK(quantapdf_composer_measure_text(
              composer, text, 40.0f, &options, &wrapped) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(wrapped.width == 123.0f);

    wrapped.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              composer, "\xF0\x9F\x98\x80", 40.0f, &options, &wrapped) ==
          QUANTAPDF_ERROR_FORMAT);

    CHECK(quantapdf_composer_measure_text(
              composer, "\xC3\x28", 40.0f, &options, &wrapped) ==
          QUANTAPDF_ERROR_FORMAT);

    CHECK(quantapdf_composer_measure_text(
              composer, text, 40.0f, &options, &wrapped) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_text(
              composer, 0u, text, &bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_test_pdf_content_count(
              data, size, 0u, " Tj ET") == wrapped.line_count);

    quantapdf_drop_output(measured_only);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_embedded_measurement(
    const unsigned char *font_data,
    size_t font_size)
{
    static const char text[] = "Hello Caf\xC3\xA9 \xCE\xA9 and more words";
    quantapdf_composer *composer = NULL;
    quantapdf_composer_font_options font_options = {0};
    quantapdf_composer_embedded_text_options options = {0};
    quantapdf_composer_text_measurement measured = {0};
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_rect bounds = {20.0f, 20.0f, 90.0f, 360.0f};
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0u;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
              composer, font_data, font_size, &font_options, &font_id) ==
          QUANTAPDF_OK);

    options.struct_size = QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE;
    options.font_id = font_id;
    options.font_size = 18.0f;
    options.argb = UINT32_C(0xff203040);
    options.line_height_multiplier = 1.25f;
    options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT;
    options.wrap = 1;

    measured.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_embedded_text(
              composer, text, 70.0f, &options, &measured) == QUANTAPDF_OK);
    CHECK(measured.line_count > 1u);
    CHECK(measured.width <= 70.01f);
    CHECK(close_float(
        measured.height,
        options.font_size +
            (float)(measured.line_count - 1u) *
                options.font_size * options.line_height_multiplier));

    CHECK(quantapdf_composer_measure_embedded_text(
              composer, "\xF0\x9F\x98\x80", 70.0f, &options, &measured) ==
          QUANTAPDF_ERROR_FORMAT);

    CHECK(quantapdf_composer_measure_embedded_text(
              composer, "\xC3\x28", 70.0f, &options, &measured) ==
          QUANTAPDF_ERROR_FORMAT);

    CHECK(quantapdf_composer_measure_embedded_text(
              composer, text, 70.0f, &options, &measured) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_embedded_text(
              composer, 0u, text, &bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_test_pdf_content_count(
              data, size, 0u, "/EF1 ") == measured.line_count);

    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    unsigned char *font_data = NULL;
    size_t font_size = 0u;

    CHECK(test_base14_measurement() == 0);
    CHECK(read_file(ROBOTO_TTF, &font_data, &font_size));
    CHECK(test_embedded_measurement(font_data, font_size) == 0);

    free(font_data);
    return 0;
}
