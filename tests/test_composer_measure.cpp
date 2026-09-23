#include <quantapdf/quantapdf.h>

#include "composer_test_helpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(                                                      \
                stderr, "CHECK failed at %s:%d: %s\n",                        \
                __FILE__, __LINE__, #expr);                                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static std::vector<unsigned char> read_file(char const* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

static bool close_float(float a, float b, float tolerance = 0.01f)
{
    return std::fabs(a - b) <= tolerance;
}

static quantapdf_composer_text_options base_options()
{
    quantapdf_composer_text_options options = {};
    options.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    options.font_size = 12.0f;
    options.argb = UINT32_C(0xff202020);
    options.line_height_multiplier = 1.2f;
    options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    options.wrap = 1;
    return options;
}

static int add_page(quantapdf_composer* composer)
{
    quantapdf_composer_page_options page = {};
    size_t index = SIZE_MAX;
    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 300.0f;
    page.height_points = 300.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(
               composer, &page, &index) == QUANTAPDF_OK &&
        index == 0u;
}

static int test_base14_measurement()
{
    quantapdf_composer_text_measurement measure = {};
    auto options = base_options();

    measure.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              "Hello", 1000.0f, &options, &measure) == QUANTAPDF_OK);
    CHECK(close_float(measure.width_points, 27.336f));
    CHECK(close_float(measure.height_points, 12.0f));
    CHECK(measure.line_count == 1u);

    CHECK(quantapdf_composer_measure_text(
              "Hello\nWorld", 1000.0f, &options, &measure) == QUANTAPDF_OK);
    CHECK(close_float(measure.width_points, 31.332f));
    CHECK(close_float(measure.height_points, 26.4f));
    CHECK(measure.line_count == 2u);

    quantapdf_composer_text_measurement trailing = {};
    trailing.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              "Hello   ", 1000.0f, &options, &trailing) == QUANTAPDF_OK);
    CHECK(close_float(trailing.width_points, 27.336f));

    CHECK(quantapdf_composer_measure_text(
              "one two three four five",
              45.0f,
              &options,
              &measure) == QUANTAPDF_OK);
    CHECK(measure.line_count > 1u);
    CHECK(measure.width_points <= 45.01f);
    CHECK(close_float(
        measure.height_points,
        options.font_size +
            static_cast<float>(measure.line_count - 1u) *
                options.font_size * options.line_height_multiplier));

    auto center = options;
    center.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER;
    quantapdf_composer_text_measurement center_measure = {};
    center_measure.struct_size =
        QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              "one two three four five",
              45.0f,
              &center,
              &center_measure) == QUANTAPDF_OK);
    CHECK(center_measure.line_count == measure.line_count);
    CHECK(close_float(center_measure.width_points, measure.width_points));
    CHECK(close_float(center_measure.height_points, measure.height_points));

    quantapdf_composer_text_measurement bad = {};
    bad.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              "Hello", 0.0f, &options, &bad) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bad.width_points == 0.0f && bad.height_points == 0.0f &&
          bad.line_count == 0u);
    bad.struct_size =
        QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_MIN_SIZE - 1u;
    CHECK(quantapdf_composer_measure_text(
              "Hello", 100.0f, &options, &bad) ==
          QUANTAPDF_ERROR_ARGUMENT);

    return 0;
}

static int test_base14_measure_matches_draw()
{
    static char const text[] = "one two three four five";
    quantapdf_composer* composer = nullptr;
    quantapdf_output* output = nullptr;
    unsigned char const* data = nullptr;
    size_t size = 0u;
    auto options = base_options();
    quantapdf_composer_text_measurement measure = {};
    quantapdf_rect bounds = {};

    measure.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_text(
              text, 45.0f, &options, &measure) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    bounds.x0 = 20.0f;
    bounds.y0 = 20.0f;
    bounds.x1 = 65.0f;
    bounds.y1 = bounds.y0 + measure.height_points;
    CHECK(quantapdf_composer_draw_text(
              composer, 0u, text, &bounds, &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_test_pdf_content_count(
              data, size, 0u, " Tj ET") == measure.line_count);

    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int setup_font_composer(
    std::vector<unsigned char> const& font,
    quantapdf_composer** out_composer,
    quantapdf_composer_font_id* out_font_id)
{
    quantapdf_composer_font_options options = {};
    *out_composer = nullptr;
    *out_font_id = 0u;
    if (quantapdf_composer_create(nullptr, out_composer) != QUANTAPDF_OK)
        return 0;
    if (!add_page(*out_composer))
        return 0;
    options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    return quantapdf_composer_add_font(
               *out_composer,
               font.data(),
               font.size(),
               &options,
               out_font_id) == QUANTAPDF_OK;
}

static quantapdf_composer_embedded_text_options embedded_options(
    quantapdf_composer_font_id font_id)
{
    quantapdf_composer_embedded_text_options options = {};
    options.struct_size =
        QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE;
    options.font_id = font_id;
    options.font_size = 18.0f;
    options.argb = UINT32_C(0xff102030);
    options.line_height_multiplier = 1.25f;
    options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    options.wrap = 1;
    return options;
}

static int test_embedded_measurement_and_state()
{
    static char const text[] = "Caf\xC3\xA9 \xCE\xA9 one two three";
    static char const missing[] = "\xF0\x9F\x98\x80";
    auto const font = read_file(ROBOTO_TTF);
    CHECK(font.size() > 1000u);

    quantapdf_composer* measured = nullptr;
    quantapdf_composer* clean = nullptr;
    quantapdf_composer_font_id measured_font = 0u;
    quantapdf_composer_font_id clean_font = 0u;
    quantapdf_output* measured_empty_output = nullptr;
    quantapdf_output* clean_empty_output = nullptr;
    quantapdf_output* drawn_output = nullptr;
    unsigned char const* measured_empty_data = nullptr;
    unsigned char const* clean_empty_data = nullptr;
    unsigned char const* drawn_data = nullptr;
    size_t measured_empty_size = 0u;
    size_t clean_empty_size = 0u;
    size_t drawn_size = 0u;

    CHECK(setup_font_composer(font, &measured, &measured_font));
    CHECK(setup_font_composer(font, &clean, &clean_font));
    CHECK(measured_font == 1u && clean_font == 1u);

    auto options = embedded_options(measured_font);
    quantapdf_composer_text_measurement measure = {};
    measure.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_embedded_text(
              measured,
              text,
              75.0f,
              &options,
              &measure) == QUANTAPDF_OK);
    CHECK(measure.line_count > 1u);
    CHECK(measure.width_points <= 75.01f);
    CHECK(close_float(
        measure.height_points,
        options.font_size +
            static_cast<float>(measure.line_count - 1u) *
                options.font_size * options.line_height_multiplier));

    auto right = options;
    right.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT;
    quantapdf_composer_text_measurement right_measure = {};
    right_measure.struct_size =
        QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_embedded_text(
              measured,
              text,
              75.0f,
              &right,
              &right_measure) == QUANTAPDF_OK);
    CHECK(right_measure.line_count == measure.line_count);
    CHECK(close_float(right_measure.width_points, measure.width_points));
    CHECK(close_float(right_measure.height_points, measure.height_points));

    quantapdf_composer_text_measurement failed = {};
    failed.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_embedded_text(
              measured,
              missing,
              75.0f,
              &options,
              &failed) == QUANTAPDF_ERROR_FORMAT);
    CHECK(failed.width_points == 0.0f &&
          failed.height_points == 0.0f &&
          failed.line_count == 0u);

    CHECK(quantapdf_composer_finish(
              measured, &measured_empty_output) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(
              clean, &clean_empty_output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              measured_empty_output,
              &measured_empty_data,
              &measured_empty_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              clean_empty_output,
              &clean_empty_data,
              &clean_empty_size) == QUANTAPDF_OK);
    CHECK(measured_empty_size == clean_empty_size);
    CHECK(std::memcmp(
              measured_empty_data,
              clean_empty_data,
              measured_empty_size) == 0);

    quantapdf_rect bounds = {
        20.0f,
        20.0f,
        95.0f,
        20.0f + measure.height_points};
    CHECK(quantapdf_composer_draw_embedded_text(
              measured,
              0u,
              text,
              &bounds,
              &options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(
              measured, &drawn_output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              drawn_output, &drawn_data, &drawn_size) == QUANTAPDF_OK);
    CHECK(quantapdf_test_pdf_content_count(
              drawn_data,
              drawn_size,
              0u,
              " Tj ET") == measure.line_count);

    quantapdf_drop_output(drawn_output);
    quantapdf_drop_output(clean_empty_output);
    quantapdf_drop_output(measured_empty_output);
    quantapdf_drop_composer(clean);
    quantapdf_drop_composer(measured);
    return 0;
}

static int test_cff2_measurement()
{
    auto const font = read_file(SOURCE_SANS_CFF2);
    CHECK(font.size() > 1000u);

    quantapdf_composer* composer = nullptr;
    quantapdf_composer_font_id font_id = 0u;
    CHECK(setup_font_composer(font, &composer, &font_id));

    auto options = embedded_options(font_id);
    quantapdf_composer_text_measurement measure = {};
    measure.struct_size = QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE;
    CHECK(quantapdf_composer_measure_embedded_text(
              composer,
              "CFF2 Caf\xC3\xA9",
              200.0f,
              &options,
              &measure) == QUANTAPDF_OK);
    CHECK(measure.line_count == 1u);
    CHECK(measure.width_points > 0.0f);
    CHECK(measure.width_points < 200.0f);
    CHECK(close_float(measure.height_points, options.font_size));

    quantapdf_drop_composer(composer);
    return 0;
}

int main()
{
    CHECK(test_base14_measurement() == 0);
    CHECK(test_base14_measure_matches_draw() == 0);
    CHECK(test_embedded_measurement_and_state() == 0);
    CHECK(test_cff2_measurement() == 0);
    return 0;
}
