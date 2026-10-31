/* Compound Clip contours stay in one V1 winding calculation and one
 * unchanged nine-command leaf. Goldens use independent authored PATHs;
 * old one-GS acceptance and old two-GS refusal are separately qualified. */
#include "render/native_device_ir_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"compound Clip device:%d: %s\n",__LINE__,#x); \
    return EXIT_FAILURE; } } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[4];
    quantapdf_composer_image_state images[2];
    quantapdf_composer_graphics_state gs[2];
    quantapdf_composer_clip_state clips[3];
    quantapdf_composer_clip_id members[2];
    quantapdf_composer_path_command leaves[2][10],paths[4][10];
    unsigned char pixels[2][12];
} fixture;

typedef struct geometry {
    unsigned dpi;
    float v[17],width,height,fraction_width,fraction_height;
    double viewport_w,viewport_h,fraction_w,fraction_h;
    size_t hole_x,hole_y,ring_x,ring_y,right_x;
} geometry;

/* Device points and physical viewports are authored independently of
 * qp_device_commands, including the 96-DPI rational coordinate grid. */
static const geometry grids[]={
    {72u,{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},
        16,12,16,12,16,12,15.25,11.25,8u,5u,3u,6u,12u},
    {96u,{0,4.0f/3,8.0f/3,4,16.0f/3,20.0f/3,8,28.0f/3,
        32.0f/3,12,40.0f/3,44.0f/3,16,52.0f/3,56.0f/3,20,64.0f/3},
        22,16,21,15,64.0/3,16,61.0/3,15,11u,7u,4u,8u,16u},
    {144u,{0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32},
        32,24,31,23,32,24,30.5,22.5,17u,11u,6u,12u,24u}
};
typedef struct profile {const char *kinds,*states;} profile;
static const profile profiles[]={
    {"PPPP","1210"},{"II","12"},{"II","21"},
    {"IIPP","1210"},{"PIPI","1210"}
};
static const quantapdf_ir_raster_scoped_mixed_v5_limits budget={
    4096u,4u,512u,8000000u,8u,8192u
};
static const uint32_t colors[]={UINT32_C(0xffff0000),UINT32_C(0xff0000ff),
    UINT32_C(0xff00ff00),UINT32_C(0xffffff00)};

static void point(quantapdf_composer_path_command *p,
    quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));p->kind=kind;p->point1=(quantapdf_point){x,y};
}

static void rectangle(quantapdf_composer_path_command *p,
    float x0,float y0,float x1,float y1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,y1);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static void triangle(quantapdf_composer_path_command *p,
    float x0,float y0,float x1,float y1,float x2,float y2)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x2,y2);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static void zero_contour(quantapdf_composer_path_command *p)
{
    triangle(p,0,0,0,0,0,0);
}

/* style0: triangle+triangle (8); style1: rectangle+triangle (9);
 * style2: rectangle+one cubic/line closed contour (9). Reversing the
 * inner contour changes nonzero winding but never even-odd membership. */
static size_t compound(quantapdf_composer_path_command *p,
    const geometry *g,unsigned style,int reverse)
{
    const float *v=g->v;
    const size_t outer=style==0u?4u:5u;
    if(style==0u)triangle(p,v[1],v[1],v[15],v[1],v[8],v[11]);
    else rectangle(p,v[1],v[1],v[15],v[11]);
    p+=outer;
    if(style<2u) {
        const unsigned bottom=style==0u?3u:4u,top=style==0u?7u:8u;
        if(reverse)triangle(p,v[5],v[bottom],v[8],v[top],v[11],v[bottom]);
        else triangle(p,v[5],v[bottom],v[11],v[bottom],v[8],v[top]);
    } else {
        point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,v[5],v[4]);
        if(reverse) {
            point(&p[1],QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[5],v[9]);
            p[1].point2=(quantapdf_point){v[11],v[9]};
            p[1].point3=(quantapdf_point){v[11],v[4]};
            point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,v[5],v[4]);
        } else {
            point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,v[11],v[4]);
            point(&p[2],QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[11],v[9]);
            p[2].point2=(quantapdf_point){v[5],v[9]};
            p[2].point3=(quantapdf_point){v[5],v[4]};
        }
        point(&p[3],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
    }
    return outer+4u;
}

static void state(quantapdf_composer_operation *op,unsigned id)
{
    op->graphics_state_id=(quantapdf_composer_graphics_state_id)id;
    if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH)
        op->value.path.options.graphics_state_id=op->graphics_state_id;
    else op->value.image.options.graphics_state_id=op->graphics_state_id;
}

static void path_op(fixture *f,size_t index,unsigned id,size_t count,uint32_t color)
{
    quantapdf_composer_operation *op=&f->ops[index];
    memset(op,0,sizeof(*op));op->kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    op->value.path.commands=f->paths[index];op->value.path.command_count=count;
    op->value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op->value.path.options.fill=1;op->value.path.options.fill_argb=color;
    state(op,id);
}

static void setup(fixture *f,const geometry *g,int fractional,const profile *p)
{
    const float *v=g->v;
    const quantapdf_rect bounds[4]={{0,0,v[16],v[12]},
        {0,0,v[16],v[12]},{v[2],0,v[4],v[12]},{0,0,v[16],v[1]}};
    size_t images=0u;
    memset(f,0,sizeof(*f));
    f->page.width_points=fractional?g->fraction_width:g->width;
    f->page.height_points=fractional?g->fraction_height:g->height;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;f->ir.page_count=1u;
    f->ir.operations=f->ops;f->ir.operation_count=strlen(p->kinds);
    f->ir.resources.graphics_states=f->gs;
    f->ir.resources.graphics_state_count=f->ir.resources.graphics_state_capacity=2u;
    f->ir.resources.clips=f->clips;
    f->ir.resources.clip_count=f->ir.resources.clip_capacity=3u;
    for(size_t i=0u;i<2u;++i) {
        f->gs[i].fill_alpha=f->gs[i].stroke_alpha=1;
        f->gs[i].blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        f->gs[i].clip_id=i==0u?1u:3u;
        f->clips[i].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        f->clips[i].commands=f->leaves[i];
        f->clips[i].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->clips[i].transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    }
    f->clips[0].command_count=compound(f->leaves[0],g,1u,0);
    rectangle(f->leaves[1],v[7],v[1],v[15],v[11]);
    f->clips[1].command_count=5u;
    f->clips[2].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[2].members=f->members;f->clips[2].member_count=2u;
    f->members[0]=1u;f->members[1]=2u;
    for(size_t i=0u;i<f->ir.operation_count;++i) {
        const unsigned id=(unsigned)(p->states[i]-'0');
        rectangle(f->paths[i],bounds[i].x0,bounds[i].y0,bounds[i].x1,bounds[i].y1);
        if(p->kinds[i]=='P')path_op(f,i,id,5u,colors[i]);
        else {
            quantapdf_composer_operation *op=&f->ops[i];
            quantapdf_composer_image_state *im=&f->images[images];
            for(size_t j=0u;j<4u;++j) {
                f->pixels[images][j*3u]=(unsigned char)(colors[i]>>16u);
                f->pixels[images][j*3u+1u]=(unsigned char)(colors[i]>>8u);
                f->pixels[images][j*3u+2u]=(unsigned char)colors[i];
            }
            im->data=f->pixels[images];im->size=12u;im->width=im->height=2u;
            im->components=3;im->format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
            op->kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;op->bounds=bounds[i];
            op->value.image.image_id=(quantapdf_composer_image_id)(images+1u);
            op->value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
            op->value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
            state(op,id);++images;
        }
    }
    if(images!=0u)f->ir.resources.images=f->images;
    f->ir.resources.image_count=f->ir.resources.image_capacity=images;
}

static void source(fixture *f,int fractional,const profile *p)
{
    setup(f,&grids[0],fractional,p);
    if(fractional) {
        f->page.width_points=15.25f;f->page.height_points=11.25f;
    }
}

static int cleared(const quantapdf_ir_raster_image *im)
{
    return im->pixels==NULL && im->size==0u && im->width==0u &&
        im->height==0u && im->stride==0u;
}

static int equal(const quantapdf_ir_raster_image *a,const quantapdf_ir_raster_image *b)
{
    return a->pixels!=NULL && b->pixels!=NULL && a->size==b->size &&
        a->width==b->width && a->height==b->height && a->stride==b->stride &&
        memcmp(a->pixels,b->pixels,a->size)==0;
}

static uint32_t rgba(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
        ((uint32_t)p[2]<<8u)|p[3];
}

static int projection_and_ownership(void)
{
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional)
    for(size_t p=0u;p<sizeof(profiles)/sizeof(profiles[0]);++p)
    for(int reverse=0;reverse<2;++reverse)
    for(int even=0;even<2;++even) {
        const geometry *g=&grids[d];
        const double vw=fractional?g->fraction_w:g->viewport_w;
        const double vh=fractional?g->fraction_h:g->viewport_h;
        fixture f,reference,saved;
        quantapdf_ir_raster_image actual={0},expected={0},again={0};
        source(&f,fractional,&profiles[p]);setup(&reference,g,fractional,&profiles[p]);
        compound(f.leaves[0],&grids[0],1u,reverse);
        compound(reference.leaves[0],g,1u,reverse);
        f.clips[0].fill_rule=reference.clips[0].fill_rule=even?
            QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
        f.members[0]=reverse?2u:1u;f.members[1]=reverse?1u:2u;
        memcpy(&saved,&f,sizeof(saved));
        OK(quantapdf_ir_raster_scoped_mixed_v5_compound_clip_states_device_viewport(
            &reference.ir,0u,&budget,vw,vh,&expected));
        OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
            &f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected) && memcmp(&saved,&f,sizeof(saved))==0);
        CHECK(rgba(&actual,g->hole_x,g->hole_y)==
            ((even || reverse)?UINT32_C(0xffffffff):UINT32_C(0x0000ffff)));
        CHECK(rgba(&actual,g->right_x,g->ring_y)==UINT32_C(0x0000ffff));
        CHECK(rgba(&actual,g->ring_x,g->ring_y)==
            (f.ir.operation_count==4u?UINT32_C(0x00ff00ff):
             profiles[p].states[1]=='1'?UINT32_C(0x0000ffff):UINT32_C(0xff0000ff)));
        CHECK(rgba(&actual,0u,g->ring_y)==UINT32_C(0xffffffff));
        OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
            &f.ir,g->dpi,&budget,&again));
        memset(&f,0,sizeof(f));
        CHECK(equal(&actual,&again) && equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&again);
        quantapdf_ir_raster_image_dispose(&expected);
        CHECK(cleared(&actual) && cleared(&again) && cleared(&expected));
    }
    return EXIT_SUCCESS;
}

/* Independent old resource-free PATH reference, authored in device
 * coordinates with EVEN_ODD when a hole is expected. The generic C11
 * Fill API already accepts compound PATHs; public Source and the two-GS
 * profiles still refuse them. One winding evaluation is necessary:
 * painting white over blue rounds an exact half-covered hole to128,
 * whereas half-covered blue over white leaves127. */
static void hole_reference(fixture *f,const geometry *g,int fractional,
    unsigned style,int hole)
{
    const profile p={"PP","12"};
    const float *v=g->v;
    setup(f,g,fractional,&p);
    memset(&f->ir.resources,0,sizeof(f->ir.resources));
    if(style==0u)triangle(f->paths[0],v[1],v[1],v[15],v[1],v[8],v[11]);
    else rectangle(f->paths[0],v[1],v[1],v[15],v[11]);
    path_op(f,0u,0u,style==0u?4u:5u,UINT32_C(0xff0000ff));
    if(style<2u) {
        const unsigned bottom=style==0u?3u:4u,top=style==0u?7u:8u;
        triangle(f->paths[1],v[5],v[bottom],v[11],v[bottom],v[8],v[top]);
    } else {
        point(&f->paths[1][0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,v[5],v[4]);
        point(&f->paths[1][1],QUANTAPDF_COMPOSER_PATH_LINE_TO,v[11],v[4]);
        point(&f->paths[1][2],QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[11],v[9]);
        f->paths[1][2].point2=(quantapdf_point){v[5],v[9]};
        f->paths[1][2].point3=(quantapdf_point){v[5],v[4]};
        point(&f->paths[1][3],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
    }
    if(hole) {
        const size_t outer=style==0u?4u:5u;
        memcpy(&f->paths[0][outer],f->paths[1],4u*sizeof(f->paths[1][0]));
        f->ops[0].value.path.command_count=outer+4u;
        f->ops[0].value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    }
    f->ir.operation_count=1u;
}

static int winding_goldens(void)
{
    const profile pair={"PP","12"};
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional)
    for(unsigned style=0u;style<3u;++style)
    for(int reverse=0;reverse<2;++reverse)
    for(int even=0;even<2;++even) {
        fixture reference;
        const geometry *g=&grids[d];
        const double vw=fractional?g->fraction_w:g->viewport_w;
        const double vh=fractional?g->fraction_h:g->viewport_h;
        quantapdf_ir_raster_image expected={0};
        hole_reference(&reference,g,fractional,style,reverse || even);
        OK(quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &reference.ir,0u,&budget,vw,vh,&expected));
        for(unsigned placement=0u;placement<3u;++placement) {
            fixture f,saved;
            quantapdf_ir_raster_image actual={0};
            source(&f,fractional,&pair);
            f.ops[0].value.path.options.fill_argb=UINT32_C(0xffffffff);
            for(size_t leaf=0u;leaf<2u;++leaf) {
                rectangle(f.leaves[leaf],0,0,16,12);
                f.clips[leaf].command_count=5u;
                if(placement==2u || placement==leaf)
                    f.clips[leaf].command_count=compound(
                        f.leaves[leaf],&grids[0],style,reverse);
                f.clips[leaf].fill_rule=even?
                    QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
            }
            memcpy(&saved,&f,sizeof(saved));
            OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
                &f.ir,g->dpi,&budget,&actual));
            if(!equal(&actual,&expected)) {
                fprintf(stderr,"winding golden dpi=%u fractional=%d style=%u reverse=%d even=%d placement=%u\n",
                    g->dpi,fractional,style,reverse,even,placement);
                quantapdf_ir_raster_image_dispose(&actual);
                quantapdf_ir_raster_image_dispose(&expected);
                return EXIT_FAILURE;
            }
            CHECK(memcmp(&saved,&f,sizeof(saved))==0);
            quantapdf_ir_raster_image_dispose(&actual);
        }
        quantapdf_ir_raster_image_dispose(&expected);
    }
    return EXIT_SUCCESS;
}

/* The older one-GS entries already admit compound leaves. Keep that
 * existing acceptance in one-leaf and flat-root PATH/Image/mixed routes. */
static int old_one_gs_acceptance(void)
{
    static const profile kinds[]={{"PP","11"},{"II","11"},{"IP","11"}};
    for(size_t d=0u;d<3u;++d)
    for(unsigned graph=1u;graph<=3u;graph+=2u)
    for(size_t p=0u;p<sizeof(kinds)/sizeof(kinds[0]);++p)
    for(int even=0;even<2;++even) {
        fixture f,reference;
        const geometry *g=&grids[d];
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,0,&kinds[p]);
        f.ir.resources.graphics_state_count=f.ir.resources.graphics_state_capacity=1u;
        f.ir.resources.clip_count=f.ir.resources.clip_capacity=graph;
        f.gs[0].clip_id=graph;
        rectangle(f.leaves[1],0,0,16,12);
        f.clips[0].fill_rule=even?
            QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
        if(p==0u)f.ops[0].value.path.options.fill_argb=UINT32_C(0xffffffff);
        else memset(f.pixels[0],255,sizeof(f.pixels[0]));
        hole_reference(&reference,g,0,1u,even);
        OK(quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &reference.ir,0u,&budget,g->viewport_w,g->viewport_h,&expected));
        if(p==0u)
            OK(quantapdf_native_device_ir_render_v1_clipped_paths(
                &f.ir,g->dpi,&budget,&actual));
        else if(graph==3u)
            OK(quantapdf_native_device_ir_render_v1_two_clip(
                &f.ir,g->dpi,&budget,&actual));
        else OK(quantapdf_native_device_ir_render_v1(&f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    return EXIT_SUCCESS;
}

static int empty_and_shared_samples(void)
{
    const profile pair={"PP","12"};
    fixture f;
    quantapdf_ir_raster_image im={0};
    /* One canonical empty contour contributes no edges or winding to
     * its nonempty partner. Two empty contours form an empty leaf. */
    for(unsigned empty=0u;empty<3u;++empty) {
        source(&f,0,&pair);
        zero_contour(f.leaves[0]);zero_contour(&f.leaves[0][4]);
        if(empty<2u)triangle(&f.leaves[0][empty==0u?0u:4u],1,1,15,1,8,11);
        f.clips[0].command_count=8u;
        rectangle(f.leaves[1],0,0,16,12);
        f.ops[0].value.path.options.fill_argb=UINT32_C(0xffffffff);
        OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
            &f.ir,72u,&budget,&im));
        CHECK(rgba(&im,8u,5u)==(empty==2u?
            UINT32_C(0xffffffff):UINT32_C(0x0000ffff)));
        CHECK(rgba(&im,0u,5u)==UINT32_C(0xffffffff));
        quantapdf_ir_raster_image_dispose(&im);
    }
    for(unsigned member_order=0u;member_order<2u;++member_order) {
        static const unsigned char pattern[12]={255,0,0,0,0,255,0,255,0,255,255,0};
        source(&f,0,&profiles[1]);
        for(size_t leaf=0u;leaf<2u;++leaf) {
            rectangle(f.leaves[leaf],0,0,16,12);
            if(leaf==0u)triangle(&f.leaves[leaf][5],8.5f,1,8.5f,11,1,6);
            else triangle(&f.leaves[leaf][5],8.5f,1,15,6,8.5f,11);
            f.clips[leaf].command_count=9u;
            f.clips[leaf].fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
        }
        f.members[0]=member_order?2u:1u;f.members[1]=member_order?1u:2u;
        memset(f.pixels[0],255,sizeof(f.pixels[0]));
        memcpy(f.pixels[1],pattern,sizeof(pattern));
        OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
            &f.ir,72u,&budget,&im));
        /* Complementary triangular holes meet at x=8.5. Their leaf
         * masks each cover half this pixel, but the root covers none. */
        CHECK(rgba(&im,8u,5u)==UINT32_C(0xffffffff));
        CHECK(rgba(&im,8u,0u)==UINT32_C(0x0000ffff));
        quantapdf_ir_raster_image_dispose(&im);
        /* Positive control: after restoring leaf1, only its right half
         * survives. The patterned source is BLUE at this pixel center. */
        state(&f.ops[0],2u);state(&f.ops[1],1u);
        OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
            &f.ir,72u,&budget,&im));
        CHECK(rgba(&im,8u,5u)==UINT32_C(0x7f7fffff));
        quantapdf_ir_raster_image_dispose(&im);
    }
    return EXIT_SUCCESS;
}

static int single_contour_subset_and_old_refusal(void)
{
    for(size_t d=0u;d<3u;++d)
    for(size_t p=0u;p<sizeof(profiles)/sizeof(profiles[0]);++p) {
        fixture f;
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,1,&profiles[p]);
        f.clips[0].command_count=5u; /* retain only the original rectangle */
        OK(quantapdf_native_device_ir_render_v1_clip_states(
            &f.ir,grids[d].dpi,&budget,&expected));
        OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
            &f.ir,grids[d].dpi,&budget,&actual));
        CHECK(equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    for(unsigned style=0u;style<3u;++style)
    for(unsigned placement=0u;placement<3u;++placement) {
        fixture f;
        quantapdf_ir_raster_image im;
        source(&f,0,&profiles[0]);
        for(size_t leaf=0u;leaf<2u;++leaf) {
            rectangle(f.leaves[leaf],0,0,16,12);f.clips[leaf].command_count=5u;
            if(placement==2u || placement==leaf)
                f.clips[leaf].command_count=compound(f.leaves[leaf],&grids[0],style,0);
        }
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_native_device_ir_render_v1_clip_states(
            &f.ir,72u,&budget,&im)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(cleared(&im));
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_clip_states_device_viewport(
            &f.ir,0u,&budget,16,12,&im)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(cleared(&im));
    }
    return EXIT_SUCCESS;
}

static int refusal(const quantapdf_ir *ir,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,int direct)
{
    quantapdf_ir_raster_image im;
    memset(&im,0xa5,sizeof(im));
    CHECK(quantapdf_native_device_ir_render_v1_compound_clip_states(
        ir,dpi,limits,&im)!=QUANTAPDF_OK);
    CHECK(cleared(&im));
    if(direct) {
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_compound_clip_states_device_viewport(
            ir,0u,limits,16,12,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
    }
    return EXIT_SUCCESS;
}

static int malformed_and_profile_isolation(void)
{
    fixture f,saved;
    quantapdf_composer_font_state font={0};
    quantapdf_composer_form_state form={0};
    quantapdf_composer_soft_mask_state mask={0};
    quantapdf_composer_paint_state paint={0};
    quantapdf_composer_link_state link={0};
    quantapdf_composer_outline_state outline={0};
    CHECK(refusal(NULL,72u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,&profiles[0]);
    CHECK(refusal(&f.ir,90u,&budget,0)==EXIT_SUCCESS);
    CHECK(refusal(&f.ir,72u,NULL,1)==EXIT_SUCCESS);
    CHECK(quantapdf_native_device_ir_render_v1_compound_clip_states(
        &f.ir,72u,&budget,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_scoped_mixed_v5_compound_clip_states_device_viewport(
        &f.ir,0u,&budget,16,12,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    for(unsigned bad=0u;bad<44u;++bad) {
        source(&f,0,&profiles[0]);
        switch(bad) {
        case 0u:f.clips[0].commands=NULL;break;
        case 1u:f.clips[0].command_count=3u;break;
        case 2u: /* two re rectangles spend ten commands, never nine */
            rectangle(&f.leaves[0][5],5,4,11,8);f.clips[0].command_count=10u;break;
        case 3u:f.leaves[0][0].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 4u:f.leaves[0][4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 5u:f.leaves[0][5].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 6u:f.leaves[0][5].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;break;
        case 7u:f.leaves[0][2].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;break;
        case 8u:f.clips[0].command_count=8u;break; /* implicit last closure forbidden in IR */
        case 9u:f.leaves[0][8].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 10u:
            zero_contour(f.leaves[0]);zero_contour(&f.leaves[0][4]);
            point(&f.leaves[0][8],QUANTAPDF_COMPOSER_PATH_MOVE_TO,1,1);break;
        case 11u:
            zero_contour(f.leaves[0]);
            point(&f.leaves[0][4],QUANTAPDF_COMPOSER_PATH_MOVE_TO,1,1);
            point(&f.leaves[0][5],QUANTAPDF_COMPOSER_PATH_LINE_TO,2,2);
            point(&f.leaves[0][6],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
            f.clips[0].command_count=7u;break;
        case 12u:f.leaves[0][7].point1.x=NAN;break;
        case 13u:
            compound(f.leaves[0],&grids[0],2u,0);
            f.leaves[0][7].point2.y=NAN;break;
        case 14u:
            compound(f.leaves[0],&grids[0],2u,0);
            f.leaves[0][7].point3.x=1000001.0f;break;
        case 15u:f.clips[0].transform.e=0.25f;break;
        case 16u:f.clips[0].members=f.members;break;
        case 17u:f.clips[1].command_count=0u;break;
        case 18u:f.ir.resources.clip_capacity=4u;break;
        case 19u:f.ir.resources.graphics_state_capacity=3u;break;
        case 20u:f.ir.resources.graphics_state_count=1u;break;
        case 21u:f.gs[0].clip_id=3u;break;
        case 22u:f.gs[1].clip_id=1u;break;
        case 23u:f.gs[1].fill_alpha=0.5f;break;
        case 24u:f.clips[2].members=NULL;break;
        case 25u:f.members[1]=1u;break;
        case 26u:f.clips[2].commands=f.leaves[0];break;
        case 27u:f.ir.resources.images=f.images;break;
        case 28u:f.ir.resources.image_capacity=1u;break;
        case 29u:f.ir.resources.fonts=&font;break;
        case 30u:f.ir.resources.font_capacity=1u;break;
        case 31u:f.ir.resources.forms=&form;break;
        case 32u:f.ir.resources.soft_masks=&mask;break;
        case 33u:f.ir.resources.paints=&paint;break;
        case 34u:f.ir.links=&link;break;
        case 35u:f.ir.outlines=&outline;break;
        case 36u:f.ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;break;
        case 37u:f.ops[3].value.path.options.graphics_state_id=1u;break;
        case 38u:f.ir.operation_count=1u;break;
        case 39u:f.ir.operation_count=5u;break;
        case 40u:f.ir.operations=NULL;break;
        case 41u:f.ir.pages=NULL;break;
        case 42u:f.ops[0].value.path.commands=f.leaves[0];
            f.ops[0].value.path.command_count=9u;break;
        case 43u:f.ops[3].value.path.commands=f.leaves[0];
            f.ops[3].value.path.command_count=9u;break;
        default:CHECK(0);break;
        }
        memcpy(&saved,&f,sizeof(saved));
        if(refusal(&f.ir,144u,&budget,1)!=EXIT_SUCCESS) {
            fprintf(stderr,"compound malformed case=%u\n",bad);return EXIT_FAILURE;
        }
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
    }
    {
        static const char *const bad_states[]={"1110","2220","1020","1201","1212","2010"};
        for(size_t i=0u;i<sizeof(bad_states)/sizeof(bad_states[0]);++i) {
            const profile p={"PPPP",bad_states[i]};
            source(&f,0,&p);CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
        }
    }
    /* Three IMAGEs, or three PATHs plus one Image, remain outside the
     * unchanged cardinalities even with valid compound Clip geometry. */
    source(&f,0,&profiles[3]);
    f.ops[2]=f.ops[0];state(&f.ops[2],1u);
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,&profiles[3]);
    path_op(&f,1u,2u,5u,UINT32_C(0xff0000ff));
    f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

static int fixed_budgets(void)
{
    const profile pair={"PP","12"};
    fixture f;
    quantapdf_ir_raster_image im={0};
    source(&f,0,&pair);
    for(unsigned quota=0u;quota<4u;++quota) {
        quantapdf_ir_raster_scoped_mixed_v5_limits limits=budget;
        switch(quota) {
        case 0u:limits.max_pixels=767u;break;
        case 1u:limits.max_operations=1u;break;
        case 2u:limits.max_edges=1u;break;
        case 3u:limits.max_sample_tests=1u;break;
        default:CHECK(0);break;
        }
        CHECK(refusal(&f.ir,144u,&limits,0)==EXIT_SUCCESS);
    }
    {
        /* Each contour is charged: root layer has four paint edges,
         * seven compound-leaf edges and four second-leaf edges. */
        quantapdf_ir_raster_scoped_mixed_v5_limits limits=budget;
        limits.max_edges=30u;limits.max_sample_tests=92160u;
        OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
            &f.ir,72u,&limits,&im));
        quantapdf_ir_raster_image_dispose(&im);
        --limits.max_edges;
        CHECK(refusal(&f.ir,72u,&limits,1)==EXIT_SUCCESS);
        ++limits.max_edges;--limits.max_sample_tests;
        CHECK(refusal(&f.ir,72u,&limits,1)==EXIT_SUCCESS);
    }
    source(&f,0,&profiles[1]);
    f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
    f.ops[1].value.image.image_id=1u;
    for(size_t d=0u;d<3u;++d) {
        quantapdf_ir_raster_scoped_mixed_v5_limits limits=budget;
        limits.max_source_pixels=4u;
        limits.max_drawn_pixels=2u*(size_t)grids[d].width*(size_t)grids[d].height;
        OK(quantapdf_native_device_ir_render_v1_compound_clip_states(
            &f.ir,grids[d].dpi,&limits,&im));
        quantapdf_ir_raster_image_dispose(&im);
        --limits.max_source_pixels;
        CHECK(refusal(&f.ir,grids[d].dpi,&limits,0)==EXIT_SUCCESS);
        ++limits.max_source_pixels;--limits.max_drawn_pixels;
        CHECK(refusal(&f.ir,grids[d].dpi,&limits,0)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(projection_and_ownership()==EXIT_SUCCESS);
    CHECK(winding_goldens()==EXIT_SUCCESS);
    CHECK(old_one_gs_acceptance()==EXIT_SUCCESS);
    CHECK(empty_and_shared_samples()==EXIT_SUCCESS);
    CHECK(single_contour_subset_and_old_refusal()==EXIT_SUCCESS);
    CHECK(malformed_and_profile_isolation()==EXIT_SUCCESS);
    CHECK(fixed_budgets()==EXIT_SUCCESS);
    fprintf(stderr,"[native.compound_clip_device] completed\n");
    return EXIT_SUCCESS;
}
