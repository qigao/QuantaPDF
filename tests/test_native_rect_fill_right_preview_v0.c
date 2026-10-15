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
#define BOTTOM(f,o) quantapdf_ir_raster_rect_fill_bottom_preview_v0( \
    &(f).ir,0u,&(f).geometry,&(f).grid_budget,&(f).raster_budget,&(o))
#define UPPER_RIGHT_Q8(f,o) quantapdf_ir_raster_rect_fill_upper_right_q8_preview_v0( \
    &(f).ir,0u,&(f).geometry,&(f).grid_budget,&(f).raster_budget,&(o))
#define NEAR_BOTTOM(f,o) quantapdf_ir_raster_rect_fill_bottom_near_edges_preview_v0( \
    &(f).ir,0u,&(f).geometry,&(f).grid_budget,&(f).raster_budget,&(o))
#define CORNER_BOTTOM_RIGHT(f,o) \
    quantapdf_ir_raster_rect_fill_bottom_right_q8_preview_v0( \
    &(f).ir,0u,&(f).geometry,&(f).grid_budget,&(f).raster_budget,&(o))
#define CORNER_CLIP_LEFT_TOP(f,o) \
    quantapdf_ir_raster_rect_clip_left_top_preview_v0( \
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

/* Eighth isolated source-Fill BOTTOM edge A/B, only a weak pixel row
 * that the previous 4x4 V0 sampler left completely transparent. */
static int test_bottom_faint_fringe(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},trial={0},repeat={0};
    setup(&f);
    /* Choose a source bottom just after the top of device row15,
     * BEFORE its first 4x4 subpixel sample: the old Fill truly left
     * that whole row untouched, unlike source bottom=15.0. */
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    OK(LEFT(f,old));
    OK(BOTTOM(f,trial));
    OK(BOTTOM(f,repeat));
    CHECK(old.size==trial.size && trial.size==repeat.size &&
          memcmp(trial.pixels,repeat.pixels,trial.size)==0);
    const double dy=24.75/25.0;
    const double row0=15.0*dy;
    const double row1=16.0*dy;
    const double true_bottom=(double)14.9f;
    CHECK(row0<true_bottom && true_bottom<row1);
    const unsigned expected=(unsigned)floor(
        255.0*(true_bottom-row0)/(row1-row0)+0.5);
    CHECK(expected>0u && expected<255u);
    CHECK(alpha(&old,10u,15u)==0u);
    CHECK(alpha(&trial,10u,15u)==expected);
    const unsigned char *p=trial.pixels+15u*trial.stride+10u*4u;
    CHECK(p[0]==255u && p[1]==0u && p[2]==0u);
    CHECK(alpha(&trial,10u,14u)==alpha(&old,10u,14u));
    CHECK(alpha(&trial,5u,7u)==alpha(&old,5u,7u));
    CHECK(alpha(&trial,15u,7u)==alpha(&old,15u,7u));
    size_t changed=0u;
    for(size_t y=0u;y<trial.height;++y)
        for(size_t x=0u;x<trial.width;++x) {
            const size_t off=y*trial.stride+x*4u;
            if(memcmp(old.pixels+off,trial.pixels+off,4u)==0)
                continue;
            ++changed;
            CHECK(y==15u && x>=7u && x<=13u);
            CHECK(old.pixels[off]==0u && old.pixels[off+1u]==0u &&
                  old.pixels[off+2u]==0u && old.pixels[off+3u]==0u);
            CHECK(trial.pixels[off]==255u &&
                  trial.pixels[off+1u]==0u &&
                  trial.pixels[off+2u]==0u &&
                  trial.pixels[off+3u]==expected);
        }
    CHECK(changed==7u);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);
    return EXIT_SUCCESS;
}
static int test_bottom_retraces_and_refusal(void)
{
    fixture f;
    quantapdf_ir_raster_image left={0},trial={0};
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    /* A repeated source rectangle border is one Fill/Clip, never
     * multiple alpha sources. Keep all three previous edge passes. */
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
    OK(LEFT(f,left));
    OK(BOTTOM(f,trial));
    CHECK(alpha(&trial,10u,15u)>0u);
    CHECK(alpha(&trial,5u,7u)==alpha(&left,5u,7u));
    CHECK(alpha(&trial,15u,7u)==alpha(&left,15u,7u));
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&left);

    /* NEGATIVE: source bottom=15.0 already covers one 4x4 sample
     * in row15, so strict mode8 must refuse touching that pixel. */
    setup(&f);
    REFUSE(BOTTOM(f,result));

    /* Work budget admits mode7's 10*13,200 slots but refuses
     * mode8's separately charged 11*13,200 slots. */
    setup(&f);
    f.grid_budget.max_sample_tests=140000u;
    f.raster_budget.max_sample_tests=140000u;
    OK(LEFT(f,left));
    quantapdf_ir_raster_image_dispose(&left);
    REFUSE(BOTTOM(f,result));
    setup(&f);
    f.gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_MULTIPLY;
    REFUSE(BOTTOM(f,result));
    setup(&f);
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    REFUSE(BOTTOM(f,result));
    setup(&f);
    f.mask[2].point1.x=21.0f;
    REFUSE(BOTTOM(f,result));
    setup(&f);
    f.op.value.path.options.transform.a=2.0f;
    REFUSE(BOTTOM(f,result));

    /* A device row EXACTLY aligned with source bottom has zero new
     * fringe. The eighth candidate must refuse rather than fabricate. */
    setup(&f);
    f.geometry.dpi=96.0f;
    f.grid_budget.max_pixels=2000u;
    f.grid_budget.max_rgba_bytes=8000u;
    f.grid_budget.max_sample_tests=800000u;
    f.raster_budget.max_pixels=2000u;
    f.raster_budget.max_sample_tests=800000u;
    REFUSE(BOTTOM(f,result));
    return EXIT_SUCCESS;
}


/* Ninth isolated, pure-C11 source Fill-right/W Clip-top Q8 coverage
 * experiment. Only the synthetic true coupled corner cell may change;
 * no original PDFium oracle, source-coordinate runtime heuristics,
 * duplicate owner or fallback is used in this standalone fixture. */
static int test_upper_right_q8_corner(void)
{
    fixture f;
    quantapdf_ir_raster_image previous={0},candidate={0},repeat={0};
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    OK(BOTTOM(f,previous));
    OK(UPPER_RIGHT_Q8(f,candidate));
    OK(UPPER_RIGHT_Q8(f,repeat));
    CHECK(previous.pixels!=NULL && candidate.pixels!=NULL &&
          repeat.pixels!=NULL && previous.size==candidate.size &&
          candidate.size==repeat.size &&
          memcmp(candidate.pixels,repeat.pixels,candidate.size)==0);
    const double dx=32.5/33.0,dy=24.75/25.0;
    const int64_t xright=(int64_t)((float)(15.0/dx)*256.0f);
    const int64_t xleft=(int64_t)((float)(5.0/dx)*256.0f);
    const int64_t ytop=(int64_t)((float)(2.0/dy)*256.0f);
    const int64_t ybottom=(int64_t)((float)((double)14.9f/dy)*256.0f);
    const int64_t cell_x=15*256,cell_y=2*256;
    const int64_t ax=xright-cell_x;
    const int64_t ay=(cell_y+256)-ytop;
    CHECK(xleft<cell_x && ybottom>cell_y+256 &&
          ax>0 && ax<256 && ay>0 && ay<256);
    const unsigned expected=(unsigned)((ax*ay+128)/256);
    CHECK(expected>0u && expected<255u);
    CHECK(alpha(&candidate,15u,2u)==expected &&
          alpha(&candidate,15u,2u)!=alpha(&previous,15u,2u));
    CHECK(alpha(&candidate,15u,7u)==alpha(&previous,15u,7u));
    CHECK(alpha(&candidate,5u,7u)==alpha(&previous,5u,7u));
    CHECK(alpha(&candidate,10u,15u)==alpha(&previous,10u,15u));
    size_t changed=0u;
    for(size_t y=0u;y<candidate.height;++y)
        for(size_t x=0u;x<candidate.width;++x) {
            const size_t off=y*candidate.stride+x*4u;
            CHECK(memcmp(previous.pixels+off,candidate.pixels+off,3u)==0);
            if(previous.pixels[off+3u]!=candidate.pixels[off+3u]) {
                CHECK(x==15u && y==2u);
                ++changed;
            }
        }
    CHECK(changed==1u);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&previous);

    /* Genuine sub-Q8 device-row phase: the continuous W top is strictly
     * inside row2, but Q8 truncation makes it EXACTLY cell_y. It must be
     * admitted with 256/256 Y coverage rather than refused or offset. */
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    rect(f.mask,5.0f,1.9801f,25.0f,20.0f);
    const double tiny_phase_dy=24.75/25.0;
    CHECK((double)1.9801f>2.0*tiny_phase_dy &&
          (double)1.9801f<3.0*tiny_phase_dy);
    const int64_t phase_top_q=
        (int64_t)((float)((double)1.9801f/tiny_phase_dy)*256.0f);
    CHECK(phase_top_q==(int64_t)2*256);
    OK(BOTTOM(f,previous));
    OK(UPPER_RIGHT_Q8(f,candidate));
    CHECK(candidate.size==previous.size &&
          alpha(&candidate,15u,2u)!=alpha(&previous,15u,2u));
    const unsigned phase_expected=(unsigned)(ax*256/256);
    CHECK(alpha(&candidate,15u,2u)==phase_expected);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&previous);

    /* Q8 mode9 charges one more original source-edge pass than mode8.
     * The latter is admissible at 150K samples, the former must refuse
     * transactionally, with ZERO partially published output. */
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.grid_budget.max_sample_tests=150000u;
    f.raster_budget.max_sample_tests=150000u;
    OK(BOTTOM(f,previous));
    quantapdf_ir_raster_image_dispose(&previous);
    REFUSE(UPPER_RIGHT_Q8(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    REFUSE(UPPER_RIGHT_Q8(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.op.value.path.options.transform.a=2.0f;
    REFUSE(UPPER_RIGHT_Q8(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.mask[2].point1.x=21.0f;
    REFUSE(UPPER_RIGHT_Q8(f,result));
    return EXIT_SUCCESS;
}


/* Tenth private source-exact bottom fringe: not a global one-pixel border
 * growth. Only source-FULL X coverage of the formerly empty bottom row
 * is admitted, leaving partial corners and all old mode9 bytes alone. */
static int test_source_bottom_near_edges(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},trial={0},repeat={0};
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    OK(UPPER_RIGHT_Q8(f,old));
    OK(NEAR_BOTTOM(f,trial));
    OK(NEAR_BOTTOM(f,repeat));
    CHECK(old.pixels && trial.pixels && repeat.pixels &&
          old.size==trial.size && trial.size==repeat.size &&
          memcmp(trial.pixels,repeat.pixels,trial.size)==0);
    const double dy=24.75/25.0;
    const unsigned expected=(unsigned)floor(
        255.0*((double)14.9f-15.0*dy)/dy+0.5);
    CHECK(expected>0u && expected<255u);
    CHECK(alpha(&old,6u,15u)==0u &&
          alpha(&trial,6u,15u)==expected);
    CHECK(alpha(&old,14u,15u)==0u &&
          alpha(&trial,14u,15u)==expected);
    CHECK(alpha(&old,5u,15u)==alpha(&trial,5u,15u));
    CHECK(alpha(&old,15u,15u)==alpha(&trial,15u,15u));
    CHECK(alpha(&old,15u,2u)==alpha(&trial,15u,2u));
    size_t count=0u;
    for(size_t y=0u;y<trial.height;++y)
        for(size_t x=0u;x<trial.width;++x) {
            const size_t off=y*trial.stride+x*4u;
            if(old.pixels[off+3u]!=trial.pixels[off+3u]) {
                /* Source A8 coverage goes from fully CLEAR to the red
                 * fringe: RGB must be initialized together with alpha,
                 * not falsely required to remain transparent black. */
                CHECK(y==15u && (x==6u || x==14u));
                CHECK(old.pixels[off]==0u && old.pixels[off+1u]==0u &&
                      old.pixels[off+2u]==0u && old.pixels[off+3u]==0u);
                CHECK(trial.pixels[off]==255u &&
                      trial.pixels[off+1u]==0u &&
                      trial.pixels[off+2u]==0u &&
                      trial.pixels[off+3u]==expected);
                ++count;
            } else {
                /* No hidden RGB changes in already-owned source cells. */
                CHECK(memcmp(old.pixels+off,trial.pixels+off,4u)==0);
            }
        }
    CHECK(count==2u);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);

    /* Previous mode9 12-edge budget admitted, new mode10 13-edge
     * budget rejected with zero output at 165K sample slots. */
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.grid_budget.max_sample_tests=165000u;
    f.raster_budget.max_sample_tests=165000u;
    OK(UPPER_RIGHT_Q8(f,old));
    quantapdf_ir_raster_image_dispose(&old);
    REFUSE(NEAR_BOTTOM(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    REFUSE(NEAR_BOTTOM(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.op.value.path.options.transform.a=2.0f;
    REFUSE(NEAR_BOTTOM(f,result));
    return EXIT_SUCCESS;
}


/* Eleventh strict C11 A/B: a single genuinely fractional Fill-right X /
 * Fill-bottom Y intersection produces a tiny red corner on the original
 * device grid. Old source rectangle edges and all other RGBA retained. */
static int test_bottom_right_q8_tiny_corner(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},trial={0},again={0};
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    OK(NEAR_BOTTOM(f,old));
    OK(CORNER_BOTTOM_RIGHT(f,trial));
    OK(CORNER_BOTTOM_RIGHT(f,again));
    CHECK(old.pixels && trial.pixels && again.pixels &&
          old.size==trial.size && trial.size==again.size &&
          memcmp(trial.pixels,again.pixels,trial.size)==0);
    const double dx=32.5/33.0,dy=24.75/25.0;
    const int64_t right_q=(int64_t)((float)(15.0/dx)*256.0f);
    const int64_t bottom_q=(int64_t)((float)((double)14.9f/dy)*256.0f);
    const int64_t x0=15*256,y0=15*256;
    CHECK(right_q>x0 && right_q<x0+256 &&
          bottom_q>y0 && bottom_q<y0+256);
    const unsigned expected=(unsigned)(
        ((right_q-x0)*(bottom_q-y0)+128)/256);
    CHECK(expected>0u && expected<255u);
    const size_t site=15u*trial.stride+15u*4u;
    CHECK(old.pixels[site]==0u && old.pixels[site+1u]==0u &&
          old.pixels[site+2u]==0u && old.pixels[site+3u]==0u);
    CHECK(trial.pixels[site]==255u && trial.pixels[site+1u]==0u &&
          trial.pixels[site+2u]==0u &&
          trial.pixels[site+3u]==expected);
    CHECK(alpha(&old,6u,15u)==alpha(&trial,6u,15u));
    CHECK(alpha(&old,14u,15u)==alpha(&trial,14u,15u));
    CHECK(alpha(&old,15u,2u)==alpha(&trial,15u,2u));
    size_t changes=0u;
    for(size_t y=0u;y<trial.height;++y)
        for(size_t x=0u;x<trial.width;++x) {
            const size_t off=y*trial.stride+x*4u;
            if(memcmp(old.pixels+off,trial.pixels+off,4u)!=0) {
                CHECK(x==15u && y==15u);
                ++changes;
            }
        }
    CHECK(changes==1u);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);

    /* The old mode10 source-edge budget (13*13200) is admitted, while
     * the new 14-edge mode11 budget exceeds 179K before allocation. */
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.grid_budget.max_sample_tests=179000u;
    f.raster_budget.max_sample_tests=179000u;
    OK(NEAR_BOTTOM(f,old));
    quantapdf_ir_raster_image_dispose(&old);
    REFUSE(CORNER_BOTTOM_RIGHT(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    REFUSE(CORNER_BOTTOM_RIGHT(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.op.value.path.options.transform.a=2.0f;
    REFUSE(CORNER_BOTTOM_RIGHT(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.mask[2].point1.x=21.0f;
    REFUSE(CORNER_BOTTOM_RIGHT(f,result));

    /* An exactly device-aligned bottom/right is not a fractional
     * source corner and must never be fabricated by Q8 rounding. */
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,15.0f);
    f.geometry.dpi=96.0f;
    f.grid_budget.max_pixels=2000u;
    f.grid_budget.max_rgba_bytes=8000u;
    f.grid_budget.max_sample_tests=900000u;
    f.raster_budget.max_pixels=2000u;
    f.raster_budget.max_sample_tests=900000u;
    REFUSE(CORNER_BOTTOM_RIGHT(f,result));
    return EXIT_SUCCESS;
}


/* TWELFTH independent true Clip-left/top source corner: validate the
 * previously qualified continuous-X 255-scale coverage times Y Q8
 * coverage, without PDFium data or user-specific source positions. */
static int test_clip_left_top_q8_corner(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},trial={0},repeat={0};
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    OK(CORNER_BOTTOM_RIGHT(f,old));
    OK(CORNER_CLIP_LEFT_TOP(f,trial));
    OK(CORNER_CLIP_LEFT_TOP(f,repeat));
    CHECK(old.pixels && trial.pixels && repeat.pixels &&
          old.size==trial.size && trial.size==repeat.size &&
          memcmp(trial.pixels,repeat.pixels,trial.size)==0);
    const double dx=32.5/33.0,dy=24.75/25.0;
    const size_t col=5u,row=2u;
    const double x0=(double)col*dx,x1=((double)col+1.0)*dx;
    const double y0=(double)row*dy,y1=((double)row+1.0)*dy;
    CHECK(x0<5.0 && 5.0<x1 && y0<2.0 && 2.0<y1);
    const unsigned continuous_x_alpha=(unsigned)floor(
        255.0*(x1-5.0)/(x1-x0)+0.5);
    const int64_t y_edge_q=(int64_t)((float)(2.0/dy)*256.0f);
    const int64_t q8_y=(int64_t)(row+1u)*256-y_edge_q;
    CHECK(continuous_x_alpha>0u && continuous_x_alpha<255u &&
          q8_y>0 && q8_y<=256);
    const unsigned expected=(unsigned)(
        (continuous_x_alpha*(unsigned)q8_y+128u)/256u);
    CHECK(expected>0u && expected<255u);
    const size_t idx=row*trial.stride+col*4u;
    CHECK(old.pixels[idx+3u]!=expected);
    CHECK(trial.pixels[idx]==255u && trial.pixels[idx+1u]==0u &&
          trial.pixels[idx+2u]==0u && trial.pixels[idx+3u]==expected);
    CHECK(alpha(&old,15u,2u)==alpha(&trial,15u,2u));
    CHECK(alpha(&old,6u,15u)==alpha(&trial,6u,15u));
    CHECK(alpha(&old,15u,15u)==alpha(&trial,15u,15u));
    size_t changes=0u;
    for(size_t y=0u;y<trial.height;++y)
        for(size_t x=0u;x<trial.width;++x) {
            const size_t off=y*trial.stride+x*4u;
            CHECK(memcmp(old.pixels+off,trial.pixels+off,3u)==0);
            if(old.pixels[off+3u]!=trial.pixels[off+3u]) {
                CHECK(x==col && y==row);
                ++changes;
            }
        }
    CHECK(changes==1u);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);

    /* Source work: old 14*13200=184800 admitted, new 15*13200=198000
     * refused before an output is published under the 190K quota. */
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.grid_budget.max_sample_tests=190000u;
    f.raster_budget.max_sample_tests=190000u;
    OK(CORNER_BOTTOM_RIGHT(f,old));
    quantapdf_ir_raster_image_dispose(&old);
    REFUSE(CORNER_CLIP_LEFT_TOP(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    REFUSE(CORNER_CLIP_LEFT_TOP(f,result));
    setup(&f);
    rect(f.fill,0.0f,0.0f,15.0f,14.9f);
    f.op.value.path.options.transform.a=2.0f;
    REFUSE(CORNER_CLIP_LEFT_TOP(f,result));
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
    CHECK(test_bottom_faint_fringe()==EXIT_SUCCESS);
    CHECK(test_bottom_retraces_and_refusal()==EXIT_SUCCESS);
    CHECK(test_upper_right_q8_corner()==EXIT_SUCCESS);
    CHECK(test_source_bottom_near_edges()==EXIT_SUCCESS);
    CHECK(test_bottom_right_q8_tiny_corner()==EXIT_SUCCESS);
    CHECK(test_clip_left_top_q8_corner()==EXIT_SUCCESS);
    CHECK(test_fail_closed()==EXIT_SUCCESS);
    puts("native-rect-fill-right-private-A/B: geometry, repeat, fail-closed PASS");
    return EXIT_SUCCESS;
}
