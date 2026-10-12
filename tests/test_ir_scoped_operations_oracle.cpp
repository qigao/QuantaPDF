/* Test-only PDFium reference for provider-free scoped native raster V0.
 * No PDFium/qpdf calls exist in the production C11 render implementation. */
#include "backend/qpdf_composer.h"
#include "render/ir_scoped_operations_v0.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(e) do { if (!(e)) { \
    std::fprintf(stderr, "%s:%d: oracle failed: %s\n", \
                 __FILE__, __LINE__, #e); std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

namespace {
constexpr size_t SIDE = 64u;
const quantapdf_ir_raster_v1_limits limits = {SIDE*SIDE, 4u, 256u, 32000000u};

struct probe {
    size_t x, y;
    unsigned r, g, b;
};

struct scene {
    quantapdf_ir ir{};
    quantapdf_composer_page_state page{};
    quantapdf_composer_operation operations[3]{};
    quantapdf_composer_graphics_state states[2]{};
    quantapdf_composer_clip_state clips[2]{};
    quantapdf_composer_path_command shape[3][5]{};
    quantapdf_composer_path_command mask[2][5]{};

    static void rect(quantapdf_composer_path_command *p,
                     float l, float t, float r, float b)
    {
        const quantapdf_composer_path_command_kind kinds[5] = {
            QUANTAPDF_COMPOSER_PATH_MOVE_TO,
            QUANTAPDF_COMPOSER_PATH_LINE_TO,
            QUANTAPDF_COMPOSER_PATH_LINE_TO,
            QUANTAPDF_COMPOSER_PATH_LINE_TO,
            QUANTAPDF_COMPOSER_PATH_CLOSE
        };
        const quantapdf_point points[5] = {
            {l,t}, {r,t}, {r,b}, {l,b}, {0,0}
        };
        for (size_t i=0u; i<5u; ++i) {
            p[i] = quantapdf_composer_path_command{};
            p[i].kind = kinds[i];
            p[i].point1 = points[i];
        }
    }

    scene()
    {
        const uint32_t colors[3] = {
            UINT32_C(0xffff0000), UINT32_C(0xff0000ff),
            UINT32_C(0xff00ff00)
        };
        page.width_points = page.height_points = (float)SIDE;
        page.background_argb = UINT32_C(0xffffffff);
        ir.pages = &page;
        ir.page_count = 1u;
        ir.operations = operations;
        ir.operation_count = 3u;
        ir.resources.graphics_states = states;
        ir.resources.graphics_state_count = 2u;
        ir.resources.graphics_state_capacity = 2u;
        ir.resources.clips = clips;
        ir.resources.clip_count = 2u;
        ir.resources.clip_capacity = 2u;
        for (size_t i=0u; i<3u; ++i) {
            auto& op = operations[i];
            op.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
            op.value.path.commands = shape[i];
            op.value.path.command_count = 5u;
            auto& opt = op.value.path.options;
            opt.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
            opt.fill = 1;
            opt.fill_argb = colors[i];
            opt.transform = quantapdf_affine_transform{1,0,0,1,0,0};
            rect(shape[i],4,4,56,56);
        }
        rect(shape[1],4,4,60,56);
        rect(shape[2],30,30,44,44);
        for (size_t i=0u; i<2u; ++i) {
            operations[i].graphics_state_id =
                (quantapdf_composer_graphics_state_id)(i+1u);
            operations[i].value.path.options.graphics_state_id =
                (quantapdf_composer_graphics_state_id)(i+1u);
            states[i].fill_alpha = states[i].stroke_alpha = 1.0f;
            states[i].clip_id = (quantapdf_composer_clip_id)(i+1u);
            states[i].blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
            clips[i].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
            clips[i].commands = mask[i];
            clips[i].command_count = 5u;
            clips[i].fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
            clips[i].transform = quantapdf_affine_transform{1,0,0,1,0,0};
        }
        rect(mask[0],8,8,40,48);
        rect(mask[1],24,12,56,52);
    }
};

struct pdfium_image {
    int width=0, height=0, stride=0, components=0;
    std::vector<unsigned char> pixels;
};

pdfium_image pdfium_render(const char *path)
{
    quantapdf_document *doc = nullptr;
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *bitmap = nullptr;
    const unsigned char *pixels = nullptr;
    size_t bytes = 0u;
    pdfium_image result;
    OK(quantapdf_open(path,nullptr,&doc));
    OK(quantapdf_load_page(doc,0,&page));
    OK(quantapdf_render_page(page,&bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap,&result.width,&result.height,
                                   &result.stride,&result.components));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&bytes));
    CHECK(result.width == (int)SIDE && result.height == (int)SIDE &&
          result.components == 3 && result.stride >= (int)(SIDE*3u));
    CHECK(bytes >= SIDE * (size_t)result.stride);
    result.pixels.assign(pixels,pixels+SIDE*(size_t)result.stride);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return result;
}

void compare(scene& s, const char *filename, const probe *samples, size_t n)
{
    quantapdf_ir_raster_image native{};
    quantapdf_ir_raster_image repeated{};
    unsigned char *pdf = nullptr;
    size_t pdf_size = 0u;
    size_t gross = 0u;
    double error = 0.0;
    OK(quantapdf_ir_raster_scoped_operations_v0(&s.ir,0u,&limits,&native));
    CHECK(native.size == SIDE*SIDE*4u && native.width == SIDE &&
          native.height == SIDE && native.stride == SIDE*4u);
    OK(quantapdf_ir_raster_scoped_operations_v0(&s.ir,0u,&limits,&repeated));
    CHECK(repeated.size == native.size &&
          std::memcmp(repeated.pixels,native.pixels,native.size) == 0);
    quantapdf_ir_raster_image_dispose(&repeated);

    OK(quantapdf_qpdf_compose(&s.ir,&pdf,&pdf_size));
    CHECK(pdf != nullptr && pdf_size > 0u);
    FILE *f = std::fopen(filename,"wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(pdf,1,pdf_size,f) == pdf_size);
    CHECK(std::fclose(f) == 0);
    std::free(pdf);
    pdfium_image expected = pdfium_render(filename);
    CHECK(std::remove(filename) == 0);

    for (size_t y=0; y<SIDE; ++y) for(size_t x=0; x<SIDE; ++x) {
        const unsigned char *a =
            native.pixels+y*native.stride+x*4u;
        const unsigned char *b =
            expected.pixels.data()+y*(size_t)expected.stride+x*3u;
        int largest=0;
        CHECK(a[3] == 255u);
        for (size_t c=0u; c<3u; ++c) {
            int diff=std::abs((int)a[c]-(int)b[c]);
            if(diff>largest)largest=diff;
            error+=(double)diff;
        }
        if(largest>64)++gross;
    }
    const double mean=error/(double)(SIDE*SIDE*3u);
    if(mean>10.0 || gross>SIDE*SIDE/12u) {
        std::fprintf(stderr,
            "%s: scoped native/PDFium mismatch mean=%.3f gross=%zu\n",
            filename,mean,gross);
        CHECK(false);
    }
    for(size_t i=0u; i<n; ++i) {
        const probe& p=samples[i];
        CHECK(p.x<SIDE && p.y<SIDE);
        const unsigned char *a =
            native.pixels+p.y*native.stride+p.x*4u;
        const unsigned char *b =
            expected.pixels.data()+p.y*(size_t)expected.stride+p.x*3u;
        const unsigned rgb[3]={p.r,p.g,p.b};
        for(size_t c=0u; c<3u; ++c) {
            CHECK(std::abs((int)a[c]-(int)rgb[c])<=3);
            CHECK(std::abs((int)b[c]-(int)rgb[c])<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native);
}

void test_scopes()
{
    scene s;
    const probe samples[]={
        {2,2,255,255,255},
        {12,12,255,0,0},
        {20,20,255,0,0},
        {32,20,0,0,255},
        {52,20,0,0,255},
        {34,34,0,255,0},
        {50,50,0,0,255},
        {20,50,255,255,255},
        {59,19,255,255,255}
    };
    compare(s,ORACLE_SCOPES_PDF,samples,sizeof(samples)/sizeof(samples[0]));
}

void test_no_leak()
{
    scene s;
    scene::rect(s.shape[2],0,0,12,12);
    const probe samples[]={
        {2,2,0,255,0}, /* third un-clipped operation outside BOTH clips */
        {10,10,0,255,0},
        {20,20,255,0,0},
        {32,20,0,0,255},
        {55,51,0,0,255},
        {59,59,255,255,255}
    };
    compare(s,ORACLE_NO_LEAK_PDF,samples,sizeof(samples)/sizeof(samples[0]));
}

void test_reversed_order()
{
    scene s;
    /* Each operation retains its own Clip state after swapping draw order. */
    quantapdf_composer_operation saved = s.operations[0];
    s.operations[0] = s.operations[1];
    s.operations[1] = saved;
    const probe samples[]={
        {12,12,255,0,0},
        {32,20,255,0,0}, /* red now painted AFTER blue in the overlap */
        {52,20,0,0,255},
        {34,34,0,255,0}
    };
    compare(s,ORACLE_REVERSED_PDF,samples,
            sizeof(samples)/sizeof(samples[0]));
}

void test_fractional_clip()
{
    scene s;
    s.clips[1].transform.e = 0.5f;
    const probe samples[]={
        {20,20,255,0,0}, {32,20,0,0,255},
        {34,34,0,255,0}, {59,19,255,255,255}
    };
    compare(s,ORACLE_FRACTIONAL_PDF,samples,
            sizeof(samples)/sizeof(samples[0]));
}
}

int main()
{
    test_scopes();
    test_no_leak();
    test_reversed_order();
    test_fractional_clip();
    std::fprintf(stderr,"[quantapdf.native_scoped_operations_oracle] complete\n");
    return EXIT_SUCCESS;
}
