#include "render/ir_visible_page_device_preview_v0.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_true(int yes, const char *expression, int line)
{
    if (!yes) {
        fprintf(stderr,"visible device Fill/W Clip line %d: %s\n",
            line,expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) require_true(!!(x), #x, __LINE__)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_graphics_state state;
    quantapdf_composer_clip_state clip;
    quantapdf_composer_path_command paint[5];
    quantapdf_composer_path_command clipping[5];
    quantapdf_graph_page_geometry geometry;
    quantapdf_ir_raster_v1_limits limits;
} fixture;

static void source_rectangle(quantapdf_composer_path_command p[5],
    float left,float bottom,float right,float top)
{
    const quantapdf_composer_path_command_kind kinds[5]={
        QUANTAPDF_COMPOSER_PATH_MOVE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_CLOSE
    };
    const quantapdf_point xy[5]={
        {left,-bottom},{right,-bottom},{right,-top},
        {left,-top},{0.0f,0.0f}
    };
    for (size_t i=0u;i<5u;++i) {
        p[i]=(quantapdf_composer_path_command){0};
        p[i].kind=kinds[i];
        p[i].point1=xy[i];
    }
}

static quantapdf_affine_transform independent_source_to_visible(
    int rotation, float unit)
{
    /* Calculated independently from Graph, in the Composer's (x,-y)
     * path basis, with visible output already Y-down. The source CropBox
     * is [2 3 12 11]. There is no extra Content CTM. */
    quantapdf_affine_transform out={0};
    switch(rotation) {
    case 0:
        out.a=unit;out.d=unit;out.e=-2.0f*unit;out.f=11.0f*unit;
        break;
    case 90:
        out.b=unit;out.c=-unit;out.e=-3.0f*unit;out.f=-2.0f*unit;
        break;
    case 180:
        out.a=-unit;out.d=-unit;out.e=12.0f*unit;out.f=-3.0f*unit;
        break;
    case 270:
        out.b=-unit;out.c=unit;out.e=11.0f*unit;out.f=12.0f*unit;
        break;
    default:
        CHECK(0);
    }
    return out;
}

static void setup(fixture *f, int rotation, float unit)
{
    quantapdf_affine_transform transform;
    memset(f,0,sizeof(*f));
    f->geometry.media_box[2]=20.0;
    f->geometry.media_box[3]=30.0;
    f->geometry.crop_box[0]=2.0;
    f->geometry.crop_box[1]=3.0;
    f->geometry.crop_box[2]=12.0;
    f->geometry.crop_box[3]=11.0;
    f->geometry.rotation=rotation;
    f->geometry.user_unit=(double)unit;
    f->page.width_points=unit*(rotation%180 ? 8.0f:10.0f);
    f->page.height_points=unit*(rotation%180 ? 10.0f:8.0f);
    f->page.suppress_background=1;
    f->ir.pages=&f->page;
    f->ir.page_count=1u;
    f->ir.operations=&f->op;
    f->ir.operation_count=1u;
    f->ir.resources.graphics_states=&f->state;
    f->ir.resources.graphics_state_count=1u;
    f->ir.resources.clips=&f->clip;
    f->ir.resources.clip_count=1u;

    f->limits=(quantapdf_ir_raster_v1_limits){
        500000u,1u,64u,500000u
    };
    f->state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->state.fill_alpha=f->state.stroke_alpha=1.0f;
    f->state.clip_id=1u;
    f->op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->op.graphics_state_id=1u;
    f->op.value.path.commands=f->paint;
    f->op.value.path.command_count=5u;
    f->op.value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->op.value.path.options.fill=1;
    f->op.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->op.value.path.options.fill_argb=UINT32_C(0xffff0000);
    f->op.value.path.options.graphics_state_id=1u;
    f->clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clip.commands=f->clipping;
    f->clip.command_count=5u;
    transform=independent_source_to_visible(rotation,unit);
    f->op.value.path.options.transform=transform;
    f->clip.transform=transform;
    source_rectangle(f->paint,4.0f,5.0f,10.0f,9.0f);
    source_rectangle(f->clipping,3.0f,4.0f,9.0f,10.0f);
}

static uint32_t rgba(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+4u*x;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|(uint32_t)p[3];
}

static void render_case(unsigned int dpi,int rotation,float unit)
{
    fixture f;
    quantapdf_ir_raster_image result={0},repeat={0},reference={0};
    quantapdf_composer_page_state before_page;
    quantapdf_composer_operation before_op;
    quantapdf_composer_clip_state before_clip;
    quantapdf_composer_path_command before_paint[5],before_clipping[5];
    double width=(double)unit*(rotation%180 ? 8.0:10.0);
    double height=(double)unit*(rotation%180 ? 10.0:8.0);
    double sx,sy;
    double target_x,target_y;
    size_t px,py;
    setup(&f,rotation,unit);
    before_page=f.page;before_op=f.op;before_clip=f.clip;
    memcpy(before_paint,f.paint,sizeof(before_paint));
    memcpy(before_clipping,f.clipping,sizeof(before_clipping));

    OK(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,dpi,&f.limits,2000000u,&result));
    CHECK(result.width==(size_t)ceil(width*(double)dpi/72.0));
    CHECK(result.height==(size_t)ceil(height*(double)dpi/72.0));
    CHECK(result.pixels!=NULL && result.stride==result.width*4u);
    CHECK(result.size==result.height*result.stride);
    sx=(double)result.width/width;
    sy=(double)result.height/height;
    /* An independently chosen original source interior point (6,7)
     * lies inside BOTH the source Fill rectangle and the W Clip. */
    switch(rotation) {
    case 0: target_x=4.0*unit;target_y=4.0*unit;break;
    case 90:target_x=4.0*unit;target_y=4.0*unit;break;
    case 180:target_x=6.0*unit;target_y=4.0*unit;break;
    case 270:target_x=4.0*unit;target_y=6.0*unit;break;
    default:CHECK(0);target_x=target_y=0.0;
    }
    px=(size_t)floor(target_x*sx);
    py=(size_t)floor(target_y*sy);
    CHECK(px<result.width && py<result.height);
    CHECK(rgba(&result,px,py)==UINT32_C(0xff0000ff));
    CHECK(rgba(&result,0u,0u)==UINT32_C(0x00000000));
    CHECK(rgba(&result,result.width-1u,0u)==UINT32_C(0x00000000));

    OK(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,dpi,&f.limits,2000000u,&repeat));
    CHECK(repeat.size==result.size &&
          memcmp(repeat.pixels,result.pixels,result.size)==0);
    if (dpi==72u) {
        /* The already-normalized source-to-visible IR renders identically
         * when no device upscaling is needed. */
        OK(quantapdf_ir_raster_scoped_operations_v1(
            &f.ir,0u,&f.limits,&reference));
        CHECK(reference.width==result.width && reference.size==result.size);
        CHECK(memcmp(reference.pixels,result.pixels,result.size)==0);
    }
    CHECK(memcmp(&f.page,&before_page,sizeof(f.page))==0);
    CHECK(memcmp(&f.op,&before_op,sizeof(f.op))==0);
    CHECK(memcmp(&f.clip,&before_clip,sizeof(f.clip))==0);
    CHECK(memcmp(f.paint,before_paint,sizeof(f.paint))==0);
    CHECK(memcmp(f.clipping,before_clipping,sizeof(f.clipping))==0);
    quantapdf_ir_raster_image_dispose(&reference);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&result);
}

static void reject_case(void)
{
    fixture f;
    quantapdf_ir_raster_image out;
    setup(&f,90,1.0f);
#define REFUSE(expr) do { \
    memset(&out,0xa5,sizeof(out)); \
    CHECK((expr)==QUANTAPDF_ERROR_UNSUPPORTED); \
    CHECK(out.pixels==NULL && out.size==0u && out.width==0u && \
          out.height==0u && out.stride==0u); \
} while(0)
    /* An accidentally double-applied PDF page affine cannot be admitted. */
    f.op.value.path.options.transform.e+=1.0f;
    REFUSE(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,72,&f.limits,2000000u,&out));
    f.op.value.path.options.transform.e-=1.0f;
    f.clip.transform.f+=1.0f;
    REFUSE(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,72,&f.limits,2000000u,&out));
    f.clip.transform.f-=1.0f;
    f.geometry.crop_box[0]+=1.0; /* Same dimensions, different origin. */
    REFUSE(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,72,&f.limits,2000000u,&out));
    f.geometry.crop_box[0]-=1.0;
    f.op.value.path.options.stroke=1;
    REFUSE(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,72,&f.limits,2000000u,&out));
    f.op.value.path.options.stroke=0;
    f.limits.max_sample_tests=8u*16u*(8u*10u)-1u;
    REFUSE(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,72,&f.limits,2000000u,&out));
    f.limits.max_sample_tests=500000u;
    REFUSE(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,72,&f.limits,8u*10u*4u-1u,&out));
    REFUSE(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
        &f.ir,0u,&f.geometry,120,&f.limits,2000000u,&out));
#undef REFUSE
}

int main(void)
{
    const unsigned int dpis[3]={72u,96u,144u};
    const float units[3]={0.5f,1.0f,2.0f};
    for (size_t d=0u;d<3u;++d)
        for (size_t u=0u;u<3u;++u)
            for (int rot=0;rot<360;rot+=90)
                render_case(dpis[d],rot,units[u]);
    reject_case();
    puts("visible-IR Fill/W Clip device preview: 36 positive geometries PASS");
    return EXIT_SUCCESS;
}
