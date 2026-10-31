/* Original-PDF Source V10 two-Clip ownership and allocation rollback.
 * Compile the EXACT production Source unit with its existing isolated
 * C-allocation fault hooks. QPDF authors the input, and the existing
 * private device bridge/C11 raster consumes the detached immutable IR.
 * No alternate Source, image provider, renderer or public ABI is used. */
#include "backend/qpdf_native_image_ir.h"
#include "render/native_device_ir_v1.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
thread_local bool track_c_allocation=false;
thread_local size_t c_fail_index=0u,c_allocations=0u,c_faults=0u;
}

/* Staging allocation faults already have their own production-unit
 * acceptance. Here every real C allocation is counted, including both
 * independent command buffers and the Intersection's owned members. */
extern "C" void quantapdf_source_stage_oom_arm(unsigned) noexcept {}
extern "C" bool quantapdf_source_ir_oom_fail_c_allocation() noexcept
{
    if(!track_c_allocation)return false;
    ++c_allocations;
    if(c_fail_index!=0u && c_allocations==c_fail_index) {
        ++c_faults;
        return true;
    }
    return false;
}

#define CHECK(x) do { if(!(x)) { \
    std::fprintf(stderr,"two Clip Source ownership:%d: %s\n",__LINE__,#x); \
    return false; \
} } while(0)

using Obj=QPDFObjectHandle;

static const quantapdf_qpdf_source_raw_image_limits source_budget={
    2u*1024u*1024u,65536u,4u,4u,1000000u,1000000u,4000000u,8u
};
static const quantapdf_ir_raster_scoped_mixed_v5_limits raster_budget={
    8192u,4u,512u,16000000u,4u,8192u
};

static std::string original_pdf(bool mixed,bool physical,
                                const std::string& suffix=std::string())
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto first=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24] >>"));
    auto second=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24] >>"));
    auto selected=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24] >>"));
    first.replaceKey("/Parent",root);
    second.replaceKey("/Parent",root);
    selected.replaceKey("/Parent",root);
    /* A real third leaf, with unsupported Text on a preceding sibling. */
    second.replaceKey("/Contents",pdf->newStream("BT ET"));
    root.replaceKey("/Kids",Obj::newArray({first,second,selected}));
    root.replaceKey("/Count",Obj::newInteger(3));
    if(physical) {
        selected.replaceKey("/Rotate",Obj::newInteger(90));
        selected.replaceKey("/UserUnit",Obj::parse("1.25"));
    }
    const std::string raw("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    auto image=pdf->newStream(raw);
    auto dict=image.getDict();
    dict.replaceKey("/Type",Obj::newName("/XObject"));
    dict.replaceKey("/Subtype",Obj::newName("/Image"));
    dict.replaceKey("/Width",Obj::newInteger(2));
    dict.replaceKey("/Height",Obj::newInteger(2));
    dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
    dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
    auto images=Obj::newDictionary(),resources=Obj::newDictionary();
    images.replaceKey("/I",image);
    resources.replaceKey("/XObject",images);
    selected.replaceKey("/Resources",resources);
    /* Both leaves precede paint, in one outer q saving an unclipped
     * state. Changing CTM before leaf 2 must not move frozen leaf 1.
     * Resetting CTM before painting must not move either leaf. */
    std::string content=
        "q "
        "4 6 m 4 18 20 18 20 6 c 20 4 l 4 4 l h W n "
        "1 0 0 1 2 1 cm "
        "4 6 m 4 18 20 18 20 6 c 20 4 l 4 4 l h W* n "
        "1 0 0 1 -2 -1 cm "
        "q 16 0 0 12 8 3 cm /I Do Q ";
    if(mixed)content+="0 0 1 rg 8 4 6 12 re f Q "
                       "0 1 0 rg 25 18 5 3 re f ";
    else content+="Q ";
    selected.replaceKey("/Contents",pdf->newStream(content+suffix));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(bytes->getBuffer()),
                       bytes->getSize());
}

static quantapdf_status source(const std::string& pdf,
    const quantapdf_qpdf_source_raw_image_limits& budget,quantapdf_ir& ir)
{
    return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
        reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),
        &budget,2u,&ir);
}

template<class T> static bool cleared(const T& value)
{
    const auto *bytes=reinterpret_cast<const unsigned char*>(&value);
    for(size_t i=0u;i<sizeof(value);++i)if(bytes[i]!=0u)return false;
    return true;
}

static bool identity(const quantapdf_affine_transform& transform)
{
    return transform.a==1.0f && transform.b==0.0f &&
           transform.c==0.0f && transform.d==1.0f &&
           transform.e==0.0f && transform.f==0.0f;
}

static bool point_is(const quantapdf_point& point,float x,float y)
{
    return point.x==x && point.y==y;
}

/* Explicit expected source coordinates, independently authored from
 * the two PDF definitions. The second row in each group includes its
 * definition-time translation; Rotate/UserUnit affect BOTH rows. */
static const float expected_points[2][2][6][2]={
    {
        {{4.0f,18.0f},{4.0f,6.0f},{20.0f,6.0f},
         {20.0f,18.0f},{20.0f,20.0f},{4.0f,20.0f}},
        {{6.0f,17.0f},{6.0f,5.0f},{22.0f,5.0f},
         {22.0f,17.0f},{22.0f,19.0f},{6.0f,19.0f}}
    },
    {
        {{7.5f,5.0f},{22.5f,5.0f},{22.5f,25.0f},
         {7.5f,25.0f},{5.0f,25.0f},{5.0f,5.0f}},
        {{8.75f,7.5f},{23.75f,7.5f},{23.75f,27.5f},
         {8.75f,27.5f},{6.25f,27.5f},{6.25f,7.5f}}
    }
};

static bool owned_graph(const quantapdf_ir& ir,bool mixed,bool physical)
{
    CHECK(ir.pages!=nullptr && ir.page_count==1u &&
          ir.pages[0].width_points==(physical?30.0f:32.0f) &&
          ir.pages[0].height_points==(physical?40.0f:24.0f));
    CHECK(ir.operations!=nullptr && ir.operation_count==(mixed?3u:1u));
    const auto& resources=ir.resources;
    CHECK(resources.images!=nullptr && resources.image_count==1u &&
          resources.image_capacity==1u &&
          resources.graphics_states!=nullptr &&
          resources.graphics_state_count==1u &&
          resources.graphics_state_capacity==1u &&
          resources.clips!=nullptr && resources.clip_count==3u &&
          resources.clip_capacity==3u);
    const auto& image=resources.images[0];
    CHECK(image.format==QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW &&
          image.data!=nullptr && image.size==12u &&
          image.width==2u && image.height==2u && image.components==3 &&
          image.alpha_data==nullptr && image.alpha_size==0u &&
          image.has_alpha==0);
    const auto& gs=resources.graphics_states[0];
    CHECK(gs.fill_alpha==1.0f && gs.stroke_alpha==1.0f &&
          gs.blend_mode==QUANTAPDF_COMPOSER_BLEND_NORMAL &&
          gs.clip_id==3u && gs.soft_mask_id==0u);
    for(size_t leaf=0u;leaf<2u;++leaf) {
        const auto& clip=resources.clips[leaf];
        CHECK(clip.kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
              clip.commands!=nullptr && clip.command_count==5u &&
              clip.members==nullptr && clip.member_count==0u &&
              identity(clip.transform) &&
              clip.fill_rule==(leaf==0u?QUANTAPDF_COMPOSER_FILL_NONZERO:
                                       QUANTAPDF_COMPOSER_FILL_EVEN_ODD));
        const auto *commands=clip.commands;
        CHECK(commands[0].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
              commands[1].kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO &&
              commands[2].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
              commands[3].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
              commands[4].kind==QUANTAPDF_COMPOSER_PATH_CLOSE);
        const auto& points=expected_points[physical?1u:0u][leaf];
        CHECK(point_is(commands[0].point1,points[0][0],points[0][1]) &&
              point_is(commands[1].point1,points[1][0],points[1][1]) &&
              point_is(commands[1].point2,points[2][0],points[2][1]) &&
              point_is(commands[1].point3,points[3][0],points[3][1]) &&
              point_is(commands[2].point1,points[4][0],points[4][1]) &&
              point_is(commands[3].point1,points[5][0],points[5][1]));
    }
    CHECK(resources.clips[0].commands!=resources.clips[1].commands);
    const auto& root=resources.clips[2];
    CHECK(root.kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL &&
          root.commands==nullptr && root.command_count==0u &&
          root.members!=nullptr && root.member_count==2u &&
          root.members[0]==1u && root.members[1]==2u);
    const auto& image_op=ir.operations[0];
    CHECK(image_op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
          image_op.page_index==0u && image_op.graphics_state_id==1u &&
          image_op.value.image.image_id==1u &&
          image_op.value.image.options.graphics_state_id==1u &&
          image_op.bounds.x0==(physical?3.75f:8.0f) &&
          image_op.bounds.y0==(physical?10.0f:9.0f) &&
          image_op.bounds.x1==(physical?18.75f:24.0f) &&
          image_op.bounds.y1==(physical?30.0f:21.0f));
    if(mixed) {
        for(size_t i=1u;i<3u;++i) {
            const auto& op=ir.operations[i];
            const size_t expected_gs=i==1u?1u:0u;
            CHECK(op.kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
                  op.page_index==0u && op.graphics_state_id==expected_gs &&
                  op.value.path.options.graphics_state_id==expected_gs &&
                  op.value.path.commands!=nullptr &&
                  op.value.path.command_count==5u &&
                  op.value.path.commands!=resources.clips[0].commands &&
                  op.value.path.commands!=resources.clips[1].commands);
        }
        CHECK(ir.operations[1].value.path.commands!=
              ir.operations[2].value.path.commands);
    }
    return true;
}

static void append_bytes(std::vector<unsigned char>& bytes,
                          const void *data,size_t size)
{
    if(size==0u)return;
    const auto *first=static_cast<const unsigned char*>(data);
    bytes.insert(bytes.end(),first,first+size);
}

/* Include every reachable payload, descriptor, resource ID and owner
 * pointer. Rendering may borrow the graph, but may not rewrite it. */
static std::vector<unsigned char> snapshot(const quantapdf_ir& ir)
{
    std::vector<unsigned char> bytes;
    append_bytes(bytes,&ir,sizeof(ir));
    append_bytes(bytes,ir.pages,ir.page_count*sizeof(*ir.pages));
    append_bytes(bytes,ir.operations,
                 ir.operation_count*sizeof(*ir.operations));
    const auto& resources=ir.resources;
    append_bytes(bytes,resources.images,
                 resources.image_count*sizeof(*resources.images));
    for(size_t i=0u;i<resources.image_count;++i)
        append_bytes(bytes,resources.images[i].data,resources.images[i].size);
    append_bytes(bytes,resources.graphics_states,
                 resources.graphics_state_count*sizeof(*resources.graphics_states));
    append_bytes(bytes,resources.clips,
                 resources.clip_count*sizeof(*resources.clips));
    for(size_t i=0u;i<resources.clip_count;++i) {
        const auto& clip=resources.clips[i];
        append_bytes(bytes,clip.commands,clip.command_count*sizeof(*clip.commands));
        append_bytes(bytes,clip.members,clip.member_count*sizeof(*clip.members));
    }
    for(size_t i=0u;i<ir.operation_count;++i)
        if(ir.operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const auto& path=ir.operations[i].value.path;
            append_bytes(bytes,path.commands,path.command_count*sizeof(*path.commands));
        }
    return bytes;
}

static uint32_t rgba_at(const quantapdf_ir_raster_image& image,size_t x,size_t y)
{
    const auto *pixel=image.pixels+y*image.stride+x*4u;
    return (static_cast<uint32_t>(pixel[0])<<24u) |
           (static_cast<uint32_t>(pixel[1])<<16u) |
           (static_cast<uint32_t>(pixel[2])<<8u) | pixel[3];
}

static bool detached_render(bool mixed,bool physical)
{
    quantapdf_ir ir{};
    {
        std::string pdf=original_pdf(mixed,physical);
        CHECK(source(pdf,source_budget,ir)==QUANTAPDF_OK);
        CHECK(owned_graph(ir,mixed,physical));
        /* The QPDF fixture, Source QPDF, and original input bytes all
         * retire before a consumer is allowed to read the owned IR. */
        pdf.assign(pdf.size(),'x');
    }
    const auto before=snapshot(ir);
    quantapdf_ir_raster_image first{},again{};
    CHECK(quantapdf_native_device_ir_render_v1_two_clip(
        &ir,96u,&raster_budget,&first)==QUANTAPDF_OK);
    CHECK(first.pixels!=nullptr && first.width==(physical?40u:43u) &&
          first.height==(physical?54u:32u) &&
          first.stride==first.width*4u && first.size==first.stride*first.height);
    size_t ink=0u;
    for(size_t y=0u;y+1u<first.height;++y)
        for(size_t x=0u;x+1u<first.width;++x)
            if(rgba_at(first,x,y)!=UINT32_C(0xffffffff))++ink;
    CHECK(ink>0u);
    if(mixed) {
        /* A green paint outside both leaves proves actual Q restoration,
         * even after the detached physical geometry is projected again. */
        CHECK(rgba_at(first,physical?32u:36u,physical?45u:6u)==
              UINT32_C(0x00ff00ff));
    }
    CHECK(quantapdf_native_device_ir_render_v1_two_clip(
        &ir,96u,&raster_budget,&again)==QUANTAPDF_OK);
    CHECK(again.width==first.width && again.height==first.height &&
          again.stride==first.stride && again.size==first.size &&
          std::memcmp(first.pixels,again.pixels,first.size)==0 &&
          snapshot(ir)==before);
    const std::vector<unsigned char> expected(first.pixels,first.pixels+first.size);
    quantapdf_ir_dispose(&ir);
    CHECK(cleared(ir));
    CHECK(std::memcmp(first.pixels,expected.data(),expected.size())==0 &&
          std::memcmp(again.pixels,expected.data(),expected.size())==0);
    quantapdf_ir_raster_image_dispose(&first);
    quantapdf_ir_raster_image_dispose(&again);
    CHECK(cleared(first) && cleared(again));
    return true;
}

static void begin_allocation_count(size_t fail)
{
    c_fail_index=fail;
    c_allocations=0u;
    c_faults=0u;
    track_c_allocation=true;
}

static bool allocation_matrix(bool mixed)
{
    const std::string pdf=original_pdf(mixed,true);
    quantapdf_ir clean{};
    begin_allocation_count(0u);
    const auto baseline=source(pdf,source_budget,clean);
    track_c_allocation=false;
    const size_t allocation_count=c_allocations;
    /* Existing image/page/operation ownership plus GS, 3 descriptors,
     * two separately allocated command arrays and owned root members.
     * Mixed additionally reallocates operations and owns two PATHs. */
    CHECK(baseline==QUANTAPDF_OK && owned_graph(clean,mixed,true) &&
          allocation_count>=(mixed?12u:9u) &&
          allocation_count<=32u && c_faults==0u);
    quantapdf_ir_dispose(&clean);
    CHECK(cleared(clean));
    for(size_t fail=1u;fail<=allocation_count;++fail) {
        quantapdf_ir failed;
        std::memset(&failed,0xA5,sizeof(failed));
        begin_allocation_count(fail);
        const auto status=source(pdf,source_budget,failed);
        track_c_allocation=false;
        if(status!=QUANTAPDF_ERROR_NOMEM || !cleared(failed) ||
           c_faults!=1u || c_allocations!=fail) {
            std::fprintf(stderr,
                "two Clip Source mixed=%d allocation=%zu/%zu"
                " status=%d faults=%zu allocations=%zu cleared=%d\n",
                mixed?1:0,fail,allocation_count,static_cast<int>(status),
                c_faults,c_allocations,cleared(failed)?1:0);
            return false;
        }
        /* Each genuine partial-owner rollback is followed by a fresh
         * full parser/materializer invocation with injection disabled. */
        quantapdf_ir recovery{};
        CHECK(source(pdf,source_budget,recovery)==QUANTAPDF_OK &&
              owned_graph(recovery,mixed,true));
        quantapdf_ir_dispose(&recovery);
        CHECK(cleared(recovery));
    }
    std::fprintf(stderr,"[native.two_clip_source_oom] mixed=%d"
        " exhausted %zu C allocations\n",mixed?1:0,allocation_count);
    return true;
}

static bool atomic_rejection(const std::string& pdf,
    const quantapdf_qpdf_source_raw_image_limits& budget)
{
    quantapdf_ir ir;
    std::memset(&ir,0xA5,sizeof(ir));
    begin_allocation_count(0u);
    const auto status=source(pdf,budget,ir);
    track_c_allocation=false;
    CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir) &&
          c_allocations==0u && c_faults==0u);
    return true;
}

static bool source_failures(bool mixed)
{
    const std::string pdf=original_pdf(mixed,true);
    auto budget=source_budget;
    budget.max_pdf_bytes=pdf.size()-1u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_content_bytes=16u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_source_pixels=3u;
    CHECK(atomic_rejection(pdf,budget));
    /* Original 32x24 bounds fit; physical media must also fit at the
     * original-source geometry preflight, before any IR allocation. */
    budget=source_budget;budget.max_page_pixels=1199u;
    CHECK(atomic_rejection(pdf,budget));
    /* Original 16x12 image placement fits; only its late UserUnit scale
     * exceeds this draw budget, after both Clips were staged. */
    budget=source_budget;budget.max_drawn_pixels=299u;
    CHECK(atomic_rejection(pdf,budget));
    if(mixed) {
        budget=source_budget;budget.max_operations=2u;
        CHECK(atomic_rejection(pdf,budget));
    }
    CHECK(atomic_rejection(original_pdf(mixed,true,"BT ET"),source_budget));
    /* Rejections cannot poison ownership or the next complete parse. */
    quantapdf_ir recovered{};
    CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
          owned_graph(recovered,mixed,true));
    quantapdf_ir_dispose(&recovered);
    CHECK(cleared(recovered));
    return true;
}

static bool run_tests()
{
    for(bool mixed:{false,true}) {
        for(bool physical:{false,true})CHECK(detached_render(mixed,physical));
        CHECK(allocation_matrix(mixed));
        CHECK(source_failures(mixed));
    }
    return true;
}

int main()
{
    if(!run_tests())return EXIT_FAILURE;
    std::fprintf(stderr,"[native.two_clip_source_oom] completed\n");
    return EXIT_SUCCESS;
}
