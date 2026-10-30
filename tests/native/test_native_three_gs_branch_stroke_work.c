/* Original-shaped C11 IR proof: 1px open butt Stroke in first W*
 * branch, followed by two independent bounded opaque Fills. Canonical
 * V1 4x4 sampler only; this is NOT public original-PDF admission. */
#include "ir/ir.h"
#include "render/ir_raster_scoped_mixed_v5.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"three-GS-Stroke:%d: %s\n",__LINE__,#x); return EXIT_FAILURE; } } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

enum { W=600, H=600 };
static const size_t first_cost=153920u;
static const size_t second_cost=69120u;
static const size_t third_upper_cost=57600u;
static const size_t acceptance_cap=461760u;

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation operations[3];
    quantapdf_composer_graphics_state states[3];
    quantapdf_composer_clip_state clips[7];
    quantapdf_composer_clip_id members[3][2];
    quantapdf_composer_path_command
        red[2],blue[5],green[5],rect_clip[5],
        rectangles[10],third_clip[5],fourth_clip[5];
    quantapdf_composer_image_state hidden_image;
} fixture;

static void point(quantapdf_composer_path_command *p,
    quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;
    p->point1.x=x;
    p->point1.y=y;
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
/* 600x600 original-yWorks-like W*: two concentric closed
 * rectangles. The inner same-winding contour is an even-odd hole.
 * Canonicalized ten-command IR keeps full eight edges, no mask shortcut. */
static void two_rectangles(quantapdf_composer_path_command p[10])
{
    rect(p,30.0f,10.0f,400.0f,80.0f);
    rect(p+5,50.0f,20.0f,150.0f,70.0f);
}
static void setup(fixture *f)
{
    const uint32_t colors[3]={
        UINT32_C(0xffff0000),UINT32_C(0xff0000ff),UINT32_C(0xff00ff00)
    };
    size_t i;
    memset(f,0,sizeof(*f));
    f->page.width_points=(float)W;
    f->page.height_points=(float)H;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;
    f->ir.page_count=1u;
    f->ir.operations=f->operations;
    f->ir.operation_count=3u;
    f->ir.resources.graphics_states=f->states;
    f->ir.resources.graphics_state_count=3u;
    f->ir.resources.graphics_state_capacity=3u;
    f->ir.resources.clips=f->clips;
    f->ir.resources.clip_count=7u;
    f->ir.resources.clip_capacity=7u;
    point(&f->red[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,30.0f,40.0f);
    point(&f->red[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,400.0f,40.0f);
    rect(f->blue,10.0f,10.0f,50.0f,50.0f);
    rect(f->green,10.0f,10.0f,50.0f,50.0f);
    rect(f->rect_clip,0.0f,0.0f,600.0f,600.0f);
    two_rectangles(f->rectangles);
    rect(f->third_clip,8.0f,10.0f,22.0f,40.0f);
    rect(f->fourth_clip,40.0f,10.0f,50.0f,40.0f);
    for(i=0u;i<4u;++i) {
        quantapdf_composer_clip_state *leaf=&f->clips[i];
        leaf->kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        leaf->commands=i==0u?f->rect_clip:i==1u?f->rectangles:
            i==2u?f->third_clip:f->fourth_clip;
        leaf->command_count=i==1u?10u:5u;
        leaf->fill_rule=i==1u?QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
            QUANTAPDF_COMPOSER_FILL_NONZERO;
        leaf->transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    }
    f->members[0][0]=1u;
    f->members[0][1]=2u;
    f->members[1][0]=1u;
    f->members[1][1]=3u;
    f->members[2][0]=1u;
    f->members[2][1]=4u;
    for(i=0u;i<3u;++i) {
        f->clips[i+4u].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        f->clips[i+4u].members=f->members[i];
        f->clips[i+4u].member_count=2u;
        f->states[i].fill_alpha=1.0f;
        f->states[i].stroke_alpha=1.0f;
        f->states[i].blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        f->states[i].clip_id=(quantapdf_composer_clip_id)
            (i==0u?5u:i==1u?6u:7u);
        f->operations[i].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
        f->operations[i].page_index=0u;
        f->operations[i].graphics_state_id=
            (quantapdf_composer_graphics_state_id)(i+1u);
        f->operations[i].value.path.commands=
            i==0u?f->red:i==1u?f->blue:f->green;
        f->operations[i].value.path.command_count=i==0u?2u:5u;
        f->operations[i].value.path.options.struct_size=
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        f->operations[i].value.path.options.fill=i==0u?0:1;
        f->operations[i].value.path.options.stroke=i==0u?1:0;
        f->operations[i].value.path.options.fill_argb=colors[i];
        f->operations[i].value.path.options.stroke_argb=colors[i];
        if(i==0u) {
            f->operations[i].value.path.options.stroke_width=1.0f;
            f->operations[i].value.path.options.line_cap=
                QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
            f->operations[i].value.path.options.line_join=
                QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
            f->operations[i].value.path.options.miter_limit=10.0f;
        }
        f->operations[i].value.path.options.fill_rule=
            QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->operations[i].value.path.options.graphics_state_id=
            (quantapdf_composer_graphics_state_id)(i+1u);
    }
}
static const quantapdf_ir_raster_scoped_mixed_v5_limits budgets={
    400000u,3u,96u,8000000u,1u,1u
};
static quantapdf_status render(fixture *f,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out)
{
    return quantapdf_ir_raster_scoped_mixed_v5_three_gs_branch_stroke_visible_work_device_viewport(
        &f->ir,0u,limits,(double)W,(double)H,out);
}
static uint32_t rgba(const quantapdf_ir_raster_image *img,
                     size_t x,size_t y)
{
    const unsigned char *p=img->pixels+y*img->stride+4u*x;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|(uint32_t)p[3];
}
static int rejects_atomically(fixture *f,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits)
{
    quantapdf_ir_raster_image out;
    memset(&out,0xa5,sizeof(out));
    CHECK(render(f,limits,&out)==QUANTAPDF_ERROR_UNSUPPORTED);
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
    CHECK(first_cost==153920u && second_cost==69120u &&
          third_upper_cost==57600u &&
          acceptance_cap==3u*first_cost);
    OK(render(&f,&limit,&a));
    CHECK(a.pixels!=NULL && a.width==(size_t)W &&
          a.height==(size_t)H && a.stride==W*4u &&
          a.size==(size_t)W*(size_t)H*4u);
    CHECK(rgba(&a,5u,5u)==UINT32_C(0xffffffff));
    CHECK(rgba(&a,15u,15u)==UINT32_C(0x0000ffff)); /* GS2 blue */
    CHECK(rgba(&a,44u,16u)==UINT32_C(0x00ff00ff)); /* GS3 green */
    CHECK(rgba(&a,40u,40u)==UINT32_C(0xff7f7fff)); /* GS1 half-width */
    CHECK(rgba(&a,160u,40u)==UINT32_C(0xff7f7fff)); /* beyond W* hole */
    CHECK(rgba(&a,100u,40u)==UINT32_C(0xffffffff)); /* W* hole empty */
    CHECK(rgba(&a,20u,40u)==UINT32_C(0xffffffff)); /* outside W* */
    CHECK(rgba(&a,599u,599u)==UINT32_C(0xffffffff));
    OK(render(&f,&limit,&b));
    CHECK(b.size==a.size && memcmp(a.pixels,b.pixels,a.size)==0);
    quantapdf_ir_raster_image_dispose(&b);
    f.clips[1].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    OK(render(&f,&limit,&changed));
    CHECK(rgba(&changed,100u,40u)==UINT32_C(0xff7f7fff));
    quantapdf_ir_raster_image_dispose(&changed);
    f.clips[1].fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    f.states[2].clip_id=6u;
    f.rectangles[5].point1.x=0.0f;
    CHECK(rgba(&a,100u,40u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&a);

    setup(&f);
    limit.max_sample_tests=acceptance_cap;
    OK(render(&f,&limit,&a));
    CHECK(rgba(&a,44u,16u)==UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&a);
    limit.max_sample_tests=acceptance_cap-1u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    limit=budgets;

    /* All historical two-GS entrypoints remain strictly closed. */
    memset(&a,0xa5,sizeof(a));
    CHECK(quantapdf_ir_raster_scoped_mixed_v5_three_gs_branch_visible_work_device_viewport(
        &f.ir,0u,&limit,(double)W,(double)H,&a)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(a.pixels==NULL && a.size==0u && a.width==0u);
    return EXIT_SUCCESS;
}
static int negative_cases(void)
{
    fixture f;
    quantapdf_ir_raster_scoped_mixed_v5_limits limit=budgets;
    setup(&f);f.operations[0].graphics_state_id=2u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[2].graphics_state_id=2u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[2].value.path.options.graphics_state_id=1u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.states[2].clip_id=6u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.states[1].fill_alpha=0.5f;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[4].members[1]=1u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[5].members[0]=2u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[6].members[0]=7u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[6].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[1].command_count=11u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[3].transform.a=2.0f;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[2].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[2].value.path.options.stroke=1;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[0].value.path.options.stroke_width=2.0f;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[0].value.path.options.line_cap=
        QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[0].value.path.options.miter_limit=4.0f;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[0].value.path.options.stroke_argb=
        UINT32_C(0x80ff0000);
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[0].value.path.dash_count=1u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.red[1].point1.y=41.0f; /* no diagonal stroke */
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[0].value.path.options.transform.a=2.0f;
    f.operations[0].value.path.options.transform.d=1.0f;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.operations[0].value.path.options.fill=1;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[1].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.clips[1].commands[4].kind=
        QUANTAPDF_COMPOSER_PATH_LINE_TO;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ir.resources.images=&f.hidden_image;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ir.resources.clip_capacity=8u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ir.resources.graphics_state_count=2u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);f.ir.operation_count=4u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);limit.max_pixels=359999u;
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);limit=budgets;limit.max_edges=38u; /* floor(38/3)=12 < 1+4+8 edges */
    CHECK(rejects_atomically(&f,&limit)==EXIT_SUCCESS);
    setup(&f);limit=budgets;
    {
        quantapdf_ir_raster_image out;
        memset(&out,0xa5,sizeof(out));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_three_gs_branch_visible_work_device_viewport(
            &f.ir,0u,&limit,599.5,(double)H,&out)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(out.pixels==NULL && out.size==0u && out.width==0u);
    }
    printf("Native 3GS open Stroke+two Fill, W/W* hole, 1px half-width, quotas\n");
    return EXIT_SUCCESS;
}
int main(void)
{
    CHECK(acceptance()==EXIT_SUCCESS);
    CHECK(negative_cases()==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
