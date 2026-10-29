/* Original-PDF Source V10 clipped-PATH ownership and allocation rollback.
 * The EXACT production Source unit uses its existing isolated allocation
 * hooks. QPDF authors the original; the canonical private device bridge
 * and C11 raster consume the detached immutable PATH/Clip IR. */
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

/* C++ staging faults have separate acceptance. Count/fail every genuine
 * C owner allocation here, including all PATHs, both Clip command arrays
 * and the two-leaf Intersection's owned member IDs. */
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
    std::fprintf(stderr,"clipped PATH Source ownership:%d: %s\n",__LINE__,#x); \
    return false; \
} } while(0)

using Obj=QPDFObjectHandle;

/* Image budgets deliberately allow only one pixel: a PATH-only source
 * must not manufacture an Image owner or charge nonexistent image work. */
static const quantapdf_qpdf_source_raw_image_limits source_budget={
    2u*1024u*1024u,65536u,4u,4u,1000000u,1u,1u,8u
};
static const quantapdf_ir_raster_scoped_mixed_v5_limits raster_budget={
    1024u,4u,512u,8000000u,1u,1u
};
static const char *const curve=
    "2 3 m 2 9 10 9 10 3 c 10 2 l 2 2 l h ";
static const char *const path_paints[4]={
    "0 0 1 rg 4 2 6 6 re ",
    "1 0 0 rg 5 4 4 3 re ",
    "1 0 1 rg 6 4 2 2 re ",
    "1 1 0 rg 7 4 1 1 re "
};
static const uint32_t path_colors[4]={
    UINT32_C(0xff0000ff),UINT32_C(0xffff0000),
    UINT32_C(0xffff00ff),UINT32_C(0xffffff00)
};

static std::string clip_prefix(bool two)
{
    std::string content="q ";
    content+=curve;
    content+="W n ";
    if(two) {
        /* The second definition has a different CTM. Neither changing
         * it nor resetting it before paint may move the first leaf. */
        content+="1 0 0 1 1 1 cm ";
        content+=curve;
        content+="W* n 1 0 0 1 -1 -1 cm ";
    }
    return content;
}

static std::string page_content(size_t count,bool two,bool restored)
{
    std::string content=clip_prefix(two);
    for(size_t i=0u;i<count;++i) {
        if(restored && i+1u==count)
            content+="Q 0 1 0 rg 12 9 3 2 re ";
        else content+=path_paints[i];
        content+=(i%2u==0u)?"f ":"f* ";
    }
    if(!restored)content+="Q ";
    return content;
}

static std::string original_pdf(const std::string& content,bool physical,
    size_t selected_index=2u,bool split_streams=false)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto selected=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 16 12] >>"));
    selected.replaceKey("/Parent",root);
    if(selected_index==2u) {
        auto first=pdf->makeIndirectObject(Obj::parse(
            "<< /Type /Page /MediaBox [0 0 16 12] >>"));
        auto second=pdf->makeIndirectObject(Obj::parse(
            "<< /Type /Page /MediaBox [0 0 16 12] >>"));
        first.replaceKey("/Parent",root);
        second.replaceKey("/Parent",root);
        /* The selected original is a real third PageTree leaf. */
        second.replaceKey("/Contents",pdf->newStream("BT ET"));
        root.replaceKey("/Kids",Obj::newArray({first,second,selected}));
        root.replaceKey("/Count",Obj::newInteger(3));
    } else {
        root.replaceKey("/Kids",Obj::newArray({selected}));
        root.replaceKey("/Count",Obj::newInteger(1));
    }
    if(physical) {
        selected.replaceKey("/Rotate",Obj::newInteger(90));
        selected.replaceKey("/UserUnit",Obj::parse("1.25"));
    }
    selected.replaceKey("/Resources",Obj::newDictionary());
    if(split_streams)
        selected.replaceKey("/Contents",Obj::newArray({
            pdf->newStream(content),pdf->newStream(" ")}));
    else selected.replaceKey("/Contents",pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(bytes->getBuffer()),
                       bytes->getSize());
}

static quantapdf_status source(const std::string& pdf,
    const quantapdf_qpdf_source_raw_image_limits& budget,quantapdf_ir& ir,
    size_t selected_index=2u)
{
    return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
        reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),
        &budget,selected_index,&ir);
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

static bool point_is(const quantapdf_point& point,const float expected[2])
{
    return point.x==expected[0] && point.y==expected[1];
}

/* Explicitly authored coordinates, not computed by a second invocation
 * of Source or its geometry helpers. Row 2 freezes translation (1,1).
 * The second group independently includes Rotate90 and UserUnit1.25. */
static const float clip_points[2][2][6][2]={
    {
        {{2.0f,9.0f},{2.0f,3.0f},{10.0f,3.0f},
         {10.0f,9.0f},{10.0f,10.0f},{2.0f,10.0f}},
        {{3.0f,8.0f},{3.0f,2.0f},{11.0f,2.0f},
         {11.0f,8.0f},{11.0f,9.0f},{3.0f,9.0f}}
    },
    {
        {{3.75f,2.5f},{11.25f,2.5f},{11.25f,12.5f},
         {3.75f,12.5f},{2.5f,12.5f},{2.5f,2.5f}},
        {{5.0f,3.75f},{12.5f,3.75f},{12.5f,13.75f},
         {5.0f,13.75f},{3.75f,13.75f},{3.75f,3.75f}}
    }
};
static const float paint_points[2][5][4][2]={
    {
        {{4.0f,10.0f},{10.0f,10.0f},{10.0f,4.0f},{4.0f,4.0f}},
        {{5.0f,8.0f},{9.0f,8.0f},{9.0f,5.0f},{5.0f,5.0f}},
        {{6.0f,8.0f},{8.0f,8.0f},{8.0f,6.0f},{6.0f,6.0f}},
        {{7.0f,8.0f},{8.0f,8.0f},{8.0f,7.0f},{7.0f,7.0f}},
        {{12.0f,3.0f},{15.0f,3.0f},{15.0f,1.0f},{12.0f,1.0f}}
    },
    {
        {{2.5f,5.0f},{2.5f,12.5f},{10.0f,12.5f},{10.0f,5.0f}},
        {{5.0f,6.25f},{5.0f,11.25f},{8.75f,11.25f},{8.75f,6.25f}},
        {{5.0f,7.5f},{5.0f,10.0f},{7.5f,10.0f},{7.5f,7.5f}},
        {{5.0f,8.75f},{5.0f,10.0f},{6.25f,10.0f},{6.25f,8.75f}},
        {{11.25f,15.0f},{11.25f,18.75f},{13.75f,18.75f},{13.75f,15.0f}}
    }
};

static bool owned_graph(const quantapdf_ir& ir,size_t count,bool two,
                         bool restored,bool physical,bool rectangle_clip=false)
{
    CHECK(!rectangle_clip || (!two && !physical));
    CHECK(ir.pages!=nullptr && ir.page_count==1u &&
          ir.pages[0].width_points==(physical?15.0f:16.0f) &&
          ir.pages[0].height_points==(physical?20.0f:12.0f) &&
          ir.pages[0].background_argb==UINT32_C(0xffffffff));
    CHECK(ir.operations!=nullptr && ir.operation_count==count);
    const auto& r=ir.resources;
    CHECK(r.images==nullptr && r.image_count==0u && r.image_capacity==0u);
    CHECK(r.fonts==nullptr && r.font_count==0u && r.font_capacity==0u &&
          r.forms==nullptr && r.form_count==0u && r.form_capacity==0u &&
          r.soft_masks==nullptr && r.soft_mask_count==0u &&
          r.soft_mask_capacity==0u && r.paints==nullptr &&
          r.paint_count==0u && r.paint_capacity==0u &&
          ir.links==nullptr && ir.link_count==0u &&
          ir.outlines==nullptr && ir.outline_count==0u);
    CHECK(r.graphics_states!=nullptr && r.graphics_state_count==1u &&
          r.graphics_state_capacity==1u && r.clips!=nullptr &&
          r.clip_count==(two?3u:1u) && r.clip_capacity==r.clip_count);
    const auto& gs=r.graphics_states[0];
    CHECK(gs.fill_alpha==1.0f && gs.stroke_alpha==1.0f &&
          gs.blend_mode==QUANTAPDF_COMPOSER_BLEND_NORMAL &&
          gs.clip_id==(two?3u:1u) && gs.soft_mask_id==0u);
    const size_t leaves=two?2u:1u;
    for(size_t leaf=0u;leaf<leaves;++leaf) {
        const auto& clip=r.clips[leaf];
        CHECK(clip.kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
              clip.commands!=nullptr && clip.command_count==5u &&
              clip.members==nullptr && clip.member_count==0u &&
              identity(clip.transform) &&
              clip.fill_rule==(leaf==0u?QUANTAPDF_COMPOSER_FILL_NONZERO:
                                       QUANTAPDF_COMPOSER_FILL_EVEN_ODD));
        const auto *commands=clip.commands;
        CHECK(commands[0].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
              commands[2].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
              commands[3].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
              commands[4].kind==QUANTAPDF_COMPOSER_PATH_CLOSE);
        if(rectangle_clip) {
            const float points[4][2]={
                {2.0f,10.0f},{10.0f,10.0f},{10.0f,3.0f},{2.0f,3.0f}
            };
            CHECK(commands[1].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO);
            for(size_t p=0u;p<4u;++p)CHECK(point_is(commands[p].point1,points[p]));
        } else {
            CHECK(commands[1].kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO);
            const auto& points=clip_points[physical?1u:0u][leaf];
            CHECK(point_is(commands[0].point1,points[0]) &&
                  point_is(commands[1].point1,points[1]) &&
                  point_is(commands[1].point2,points[2]) &&
                  point_is(commands[1].point3,points[3]) &&
                  point_is(commands[2].point1,points[4]) &&
                  point_is(commands[3].point1,points[5]));
        }
    }
    if(two) {
        CHECK(r.clips[0].commands!=r.clips[1].commands);
        const auto& root=r.clips[2];
        CHECK(root.kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL &&
              root.commands==nullptr && root.command_count==0u &&
              root.members!=nullptr && root.member_count==2u &&
              root.members[0]==1u && root.members[1]==2u);
    }
    for(size_t i=0u;i<count;++i) {
        const auto& op=ir.operations[i];
        const bool outside=restored && i+1u==count;
        const size_t expected_gs=outside?0u:1u;
        CHECK(op.kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
              op.page_index==0u && op.graphics_state_id==expected_gs);
        const auto& path=op.value.path;
        CHECK(path.commands!=nullptr && path.command_count==5u &&
              path.dash_lengths==nullptr && path.dash_count==0u &&
              path.dash_phase==0.0f && path.options.fill==1 &&
              path.options.stroke==0 &&
              path.options.graphics_state_id==expected_gs &&
              path.options.fill_paint_id==0u &&
              path.options.stroke_paint_id==0u &&
              path.options.fill_argb==(outside?UINT32_C(0xff00ff00):
                                              path_colors[i]) &&
              path.options.fill_rule==(i%2u==0u?QUANTAPDF_COMPOSER_FILL_NONZERO:
                                              QUANTAPDF_COMPOSER_FILL_EVEN_ODD) &&
              cleared(path.options.transform));
        CHECK(path.commands[0].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
              path.commands[4].kind==QUANTAPDF_COMPOSER_PATH_CLOSE);
        const auto& points=paint_points[physical?1u:0u][outside?4u:i];
        for(size_t p=0u;p<4u;++p) {
            CHECK(point_is(path.commands[p].point1,points[p]));
            if(p!=0u)CHECK(path.commands[p].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO);
        }
        for(size_t leaf=0u;leaf<leaves;++leaf)
            CHECK(path.commands!=r.clips[leaf].commands);
        for(size_t earlier=0u;earlier<i;++earlier)
            CHECK(path.commands!=ir.operations[earlier].value.path.commands);
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

/* Every reachable descriptor, owner pointer, command and root member is
 * compared before/after the immutable consumer borrows the Source IR. */
static std::vector<unsigned char> snapshot(const quantapdf_ir& ir)
{
    std::vector<unsigned char> bytes;
    append_bytes(bytes,&ir,sizeof(ir));
    append_bytes(bytes,ir.pages,ir.page_count*sizeof(*ir.pages));
    append_bytes(bytes,ir.operations,ir.operation_count*sizeof(*ir.operations));
    const auto& r=ir.resources;
    append_bytes(bytes,r.graphics_states,
                 r.graphics_state_count*sizeof(*r.graphics_states));
    append_bytes(bytes,r.clips,r.clip_count*sizeof(*r.clips));
    for(size_t i=0u;i<r.clip_count;++i) {
        const auto& clip=r.clips[i];
        append_bytes(bytes,clip.commands,clip.command_count*sizeof(*clip.commands));
        append_bytes(bytes,clip.members,clip.member_count*sizeof(*clip.members));
    }
    for(size_t i=0u;i<ir.operation_count;++i) {
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

static bool detached_render(size_t count,bool two,bool restored,bool physical)
{
    quantapdf_ir ir{};
    {
        std::string pdf=original_pdf(page_content(count,two,restored),physical);
        CHECK(source(pdf,source_budget,ir)==QUANTAPDF_OK &&
              owned_graph(ir,count,two,restored,physical));
        /* QPDF fixture/Source owners and the original bytes retire before
         * the consumer may inspect any owned PATH/Clip payload. */
        pdf.assign(pdf.size(),'x');
    }
    const auto before=snapshot(ir);
    quantapdf_ir_raster_image first{},again{};
    CHECK(quantapdf_native_device_ir_render_v1_clipped_paths(
        &ir,96u,&raster_budget,&first)==QUANTAPDF_OK);
    CHECK(first.pixels!=nullptr && first.width==(physical?20u:22u) &&
          first.height==(physical?27u:16u) &&
          first.stride==first.width*4u && first.size==first.stride*first.height);
    /* Full-coverage blue survives both Clips; later paints start above
     * this strip. Green lies outside every Clip and requires actual Q. */
    CHECK(rgba_at(first,physical?5u:6u,physical?7u:11u)==UINT32_C(0x0000ffff));
    if(restored)
        CHECK(rgba_at(first,physical?16u:18u,physical?22u:2u)==
              UINT32_C(0x00ff00ff));
    CHECK(quantapdf_native_device_ir_render_v1_clipped_paths(
        &ir,96u,&raster_budget,&again)==QUANTAPDF_OK);
    CHECK(again.pixels!=first.pixels && again.width==first.width &&
          again.height==first.height && again.stride==first.stride &&
          again.size==first.size &&
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

static bool allocation_matrix(size_t count,bool two)
{
    const bool restored=count>1u;
    const std::string pdf=original_pdf(page_content(count,two,restored),true);
    quantapdf_ir clean{};
    begin_allocation_count(0u);
    const auto baseline=source(pdf,source_budget,clean);
    track_c_allocation=false;
    const size_t allocation_count=c_allocations;
    /* Page + operation descriptors + every PATH buffer + GS + Clip
     * descriptors + leaf buffers, plus owned root members for two leaves. */
    CHECK(baseline==QUANTAPDF_OK &&
          owned_graph(clean,count,two,restored,true) &&
          allocation_count==count+(two?7u:5u) && c_faults==0u);
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
                "clipped PATH Source paths=%zu two=%d allocation=%zu/%zu"
                " status=%d faults=%zu allocations=%zu cleared=%d\n",
                count,two?1:0,fail,allocation_count,static_cast<int>(status),
                c_faults,c_allocations,cleared(failed)?1:0);
            return false;
        }
        quantapdf_ir recovered{};
        CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
              owned_graph(recovered,count,two,restored,true));
        quantapdf_ir_dispose(&recovered);
        CHECK(cleared(recovered));
    }
    std::fprintf(stderr,"[native.clipped_path_source_oom] paths=%zu two=%d"
        " exhausted %zu C allocations\n",count,two?1:0,allocation_count);
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

static bool source_failures(bool two)
{
    const std::string content=page_content(4u,two,true);
    const std::string pdf=original_pdf(content,true);
    auto budget=source_budget;
    budget.max_pdf_bytes=pdf.size()-1u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_content_bytes=16u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_content_streams=1u;
    CHECK(atomic_rejection(original_pdf(content,true,2u,true),budget));
    budget=source_budget;budget.max_page_pixels=299u;
    CHECK(atomic_rejection(pdf,budget));
    for(size_t count=2u;count<=4u;++count) {
        budget=source_budget;budget.max_operations=count-1u;
        CHECK(atomic_rejection(original_pdf(page_content(count,two,true),true),budget));
    }
    /* These failures occur after all admitted PATHs have been staged.
     * Neither late Text nor an unqualified graphics state can publish IR. */
    CHECK(atomic_rejection(original_pdf(content+"BT ET",true),source_budget));
    CHECK(atomic_rejection(original_pdf(content+"/Unqualified gs",true),source_budget));
    /* Clip reentry cannot replace the single immutable GS identity. */
    CHECK(atomic_rejection(original_pdf(content+clip_prefix(false)+
        "0 0 1 rg 4 2 6 6 re f Q",true),source_budget));
    /* Every paint after Q would leave the Clip unused: first paint must
     * have belonged to the admitted clipped prefix. */
    CHECK(atomic_rejection(original_pdf(clip_prefix(two)+
        "Q 0 1 0 rg 12 9 3 2 re f",true),source_budget));
    quantapdf_ir recovered{};
    CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
          owned_graph(recovered,4u,two,true,true));
    quantapdf_ir_dispose(&recovered);
    CHECK(cleared(recovered));
    return true;
}

static bool older_source_refusal()
{
    using source_function=quantapdf_status (*)(const unsigned char*,size_t,
        const quantapdf_qpdf_source_raw_image_limits*,quantapdf_ir*);
    const source_function older[]={
        quantapdf_qpdf_source_scoped_mixed_v8_to_ir,
        quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir
    };
    /* Plain geometry, page zero and an all-LINE rectangular Clip ensure
     * older signatures refuse the new PATH-only cardinality, rather than
     * cubic Clip, Rotate/UserUnit or different-page restrictions. */
    for(size_t count=1u;count<=2u;++count) {
        const bool restored=count==2u;
        std::string content="q 2 2 8 7 re W n 0 0 1 rg 4 2 6 6 re f Q ";
        if(restored)content+="0 1 0 rg 12 9 3 2 re f* ";
        const std::string pdf=original_pdf(content,false,0u);
        for(source_function old_source:older) {
            quantapdf_ir ir;
            std::memset(&ir,0xA5,sizeof(ir));
            begin_allocation_count(0u);
            const auto status=old_source(
                reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),
                &source_budget,&ir);
            track_c_allocation=false;
            CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir) &&
                  c_allocations==0u && c_faults==0u);
        }
        quantapdf_ir admitted{};
        CHECK(source(pdf,source_budget,admitted,0u)==QUANTAPDF_OK &&
              owned_graph(admitted,count,false,restored,false,true));
        quantapdf_ir_dispose(&admitted);
        CHECK(cleared(admitted));
    }
    return true;
}

static bool run_tests()
{
    for(bool two:{false,true}) {
        for(size_t count=1u;count<=4u;++count) {
            for(bool physical:{false,true}) {
                CHECK(detached_render(count,two,false,physical));
                if(count>1u)CHECK(detached_render(count,two,true,physical));
            }
            CHECK(allocation_matrix(count,two));
        }
        CHECK(source_failures(two));
    }
    CHECK(older_source_refusal());
    return true;
}

int main()
{
    if(!run_tests())return EXIT_FAILURE;
    std::fprintf(stderr,"[native.clipped_path_source_oom] completed\n");
    return EXIT_SUCCESS;
}
