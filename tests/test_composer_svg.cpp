#include <quantapdf/quantapdf.h>

#include "composer_test_helpers.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(                                                      \
                stderr, "CHECK failed at %s:%d: %s\n",                        \
                __FILE__, __LINE__, #expr);                                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int add_page(quantapdf_composer* composer)
{
    quantapdf_composer_page_options page = {};
    size_t page_index = SIZE_MAX;
    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 240.0f;
    page.height_points = 240.0f;
    page.background_argb = UINT32_C(0xffffffff);
    return quantapdf_composer_add_page(
               composer, &page, &page_index) == QUANTAPDF_OK &&
        page_index == 0u;
}

static quantapdf_status draw(
    quantapdf_composer* composer,
    char const* svg,
    quantapdf_rect bounds)
{
    quantapdf_composer_svg_options options = {};
    options.struct_size = QUANTAPDF_COMPOSER_SVG_OPTIONS_V1_SIZE;
    return quantapdf_composer_draw_svg(
        composer,
        0u,
        reinterpret_cast<unsigned char const*>(svg),
        std::strlen(svg),
        &bounds,
        &options);
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

static int pixel_near(
    unsigned char const* pixels,
    int stride,
    int x,
    int y,
    int r,
    int g,
    int b,
    int tolerance)
{
    auto const* p =
        pixels + static_cast<size_t>(y) * static_cast<size_t>(stride) +
        static_cast<size_t>(x) * 3u;
    auto near = [&](unsigned char actual, int expected) {
        int delta = static_cast<int>(actual) - expected;
        if (delta < 0)
            delta = -delta;
        return delta <= tolerance;
    };
    return near(p[0], r) && near(p[1], g) && near(p[2], b);
}

static int test_argument_contract()
{
    static char const svg[] =
        "<svg viewBox=\"0 0 10 10\"><rect width=\"2\" height=\"2\"/></svg>";
    quantapdf_composer* composer = nullptr;
    quantapdf_composer_svg_options options = {};
    quantapdf_rect bounds = {0.0f, 0.0f, 100.0f, 100.0f};

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));

    options.struct_size = QUANTAPDF_COMPOSER_SVG_OPTIONS_V1_SIZE;
    CHECK(quantapdf_composer_draw_svg(
              nullptr,
              0u,
              reinterpret_cast<unsigned char const*>(svg),
              sizeof(svg) - 1u,
              &bounds,
              &options) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_draw_svg(
              composer,
              1u,
              reinterpret_cast<unsigned char const*>(svg),
              sizeof(svg) - 1u,
              &bounds,
              &options) == QUANTAPDF_ERROR_ARGUMENT);

    options.struct_size = 0u;
    CHECK(quantapdf_composer_draw_svg(
              composer,
              0u,
              reinterpret_cast<unsigned char const*>(svg),
              sizeof(svg) - 1u,
              &bounds,
              &options) == QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_render_geometry_and_determinism()
{
    static char const svg[] =
        "<svg viewBox=\"0 0 100 100\" "
        "xmlns=\"http://www.w3.org/2000/svg\">"
        "<g fill=\"#ff0000\">"
        "<rect x=\"5\" y=\"5\" width=\"20\" height=\"15\"/>"
        "<circle cx=\"50\" cy=\"15\" r=\"10\" fill=\"#00ff00\"/>"
        "<ellipse cx=\"80\" cy=\"15\" rx=\"12\" ry=\"8\" fill=\"#0000ff\"/>"
        "</g>"
        "<g transform=\"translate(10 40) scale(0.8)\" "
        "fill=\"none\" stroke=\"#000000\" stroke-width=\"2\" "
        "stroke-linecap=\"round\" stroke-linejoin=\"bevel\">"
        "<path d=\"M 0 0 Q 20 30 40 0 T 80 0 "
        "C 70 20 60 20 50 0 S 30 -20 20 0\"/>"
        "<polyline points=\"0,20 20,30 40,20\"/>"
        "</g>"
        "<polygon points=\"10,80 30,60 50,80\" "
        "style=\"fill:#ffff00;stroke:#000000;stroke-width:1\"/>"
        "</svg>";

    quantapdf_composer* composer = nullptr;
    quantapdf_output* first = nullptr;
    quantapdf_output* second = nullptr;
    quantapdf_document* document = nullptr;
    quantapdf_page* page = nullptr;
    quantapdf_bitmap* bitmap = nullptr;
    quantapdf_render_options render = {};
    quantapdf_rect bounds = {20.0f, 20.0f, 220.0f, 220.0f};
    unsigned char const* first_data = nullptr;
    unsigned char const* second_data = nullptr;
    unsigned char const* pixels = nullptr;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));
    CHECK(draw(composer, svg, bounds) == QUANTAPDF_OK);

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
        "30 210 m 70 210 l 70 180 l 30 180 l h f"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "1 0 0 rg"));
    CHECK(quantapdf_test_pdf_content_count(
        first_data, first_size, 0u, " c ") >= 8u);

    CHECK(quantapdf_output_save_file(
              first, COMPOSER_SVG_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_SVG_OUTPUT_PDF, nullptr, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);

    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) ==
          QUANTAPDF_OK);
    CHECK(width == 240 && height == 240 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);
    CHECK(pixel_size ==
          static_cast<size_t>(stride) * static_cast<size_t>(height));
    CHECK(count_nonwhite(pixels, stride, 20, 20, 225, 225) > 500u);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_v2_dash_opacity_arcs()
{
    static char const svg[] =
        "<svg viewBox=\"0 0 100 100\">"
        "<path d=\"M10 50 A40 30 30 1 1 90 50\" "
        "fill=\"none\" stroke=\"#000000\" stroke-width=\"2\" "
        "stroke-opacity=\"0.5\" "
        "stroke-dasharray=\"5 3 2\" stroke-dashoffset=\"-4\"/>"
        "<rect x=\"10\" y=\"70\" width=\"80\" height=\"20\" "
        "fill=\"#ff0000\" fill-opacity=\"0.25\"/>"
        "<path d=\"M10 10 A-10 -5 0 0 1 30 10 "
        "A0 4 0 0 0 50 10 A5 5 0 0 0 50 10\" "
        "fill=\"none\" stroke=\"#0000ff\"/>"
        "</svg>";
    quantapdf_composer* composer = nullptr;
    quantapdf_output* output = nullptr;
    quantapdf_document* document = nullptr;
    quantapdf_page* page = nullptr;
    quantapdf_bitmap* bitmap = nullptr;
    quantapdf_render_options render = {};
    quantapdf_rect bounds = {20.0f, 20.0f, 220.0f, 220.0f};
    unsigned char const* data = nullptr;
    unsigned char const* pixels = nullptr;
    size_t size = 0u;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;
    double fill_alpha = 0.0;
    double stroke_alpha = 0.0;
    char blend[32] = {};

    CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
    CHECK(add_page(composer));
    CHECK(draw(composer, svg, bounds) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);

    CHECK(quantapdf_test_pdf_content_contains(
        data, size, 0u, "[10 6 4 10 6 4] 32 d"));
    CHECK(quantapdf_test_pdf_content_count(data, size, 0u, " c ") >= 3u);
    CHECK(quantapdf_test_pdf_extgstate_info(
        data,
        size,
        0u,
        1u,
        &fill_alpha,
        &stroke_alpha,
        blend,
        sizeof(blend)));
    CHECK(std::fabs(fill_alpha - 1.0) < 0.001);
    CHECK(std::fabs(stroke_alpha - 0.5) < 0.001);
    CHECK(std::strcmp(blend, "/Normal") == 0);
    CHECK(quantapdf_test_pdf_extgstate_info(
        data,
        size,
        0u,
        2u,
        &fill_alpha,
        &stroke_alpha,
        blend,
        sizeof(blend)));
    CHECK(std::fabs(fill_alpha - 0.25) < 0.001);
    CHECK(std::fabs(stroke_alpha - 1.0) < 0.001);

    CHECK(quantapdf_output_save_file(
              output, COMPOSER_SVG_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(
              COMPOSER_SVG_OUTPUT_PDF, nullptr, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    render.struct_size = sizeof(render);
    render.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 240 && height == 240 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(count_nonwhite(pixels, stride, 20, 20, 220, 220) > 400u);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_v2_explicit_preserve_aspect_ratio()
{
    static char const meet_svg[] =
        "<svg viewBox=\"0 0 100 50\" preserveAspectRatio=\"xMidYMid meet\">"
        "<rect width=\"100\" height=\"50\" fill=\"#ff0000\"/>"
        "</svg>";
    static char const slice_svg[] =
        "<svg viewBox=\"0 0 100 50\" preserveAspectRatio=\"xMidYMid slice\">"
        "<rect width=\"100\" height=\"50\" fill=\"#00ff00\"/>"
        "</svg>";
    quantapdf_rect bounds = {20.0f, 20.0f, 220.0f, 220.0f};

    {
        quantapdf_composer* composer = nullptr;
        quantapdf_output* output = nullptr;
        unsigned char const* data = nullptr;
        size_t size = 0u;

        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, meet_svg, bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
        CHECK(quantapdf_test_pdf_content_contains(
            data,
            size,
            0u,
            "20 170 m 220 170 l 220 70 l 20 70 l h f"));
        CHECK(quantapdf_test_pdf_content_count(data, size, 0u, " W n") == 0u);
        quantapdf_drop_output(output);
        quantapdf_drop_composer(composer);
    }

    {
        quantapdf_composer* composer = nullptr;
        quantapdf_output* output = nullptr;
        quantapdf_document* document = nullptr;
        quantapdf_page* page = nullptr;
        quantapdf_bitmap* bitmap = nullptr;
        quantapdf_render_options render = {};
        unsigned char const* data = nullptr;
        unsigned char const* pixels = nullptr;
        size_t size = 0u;
        size_t pixel_size = 0u;
        int width = 0;
        int height = 0;
        int stride = 0;
        int components = 0;

        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, slice_svg, bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
        CHECK(quantapdf_test_pdf_content_contains(
            data,
            size,
            0u,
            "20 220 m 220 220 l 220 20 l 20 20 l h W n"));
        CHECK(quantapdf_test_pdf_content_contains(
            data,
            size,
            0u,
            "-80 220 m 320 220 l 320 20 l -80 20 l h f"));

        CHECK(quantapdf_output_save_file(
                  output, COMPOSER_SVG_OUTPUT_PDF) == QUANTAPDF_OK);
        CHECK(quantapdf_open(
                  COMPOSER_SVG_OUTPUT_PDF, nullptr, &document) == QUANTAPDF_OK);
        CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
        render.struct_size = sizeof(render);
        render.dpi = 72.0f;
        CHECK(quantapdf_render_page_with_options(page, &render, &bitmap) ==
              QUANTAPDF_OK);
        CHECK(quantapdf_bitmap_dimensions(
                  bitmap, &width, &height, &stride, &components) ==
              QUANTAPDF_OK);
        CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
              QUANTAPDF_OK);
        CHECK(pixel_near(pixels, stride, 30, 30, 0, 255, 0, 20));
        CHECK(pixel_near(pixels, stride, 210, 210, 0, 255, 0, 20));
        CHECK(pixel_near(pixels, stride, 10, 30, 255, 255, 255, 15));
        CHECK(pixel_near(pixels, stride, 230, 210, 255, 255, 255, 15));

        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
        quantapdf_close(document);
        quantapdf_drop_output(output);
        quantapdf_drop_composer(composer);
    }
    return 0;
}

static int test_security_and_unsupported_features()
{
    static char const* cases[] = {
        "<!DOCTYPE svg [<!ENTITY x SYSTEM \"file:///etc/passwd\">]>"
        "<svg viewBox=\"0 0 10 10\"><path d=\"M0 0 L1 1\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><script>alert(1)</script></svg>",
        "<svg viewBox=\"0 0 10 10\"><image href=\"https://example.com/a.png\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><use href=\"#shape\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><text>hello</text></svg>",
        "<svg viewBox=\"0 0 10 10\"><rect x=\"1\" y=\"1\" width=\"4\" height=\"4\" opacity=\"0.5\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><path d=\"M0 0 L9 9\" "
        "stroke=\"black\" fill=\"none\" transform=\"scale(2 1)\"/></svg>",
        "<svg><rect width=\"2\" height=\"2\"/></svg>"
    };
    quantapdf_rect bounds = {0.0f, 0.0f, 100.0f, 100.0f};

    for (char const* svg: cases) {
        quantapdf_composer* composer = nullptr;
        CHECK(quantapdf_composer_create(nullptr, &composer) ==
              QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg, bounds) == QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_drop_composer(composer);
    }
    return 0;
}

static int test_malformed_inputs()
{
    static char const* cases[] = {
        "<svg viewBox=\"0 0 0 10\"><rect width=\"2\" height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><path d=\"M 1\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><path d=\"M1 1 A2 2 0 2 0 5 5\"/></svg>",
        "<svg viewBox=\"0 0 10 10\" preserveAspectRatio=\"xBadYMid meet\"><rect width=\"2\" height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><polygon points=\"1,1 2,2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><rect width=\"-1\" height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><g><rect width=\"2\" height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><rect width=\"2\" width=\"3\" height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><rect width=\"2 height=\"2\"/></svg>"
    };
    quantapdf_rect bounds = {0.0f, 0.0f, 100.0f, 100.0f};

    for (char const* svg: cases) {
        quantapdf_composer* composer = nullptr;
        CHECK(quantapdf_composer_create(nullptr, &composer) ==
              QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg, bounds) == QUANTAPDF_ERROR_FORMAT);
        quantapdf_drop_composer(composer);
    }
    return 0;
}

static int test_atomic_capacity_failure()
{
    static char const svg_two_shapes[] =
        "<svg viewBox=\"0 0 10 10\">"
        "<rect x=\"1\" y=\"1\" width=\"2\" height=\"2\"/>"
        "<rect x=\"5\" y=\"5\" width=\"2\" height=\"2\"/>"
        "</svg>";
    static char const svg_one_shape[] =
        "<svg viewBox=\"0 0 10 10\">"
        "<rect x=\"1\" y=\"1\" width=\"2\" height=\"2\"/>"
        "</svg>";

    quantapdf_composer_options composer_options = {};
    quantapdf_composer* rejected = nullptr;
    quantapdf_composer* clean = nullptr;
    quantapdf_output* rejected_output = nullptr;
    quantapdf_output* clean_output = nullptr;
    quantapdf_rect bounds = {0.0f, 0.0f, 100.0f, 100.0f};
    unsigned char const* rejected_data = nullptr;
    unsigned char const* clean_data = nullptr;
    size_t rejected_size = 0u;
    size_t clean_size = 0u;

    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_operations = 1u;
    CHECK(quantapdf_composer_create(
              &composer_options, &rejected) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_create(
              &composer_options, &clean) == QUANTAPDF_OK);
    CHECK(add_page(rejected));
    CHECK(add_page(clean));

    CHECK(draw(rejected, svg_two_shapes, bounds) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(draw(rejected, svg_one_shape, bounds) == QUANTAPDF_OK);
    CHECK(draw(clean, svg_one_shape, bounds) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(rejected, &rejected_output) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(clean, &clean_output) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              rejected_output, &rejected_data, &rejected_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              clean_output, &clean_data, &clean_size) == QUANTAPDF_OK);
    CHECK(rejected_size == clean_size);
    CHECK(std::memcmp(rejected_data, clean_data, clean_size) == 0);

    quantapdf_drop_output(clean_output);
    quantapdf_drop_output(rejected_output);
    quantapdf_drop_composer(clean);
    quantapdf_drop_composer(rejected);
    return 0;
}

int main()
{
    CHECK(test_argument_contract() == 0);
    CHECK(test_render_geometry_and_determinism() == 0);
    CHECK(test_v2_dash_opacity_arcs() == 0);
    CHECK(test_v2_explicit_preserve_aspect_ratio() == 0);
    CHECK(test_security_and_unsupported_features() == 0);
    CHECK(test_malformed_inputs() == 0);
    CHECK(test_atomic_capacity_failure() == 0);
    return 0;
}
