#include <quantapdf/quantapdf.h>

#include "backend/ttf_font.h"
#include "composer_test_helpers.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
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

static int add_page(quantapdf_composer* composer)
{
    quantapdf_composer_page_options page = {};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 300.0f;
    page.height_points = 200.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(
               composer, &page, &page_index) == QUANTAPDF_OK &&
        page_index == 0u;
}

static quantapdf_composer_glyph_run_options run_options(
    quantapdf_composer_font_id font_id)
{
    quantapdf_composer_glyph_run_options options = {};
    options.struct_size = QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_SIZE;
    options.font_id = font_id;
    options.font_size = 30.0f;
    options.argb = UINT32_C(0xff102030);
    return options;
}

static bool contains_bytes(
    char const* haystack,
    size_t haystack_size,
    char const* needle,
    size_t needle_size)
{
    if (needle_size == 0u)
        return true;
    if (haystack == nullptr || needle_size > haystack_size)
        return false;
    for (size_t i = 0u; i <= haystack_size - needle_size; ++i) {
        if (std::memcmp(haystack + i, needle, needle_size) == 0)
            return true;
    }
    return false;
}

static size_t count_nonwhite(
    unsigned char const* pixels,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1)
{
    size_t count = 0u;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            auto const* p =
                pixels + static_cast<size_t>(y) *
                    static_cast<size_t>(stride) +
                static_cast<size_t>(x) * 3u;
            if (p[0] < 240u || p[1] < 240u || p[2] < 240u)
                ++count;
        }
    }
    return count;
}

static int test_validation(
    std::vector<unsigned char> const& font_data,
    quantapdf::detail::ttf_font_face const& face)
{
    quantapdf_composer* composer = nullptr;
    quantapdf_composer_font_options font_options = {};
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_composer_glyph glyph = {};
    quantapdf_composer_glyph_run_options options = {};
    quantapdf_point origin = {20.0f, 60.0f};
    char const valid[] = "A";
    char const bad_utf8[] = {static_cast<char>(0xc0), 0};

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
              composer,
              font_data.data(),
              font_data.size(),
              &font_options,
              &font_id) == QUANTAPDF_OK);
    options = run_options(font_id);

    glyph.glyph_id = face.glyph_for('A');
    CHECK(glyph.glyph_id != 0u);
    glyph.x_advance = 600.0f;
    glyph.unicode_offset = 0u;
    glyph.unicode_length = 1u;

    CHECK(quantapdf_composer_draw_glyph_run(
              nullptr,
              0u,
              origin,
              &glyph,
              1u,
              valid,
              1u,
              &options) == QUANTAPDF_ERROR_ARGUMENT);

    options.font_id = font_id + 1u;
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              origin,
              &glyph,
              1u,
              valid,
              1u,
              &options) == QUANTAPDF_ERROR_ARGUMENT);
    options = run_options(font_id);

    glyph.glyph_id = face.num_glyphs;
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              origin,
              &glyph,
              1u,
              valid,
              1u,
              &options) == QUANTAPDF_ERROR_ARGUMENT);
    glyph.glyph_id = face.glyph_for('A');

    glyph.x_advance = NAN;
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              origin,
              &glyph,
              1u,
              valid,
              1u,
              &options) == QUANTAPDF_ERROR_ARGUMENT);
    glyph.x_advance = 600.0f;

    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              origin,
              &glyph,
              1u,
              bad_utf8,
              1u,
              &options) == QUANTAPDF_ERROR_FORMAT);

    char const accent[] = "e\xCC\x81";
    glyph.unicode_offset = 2u;
    glyph.unicode_length = 1u;
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              origin,
              &glyph,
              1u,
              accent,
              sizeof(accent) - 1u,
              &options) == QUANTAPDF_ERROR_ARGUMENT);

    glyph.unicode_offset = 0u;
    glyph.unicode_length = 0u;
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              origin,
              &glyph,
              1u,
              nullptr,
              0u,
              &options) == QUANTAPDF_OK);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_render_extract_and_determinism(
    std::vector<unsigned char> const& font_data,
    quantapdf::detail::ttf_font_face const& face)
{
    static char const av_text[] = "AV";
    static char const ligature_source[] = "e\xCC\x81";
    static char const combining_source[] = "o\xCC\x81";

    quantapdf_composer* composer = nullptr;
    quantapdf_composer_font_options font_options = {};
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_composer_glyph_run_options options = {};
    quantapdf_composer_glyph av[2] = {};
    quantapdf_composer_glyph ligature[1] = {};
    quantapdf_composer_glyph combining[2] = {};
    quantapdf_output* first = nullptr;
    quantapdf_output* second = nullptr;
    quantapdf_document* document = nullptr;
    quantapdf_page* page = nullptr;
    quantapdf_bitmap* bitmap = nullptr;
    quantapdf_render_options render = {};
    unsigned char const* first_data = nullptr;
    unsigned char const* second_data = nullptr;
    unsigned char const* pixels = nullptr;
    char* extracted = nullptr;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t extracted_size = 0u;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    uint16_t const gid_a = face.glyph_for('A');
    uint16_t const gid_v = face.glyph_for('V');
    uint16_t const gid_e_acute = face.glyph_for(UINT32_C(0x00e9));
    uint16_t const gid_o = face.glyph_for('o');
    uint16_t const gid_acute = face.glyph_for(UINT32_C(0x0301));
    CHECK(gid_a != 0u && gid_v != 0u && gid_e_acute != 0u &&
          gid_o != 0u && gid_acute != 0u);

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
              composer,
              font_data.data(),
              font_data.size(),
              &font_options,
              &font_id) == QUANTAPDF_OK);
    options = run_options(font_id);

    av[0].glyph_id = gid_a;
    av[0].x_advance = 650.0f;
    av[0].unicode_offset = 0u;
    av[0].unicode_length = 1u;
    av[1].glyph_id = gid_v;
    av[1].x_advance = 600.0f;
    av[1].x_offset = -50.0f;
    av[1].unicode_offset = 1u;
    av[1].unicode_length = 1u;
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              {30.0f, 60.0f},
              av,
              2u,
              av_text,
              sizeof(av_text) - 1u,
              &options) == QUANTAPDF_OK);

    ligature[0].glyph_id = gid_e_acute;
    ligature[0].x_advance = 600.0f;
    ligature[0].unicode_offset = 0u;
    ligature[0].unicode_length =
        static_cast<uint32_t>(sizeof(ligature_source) - 1u);
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              {30.0f, 105.0f},
              ligature,
              1u,
              ligature_source,
              sizeof(ligature_source) - 1u,
              &options) == QUANTAPDF_OK);

    combining[0].glyph_id = gid_o;
    combining[0].x_advance = 600.0f;
    combining[0].unicode_offset = 0u;
    combining[0].unicode_length =
        static_cast<uint32_t>(sizeof(combining_source) - 1u);
    combining[1].glyph_id = gid_acute;
    combining[1].x_advance = 0.0f;
    combining[1].y_advance = 0.0f;
    combining[1].x_offset = -500.0f;
    combining[1].y_offset = -250.0f;
    combining[1].unicode_offset =
        static_cast<uint32_t>(sizeof(combining_source) - 1u);
    combining[1].unicode_length = 0u;
    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              {30.0f, 150.0f},
              combining,
              2u,
              combining_source,
              sizeof(combining_source) - 1u,
              &options) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(std::memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "/GR1 30 Tf"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "1 0 0 1 30 140 Tm <0001> Tj 1 0 0 1 48 140 Tm <0002> Tj"));

    CHECK(quantapdf_output_save_file(
              first, COMPOSER_GLYPH_RUN_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_GLYPH_RUN_OUTPUT_PDF, nullptr, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(extracted != nullptr);
    CHECK(contains_bytes(
        extracted, extracted_size, "AV", 2u));
    CHECK(contains_bytes(
        extracted,
        extracted_size,
        ligature_source,
        sizeof(ligature_source) - 1u));
    CHECK(contains_bytes(
        extracted,
        extracted_size,
        combining_source,
        sizeof(combining_source) - 1u));
    quantapdf_free(extracted);
    extracted = nullptr;

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 300 && height == 200 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size == static_cast<size_t>(stride) *
            static_cast<size_t>(height));
    CHECK(count_nonwhite(pixels, stride, 20, 30, 160, 180) > 150u);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

int main()
{
    auto const font_data = read_file(ROBOTO_TTF);
    CHECK(font_data.size() > 1000u);

    quantapdf::detail::ttf_font_face face;
    CHECK(quantapdf::detail::ttf_font_face::parse(
              font_data.data(), font_data.size(), &face) == QUANTAPDF_OK);

    CHECK(test_validation(font_data, face) == 0);
    CHECK(test_render_extract_and_determinism(font_data, face) == 0);
    return 0;
}
