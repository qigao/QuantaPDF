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
                stderr, "CHECK failed at %s:%d: %s\\n",                     \
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
    if (haystack == nullptr || needle == nullptr || needle_size > haystack_size)
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
    int width,
    int height)
{
    size_t count = 0u;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
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

static int add_page(quantapdf_composer* composer, size_t expected_index)
{
    quantapdf_composer_page_options page = {};
    size_t page_index = SIZE_MAX;

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 300.0f;
    page.height_points = 300.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(
               composer, &page, &page_index) == QUANTAPDF_OK &&
        page_index == expected_index;
}

static quantapdf_composer_text_options base_options(void)
{
    quantapdf_composer_text_options options = {};
    options.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    options.font_size = 18.0f;
    options.argb = UINT32_C(0xff102030);
    options.line_height_multiplier = 1.2f;
    options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    options.wrap = 0;
    return options;
}

static int make_identity_base_output(
    int transformed,
    std::vector<unsigned char>* out)
{
    quantapdf_composer* composer = nullptr;
    quantapdf_output* output = nullptr;
    quantapdf_composer_text_options options = base_options();
    quantapdf_rect bounds = {30.0f, 40.0f, 220.0f, 90.0f};
    quantapdf_affine_transform identity = {
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    unsigned char const* data = nullptr;
    size_t size = 0u;

    if (out == nullptr)
        return 0;
    out->clear();
    if (quantapdf_composer_create(nullptr, &composer) != QUANTAPDF_OK ||
        !add_page(composer, 0u))
        goto fail;

    if (transformed) {
        if (quantapdf_composer_draw_text_transformed(
                composer,
                0u,
                "Identity transform",
                &bounds,
                &identity,
                &options) != QUANTAPDF_OK)
            goto fail;
    } else {
        if (quantapdf_composer_draw_text(
                composer,
                0u,
                "Identity transform",
                &bounds,
                &options) != QUANTAPDF_OK)
            goto fail;
    }

    if (quantapdf_composer_finish(composer, &output) != QUANTAPDF_OK ||
        quantapdf_output_data(output, &data, &size) != QUANTAPDF_OK)
        goto fail;
    out->assign(data, data + size);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 1;

fail:
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_identity_and_validation(
    std::vector<unsigned char> const& font_data,
    quantapdf::detail::ttf_font_face const& face)
{
    std::vector<unsigned char> plain;
    std::vector<unsigned char> transformed;
    quantapdf_composer* composer = nullptr;
    quantapdf_composer_font_options font_options = {};
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_composer_text_options base = base_options();
    quantapdf_composer_embedded_text_options embedded = {};
    quantapdf_composer_glyph_run_options run = {};
    quantapdf_composer_glyph glyph = {};
    quantapdf_rect bounds = {0.0f, 0.0f, 120.0f, 50.0f};
    quantapdf_affine_transform bad = {
        NAN, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    char const cluster[] = "A";

    CHECK(make_identity_base_output(0, &plain));
    CHECK(make_identity_base_output(1, &transformed));
    CHECK(plain == transformed);

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer, 0u));
    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
              composer,
              font_data.data(),
              font_data.size(),
              &font_options,
              &font_id) == QUANTAPDF_OK);

    embedded.struct_size =
        QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE;
    embedded.font_id = font_id;
    embedded.font_size = 18.0f;
    embedded.argb = UINT32_C(0xff102030);
    embedded.line_height_multiplier = 1.2f;
    embedded.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    embedded.wrap = 0;

    run.struct_size = QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_SIZE;
    run.font_id = font_id;
    run.font_size = 18.0f;
    run.argb = UINT32_C(0xff102030);
    glyph.glyph_id = face.glyph_for('A');
    CHECK(glyph.glyph_id != 0u);
    glyph.x_advance = 600.0f;
    glyph.unicode_offset = 0u;
    glyph.unicode_length = 1u;

    CHECK(quantapdf_composer_draw_text_transformed(
              composer, 0u, "bad", &bounds, nullptr, &base) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_text_transformed(
              composer, 0u, "bad", &bounds, &bad, &base) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_embedded_text_transformed(
              composer, 0u, "bad", &bounds, &bad, &embedded) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_glyph_run_transformed(
              composer,
              0u,
              {0.0f, 0.0f},
              &glyph,
              1u,
              cluster,
              1u,
              &bad,
              &run) == QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    return 0;
}

static int verify_page(
    quantapdf_document* document,
    int page_index,
    char const* expected,
    size_t expected_size)
{
    quantapdf_page* page = nullptr;
    quantapdf_bitmap* bitmap = nullptr;
    quantapdf_render_options render = {};
    unsigned char const* pixels = nullptr;
    char* extracted = nullptr;
    size_t extracted_size = 0u;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(contains_bytes(extracted, extracted_size, expected, expected_size));

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 300 && height == 300 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size == static_cast<size_t>(stride) *
            static_cast<size_t>(height));
    CHECK(count_nonwhite(pixels, stride, width, height) > 10u);

    quantapdf_free(extracted);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    return 0;
}

static int test_affine_render_extract_determinism(
    std::vector<unsigned char> const& font_data,
    quantapdf::detail::ttf_font_face const& face)
{
    static char const embedded_text[] = "Caf\xC3\xA9 \xCE\xA9";
    static char const run_text[] = "AV";
    quantapdf_composer* composer = nullptr;
    quantapdf_composer_font_options font_options = {};
    quantapdf_composer_font_id font_id = 0u;
    quantapdf_composer_text_options base = base_options();
    quantapdf_composer_embedded_text_options embedded = {};
    quantapdf_composer_glyph_run_options run = {};
    quantapdf_composer_glyph glyphs[2] = {};
    quantapdf_rect bounds = {0.0f, 0.0f, 140.0f, 50.0f};
    quantapdf_rect embedded_bounds = {0.0f, 0.0f, 180.0f, 55.0f};
    quantapdf_affine_transform rotate90 = {
        0.0f, 1.0f, -1.0f, 0.0f, 200.0f, 40.0f
    };
    quantapdf_affine_transform rotate180 = {
        -1.0f, 0.0f, 0.0f, -1.0f, 260.0f, 120.0f
    };
    quantapdf_affine_transform rotate270 = {
        0.0f, -1.0f, 1.0f, 0.0f, 60.0f, 240.0f
    };
    quantapdf_affine_transform rotate30 = {
        0.8660254f, 0.5f, -0.5f, 0.8660254f, 100.0f, 50.0f
    };
    quantapdf_affine_transform embedded90 = {
        0.0f, 1.0f, -1.0f, 0.0f, 220.0f, 30.0f
    };
    quantapdf_affine_transform glyph90 = {
        0.0f, 1.0f, -1.0f, 0.0f, 220.0f, 80.0f
    };
    quantapdf_output* first = nullptr;
    quantapdf_output* second = nullptr;
    quantapdf_document* document = nullptr;
    unsigned char const* first_data = nullptr;
    unsigned char const* second_data = nullptr;
    size_t first_size = 0u;
    size_t second_size = 0u;

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    for (size_t i = 0u; i < 6u; ++i)
        CHECK(add_page(composer, i));

    font_options.struct_size = QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_add_font(
              composer,
              font_data.data(),
              font_data.size(),
              &font_options,
              &font_id) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_draw_text_transformed(
              composer, 0u, "R90", &bounds, &rotate90, &base) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_text_transformed(
              composer, 1u, "R180", &bounds, &rotate180, &base) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_text_transformed(
              composer, 2u, "R270", &bounds, &rotate270, &base) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_text_transformed(
              composer, 3u, "R30", &bounds, &rotate30, &base) ==
          QUANTAPDF_OK);

    embedded.struct_size =
        QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE;
    embedded.font_id = font_id;
    embedded.font_size = 20.0f;
    embedded.argb = UINT32_C(0xff203040);
    embedded.line_height_multiplier = 1.2f;
    embedded.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    embedded.wrap = 0;
    CHECK(quantapdf_composer_draw_embedded_text_transformed(
              composer,
              4u,
              embedded_text,
              &embedded_bounds,
              &embedded90,
              &embedded) == QUANTAPDF_OK);

    run.struct_size = QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_SIZE;
    run.font_id = font_id;
    run.font_size = 24.0f;
    run.argb = UINT32_C(0xff405060);
    glyphs[0].glyph_id = face.glyph_for('A');
    glyphs[0].x_advance = 650.0f;
    glyphs[0].unicode_offset = 0u;
    glyphs[0].unicode_length = 1u;
    glyphs[1].glyph_id = face.glyph_for('V');
    glyphs[1].x_advance = 600.0f;
    glyphs[1].x_offset = -50.0f;
    glyphs[1].y_offset = -100.0f;
    glyphs[1].unicode_offset = 1u;
    glyphs[1].unicode_length = 1u;
    CHECK(glyphs[0].glyph_id != 0u && glyphs[1].glyph_id != 0u);
    CHECK(quantapdf_composer_draw_glyph_run_transformed(
              composer,
              5u,
              {0.0f, 0.0f},
              glyphs,
              2u,
              run_text,
              sizeof(run_text) - 1u,
              &glyph90,
              &run) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(std::memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "0 -1 1 0 182 260 Tm"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 1u, "-1 0 0 -1 260 198 Tm"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 2u, "0 1 -1 0 78 60 Tm"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 4u, "0 -1 1 0 200 270 Tm"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 5u, "0 -1 1 0 220 220 Tm"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 5u, "0 -1 1 0 222.4 205.6 Tm"));

    CHECK(quantapdf_output_save_file(first, COMPOSER_AFFINE_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_AFFINE_OUTPUT_PDF, nullptr, &document) ==
          QUANTAPDF_OK);
    CHECK(verify_page(document, 0, "R90", 3u) == 0);
    CHECK(verify_page(document, 1, "R180", 4u) == 0);
    CHECK(verify_page(document, 2, "R270", 4u) == 0);
    CHECK(verify_page(document, 3, "R30", 3u) == 0);
    CHECK(verify_page(
              document,
              4,
              embedded_text,
              sizeof(embedded_text) - 1u) == 0);
    CHECK(verify_page(document, 5, run_text, sizeof(run_text) - 1u) == 0);

    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    std::vector<unsigned char> const font_data = read_file(ROBOTO_TTF);
    quantapdf::detail::ttf_font_face face;

    CHECK(!font_data.empty());
    CHECK(quantapdf::detail::ttf_font_face::parse(
              font_data.data(), font_data.size(), &face) == QUANTAPDF_OK);
    CHECK(test_identity_and_validation(font_data, face) == 0);
    CHECK(test_affine_render_extract_determinism(font_data, face) == 0);
    return 0;
}
