#include "render/ir_device_polyline_stroke_v0.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); \
    return EXIT_FAILURE; } } while (0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)
#define BAD(x) CHECK((x)==QUANTAPDF_ERROR_UNSUPPORTED)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_path_command cmds[5];
    quantapdf_native_device_grid_geometry geom;
    quantapdf_native_device_grid_limits grid_budget;
    quantapdf_ir_raster_v1_limits raster_budget;
} fixture;

static uint32_t pixel(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|(uint32_t)p[3];
}
static unsigned alpha(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    return im->pixels[y*im->stride+x*4u+3u];
}
static void setup(fixture *f)
{
    memset(f,0,sizeof(*f));
    f->geom=(quantapdf_native_device_grid_geometry){
        32.5,24.75,0.0,0.0,1.0,0,0,72.0f
    };
    f->grid_budget=(quantapdf_native_device_grid_limits){
        1200u,4800u,64u,2000000u
    };
    f->raster_budget=(quantapdf_ir_raster_v1_limits){
        1200u,1u,64u,2000000u
    };
    f->page.width_points=32.5f;
    f->page.height_points=24.75f;
    f->page.suppress_background=1;
    f->ir.pages=&f->page;
    f->ir.page_count=1u;
    f->ir.operations=&f->op;
    f->ir.operation_count=1u;
    f->op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->op.value.path.commands=f->cmds;
    f->op.value.path.command_count=3u;
    f->op.value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->op.value.path.options.stroke=1;
    f->op.value.path.options.fill=0;
    f->op.value.path.options.stroke_argb=UINT32_C(0xffbf4010);
    f->op.value.path.options.stroke_width=4.0f;
    f->op.value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    f->op.value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    f->op.value.path.options.miter_limit=10.0f;
    f->op.value.path.options.transform=
        (quantapdf_affine_transform){1,0,0,1,0,0};
    f->cmds[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    f->cmds[0].point1=(quantapdf_point){2.0f,8.0f};
    f->cmds[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f->cmds[1].point1=(quantapdf_point){20.0f,8.0f};
    f->cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f->cmds[2].point1=(quantapdf_point){20.0f,20.0f};
}
#define RENDER(f,image) quantapdf_ir_raster_device_polyline_stroke_v0(\
    &(f).ir,0u,&(f).geom,&(f).grid_budget,&(f).raster_budget,&(image))

static int test_miter_and_butt_and_repeat(void)
{
    fixture f;
    quantapdf_ir_raster_image miter={0},repeat={0},bevel={0};
    quantapdf_composer_path_command original[5];
    quantapdf_composer_path_options original_opt;
    setup(&f);
    memcpy(original,f.cmds,sizeof(original));
    original_opt=f.op.value.path.options;
    OK(RENDER(f,miter));
    CHECK(miter.width==33u && miter.height==25u && miter.stride==132u);
    CHECK(miter.size==3300u);
    CHECK(pixel(&miter,10u,8u)==UINT32_C(0xbf4010ff)); /* horizontal */
    CHECK(pixel(&miter,20u,12u)==UINT32_C(0xbf4010ff)); /* vertical */
    CHECK(alpha(&miter,0u,8u)==0u); /* butt end does not extend backward */
    CHECK(alpha(&miter,25u,8u)==0u);
    CHECK(alpha(&miter,21u,6u)==255u); /* outer miter wedge */
    CHECK(alpha(&miter,22u,6u)>0u && alpha(&miter,22u,6u)<255u);
    OK(RENDER(f,repeat));
    CHECK(repeat.size==miter.size &&
          memcmp(repeat.pixels,miter.pixels,miter.size)==0);
    CHECK(memcmp(f.cmds,original,sizeof(original))==0);
    CHECK(memcmp(&f.op.value.path.options,&original_opt,sizeof(original_opt))==0);
    quantapdf_ir_raster_image_dispose(&repeat);

    /* Exact PDF source-style rebase (x,-y) + (0,page_height) must be
     * byte-identical to already-normalized coordinates, without editing
     * either source IR or the device grid. */
    for (size_t i=0u;i<f.op.value.path.command_count;++i)
        f.cmds[i].point1.y -= 24.75f;
    f.op.value.path.options.transform.f=24.75f;
    OK(RENDER(f,repeat));
    CHECK(repeat.size==miter.size &&
          memcmp(repeat.pixels,miter.pixels,miter.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    for (size_t i=0u;i<f.op.value.path.command_count;++i)
        f.cmds[i].point1.y += 24.75f;
    f.op.value.path.options.transform.f=0.0f;

    f.op.value.path.options.miter_limit=1.0f; /* bevel fallback */
    OK(RENDER(f,bevel));
    CHECK(alpha(&bevel,21u,6u)<alpha(&miter,21u,6u));
    CHECK(alpha(&bevel,10u,8u)==255u);
    quantapdf_ir_raster_image_dispose(&bevel);

    /* Repeated overlapping segments within one source Stroke are a SET,
     * not repeated source-over alpha; retrace the same L path. */
    f.op.value.path.options.miter_limit=10.0f;
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.cmds[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[4].point1=(quantapdf_point){2.0f,8.0f};
    f.op.value.path.command_count=5u;
    OK(RENDER(f,repeat));
    CHECK(repeat.size==miter.size);
    CHECK(memcmp(repeat.pixels,miter.pixels,miter.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&miter);
    return EXIT_SUCCESS;
}
/* Diagnostic only: test a narrow scan-row-fullcover *candidate* without
 * ever relaxing or replacing the existing source-width Stroke entrypoint. */
/* PDFium raw-original topology matters: a source Stroke that traces a
 * horizontal line only once is antialiased at its boundary, but repeated
 * source segments can accumulate greater coverage. This preview explicitly
 * refuses to amplify any single-pass/vertical/wide or >=2-device-row line. */
static int test_horizontal_thin_preview(void)
{
    fixture f;
    quantapdf_ir_raster_image baseline={0},trial={0},repeat={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    OK(RENDER(f,baseline));
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(baseline.size==trial.size &&
          memcmp(baseline.pixels,trial.pixels,baseline.size)==0);
    /* Unrepeated 1pt source line must retain fractional 4x4 coverage. */
    CHECK(alpha(&trial,10u,7u)>0u && alpha(&trial,10u,7u)<255u);
    CHECK(alpha(&trial,10u,8u)>0u && alpha(&trial,10u,8u)<255u);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&baseline);

    /* Same original source path, traced forward/back and forward again:
     * the strict repeated-horizontal preview may now fill intersected
     * scan rows only in the segment interior. */
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.op.value.path.command_count=4u;
    OK(RENDER(f,baseline));
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(trial.width==baseline.width && trial.height==baseline.height &&
          trial.size==baseline.size);
    CHECK(alpha(&baseline,10u,7u)>0u && alpha(&baseline,10u,7u)<255u);
    CHECK(alpha(&baseline,10u,8u)>0u && alpha(&baseline,10u,8u)<255u);
    CHECK(alpha(&trial,10u,7u)==255u && alpha(&trial,10u,8u)==255u);
    CHECK(alpha(&trial,10u,6u)==0u && alpha(&trial,10u,9u)==0u);
    CHECK(alpha(&trial,1u,7u)==alpha(&baseline,1u,7u));
    CHECK(alpha(&trial,25u,7u)==0u);
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&repeat));
    CHECK(repeat.size==trial.size &&
          memcmp(repeat.pixels,trial.pixels,trial.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&baseline);

    /* Vertical-only 1pt source geometry is independently better aligned
     * with PDFium using the original area sampler. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
    f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
    OK(RENDER(f,baseline));
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(baseline.size==trial.size &&
          memcmp(baseline.pixels,trial.pixels,baseline.size)==0);
    quantapdf_ir_raster_image_dispose(&baseline);
    quantapdf_ir_raster_image_dispose(&trial);

    /* Wider 4pt Stroke never snaps, even if its top source segment repeats. */
    setup(&f);
    OK(RENDER(f,baseline));
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(baseline.size==trial.size &&
          memcmp(baseline.pixels,trial.pixels,baseline.size)==0);
    quantapdf_ir_raster_image_dispose(&baseline);
    quantapdf_ir_raster_image_dispose(&trial);

    /* At 144dpi a source 1pt stroke spans >=2 device rows: this limited
     * preview must not change baseline 4x4 output even if traced thrice. */
    setup(&f);
    f.geom.dpi=144.0f;
    f.grid_budget.max_pixels=4000u;
    f.grid_budget.max_rgba_bytes=16000u;
    f.raster_budget.max_pixels=4000u;
    f.op.value.path.options.stroke_width=1.0f;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.op.value.path.command_count=4u;
    OK(RENDER(f,baseline));
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(baseline.size==trial.size &&
          memcmp(baseline.pixels,trial.pixels,baseline.size)==0);
    quantapdf_ir_raster_image_dispose(&baseline);
    quantapdf_ir_raster_image_dispose(&trial);

    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.grid_budget.max_rgba_bytes=16u;
    memset(&trial,0xa5,sizeof(trial));
    CHECK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial)==
        QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(trial.pixels==NULL && trial.size==0u && trial.width==0u &&
          trial.height==0u && trial.stride==0u);
    return EXIT_SUCCESS;
}

/* A/B only: repeated 1pt horizontal source endpoints are a separate
 * hypothesis; the previous CI-qualified interior preview remains exactly
 * unchanged and no wider/vertical/single-pass line is automatically snapped. */
static int test_repeated_horizontal_endpoint_trial(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},inner={0},endpoint={0},again={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    OK(RENDER(f,old));
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&endpoint));
    CHECK(endpoint.size==old.size &&
          memcmp(old.pixels,endpoint.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&endpoint);
    quantapdf_ir_raster_image_dispose(&old);

    /* One same-Y horizontal Stroke retraced twice, including both source
     * endpoints. The existing interior preview excludes the endpoint
     * device columns, while the new trial includes *only* intersecting
     * repeated-strip columns. */
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.op.value.path.command_count=4u;
    OK(RENDER(f,old));
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&inner));
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&endpoint));
    CHECK(old.size==inner.size && old.size==endpoint.size);
    CHECK(alpha(&inner,10u,7u)==255u &&
          alpha(&endpoint,10u,7u)==255u);
    CHECK(alpha(&inner,2u,7u)<255u &&
          alpha(&endpoint,2u,7u)==255u);
    CHECK(alpha(&inner,20u,7u)<255u &&
          alpha(&endpoint,20u,7u)==255u);
    CHECK(alpha(&endpoint,1u,7u)==alpha(&old,1u,7u));
    CHECK(alpha(&endpoint,21u,7u)==alpha(&old,21u,7u));
    CHECK(alpha(&endpoint,21u,7u)==0u);
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&again));
    CHECK(endpoint.size==again.size &&
          memcmp(endpoint.pixels,again.pixels,endpoint.size)==0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&endpoint);
    quantapdf_ir_raster_image_dispose(&inner);
    quantapdf_ir_raster_image_dispose(&old);

    /* Source lines on slightly different Y coordinates may touch the
     * same device row: they must NEVER count as exact source repeats. */
    f.cmds[2].point1=(quantapdf_point){2.0f,8.25f};
    f.cmds[3].point1=(quantapdf_point){20.0f,8.25f};
    OK(RENDER(f,old));
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&endpoint));
    CHECK(endpoint.size==old.size &&
          memcmp(endpoint.pixels,old.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&endpoint);
    quantapdf_ir_raster_image_dispose(&old);

    /* 144 DPI makes a 1pt Stroke >=2 device rows: both preview modes
     * must remain byte-identical to the original 4x4 renderer. */
    setup(&f);
    f.geom.dpi=144.0f;
    f.grid_budget.max_pixels=4000u;
    f.grid_budget.max_rgba_bytes=16000u;
    f.raster_budget.max_pixels=4000u;
    f.op.value.path.options.stroke_width=1.0f;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.op.value.path.command_count=4u;
    OK(RENDER(f,old));
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&endpoint));
    CHECK(endpoint.size==old.size &&
          memcmp(endpoint.pixels,old.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&endpoint);
    quantapdf_ir_raster_image_dispose(&old);

    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.grid_budget.max_rgba_bytes=16u; /* valid argument, below output */
    memset(&endpoint,0xa5,sizeof(endpoint));
    BAD(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&endpoint));
    CHECK(endpoint.pixels==NULL && endpoint.size==0u &&
          endpoint.width==0u && endpoint.height==0u &&
          endpoint.stride==0u);
    return EXIT_SUCCESS;
}

/* Third opt-in A/B uses source topology, not original page/device literal
 * coordinates: repeated 1pt horizontal source Strip -> down-going vertical
 * join; only the outer upper-right corner receives bounded multi-pass
 * source-over, rather than the #286 opaque pixel rectangle. */
static int test_repeated_right_down_corner_trial(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},endpoints={0},corner={0},repeat={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    /* horizontal right, vertical down and up, horizontal left: two
     * repeated top segments, one genuine horizontal->vertical join. */
    f.op.value.path.command_count=5u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){20.0f,18.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.cmds[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[4].point1=(quantapdf_point){2.0f,8.0f};

    OK(RENDER(f,old));
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&endpoints));
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&corner));
    CHECK(old.size==endpoints.size && endpoints.size==corner.size);
    CHECK(alpha(&endpoints,10u,7u)==255u && alpha(&corner,10u,7u)==255u);
    CHECK(alpha(&endpoints,20u,7u)==255u);
    CHECK(alpha(&corner,20u,7u)>0u && alpha(&corner,20u,7u)<255u);
    /* The inside/lower join and the left repeated endpoint remain fully
     * opaque in both experimental modes; no global pixel shift. */
    CHECK(alpha(&corner,20u,8u)==alpha(&endpoints,20u,8u));
    CHECK(alpha(&corner,2u,7u)==alpha(&endpoints,2u,7u));
    CHECK(alpha(&corner,1u,7u)==alpha(&old,1u,7u));
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&repeat));
    CHECK(corner.size==repeat.size &&
          memcmp(corner.pixels,repeat.pixels,corner.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&old);
    quantapdf_ir_raster_image_dispose(&endpoints);
    quantapdf_ir_raster_image_dispose(&corner);

    /* Remove the perpendicular turn and keep a repeated horizontal line:
     * modes 2/3 must render the same bytes. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=4u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&endpoints));
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&corner));
    CHECK(endpoints.size==corner.size &&
          memcmp(endpoints.pixels,corner.pixels,corner.size)==0);
    quantapdf_ir_raster_image_dispose(&corner);
    quantapdf_ir_raster_image_dispose(&endpoints);

    /* The right-angle trial charges one additional bounded source-edge
     * pass. An allowance sufficient for mode 2 must still refuse mode 3
     * transactionally when it cannot fund the extra accounting. */
    /* 33*25*16 slots * (3 segments): mode2 charges 79200,
     * mode3 charges 118800; choose a VALID limit between them. */
    f.grid_budget.max_sample_tests=100000u;
    f.raster_budget.max_sample_tests=100000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&endpoints));
    quantapdf_ir_raster_image_dispose(&endpoints);
    memset(&corner,0xa5,sizeof(corner));
    BAD(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&corner));
    CHECK(corner.pixels==NULL && corner.size==0u && corner.width==0u &&
          corner.height==0u && corner.stride==0u);
    return EXIT_SUCCESS;
}


/* Fourth private hypothesis: rectangle-pixel intersection, not global
 * sampler shift. Synthetic checks across each admitted device DPI and
 * an explicit fail-closed edge-work budget independent of mode 3. */
static int test_single_vertical_analytic_trial(void)
{
    const float dpi[]={72.0f,96.0f,144.0f};
    for(size_t index=0u;index<3u;++index) {
        fixture f;
        quantapdf_ir_raster_image old={0},corner={0},trial={0},again={0};
        setup(&f);
        f.geom.dpi=dpi[index];
        f.grid_budget.max_pixels=4000u;
        f.grid_budget.max_rgba_bytes=16000u;
        f.raster_budget.max_pixels=4000u;
        f.op.value.path.options.stroke_width=1.0f;
        f.op.value.path.command_count=2u;
        f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
        f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
        OK(RENDER(f,old));
        OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&corner));
        OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
        OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&again));
        CHECK(old.size==trial.size && trial.size==again.size &&
              memcmp(trial.pixels,again.pixels,trial.size)==0 &&
              memcmp(old.pixels,corner.pixels,old.size)==0);
        const double px=f.geom.page_width_points/(double)trial.width;
        const double py=f.geom.page_height_points/(double)trial.height;
        const size_t y=(size_t)floor(10.0/py);
        size_t different=0u;
        for(size_t x=0u;x<trial.width;++x) {
            const double x0=(double)x*px,x1=((double)x+1.0)*px;
            const double common=fmax(0.0,fmin(x1,20.5)-fmax(x0,19.5));
            const unsigned expectation=(unsigned)floor(
                255.0*fmin(1.0,common/px)+0.5);
            CHECK(alpha(&trial,x,y)==expectation);
            if(alpha(&trial,x,y)!=alpha(&old,x,y))++different;
        }
        if(index<2u) CHECK(different>0u);
        else CHECK(different==0u);
        quantapdf_ir_raster_image_dispose(&again);
        quantapdf_ir_raster_image_dispose(&trial);
        quantapdf_ir_raster_image_dispose(&corner);
        quantapdf_ir_raster_image_dispose(&old);
    }
    /* Single horizontal-only Path cannot gain new vertical coverage. */
    fixture f;
    quantapdf_ir_raster_image previous={0},candidate={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&previous));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(previous.size==candidate.size &&
          memcmp(previous.pixels,candidate.pixels,previous.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&previous);


    /* The fourth trial is NOT a generic miter renderer: an unrelated
     * diagonal/acute turn in the same source path must leave all pixels
     * unchanged, even where an earlier vertical interior would otherwise
     * be eligible for the private analytic shortcut. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=3u;
    f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
    f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){23.0f,22.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&previous));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(previous.size==candidate.size &&
          memcmp(previous.pixels,candidate.pixels,previous.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&previous);

    /* Also reject repeated same-X source vertical traversals: source
     * membership is one Stroke union, not N alpha source-over passes. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=3u;
    f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
    f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){20.0f,3.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&previous));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(previous.size==candidate.size &&
          memcmp(previous.pixels,candidate.pixels,previous.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&previous);

    /* Separate fourth source-edge pass: count=1, 33x25x16 slots;
     * mode3 requires 39,600 while mode4 requires 52,800. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
    f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
    f.grid_budget.max_sample_tests=47000u;
    f.raster_budget.max_sample_tests=47000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&previous));
    quantapdf_ir_raster_image_dispose(&previous);
    memset(&candidate,0xa5,sizeof(candidate));
    BAD(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(candidate.pixels==NULL && candidate.size==0u &&
          candidate.width==0u && candidate.height==0u &&
          candidate.stride==0u);
    return EXIT_SUCCESS;
}


/* Fifth strict C11 A/B: source-space analytic horizontal width, only
 * once-per-Path and only safely inside the source segment. */
static int test_single_horizontal_analytic_trial(void)
{
    const float dpi_values[]={72.0f,96.0f,144.0f};
    for(size_t i=0u;i<3u;++i) {
        fixture f;
        quantapdf_ir_raster_image old={0},horizontal={0},repeat={0};
        setup(&f);
        f.geom.dpi=dpi_values[i];
        f.grid_budget.max_pixels=4000u;
        f.grid_budget.max_rgba_bytes=16000u;
        f.raster_budget.max_pixels=4000u;
        f.op.value.path.options.stroke_width=1.0f;
        f.op.value.path.command_count=2u;
        OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
        OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&horizontal));
        OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&repeat));
        CHECK(old.size==horizontal.size && horizontal.size==repeat.size &&
              memcmp(horizontal.pixels,repeat.pixels,repeat.size)==0);
        const double px=f.geom.page_width_points/(double)horizontal.width;
        const double py=f.geom.page_height_points/(double)horizontal.height;
        const size_t x=(size_t)floor(10.0/px);
        size_t differences=0u;
        for(size_t y=0u;y<horizontal.height;++y) {
            const double y0=(double)y*py,y1=((double)y+1.0)*py;
            const double shared=fmax(0.0,fmin(y1,8.5)-fmax(y0,7.5));
            const unsigned expected=(unsigned)floor(
                255.0*fmin(1.0,shared/py)+0.5);
            CHECK(alpha(&horizontal,x,y)==expected);
            if(alpha(&horizontal,x,y)!=alpha(&old,x,y))++differences;
        }
        CHECK(differences>0u);
        quantapdf_ir_raster_image_dispose(&repeat);
        quantapdf_ir_raster_image_dispose(&horizontal);
        quantapdf_ir_raster_image_dispose(&old);
    }
    fixture f;
    quantapdf_ir_raster_image old={0},candidate={0};
    /* Three genuine same-Y horizontal source traversals remain mode4. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=4u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(old.size==candidate.size &&
          memcmp(old.pixels,candidate.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&old);

    /* Vertical-only retains exactly the previous mode4 analytic result. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
    f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(old.size==candidate.size &&
          memcmp(old.pixels,candidate.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&old);

    /* A diagonal following a horizontal invalidates the shortcut,
     * including otherwise eligible earlier interior pixels. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=3u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){22.0f,10.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(old.size==candidate.size &&
          memcmp(old.pixels,candidate.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&old);

    /* Count=1 edge work: 33*25*16*4 = 52,800 mode4;
     * mode5 needs 66,000 slots. Reject mode5 under a valid 60,000 cap. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.grid_budget.max_sample_tests=60000u;
    f.raster_budget.max_sample_tests=60000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    quantapdf_ir_raster_image_dispose(&old);
    memset(&candidate,0xa5,sizeof(candidate));
    BAD(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(candidate.pixels==NULL && candidate.size==0u &&
          candidate.stride==0u && candidate.width==0u &&
          candidate.height==0u);
    return EXIT_SUCCESS;
}


/* SIXTH private C11 source-Stroke trial: only quantization of already
 * eligible single-pass 1pt horizontal INTERIOR changes, NEVER any cap,
 * miter, repeated top trace, diagonal or vertical coverage. */
static int test_single_horizontal_ceil_quantization_trial(void)
{
    const float dpis[]={72.0f,96.0f,144.0f};
    size_t changed_total=0u;
    for(size_t i=0u;i<3u;++i) {
        fixture f;
        quantapdf_ir_raster_image old={0},new_image={0},repeat={0};
        setup(&f);
        f.geom.dpi=dpis[i];
        f.grid_budget.max_pixels=4000u;
        f.grid_budget.max_rgba_bytes=16000u;
        f.raster_budget.max_pixels=4000u;
        f.op.value.path.options.stroke_width=1.0f;
        f.op.value.path.command_count=2u;
        OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
        OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&new_image));
        OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&repeat));
        CHECK(old.size==new_image.size && old.stride==new_image.stride &&
              new_image.size==repeat.size &&
              memcmp(new_image.pixels,repeat.pixels,repeat.size)==0);
        const double dx=f.geom.page_width_points/(double)new_image.width;
        const double dy=f.geom.page_height_points/(double)new_image.height;
        const size_t center_x=(size_t)floor(10.0/dx);
        size_t changes=0u;
        for(size_t y=0u;y<new_image.height;++y) {
            const double y0=(double)y*dy;
            const double y1=((double)y+1.0)*dy;
            const double shared=fmax(0.0,fmin(y1,8.5)-fmax(y0,7.5));
            const double coverage=fmin(1.0,shared/dy);
            const unsigned old_expected=
                (unsigned)floor(255.0*coverage+0.5);
            const unsigned new_expected=
                (unsigned)ceil(255.0*coverage);
            CHECK(alpha(&old,center_x,y)==old_expected);
            CHECK(alpha(&new_image,center_x,y)==new_expected);
            CHECK(new_expected>=old_expected &&
                  new_expected<=old_expected+1u);
        }
        for(size_t y=0u;y<new_image.height;++y)
            for(size_t x=0u;x<new_image.width;++x) {
                const size_t off=y*new_image.stride+x*4u;
                const unsigned oa=old.pixels[off+3u];
                const unsigned na=new_image.pixels[off+3u];
                CHECK(memcmp(old.pixels+off,new_image.pixels+off,3u)==0);
                CHECK(na>=oa && na<=oa+1u);
                if(na!=oa) {
                    const double x0=(double)x*dx;
                    const double x1=((double)x+1.0)*dx;
                    const double y0=(double)y*dy;
                    const double y1=((double)y+1.0)*dy;
                    const double guard=0.5+dx;
                    CHECK(x0>2.0+guard && x1<20.0-guard);
                    CHECK(y0<8.5 && y1>7.5);
                    ++changes;
                }
            }
        /* Different original grid phases can have integral coverage;
         * 96dpi legitimately has no ceil-vs-round difference here. */
        if(dpis[i]!=96.0f)CHECK(changes>0u);
        changed_total+=changes;
        quantapdf_ir_raster_image_dispose(&repeat);
        quantapdf_ir_raster_image_dispose(&new_image);
        quantapdf_ir_raster_image_dispose(&old);
    }
    CHECK(changed_total>0u);
    fixture f;
    quantapdf_ir_raster_image old={0},candidate={0};
    /* Repeated source top horizontal: NEVER ceil-paint or amplify. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=4u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(old.size==candidate.size &&
          memcmp(old.pixels,candidate.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&old);
    /* Isolated source vertical retains the exact mode5 analytic strip. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
    f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(old.size==candidate.size &&
          memcmp(old.pixels,candidate.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&old);
    /* Any diagonal edge in source Path suppresses BOTH narrow trials. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=3u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){22.0f,10.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(old.size==candidate.size &&
          memcmp(old.pixels,candidate.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&candidate);
    quantapdf_ir_raster_image_dispose(&old);
    /* Mode5 count*5 budget = 66,000 < 70,000; mode6 count*6 =
     * 79,200 > 70,000. Refusal publishes ZERO partial RGBA. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.grid_budget.max_sample_tests=70000u;
    f.raster_budget.max_sample_tests=70000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    quantapdf_ir_raster_image_dispose(&old);
    memset(&candidate,0xa5,sizeof(candidate));
    BAD(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&candidate));
    CHECK(candidate.pixels==NULL && candidate.size==0u &&
          candidate.stride==0u && candidate.width==0u &&
          candidate.height==0u);
    return EXIT_SUCCESS;
}


/* Seventh PRIVATE geometric A/B: add exactly one alpha LSB only when a
 * whole device row lies above a SINGLE source horizontal line center.
 * Preserve old round alpha below/crossing the center, all endpoints,
 * truly repeated horizontal sources, verticals and diagonal miters. */
static int test_upper_horizontal_lsb_trial(void)
{
    const float dpis[]={72.0f,96.0f,144.0f};
    size_t positives=0u;
    for(size_t i=0u;i<3u;++i) {
        fixture f;
        quantapdf_ir_raster_image old={0},trial={0},repeat={0};
        setup(&f);
        f.geom.dpi=dpis[i];
        f.grid_budget.max_pixels=4000u;
        f.grid_budget.max_rgba_bytes=16000u;
        f.raster_budget.max_pixels=4000u;
        f.op.value.path.options.stroke_width=1.0f;
        f.op.value.path.command_count=2u;
        OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
        OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
        OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&repeat));
        CHECK(old.size==trial.size && trial.size==repeat.size &&
              memcmp(trial.pixels,repeat.pixels,trial.size)==0);
        const double dx=f.geom.page_width_points/(double)trial.width;
        const double dy=f.geom.page_height_points/(double)trial.height;
        const size_t center_x=(size_t)floor(10.0/dx);
        size_t changes=0u;
        for(size_t y=0u;y<trial.height;++y) {
            const double y0=(double)y*dy;
            const double y1=((double)y+1.0)*dy;
            const double common=fmax(0.0,fmin(y1,8.5)-fmax(y0,7.5));
            const double cover=fmin(1.0,common/dy);
            const unsigned rounded=(unsigned)floor(255.0*cover+0.5);
            const unsigned expected=(y1<=8.0 && rounded>0u &&
                rounded<255u)?rounded+1u:rounded;
            CHECK(alpha(&old,center_x,y)==rounded);
            CHECK(alpha(&trial,center_x,y)==expected);
        }
        for(size_t y=0u;y<trial.height;++y)
            for(size_t x=0u;x<trial.width;++x) {
                const size_t off=y*trial.stride+x*4u;
                const unsigned oa=old.pixels[off+3u];
                const unsigned na=trial.pixels[off+3u];
                CHECK(memcmp(old.pixels+off,trial.pixels+off,3u)==0);
                CHECK(na>=oa && na<=oa+1u);
                if(na!=oa) {
                    const double x0=(double)x*dx;
                    const double x1=((double)x+1.0)*dx;
                    const double y0=(double)y*dy;
                    const double y1=((double)y+1.0)*dy;
                    const double guard=0.5+dx;
                    CHECK(x0>2.0+guard && x1<20.0-guard);
                    CHECK(y0<8.0 && y1<=8.0);
                    CHECK(oa>0u && oa<255u);
                    ++changes;
                }
            }
        if(dpis[i]!=96.0f)CHECK(changes>0u);
        positives+=changes;
        quantapdf_ir_raster_image_dispose(&repeat);
        quantapdf_ir_raster_image_dispose(&trial);
        quantapdf_ir_raster_image_dispose(&old);
    }
    CHECK(positives>0u);
    fixture f;
    quantapdf_ir_raster_image old={0},trial={0};
    /* Retain mode5 bytes on genuine repeated source horizontal traces. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=4u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(old.size==trial.size &&
          memcmp(old.pixels,trial.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);
    /* Vertical source must never enter the top-row horizontal policy. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
    f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(old.size==trial.size &&
          memcmp(old.pixels,trial.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);
    /* A diagonal source edge could extend a long acute miter. Refuse
     * the analytic shortcut throughout that source Path. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=3u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){22.0f,10.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(old.size==trial.size &&
          memcmp(old.pixels,trial.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);
    /* Separate resource admission: count6*13200=79,200 is allowed,
     * but mode7 count7*13200=92,400 exceeds the same 85,000 budget.
     * There must be no partial RGBA on any refusal. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.grid_budget.max_sample_tests=85000u;
    f.raster_budget.max_sample_tests=85000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    quantapdf_ir_raster_image_dispose(&old);
    memset(&trial,0xa5,sizeof(trial));
    BAD(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(trial.pixels==NULL && trial.size==0u &&
          trial.stride==0u && trial.width==0u &&
          trial.height==0u);
    return EXIT_SUCCESS;
}


/* Eighth independent private C11 quantized-edge trial. The alpha oracle
 * here is derived only from source geometry and Q8 device-cell arithmetic,
 * never from a PDFium image or hard-coded source PDF coordinates. */
static int test_device_q8_strip_trial(void)
{
    const float dpis[]={72.0f,96.0f,144.0f};
    size_t total_changes=0u;
    for(size_t i=0u;i<3u;++i) {
        fixture f;
        quantapdf_ir_raster_image previous={0},q8={0},repeat={0};
        setup(&f);
        f.geom.dpi=dpis[i];
        f.grid_budget.max_pixels=4000u;
        f.grid_budget.max_rgba_bytes=16000u;
        f.raster_budget.max_pixels=4000u;
        f.op.value.path.options.stroke_width=1.0f;
        f.op.value.path.command_count=2u;
        OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&previous));
        OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&q8));
        OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
            &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&repeat));
        CHECK(q8.size==repeat.size && q8.size==previous.size &&
              memcmp(q8.pixels,repeat.pixels,q8.size)==0);
        const double dx=f.geom.page_width_points/(double)q8.width;
        const double dy=f.geom.page_height_points/(double)q8.height;
        const size_t cx=(size_t)floor(10.0/dx);
        const float devlo=(float)(7.5/dy),devhi=(float)(8.5/dy);
        const int64_t qlo=(int64_t)(devlo*256.0f);
        const int64_t qhi=(int64_t)(devhi*256.0f);
        for(size_t y=0u;y<q8.height;++y) {
            const int64_t first=(int64_t)y*256;
            const int64_t last=first+256;
            const int64_t low=qlo>first?qlo:first;
            const int64_t high=qhi<last?qhi:last;
            const int64_t span=high>low?high-low:0;
            const unsigned expected=(unsigned)(span>=255?255:span);
            CHECK(alpha(&q8,cx,y)==expected);
        }
        for(size_t y=0u;y<q8.height;++y)for(size_t x=0u;x<q8.width;++x) {
            const size_t off=y*q8.stride+x*4u;
            CHECK(memcmp(previous.pixels+off,q8.pixels+off,3u)==0);
            if(previous.pixels[off+3u]!=q8.pixels[off+3u]) {
                const double x0=(double)x*dx,x1=((double)x+1.0)*dx;
                const double y0=(double)y*dy,y1=((double)y+1.0)*dy;
                const double guard=0.5+dx;
                CHECK(x0>2.0+guard && x1<20.0-guard);
                CHECK(y0<8.5 && y1>7.5);
                ++total_changes;
            }
        }
        quantapdf_ir_raster_image_dispose(&repeat);
        quantapdf_ir_raster_image_dispose(&q8);
        quantapdf_ir_raster_image_dispose(&previous);
    }
    CHECK(total_changes>0u);
    fixture f;
    quantapdf_ir_raster_image old={0},trial={0};
    /* Single-pass vertical source strip: x direction is quantized Q8. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.cmds[0].point1=(quantapdf_point){20.0f,3.0f};
    f.cmds[1].point1=(quantapdf_point){20.0f,20.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    const double dx=f.geom.page_width_points/(double)trial.width;
    const double dy=f.geom.page_height_points/(double)trial.height;
    const size_t cy=(size_t)floor(12.0/dy);
    const int64_t qleft=(int64_t)((float)(19.5/dx)*256.0f);
    const int64_t qright=(int64_t)((float)(20.5/dx)*256.0f);
    for(size_t x=0u;x<trial.width;++x) {
        const int64_t low=(int64_t)x*256,high=low+256;
        const int64_t a=qleft>low?qleft:low;
        const int64_t b=qright<high?qright:high;
        const int64_t span=b>a?b-a:0;
        CHECK(alpha(&trial,x,cy)==(unsigned)(span>=255?255:span));
    }
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);
    /* Repeated source horizontal and any diagonal retain old mode7 bytes. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=4u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){2.0f,8.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(old.size==trial.size && memcmp(old.pixels,trial.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=3u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){22.0f,10.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(old.size==trial.size && memcmp(old.pixels,trial.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);
    /* count7*13,200=92,400 is admitted; count8*13,200=105,600 is
     * refused under 100,000 slots, with ZERO partial output. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.grid_budget.max_sample_tests=100000u;
    f.raster_budget.max_sample_tests=100000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    quantapdf_ir_raster_image_dispose(&old);
    memset(&trial,0xa5,sizeof(trial));
    BAD(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(trial.pixels==NULL && trial.size==0u &&
          trial.stride==0u && trial.width==0u && trial.height==0u);
    return EXIT_SUCCESS;
}


/* Ninth strictly private orthogonal miter join trial. Q8 gives exact
 * axis-strip interiors; this A/B changes ONLY an isolated connected
 * right-angle vertex, not a cap, retrace or source coordinate fixture. */
static int test_q8_inner_bevel_miter_join_trial(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},new_image={0},repeat={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=3u;
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&new_image));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&repeat));
    CHECK(old.pixels && new_image.pixels && repeat.pixels &&
          old.size==new_image.size && old.size==repeat.size &&
          memcmp(new_image.pixels,repeat.pixels,new_image.size)==0);
    size_t changed=0u,changed_outside=0u;
    const double dx=f.geom.page_width_points/(double)new_image.width;
    const double dy=f.geom.page_height_points/(double)new_image.height;
    for(size_t y=0u;y<new_image.height;++y)
        for(size_t x=0u;x<new_image.width;++x) {
            const size_t off=y*new_image.stride+x*4u;
            const unsigned prior=old.pixels[off+3u];
            const unsigned trial=new_image.pixels[off+3u];
            CHECK(memcmp(old.pixels+off,new_image.pixels+off,3u)==0);
            if(prior==trial)continue;
            ++changed;
            const double px0=(double)x*dx,px1=((double)x+1.0)*dx;
            const double py0=(double)y*dy,py1=((double)y+1.0)*dy;
            if(!(px0<20.5 && px1>19.5 && py0<8.5 && py1>7.5))
                ++changed_outside;
        }
    CHECK(changed>0u && changed_outside==0u);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&new_image);
    quantapdf_ir_raster_image_dispose(&old);

    /* Explicit reverse/retrace: a THIRD expanded edge meets the joint,
     * thus no local Q8 miter correction is authorized. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=5u;
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.cmds[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[4].point1=(quantapdf_point){2.0f,8.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&new_image));
    CHECK(old.size==new_image.size &&
          memcmp(old.pixels,new_image.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&new_image);
    quantapdf_ir_raster_image_dispose(&old);

    /* No geometry shortcut when source has a diagonal or insufficient
     * outer miter limit; previous eighth preview remains byte-exact. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.cmds[2].point1=(quantapdf_point){23.0f,20.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&new_image));
    CHECK(old.size==new_image.size &&
          memcmp(old.pixels,new_image.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&new_image);
    quantapdf_ir_raster_image_dispose(&old);
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.options.miter_limit=1.0f;
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&new_image));
    CHECK(old.size==new_image.size &&
          memcmp(old.pixels,new_image.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&new_image);
    quantapdf_ir_raster_image_dispose(&old);

    /* Same 825*16 sample grid: mode8 count*8=211200 admitted,
     * mode9 count*9=237600 refused under 220000 with zero output. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.grid_budget.max_sample_tests=220000u;
    f.raster_budget.max_sample_tests=220000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    quantapdf_ir_raster_image_dispose(&old);
    memset(&new_image,0xa5,sizeof(new_image));
    BAD(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&new_image));
    CHECK(new_image.pixels==NULL && new_image.size==0u &&
          new_image.stride==0u && new_image.width==0u &&
          new_image.height==0u);
    return EXIT_SUCCESS;
}


/* Tenth independent private cell-extent A/B: same Q8 source join shape as
 * mode9, with additional bounded adjacent device cells. No source
 * position magic or PDFium oracle in this pure-C11 geometry test. */
static int test_q8_miter_adjacent_device_cells_trial(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},trial={0},repeat={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=3u;
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_miter_adjacent_cells_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_miter_adjacent_cells_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&repeat));
    CHECK(old.pixels && trial.pixels && repeat.pixels &&
          old.size==trial.size && trial.size==repeat.size &&
          memcmp(trial.pixels,repeat.pixels,trial.size)==0);
    const double dx=f.geom.page_width_points/(double)trial.width;
    const double dy=f.geom.page_height_points/(double)trial.height;
    size_t changed=0u,escaped=0u;
    for(size_t y=0u;y<trial.height;++y)
        for(size_t x=0u;x<trial.width;++x) {
            const size_t off=y*trial.stride+x*4u;
            CHECK(memcmp(old.pixels+off,trial.pixels+off,3u)==0);
            if(old.pixels[off+3u]!=trial.pixels[off+3u]) {
                const double x0=(double)x*dx,x1=((double)x+1.0)*dx;
                const double y0=(double)y*dy,y1=((double)y+1.0)*dy;
                ++changed;
                if(!(x0<20.5+2.0*dx && x1>19.5-2.0*dx &&
                     y0<8.5+dy && y1>7.5-dy))
                    ++escaped;
            }
        }
    CHECK(changed>0u && escaped==0u);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);

    /* Repeated orthogonal retrace has ambiguous third coverage; widening
     * device cells must NEVER bypass mode9's source-overlap exclusion. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=5u;
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.cmds[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[4].point1=(quantapdf_point){2.0f,8.0f};
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_miter_adjacent_cells_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(old.size==trial.size && memcmp(old.pixels,trial.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);

    /* Explicit additional bounded work: mode9 count*9*13200=237600
     * is allowed; mode10 count*10*13200=264000 refused with ZERO output. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.grid_budget.max_sample_tests=250000u;
    f.raster_budget.max_sample_tests=250000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    quantapdf_ir_raster_image_dispose(&old);
    memset(&trial,0xa5,sizeof(trial));
    BAD(quantapdf_ir_raster_device_polyline_stroke_q8_miter_adjacent_cells_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(trial.pixels==NULL && trial.size==0u &&
          trial.width==0u && trial.height==0u && trial.stride==0u);
    return EXIT_SUCCESS;
}


/* Eleventh source-topology A/B. A truly retraced horizontal source edge
 * may be ignored as a third distinct contour only at its connected
 * orthogonal Miter; another diagonal or unrelated overlap cannot. */
static int test_retraced_top_q8_miter_trial(void)
{
    fixture f;
    quantapdf_ir_raster_image old={0},trial={0},again={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=5u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){20.0f,20.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){2.0f,20.0f};
    f.cmds[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[4].point1=(quantapdf_point){2.0f,8.0f};
    /* No top repeated horizontal: mode11 must preserve mode10. */
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_miter_adjacent_cells_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_retraced_top_miter_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(old.size==trial.size &&
          memcmp(old.pixels,trial.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);

    /* A genuine retrace of the top source line: retain exact RGB,
     * deterministic repeat and restrict changed alpha near top joins. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=5u;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1=(quantapdf_point){20.0f,20.0f};
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){2.0f,8.0f};
    /* Diagonal controls MUST retain mode10; do not admit it as Miter. */
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_miter_adjacent_cells_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_retraced_top_miter_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(old.size==trial.size &&
          memcmp(old.pixels,trial.pixels,old.size)==0);
    quantapdf_ir_raster_image_dispose(&trial);
    quantapdf_ir_raster_image_dispose(&old);

    /* Source Budget: 2 edges * 10 passes admitted, *11 rejected. */
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.command_count=2u;
    f.grid_budget.max_sample_tests=135000u;
    f.raster_budget.max_sample_tests=135000u;
    OK(quantapdf_ir_raster_device_polyline_stroke_q8_miter_adjacent_cells_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&old));
    quantapdf_ir_raster_image_dispose(&old);
    memset(&trial,0xa5,sizeof(trial));
    BAD(quantapdf_ir_raster_device_polyline_stroke_q8_retraced_top_miter_preview_v0(
        &f.ir,0u,&f.geom,&f.grid_budget,&f.raster_budget,&trial));
    CHECK(trial.pixels==NULL && trial.size==0u &&
          trial.width==0u && trial.height==0u && trial.stride==0u);
    return EXIT_SUCCESS;
}

static int test_pixel_width_and_rejections(void)
{
    fixture f;
    quantapdf_ir_raster_image im={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    OK(RENDER(f,im));
    CHECK(alpha(&im,10u,8u)>0u && alpha(&im,10u,8u)<255u);
    CHECK(alpha(&im,10u,6u)==0u);
    quantapdf_ir_raster_image_dispose(&im);
#define REFUSE(call) do{ \
    memset(&im,0xa5,sizeof(im)); \
    BAD(call); \
    CHECK(im.pixels==NULL&&im.width==0u&&im.height==0u&&im.size==0u&&im.stride==0u); \
}while(0)
    f.op.value.path.options.stroke_width=0.0f;
    REFUSE(RENDER(f,im)); f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    REFUSE(RENDER(f,im)); f.op.value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    f.op.value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL;
    REFUSE(RENDER(f,im)); f.op.value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    f.op.value.path.options.fill=1;
    REFUSE(RENDER(f,im)); f.op.value.path.options.fill=0;
    f.op.value.path.options.transform.a=2.0f;
    REFUSE(RENDER(f,im)); f.op.value.path.options.transform.a=1.0f;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    REFUSE(RENDER(f,im)); f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1.x=33.0f;
    REFUSE(RENDER(f,im)); f.cmds[2].point1.x=20.0f;
    f.op.value.path.options.stroke_argb=UINT32_C(0x80bf4010);
    REFUSE(RENDER(f,im)); f.op.value.path.options.stroke_argb=UINT32_C(0xffbf4010);
    f.geom.user_unit=2.0;
    REFUSE(RENDER(f,im)); f.geom.user_unit=1.0;
    f.geom.page_width_points=32.000001;
    REFUSE(RENDER(f,im)); f.geom.page_width_points=32.5;
    f.raster_budget.max_edges=2u; /* 2*N edge+join accounting */
    REFUSE(RENDER(f,im)); f.raster_budget.max_edges=64u;
    f.grid_budget.max_sample_tests=30000u;
    REFUSE(RENDER(f,im)); f.grid_budget.max_sample_tests=2000000u;
    f.grid_budget.max_rgba_bytes=3299u;
    REFUSE(RENDER(f,im)); f.grid_budget.max_rgba_bytes=4800u;
#undef REFUSE
    return EXIT_SUCCESS;
}
int main(void)
{
    CHECK(test_miter_and_butt_and_repeat()==EXIT_SUCCESS);
    CHECK(test_horizontal_thin_preview()==EXIT_SUCCESS);
    CHECK(test_repeated_horizontal_endpoint_trial()==EXIT_SUCCESS);
    CHECK(test_repeated_right_down_corner_trial()==EXIT_SUCCESS);
    CHECK(test_single_vertical_analytic_trial()==EXIT_SUCCESS);
    CHECK(test_single_horizontal_analytic_trial()==EXIT_SUCCESS);
    CHECK(test_single_horizontal_ceil_quantization_trial()==EXIT_SUCCESS);
    CHECK(test_upper_horizontal_lsb_trial()==EXIT_SUCCESS);
    CHECK(test_device_q8_strip_trial()==EXIT_SUCCESS);
    CHECK(test_q8_inner_bevel_miter_join_trial()==EXIT_SUCCESS);
    CHECK(test_q8_miter_adjacent_device_cells_trial()==EXIT_SUCCESS);
    CHECK(test_retraced_top_q8_miter_trial()==EXIT_SUCCESS);
    CHECK(test_pixel_width_and_rejections()==EXIT_SUCCESS);
    puts("native-device-polyline-stroke: butt+miter+bevel+union+limits PASS");
    return EXIT_SUCCESS;
}
