/* Differential conformance for a bounded nested Clip DAG. This test alone
 * uses QPDF Composer + PDFium; the native Clip DAG and Raster remain C11-only. */
#include "backend/qpdf_composer.h"
#include "render/ir_clip_dag_v3.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(e) do { if (!(e)) { \
    std::fprintf(stderr, "%s:%d: nested Clip oracle: %s\n", \
                 __FILE__, __LINE__, #e); std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

namespace {
constexpr size_t SIDE = 64;
const quantapdf_ir_raster_v1_limits limits = {
    SIDE * SIDE, 1u, 512u, 32000000u
};

struct pixel {
    size_t x, y;
    unsigned red, green, blue;
};

struct scene {
    quantapdf_ir ir{};
    quantapdf_composer_page_state page{};
    quantapdf_composer_operation operation{};
    quantapdf_composer_graphics_state state{};
    quantapdf_composer_clip_state clips[5]{};
    quantapdf_composer_path_command paint[5]{};
    quantapdf_composer_path_command paths[3][12]{};
    quantapdf_composer_clip_id inner[2]{1u,2u};
    quantapdf_composer_clip_id outer[2]{3u,4u};

    static void cmd(quantapdf_composer_path_command *p,
                    quantapdf_composer_path_command_kind kind,
                    float x, float y)
    {
        *p = quantapdf_composer_path_command{};
        p->kind = kind;
        p->point1 = quantapdf_point{x,y};
    }

    static void rectangle(quantapdf_composer_path_command *p,
                          float x0, float y0, float x1, float y1)
    {
        cmd(p,   QUANTAPDF_COMPOSER_PATH_MOVE_TO, x0,y0);
        cmd(p+1, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1,y0);
        cmd(p+2, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1,y1);
        cmd(p+3, QUANTAPDF_COMPOSER_PATH_LINE_TO, x0,y1);
        cmd(p+4, QUANTAPDF_COMPOSER_PATH_CLOSE, 0,0);
    }

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
        ir.resources.clips = clips;
        ir.resources.clip_count = 5u;
        ir.resources.clip_capacity = 5u;

        operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        operation.graphics_state_id = 1u;
        operation.value.path.commands = paint;
        operation.value.path.command_count = 5u;
        auto& opt = operation.value.path.options;
        opt.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        opt.fill = 1;
        opt.fill_argb = UINT32_C(0xffff0000);
        opt.graphics_state_id = 1u;
        opt.transform = quantapdf_affine_transform{1,0,0,1,0,0};
        rectangle(paint,0,0,64,64);
        state.clip_id = 5u;
        state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
        state.fill_alpha = state.stroke_alpha = 1.0f;
        for(size_t i=0u;i<3u;++i) {
            auto& clip = clips[i==2u ? 3u:i];
            clip.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
            clip.commands = paths[i];
            clip.command_count = 5u;
            clip.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
            clip.transform = quantapdf_affine_transform{1,0,0,1,0,0};
        }
        rectangle(paths[0],4,4,60,60);
        rectangle(paths[1],12,12,52,52);
        rectangle(paths[2],20,20,44,44);
        clips[2].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        clips[2].members = inner;
        clips[2].member_count = 2u;
        clips[4].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        clips[4].members = outer;
        clips[4].member_count = 2u;
    }
};

struct raster {
    int width=0,height=0,stride=0,components=0;
    std::vector<unsigned char> data;
};
raster render_pdfium(const char *filename)
{
    raster r;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    const unsigned char *borrowed=nullptr;
    size_t bytes=0u;
    OK(quantapdf_open(filename,nullptr,&doc));
    OK(quantapdf_load_page(doc,0,&page));
    OK(quantapdf_render_page(page,&bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap,&r.width,&r.height,
                                   &r.stride,&r.components));
    OK(quantapdf_bitmap_data(bitmap,&borrowed,&bytes));
    CHECK(r.width==(int)SIDE && r.height==(int)SIDE &&
          r.components==3 && r.stride>=(int)(SIDE*3u));
    CHECK(bytes>=SIDE*(size_t)r.stride);
    r.data.assign(borrowed,borrowed+SIDE*(size_t)r.stride);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return r;
}

void compare(scene& s, const char *file, const pixel *goldens, size_t n)
{
    quantapdf_ir_raster_image native{};
    quantapdf_ir_raster_image again{};
    unsigned char *pdf=nullptr;
    size_t pdf_size=0u, large=0u;
    double error=0;
    OK(quantapdf_ir_raster_clip_dag_v3(&s.ir,0u,&limits,&native));
    CHECK(native.pixels!=nullptr && native.width==SIDE &&
          native.height==SIDE && native.stride==SIDE*4u);
    OK(quantapdf_ir_raster_clip_dag_v3(&s.ir,0u,&limits,&again));
    CHECK(native.size==again.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&again);

    OK(quantapdf_qpdf_compose(&s.ir,&pdf,&pdf_size));
    CHECK(pdf!=nullptr && pdf_size>0);
    FILE *fp=std::fopen(file,"wb");
    CHECK(fp!=nullptr);
    CHECK(std::fwrite(pdf,1,pdf_size,fp)==pdf_size);
    CHECK(std::fclose(fp)==0);
    std::free(pdf);
    raster reference=render_pdfium(file);
    CHECK(std::remove(file)==0);

    for(size_t y=0;y<SIDE;++y) for(size_t x=0;x<SIDE;++x) {
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=reference.data.data()+
            y*(size_t)reference.stride+x*3u;
        int worst=0;
        CHECK(a[3]==255u);
        for(size_t c=0;c<3u;++c) {
            int delta=std::abs((int)a[c]-(int)b[c]);
            if(delta>worst)worst=delta;
            error+=(double)delta;
        }
        if(worst>64)++large;
    }
    double mean=error/(double)(SIDE*SIDE*3u);
    if(mean>10.0 || large>SIDE*SIDE/12u) {
        std::fprintf(stderr,"%s nested Clip diff: mean=%.3f gross=%zu\n",
                     file,mean,large);
        CHECK(false);
    }
    for(size_t i=0u;i<n;++i) {
        const pixel& g=goldens[i];
        CHECK(g.x<SIDE && g.y<SIDE);
        const unsigned char *a=
            native.pixels+g.y*native.stride+g.x*4u;
        const unsigned char *b=
            reference.data.data()+g.y*(size_t)reference.stride+g.x*3u;
        const unsigned expected[3]={g.red,g.green,g.blue};
        for(size_t c=0;c<3u;++c) {
            CHECK(std::abs((int)a[c]-(int)expected[c])<=3);
            CHECK(std::abs((int)b[c]-(int)expected[c])<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native);
}

void nested()
{
    scene s;
    const pixel goldens[]={
        {8,8,255,255,255}, {16,16,255,255,255},
        {24,24,255,0,0}, {40,40,255,0,0},
        {48,32,255,255,255}
    };
    compare(s,ORACLE_NESTED_PDF,goldens,5u);
}
void fractional()
{
    scene s;
    s.clips[3].transform.e=0.5f;
    const pixel goldens[]={
        {16,16,255,255,255}, {30,30,255,0,0},
        {45,30,255,255,255}
    };
    compare(s,ORACLE_FRACTIONAL_PDF,goldens,3u);
}
void shared()
{
    scene s;
    s.ir.resources.clip_count=4u;
    s.state.clip_id=4u;
    s.clips[3]=s.clips[4];
    s.clips[3].members=s.outer;
    s.outer[0]=3u;
    s.outer[1]=2u; /* leaf 2 is also reachable through node 3 */
    const pixel goldens[]={
        {16,16,255,0,0}, {48,48,255,0,0},
        {8,8,255,255,255}, {58,58,255,255,255}
    };
    compare(s,ORACLE_SHARED_PDF,goldens,4u);
}
void evenodd_hole()
{
    scene s;
    scene::rectangle(s.paths[2],18,18,46,46);
    scene::rectangle(s.paths[2]+5,28,28,36,36);
    s.clips[3].command_count=10u;
    s.clips[3].fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    const pixel goldens[]={
        {22,22,255,0,0}, {32,32,255,255,255},
        {48,48,255,255,255}
    };
    compare(s,ORACLE_HOLE_PDF,goldens,3u);
}
void cubic()
{
    scene s;
    scene::cmd(s.paths[2],QUANTAPDF_COMPOSER_PATH_MOVE_TO,20,44);
    auto& curve=s.paths[2][1];
    curve.kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    curve.point1=quantapdf_point{20,12};
    curve.point2=quantapdf_point{44,12};
    curve.point3=quantapdf_point{44,44};
    scene::cmd(s.paths[2]+2,QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
    s.clips[3].command_count=3u;
    const pixel goldens[]={
        {32,32,255,0,0}, {32,16,255,255,255},
        {16,40,255,255,255}
    };
    compare(s,ORACLE_CUBIC_PDF,goldens,3u);
}
}

int main()
{
    nested();
    fractional();
    shared();
    evenodd_hole();
    cubic();
    std::fprintf(stderr,"[quantapdf.native_nested_clip_oracle] complete\n");
    return EXIT_SUCCESS;
}
