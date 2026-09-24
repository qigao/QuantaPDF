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
    auto near_channel = [=](unsigned char actual, int expected) {
        int delta = static_cast<int>(actual) - expected;
        if (delta < 0)
            delta = -delta;
        return delta <= tolerance;
    };
    return near_channel(p[0], r) &&
        near_channel(p[1], g) &&
        near_channel(p[2], b);
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
        "2 0 0 2 20 -260 cm"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "5 235 m 25 235 l 25 220 l 5 220 l h f"));
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

static int test_v2a_dash_opacity_arcs_and_aspect()
{
    static char const svg_meet[] =
        "<svg viewBox=\"0 0 100 100\">"
        "<g fill-opacity=\"0.5\" stroke-opacity=\"0.5\" "
        "stroke-dasharray=\"5 3 2\" stroke-dashoffset=\"-3\">"
        "<path d=\"M10 50 A40 30 0 0 1 90 50\" "
        "fill=\"#ff0000\" stroke=\"#000000\" stroke-width=\"2\" "
        "opacity=\"0.5\"/>"
        "</g></svg>";
    static char const svg_none[] =
        "<svg viewBox=\"0 0 100 100\" preserveAspectRatio=\"none\">"
        "<rect x=\"10\" y=\"10\" width=\"10\" height=\"10\" "
        "fill=\"#ff0000\"/></svg>";
    static char const svg_slice[] =
        "<svg viewBox=\"0 0 100 100\" "
        "preserveAspectRatio=\"xMaxYMax slice\">"
        "<rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" "
        "fill=\"#00ff00\"/></svg>";

    quantapdf_rect bounds = {20.0f, 20.0f, 220.0f, 120.0f};

    {
        quantapdf_composer* composer = nullptr;
        quantapdf_output* output = nullptr;
        unsigned char const* data = nullptr;
        size_t size = 0u;
        double fill_alpha = 0.0;
        double stroke_alpha = 0.0;
        char blend[32] = {};

        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg_meet, bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);

        CHECK(quantapdf_test_pdf_content_contains(
            data, size, 0u, "1 0 0 1 70 -20 cm"));
        CHECK(quantapdf_test_pdf_content_contains(
            data, size, 0u, "10 190 m"));
        CHECK(quantapdf_test_pdf_content_contains(
            data, size, 0u, "[5 3 2 5 3 2] 17 d"));
        CHECK(quantapdf_test_pdf_content_count(
            data, size, 0u, " c ") >= 2u);
        CHECK(quantapdf_test_pdf_content_count(
            data, size, 0u, "/GS1 gs") == 1u);
        CHECK(quantapdf_test_pdf_extgstate_info(
            data,
            size,
            0u,
            1u,
            &fill_alpha,
            &stroke_alpha,
            blend,
            sizeof(blend)));
        CHECK(std::fabs(fill_alpha - 0.25) < 0.001);
        CHECK(std::fabs(stroke_alpha - 0.25) < 0.001);
        CHECK(std::strcmp(blend, "/Normal") == 0);

        quantapdf_drop_output(output);
        quantapdf_drop_composer(composer);
    }

    {
        quantapdf_composer* composer = nullptr;
        quantapdf_output* output = nullptr;
        unsigned char const* data = nullptr;
        size_t size = 0u;

        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg_none, bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
        CHECK(quantapdf_test_pdf_content_contains(
            data,
            size,
            0u,
            "2 0 0 1 20 -20 cm"));
        CHECK(quantapdf_test_pdf_content_contains(
            data,
            size,
            0u,
            "10 230 m 20 230 l 20 220 l 10 220 l h f"));
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
        CHECK(draw(composer, svg_slice, bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
        CHECK(quantapdf_test_pdf_content_contains(
            data,
            size,
            0u,
            "20 220 m 220 220 l 220 120 l 20 120 l h W n"));

        CHECK(quantapdf_output_save_file(
                  output, COMPOSER_SVG_OUTPUT_PDF) == QUANTAPDF_OK);
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
        CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
              QUANTAPDF_OK);
        CHECK(pixel_near(pixels, stride, 30, 30, 0, 255, 0, 20));
        CHECK(pixel_near(pixels, stride, 30, 10, 255, 255, 255, 15));

        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
        quantapdf_close(document);
        quantapdf_drop_output(output);
        quantapdf_drop_composer(composer);
    }

    return 0;
}

static int test_v2b1_gradient_and_clip_references()
{
    static char const svg[] =
        "<svg viewBox=\"0 0 100 100\">"
        "<rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" "
        "fill=\"url(#linear)\" clip-path=\"url(#clip)\"/>"
        "<defs>"
        "<linearGradient id=\"linear\" gradientUnits=\"userSpaceOnUse\" "
        "x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\" "
        "gradientTransform=\"translate(10 0)\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient>"
        "<radialGradient id=\"radial\" gradientUnits=\"userSpaceOnUse\" "
        "cx=\"75\" cy=\"75\" r=\"20\" fx=\"75\" fy=\"75\" fr=\"0\">"
        "<stop offset=\"0%\" style=\"stop-color:#ffffff;stop-opacity:1\"/>"
        "<stop offset=\"100%\" stop-color=\"#000000\"/>"
        "</radialGradient>"
        "<clipPath id=\"clip\" clipPathUnits=\"userSpaceOnUse\" "
        "transform=\"translate(10 0)\">"
        "<rect x=\"0\" y=\"0\" width=\"50\" height=\"100\"/>"
        "</clipPath>"
        "</defs>"
        "<circle cx=\"75\" cy=\"75\" r=\"20\" fill=\"url(#radial)\"/>"
        "<path d=\"M10 90 L90 90\" fill=\"none\" stroke=\"url(#linear)\" "
        "stroke-width=\"2\"/>"
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
    int shading_type = 0;
    int function_type = 0;
    double matrix[6] = {};

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

    CHECK(quantapdf_test_pdf_pattern_info(
        first_data,
        first_size,
        0u,
        1u,
        &shading_type,
        &function_type,
        matrix));
    CHECK(shading_type == 2 && function_type == 2);
    CHECK(std::fabs(matrix[0] - 1.0) < 0.001);
    CHECK(std::fabs(matrix[1]) < 0.001);
    CHECK(std::fabs(matrix[2]) < 0.001);
    CHECK(std::fabs(matrix[3] + 1.0) < 0.001);
    CHECK(std::fabs(matrix[4] - 10.0) < 0.001);
    CHECK(std::fabs(matrix[5] - 240.0) < 0.001);
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "2 0 0 2 20 -260 cm"));

    CHECK(quantapdf_test_pdf_pattern_info(
        first_data,
        first_size,
        0u,
        2u,
        &shading_type,
        &function_type,
        matrix));
    CHECK(shading_type == 3 && function_type == 2);

    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "40 220 m 140 220 l 140 20 l 40 20 l h W n"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "/Pattern cs /P1 scn"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data, first_size, 0u, "/Pattern CS /P1 SCN"));

    CHECK(quantapdf_output_save_file(
              first, COMPOSER_SVG_OUTPUT_PDF) == QUANTAPDF_OK);
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

    CHECK(pixel_near(pixels, stride, 30, 80, 255, 255, 255, 20));
    CHECK(pixel_near(pixels, stride, 55, 80, 235, 0, 20, 45));
    CHECK(pixel_near(pixels, stride, 130, 80, 140, 0, 115, 55));
    CHECK(pixel_near(pixels, stride, 170, 170, 255, 255, 255, 35));
    CHECK(pixel_near(pixels, stride, 205, 170, 20, 20, 20, 55));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_v2b1_reference_failures_and_rollback()
{
    static char const* unsupported[] = {
        "<svg viewBox=\"0 0 10 10\"><rect width=\"5\" height=\"5\" "
        "fill=\"url(#missing)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<clipPath id=\"c\" clipPathUnits=\"userSpaceOnUse\">"
        "<rect width=\"5\" height=\"5\"/></clipPath></defs>"
        "<rect width=\"5\" height=\"5\" fill=\"url(#c)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><rect width=\"5\" height=\"5\" "
        "fill=\"url(https://example.com/g)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"red\"/>"
        "<stop offset=\"1\" stop-color=\"blue\"/>"
        "</linearGradient></defs><rect width=\"10\" height=\"10\" "
        "fill=\"url(#g)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<linearGradient id=\"g\" gradientUnits=\"userSpaceOnUse\" "
        "x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"red\" stop-opacity=\"0.5\"/>"
        "<stop offset=\"1\" stop-color=\"blue\"/>"
        "</linearGradient></defs><rect width=\"10\" height=\"10\" "
        "fill=\"url(#g)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<linearGradient id=\"g\" gradientUnits=\"userSpaceOnUse\" "
        "x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\">"
        "<stop offset=\"0.5\" stop-color=\"red\"/>"
        "<stop offset=\"0.5\" stop-color=\"blue\"/>"
        "</linearGradient></defs><rect width=\"10\" height=\"10\" "
        "fill=\"url(#g)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<clipPath id=\"c\" clipPathUnits=\"objectBoundingBox\">"
        "<rect width=\"1\" height=\"1\"/></clipPath></defs>"
        "<rect width=\"10\" height=\"10\" clip-path=\"url(#c)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\" preserveAspectRatio=\"xMidYMid slice\">"
        "<defs><clipPath id=\"c\" clipPathUnits=\"userSpaceOnUse\">"
        "<rect width=\"5\" height=\"10\"/></clipPath></defs>"
        "<rect width=\"10\" height=\"10\" clip-path=\"url(#c)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<clipPath id=\"c\" clipPathUnits=\"userSpaceOnUse\">"
        "<rect width=\"5\" height=\"10\"/></clipPath></defs>"
        "<g clip-path=\"url(#c)\"><rect width=\"10\" height=\"10\"/></g></svg>"
    };
    static char const duplicate_id[] =
        "<svg viewBox=\"0 0 10 10\">"
        "<g id=\"same\"/><g id=\"same\"/></svg>";

    quantapdf_rect bounds = {0.0f, 0.0f, 100.0f, 100.0f};
    for (char const* svg: unsupported) {
        quantapdf_composer* composer = nullptr;
        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg, bounds) == QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_drop_composer(composer);
    }
    {
        quantapdf_composer* composer = nullptr;
        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, duplicate_id, bounds) == QUANTAPDF_ERROR_FORMAT);
        quantapdf_drop_composer(composer);
    }

    static char const gradient_svg[] =
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<linearGradient id=\"g\" gradientUnits=\"userSpaceOnUse\" "
        "x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"red\"/>"
        "<stop offset=\"1\" stop-color=\"blue\"/>"
        "</linearGradient></defs>"
        "<rect width=\"10\" height=\"10\" fill=\"url(#g)\"/></svg>";
    static char const solid_svg[] =
        "<svg viewBox=\"0 0 10 10\">"
        "<rect width=\"10\" height=\"10\" fill=\"red\"/></svg>";

    quantapdf_composer_options composer_options = {};
    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_resource_bytes =
        5u * sizeof(quantapdf_composer_path_command);

    quantapdf_composer* rejected = nullptr;
    quantapdf_composer* clean = nullptr;
    quantapdf_output* rejected_output = nullptr;
    quantapdf_output* clean_output = nullptr;
    unsigned char const* rejected_data = nullptr;
    unsigned char const* clean_data = nullptr;
    size_t rejected_size = 0u;
    size_t clean_size = 0u;

    CHECK(quantapdf_composer_create(&composer_options, &rejected) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_create(&composer_options, &clean) ==
          QUANTAPDF_OK);
    CHECK(add_page(rejected));
    CHECK(add_page(clean));

    CHECK(draw(rejected, gradient_svg, bounds) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(draw(rejected, solid_svg, bounds) == QUANTAPDF_OK);
    CHECK(draw(clean, solid_svg, bounds) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(rejected, &rejected_output) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(clean, &clean_output) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              rejected_output, &rejected_data, &rejected_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              clean_output, &clean_data, &clean_size) ==
          QUANTAPDF_OK);
    CHECK(rejected_size == clean_size);
    CHECK(std::memcmp(rejected_data, clean_data, clean_size) == 0);

    quantapdf_drop_output(clean_output);
    quantapdf_drop_output(rejected_output);
    quantapdf_drop_composer(clean);
    quantapdf_drop_composer(rejected);
    return 0;
}

static int test_v2b2_symbol_use_form_reuse()
{
    static char const svg[] =
        "<svg viewBox=\"0 0 240 240\">"
        "<use href=\"#outer\" x=\"20\" y=\"20\" width=\"100\" height=\"100\"/>"
        "<defs>"
        "<linearGradient id=\"g\" gradientUnits=\"userSpaceOnUse\" "
        "x1=\"0\" y1=\"0\" x2=\"20\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient>"
        "<clipPath id=\"c\" clipPathUnits=\"userSpaceOnUse\">"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\"/>"
        "</clipPath>"
        "<symbol id=\"inner\" viewBox=\"0 0 20 20\" preserveAspectRatio=\"none\">"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" "
        "fill=\"url(#g)\" clip-path=\"url(#c)\"/>"
        "</symbol>"
        "<symbol id=\"outer\" viewBox=\"0 0 40 20\" "
        "preserveAspectRatio=\"xMidYMid meet\">"
        "<use href=\"#inner\" x=\"0\" y=\"0\" width=\"20\" height=\"20\"/>"
        "<use href=\"#inner\" x=\"20\" y=\"0\" width=\"20\" height=\"20\"/>"
        "</symbol>"
        "<symbol id=\"slice\" viewBox=\"0 0 100 50\" "
        "preserveAspectRatio=\"xMidYMid slice\">"
        "<rect x=\"0\" y=\"0\" width=\"100\" height=\"50\" fill=\"#ff0000\"/>"
        "</symbol>"
        "<symbol id=\"none\" viewBox=\"0 0 100 50\" preserveAspectRatio=\"none\">"
        "<rect x=\"0\" y=\"0\" width=\"100\" height=\"50\" fill=\"#00ff00\"/>"
        "</symbol>"
        "</defs>"
        "<use href=\"#outer\" x=\"140\" y=\"20\" width=\"80\" height=\"40\"/>"
        "<use href=\"#slice\" x=\"20\" y=\"140\" width=\"80\" height=\"60\"/>"
        "<use href=\"#none\" x=\"140\" y=\"140\" width=\"80\" height=\"60\"/>"
        "</svg>";

    quantapdf_composer* composer = nullptr;
    quantapdf_output* first = nullptr;
    quantapdf_output* second = nullptr;
    quantapdf_document* document = nullptr;
    quantapdf_page* page = nullptr;
    quantapdf_bitmap* bitmap = nullptr;
    quantapdf_render_options render = {};
    quantapdf_rect bounds = {0.0f, 0.0f, 240.0f, 240.0f};
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
    double bbox[4] = {};
    int has_resources = 0;

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

    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/Fm1 Do") == 2u);
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/Fm2 Do") == 1u);
    CHECK(quantapdf_test_pdf_content_count(
              first_data, first_size, 0u, "/Fm3 Do") == 1u);
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "2.5 0 0 2.5 20 145 cm /Fm1 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "2 0 0 2 140 180 cm /Fm1 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "1.2 0 0 1.2 0 40 cm /Fm2 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "0.8 0 0 1.2 140 40 cm /Fm3 Do"));
    CHECK(quantapdf_test_pdf_content_contains(
        first_data,
        first_size,
        0u,
        "20 100 m 100 100 l 100 40 l 20 40 l h W n"));

    CHECK(quantapdf_test_pdf_form_xobject_info(
              first_data,
              first_size,
              0u,
              1u,
              bbox,
              &has_resources));
    CHECK(std::fabs(bbox[0]) < 0.001);
    CHECK(std::fabs(bbox[1]) < 0.001);
    CHECK(std::fabs(bbox[2] - 40.0) < 0.001);
    CHECK(std::fabs(bbox[3] - 20.0) < 0.001);
    CHECK(has_resources == 1);

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
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
          QUANTAPDF_OK);

    CHECK(pixel_near(pixels, stride, 30, 30, 255, 255, 255, 20));
    CHECK(count_nonwhite(pixels, stride, 20, 45, 120, 95) > 1000u);
    CHECK(pixel_near(pixels, stride, 25, 150, 255, 0, 0, 30));
    CHECK(pixel_near(pixels, stride, 10, 150, 255, 255, 255, 20));
    CHECK(pixel_near(pixels, stride, 150, 150, 0, 255, 0, 30));

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_v2b2_symbol_reference_failures_and_rollback()
{
    static char const* unsupported[] = {
        "<svg viewBox=\"0 0 10 10\"><use href=\"#missing\" x=\"0\" y=\"0\" "
        "width=\"10\" height=\"10\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<linearGradient id=\"g\" gradientUnits=\"userSpaceOnUse\" "
        "x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"red\"/>"
        "<stop offset=\"1\" stop-color=\"blue\"/>"
        "</linearGradient></defs>"
        "<use href=\"#g\" x=\"0\" y=\"0\" width=\"10\" height=\"10\"/>"
        "</svg>",
        "<svg viewBox=\"0 0 10 10\"><use href=\"https://example.com/s\" "
        "x=\"0\" y=\"0\" width=\"10\" height=\"10\"/></svg>",
        "<svg viewBox=\"0 0 10 10\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">"
        "<defs><symbol id=\"s\" viewBox=\"0 0 10 10\"/></defs>"
        "<use xlink:href=\"#s\" x=\"0\" y=\"0\" width=\"10\" height=\"10\"/>"
        "</svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<symbol id=\"s\" viewBox=\"0 0 10 10\">"
        "<rect width=\"10\" height=\"10\" fill=\"red\"/>"
        "</symbol></defs>"
        "<use href=\"#s\" x=\"0\" y=\"0\" width=\"10\" height=\"10\" "
        "fill=\"blue\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<symbol id=\"a\" viewBox=\"0 0 10 10\">"
        "<use href=\"#b\" width=\"10\" height=\"10\"/></symbol>"
        "<symbol id=\"b\" viewBox=\"0 0 10 10\">"
        "<use href=\"#a\" width=\"10\" height=\"10\"/></symbol>"
        "</defs><use href=\"#a\" width=\"10\" height=\"10\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<symbol id=\"s\" viewBox=\"0 0 10 10\">"
        "<use href=\"#s\" width=\"10\" height=\"10\"/></symbol>"
        "</defs><use href=\"#s\" width=\"10\" height=\"10\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<symbol id=\"s\"><rect width=\"10\" height=\"10\"/></symbol>"
        "</defs><use href=\"#s\" width=\"10\" height=\"10\"/></svg>"
    };

    static char const malformed[] =
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<symbol id=\"s\" viewBox=\"0 0 10 10\">"
        "<rect width=\"10\" height=\"10\"/></symbol></defs>"
        "<use href=\"#s\" width=\"0\" height=\"10\"/></svg>";

    quantapdf_rect bounds = {0.0f, 0.0f, 100.0f, 100.0f};
    for (char const* svg: unsupported) {
        quantapdf_composer* composer = nullptr;
        CHECK(quantapdf_composer_create(nullptr, &composer) ==
              QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg, bounds) == QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_drop_composer(composer);
    }
    {
        quantapdf_composer* composer = nullptr;
        CHECK(quantapdf_composer_create(nullptr, &composer) ==
              QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, malformed, bounds) == QUANTAPDF_ERROR_FORMAT);
        quantapdf_drop_composer(composer);
    }

    static char const form_svg[] =
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<symbol id=\"s\" viewBox=\"0 0 10 10\">"
        "<rect width=\"10\" height=\"10\" fill=\"red\"/></symbol>"
        "</defs><use href=\"#s\" width=\"10\" height=\"10\"/></svg>";
    static char const solid_svg[] =
        "<svg viewBox=\"0 0 10 10\">"
        "<rect width=\"10\" height=\"10\" fill=\"red\"/></svg>";

    quantapdf_composer_options composer_options = {};
    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_resource_bytes = 256u;

    quantapdf_composer* rejected = nullptr;
    quantapdf_composer* clean = nullptr;
    quantapdf_output* rejected_output = nullptr;
    quantapdf_output* clean_output = nullptr;
    unsigned char const* rejected_data = nullptr;
    unsigned char const* clean_data = nullptr;
    size_t rejected_size = 0u;
    size_t clean_size = 0u;

    CHECK(quantapdf_composer_create(&composer_options, &rejected) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_create(&composer_options, &clean) ==
          QUANTAPDF_OK);
    CHECK(add_page(rejected));
    CHECK(add_page(clean));

    CHECK(draw(rejected, form_svg, bounds) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(draw(rejected, solid_svg, bounds) == QUANTAPDF_OK);
    CHECK(draw(clean, solid_svg, bounds) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_finish(rejected, &rejected_output) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(clean, &clean_output) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              rejected_output, &rejected_data, &rejected_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              clean_output, &clean_data, &clean_size) ==
          QUANTAPDF_OK);
    CHECK(rejected_size == clean_size);
    CHECK(std::memcmp(rejected_data, clean_data, clean_size) == 0);

    quantapdf_drop_output(clean_output);
    quantapdf_drop_output(rejected_output);
    quantapdf_drop_composer(clean);
    quantapdf_drop_composer(rejected);
    return 0;
}

static int test_v3a_nonconformal_path_and_pattern()
{
    static char const svg_path[] =
        "<svg viewBox=\"0 0 100 100\">"
        "<path d=\"M10 20 L40 20\" fill=\"none\" stroke=\"#000000\" "
        "stroke-width=\"4\" stroke-dasharray=\"5 3\" "
        "transform=\"matrix(2 0 0 1 0 0)\"/>"
        "</svg>";
    static char const svg_pattern[] =
        "<svg viewBox=\"0 0 120 100\">"
        "<defs>"
        "<pattern id=\"P\" patternUnits=\"userSpaceOnUse\" "
        "patternContentUnits=\"userSpaceOnUse\" "
        "x=\"0\" y=\"0\" width=\"10\" height=\"10\" "
        "patternTransform=\"translate(2 3)\">"
        "<rect x=\"0\" y=\"0\" width=\"5\" height=\"10\" fill=\"#ff0000\"/>"
        "</pattern>"
        "</defs>"
        "<rect x=\"0\" y=\"0\" width=\"80\" height=\"60\" fill=\"url(#P)\"/>"
        "<path d=\"M0 80 L100 80\" fill=\"none\" stroke=\"url(#P)\" "
        "stroke-width=\"6\"/>"
        "</svg>";

    {
        quantapdf_composer* composer = nullptr;
        quantapdf_output* output = nullptr;
        unsigned char const* data = nullptr;
        size_t size = 0u;
        quantapdf_rect bounds = {0.0f, 0.0f, 100.0f, 100.0f};

        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg_path, bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);

        CHECK(quantapdf_test_pdf_content_contains(
            data, size, 0u, "2 0 0 1 0 0 cm"));
        CHECK(quantapdf_test_pdf_content_contains(
            data, size, 0u, "[5 3] 0 d"));
        CHECK(quantapdf_test_pdf_content_contains(
            data, size, 0u, "4 w"));
        CHECK(quantapdf_test_pdf_content_contains(
            data, size, 0u, "10 220 m 40 220 l S"));

        quantapdf_drop_output(output);
        quantapdf_drop_composer(composer);
    }

    {
        quantapdf_composer* composer = nullptr;
        quantapdf_output* first = nullptr;
        quantapdf_output* second = nullptr;
        quantapdf_document* document = nullptr;
        quantapdf_page* page = nullptr;
        quantapdf_bitmap* bitmap = nullptr;
        quantapdf_render_options render = {};
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
        double bbox[4] = {};
        double x_step = 0.0;
        double y_step = 0.0;
        double matrix[6] = {};
        int has_tile_form = 0;
        quantapdf_rect bounds = {0.0f, 0.0f, 120.0f, 100.0f};

        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg_pattern, bounds) == QUANTAPDF_OK);

        CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
        CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
              QUANTAPDF_OK);
        CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
              QUANTAPDF_OK);
        CHECK(first_size == second_size);
        CHECK(std::memcmp(first_data, second_data, first_size) == 0);

        CHECK(quantapdf_test_pdf_tiling_pattern_info(
            first_data,
            first_size,
            0u,
            1u,
            bbox,
            &x_step,
            &y_step,
            matrix,
            &has_tile_form));
        CHECK(std::fabs(bbox[0]) < 0.001);
        CHECK(std::fabs(bbox[1]) < 0.001);
        CHECK(std::fabs(bbox[2] - 10.0) < 0.001);
        CHECK(std::fabs(bbox[3] - 10.0) < 0.001);
        CHECK(std::fabs(x_step - 10.0) < 0.001);
        CHECK(std::fabs(y_step + 10.0) < 0.001);
        CHECK(std::fabs(matrix[0] - 1.0) < 0.001);
        CHECK(std::fabs(matrix[3] - 1.0) < 0.001);
        CHECK(std::fabs(matrix[4] - 2.0) < 0.001);
        CHECK(std::fabs(matrix[5] - 227.0) < 0.001);
        CHECK(has_tile_form == 1);

        CHECK(quantapdf_test_pdf_content_contains(
            first_data, first_size, 0u, "/Pattern cs /P1 scn"));
        CHECK(quantapdf_test_pdf_content_contains(
            first_data, first_size, 0u, "/Pattern CS /P1 SCN"));

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
        CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
              QUANTAPDF_OK);

        CHECK(pixel_near(pixels, stride, 4, 5, 255, 0, 0, 35));
        CHECK(pixel_near(pixels, stride, 9, 5, 255, 255, 255, 25));
        CHECK(pixel_near(pixels, stride, 14, 5, 255, 0, 0, 35));

        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
        quantapdf_close(document);
        quantapdf_drop_output(second);
        quantapdf_drop_output(first);
        quantapdf_drop_composer(composer);
    }

    return 0;
}

static int test_v3a_group_opacity_and_dom_order()
{
    static char const svg[] =
        "<svg viewBox=\"0 0 120 80\">"
        "<rect x=\"0\" y=\"0\" width=\"120\" height=\"80\" fill=\"#ffffff\"/>"
        "<g opacity=\"0.5\">"
        "<rect x=\"10\" y=\"10\" width=\"60\" height=\"50\" fill=\"#ff0000\"/>"
        "<rect x=\"40\" y=\"10\" width=\"60\" height=\"50\" fill=\"#0000ff\"/>"
        "<g opacity=\"0.5\">"
        "<rect x=\"10\" y=\"60\" width=\"30\" height=\"20\" fill=\"#ff0000\"/>"
        "</g>"
        "</g>"
        "<rect x=\"80\" y=\"20\" width=\"30\" height=\"30\" fill=\"#00ff00\"/>"
        "</svg>";
    static char const root_svg[] =
        "<svg viewBox=\"0 0 100 60\" opacity=\"0.5\">"
        "<rect x=\"0\" y=\"0\" width=\"60\" height=\"60\" fill=\"#ff0000\"/>"
        "<rect x=\"40\" y=\"0\" width=\"60\" height=\"60\" fill=\"#0000ff\"/>"
        "</svg>";

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
        int has_group = 0;
        int isolated = 0;
        int knockout = 0;
        quantapdf_rect bounds = {0.0f, 0.0f, 120.0f, 80.0f};

        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, svg, bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);

        CHECK(quantapdf_test_pdf_form_group_info(
            data, size, 0u, 1u,
            &has_group, &isolated, &knockout));
        CHECK(has_group == 1 && isolated == 1 && knockout == 0);

        CHECK(quantapdf_output_save_file(
                  output, COMPOSER_SVG_OUTPUT_PDF) == QUANTAPDF_OK);
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
        CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
              QUANTAPDF_OK);

        CHECK(pixel_near(pixels, stride, 20, 20, 255, 128, 128, 35));
        CHECK(pixel_near(pixels, stride, 50, 20, 128, 128, 255, 35));
        CHECK(pixel_near(pixels, stride, 90, 30, 0, 255, 0, 25));
        CHECK(pixel_near(pixels, stride, 20, 70, 255, 191, 191, 40));

        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
        quantapdf_close(document);
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
        unsigned char const* pixels = nullptr;
        size_t pixel_size = 0u;
        int width = 0;
        int height = 0;
        int stride = 0;
        int components = 0;
        quantapdf_rect bounds = {10.0f, 10.0f, 110.0f, 70.0f};

        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, root_svg, bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_save_file(
                  output, COMPOSER_SVG_OUTPUT_PDF) == QUANTAPDF_OK);
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
        CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) ==
              QUANTAPDF_OK);

        CHECK(pixel_near(pixels, stride, 20, 20, 255, 128, 128, 35));
        CHECK(pixel_near(pixels, stride, 60, 20, 128, 128, 255, 35));

        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
        quantapdf_close(document);
        quantapdf_drop_output(output);
        quantapdf_drop_composer(composer);
    }

    return 0;
}

static int test_v3a_rollback_and_unsupported_pattern_units()
{
    static char const* unsupported[] = {
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<pattern id=\"p\" width=\"1\" height=\"1\">"
        "<rect width=\"1\" height=\"1\" fill=\"red\"/></pattern>"
        "</defs><rect width=\"10\" height=\"10\" fill=\"url(#p)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<pattern id=\"p\" patternUnits=\"userSpaceOnUse\" "
        "patternContentUnits=\"objectBoundingBox\" width=\"1\" height=\"1\">"
        "<rect width=\"1\" height=\"1\" fill=\"red\"/></pattern>"
        "</defs><rect width=\"10\" height=\"10\" fill=\"url(#p)\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<pattern id=\"a\" patternUnits=\"userSpaceOnUse\" width=\"1\" height=\"1\">"
        "<rect width=\"1\" height=\"1\" fill=\"url(#b)\"/></pattern>"
        "<pattern id=\"b\" patternUnits=\"userSpaceOnUse\" width=\"1\" height=\"1\">"
        "<rect width=\"1\" height=\"1\" fill=\"url(#a)\"/></pattern>"
        "</defs><rect width=\"10\" height=\"10\" fill=\"url(#a)\"/></svg>"
    };
    quantapdf_rect bounds = {0.0f, 0.0f, 100.0f, 100.0f};
    for (char const* text: unsupported) {
        quantapdf_composer* composer = nullptr;
        CHECK(quantapdf_composer_create(nullptr, &composer) == QUANTAPDF_OK);
        CHECK(add_page(composer));
        CHECK(draw(composer, text, bounds) == QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_drop_composer(composer);
    }

    static char const group_svg[] =
        "<svg viewBox=\"0 0 10 10\"><g opacity=\"0.5\">"
        "<rect width=\"10\" height=\"10\" fill=\"red\"/></g></svg>";
    static char const solid_svg[] =
        "<svg viewBox=\"0 0 10 10\">"
        "<rect width=\"10\" height=\"10\" fill=\"red\"/></svg>";

    quantapdf_composer_options composer_options = {};
    composer_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    composer_options.max_resource_bytes = 256u;
    quantapdf_composer* rejected = nullptr;
    quantapdf_composer* clean = nullptr;
    quantapdf_output* rejected_output = nullptr;
    quantapdf_output* clean_output = nullptr;
    unsigned char const* rejected_data = nullptr;
    unsigned char const* clean_data = nullptr;
    size_t rejected_size = 0u;
    size_t clean_size = 0u;

    CHECK(quantapdf_composer_create(&composer_options, &rejected) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_create(&composer_options, &clean) ==
          QUANTAPDF_OK);
    CHECK(add_page(rejected));
    CHECK(add_page(clean));
    CHECK(draw(rejected, group_svg, bounds) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(draw(rejected, solid_svg, bounds) == QUANTAPDF_OK);
    CHECK(draw(clean, solid_svg, bounds) == QUANTAPDF_OK);
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

static int test_security_and_unsupported_features()
{
    static char const* cases[] = {
        "<!DOCTYPE svg [<!ENTITY x SYSTEM \"file:///etc/passwd\">]>"
        "<svg viewBox=\"0 0 10 10\"><path d=\"M0 0 L1 1\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><script>alert(1)</script></svg>",
        "<svg viewBox=\"0 0 10 10\"><image href=\"https://example.com/a.png\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><use href=\"#shape\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><text>hello</text></svg>",
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
        "<svg viewBox=\"0 0 10 10\"><polygon points=\"1,1 2,2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><rect width=\"-1\" height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><g><rect width=\"2\" height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><rect width=\"2\" width=\"3\" height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><rect width=\"2 height=\"2\"/></svg>",
        "<svg viewBox=\"0 0 10 10\"><path d=\"M1 1 A2 2 0 2 0 5 5\"/></svg>"
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
    CHECK(test_v2a_dash_opacity_arcs_and_aspect() == 0);
    CHECK(test_v2b1_gradient_and_clip_references() == 0);
    CHECK(test_v2b1_reference_failures_and_rollback() == 0);
    CHECK(test_v2b2_symbol_use_form_reuse() == 0);
    CHECK(test_v2b2_symbol_reference_failures_and_rollback() == 0);
    CHECK(test_v3a_nonconformal_path_and_pattern() == 0);
    CHECK(test_v3a_group_opacity_and_dom_order() == 0);
    CHECK(test_v3a_rollback_and_unsupported_pattern_units() == 0);
    CHECK(test_security_and_unsupported_features() == 0);
    CHECK(test_malformed_inputs() == 0);
    CHECK(test_atomic_capacity_failure() == 0);
    return 0;
}
