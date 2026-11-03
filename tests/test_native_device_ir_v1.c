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
          ledger.peak_reserved.sample_work==2u*cost && ledger.peak_reserved.edges==8u &&
          ledger.reserved.sample_work==0u && ledger.reserved.edges==0u);
    quantapdf_ir_raster_image_dispose(&out);quantapdf_ir_raster_image_dispose(&reference);
    for(unsigned dimension=0u;dimension<2u;++dimension) {
        quantapdf_native_work_init_v1(&ledger,2u*cost-(dimension==0u),8u-(dimension==1u));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_accounted(&page,0u,&limits,8.0,8.0,
            QP_SCOPED_LEGACY,&ledger,&out)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(out.pixels==NULL && out.size==0u && ledger.failed &&
              ledger.completed_leases==0u && ledger.used.sample_work==cost &&
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

static int resource_denied(const quantapdf_native_prepared_page_v1 *plan)
{
    quantapdf_native_execution_receipt_v1 receipt,zero;
    quantapdf_ir_raster_image out;
    memset(&receipt,0xa5,sizeof(receipt));memset(&zero,0,sizeof(zero));
    memset(&out,0xa5,sizeof(out));
    CHECK(quantapdf_native_execute_page_v1(plan,&receipt,&out)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(out.pixels==NULL && out.size==0u && out.width==0u && out.height==0u &&
          out.stride==0u && memcmp(&receipt,&zero,sizeof(receipt))==0);
    return 0;
}

static int resource_ledger_contracts(const quantapdf_ir *source)
{
    quantapdf_native_work_ledger_v1 ledger;
    quantapdf_native_resource_limits_v1 caps={SIZE_MAX,SIZE_MAX,SIZE_MAX};
    quantapdf_native_work_init_v1(&ledger,1u,1u);
    CHECK(quantapdf_native_resources_init_v1(&ledger,&caps,SIZE_MAX-1u));
    CHECK(quantapdf_native_memory_take_v1(&ledger,1u));
    CHECK(!quantapdf_native_memory_take_v1(&ledger,1u));
    CHECK(ledger.failed && ledger.resources.live_bytes==SIZE_MAX);
    CHECK(quantapdf_native_memory_give_v1(&ledger,1u));
    CHECK(!quantapdf_native_memory_give_v1(&ledger,1u));
    CHECK(ledger.resources.live_bytes==SIZE_MAX-1u);
    quantapdf_native_work_init_v1(&ledger,1u,1u);
    CHECK(quantapdf_native_resources_init_v1(&ledger,&caps,0u));
    CHECK(quantapdf_native_pixels_charge_v1(&ledger,SIZE_MAX,SIZE_MAX));
    CHECK(!quantapdf_native_pixels_charge_v1(&ledger,1u,0u));
    CHECK(ledger.resources.source_pixels==SIZE_MAX && ledger.resources.drawn_pixels==SIZE_MAX);
    quantapdf_native_work_init_v1(&ledger,1u,1u);caps.max_source_pixels=caps.max_drawn_pixels=1u;
    CHECK(quantapdf_native_resources_init_v1(&ledger,&caps,0u));
    CHECK(!quantapdf_native_pixels_charge_v1(&ledger,1u,2u));
    CHECK(ledger.resources.source_pixels==0u && ledger.resources.drawn_pixels==0u);

    quantapdf_ir ir=*source;
    quantapdf_composer_operation ops[2]={{0}};
    quantapdf_composer_image_state raw={0};
    quantapdf_composer_clip_state clip={0};
    quantapdf_composer_graphics_state gs={0};
    unsigned char red[3]={255u,0u,0u};
    raw.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    raw.data=red;raw.size=3u;raw.width=raw.height=1u;raw.components=3;
    ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[0].bounds=(quantapdf_rect){0.0f,0.0f,8.0f,8.0f};
    ops[0].value.image.image_id=1u;

    ops[0].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    ops[1]=ops[0];
    ir.operations=ops;ir.operation_count=2u;
    ir.resources.images=&raw;ir.resources.image_count=ir.resources.image_capacity=1u;
    clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clip.commands=source->operations[0].value.path.commands;clip.command_count=5u;
    clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    clip.transform.a=clip.transform.d=1.0f;
    gs.clip_id=1u;gs.fill_alpha=gs.stroke_alpha=1.0f;gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    for(unsigned clipped=0u;clipped<2u;++clipped) {
        if(clipped) {
            ir.resources.clips=&clip;ir.resources.clip_count=ir.resources.clip_capacity=1u;
            ir.resources.graphics_states=&gs;
            ir.resources.graphics_state_count=ir.resources.graphics_state_capacity=1u;
            for(size_t i=0u;i<2u;++i) {
                ops[i].graphics_state_id=1u;ops[i].value.image.options.graphics_state_id=1u;
            }
        }
        ir.operation_count=clipped?1u:2u;
        for(unsigned dpi=72u;dpi<=144u;dpi+=dpi==72u?24u:48u) {
            const size_t placements=clipped?1u:2u;
            const size_t side=dpi==72u?8u:dpi==96u?11u:16u;
            const size_t pixels=side*side;
            quantapdf_ir_raster_scoped_mixed_v5_limits limits={pixels,4u,512u,8000000u,1u,placements*pixels};
            quantapdf_native_source_raster_device_v1 device={0};device.dpi=(float)dpi;
            quantapdf_native_prepared_page_v1 plan,budget;
            quantapdf_native_execution_receipt_v1 receipt;
            quantapdf_ir_raster_image out={0},reference={0};
            OK(quantapdf_native_prepare_page_v1(&ir,&limits,&device,
                QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&plan));
            OK(quantapdf_native_execute_page_v1(&plan,&receipt,&out));
            CHECK(receipt.source_pixels==1u && receipt.drawn_pixels==placements*pixels &&
                  receipt.retained_bytes==plan.usage.retained_bytes &&
                  receipt.output_bytes==out.size && out.size==pixels*4u &&
                  receipt.peak_live_bytes>=receipt.retained_bytes+receipt.output_bytes &&
                  receipt.peak_live_bytes<=plan.resource_limits.max_live_bytes);
            CHECK(at(&out,1u,1u)==UINT32_C(0xff0000ff));
            if(clipped)CHECK(at(&out,side-1u,side-1u)==UINT32_C(0xffffffff));
            /* Independent live-buffer count: two integer RAW layers and one
             * final composite, with no geometry/mask scratch at 72/144 DPI. */
            if(!clipped && dpi!=96u)
                CHECK(receipt.peak_live_bytes==receipt.retained_bytes+3u*out.size &&
                      receipt.completed_leases==0u);
            if(clipped)CHECK(receipt.completed_leases==1u && receipt.compiled_edges>0u);
            const size_t peak=receipt.peak_live_bytes;
            OK(quantapdf_native_device_ir_render_v1(&ir,dpi,&limits,&reference));
            CHECK(out.size==reference.size && memcmp(out.pixels,reference.pixels,out.size)==0);
            quantapdf_ir_raster_image_dispose(&out);
            budget=plan;budget.resource_limits.max_live_bytes=peak;
            OK(quantapdf_native_execute_page_v1(&budget,&receipt,&out));
            CHECK(receipt.peak_live_bytes==peak && out.size==reference.size &&
                  memcmp(out.pixels,reference.pixels,out.size)==0);
            quantapdf_ir_raster_image_dispose(&out);
            budget.resource_limits.max_live_bytes=peak-1u;CHECK(resource_denied(&budget)==0);
            budget=plan;budget.resource_limits.max_source_pixels=0u;CHECK(resource_denied(&budget)==0);
            budget=plan;budget.resource_limits.max_drawn_pixels=placements*pixels-1u;CHECK(resource_denied(&budget)==0);
            OK(quantapdf_native_execute_page_v1(&plan,&receipt,&out));
            CHECK(receipt.source_pixels==1u && receipt.drawn_pixels==placements*pixels &&
                  memcmp(out.pixels,reference.pixels,out.size)==0);
            quantapdf_ir_raster_image_dispose(&out);quantapdf_ir_raster_image_dispose(&reference);
        }
    }
    /* Preserve the historical clipped IMAGE-only profile: two placements
     * sharing one resource are still refused by its exact capacity rule. */
    {
        quantapdf_native_prepared_page_v1 plan;
        quantapdf_native_source_raster_device_v1 device={0};device.dpi=72.0f;
        const quantapdf_ir_raster_scoped_mixed_v5_limits limits={64u,4u,512u,8000000u,1u,128u};
        ir.operation_count=2u;
        OK(quantapdf_native_prepare_page_v1(&ir,&limits,&device,
            QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&plan));
        CHECK(resource_denied(&plan)==0);
    }
    puts("Resource ledger: unique RAW inputs, repeated/clipped placements, exact byte peaks and N-1 passed");
    return 0;
}

static int owned_geometry_contracts(const quantapdf_ir *source)
{
    quantapdf_ir ir=*source;
    quantapdf_composer_page_state page=source->pages[0];
    quantapdf_composer_operation op=source->operations[0];
    quantapdf_composer_path_command path[5],clip_path[5];
    quantapdf_composer_clip_state clip={0};
    const quantapdf_composer_clip_state *clips[1]={&clip};
    quantapdf_native_device_plan plan;
    quantapdf_native_geometry_v1 owner={0};
    quantapdf_native_geometry_info_v1 info,after;
    quantapdf_native_work_ledger_v1 ledger;
    quantapdf_native_resource_limits_v1 resources={0u,0u,100000u};
    quantapdf_ir_raster_v1_limits limits={64u,1u,8u,8192u};
    quantapdf_ir_raster_image a={0},b={0},refused;
    size_t work=0u;
    memcpy(path,op.value.path.commands,sizeof(path));memcpy(clip_path,path,sizeof(path));
    for(size_t i=0u;i<4u;++i) {clip_path[i].point1.x+=2.0f;clip_path[i].point1.y+=2.0f;}
    op.value.path.commands=path;ir.pages=&page;ir.operations=&op;
    clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;clip.commands=clip_path;clip.command_count=5u;
    clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;clip.transform.a=clip.transform.d=1.0f;
    OK(quantapdf_native_device_plan_create("0 0 8 8","0 0 8 8","1",0,72u,64u,&plan));
    quantapdf_native_work_init_v1(&ledger,16384u,8u);
    CHECK(quantapdf_native_resources_init_v1(&ledger,&resources,0u));
    OK(quantapdf_native_geometry_prepare_v1(&ir,0u,&limits,&plan,clips,1u,
        QP_NATIVE_WORK_FULL,&ledger,&owner));
    OK(quantapdf_native_geometry_info_get_v1(&owner,&info));
    CHECK(info.width==8u && info.height==8u && info.draws==1u && info.clips==1u &&
          info.edges==8u && info.commands==0u && info.retained_bytes==ledger.resources.live_bytes &&
          ledger.reserved.sample_work==8192u && ledger.reserved.edges==8u && ledger.used.edges==0u);
    /* Destroy every borrowed descriptor, command array, budget and plan.
     * The two later samplings must use only the owned compiled payload. */
    memset(path,0xa5,sizeof(path));memset(clip_path,0xa5,sizeof(clip_path));
    memset(&op,0xa5,sizeof(op));memset(&page,0xa5,sizeof(page));memset(&clip,0xa5,sizeof(clip));
    memset(&ir,0xa5,sizeof(ir));memset(&plan,0xa5,sizeof(plan));memset(&limits,0xa5,sizeof(limits));
    OK(quantapdf_native_geometry_execute_v1(&owner,&work,&a));
    CHECK(work==8192u && at(&a,3u,3u)==UINT32_C(0x0000ffff) &&
          at(&a,1u,1u)==UINT32_C(0xffffffff) && ledger.used.edges==8u);
    OK(quantapdf_native_geometry_execute_v1(&owner,&work,&b));
    CHECK(work==8192u && a.pixels!=b.pixels && a.size==b.size &&
          memcmp(a.pixels,b.pixels,a.size)==0 && ledger.used.sample_work==16384u &&
          ledger.used.edges==8u && ledger.completed_leases==2u);
    OK(quantapdf_native_geometry_info_get_v1(&owner,&after));
    CHECK(memcmp(&info,&after,sizeof(info))==0);
    memset(&refused,0xa5,sizeof(refused));work=999u;
    CHECK(quantapdf_native_geometry_execute_v1(&owner,&work,&refused)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(refused.pixels==NULL && refused.size==0u && work==0u && ledger.used.edges==8u);
    quantapdf_native_geometry_dispose_v1(&owner);
    CHECK(owner.data==NULL && ledger.resources.live_bytes==a.size+b.size);
    a.pixels[0]=17u;CHECK(b.pixels[0]!=17u);
    quantapdf_native_raster_image_dispose_v1(&ledger,&a);
    quantapdf_native_raster_image_dispose_v1(&ledger,&b);
    CHECK(ledger.resources.live_bytes==0u && ledger.reserved.sample_work==0u);
    quantapdf_native_geometry_dispose_v1(&owner);

    limits=(quantapdf_ir_raster_v1_limits){64u,1u,4u,4096u};
    quantapdf_native_work_init_v1(&ledger,4096u,4u);
    CHECK(quantapdf_native_resources_init_v1(&ledger,&resources,0u));
    OK(quantapdf_native_geometry_prepare_v1(source,0u,&limits,NULL,NULL,0u,
        QP_NATIVE_WORK_FULL,&ledger,&owner));
    quantapdf_native_geometry_dispose_v1(&owner);
    CHECK(ledger.failed && ledger.resources.live_bytes==0u && ledger.reserved.sample_work==0u);
    quantapdf_native_work_init_v1(&ledger,4096u,4u);resources.max_live_bytes=1u;
    CHECK(quantapdf_native_resources_init_v1(&ledger,&resources,0u));
    memset(&owner,0xa5,sizeof(owner));
    CHECK(quantapdf_native_geometry_prepare_v1(source,0u,&limits,NULL,NULL,0u,
        QP_NATIVE_WORK_FULL,&ledger,&owner)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(owner.data==NULL && owner.ledger==NULL && owner.lease.owner==NULL &&
          ledger.failed && ledger.resources.live_bytes==0u && ledger.reserved.sample_work==0u);

    /* Source-shaped exact Text also snapshots canonical operands and the
     * physical plan: the 4/3-pixel boundary stays 1/4 covered, not FIT. */
    ir=*source;page=source->pages[0];op=(quantapdf_composer_operation){0};
    quantapdf_outline_glyph glyph={0};memcpy(path,source->operations[0].value.path.commands,sizeof(path));
    glyph.commands=path;glyph.command_count=5u;
    op.kind=QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES;
    op.value.text_outlines.glyphs=&glyph;op.value.text_outlines.glyph_count=1u;
    op.value.text_outlines.argb=UINT32_C(0xff0000ff);ir.operations=&op;ir.pages=&page;
    limits=(quantapdf_ir_raster_v1_limits){121u,1u,512u,8000000u};
    OK(quantapdf_native_device_plan_create_physical("0 0 8 8","0 0 8 8","1",0,96u,121u,&plan));
    quantapdf_native_work_init_v1(&ledger,8000000u,512u);resources.max_live_bytes=100000u;
    CHECK(quantapdf_native_resources_init_v1(&ledger,&resources,0u));
    OK(quantapdf_native_geometry_prepare_text_v1(&ir,&limits,&plan,&ledger,&owner));
    OK(quantapdf_native_geometry_info_get_v1(&owner,&info));CHECK(info.commands==5u);
    memset(path,0xa5,sizeof(path));memset(&glyph,0xa5,sizeof(glyph));memset(&op,0xa5,sizeof(op));
    memset(&ir,0xa5,sizeof(ir));memset(&page,0xa5,sizeof(page));memset(&plan,0xa5,sizeof(plan));
    OK(quantapdf_native_geometry_execute_v1(&owner,&work,&a));
    CHECK(a.width==11u && a.height==11u && at(&a,2u,2u)==UINT32_C(0x0000ffff) &&
          at(&a,5u,2u)==UINT32_C(0xbfbfffff) && at(&a,6u,2u)==UINT32_C(0xffffffff) &&
          work>0u && ledger.samples_exact && ledger.used.edges==info.edges);
    quantapdf_native_geometry_dispose_v1(&owner);
    quantapdf_native_raster_image_dispose_v1(&ledger,&a);CHECK(ledger.resources.live_bytes==0u);
    puts("Owned geometry: detached PATH/Clip/Text operands, replay quotas and release passed");
    return 0;
}

/* Full compiled page contains no projected PATH/Clip/GS/IR descriptor borrow. */
static int compiled_page_contracts(const quantapdf_ir *fixture)
{
    for(unsigned mode=0u;mode<3u;++mode) {
        quantapdf_ir ir=*fixture;
        quantapdf_composer_page_state page=fixture->pages[0];
        quantapdf_composer_operation op=fixture->operations[0];
        quantapdf_composer_path_command commands[5];
        quantapdf_composer_clip_state clip={0};quantapdf_composer_graphics_state gs={0};
        quantapdf_composer_image_state image={0};unsigned char red[3]={255u,0u,0u};
        quantapdf_ir_raster_scoped_mixed_v5_limits limits={512u,4u,512u,8000000u,1u,128u};
        quantapdf_native_source_raster_device_v1 device={0};device.dpi=mode==2u?96.0f:72.0f;
        quantapdf_native_prepared_page_v1 metadata;
        quantapdf_native_compiled_page_v1 compiled,zero;
        quantapdf_native_execution_receipt_v1 receipt;quantapdf_ir_raster_image out={0},denied={0};
        memcpy(commands,op.value.path.commands,sizeof(commands));
        ir.pages=&page;ir.operations=&op;op.value.path.commands=commands;
        if(mode!=0u) {
            memset(&op,0,sizeof(op));op.kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
            op.bounds.x1=op.bounds.y1=8.0f;op.graphics_state_id=1u;
            op.value.image.image_id=1u;
            op.value.image.options.graphics_state_id=1u;
            op.value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
            image.data=red;image.size=3u;image.width=image.height=1u;image.components=3;
            image.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
            clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;clip.commands=commands;clip.command_count=5u;
            clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;clip.transform.a=clip.transform.d=1.0f;
            gs.clip_id=1u;gs.fill_alpha=gs.stroke_alpha=1.0f;gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
            ir.resources.images=&image;ir.resources.image_count=ir.resources.image_capacity=1u;
            ir.resources.clips=&clip;ir.resources.clip_count=ir.resources.clip_capacity=1u;
            ir.resources.graphics_states=&gs;ir.resources.graphics_state_count=ir.resources.graphics_state_capacity=1u;
        }
        OK(quantapdf_native_prepare_page_v1(&ir,&limits,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&metadata));
        OK(quantapdf_native_compiled_page_compile_v1(&metadata,&compiled));
        CHECK(compiled.metadata.source==NULL && compiled.ledger.completed_leases==0u);
        memset(&ir,0xa5,sizeof(ir));memset(&page,0xa5,sizeof(page));memset(&op,0xa5,sizeof(op));
        memset(commands,0xa5,sizeof(commands));memset(&clip,0xa5,sizeof(clip));memset(&gs,0xa5,sizeof(gs));
        memset(&image,0xa5,sizeof(image));memset(&device,0xa5,sizeof(device));
        memset(&limits,0xa5,sizeof(limits));memset(&metadata,0xa5,sizeof(metadata));
        OK(quantapdf_native_compiled_page_execute_v1(&compiled,&receipt,&out));
        CHECK(at(&out,1u,1u)==(mode==0u?UINT32_C(0x0000ffff):UINT32_C(0xff0000ff)));
        CHECK(receipt.completed_leases==1u && receipt.compiled_edges>0u &&
              compiled.ledger.resources.live_bytes==receipt.retained_bytes+qp_compiled_retained_bytes(&compiled));
        CHECK(quantapdf_native_compiled_page_execute_v1(&compiled,&receipt,&denied)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(denied.pixels==NULL && receipt.output_bytes==0u && compiled.ledger.failed);
        quantapdf_native_compiled_page_dispose_v1(&compiled);memset(&zero,0,sizeof(zero));
        CHECK(memcmp(&compiled,&zero,sizeof(zero))==0);
        CHECK(at(&out,1u,1u)==(mode==0u?UINT32_C(0x0000ffff):UINT32_C(0xff0000ff)));
        quantapdf_ir_raster_image_dispose(&out);
    }
    puts("Compiled page: frozen PATH/RAW/Clip projection, cumulative refusal and independent output passed");
    return 0;
}

/* M2b.2a: exact 16-bit glyph coverage AND frozen W/W* Clip coverage.
 * The disjoint edge control produces zero ink even though each mask
 * separately covers part of pixel (0,0): rounded-alpha multiplication
 * would spuriously darken it. Real V1 compiler/sampler, no second raster. */
static int correlated_glyph_clip_contracts(void)
{
    for(unsigned variant=0u;variant<2u;++variant)
    for(unsigned dpi=72u;dpi<=144u;dpi+=dpi==72u?24u:48u) {
        quantapdf_composer_page_state page={0};
        quantapdf_composer_path_command glyph_commands[5]={{0}};
        quantapdf_composer_path_command clip_commands[5]={{0}};
        quantapdf_outline_glyph glyph={0};
        quantapdf_composer_operation op={0};
        quantapdf_composer_clip_state clip={0};
        quantapdf_ir ir={0};
        quantapdf_native_device_plan plan={0};
        quantapdf_native_work_ledger_v1 ledger;
        quantapdf_native_resource_limits_v1 resources={0u,0u,100000u};
        quantapdf_ir_raster_v1_limits limits={16u,1u,64u,800000u};
        quantapdf_native_geometry_v1 owner={0};
        quantapdf_native_geometry_info_v1 info={0};
        quantapdf_ir_raster_image rendered={0};
        size_t work=0u;
        const float right=variant?0.75f:0.5f;
        page.width_points=2.0f;page.height_points=2.0f;
        page.background_argb=UINT32_C(0xffffffff);
        glyph_commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
        glyph_commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        glyph_commands[1].point1.x=right;
        glyph_commands[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        glyph_commands[2].point1.x=right;glyph_commands[2].point1.y=1.0f;
        glyph_commands[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        glyph_commands[3].point1.y=1.0f;
        glyph_commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
        clip_commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
        clip_commands[0].point1.x=0.5f;
        clip_commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_commands[1].point1.x=1.0f;
        clip_commands[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_commands[2].point1.x=1.0f;clip_commands[2].point1.y=1.0f;
        clip_commands[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_commands[3].point1.x=0.5f;clip_commands[3].point1.y=1.0f;
        clip_commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
        glyph.commands=glyph_commands;glyph.command_count=5u;
        op.kind=QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES;
        op.value.text_outlines.glyphs=&glyph;op.value.text_outlines.glyph_count=1u;
        op.value.text_outlines.argb=UINT32_C(0xff000000);
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.commands=clip_commands;clip.command_count=5u;
        clip.fill_rule=variant?QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
                               QUANTAPDF_COMPOSER_FILL_NONZERO;
        clip.transform.a=clip.transform.d=1.0f;
        ir.pages=&page;ir.page_count=1u;ir.operations=&op;ir.operation_count=1u;
        OK(quantapdf_native_device_plan_create_physical(
            "0 0 2 2","0 0 2 2","1",0,dpi,16u,&plan));
        quantapdf_native_work_init_v1(&ledger,800000u,64u);
        CHECK(quantapdf_native_resources_init_v1(&ledger,&resources,0u));
        OK(quantapdf_native_geometry_prepare_text_clipped_v1(
            &ir,&limits,&plan,&clip,&ledger,&owner));
        OK(quantapdf_native_geometry_info_get_v1(&owner,&info));
        CHECK(info.clips==1u && info.draws==1u && info.edges>=8u);
        /* Prove full snapshot ownership before execution. Keep independent
         * original commands for fresh N/N-1 denial builds below. */
        quantapdf_composer_path_command saved_glyph[5],saved_clip[5];
        memcpy(saved_glyph,glyph_commands,sizeof(saved_glyph));
        memcpy(saved_clip,clip_commands,sizeof(saved_clip));
        memset(glyph_commands,0xa5,sizeof(glyph_commands));
        memset(clip_commands,0xa5,sizeof(clip_commands));
        memset(&glyph,0xa5,sizeof(glyph));memset(&clip,0xa5,sizeof(clip));
        memset(&op,0xa5,sizeof(op));memset(&ir,0xa5,sizeof(ir));
        OK(quantapdf_native_geometry_execute_v1(&owner,&work,&rendered));
        const double scale=(double)dpi/72.0;
        CHECK(rendered.width==(size_t)(2.0*scale+0.5) &&
              rendered.height==(size_t)(2.0*scale+0.5) &&
              work>0u && work<=800000u && ledger.samples_exact);
        unsigned ink=0u;
        for(size_t y=0u;y<rendered.height;++y)
        for(size_t x=0u;x<rendered.width;++x) {
            unsigned covered=0u,glyph_samples=0u,clip_samples=0u;
            for(unsigned sy=0u;sy<4u;++sy)
            for(unsigned sx=0u;sx<4u;++sx) {
                const double px=(double)x+((double)sx+0.5)/4.0;
                const double py=(double)y+((double)sy+0.5)/4.0;
                const int in_glyph=px<(double)right*scale && py<scale;
                const int in_clip=px>=0.5*scale && px<scale && py<scale;
                glyph_samples+=(unsigned)in_glyph;
                clip_samples+=(unsigned)in_clip;
                covered+=(unsigned)(in_glyph && in_clip);
            }
            if(variant==0u && dpi==72u && x==0u && y==0u)
                CHECK(covered==0u && glyph_samples>0u && clip_samples>0u);
            ink+=covered;
            const unsigned gray=(255u*(16u-covered)+8u)/16u;
            const uint32_t expected=(gray<<24u)|(gray<<16u)|(gray<<8u)|255u;
            CHECK(at(&rendered,x,y)==expected);
        }
        CHECK((variant==0u && ink==0u) || (variant==1u && ink>0u));
        quantapdf_native_geometry_dispose_v1(&owner);
        quantapdf_native_raster_image_dispose_v1(&ledger,&rendered);
        CHECK(ledger.resources.live_bytes==0u &&
              ledger.reserved.sample_work==0u);

        /* Three strict N/N-1 refusals from the exact successful execution:
         * (1) actual glyph+Clip visited work, (2) compiled glyph+Clip edges,
         * (3) retained compiler bytes. Never publish a partial image.
         * These use fresh owners; successful mode-10 goldens are unchanged. */
        CHECK(work>0u && info.edges>1u && info.retained_bytes>1u);
        memcpy(glyph_commands,saved_glyph,sizeof(glyph_commands));
        memcpy(clip_commands,saved_clip,sizeof(clip_commands));
        memset(&glyph,0,sizeof(glyph));
        glyph.commands=glyph_commands;glyph.command_count=5u;
        memset(&op,0,sizeof(op));
        op.kind=QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES;
        op.value.text_outlines.glyphs=&glyph;
        op.value.text_outlines.glyph_count=1u;
        op.value.text_outlines.argb=UINT32_C(0xff000000);
        memset(&clip,0,sizeof(clip));
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.commands=clip_commands;clip.command_count=5u;
        clip.fill_rule=variant?QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
                               QUANTAPDF_COMPOSER_FILL_NONZERO;
        clip.transform.a=clip.transform.d=1.0f;
        memset(&ir,0,sizeof(ir));
        ir.pages=&page;ir.page_count=1u;
        ir.operations=&op;ir.operation_count=1u;
        for(unsigned denial=0u;denial<3u;++denial) {
            const size_t sample_cap=denial==0u?work-1u:800000u;
            const size_t edge_cap=denial==1u?info.edges-1u:64u;
            const size_t byte_cap=denial==2u?info.retained_bytes-1u:100000u;
            const quantapdf_ir_raster_v1_limits smaller={
                16u,1u,edge_cap,sample_cap
            };
            quantapdf_native_resource_limits_v1 small_resources={
                0u,0u,byte_cap
            };
            quantapdf_native_work_ledger_v1 refused_ledger;
            quantapdf_native_geometry_v1 refused_owner={0};
            quantapdf_ir_raster_image refused_pixels={0};
            quantapdf_native_work_init_v1(&refused_ledger,sample_cap,edge_cap);
            CHECK(quantapdf_native_resources_init_v1(
                &refused_ledger,&small_resources,0u));
            quantapdf_status refused=
                quantapdf_native_geometry_prepare_text_clipped_v1(
                    &ir,&smaller,&plan,&clip,&refused_ledger,&refused_owner);
            if(refused==QUANTAPDF_OK)
                refused=quantapdf_native_geometry_execute_v1(
                    &refused_owner,NULL,&refused_pixels);
            CHECK(refused==QUANTAPDF_ERROR_UNSUPPORTED &&
                  refused_pixels.pixels==NULL && refused_pixels.size==0u);
            quantapdf_native_geometry_dispose_v1(&refused_owner);
            CHECK(refused_ledger.resources.live_bytes==0u &&
                  refused_ledger.reserved.sample_work==0u);
        }
    }
    puts("M2b.2a private C11 correlated glyph/Clip, N/N-1 owner PASS");
    return EXIT_SUCCESS;
}

int main(void) {
    CHECK(correlated_glyph_clip_contracts()==EXIT_SUCCESS);
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
    CHECK(resource_ledger_contracts(&ir)==0);
    CHECK(owned_geometry_contracts(&ir)==0);
    CHECK(compiled_page_contracts(&ir)==0);
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
