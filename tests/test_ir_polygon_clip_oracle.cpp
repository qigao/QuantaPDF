/* Independent PDFium oracle: Native Polygon Clip V2 itself is pure C11
 * with no backend linkage. Only this conformance executable depends on
 * QuantaPDF's existing PDF writer and PDFium reader/raster renderer. */
#include "backend/qpdf_composer.h"
#include "render/ir_clip_path_v2.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(e) do { if (!(e)) { \
    std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #e); \
    std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

namespace {
constexpr size_t SIDE = 64u;
const quantapdf_ir_raster_v1_limits limits = {4096u, 1u, 512u, 32000000u};

struct scene {
    quantapdf_ir ir{};
    quantapdf_composer_page_state page{};
    quantapdf_composer_operation operation{};
    quantapdf_composer_graphics_state state{};
    quantapdf_composer_clip_state clip{};
    quantapdf_composer_path_command paint[5]{}, shape[12]{};

    scene()
    {
        page.width_points = page.height_points = (float)SIDE;
        page.background_argb = UINT32_C(0xffffffff);
        ir.pages = &page;
        ir.page_count = 1u;
        ir.operations = &operation;
        ir.operation_count = 1u;
        ir.resources.graphics_states = &state;
        ir.resources.graphics_state_count = 1u;
        ir.resources.graphics_state_capacity = 1u;
        ir.resources.clips = &clip;
        ir.resources.clip_count = 1u;
        ir.resources.clip_capacity = 1u;
        operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        operation.graphics_state_id = 1u;
        operation.value.path.commands = paint;
        operation.value.path.command_count = 5u;
        auto& opt = operation.value.path.options;
        opt.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        opt.fill = 1;
        opt.fill_argb = UINT32_C(0xffff0000);
        opt.graphics_state_id = 1u;
        opt.transform = quantapdf_affine_transform{1, 0, 0, 1, 0, 0};
        state.clip_id = 1u;
        state.fill_alpha = state.stroke_alpha = 1.0f;
        state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
        clip.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.commands = shape;
        clip.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
        clip.transform = quantapdf_affine_transform{1, 0, 0, 1, 0, 0};
        rect(paint, 0, 0, 64, 64);
    }

    static void cmd(quantapdf_composer_path_command *out,
                    quantapdf_composer_path_command_kind kind,
                    float x, float y)
    {
        *out = quantapdf_composer_path_command{};
        out->kind = kind;
        out->point1 = quantapdf_point{x, y};
    }

    static void rect(quantapdf_composer_path_command *p,
                     float left, float top, float right, float bottom)
    {
        cmd(p, QUANTAPDF_COMPOSER_PATH_MOVE_TO, left, top);
        cmd(p+1, QUANTAPDF_COMPOSER_PATH_LINE_TO, right, top);
        cmd(p+2, QUANTAPDF_COMPOSER_PATH_LINE_TO, right, bottom);
        cmd(p+3, QUANTAPDF_COMPOSER_PATH_LINE_TO, left, bottom);
        cmd(p+4, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
    }
};

struct reference {
    int width=0, height=0, stride=0, channels=0;
    std::vector<unsigned char> pixels;
};

reference render_pdfium(char const* file)
{
    quantapdf_document *doc = nullptr;
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *bitmap = nullptr;
    const unsigned char *pixels = nullptr;
    size_t bytes = 0u;
    reference out;
    OK(quantapdf_open(file, nullptr, &doc));
    OK(quantapdf_load_page(doc, 0, &page));
    OK(quantapdf_render_page(page, &bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap, &out.width, &out.height,
                                   &out.stride, &out.channels));
    OK(quantapdf_bitmap_data(bitmap, &pixels, &bytes));
    CHECK(out.width == (int)SIDE && out.height == (int)SIDE &&
          out.channels == 3 && out.stride >= (int)(SIDE * 3u));
    CHECK(bytes >= SIDE * (size_t)out.stride);
    out.pixels.assign(pixels, pixels + SIDE * (size_t)out.stride);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return out;
}

struct probe {
    size_t x, y;
    unsigned red, green, blue;
};

void compare(scene& s, const char* name, const probe *probes, size_t n)
{
    quantapdf_ir_raster_image native{};
    quantapdf_ir_raster_image again{};
    unsigned char *pdf = nullptr;
    size_t size = 0u;
    size_t gross = 0u;
    double error = 0.0;

    OK(quantapdf_ir_raster_clip_path_v2(&s.ir, 0u, &limits, &native));
    OK(quantapdf_ir_raster_clip_path_v2(&s.ir, 0u, &limits, &again));
    CHECK(native.size == again.size &&
          std::memcmp(native.pixels, again.pixels, native.size) == 0);
    quantapdf_ir_raster_image_dispose(&again);

    OK(quantapdf_qpdf_compose(&s.ir, &pdf, &size));
    CHECK(pdf != nullptr && size > 0u);
    FILE *f = std::fopen(name, "wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(pdf, 1, size, f) == size);
    CHECK(std::fclose(f) == 0);
    std::free(pdf);
    reference rendered = render_pdfium(name);
    CHECK(std::remove(name) == 0);

    for (size_t y=0; y<SIDE; ++y) for(size_t x=0; x<SIDE; ++x) {
        const unsigned char *a =
            native.pixels + y * native.stride + x * 4u;
        const unsigned char *b =
            rendered.pixels.data() + y * (size_t)rendered.stride + x * 3u;
        int worst = 0;
        CHECK(a[3] == 255u);
        for (size_t c=0; c<3u; ++c) {
            int delta = std::abs((int)a[c]-(int)b[c]);
            if (delta > worst) worst = delta;
            error += (double)delta;
        }
        if (worst > 64) ++gross;
    }
    double mean = error / (double)(SIDE * SIDE * 3u);
    if (mean > 10.0 || gross > SIDE * SIDE / 12u) {
        std::fprintf(stderr, "%s native/PDFium mismatch mean=%.3f gross=%zu\n",
                     name, mean, gross);
        CHECK(false);
    }
    for (size_t i=0; i<n; ++i) {
        const probe& p = probes[i];
        CHECK(p.x < SIDE && p.y < SIDE);
        const unsigned char *a =
            native.pixels + p.y * native.stride + p.x * 4u;
        const unsigned char *b =
            rendered.pixels.data() + p.y * (size_t)rendered.stride + p.x * 3u;
        const unsigned expected[] = {p.red,p.green,p.blue};
        for (size_t c=0; c<3u; ++c) {
            CHECK(std::abs((int)a[c]-(int)expected[c]) <= 3);
            CHECK(std::abs((int)b[c]-(int)expected[c]) <= 3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native);
}

void triangle()
{
    scene s;
    scene::cmd(s.shape, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 4, 4);
    scene::cmd(s.shape+1, QUANTAPDF_COMPOSER_PATH_LINE_TO, 52, 4);
    scene::cmd(s.shape+2, QUANTAPDF_COMPOSER_PATH_LINE_TO, 4, 52);
    scene::cmd(s.shape+3, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
    s.clip.command_count=4u;
    const probe points[] = {
        {10,10,255,0,0}, {55,55,255,255,255},
        {50,25,255,255,255}
    };
    compare(s, ORACLE_TRIANGLE_PDF, points, 3u);
}

void hole()
{
    scene s;
    scene::rect(s.shape, 6,6,58,58);
    scene::rect(s.shape+5, 22,22,42,42);
    s.clip.command_count=10u;
    s.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    const probe points[] = {
        {12,12,255,0,0}, {32,32,255,255,255},
        {55,55,255,0,0}, {2,2,255,255,255}
    };
    compare(s, ORACLE_HOLE_PDF, points, 4u);
}

void fractional()
{
    scene s;
    scene::rect(s.shape, 8,8,48,48);
    s.clip.command_count=5u;
    s.clip.transform = quantapdf_affine_transform{1,0,0,1,0.5f,0};
    const probe points[] = {
        {20,20,255,0,0}, {4,20,255,255,255},
        {52,52,255,255,255}
    };
    compare(s, ORACLE_FRACTIONAL_PDF, points, 3u);
}

void bezier()
{
    scene s;
    scene::cmd(s.shape, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 8,50);
    s.shape[1].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    s.shape[1].point1=quantapdf_point{8,10};
    s.shape[1].point2=quantapdf_point{56,10};
    s.shape[1].point3=quantapdf_point{56,50};
    scene::cmd(s.shape+2, QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
    s.clip.command_count=3u;
    const probe points[]={
        {32,38,255,0,0}, {32,8,255,255,255},
        {3,40,255,255,255}
    };
    compare(s, ORACLE_BEZIER_PDF, points, 3u);
}
}

int main()
{
    triangle();
    hole();
    fractional();
    bezier();
    std::fprintf(stderr, "[quantapdf.native_polygon_clip_oracle] complete\n");
    return EXIT_SUCCESS;
}
