#include "render/native_device_ir_v1.h"
#include "backend/native_prepared_page_v1.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"device IR at %d: %s\n",__LINE__,#x); return 1; } }while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

static uint32_t at(const quantapdf_ir_raster_image *im,size_t x,size_t y) {
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|((uint32_t)p[2]<<8u)|p[3];
}
static int denied(const quantapdf_ir *ir,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *lim) {
    quantapdf_ir_raster_image out;
    memset(&out,0xa5,sizeof(out));
    CHECK(quantapdf_native_device_ir_render_v1(ir,dpi,lim,&out)
          ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(out.pixels==NULL && out.size==0u && out.width==0u);
    return 0;
}
static int prepared_denied(const quantapdf_ir *ir,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device)
{
    quantapdf_native_prepared_page_v1 plan,zero;
    memset(&plan,0xa5,sizeof(plan));memset(&zero,0,sizeof(zero));
    CHECK(quantapdf_native_prepare_page_v1(ir,&limits->raster,device,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&plan)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(memcmp(&plan,&zero,sizeof(plan))==0);
    return 0;
}
static int prepared_contracts(quantapdf_ir *ir,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *raster)
{
    quantapdf_native_source_raster_bridge_limits_v1 limits;
    quantapdf_native_source_raster_device_v1 device;
    quantapdf_native_prepared_page_v1 plan,before;
    quantapdf_native_execution_receipt_v1 receipt;
    quantapdf_ir_raster_image a,b;
    quantapdf_resource_registry saved=ir->resources;
    quantapdf_composer_operation old_op=ir->operations[0];
    quantapdf_composer_clip_state clips[3];
    quantapdf_composer_graphics_state gs[2];
    quantapdf_composer_path_command geometry[2][5];
    quantapdf_composer_clip_id members[2]={1u,2u};
    memset(&limits,0,sizeof(limits));memset(&device,0,sizeof(device));
    limits.raster=*raster;device.dpi=96.0f;
    for(unsigned dpi=72u;dpi<=144u;dpi+=dpi==72u?24u:48u) {
        device.dpi=(float)dpi;
        OK(quantapdf_native_prepare_page_v1(ir,&limits.raster,&device,
            QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&plan));
        CHECK(plan.source==ir && plan.route==QP_NATIVE_PREPARED_DEVICE &&
              plan.usage.operations==1u && plan.usage.commands==5u &&
              plan.usage.expanded_paints==1u && plan.usage.glyphs==0u &&
              plan.usage.retained_bytes>0u);
        memcpy(&before,&plan,sizeof(plan));
        /* The caller's later limit edits cannot alter this render envelope. */
        limits.raster.max_sample_tests=0u;
        memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));memset(&receipt,0xa5,sizeof(receipt));
        OK(quantapdf_native_execute_page_v1(&plan,&receipt,&a));
        CHECK(receipt.sample_tests==0u && receipt.sample_tests_exact==0);
        CHECK(receipt.charged_sample_work>0u &&
              receipt.charged_sample_work<=raster->max_sample_tests &&
              receipt.compiled_edges>0u && receipt.compiled_edges<=raster->max_edges &&
              receipt.peak_reserved_sample_work==raster->max_sample_tests &&
              receipt.peak_reserved_edges==raster->max_edges && receipt.completed_leases==1u);
        limits.raster=*raster;
        OK(quantapdf_native_device_ir_render_v1(ir,dpi,raster,&b));
        CHECK(a.size==b.size && memcmp(a.pixels,b.pixels,a.size)==0 &&
              memcmp(&plan,&before,sizeof(plan))==0);
        quantapdf_ir_raster_image_dispose(&a);quantapdf_ir_raster_image_dispose(&b);
    }
    device.dpi=96.0f;
    ir->resources.fonts=(quantapdf_composer_font_state *)(uintptr_t)1u;
    CHECK(prepared_denied(ir,&limits,&device)==0);ir->resources=saved;
    ir->resources.image_count=SIZE_MAX;ir->resources.image_capacity=SIZE_MAX;
    CHECK(prepared_denied(ir,&limits,&device)==0);ir->resources=saved;
    ir->operations[0].page_index=1u;
    CHECK(prepared_denied(ir,&limits,&device)==0);ir->operations[0]=old_op;
    ir->operations[0].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(prepared_denied(ir,&limits,&device)==0);ir->operations[0]=old_op;
    ir->operations[0].value.path.command_count=SIZE_MAX;
    CHECK(prepared_denied(ir,&limits,&device)==0);ir->operations[0]=old_op;
    limits.raster.max_operations=0u;
    CHECK(prepared_denied(ir,&limits,&device)==0);limits.raster=*raster;
    device.dpi=73.0f;CHECK(prepared_denied(ir,&limits,&device)==0);device.dpi=96.0f;

    memset(clips,0,sizeof(clips));memset(gs,0,sizeof(gs));
    for(size_t i=0u;i<2u;++i) {
        memcpy(geometry[i],old_op.value.path.commands,sizeof(geometry[i]));
        clips[i].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clips[i].commands=geometry[i];clips[i].command_count=5u;
        clips[i].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        clips[i].transform.a=clips[i].transform.d=1.0f;
    }
    clips[2].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    clips[2].members=members;clips[2].member_count=2u;
    gs[0].fill_alpha=gs[0].stroke_alpha=1.0f;
    gs[0].blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;gs[0].clip_id=3u;gs[1]=gs[0];
    ir->resources.clips=clips;ir->resources.clip_count=ir->resources.clip_capacity=3u;
    ir->resources.graphics_states=gs;
    ir->resources.graphics_state_count=ir->resources.graphics_state_capacity=1u;
    ir->operations[0].graphics_state_id=1u;
    ir->operations[0].value.path.options.graphics_state_id=1u;
    OK(quantapdf_native_prepare_page_v1(ir,&limits.raster,&device,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&plan));
    CHECK(plan.route==QP_NATIVE_PREPARED_CLIPPED_PATHS &&
          plan.usage.clips_used==7u && plan.usage.states_used==1u &&
          plan.usage.commands==15u);
    members[1]=3u;CHECK(prepared_denied(ir,&limits,&device)==0);members[1]=2u;
    members[0]=0u;CHECK(prepared_denied(ir,&limits,&device)==0);members[0]=1u;
    gs[0].clip_id=4u;CHECK(prepared_denied(ir,&limits,&device)==0);gs[0].clip_id=3u;
    ir->resources.clip_capacity=4u;CHECK(prepared_denied(ir,&limits,&device)==0);
    ir->resources.clip_capacity=3u;
    gs[0].clip_id=1u;CHECK(prepared_denied(ir,&limits,&device)==0);gs[0].clip_id=3u;
    ir->resources.graphics_state_count=ir->resources.graphics_state_capacity=2u;
    CHECK(prepared_denied(ir,&limits,&device)==0);
    ir->resources=saved;ir->operations[0]=old_op;
    memset(&plan,0,sizeof(plan));plan.source=ir;
    memset(&a,0xa5,sizeof(a));memset(&receipt,0xa5,sizeof(receipt));
    CHECK(quantapdf_native_execute_page_v1(&plan,&receipt,&a)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(a.pixels==NULL && a.size==0u && receipt.sample_tests_exact==0 && receipt.sample_tests==0u);
    puts("Prepared page: frozen budgets, complete Clip/GS closure and same C11 pixels passed");
    return 0;
}

/* A 4x4 rectangle on an 8x8 page bills 4 edges * 16 samples * 64 pixels.
 * These limits are intentionally exact; resetting a child/page ledger would
 * incorrectly accept the third draw or the second half of a short page cap. */
static int work_ledger_contracts(const quantapdf_ir *source)
{
    const size_t cost=4096u;
    quantapdf_native_work_ledger_v1 ledger;
    quantapdf_native_work_lease_v1 a={0},b={0},c={0};
    quantapdf_ir_raster_v1_limits primitive={64u,1u,4u,4096u};
    quantapdf_ir_raster_scoped_mixed_v5_limits limits={64u,2u,8u,8192u,1u,1u};
    quantapdf_ir_raster_image out={0},reference={0};
    quantapdf_composer_operation ops[2]={source->operations[0],source->operations[0]};
    quantapdf_composer_path_command bad[5];
    quantapdf_ir page=*source;
    ops[1].value.path.options.fill_argb=UINT32_C(0xffff0000);
    page.operations=ops;page.operation_count=2u;

    quantapdf_native_work_init_v1(&ledger,2u*cost,8u);
    CHECK(quantapdf_native_work_reserve_v1(&ledger,cost,4u,&a));
    CHECK(quantapdf_native_work_reserve_v1(&ledger,cost,4u,&b));
    CHECK(!quantapdf_native_work_reserve_v1(&ledger,1u,0u,&c));
    CHECK(!quantapdf_native_work_reserve_v1(&ledger,0u,1u,&c));
    CHECK(ledger.used.sample_work==0u && ledger.reserved.sample_work==2u*cost &&
          ledger.peak_reserved.edges==8u);
    CHECK(quantapdf_native_work_settle_v1(&a,1u,1u,1));
    CHECK(quantapdf_native_work_settle_v1(&b,2u,2u,0));
    CHECK(ledger.used.sample_work==3u && ledger.used.edges==3u &&
          ledger.reserved.sample_work==0u && ledger.reserved.edges==0u &&
          ledger.completed_leases==2u && !ledger.samples_exact);
    CHECK(!quantapdf_native_work_settle_v1(&a,0u,0u,1));

    /* Checked arithmetic at SIZE_MAX, independently for both dimensions. */
    quantapdf_native_work_init_v1(&ledger,SIZE_MAX,SIZE_MAX);
    CHECK(quantapdf_native_work_reserve_v1(&ledger,SIZE_MAX,SIZE_MAX,&a));
    CHECK(!quantapdf_native_work_reserve_v1(&ledger,1u,0u,&b));
    CHECK(!quantapdf_native_work_reserve_v1(&ledger,0u,1u,&b));
    CHECK(quantapdf_native_work_settle_v1(&a,SIZE_MAX,SIZE_MAX,1));
    CHECK(!quantapdf_native_work_reserve_v1(&ledger,1u,1u,&b));
    quantapdf_native_work_init_v1(&ledger,4u,4u);
    CHECK(quantapdf_native_work_reserve_v1(&ledger,2u,2u,&a));
    CHECK(!quantapdf_native_work_settle_v1(&a,3u,1u,1));
    CHECK(ledger.failed && ledger.used.sample_work==2u && ledger.used.edges==2u &&
          !quantapdf_native_work_reserve_v1(&ledger,0u,0u,&b));

    quantapdf_native_work_init_v1(&ledger,2u*cost,8u);
    for(unsigned i=0u;i<2u;++i) {
        OK(quantapdf_ir_raster_render_v1_accounted(source,0u,&primitive,NULL,0u,
            QP_NATIVE_WORK_FULL,&ledger,&out));
        CHECK(ledger.used.sample_work==(i+1u)*cost && ledger.used.edges==(i+1u)*4u);
        CHECK(at(&out,1u,1u)==UINT32_C(0x0000ffff) &&
              at(&out,5u,5u)==UINT32_C(0xffffffff));
        quantapdf_ir_raster_image_dispose(&out);
    }
    memset(&out,0xa5,sizeof(out));
    CHECK(quantapdf_ir_raster_render_v1_accounted(source,0u,&primitive,NULL,0u,
        QP_NATIVE_WORK_FULL,&ledger,&out)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(out.pixels==NULL && out.size==0u && ledger.completed_leases==2u);

    /* Actual two-layer compositor: same bytes and one aggregate receipt. */
    OK(quantapdf_ir_raster_scoped_mixed_v5(&page,0u,&limits,&reference));
    quantapdf_native_work_init_v1(&ledger,2u*cost,8u);
    OK(quantapdf_ir_raster_scoped_mixed_v5_accounted(&page,0u,&limits,8.0,8.0,
        QP_SCOPED_LEGACY,&ledger,&out));
    CHECK(out.size==reference.size && memcmp(out.pixels,reference.pixels,out.size)==0 &&
          at(&out,1u,1u)==UINT32_C(0xff0000ff) && ledger.used.sample_work==2u*cost &&
          ledger.used.edges==8u && ledger.completed_leases==2u && !ledger.samples_exact &&
          ledger.reserved.sample_work==0u && ledger.reserved.edges==0u);
    quantapdf_ir_raster_image_dispose(&out);quantapdf_ir_raster_image_dispose(&reference);
    for(unsigned dimension=0u;dimension<2u;++dimension) {
        quantapdf_native_work_init_v1(&ledger,2u*cost-(dimension==0u),8u-(dimension==1u));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_accounted(&page,0u,&limits,8.0,8.0,
            QP_SCOPED_LEGACY,&ledger,&out)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(out.pixels==NULL && out.size==0u && ledger.failed &&
              ledger.completed_leases==1u && ledger.used.sample_work==cost &&
              ledger.used.edges==4u && ledger.reserved.sample_work==0u);
    }
    memcpy(bad,ops[1].value.path.commands,sizeof(bad));
    bad[4].kind=(quantapdf_composer_path_command_kind)99;
    ops[1].value.path.commands=bad;
    quantapdf_native_work_init_v1(&ledger,2u*cost,8u);
    CHECK(quantapdf_ir_raster_scoped_mixed_v5_accounted(&page,0u,&limits,8.0,8.0,
        QP_SCOPED_LEGACY,&ledger,&out)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(out.pixels==NULL && out.size==0u && ledger.failed &&
          !quantapdf_native_work_reserve_v1(&ledger,0u,0u,&a));
    puts("Page ledger: exact aggregate limits, held reservations, late refusal and overflow passed");
    return 0;
}

int main(void) {
    quantapdf_composer_page_state page={0};
    quantapdf_composer_path_command commands[5]={{0}};
    quantapdf_composer_operation op={0};
    quantapdf_composer_image_state raw={0};
    quantapdf_ir ir={0};
    quantapdf_ir_raster_image out={0},again={0};
    quantapdf_ir_raster_scoped_mixed_v5_limits lim={
        512u,4u,512u,8000000u,512u,512u
    };
    unsigned char sample[3]={255u,0u,0u};
    unsigned char old_fractional_pixels[11u*11u*4u]={0};
    unsigned char source_alpha=128u;
    page.width_points=8.0f;page.height_points=8.0f;
    page.background_argb=UINT32_C(0xffffffff);
    ir.pages=&page;ir.page_count=1u;
    ir.operations=&op;ir.operation_count=1u;
    op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    op.value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op.value.path.options.fill=1;
    op.value.path.options.fill_argb=UINT32_C(0xff0000ff);
    op.value.path.commands=commands;op.value.path.command_count=5u;
    commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1.x=4.0f;
    commands[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[2].point1.x=4.0f;commands[2].point1.y=4.0f;
    commands[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[3].point1.y=4.0f;
    commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    CHECK(prepared_contracts(&ir,&lim)==0);
    CHECK(work_ledger_contracts(&ir)==0);
    /* True 4/3-device-space subpixel edge, not 72-DPI enlargement. */
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(out.width==11u && out.height==11u && out.size==484u);
    CHECK(at(&out,4u,2u)==UINT32_C(0x0000ffff));
    CHECK(at(&out,5u,2u)==UINT32_C(0xbfbfffff));
    CHECK(at(&out,6u,2u)==UINT32_C(0xffffffff));
    CHECK(page.width_points==8.0f && commands[1].point1.x==4.0f);
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&again));
    CHECK(out.size==again.size && memcmp(out.pixels,again.pixels,out.size)==0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&out);
    OK(quantapdf_native_device_ir_render_v1(&ir,144u,&lim,&out));
    CHECK(out.width==16u && out.height==16u &&
          at(&out,7u,2u)==UINT32_C(0x0000ffff) &&
          at(&out,8u,2u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);
    lim.max_pixels=100u;CHECK(denied(&ir,96u,&lim)==0);
    lim.max_pixels=512u;
    /* Source V10's device projection now includes REAL 72 DPI.
     * The legacy integer-only V5 entry remains unchanged. */
    OK(quantapdf_native_device_ir_render_v1(&ir,72u,&lim,&out));
    CHECK(out.width==8u && out.height==8u &&
          at(&out,0u,0u)==UINT32_C(0x0000ffff) &&
          at(&out,4u,4u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);
    /* A 1/4-point ORIGINAL page span produces a CEIL device bitmap
     * with exact partial right/bottom pixel coverage, not FIT scaling
     * or post-raster crop. Reuse the same fill and C11 V5 renderer. */
    page.width_points=8.5f;page.height_points=8.25f;
    commands[1].point1.x=9.0f;
    commands[2].point1.x=9.0f;commands[2].point1.y=9.0f;
    commands[3].point1.y=9.0f;
    OK(quantapdf_native_device_ir_render_v1(&ir,72u,&lim,&out));
    CHECK(out.width==9u && out.height==9u &&
          at(&out,8u,4u)==UINT32_C(0x7f7fffff) &&
          at(&out,4u,8u)==UINT32_C(0xbfbfffff) &&
          at(&out,8u,8u)==UINT32_C(0xdfdfffff));
    quantapdf_ir_raster_image_dispose(&out);
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(out.width==12u && out.height==11u &&
          at(&out,11u,4u)==UINT32_C(0xbfbfffff) &&
          at(&out,6u,10u)==UINT32_C(0x0000ffff));
    quantapdf_ir_raster_image_dispose(&out);
    OK(quantapdf_native_device_ir_render_v1(&ir,144u,&lim,&out));
    CHECK(out.width==17u && out.height==17u &&
          at(&out,6u,16u)==UINT32_C(0x7f7fffff));
    quantapdf_ir_raster_image_dispose(&out);
    page.width_points=8.0f;page.height_points=8.0f;
    commands[1].point1.x=4.0f;
    commands[2].point1.x=4.0f;commands[2].point1.y=4.0f;
    commands[3].point1.y=4.0f;
    /* RAW image source is re-sampled on true target grid. */
    op=(quantapdf_composer_operation){0};
    op.kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    op.value.image.image_id=(quantapdf_composer_image_id)1;
    op.value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    op.value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    op.bounds=(quantapdf_rect){0,0,8,8};
    raw.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    raw.data=sample;raw.size=3u;raw.width=raw.height=1u;raw.components=3;
    ir.resources.images=&raw;ir.resources.image_count=1u;
    ir.resources.image_capacity=1u;
    OK(quantapdf_native_device_ir_render_v1(&ir,144u,&lim,&out));
    CHECK(out.width==16u && at(&out,15u,15u)==UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&out);
    /* The same 1x1 RAW resource is sampled against the real 11x11
     * device grid. Its bottom/right fractional 4/3 edge covers 12/16
     * subpixel samples, and the corner covers 9/16. */
    {
        quantapdf_native_prepared_page_v1 prepared;
        quantapdf_native_source_raster_device_v1 device={0};device.dpi=96.0f;
        OK(quantapdf_native_prepare_page_v1(&ir,&lim,&device,
            QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&prepared));
        CHECK(prepared.usage.image_pixels==1u && prepared.usage.images_used==1u &&
              prepared.usage.expanded_paints==1u && prepared.usage.commands==0u);
        OK(quantapdf_native_execute_page_v1(&prepared,NULL,&out));
    }
    CHECK(out.width==11u && out.height==11u &&
          at(&out,0u,0u)==UINT32_C(0xff0000ff) &&
          at(&out,10u,5u)==UINT32_C(0xff4040ff) &&
          at(&out,10u,10u)==UINT32_C(0xff7070ff));
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&again));
    CHECK(out.size==again.size &&
          memcmp(out.pixels,again.pixels,out.size)==0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&out);

    op.bounds=(quantapdf_rect){2,2,6,6};
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(out.width==11u && out.height==11u &&
          out.size==sizeof(old_fractional_pixels) &&
          at(&out,2u,2u)==UINT32_C(0xffefefff) &&
          at(&out,3u,3u)==UINT32_C(0xff0000ff) &&
          at(&out,8u,8u)==UINT32_C(0xffffffff));
    memcpy(old_fractional_pixels,out.pixels,out.size);
    quantapdf_ir_raster_image_dispose(&out);

    /* A frozen Clip matching the fractional source bounds must NOT
     * square its coverage alpha: both shapes share one 4x4 grid. */
    {
        quantapdf_composer_graphics_state gs={0};
        quantapdf_composer_clip_state clip={0};
        quantapdf_composer_path_command clip_path[5]={{0}};
        gs.fill_alpha=gs.stroke_alpha=1.0f;
        gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        gs.clip_id=(quantapdf_composer_clip_id)1u;
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.commands=clip_path;clip.command_count=5u;
        clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        clip.transform=(quantapdf_affine_transform){1,0,0,1,0,0};
        clip_path[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
        clip_path[0].point1=(quantapdf_point){2,2};
        clip_path[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_path[1].point1=(quantapdf_point){6,2};
        clip_path[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_path[2].point1=(quantapdf_point){6,6};
        clip_path[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_path[3].point1=(quantapdf_point){2,6};
        clip_path[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
        ir.resources.graphics_states=&gs;
        ir.resources.graphics_state_count=1u;
        ir.resources.graphics_state_capacity=1u;
        ir.resources.clips=&clip;
        ir.resources.clip_count=1u;
        ir.resources.clip_capacity=1u;
        op.graphics_state_id=(quantapdf_composer_graphics_state_id)1u;
        op.value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
        op.value.image.options.graphics_state_id=
            (quantapdf_composer_graphics_state_id)1u;
        OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
        CHECK(out.size==sizeof(old_fractional_pixels) &&
              memcmp(out.pixels,old_fractional_pixels,out.size)==0 &&
              at(&out,2u,2u)==UINT32_C(0xffefefff));
        quantapdf_ir_raster_image_dispose(&out);
        lim.max_sample_tests=128u;
        CHECK(denied(&ir,96u,&lim)==0);
        lim.max_sample_tests=8000000u;
        /* The color remains source-owned; only the completed pixels
         * survive. Effective alpha is source-alpha times ONE mask. */
        raw.has_alpha=1;
        raw.alpha_data=&source_alpha;
        raw.alpha_size=1u;
        OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
        CHECK(at(&out,2u,2u)==UINT32_C(0xfff7f7ff));
        quantapdf_ir_raster_image_dispose(&out);
        raw.has_alpha=0;raw.alpha_data=NULL;raw.alpha_size=0u;
        ir.resources.graphics_states=NULL;
        ir.resources.graphics_state_count=0u;
        ir.resources.graphics_state_capacity=0u;
        ir.resources.clips=NULL;
        ir.resources.clip_count=0u;
        ir.resources.clip_capacity=0u;
        op.graphics_state_id=(quantapdf_composer_graphics_state_id)0u;
        op.value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
        op.value.image.options.graphics_state_id=
            (quantapdf_composer_graphics_state_id)0u;
    }
    /* Exact 72-DPI/int-only V5 entry remains restrictive; only the
     * explicit native device entry admits fractional image geometry. */
    {
        quantapdf_ir borrowed=ir;
        quantapdf_composer_page_state device_page=page;
        quantapdf_composer_operation device_op=op;
        quantapdf_ir_raster_image rejected={0};
        device_page.width_points=11.0f;
        device_page.height_points=11.0f;
        device_op.bounds=(quantapdf_rect){
            8.0f/3.0f,8.0f/3.0f,8.0f,8.0f};
        borrowed.pages=&device_page;
        borrowed.operations=&device_op;
        CHECK(quantapdf_ir_raster_scoped_mixed_v5(
                &borrowed,0u,&lim,&rejected)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(rejected.pixels==NULL && rejected.size==0u);
    }
    lim.max_drawn_pixels=35u;
    CHECK(denied(&ir,96u,&lim)==0); /* ceil-box 6x6, not 4x4 */
    lim.max_drawn_pixels=512u;
    ir.resources.fonts=(quantapdf_composer_font_state *)(uintptr_t)1u;
    CHECK(denied(&ir,96u,&lim)==0);
    ir.resources.fonts=NULL;
    op.kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(denied(&ir,144u,&lim)==0);
    ir.operations=NULL;ir.operation_count=0u;
    ir.resources.images=NULL;ir.resources.image_count=0u;
    ir.resources.image_capacity=0u;
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(out.width==11u && at(&out,10u,10u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);
    return 0;
}
