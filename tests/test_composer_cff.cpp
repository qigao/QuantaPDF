#include <quantapdf/quantapdf.h>

#include "backend/ttf_font.h"
#include "composer_test_helpers.h"

#include <cstdint>
#include <cstdio>
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

static int add_page(quantapdf_composer* composer)
{
    quantapdf_composer_page_options page = {};
    size_t page_index = SIZE_MAX;
    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 320.0f;
    page.height_points = 180.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(
               composer, &page, &page_index) == QUANTAPDF_OK &&
        page_index == 0u;
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

static int exercise_font(
    char const* input_path,
    char const* output_path,
    quantapdf::detail::sfnt_outline_kind expected_kind)
{
    static char const direct_text[] = "CFF Caf\xC3\xA9 \xCE\xA9";
    static char const run_text[] = "AV";

    auto const data = read_file(input_path);
    CHECK(data.size() > 1000u);

    quantapdf::detail::ttf_font_face face;
    CHECK(quantapdf::detail::ttf_font_face::parse(
              data.data(), data.size(), &face) == QUANTAPDF_OK);
    CHECK(face.outline_kind == expected_kind);

    uint16_t const gid_a = face.glyph_for('A');
    uint16_t const gid_v = face.glyph_for('V');
    CHECK(gid_a != 0u && gid_v != 0u);

    quantapdf_composer* composer = nullptr;
    quantapdf_composer_font_options font_options = {};
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_composer_embedded_text_options text_options = {};
    quantapdf_composer_glyph_run_options run_options = {};
    quantapdf_composer_glyph glyphs[2] = {};
    quantapdf_output* first = nullptr;
    quantapdf_output* second = nullptr;
    quantapdf_document* document = nullptr;
    quantapdf_page* page = nullptr;
    quantapdf_bitmap* bitmap = nullptr;
    quantapdf_render_options render = {};
    quantapdf_rect bounds = {20.0f, 20.0f, 300.0f, 75.0f};
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

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
              composer,
              data.data(),
              data.size(),
              &font_options,
              &font_id) == QUANTAPDF_OK);
    CHECK(font_id == 1u);

    text_options.struct_size =
        QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE;
    text_options.font_id = font_id;
    text_options.font_size = 24.0f;
    text_options.argb = UINT32_C(0xff202020);
    text_options.line_height_multiplier = 1.2f;
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text_options.wrap = 1;
    CHECK(quantapdf_composer_draw_embedded_text(
              composer,
              0u,
              direct_text,
              &bounds,
              &text_options) == QUANTAPDF_OK);

    run_options.struct_size =
        QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_SIZE;
    run_options.font_id = font_id;
    run_options.font_size = 28.0f;
    run_options.argb = UINT32_C(0xff204060);

    glyphs[0].glyph_id = gid_a;
    glyphs[0].x_advance = 650.0f;
    glyphs[0].unicode_offset = 0u;
    glyphs[0].unicode_length = 1u;
    glyphs[1].glyph_id = gid_v;
    glyphs[1].x_advance = 600.0f;
    glyphs[1].x_offset = -50.0f;
    glyphs[1].unicode_offset = 1u;
    glyphs[1].unicode_length = 1u;

    CHECK(quantapdf_composer_draw_glyph_run(
              composer,
              0u,
              {20.0f, 125.0f},
              glyphs,
              2u,
              run_text,
              sizeof(run_text) - 1u,
              &run_options) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(std::memcmp(first_data, second_data, first_size) == 0);
    CHECK(first_size > 8u);
    CHECK(std::memcmp(first_data, "%PDF-1.6", 8u) == 0);

    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "/EF1 24 Tf"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "/GR1 28 Tf"));

    CHECK(quantapdf_output_save_file(first, output_path) == QUANTAPDF_OK);
    CHECK(quantapdf_open(output_path, nullptr, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(extracted != nullptr);
    CHECK(contains_bytes(
        extracted,
        extracted_size,
        direct_text,
        sizeof(direct_text) - 1u));
    CHECK(contains_bytes(
        extracted,
        extracted_size,
        run_text,
        sizeof(run_text) - 1u));
    quantapdf_free(extracted);
    extracted = nullptr;

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 320 && height == 180 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size ==
          static_cast<size_t>(stride) * static_cast<size_t>(height));
    CHECK(count_nonwhite(pixels, stride, 10, 10, 310, 160) > 150u);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_malformed_cff_header()
{
    auto data = read_file(SOURCE_SANS_CFF);
    CHECK(data.size() > 1000u);

    bool patched = false;
    uint16_t const table_count =
        static_cast<uint16_t>((data[4] << 8u) | data[5]);
    for (uint16_t i = 0u; i < table_count; ++i) {
        size_t const at = 12u + static_cast<size_t>(i) * 16u;
        if (at + 16u > data.size())
            return 1;
        if (std::memcmp(data.data() + at, "CFF ", 4u) == 0) {
            uint32_t const offset =
                (static_cast<uint32_t>(data[at + 8u]) << 24u) |
                (static_cast<uint32_t>(data[at + 9u]) << 16u) |
                (static_cast<uint32_t>(data[at + 10u]) << 8u) |
                static_cast<uint32_t>(data[at + 11u]);
            CHECK(offset < data.size());
            data[offset] = 9u;
            patched = true;
            break;
        }
    }
    CHECK(patched);

    quantapdf_composer* composer = nullptr;
    quantapdf_composer_font_options options = {};
    quantapdf_composer_font_id id = 99u;
    options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_font(
              composer,
              data.data(),
              data.size(),
              &options,
              &id) == QUANTAPDF_ERROR_FORMAT);
    CHECK(id == 0u);
    quantapdf_drop_composer(composer);
    return 0;
}

int main()
{
    CHECK(exercise_font(
              SOURCE_SANS_CFF,
              CFF_OUTPUT_PDF,
              quantapdf::detail::sfnt_outline_kind::cff) == 0);
    CHECK(exercise_font(
              SOURCE_SANS_CFF2,
              CFF2_OUTPUT_PDF,
              quantapdf::detail::sfnt_outline_kind::cff2) == 0);
    CHECK(test_malformed_cff_header() == 0);
    return 0;
}
