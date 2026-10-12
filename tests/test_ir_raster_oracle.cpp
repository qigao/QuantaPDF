/* Test-only PDFium oracle. Production native IR raster has no PDFium/qpdf
 * linkage. This executable deliberately links both for differential testing.
 * Compare semantics at selected interior samples and bound the error on
 * antialiased edges instead of claiming cross-renderer bitwise equality. */
#include "render/ir_raster_v1.h"
#include "backend/qpdf_composer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "%s:%d: oracle failed: %s\n", __FILE__, __LINE__, #x); \
    std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

namespace {
constexpr size_t side = 64;
const quantapdf_ir_raster_v1_limits limits = {
    side * side, 4u, 512u, 32000000u
};

struct scene {
    quantapdf_ir ir{};
    quantapdf_composer_page_state page{};
    quantapdf_composer_operation op[2]{};
    quantapdf_composer_path_command path[2][12]{};

    scene()
    {
        page.width_points = (float)side;
        page.height_points = (float)side;
        page.background_argb = UINT32_C(0xffffffff);
        ir.pages = &page;
        ir.page_count = 1;
        ir.operations = op;
    }

    void add(size_t index, size_t command_count, uint32_t color,
             quantapdf_composer_fill_rule rule = QUANTAPDF_COMPOSER_FILL_NONZERO)
    {
        CHECK(index < 2 && command_count <= 12 && command_count != 0);
        op[index].kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        op[index].page_index = 0;
        op[index].value.path.commands = path[index];
        op[index].value.path.command_count = command_count;
        auto& opts = op[index].value.path.options;
        opts.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        opts.fill = 1;
        opts.fill_argb = color;
        opts.fill_rule = rule;
        ir.operation_count = index + 1;
    }

    void command(size_t index, size_t n,
                 quantapdf_composer_path_command_kind kind,
                 float x, float y)
    {
        CHECK(index < 2 && n < 12);
        auto& cmd = path[index][n];
        cmd.kind = kind;
        cmd.point1.x = x;
        cmd.point1.y = y;
    }
};

void rectangle(scene& s, size_t op, float x0, float y0, float x1, float y1)
{
    s.command(op, 0, QUANTAPDF_COMPOSER_PATH_MOVE_TO, x0, y0);
    s.command(op, 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1, y0);
    s.command(op, 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1, y1);
    s.command(op, 3, QUANTAPDF_COMPOSER_PATH_LINE_TO, x0, y1);
    s.command(op, 4, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
}

void save(const char *name, const unsigned char *data, size_t size)
{
    FILE *f = std::fopen(name, "wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(data, 1, size, f) == size);
    CHECK(std::fclose(f) == 0);
}

struct pdfium_pixels {
    int width = 0, height = 0, stride = 0, components = 0;
    std::vector<unsigned char> bytes;
};

pdfium_pixels render_pdfium(const char *name)
{
    pdfium_pixels out;
    quantapdf_document *doc = nullptr;
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *bitmap = nullptr;
    const unsigned char *bytes = nullptr;
    size_t size = 0;
    OK(quantapdf_open(name, nullptr, &doc));
    OK(quantapdf_load_page(doc, 0, &page));
    OK(quantapdf_render_page(page, &bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap, &out.width, &out.height,
                                   &out.stride, &out.components));
    OK(quantapdf_bitmap_data(bitmap, &bytes, &size));
    CHECK(out.width == (int)side && out.height == (int)side &&
          out.components == 3 && out.stride >= (int)(side * 3u) &&
          size >= side * (size_t)out.stride);
    out.bytes.assign(bytes, bytes + side * (size_t)out.stride);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return out;
}

struct sample {
    size_t x, y;
    unsigned r, g, b;
};

void compare(scene& s, const char *name,
             const sample *samples, size_t sample_count)
{
    quantapdf_ir_raster_image native = {};
    quantapdf_ir_raster_image repeated = {};
    unsigned char *pdf = nullptr;
    size_t pdf_size = 0;
    double total_error = 0;
    size_t large_mismatch = 0;
    OK(quantapdf_ir_raster_render_v1(&s.ir, 0, &limits, &native));
    CHECK(native.width == side && native.height == side &&
          native.stride == side * 4u && native.pixels != nullptr);
    OK(quantapdf_ir_raster_render_v1(&s.ir, 0, &limits, &repeated));
    CHECK(repeated.size == native.size &&
          std::memcmp(native.pixels, repeated.pixels, native.size) == 0);
    quantapdf_ir_raster_image_dispose(&repeated);

    OK(quantapdf_qpdf_compose(&s.ir, &pdf, &pdf_size));
    CHECK(pdf != nullptr && pdf_size > 0u);
    save(name, pdf, pdf_size);
    std::free(pdf);
    pdfium_pixels reference = render_pdfium(name);
    CHECK(std::remove(name) == 0);

    for (size_t y = 0; y < side; ++y) {
        for (size_t x = 0; x < side; ++x) {
            const unsigned char *a = native.pixels + y * native.stride + 4u * x;
            const unsigned char *b = reference.bytes.data() +
                                     y * (size_t)reference.stride + 3u * x;
            int worst = 0;
            CHECK(a[3] == 255u);
            for (size_t c = 0; c < 3u; ++c) {
                int delta = std::abs((int)a[c] - (int)b[c]);
                if (delta > worst) worst = delta;
                total_error += (double)delta;
            }
            if (worst > 64)
                ++large_mismatch;
        }
    }
    /* Independent renderers use different edge AA. These bounds are
     * intentionally tolerant at edges but reject bulk image mismatches. */
    double mean_error = total_error / (double)(side * side * 3u);
    if (mean_error > 10.0 || large_mismatch > side * side / 12u) {
        std::fprintf(stderr,
            "%s: PDFium/native mismatch: mean=%.3f, changed=%zu/%zu\n",
            name, mean_error, large_mismatch, side * side);
        CHECK(false);
    }

    for (size_t i = 0; i < sample_count; ++i) {
        const auto& s0 = samples[i];
        CHECK(s0.x < side && s0.y < side);
        const unsigned char *a = native.pixels +
            s0.y * native.stride + s0.x * 4u;
        const unsigned char *b = reference.bytes.data() +
            s0.y * (size_t)reference.stride + s0.x * 3u;
        CHECK(std::abs((int)a[0] - (int)s0.r) <= 3 &&
              std::abs((int)a[1] - (int)s0.g) <= 3 &&
              std::abs((int)a[2] - (int)s0.b) <= 3);
        CHECK(std::abs((int)b[0] - (int)s0.r) <= 3 &&
              std::abs((int)b[1] - (int)s0.g) <= 3 &&
              std::abs((int)b[2] - (int)s0.b) <= 3);
    }
    quantapdf_ir_raster_image_dispose(&native);
}

void test_rectangles()
{
    scene s;
    s.add(0, 5, UINT32_C(0xffff0000));
    rectangle(s, 0, 8, 8, 40, 40);
    s.add(1, 5, UINT32_C(0xff0000ff));
    rectangle(s, 1, 24, 24, 56, 56);
    const sample samples[] = {
        {2, 2, 255, 255, 255}, {12, 12, 255, 0, 0},
        {32, 32, 0, 0, 255}, {48, 48, 0, 0, 255}
    };
    compare(s, ORACLE_RECT_PDF, samples, 4u);
}

void test_evenodd_hole()
{
    scene s;
    s.add(0, 10, UINT32_C(0xff008000),
          QUANTAPDF_COMPOSER_FILL_EVEN_ODD);
    /* Explicitly close both subpaths using the same orientation. */
    rectangle(s, 0, 4, 4, 60, 60);
    s.command(0, 5, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 20, 20);
    s.command(0, 6, QUANTAPDF_COMPOSER_PATH_LINE_TO, 44, 20);
    s.command(0, 7, QUANTAPDF_COMPOSER_PATH_LINE_TO, 44, 44);
    s.command(0, 8, QUANTAPDF_COMPOSER_PATH_LINE_TO, 20, 44);
    s.command(0, 9, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
    const sample samples[] = {
        {8, 8, 0, 128, 0}, {32, 24, 255, 255, 255},
        {50, 50, 0, 128, 0}, {2, 2, 255, 255, 255}
    };
    compare(s, ORACLE_HOLE_PDF, samples, 4u);
}

void test_triangle_affine()
{
    scene s;
    s.add(0, 4, UINT32_C(0xff0000ff));
    s.command(0, 0, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 6, 6);
    s.command(0, 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, 50, 6);
    s.command(0, 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, 6, 50);
    s.command(0, 3, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
    s.op[0].value.path.options.transform =
        quantapdf_affine_transform{1, 0, 0.125f, 1, 0, 0};
    const sample samples[] = {
        {12, 12, 0, 0, 255}, {56, 56, 255, 255, 255}
    };
    compare(s, ORACLE_TRIANGLE_PDF, samples, 2u);
}

void test_cubic()
{
    scene s;
    s.add(0, 3, UINT32_C(0xffff0000));
    s.command(0, 0, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 8, 50);
    auto& cmd = s.path[0][1];
    cmd.kind = QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    cmd.point1 = {8, 10};
    cmd.point2 = {56, 10};
    cmd.point3 = {56, 50};
    s.command(0, 2, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
    const sample samples[] = {
        {32, 38, 255, 0, 0}, {32, 8, 255, 255, 255},
        {3, 40, 255, 255, 255}
    };
    compare(s, ORACLE_CUBIC_PDF, samples, 3u);
}
}

int main()
{
    test_rectangles();
    test_evenodd_hole();
    test_triangle_affine();
    test_cubic();
    std::fprintf(stderr, "[quantapdf.native_ir_raster_oracle] complete\n");
    return EXIT_SUCCESS;
}
