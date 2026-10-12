/* Test-only QPDF/PDFium reference for private scoped native Clip DAG V1.
 * Native raster and its separate strict C11 conformance remain provider-free. */
#include "backend/qpdf_composer.h"
#include "render/ir_scoped_operations_v1.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(e) do { if (!(e)) { \
    std::fprintf(stderr, "%s:%d: scoped DAG oracle failed: %s\n", \
                 __FILE__, __LINE__, #e); std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

namespace {
constexpr size_t SIDE = 64u;
const quantapdf_ir_raster_v1_limits LIMITS = {
    SIDE * SIDE, 3u, 768u, 32000000u
};
struct sample { size_t x, y; unsigned red, green, blue; };

struct scene {
    quantapdf_ir ir{};
    quantapdf_composer_page_state page{};
    quantapdf_composer_operation operations[3]{};
    quantapdf_composer_graphics_state states[2]{};
    quantapdf_composer_clip_state clips[5]{};
    quantapdf_composer_path_command paints[3][5]{};
    quantapdf_composer_path_command paths[3][5]{};
    quantapdf_composer_clip_id inner[2]{1u,2u};
    quantapdf_composer_clip_id outer[2]{3u,4u};

    static void rectangle(quantapdf_composer_path_command *p,
                          float left, float top, float right, float bottom)
    {
        const quantapdf_composer_path_command_kind kinds[5] = {
            QUANTAPDF_COMPOSER_PATH_MOVE_TO, QUANTAPDF_COMPOSER_PATH_LINE_TO,
            QUANTAPDF_COMPOSER_PATH_LINE_TO, QUANTAPDF_COMPOSER_PATH_LINE_TO,
            QUANTAPDF_COMPOSER_PATH_CLOSE
        };
        const quantapdf_point vertices[5] = {
            {left,top}, {right,top}, {right,bottom}, {left,bottom}, {0,0}
        };
        for (size_t i = 0u; i < 5u; ++i) {
            p[i] = quantapdf_composer_path_command{};
            p[i].kind = kinds[i];
            p[i].point1 = vertices[i];
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
        ir.resources.clip_count = 5u;
        ir.resources.clip_capacity = 5u;
        for (size_t i = 0u; i < 3u; ++i) {
            auto& op = operations[i];
            op.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
            op.value.path.commands = paints[i];
            op.value.path.command_count = 5u;
            auto& opt = op.value.path.options;
            opt.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
            opt.fill = 1;
            opt.fill_argb = colors[i];
            opt.transform = quantapdf_affine_transform{1,0,0,1,0,0};
        }
        rectangle(paints[0],0,0,64,64);
        rectangle(paints[1],32,0,64,64);
        rectangle(paints[2],0,0,12,12);
        for (size_t i = 0u; i < 2u; ++i) {
            operations[i].graphics_state_id =
                (quantapdf_composer_graphics_state_id)(i+1u);
            operations[i].value.path.options.graphics_state_id =
                (quantapdf_composer_graphics_state_id)(i+1u);
            states[i].fill_alpha = states[i].stroke_alpha = 1.0f;
            states[i].blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
        }
        states[0].clip_id = 5u; /* nested root */
        states[1].clip_id = 1u; /* shared leaf from same graph */
        for (size_t i = 0u; i < 3u; ++i) {
            auto& node = clips[i == 2u ? 3u : i];
            node.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
            node.commands = paths[i];
            node.command_count = 5u;
            node.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
            node.transform = quantapdf_affine_transform{1,0,0,1,0,0};
        }
        rectangle(paths[0],8,8,56,56);
        rectangle(paths[1],16,16,48,48);
        rectangle(paths[2],24,24,40,40);
        clips[2].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        clips[2].members = inner;
        clips[2].member_count = 2u;
        clips[4].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        clips[4].members = outer;
        clips[4].member_count = 2u;
    }
};

struct image {
    int width=0, height=0, stride=0, components=0;
    std::vector<unsigned char> pixels;
};

image pdfium_render(const char *name)
{
    image result;
    quantapdf_document *doc = nullptr;
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *bitmap = nullptr;
    const unsigned char *data = nullptr;
    size_t bytes = 0u;
    OK(quantapdf_open(name,nullptr,&doc));
    OK(quantapdf_load_page(doc,0,&page));
    OK(quantapdf_render_page(page,&bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap,&result.width,&result.height,
                                   &result.stride,&result.components));
    OK(quantapdf_bitmap_data(bitmap,&data,&bytes));
    CHECK(result.width==(int)SIDE && result.height==(int)SIDE &&
          result.components==3 && result.stride>=(int)(SIDE*3u));
    CHECK(bytes>=SIDE*(size_t)result.stride);
    result.pixels.assign(data,data+SIDE*(size_t)result.stride);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return result;
}

void compare(scene& s, const char *filename,
             const sample *goldens, size_t sample_count)
{
    quantapdf_ir_raster_image native{};
    quantapdf_ir_raster_image again{};
    unsigned char *pdf=nullptr;
    size_t pdf_size=0u, gross=0u;
    double error=0.0;
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &s.ir,0u,&LIMITS,&native));
    CHECK(native.pixels!=nullptr && native.width==SIDE &&
          native.height==SIDE && native.stride==SIDE*4u);
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &s.ir,0u,&LIMITS,&again));
    CHECK(native.size==again.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&again);

    OK(quantapdf_qpdf_compose(&s.ir,&pdf,&pdf_size));
    CHECK(pdf!=nullptr && pdf_size>0u);
    FILE *file=std::fopen(filename,"wb");
    CHECK(file!=nullptr);
    CHECK(std::fwrite(pdf,1,pdf_size,file)==pdf_size);
    CHECK(std::fclose(file)==0);
    std::free(pdf);
    image reference=pdfium_render(filename);
    CHECK(std::remove(filename)==0);
    for(size_t y=0u;y<SIDE;++y)
        for(size_t x=0u;x<SIDE;++x) {
            const unsigned char *a =
                native.pixels+y*native.stride+x*4u;
            const unsigned char *b =
                reference.pixels.data()+y*(size_t)reference.stride+x*3u;
            int worst=0;
            CHECK(a[3]==255u);
            for(size_t c=0u;c<3u;++c) {
                const int delta=std::abs((int)a[c]-(int)b[c]);
                if(delta>worst)worst=delta;
                error+=(double)delta;
            }
            if(worst>64)++gross;
        }
    const double mean=error/(double)(SIDE*SIDE*3u);
    if(mean>10.0 || gross>SIDE*SIDE/12u) {
        std::fprintf(stderr,
            "%s nested-scoped mismatch mean=%.3f gross=%zu\n",
            filename,mean,gross);
        CHECK(false);
    }
    for(size_t i=0u;i<sample_count;++i) {
        const auto& p=goldens[i];
        CHECK(p.x<SIDE && p.y<SIDE);
        const unsigned char *a=native.pixels+p.y*native.stride+p.x*4u;
        const unsigned char *b=reference.pixels.data()+
            p.y*(size_t)reference.stride+p.x*3u;
        const unsigned want[3]={p.red,p.green,p.blue};
        for(size_t c=0u;c<3u;++c) {
            CHECK(std::abs((int)a[c]-(int)want[c])<=3);
            CHECK(std::abs((int)b[c]-(int)want[c])<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native);
}

void scoped()
{
    scene s;
    const sample expected[]={
        {4,4,0,255,0}, {20,20,255,255,255},
        {28,28,255,0,0}, {36,36,0,0,255},
        {52,28,0,0,255}, {56,28,255,255,255},
        {15,15,255,255,255}, {44,44,0,0,255}
    };
    compare(s,ORACLE_SCOPED_PDF,expected,sizeof(expected)/sizeof(expected[0]));
}

void reversed()
{
    scene s;
    quantapdf_composer_operation temp=s.operations[0];
    s.operations[0]=s.operations[1];
    s.operations[1]=temp;
    const sample expected[]={
        {4,4,0,255,0}, {28,28,255,0,0},
        {36,36,255,0,0}, {52,28,0,0,255}
    };
    compare(s,ORACLE_REVERSE_PDF,expected,sizeof(expected)/sizeof(expected[0]));
}

void fraction()
{
    scene s;
    s.clips[3].transform.e=0.5f;
    const sample expected[]={
        {4,4,0,255,0}, {28,28,255,0,0},
        {36,36,0,0,255}, {52,28,0,0,255}
    };
    compare(s,ORACLE_FRACTION_PDF,expected,sizeof(expected)/sizeof(expected[0]));
}

void invalid()
{
    scene s;
    unsigned char *pdf=(unsigned char *)(uintptr_t)1u;
    size_t size=SIZE_MAX;
    quantapdf_ir_raster_image image{};
    s.outer[0]=5u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(
        &s.ir,0u,&LIMITS,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels==nullptr && image.size==0u);
    CHECK(quantapdf_qpdf_compose(&s.ir,&pdf,&size)==QUANTAPDF_ERROR_BACKEND);
    CHECK(pdf==nullptr && size==0u);
}
}

int main()
{
    scoped();
    reversed();
    fraction();
    invalid();
    std::fprintf(stderr,"[quantapdf.native_scoped_dag_oracle] complete\n");
    return EXIT_SUCCESS;
}
