#include "render/ir_rect_fill_right_preview_v0.h"
#include "render/ir_raster.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if(!(e)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#e); \
    return EXIT_FAILURE; } } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)
#define REFUSE(e) do { \
    quantapdf_ir_raster_image result; \
    memset(&result,0xa5,sizeof(result)); \
    CHECK((e)==QUANTAPDF_ERROR_UNSUPPORTED); \
    CHECK(result.pixels==NULL&&result.size==0u&& \
          result.stride==0u&&result.width==0u&&result.height==0u); \
} while(0)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_graphics_state gs;
    quantapdf_composer_clip_state clip;
    quantapdf_composer_path_command fill[8],mask[8];
    quantapdf_native_device_grid_geometry geometry;
    quantapdf_native_device_grid_limits grid_budget;
    quantapdf_ir_raster_v1_limits raster_budget;
} fixture;

static void rect(quantapdf_composer_path_command *cmd,
                 float left,float top,float right,float bottom)
{
    const quantapdf_composer_path_command_kind types[5]={
        QUANTAPDF_COMPOSER_PATH_MOVE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_CLOSE
    };
    const quantapdf_point points[5]={
        {left,top},{right,top},{right,bottom},{left,bottom},{0,0}
    };
    for(size_t i=0u;i<5u;++i) {
        memset(&cmd[i],0,sizeof(cmd[i]));
        cmd[i].kind=types[i];
        cmd[i].point1=points[i];
    }
}
static void setup(fixture *f)
{
    memset(f,0,sizeof(*f));
    f->geometry=(quantapdf_native_device_grid_geometry){
        32.5,24.75,0.0,0.0,1.0,0,0,72.0f
    };
    f->grid_budget=(quantapdf_native_device_grid_limits){
        1200u,4800u,64u,300000u
    };
    f->raster_budget=(quantapdf_ir_raster_v1_limits){
        1200u,1u,64u,300000u
    };
    f->page.width_points=32.5f;
    f->page.height_points=24.75f;
    f->page.suppress_background=1;
    f->ir.pages=&f->page;
    f->ir.page_count=1u;
    f->ir.operations=&f->op;
    f->ir.operation_count=1u;
    f->ir.resources.graphics_states=&f->gs;
    f->ir.resources.graphics_state_count=1u;
    f->ir.resources.clips=&f->clip;
    f->ir.resources.clip_count=1u;
    f->gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->gs.fill_alpha=f->gs.stroke_alpha=1.0f;
    f->gs.clip_id=1u;
    f->op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->op.graphics_state_id=1u;
    f->op.value.path.commands=f->fill;
    f->op.value.path.command_count=5u;
    f->op.value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->op.value.path.options.fill=1;
    f->op.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->op.value.path.options.fill_argb=UINT32_C(0xffff0000);
    f->op.value.path.options.graphics_state_id=1u;
    f->op.value.path.options.transform=
        (quantapdf_affine_transform){1,0,0,1,0,0};
    f->clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clip.commands=f->mask;
    f->clip.command_count=5u;
    f->clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clip.transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    rect(f->fill,0.0f,0.0f,15.0f,15.0f);
    rect(f->mask,5.0f,2.0f,25.0f,20.0f);
}
static unsigned alpha(const quantapdf_ir_raster_image *im,
                      size_t x,size_t y)
{
    return im->pixels[y*im->stride+x*4u+3u];
}
#define BASE(f,o) quantapdf_ir_raster_scoped_device_grid_v0( \
    &(f).ir,0u,&(f).geometry,&(f).grid_budget,&(f).raster_budget,&(o))
#define TRIAL(f,o) quantapdf_ir_raster_rect_fill_right_preview_v0( \
    &(f).ir,0u,&(f).geometry,&(f).grid_budget,&(f).raster_budget,&(o))
#define LEFT(f,o) quantapdf_ir_raster_rect_fill_left_clip_preview_v0( \
    &(f).ir,0u,&(f).geometry,&(f).grid_budget,&(f).raster_budget,&(o))

static int test_analytic_edge_and_repeat(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},new_image={0},repeat={0};
    setup(&f);
    OK(BASE(f,old));
    OK(TRIAL(f,new_image));
    OK(TRIAL(f,repeat));
    CHECK(new_image.size==old.size && repeat.size==old.size &&
          new_image.width==33u && new_image.height==25u &&
          memcmp(new_image.pixels,repeat.pixels,new_image.size)==0);
    /* Fill right boundary x=15 is neither W right edge (x=25)
     * nor a hardcoded pixel in the preview; its device interval
     * crosses the actual source rectangle edge. */
    const double dx=32.5/33.0;
    const double x0=15.0*dx;
    const double coverage=(15.0-x0)/dx;
    const unsigned expected=(unsigned)floor(255.0*coverage+0.5);
    CHECK(expected>0u && expected<255u);
    CHECK(alpha(&new_image,15u,7u)==expected);
    CHECK(alpha(&new_image,15u,7u)!=alpha(&old,15u,7u));
    CHECK(alpha(&new_image,14u,7u)==alpha(&old,14u,7u));
    CHECK(alpha(&new_image,5u,7u)==alpha(&old,5u,7u));
    CHECK(alpha(&new_image,15u,2u)==alpha(&old,15u,2u));
    CHECK(new_image.pixels[7u*new_image.stride+15u*4u]==255u);
    CHECK(new_image.pixels[7u*new_image.stride+15u*4u+1u]==0u);
    CHECK(new_image.pixels[7u*new_image.stride+15u*4u+2u]==0u);
    size_t changed=0u;
    for(size_t y=0u;y<new_image.height;++y)for(size_t x=0u;x<new_image.width;++x) {
        const size_t offset=y*new_image.stride+x*4u;
        if(old.pixels[offset+3u]==new_image.pixels[offset+3u])
            continue;
        ++changed;
        CHECK(x==15u && y>=3u && y<=14u);
        CHECK(memcmp(old.pixels+offset,new_image.pixels+offset,3u)==0);
    }
    CHECK(changed>0u);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&new_image);
    quantapdf_ir_raster_image_dispose(&old);
    return EXIT_SUCCESS;
}
static int test_retraced_rectangle(void)
{
    fixture f;
    quantapdf_ir_raster_image baseline={0},trial={0};
    setup(&f);
    /* The original jsPDF source draws a full rectangle once and then
     * retraces its upper edge forward/backwards in the same Fill/W Path.
     * That does not create a second paint/alpha source. */
    f.fill[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.fill[4].point1=(quantapdf_point){0.0f,0.0f};
    f.fill[5].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.fill[5].point1=(quantapdf_point){15.0f,0.0f};
    f.fill[6].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.fill[6].point1=(quantapdf_point){0.0f,0.0f};
    f.mask[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.mask[4].point1=(quantapdf_point){5.0f,2.0f};
    f.mask[5].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.mask[5].point1=(quantapdf_point){25.0f,2.0f};
    f.mask[6].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.mask[6].point1=(quantapdf_point){5.0f,2.0f};
    f.op.value.path.command_count=7u;
    f.clip.command_count=7u;
    OK(BASE(f,baseline));
    OK(TRIAL(f,trial));
    CHECK(baseline.size==trial.size &&
          alpha(&trial,15u,7u)<alpha(&baseline,15u,7u));
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&baseline);
    return EXIT_SUCCESS;
}

/* Seventh-only W Clip LEFT analytic pixel model. Right-Fill preview
 * remains independently callable/unchanged; no source-coordinate
 * literal in the actual C11 implementation. */
static int test_left_clip_edge_and_repeat(void)
{
    fixture f;
    quantapdf_ir_raster_image right={0},trial={0},again={0};
    setup(&f);
    OK(TRIAL(f,right));
    OK(LEFT(f,trial));
    OK(LEFT(f,again));
    CHECK(right.size==trial.size && trial.size==again.size &&
          memcmp(trial.pixels,again.pixels,trial.size)==0);
    const double dx=32.5/33.0;
    const double x0=5.0*dx;
    const double x1=6.0*dx;
    CHECK(x0<5.0 && 5.0<x1);
    const double fraction=(x1-5.0)/dx;
    const unsigned expected=(unsigned)floor(255.0*fraction+0.5);
    CHECK(expected>0u && expected<255u);
    CHECK(alpha(&trial,5u,7u)==expected);
    CHECK(alpha(&trial,5u,7u)<alpha(&right,5u,7u));
    CHECK(alpha(&trial,15u,7u)==alpha(&right,15u,7u));
    CHECK(alpha(&trial,6u,7u)==alpha(&right,6u,7u));
    CHECK(alpha(&trial,5u,2u)==alpha(&right,5u,2u));
    size_t changed=0u;
    for(size_t y=0u;y<trial.height;++y)for(size_t x=0u;x<trial.width;++x) {
        const size_t off=y*trial.stride+x*4u;
        CHECK(memcmp(trial.pixels+off,right.pixels+off,3u)==0);
        if(trial.pixels[off+3u]==right.pixels[off+3u])continue;
        ++changed;
        CHECK(x==5u && y>=3u && y<=14u);
    }
    CHECK(changed>0u);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&right);
    return EXIT_SUCCESS;
}
static int test_left_clip_retraces_and_refusal(void)
{
    fixture f;
    quantapdf_ir_raster_image right={0},trial={0},base={0};
    setup(&f);
    f.fill[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.fill[4].point1=(quantapdf_point){0.0f,0.0f};
    f.fill[5].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.fill[5].point1=(quantapdf_point){15.0f,0.0f};
    f.fill[6].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.fill[6].point1=(quantapdf_point){0.0f,0.0f};
    f.mask[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.mask[4].point1=(quantapdf_point){5.0f,2.0f};
    f.mask[5].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.mask[5].point1=(quantapdf_point){25.0f,2.0f};
    f.mask[6].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.mask[6].point1=(quantapdf_point){5.0f,2.0f};
    f.op.value.path.command_count=7u;
    f.clip.command_count=7u;
    OK(TRIAL(f,right));
    OK(LEFT(f,trial));
    CHECK(right.size==trial.size &&
          alpha(&trial,5u,7u)<alpha(&right,5u,7u) &&
          alpha(&trial,15u,7u)==alpha(&right,15u,7u));
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&right);

    /* Different edge-pass funding: mode6 right requires 9*13200
     * sample slots; mode7 right+left needs 10*13200. */
    setup(&f);
    f.grid_budget.max_sample_tests=125000u;
    f.raster_budget.max_sample_tests=125000u;
    OK(TRIAL(f,base));
    quantapdf_ir_raster_image_dispose(&base);
    REFUSE(LEFT(f,result));

    setup(&f);
    f.gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_MULTIPLY;
    REFUSE(LEFT(f,result));
    setup(&f);
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    REFUSE(LEFT(f,result));
    setup(&f);
    f.mask[2].point1.x=21.0f;
    REFUSE(LEFT(f,result));
    setup(&f);
    f.op.value.path.options.transform.a=2.0f;
    REFUSE(LEFT(f,result));
    return EXIT_SUCCESS;
}
static int test_fail_closed(void)
{
    fixture f;
    quantapdf_ir_raster_image baseline={0};
    setup(&f);
    /* The old implementation is allowed at this budget, but the
     * separately charged analytical pass must be refused. */
    f.grid_budget.max_sample_tests=110000u;
    f.raster_budget.max_sample_tests=110000u;
    OK(BASE(f,baseline));
    quantapdf_ir_raster_image_dispose(&baseline);
    REFUSE(TRIAL(f,result));
    setup(&f);
    f.op.value.path.options.transform.a=2.0f;
    REFUSE(TRIAL(f,result)); /* non-translation affine */
    setup(&f);
    f.mask[2].point1.x=21.0f;
    REFUSE(TRIAL(f,result)); /* malformed rectangle */
    setup(&f);
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    REFUSE(TRIAL(f,result));
    setup(&f);
    f.gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_MULTIPLY;
    REFUSE(TRIAL(f,result));
    setup(&f);
    f.ir.resources.clip_count=0u;
    REFUSE(TRIAL(f,result));
    return EXIT_SUCCESS;
}
int main(void)
{
    CHECK(test_analytic_edge_and_repeat()==EXIT_SUCCESS);
    CHECK(test_retraced_rectangle()==EXIT_SUCCESS);
    CHECK(test_left_clip_edge_and_repeat()==EXIT_SUCCESS);
    CHECK(test_left_clip_retraces_and_refusal()==EXIT_SUCCESS);
    CHECK(test_fail_closed()==EXIT_SUCCESS);
    puts("native-rect-fill-right-private-A/B: geometry, repeat, fail-closed PASS");
    return EXIT_SUCCESS;
}
