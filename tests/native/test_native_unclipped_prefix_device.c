/* Explicit GS0 paint before W keeps the existing four-layer sampler,
 * exact state order, graph owners and budgets. Rectangle goldens are
 * independently intersected PATHs on authored device coordinate grids. */
#include "render/native_device_ir_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"unclipped prefix device:%d: %s\n",__LINE__,#x); \
    return EXIT_FAILURE; } } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[5];
    quantapdf_composer_image_state images[3];
    quantapdf_composer_graphics_state gs[2];
    quantapdf_composer_clip_state clips[3];
    quantapdf_composer_clip_id members[2];
    quantapdf_composer_path_command leaves[2][10],paths[5][10];
    unsigned char pixels[3][12];
} fixture;

typedef struct geometry {
    unsigned dpi;
    float v[17],width,height,fraction_width,fraction_height;
    double viewport_w,viewport_h,fraction_w,fraction_h;
} geometry;

/* The 96-DPI rational grid and fractional physical viewports are
 * authored independently of the production point projector. */
static const geometry grids[]={
    {72u,{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},
        16,12,16,12,16,12,15.25,11.25},
    {96u,{0,4.0f/3,8.0f/3,4,16.0f/3,20.0f/3,8,28.0f/3,
        32.0f/3,12,40.0f/3,44.0f/3,16,52.0f/3,56.0f/3,20,64.0f/3},
        22,16,21,15,64.0/3,16,61.0/3,15},
    {144u,{0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32},
        32,24,31,23,32,24,30.5,22.5}
};
static const quantapdf_ir_raster_scoped_mixed_v5_limits budget={
    4096u,4u,512u,8000000u,8u,8192u
};
static const uint32_t colors[]={UINT32_C(0xffff0000),UINT32_C(0xff0000ff),
    UINT32_C(0xff00ff00),UINT32_C(0xffffff00),UINT32_C(0xff00ffff)};

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

/* graph 0: one leaf/one GS; graph 1: two leaves/root/one GS;
 * graph 2: two leaves/root/two GS. No owner is inferred from a paint. */
static void setup(fixture *f,const geometry *g,int fractional,unsigned graph,
                  const char *kinds,const char *states)
{
    const float *v=g->v;
    const quantapdf_rect bounds[5]={{0,0,v[16],v[12]},
        {0,0,v[16],v[12]},{v[2],0,v[4],v[12]},
        {0,0,v[16],v[1]},{0,0,v[16],v[12]}};
    size_t images=0u;
    memset(f,0,sizeof(*f));
    f->page.width_points=fractional?g->fraction_width:g->width;
    f->page.height_points=fractional?g->fraction_height:g->height;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;f->ir.page_count=1u;
    f->ir.operations=f->ops;f->ir.operation_count=strlen(kinds);
    f->ir.resources.graphics_states=f->gs;
    f->ir.resources.graphics_state_count=f->ir.resources.graphics_state_capacity=
        graph==2u?2u:1u;
    f->ir.resources.clips=f->clips;
    f->ir.resources.clip_count=f->ir.resources.clip_capacity=graph==0u?1u:3u;
    for(size_t i=0u;i<2u;++i) {
        f->gs[i].fill_alpha=f->gs[i].stroke_alpha=1;
        f->gs[i].blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        f->gs[i].clip_id=graph==0u || (graph==2u && i==0u)?1u:3u;
        f->clips[i].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        f->clips[i].commands=f->leaves[i];
        f->clips[i].command_count=5u;
        f->clips[i].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->clips[i].transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    }
    rectangle(f->leaves[0],v[2],v[2],v[12],v[10]);
    rectangle(f->leaves[1],v[6],0,v[14],v[8]);
    f->clips[2].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[2].members=f->members;f->clips[2].member_count=2u;
    f->members[0]=1u;f->members[1]=2u;
    for(size_t i=0u;i<f->ir.operation_count;++i) {
        const unsigned id=(unsigned)(states[i]-'0');
        rectangle(f->paths[i],bounds[i].x0,bounds[i].y0,bounds[i].x1,bounds[i].y1);
        if(kinds[i]=='P')path_op(f,i,id,5u,colors[i]);
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

static void source(fixture *f,int fractional,unsigned graph,
                   const char *kinds,const char *states)
{
    setup(f,&grids[0],fractional,graph,kinds,states);
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

static int refusal(const quantapdf_ir *ir,unsigned dpi,
                   const quantapdf_ir_raster_scoped_mixed_v5_limits *limit,
                   int direct)
{
    quantapdf_ir_raster_image im;
    memset(&im,0xa5,sizeof(im));
    CHECK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
        ir,dpi,limit,&im)!=QUANTAPDF_OK);
    CHECK(cleared(&im));
    if(direct) {
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_unclipped_prefix_device_viewport(
            ir,0u,limit,16,12,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
    }
    return EXIT_SUCCESS;
}

static void rectangle_reference(fixture *f,const geometry *g,int fractional,
                                unsigned graph,const char *kinds,const char *states)
{
    const float *v=g->v;
    const quantapdf_rect bounds[4]={{0,0,v[16],v[12]},
        {0,0,v[16],v[12]},{v[2],0,v[4],v[12]},{0,0,v[16],v[1]}};
    setup(f,g,fractional,graph,kinds,states);
    memset(&f->ir.resources,0,sizeof(f->ir.resources));
    for(size_t i=0u;i<f->ir.operation_count;++i) {
        quantapdf_rect r=bounds[i];
        if(states[i]!='0') {
            const int root=graph==1u || (graph==2u && states[i]=='2');
            const quantapdf_rect clip=root?
                (quantapdf_rect){v[6],v[2],v[12],v[8]}:
                (quantapdf_rect){v[2],v[2],v[12],v[10]};
            if(r.x0<clip.x0)r.x0=clip.x0;
            if(r.y0<clip.y0)r.y0=clip.y0;
            if(r.x1>clip.x1)r.x1=clip.x1;
            if(r.y1>clip.y1)r.y1=clip.y1;
        }
        if(r.x1<=r.x0 || r.y1<=r.y0) {
            /* The old painted-PATH profile requires a nonempty edge
             * list. A valid off-viewport rectangle supplies no pixels. */
            rectangle(f->paths[i],-2,-2,-1,-1);
            path_op(f,i,0u,5u,colors[i]);
        } else {
            rectangle(f->paths[i],r.x0,r.y0,r.x1,r.y1);
            path_op(f,i,0u,5u,colors[i]);
        }
    }
}

static int rectangular_projection_and_ownership(void)
{
    static const struct {unsigned graph;const char *kinds,*states;} profiles[]={
        {0u,"PP","01"},{0u,"PPPP","0010"},{0u,"PPPP","0110"},
        {0u,"II","01"},{0u,"IP","01"},{0u,"PI","01"},
        {0u,"IPIP","0110"},{0u,"PIPI","0010"},
        {1u,"PP","01"},{1u,"PPPP","0110"},{1u,"II","01"},
        {1u,"IP","01"},{1u,"PIPI","0110"},
        {2u,"PPP","012"},{2u,"PPP","021"},{2u,"PPPP","0012"},
        {2u,"PPPP","0121"},{2u,"PPPP","0210"},
        {2u,"IPP","012"},{2u,"PIP","021"},
        {2u,"IPIP","0121"},{2u,"PIPI","0210"}
    };
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional)
    for(size_t p=0u;p<sizeof(profiles)/sizeof(profiles[0]);++p) {
        const geometry *g=&grids[d];
        const double vw=fractional?g->fraction_w:g->viewport_w;
        const double vh=fractional?g->fraction_h:g->viewport_h;
        fixture f,device,reference,saved;
        quantapdf_ir_raster_image actual={0},projected={0},expected={0},again={0};
        source(&f,fractional,profiles[p].graph,profiles[p].kinds,profiles[p].states);
        setup(&device,g,fractional,profiles[p].graph,profiles[p].kinds,profiles[p].states);
        rectangle_reference(&reference,g,fractional,profiles[p].graph,
                            profiles[p].kinds,profiles[p].states);
        memcpy(&saved,&f,sizeof(saved));
        const quantapdf_status reference_status=
            quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
                &reference.ir,0u,&budget,vw,vh,&expected);
        if(reference_status!=QUANTAPDF_OK)
            fprintf(stderr,"rectangle reference graph=%u kinds=%s states=%s dpi=%u fractional=%d status=%d\n",
                profiles[p].graph,profiles[p].kinds,profiles[p].states,
                g->dpi,fractional,(int)reference_status);
        CHECK(reference_status==QUANTAPDF_OK);
        OK(quantapdf_ir_raster_scoped_mixed_v5_unclipped_prefix_device_viewport(
            &device.ir,0u,&budget,vw,vh,&projected));
        OK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
            &f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected) && equal(&actual,&projected));
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
        /* Every graph leaves the last opaque prefix paint visible
         * outside W; a second initial GS0 paint may cover the first. */
        size_t last_prefix=0u;
        while(profiles[p].states[last_prefix+1u]=='0')++last_prefix;
        CHECK(rgba(&actual,0u,(size_t)g->v[4])==
              ((colors[last_prefix]<<8u)|UINT32_C(0xff)));
        OK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
            &f.ir,g->dpi,&budget,&again));
        memset(&f,0,sizeof(f));
        CHECK(equal(&actual,&again) && equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&again);
        quantapdf_ir_raster_image_dispose(&projected);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    return EXIT_SUCCESS;
}

static int all_state_orders(void)
{
    static const char *const one[]={"01","001","010","011","0001",
        "0010","0011","0100","0110","0111"};
    static const char *const two[]={"012","021","0012","0021","0112",
        "0120","0121","0122","0210","0211","0221"};
    size_t accepted=0u,rejected=0u;
    for(unsigned graph=0u;graph<3u;++graph)
    for(size_t count=1u;count<=4u;++count) {
        const unsigned base=graph==2u?3u:2u;
        unsigned words=1u;
        for(size_t i=0u;i<count;++i)words*=base;
        for(unsigned word=0u;word<words;++word) {
            unsigned value=word;char order[5]={0},kinds[5]={0};
            int admit=0;
            fixture f;
            quantapdf_ir_raster_image im={0};
            for(size_t i=count;i>0u;--i) {
                order[i-1u]=(char)('0'+value%base);value/=base;kinds[i-1u]='P';
            }
            if(graph==2u) {
                for(size_t i=0u;i<sizeof(two)/sizeof(two[0]);++i)
                    if(strcmp(order,two[i])==0)admit=1;
            } else for(size_t i=0u;i<sizeof(one)/sizeof(one[0]);++i)
                if(strcmp(order,one[i])==0)admit=1;
            source(&f,0,graph,kinds,order);
            if(admit) {
                OK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
                    &f.ir,72u,&budget,&im));
                quantapdf_ir_raster_image_dispose(&im);
                OK(quantapdf_ir_raster_scoped_mixed_v5_unclipped_prefix_device_viewport(
                    &f.ir,0u,&budget,16,12,&im));
                quantapdf_ir_raster_image_dispose(&im);
                /* These prior PATH/two-GS contracts really refused a
                 * GS0 prefix. Old mixed acceptance is checked below. */
                memset(&im,0xa5,sizeof(im));
                CHECK((graph==2u?
                    quantapdf_native_device_ir_render_v1_compound_clip_states(
                        &f.ir,72u,&budget,&im):
                    quantapdf_native_device_ir_render_v1_clipped_paths(
                        &f.ir,72u,&budget,&im))==QUANTAPDF_ERROR_UNSUPPORTED);
                CHECK(cleared(&im));
                memset(&im,0xa5,sizeof(im));
                CHECK((graph==2u?
                    quantapdf_ir_raster_scoped_mixed_v5_compound_clip_states_device_viewport(
                        &f.ir,0u,&budget,16,12,&im):
                    quantapdf_ir_raster_scoped_mixed_v5_clipped_paths_device_viewport(
                        &f.ir,0u,&budget,16,12,&im))==QUANTAPDF_ERROR_UNSUPPORTED);
                CHECK(cleared(&im));++accepted;
            } else {
                CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);++rejected;
            }
        }
    }
    CHECK(accepted==31u && rejected==149u);
    return EXIT_SUCCESS;
}

/* Two contours still share ONE winding calculation. The old resource-
 * free PATH consumer is an independent admission for each authored
 * full-page geometry reference; no white-over-color subtraction. */
static size_t complex_clip(quantapdf_composer_path_command *p,
                           const geometry *g,unsigned style,int reverse)
{
    const float *v=g->v;
    if(style==3u || style==5u) {
        triangle(p,0,0,0,0,0,0);
        if(style==5u)triangle(p+4,0,0,0,0,0,0);
        return style==3u?4u:8u;
    }
    if(style==4u) {
        point(p,QUANTAPDF_COMPOSER_PATH_MOVE_TO,v[1],v[1]);
        point(p+1,QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[1],v[11]);
        p[1].point2=(quantapdf_point){v[15],v[11]};
        p[1].point3=(quantapdf_point){v[15],v[1]};
        point(p+2,QUANTAPDF_COMPOSER_PATH_LINE_TO,v[1],v[1]);
        point(p+3,QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
        return 4u;
    }
    const size_t outer=style==0u?4u:5u;
    if(style==0u)triangle(p,v[1],v[1],v[15],v[1],v[8],v[11]);
    else rectangle(p,v[1],v[1],v[15],v[11]);
    p+=outer;
    if(style<2u) {
        const unsigned bottom=style==0u?3u:4u,top=style==0u?7u:8u;
        if(reverse)triangle(p,v[5],v[bottom],v[8],v[top],v[11],v[bottom]);
        else triangle(p,v[5],v[bottom],v[11],v[bottom],v[8],v[top]);
    } else {
        point(p,QUANTAPDF_COMPOSER_PATH_MOVE_TO,v[5],v[4]);
        if(reverse) {
            point(p+1,QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[5],v[9]);
            p[1].point2=(quantapdf_point){v[11],v[9]};
            p[1].point3=(quantapdf_point){v[11],v[4]};
            point(p+2,QUANTAPDF_COMPOSER_PATH_LINE_TO,v[5],v[4]);
        } else {
            point(p+1,QUANTAPDF_COMPOSER_PATH_LINE_TO,v[11],v[4]);
            point(p+2,QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[11],v[9]);
            p[2].point2=(quantapdf_point){v[5],v[9]};
            p[2].point3=(quantapdf_point){v[5],v[4]};
        }
        point(p+3,QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
    }
    return outer+4u;
}

static int compound_and_curved_goldens(void)
{
    for(size_t d=0u;d<3u;++d)
    for(unsigned graph=0u;graph<3u;++graph)
    for(unsigned leaf=0u;leaf<(graph==0u?1u:2u);++leaf)
    for(unsigned style=0u;style<6u;++style)
    for(int even=0;even<2;++even) {
        const geometry *g=&grids[d];
        const char *kinds=graph==2u?"PPP":"PP";
        const char *states=graph==2u?"012":"01";
        fixture f,reference;
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,1,graph,kinds,states);
        setup(&reference,g,1,graph,kinds,states);
        for(size_t i=0u;i<2u;++i) {
            rectangle(f.leaves[i],0,0,16,12);f.clips[i].command_count=5u;
        }
        f.clips[leaf].command_count=complex_clip(f.leaves[leaf],&grids[0],style,even);
        f.clips[leaf].fill_rule=even?QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
                                       QUANTAPDF_COMPOSER_FILL_NONZERO;
        memset(&reference.ir.resources,0,sizeof(reference.ir.resources));
        for(size_t i=0u;i<f.ir.operation_count;++i) {
            const int clipped=states[i]!='0' &&
                (graph!=2u || states[i]=='2' || leaf==0u);
            rectangle(f.paths[i],0,0,16,12);
            if(clipped && (style==3u || style==5u)) {
                rectangle(reference.paths[i],-2,-2,-1,-1);
                path_op(&reference,i,0u,5u,colors[i]);
            } else if(clipped) {
                const size_t count=complex_clip(reference.paths[i],g,style,even);
                path_op(&reference,i,0u,count,colors[i]);
                reference.ops[i].value.path.options.fill_rule=f.clips[leaf].fill_rule;
            } else {
                rectangle(reference.paths[i],0,0,g->v[16],g->v[12]);
                path_op(&reference,i,0u,5u,colors[i]);
            }
        }
        OK(quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &reference.ir,0u,&budget,g->fraction_w,g->fraction_h,&expected));
        OK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
            &f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    return EXIT_SUCCESS;
}

static int legacy_mixed_isolation(void)
{
    for(unsigned graph=0u;graph<2u;++graph)
    for(unsigned reentry=0u;reentry<2u;++reentry) {
        fixture f;
        quantapdf_ir_raster_image native={0},direct={0},fresh={0};
        source(&f,0,graph,reentry?"IPIP":"IP",reentry?"0101":"01");
        OK(graph==0u?quantapdf_native_device_ir_render_v1(
            &f.ir,72u,&budget,&native):quantapdf_native_device_ir_render_v1_two_clip(
            &f.ir,72u,&budget,&native));
        OK(graph==0u?quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &f.ir,0u,&budget,16,12,&direct):
            quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
                &f.ir,0u,&budget,16,12,&direct));
        CHECK(equal(&native,&direct));
        if(reentry)CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
        else {
            OK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
                &f.ir,72u,&budget,&fresh));
            CHECK(equal(&fresh,&native));
            quantapdf_ir_raster_image_dispose(&fresh);
        }
        quantapdf_ir_raster_image_dispose(&native);
        quantapdf_ir_raster_image_dispose(&direct);
        source(&f,0,graph,"II","01");
        memset(&native,0xa5,sizeof(native));
        CHECK((graph==0u?quantapdf_native_device_ir_render_v1(
            &f.ir,72u,&budget,&native):quantapdf_native_device_ir_render_v1_two_clip(
            &f.ir,72u,&budget,&native))==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(cleared(&native));
    }
    return EXIT_SUCCESS;
}

static int malformed_graphs_and_paints(void)
{
    fixture f,saved;
    quantapdf_composer_font_state font={0};
    quantapdf_composer_form_state form={0};
    quantapdf_composer_soft_mask_state mask={0};
    quantapdf_composer_paint_state paint={0};
    quantapdf_composer_link_state link={0};
    quantapdf_composer_outline_state outline={0};
    CHECK(refusal(NULL,72u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,2u,"PPPP","0210");
    CHECK(refusal(&f.ir,90u,&budget,0)==EXIT_SUCCESS);
    CHECK(refusal(&f.ir,72u,NULL,1)==EXIT_SUCCESS);
    CHECK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
        &f.ir,72u,&budget,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_scoped_mixed_v5_unclipped_prefix_device_viewport(
        &f.ir,0u,&budget,16,12,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    for(unsigned bad=0u;bad<52u;++bad) {
        source(&f,0,2u,"PPPP","0210");
        switch(bad) {
        case 0u:f.ir.resources.clip_count=0u;break;
        case 1u:f.ir.resources.clip_count=2u;break;
        case 2u:f.ir.resources.clip_count=4u;break;
        case 3u:f.ir.resources.clip_capacity=2u;break;
        case 4u:f.ir.resources.clips=NULL;break;
        case 5u:f.ir.resources.graphics_state_count=0u;break;
        case 6u:f.ir.resources.graphics_state_count=3u;break;
        case 7u:f.ir.resources.graphics_state_capacity=1u;break;
        case 8u:f.ir.resources.graphics_states=NULL;break;
        case 9u:f.gs[0].clip_id=3u;break;
        case 10u:f.gs[1].clip_id=1u;break;
        case 11u:f.gs[1].fill_alpha=0.5f;break;
        case 12u:f.gs[0].soft_mask_id=1u;break;
        case 13u:f.clips[2].members=NULL;break;
        case 14u:f.clips[2].member_count=1u;break;
        case 15u:f.members[1]=1u;break;
        case 16u:f.members[0]=3u;break;
        case 17u:f.clips[2].commands=f.leaves[0];break;
        case 18u:f.clips[2].transform.e=1;break;
        case 19u:f.clips[0].commands=NULL;break;
        case 20u:f.clips[0].command_count=3u;break;
        case 21u:f.clips[0].command_count=10u;break;
        case 22u:f.leaves[0][0].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 23u:f.leaves[0][4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 24u:f.leaves[0][2].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;break;
        case 25u:f.clips[0].members=f.members;break;
        case 26u:f.clips[1].command_count=0u;break;
        case 27u:f.clips[0].transform.e=0.25f;break;
        case 28u:f.leaves[1][2].point1.x=NAN;break;
        case 29u:f.leaves[1][2].point1.y=1000001.0f;break;
        case 30u:f.ir.resources.images=f.images;break;
        case 31u:f.ir.resources.image_capacity=1u;break;
        case 32u:f.ir.resources.fonts=&font;break;
        case 33u:f.ir.resources.font_capacity=1u;break;
        case 34u:f.ir.resources.forms=&form;break;
        case 35u:f.ir.resources.soft_masks=&mask;break;
        case 36u:f.ir.resources.paints=&paint;break;
        case 37u:f.ir.links=&link;break;
        case 38u:f.ir.outlines=&outline;break;
        case 39u:f.ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;break;
        case 40u:f.ops[3].value.path.options.graphics_state_id=1u;break;
        case 41u:f.ops[0].value.path.options.struct_size=
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE;break;
        case 42u:f.ops[3].value.path.options.stroke=1;break;
        case 43u:f.ops[3].value.path.options.fill_argb=UINT32_C(0x80ff0000);break;
        case 44u:f.ir.operation_count=1u;break;
        case 45u:f.ir.operation_count=5u;break;
        case 46u:f.ir.operations=NULL;break;
        case 47u:f.ir.pages=NULL;break;
        case 48u:f.ops[0].value.path.command_count=
            complex_clip(f.paths[0],&grids[0],0u,0);break;
        case 49u:f.ops[3].value.path.command_count=
            complex_clip(f.paths[3],&grids[0],0u,0);break;
        case 50u:f.ir.resources.clip_count=f.ir.resources.clip_capacity=1u;break;
        case 51u:f.ops[3].page_index=1u;break;
        default:CHECK(0);break;
        }
        memcpy(&saved,&f,sizeof(saved));
        CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
    }
    /* One-leaf/one-GS must independently validate capacities and frozen
     * grammar; it cannot rely on the two-leaf helper having run. */
    for(unsigned bad=0u;bad<10u;++bad) {
        source(&f,0,0u,"IP","01");
        switch(bad) {
        case 0u:f.ir.resources.graphics_state_capacity=2u;break;
        case 1u:f.ir.resources.clip_capacity=2u;break;
        case 2u:f.clips[0].transform.e=1;break;
        case 3u:f.clips[0].command_count=3u;break;
        case 4u:f.clips[0].members=f.members;break;
        case 5u:f.gs[0].clip_id=3u;break;
        case 6u:f.ops[0].value.image.options.struct_size=
            QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE;break;
        case 7u:f.ops[0].value.image.options.graphics_state_id=1u;break;
        case 8u:f.images[0].size=11u;break;
        case 9u:f.ir.resources.image_capacity=2u;break;
        default:CHECK(0);break;
        }
        CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    }
    source(&f,0,2u,"III","012");
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
    f.ops[1].value.image.image_id=f.ops[2].value.image.image_id=1u;
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,2u,"PPPI","0121");
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,2u,"IIIP","0121");
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
    f.ops[1].value.image.image_id=f.ops[2].value.image.image_id=1u;
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

static int exact_budgets_and_shared_images(void)
{
    fixture f;
    quantapdf_ir_raster_image im={0};
    for(unsigned graph=0u;graph<3u;++graph) {
        quantapdf_ir_raster_scoped_mixed_v5_limits limits=budget;
        const size_t layers=graph==2u?3u:2u;
        const size_t largest_edges=graph==0u?8u:12u;
        source(&f,0,graph,graph==2u?"PPP":"PP",graph==2u?"012":"01");
        /* Every PATH, including the prefix, reserves a geometry share.
         * Root layers charge paint plus both Clip edge lists. */
        rectangle(f.paths[2],0,0,16,12);
        limits.max_edges=layers*largest_edges;
        limits.max_sample_tests=layers*largest_edges*16u*16u*12u;
        OK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
            &f.ir,72u,&limits,&im));
        quantapdf_ir_raster_image_dispose(&im);
        --limits.max_edges;
        CHECK(refusal(&f.ir,72u,&limits,1)==EXIT_SUCCESS);
        ++limits.max_edges;--limits.max_sample_tests;
        CHECK(refusal(&f.ir,72u,&limits,1)==EXIT_SUCCESS);
        limits=budget;limits.max_operations=layers-1u;
        CHECK(refusal(&f.ir,72u,&limits,1)==EXIT_SUCCESS);
    }
    for(unsigned graph=0u;graph<2u;++graph)
    for(size_t d=0u;d<3u;++d) {
        quantapdf_ir_raster_scoped_mixed_v5_limits limits=budget;
        fixture saved;
        quantapdf_ir_raster_image again={0};
        source(&f,0,graph,"II","01");
        f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
        f.ops[1].value.image.image_id=1u;
        limits.max_source_pixels=4u;
        limits.max_drawn_pixels=2u*(size_t)grids[d].width*(size_t)grids[d].height;
        memcpy(&saved,&f,sizeof(saved));
        OK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
            &f.ir,grids[d].dpi,&limits,&im));
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
        --limits.max_source_pixels;
        CHECK(refusal(&f.ir,grids[d].dpi,&limits,0)==EXIT_SUCCESS);
        ++limits.max_source_pixels;--limits.max_drawn_pixels;
        CHECK(refusal(&f.ir,grids[d].dpi,&limits,0)==EXIT_SUCCESS);
        ++limits.max_drawn_pixels;
        OK(quantapdf_native_device_ir_render_v1_unclipped_prefix(
            &f.ir,grids[d].dpi,&limits,&again));
        memset(&f,0,sizeof(f));
        CHECK(equal(&im,&again));
        quantapdf_ir_raster_image_dispose(&im);
        quantapdf_ir_raster_image_dispose(&again);
    }
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(rectangular_projection_and_ownership()==EXIT_SUCCESS);
    CHECK(all_state_orders()==EXIT_SUCCESS);
    CHECK(compound_and_curved_goldens()==EXIT_SUCCESS);
    CHECK(legacy_mixed_isolation()==EXIT_SUCCESS);
    CHECK(malformed_graphs_and_paints()==EXIT_SUCCESS);
    CHECK(exact_budgets_and_shared_images()==EXIT_SUCCESS);
    fprintf(stderr,"[native.unclipped_prefix_device] completed\n");
    return EXIT_SUCCESS;
}
