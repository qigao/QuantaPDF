/* Independent sibling Clips retain their own GS identities. Exact
 * state words, old resource-free geometry references, RAW Image leaf
 * selection, immutable borrows and fixed work budgets are qualified. */
#include "render/native_device_ir_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"sibling Clip device:%d: %s\n",__LINE__,#x); \
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

/* Exactly two independently owned leaves and two corresponding GS.
 * Extra local slots exist only for malformed-count refusal controls. */
static void setup(fixture *f,const geometry *g,int fractional,
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
    f->ir.resources.graphics_state_count=f->ir.resources.graphics_state_capacity=2u;
    f->ir.resources.clips=f->clips;
    f->ir.resources.clip_count=f->ir.resources.clip_capacity=2u;
    for(size_t i=0u;i<2u;++i) {
        f->gs[i].fill_alpha=f->gs[i].stroke_alpha=1;
        f->gs[i].blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        f->gs[i].clip_id=(quantapdf_composer_clip_id)(i+1u);
        f->clips[i].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        f->clips[i].commands=f->leaves[i];
        f->clips[i].command_count=5u;
        f->clips[i].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->clips[i].transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    }
    rectangle(f->leaves[0],v[2],v[2],v[12],v[10]);
    rectangle(f->leaves[1],v[6],0,v[14],v[8]);
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

static void source(fixture *f,int fractional,
                   const char *kinds,const char *states)
{
    setup(f,&grids[0],fractional,kinds,states);
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
    CHECK(quantapdf_native_device_ir_render_v1_sibling_clips(
        ir,dpi,limit,&im)!=QUANTAPDF_OK);
    CHECK(cleared(&im));
    if(direct) {
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_sibling_clips_device_viewport(
            ir,0u,limit,16,12,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
    }
    return EXIT_SUCCESS;
}

static void rectangle_reference(fixture *f,const geometry *g,int fractional,
                                const char *kinds,const char *states)
{
    const float *v=g->v;
    const quantapdf_rect bounds[4]={{0,0,v[16],v[12]},
        {0,0,v[16],v[12]},{v[2],0,v[4],v[12]},{0,0,v[16],v[1]}};
    setup(f,g,fractional,kinds,states);
    memset(&f->ir.resources,0,sizeof(f->ir.resources));
    for(size_t i=0u;i<f->ir.operation_count;++i) {
        quantapdf_rect r=bounds[i];
        if(states[i]!='0') {
            const quantapdf_rect clip=states[i]=='1'?
                (quantapdf_rect){v[2],v[2],v[12],v[10]}:
                (quantapdf_rect){v[6],0,v[14],v[8]};
            if(r.x0<clip.x0)r.x0=clip.x0;
            if(r.y0<clip.y0)r.y0=clip.y0;
            if(r.x1>clip.x1)r.x1=clip.x1;
            if(r.y1>clip.y1)r.y1=clip.y1;
        }
        if(r.x1<=r.x0 || r.y1<=r.y0) {
            /* A valid off-viewport PATH contributes no pixels without
             * relying on the old painted-PATH zero-edge refusal. */
            rectangle(f->paths[i],-2,-2,-1,-1);
        } else rectangle(f->paths[i],r.x0,r.y0,r.x1,r.y1);
        path_op(f,i,0u,5u,colors[i]);
    }
}

static int rectangular_projection_and_ownership(void)
{
    static const struct {const char *kinds,*states;} profiles[]={
        {"PP","12"},{"PPP","012"},{"PPP","102"},{"PPP","120"},
        {"PPPP","0102"},{"PPPP","1020"},{"PPPP","0120"},
        {"PPPP","1122"},{"PPPP","1200"},{"PPPP","1220"},
        {"II","12"},{"IP","12"},{"PI","12"},{"IPP","102"},
        {"PIP","120"},{"PII","012"},{"IIP","120"},
        {"IPIP","1020"},{"PIPI","0120"},{"PPII","1122"}
    };
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional)
    for(size_t p=0u;p<sizeof(profiles)/sizeof(profiles[0]);++p) {
        const geometry *g=&grids[d];
        const double vw=fractional?g->fraction_w:g->viewport_w;
        const double vh=fractional?g->fraction_h:g->viewport_h;
        fixture f,device,reference,saved;
        quantapdf_ir_raster_image actual={0},projected={0},expected={0},again={0};
        source(&f,fractional,profiles[p].kinds,profiles[p].states);
        setup(&device,g,fractional,profiles[p].kinds,profiles[p].states);
        rectangle_reference(&reference,g,fractional,profiles[p].kinds,profiles[p].states);
        memcpy(&saved,&f,sizeof(saved));
        OK(quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &reference.ir,0u,&budget,vw,vh,&expected));
        OK(quantapdf_ir_raster_scoped_mixed_v5_sibling_clips_device_viewport(
            &device.ir,0u,&budget,vw,vh,&projected));
        OK(quantapdf_native_device_ir_render_v1_sibling_clips(
            &f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected) && equal(&actual,&projected));
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
        if(p==0u || p==10u) {
            /* Leaf B extends beyond A. A mistaken intersection or
             * first-leaf pointer cannot paint this blue B-only cell. */
            CHECK(rgba(&actual,(size_t)g->v[13],(size_t)g->v[4])==
                  UINT32_C(0x0000ffff));
            CHECK(rgba(&actual,(size_t)g->v[3],(size_t)g->v[9])==
                  UINT32_C(0xff0000ff));
        }
        OK(quantapdf_native_device_ir_render_v1_sibling_clips(
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

static int older_entries_refuse(const quantapdf_ir *ir)
{
    typedef quantapdf_status (*native_fn)(const quantapdf_ir *,unsigned,
        const quantapdf_ir_raster_scoped_mixed_v5_limits *,quantapdf_ir_raster_image *);
    typedef quantapdf_status (*viewport_fn)(const quantapdf_ir *,size_t,
        const quantapdf_ir_raster_scoped_mixed_v5_limits *,double,double,
        quantapdf_ir_raster_image *);
    static const native_fn native[]={
        quantapdf_native_device_ir_render_v1,
        quantapdf_native_device_ir_render_v1_two_clip,
        quantapdf_native_device_ir_render_v1_clipped_paths,
        quantapdf_native_device_ir_render_v1_clip_states,
        quantapdf_native_device_ir_render_v1_compound_clip_states,
        quantapdf_native_device_ir_render_v1_unclipped_prefix
    };
    static const viewport_fn viewport[]={
        quantapdf_ir_raster_scoped_mixed_v5_device_viewport,
        quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport,
        quantapdf_ir_raster_scoped_mixed_v5_clipped_paths_device_viewport,
        quantapdf_ir_raster_scoped_mixed_v5_clip_states_device_viewport,
        quantapdf_ir_raster_scoped_mixed_v5_compound_clip_states_device_viewport,
        quantapdf_ir_raster_scoped_mixed_v5_unclipped_prefix_device_viewport
    };
    quantapdf_ir_raster_image im;
    for(size_t i=0u;i<sizeof(native)/sizeof(native[0]);++i) {
        memset(&im,0xa5,sizeof(im));
        CHECK(native[i](ir,72u,&budget,&im)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(cleared(&im));
    }
    for(size_t i=0u;i<sizeof(viewport)/sizeof(viewport[0]);++i) {
        memset(&im,0xa5,sizeof(im));
        CHECK(viewport[i](ir,0u,&budget,16,12,&im)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(cleared(&im));
    }
    memset(&im,0xa5,sizeof(im));
    CHECK(quantapdf_ir_raster_scoped_mixed_v5(ir,0u,&budget,&im)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(cleared(&im));
    memset(&im,0xa5,sizeof(im));
    CHECK(quantapdf_ir_raster_scoped_mixed_v5_device(ir,0u,&budget,&im)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(cleared(&im));
    return EXIT_SUCCESS;
}

static int all_state_orders(void)
{
    static const char *const admitted[]={
        "12","012","102","112","120","122",
        "0012","0102","0112","0120","0122","1002","1020",
        "1022","1102","1112","1120","1122","1200","1220","1222"
    };
    size_t accepted=0u,rejected=0u;
    for(size_t count=0u;count<=4u;++count) {
        unsigned words=1u;
        for(size_t i=0u;i<count;++i)words*=3u;
        for(unsigned word=0u;word<words;++word) {
            unsigned value=word;char order[5]={0},kinds[5]={0};
            int admit=0;
            fixture f;
            quantapdf_ir_raster_image im={0};
            for(size_t i=count;i>0u;--i) {
                order[i-1u]=(char)('0'+value%3u);value/=3u;kinds[i-1u]='P';
            }
            for(size_t i=0u;i<sizeof(admitted)/sizeof(admitted[0]);++i)
                if(strcmp(order,admitted[i])==0)admit=1;
            source(&f,0,kinds,order);
            if(admit) {
                OK(quantapdf_native_device_ir_render_v1_sibling_clips(
                    &f.ir,72u,&budget,&im));
                quantapdf_ir_raster_image_dispose(&im);
                OK(quantapdf_ir_raster_scoped_mixed_v5_sibling_clips_device_viewport(
                    &f.ir,0u,&budget,16,12,&im));
                quantapdf_ir_raster_image_dispose(&im);
                CHECK(older_entries_refuse(&f.ir)==EXIT_SUCCESS);++accepted;
            } else {
                CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);++rejected;
            }
        }
    }
    CHECK(accepted==21u && rejected==100u);
    return EXIT_SUCCESS;
}

/* Disjoint authored left/right geometries ensure each independent
 * sibling remains visible. Winding and empty contours belong only
 * to their own leaf; no union/intersection descriptor is involved. */
static size_t complex_clip(quantapdf_composer_path_command *p,
    const geometry *g,unsigned offset,unsigned style,int reverse)
{
    const float *v=g->v;
    if(style==3u || style==5u) {
        triangle(p,v[offset+1u],v[1],v[offset+1u],v[1],v[offset+1u],v[1]);
        if(style==5u)triangle(p+4,v[offset+2u],v[2],v[offset+2u],v[2],v[offset+2u],v[2]);
        return style==3u?4u:8u;
    }
    if(style==4u) {
        point(p,QUANTAPDF_COMPOSER_PATH_MOVE_TO,v[offset+1u],v[1]);
        point(p+1,QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[offset+1u],v[11]);
        p[1].point2=(quantapdf_point){v[offset+7u],v[11]};
        p[1].point3=(quantapdf_point){v[offset+7u],v[1]};
        point(p+2,QUANTAPDF_COMPOSER_PATH_LINE_TO,v[offset+1u],v[1]);
        point(p+3,QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
        return 4u;
    }
    const size_t outer=style==0u?4u:5u;
    if(style==0u)triangle(p,v[offset+1u],v[1],v[offset+7u],v[1],v[offset+4u],v[11]);
    else rectangle(p,v[offset+1u],v[1],v[offset+7u],v[11]);
    p+=outer;
    if(style<2u) {
        if(reverse)triangle(p,v[offset+2u],v[3],v[offset+4u],v[7],v[offset+6u],v[3]);
        else triangle(p,v[offset+2u],v[3],v[offset+6u],v[3],v[offset+4u],v[7]);
    } else {
        point(p,QUANTAPDF_COMPOSER_PATH_MOVE_TO,v[offset+2u],v[4]);
        if(reverse) {
            point(p+1,QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[offset+2u],v[9]);
            p[1].point2=(quantapdf_point){v[offset+6u],v[9]};
            p[1].point3=(quantapdf_point){v[offset+6u],v[4]};
            point(p+2,QUANTAPDF_COMPOSER_PATH_LINE_TO,v[offset+2u],v[4]);
        } else {
            point(p+1,QUANTAPDF_COMPOSER_PATH_LINE_TO,v[offset+6u],v[4]);
            point(p+2,QUANTAPDF_COMPOSER_PATH_CUBIC_TO,v[offset+6u],v[9]);
            p[2].point2=(quantapdf_point){v[offset+2u],v[9]};
            p[2].point3=(quantapdf_point){v[offset+2u],v[4]};
        }
        point(p+3,QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
    }
    return outer+4u;
}

static int compound_and_curved_goldens(void)
{
    for(size_t d=0u;d<3u;++d)
    for(unsigned style=0u;style<6u;++style)
    for(unsigned rules=0u;rules<4u;++rules)
    for(unsigned prefix=0u;prefix<2u;++prefix) {
        const geometry *g=&grids[d];
        const char *kinds=prefix?"PPP":"PP";
        const char *states=prefix?"012":"12";
        fixture f,reference;
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,1,kinds,states);setup(&reference,g,1,kinds,states);
        memset(&reference.ir.resources,0,sizeof(reference.ir.resources));
        for(size_t i=0u;i<f.ir.operation_count;++i) {
            rectangle(f.paths[i],0,0,16,12);
            rectangle(reference.paths[i],0,0,g->v[16],g->v[12]);
            path_op(&reference,i,0u,5u,colors[i]);
        }
        for(unsigned leaf=0u;leaf<2u;++leaf) {
            const unsigned shape=leaf?5u-style:style;
            const unsigned offset=leaf?8u:0u;
            const int even=(rules & (1u<<leaf))!=0u;
            const int reverse=(style & 1u)!=0u;
            f.clips[leaf].command_count=complex_clip(f.leaves[leaf],&grids[0],offset,shape,reverse);
            f.clips[leaf].fill_rule=even?QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
                                           QUANTAPDF_COMPOSER_FILL_NONZERO;
            const size_t op=prefix+leaf;
            if(shape==3u || shape==5u) {
                rectangle(reference.paths[op],-2,-2,-1,-1);
                path_op(&reference,op,0u,5u,colors[op]);
            } else {
                const size_t count=complex_clip(reference.paths[op],g,offset,shape,reverse);
                path_op(&reference,op,0u,count,colors[op]);
                reference.ops[op].value.path.options.fill_rule=f.clips[leaf].fill_rule;
            }
        }
        OK(quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &reference.ir,0u,&budget,g->fraction_w,g->fraction_h,&expected));
        OK(quantapdf_native_device_ir_render_v1_sibling_clips(
            &f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    return EXIT_SUCCESS;
}

static int image_leaf_selection_and_touching(void)
{
    static const unsigned char pattern[]={255,0,0,0,255,0,0,0,255,255,255,0};
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional) {
        fixture f,reference;
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,fractional,"II","12");
        source(&reference,fractional,"I","1");
        triangle(f.leaves[0],0,0,0,0,0,0);f.clips[0].command_count=4u;
        memcpy(f.pixels[1],pattern,sizeof(pattern));
        memcpy(reference.pixels[0],pattern,sizeof(pattern));
        reference.ir.resources.graphics_state_count=reference.ir.resources.graphics_state_capacity=1u;
        reference.ir.resources.clip_count=reference.ir.resources.clip_capacity=1u;
        rectangle(reference.leaves[0],6,0,14,8);
        OK(quantapdf_native_device_ir_render_v1(
            &reference.ir,grids[d].dpi,&budget,&expected));
        OK(quantapdf_native_device_ir_render_v1_sibling_clips(
            &f.ir,grids[d].dpi,&budget,&actual));
        CHECK(equal(&actual,&expected));
        if(d==0u) {
            CHECK(rgba(&actual,13u,2u)==UINT32_C(0x00ff00ff));
            CHECK(rgba(&actual,13u,7u)==UINT32_C(0xffff00ff));
            CHECK(rgba(&actual,3u,9u)==UINT32_C(0xffffffff));
        }
        CHECK(older_entries_refuse(&f.ir)==EXIT_SUCCESS);
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    for(int images=0;images<2;++images) {
        fixture f,reference;
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,0,images?"II":"PP","12");
        source(&reference,0,"PP","12");
        rectangle(f.leaves[0],0,0,8.5f,12);
        rectangle(f.leaves[1],8.5f,0,16,12);
        memset(&reference.ir.resources,0,sizeof(reference.ir.resources));
        rectangle(reference.paths[0],0,0,8.5f,12);
        rectangle(reference.paths[1],8.5f,0,16,12);
        state(&reference.ops[0],0u);state(&reference.ops[1],0u);
        OK(quantapdf_native_device_ir_render_v1(&reference.ir,72u,&budget,&expected));
        OK(quantapdf_native_device_ir_render_v1_sibling_clips(&f.ir,72u,&budget,&actual));
        CHECK(equal(&actual,&expected));
        /* These are separate paints, so their independently averaged
         * half-covered layers compose in source order over white. */
        CHECK(rgba(&actual,8u,4u)==UINT32_C(0x7f3fbfff));
        CHECK(rgba(&actual,13u,4u)==UINT32_C(0x0000ffff));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
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
    source(&f,0,"PPPP","1020");
    CHECK(refusal(&f.ir,90u,&budget,0)==EXIT_SUCCESS);
    CHECK(refusal(&f.ir,72u,NULL,1)==EXIT_SUCCESS);
    CHECK(quantapdf_native_device_ir_render_v1_sibling_clips(
        &f.ir,72u,&budget,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_scoped_mixed_v5_sibling_clips_device_viewport(
        &f.ir,0u,&budget,16,12,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    for(unsigned bad=0u;bad<58u;++bad) {
        source(&f,0,"PPPP","1020");
        switch(bad) {
        case 0u:f.ir.resources.clip_count=0u;break;
        case 1u:f.ir.resources.clip_count=1u;break;
        case 2u:f.ir.resources.clip_count=f.ir.resources.clip_capacity=3u;break;
        case 3u:f.ir.resources.clip_capacity=3u;break;
        case 4u:f.ir.resources.clips=NULL;break;
        case 5u:f.ir.resources.graphics_state_count=0u;break;
        case 6u:f.ir.resources.graphics_state_count=1u;break;
        case 7u:f.ir.resources.graphics_state_count=3u;break;
        case 8u:f.ir.resources.graphics_state_capacity=3u;break;
        case 9u:f.ir.resources.graphics_states=NULL;break;
        case 10u:f.gs[0].clip_id=2u;break;
        case 11u:f.gs[1].clip_id=1u;break;
        case 12u:f.gs[1].clip_id=3u;break;
        case 13u:f.gs[1].fill_alpha=0.5f;break;
        case 14u:f.gs[0].soft_mask_id=1u;break;
        case 15u:f.clips[1].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;break;
        case 16u:f.clips[0].members=f.members;break;
        case 17u:f.clips[1].member_count=1u;break;
        case 18u:f.clips[1].transform.e=0.25f;break;
        case 19u:f.clips[0].commands=NULL;break;
        case 20u:f.clips[0].command_count=3u;break;
        case 21u:f.clips[1].command_count=10u;break;
        case 22u:f.leaves[1][0].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 23u:f.leaves[0][4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 24u:f.leaves[1][2].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;break;
        case 25u:f.clips[1].fill_rule=(quantapdf_composer_fill_rule)99;break;
        case 26u:f.clips[1].command_count=0u;break;
        case 27u:f.clips[0].transform.a=NAN;break;
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
        case 41u:f.ops[1].value.path.options.struct_size=
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE;break;
        case 42u:f.ops[3].value.path.options.stroke=1;break;
        case 43u:f.ops[3].value.path.options.fill_argb=UINT32_C(0x80ff0000);break;
        case 44u:f.ir.operation_count=1u;break;
        case 45u:f.ir.operation_count=5u;break;
        case 46u:f.ir.operations=NULL;break;
        case 47u:f.ir.pages=NULL;break;
        case 48u:f.ops[0].value.path.command_count=
            complex_clip(f.paths[0],&grids[0],0u,0u,0);break;
        case 49u:f.ops[3].value.path.command_count=
            complex_clip(f.paths[3],&grids[0],0u,0u,0);break;
        case 50u:f.ops[2].graphics_state_id=3u;break;
        case 51u:f.ops[3].page_index=1u;break;
        case 52u: /* Too-short second contour. */
            triangle(f.leaves[0],1,1,7,1,4,11);
            point(f.leaves[0]+4,QUANTAPDF_COMPOSER_PATH_MOVE_TO,2,2);
            point(f.leaves[0]+5,QUANTAPDF_COMPOSER_PATH_LINE_TO,3,3);
            point(f.leaves[0]+6,QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
            f.clips[0].command_count=7u;break;
        case 53u:
            f.clips[1].command_count=complex_clip(f.leaves[1],&grids[0],8u,2u,0);
            f.leaves[1][7].point2.x=NAN;break;
        case 54u:
            f.clips[1].command_count=complex_clip(f.leaves[1],&grids[0],8u,2u,0);
            f.leaves[1][7].point3.y=1000001.0f;break;
        case 55u:f.ir.resources.clip_capacity=1u;break;
        case 56u:f.ir.resources.graphics_state_capacity=1u;break;
        case 57u:f.ir.page_count=2u;break;
        default:CHECK(0);break;
        }
        memcpy(&saved,&f,sizeof(saved));
        CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
    }
    for(unsigned bad=0u;bad<9u;++bad) {
        source(&f,0,"II","12");
        switch(bad) {
        case 0u:f.ops[1].value.image.options.graphics_state_id=1u;break;
        case 1u:f.ops[1].value.image.options.struct_size=
            QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE;break;
        case 2u:f.ops[1].value.image.image_id=3u;break;
        case 3u:f.ops[1].value.image.image_id=1u;break; /* unused Image 2 */
        case 4u:f.images[1].size=11u;break;
        case 5u:f.ir.resources.image_capacity=3u;break;
        case 6u:f.ops[1].bounds.x1=f.ops[1].bounds.x0;break;
        case 7u:f.images[1].data=NULL;break;
        case 8u:f.images[1].alpha_data=f.pixels[0];break;
        default:CHECK(0);break;
        }
        CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    }
    source(&f,0,"III","112");
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
    f.ops[1].value.image.image_id=f.ops[2].value.image.image_id=1u;
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,"PPPI","1122");
    CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,"IIIP","1122");
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
    for(int fractional=0;fractional<2;++fractional) {
        quantapdf_ir_raster_scoped_mixed_v5_limits limits=budget;
        source(&f,fractional,"PPPP","1020");
        rectangle(f.paths[2],0,0,16,12);
        /* Every PATH reserves one share. Each sibling paint charges
         * only its own four Clip edges, plus viewport edges if needed. */
        const size_t largest=fractional?12u:8u;
        limits.max_edges=4u*largest;
        limits.max_sample_tests=4u*largest*16u*16u*12u;
        OK(quantapdf_native_device_ir_render_v1_sibling_clips(
            &f.ir,72u,&limits,&im));
        quantapdf_ir_raster_image_dispose(&im);
        --limits.max_edges;
        CHECK(refusal(&f.ir,72u,&limits,!fractional)==EXIT_SUCCESS);
        ++limits.max_edges;--limits.max_sample_tests;
        CHECK(refusal(&f.ir,72u,&limits,!fractional)==EXIT_SUCCESS);
        limits=budget;limits.max_operations=3u;
        CHECK(refusal(&f.ir,72u,&limits,!fractional)==EXIT_SUCCESS);
        limits=budget;limits.max_pixels=191u;
        CHECK(refusal(&f.ir,72u,&limits,!fractional)==EXIT_SUCCESS);
    }
    for(size_t d=0u;d<3u;++d) {
        quantapdf_ir_raster_scoped_mixed_v5_limits limits=budget;
        fixture saved;
        quantapdf_ir_raster_image again={0};
        source(&f,0,"II","12");
        f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
        f.ops[1].value.image.image_id=1u;
        limits.max_source_pixels=4u;
        limits.max_drawn_pixels=2u*(size_t)grids[d].width*(size_t)grids[d].height;
        memcpy(&saved,&f,sizeof(saved));
        OK(quantapdf_native_device_ir_render_v1_sibling_clips(
            &f.ir,grids[d].dpi,&limits,&im));
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
        --limits.max_source_pixels;
        CHECK(refusal(&f.ir,grids[d].dpi,&limits,0)==EXIT_SUCCESS);
        ++limits.max_source_pixels;--limits.max_drawn_pixels;
        CHECK(refusal(&f.ir,grids[d].dpi,&limits,0)==EXIT_SUCCESS);
        ++limits.max_drawn_pixels;
        OK(quantapdf_native_device_ir_render_v1_sibling_clips(
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
    CHECK(image_leaf_selection_and_touching()==EXIT_SUCCESS);
    CHECK(malformed_graphs_and_paints()==EXIT_SUCCESS);
    CHECK(exact_budgets_and_shared_images()==EXIT_SUCCESS);
    fprintf(stderr,"[native.sibling_clip_device] completed: 120 rectangle, "
        "144 complex, 121 state words (21 accepted/100 refused), "
        "67 malformed graph/paint/resource controls\n");
    return EXIT_SUCCESS;
}

