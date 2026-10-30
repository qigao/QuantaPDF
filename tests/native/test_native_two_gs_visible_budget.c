/* Two owned Clip leaves, their real intersection root, and two opaque
 * GS-scoped PATH Fill operations on a 600x600 C11 canvas.
 * This is a PURE C11 opt-in work-budget acceptance; no QPDF/PDFium,
 * second raster, unbounded allocations or public SDK ABI changes.
 * Existing two-GS V5 callers retain whole-page refusal behavior.
 */
#include "ir/ir.h"
#include "render/ir_raster_scoped_mixed_v5.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"Native two-GS work:%d: %s\n",__LINE__,#x); \
    return EXIT_FAILURE; } } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

enum { W=600, H=600, EDGE_LEASE=8000000 };
/* Red Fill+Clip leaf1 (4+4 edges), 16 subpixels, 40x40 bbox.
 * Second Fill+leaf1+leaf2 (4+4+6 edges), bbox 26x28. Each operation
 * receives <= floor(total/2); the sums never exceed total. */
static const size_t first_cost=(4u+4u)*16u*40u*40u;
static const size_t second_cost=(4u+4u+6u)*16u*26u*28u;
static const size_t acceptance_cap=2u*first_cost;

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[2];
    quantapdf_composer_graphics_state states[2];
    quantapdf_composer_clip_state clips[3];
    quantapdf_composer_clip_id members[2];
    quantapdf_composer_path_command
        red[5],blue[5],rectangle[5],triangles[8];
    quantapdf_composer_image_state hidden_image;
} fixture;

static void point(quantapdf_composer_path_command *p,
    quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;p->point1.x=x;p->point1.y=y;
}
static void rect(quantapdf_composer_path_command p[5],
    float x0,float y0,float x1,float y1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,y1);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
}
static void triangles(quantapdf_composer_path_command p[8])
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,22.0f,12.0f);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,48.0f,12.0f);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,35.0f,40.0f);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_MOVE_TO,32.0f,20.0f);
    point(&p[5],QUANTAPDF_COMPOSER_PATH_LINE_TO,38.0f,20.0f);
    point(&p[6],QUANTAPDF_COMPOSER_PATH_LINE_TO,35.0f,30.0f);
    point(&p[7],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
}
static void setup(fixture *f)
{
    memset(f,0,sizeof(*f));
    f->page.width_points=(float)W;
    f->page.height_points=(float)H;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;f->ir.page_count=1u;
    f->ir.operations=f->ops;f->ir.operation_count=2u;
    f->ir.resources.graphics_states=f->states;
    f->ir.resources.graphics_state_count=2u;
    f->ir.resources.graphics_state_capacity=2u;
    f->ir.resources.clips=f->clips;
    f->ir.resources.clip_count=3u;
    f->ir.resources.clip_capacity=3u;
    rect(f->red,10.0f,10.0f,50.0f,50.0f);
    rect(f->blue,10.0f,10.0f,50.0f,50.0f);
    rect(f->rectangle,10.0f,10.0f,50.0f,50.0f);
    triangles(f->triangles);
    f->clips[0].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clips[0].commands=f->rectangle;
    f->clips[0].command_count=5u;
    f->clips[0].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clips[0].transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    f->clips[1].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clips[1].commands=f->triangles;
    f->clips[1].command_count=8u;
    f->clips[1].fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    f->clips[1].transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    f->members[0]=1u;f->members[1]=2u;
    f->clips[2].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[2].members=f->members;
    f->clips[2].member_count=2u;

    f->states[0].fill_alpha=1.0f;
    f->states[0].stroke_alpha=1.0f;
    f->states[0].blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->states[0].clip_id=1u;
    f->states[1]=f->states[0];
    f->states[1].clip_id=3u;
    for(size_t i=0u;i<2u;++i) {
        f->ops[i].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
        f->ops[i].page_index=0u;
        f->ops[i].graphics_state_id=(quantapdf_composer_graphics_state_id)(i+1u);
        f->ops[i].value.path.commands=i==0u?f->red:f->blue;
        f->ops[i].value.path.command_count=5u;
        f->ops[i].value.path.options.struct_size=
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        f->ops[i].value.path.options.fill=1;
        f->ops[i].value.path.options.fill_argb=
            i==0u?UINT32_C(0xffff0000):UINT32_C(0xff0000ff);
        f->ops[i].value.path.options.fill_rule=
            QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->ops[i].value.path.options.graphics_state_id=
            (quantapdf_composer_graphics_state_id)(i+1u);
    }
}
static const quantapdf_ir_raster_scoped_mixed_v5_limits budgets={
    400000u,2u,80u,8000000u,1u,1u
};
static quantapdf_status render(fixture *f,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limit,
    quantapdf_ir_raster_image *out)
{
    return quantapdf_ir_raster_scoped_mixed_v5_two_gs_visible_work_device_viewport(
        &f->ir,0u,limit,(double)W,(double)H,out);
}
static uint32_t rgba(const quantapdf_ir_raster_image *img,
                     size_t x,size_t y)
{
    const unsigned char *p=img->pixels+y*img->stride+4u*x;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|(uint32_t)p[3];
}
static int expect_unsupported(fixture *f,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limit)
{
    quantapdf_ir_raster_image out;
    memset(&out,0xa5,sizeof(out));
    CHECK(render(f,limit,&out)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(out.pixels==NULL && out.width==0u && out.height==0u &&
          out.stride==0u && out.size==0u);
    return EXIT_SUCCESS;
}
static int acceptance(void)
{
    fixture f;
    quantapdf_ir_raster_image a={0},b={0},changed={0};
    quantapdf_ir_raster_scoped_mixed_v5_limits limit=budgets;
    setup(&f);
    CHECK(first_cost==204800u && second_cost==163072u &&
          second_cost<first_cost && acceptance_cap==409600u);
    OK(render(&f,&limit,&a));
    CHECK(a.pixels!=NULL && a.width==(size_t)W &&
          a.height==(size_t)H && a.stride==W*4u &&
          a.size==(size_t)W*(size_t)H*4u);
    CHECK(rgba(&a,5u,5u)==UINT32_C(0xffffffff));
    CHECK(rgba(&a,15u,15u)==UINT32_C(0xff0000ff)); /* first GS red */
    CHECK(rgba(&a,35u,16u)==UINT32_C(0x0000ffff)); /* intersection blue */
    CHECK(rgba(&a,35u,24u)==UINT32_C(0xff0000ff)); /* W* hole: retain red */
    CHECK(rgba(&a,599u,599u)==UINT32_C(0xffffffff));
    OK(render(&f,&limit,&b));
    CHECK(b.pixels!=NULL && b.size==a.size &&
          memcmp(a.pixels,b.pixels,a.size)==0);
    quantapdf_ir_raster_image_dispose(&b);

    /* Nonzero same-winding inner contour does NOT cut a hole.
     * The first GS still paints red outside leaf2. */
    f.clips[1].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    OK(render(&f,&limit,&changed));
    CHECK(rgba(&changed,15u,15u)==UINT32_C(0xff0000ff) &&
          rgba(&changed,35u,16u)==UINT32_C(0x0000ffff) &&
          rgba(&changed,35u,24u)==UINT32_C(0x0000ffff));
    quantapdf_ir_raster_image_dispose(&changed);
    f.clips[1].fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    /* A detached output must not borrow the original IR/Gs/Clip data. */
    f.states[1].clip_id=1u;
    f.triangles[4].point1.x=0.0f;
    CHECK(rgba(&a,35u,24u)==UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&a);

    setup(&f);
    limit.max_sample_tests=acceptance_cap;
    OK(render(&f,&limit,&a));
    CHECK(rgba(&a,35u,24u)==UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&a);
    limit.max_sample_tests=acceptance_cap-1u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    limit=budgets;

    /* The existing strict PRIVATE two-GS entry must still refuse the
     * very same 600x600 source at the original 8m full-page lease. */
    memset(&a,0xa5,sizeof(a));
    CHECK(quantapdf_ir_raster_scoped_mixed_v5_compound_clip_states_device_viewport(
        &f.ir,0u,&limit,(double)W,(double)H,&a)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(a.pixels==NULL && a.size==0u && a.width==0u);
    return EXIT_SUCCESS;
}
static int negative_cases(void)
{
    fixture f;
    quantapdf_ir_raster_scoped_mixed_v5_limits limit=budgets;
    setup(&f);f.ops[0].graphics_state_id=2u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ops[1].graphics_state_id=1u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ops[0].value.path.options.graphics_state_id=2u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.states[1].clip_id=1u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.states[0].fill_alpha=0.5f;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[2].members[1]=1u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[2].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[1].command_count=10u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[0].transform.a=2.0f;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ops[0].value.path.options.fill=0;
    f.ops[0].value.path.options.stroke=1;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ir.resources.images=&f.hidden_image;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ir.resources.clip_count=4u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ir.resources.graphics_state_count=1u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ir.operation_count=3u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    /* Source and geometry caps remain real despite the clipped-area
     * proof, not merely positive-pixel assertions. */
    setup(&f);limit.max_pixels=359999u;
    CHECK(expect_unsupported(&f,&limit)==EXIT_SUCCESS);
    printf("Native two-GS 600x600 W/W* visible work: exact RGB and "
           "two-layer budget; 16 strict atomic negative profiles\n");
    return EXIT_SUCCESS;
}
int main(void)
{
    CHECK(acceptance()==EXIT_SUCCESS);
    CHECK(negative_cases()==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
