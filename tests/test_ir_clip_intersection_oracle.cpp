/* Test-only PDFium oracle for the strict native flat Clip intersection.
 * Production Native Raster has no PDFium/qpdf linkage or fallback. */
#include "backend/qpdf_composer.h"
#include "render/ir_clip_intersection_v1.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "%s:%d: oracle check failed: %s\n", \
                 __FILE__, __LINE__, #x); std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

namespace {
constexpr size_t side = 64;
const quantapdf_ir_raster_v1_limits limits = {4096, 1, 256, 32000000};

struct rgb {
    unsigned red, green, blue;
};

struct observation {
    size_t x, y;
    rgb expected;
};

struct scene {
    quantapdf_ir ir{};
    quantapdf_composer_page_state page{};
    quantapdf_composer_operation operation{};
    quantapdf_composer_graphics_state state{};
    quantapdf_composer_clip_state clips[4]{};
    quantapdf_composer_path_command paint[5]{};
    quantapdf_composer_path_command members_paths[3][5]{};
    quantapdf_composer_clip_id members[3]{1u, 2u, 3u};

    explicit scene(size_t clip_members)
    {
        CHECK(clip_members >= 2u && clip_members <= 3u);
        page.width_points = page.height_points = (float)side;
        page.background_argb = UINT32_C(0xffffffff);
        ir.pages = &page;
        ir.page_count = 1u;
        ir.operations = &operation;
        ir.operation_count = 1u;
        ir.resources.graphics_states = &state;
        ir.resources.graphics_state_count = 1u;
        ir.resources.clips = clips;
        ir.resources.clip_count = clip_members + 1u;

        operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        operation.graphics_state_id = 1u;
        operation.value.path.commands = paint;
        operation.value.path.command_count = 5u;
        auto& options = operation.value.path.options;
        options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        options.fill = 1;
        options.fill_argb = UINT32_C(0xffff0000);
        options.graphics_state_id = 1u;
        options.transform = quantapdf_affine_transform{1, 0, 0, 1, 0, 0};

        state.fill_alpha = state.stroke_alpha = 1.0f;
        state.clip_id = clip_members + 1u;
        state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
        for (size_t i = 0; i < clip_members; ++i) {
            clips[i].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
            clips[i].commands = members_paths[i];
            clips[i].command_count = 5u;
            clips[i].fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
            clips[i].transform = quantapdf_affine_transform{1, 0, 0, 1, 0, 0};
        }
        auto& root = clips[clip_members];
        root.kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        root.members = members;
        root.member_count = clip_members;
        rectangle(paint, 4, 4, 60, 60);
        rectangle(members_paths[0], 8, 8, 50, 50);
        rectangle(members_paths[1], 24, 16, 56, 40);
        if (clip_members == 3u)
            rectangle(members_paths[2], 30, 24, 52, 45);
    }

    static void rectangle(quantapdf_composer_path_command *p,
                          float x0, float y0, float x1, float y1)
    {
        const quantapdf_composer_path_command_kind kinds[5] = {
            QUANTAPDF_COMPOSER_PATH_MOVE_TO,
            QUANTAPDF_COMPOSER_PATH_LINE_TO,
            QUANTAPDF_COMPOSER_PATH_LINE_TO,
            QUANTAPDF_COMPOSER_PATH_LINE_TO,
            QUANTAPDF_COMPOSER_PATH_CLOSE
        };
        const quantapdf_point points[5] = {
            {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {0, 0}
        };
        for (size_t i = 0; i < 5u; ++i) {
            p[i].kind = kinds[i];
            p[i].point1 = points[i];
        }
    }
};

struct pdfium_image {
    int width = 0, height = 0, stride = 0, components = 0;
    std::vector<unsigned char> data;
};

pdfium_image render_pdfium(const char *name)
{
    pdfium_image result;
    quantapdf_document *doc = nullptr;
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *bitmap = nullptr;
    const unsigned char *borrowed = nullptr;
    size_t available = 0u;
    OK(quantapdf_open(name, nullptr, &doc));
    OK(quantapdf_load_page(doc, 0, &page));
    OK(quantapdf_render_page(page, &bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap, &result.width, &result.height,
                                   &result.stride, &result.components));
    OK(quantapdf_bitmap_data(bitmap, &borrowed, &available));
    CHECK(result.width == (int)side && result.height == (int)side);
    CHECK(result.components == 3 && result.stride >= (int)(side * 3u));
    CHECK(available >= side * (size_t)result.stride);
    result.data.assign(borrowed, borrowed + side * (size_t)result.stride);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return result;
}

void write_pdf(const char *name, const unsigned char *bytes, size_t size)
{
    FILE *f = std::fopen(name, "wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(bytes, 1, size, f) == size);
    CHECK(std::fclose(f) == 0);
}

void run_case(scene& s, const char *filename,
              const observation *samples, size_t count)
{
    quantapdf_ir_raster_image native{};
    quantapdf_ir_raster_image again{};
    unsigned char *pdf = nullptr;
    size_t pdf_size = 0;
    size_t large_mismatch = 0;
    double total_difference = 0.0;
    OK(quantapdf_ir_raster_clip_intersection_v1(&s.ir, 0u, &limits, &native));
    CHECK(native.pixels != nullptr && native.width == side &&
          native.height == side && native.stride == side * 4u);
    OK(quantapdf_ir_raster_clip_intersection_v1(&s.ir, 0u, &limits, &again));
    CHECK(again.size == native.size &&
          std::memcmp(again.pixels, native.pixels, native.size) == 0);
    quantapdf_ir_raster_image_dispose(&again);

    OK(quantapdf_qpdf_compose(&s.ir, &pdf, &pdf_size));
    CHECK(pdf != nullptr && pdf_size > 0u);
    write_pdf(filename, pdf, pdf_size);
    std::free(pdf);
    pdfium_image reference = render_pdfium(filename);
    CHECK(std::remove(filename) == 0);

    for (size_t y = 0; y < side; ++y)
        for (size_t x = 0; x < side; ++x) {
            const unsigned char *a =
                native.pixels + y * native.stride + 4u * x;
            const unsigned char *b =
                reference.data.data() + y * (size_t)reference.stride + 3u * x;
            int largest = 0;
            CHECK(a[3] == 255u);
            for (size_t c = 0; c < 3u; ++c) {
                int delta = std::abs((int)a[c] - (int)b[c]);
                if (delta > largest) largest = delta;
                total_difference += (double)delta;
            }
            if (largest > 64) ++large_mismatch;
        }
    const double mean = total_difference / (double)(side * side * 3u);
    if (mean > 10.0 || large_mismatch > side * side / 12u) {
        std::fprintf(stderr, "native clip oracle mismatch %s mean=%f bad=%zu\n",
                     filename, mean, large_mismatch);
        CHECK(false);
    }
    for (size_t i = 0u; i < count; ++i) {
        const observation& sample = samples[i];
        CHECK(sample.x < side && sample.y < side);
        const unsigned char *a =
            native.pixels + sample.y * native.stride + 4u * sample.x;
        const unsigned char *b =
            reference.data.data() +
            sample.y * (size_t)reference.stride + 3u * sample.x;
        const unsigned values[3] = {
            sample.expected.red, sample.expected.green, sample.expected.blue
        };
        for (size_t channel = 0u; channel < 3u; ++channel) {
            CHECK(std::abs((int)a[channel] - (int)values[channel]) <= 3);
            CHECK(std::abs((int)b[channel] - (int)values[channel]) <= 3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native);
}
}

int main()
{
    scene two(2u);
    const observation two_golden[] = {
        {28, 20, {255, 0, 0}}, {40, 35, {255, 0, 0}},
        {20, 20, {255, 255, 255}}, {52, 30, {255, 255, 255}},
        {35, 44, {255, 255, 255}}
    };
    run_case(two, ORACLE_TWO_PDF, two_golden,
             sizeof(two_golden) / sizeof(two_golden[0]));

    scene three(3u);
    const observation three_golden[] = {
        {32, 28, {255, 0, 0}}, {48, 38, {255, 0, 0}},
        {26, 20, {255, 255, 255}}, {27, 35, {255, 255, 255}},
        {52, 38, {255, 255, 255}}
    };
    run_case(three, ORACLE_THREE_PDF, three_golden,
             sizeof(three_golden) / sizeof(three_golden[0]));

    std::fprintf(stderr, "[quantapdf.native_clip_intersection_oracle] complete\n");
    return EXIT_SUCCESS;
}
