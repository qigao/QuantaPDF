/* Original-PDF bounded Clip-pair ownership and allocation rollback.
 * The EXACT production Source unit uses its existing isolated C/C++
 * allocation hooks. The existing private device bridge and C11 raster
 * consume detached immutable PATH/Image/Clip owners; no provider or
 * replacement Source implementation is introduced. */
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
#include <new>
#include <string>
#include <vector>

namespace {
thread_local bool track_c_allocation=false,stage_armed=false;
thread_local size_t c_fail_index=0u,c_allocations=0u,c_faults=0u;
thread_local unsigned stage_target=0u,stage_observed=0u,stage_faults=0u;
}

extern "C" void quantapdf_source_stage_oom_arm(unsigned phase) noexcept
{
    if(phase==stage_target) {
        stage_target=0u;
        stage_observed=phase;
        stage_armed=true;
    }
}
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

/* Production's compile-time-elided phase 8 arms immediately before an
 * actual capacity-growing canonical-empty vector assign. No assumption
 * is made about vendor-specific vector growth when closing a contour. */
void *operator new(std::size_t bytes)
{
    if(stage_armed) {
        stage_armed=false;
        ++stage_faults;
        throw std::bad_alloc();
    }
    if(void *p=std::malloc(bytes?bytes:1u))return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t bytes) {return ::operator new(bytes);}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete[](void *p) noexcept {std::free(p);}
void operator delete(void *p,std::size_t) noexcept {std::free(p);}
void operator delete[](void *p,std::size_t) noexcept {std::free(p);}

#define CHECK(x) do { if(!(x)) { \
    std::fprintf(stderr,"bounded Clip pair ownership:%d: %s\n",__LINE__,#x); \
    return false; \
} } while(0)

using Obj=QPDFObjectHandle;
enum class profile { paths,image,mixed };
enum class contour { polygon,open_polygon,empty,move,open_line,rectangle,open_eight };
struct clip_pair {contour first,second;};

static const quantapdf_qpdf_source_raw_image_limits source_budget={
    2u*1024u*1024u,65536u,4u,4u,1000000u,1u,4000000u,8u
};
static const quantapdf_ir_raster_scoped_mixed_v5_limits raster_budget={
    1024u,4u,512u,8000000u,1u,4096u
};

static bool empty_contour(contour shape)
{
    return shape==contour::empty || shape==contour::move ||
           shape==contour::open_line;
}
static bool reduced(const clip_pair& pair)
{
    return pair.first==contour::rectangle && pair.second==contour::rectangle;
}
static size_t operation_count(profile kind)
{
    return kind==profile::paths?2u:kind==profile::image?1u:3u;
}
static const char *contour_source(contour shape)
{
    switch(shape) {
    case contour::polygon:
        return "2 2 m 10 2 l 12 9 l 4 9 l h ";
    case contour::open_polygon:
        return "2 2 m 10 2 l 12 9 l 4 9 l ";
    case contour::empty:return "";
    case contour::move:return "5 4 m ";
    case contour::open_line:return "5 4 m 8 6 l ";
    case contour::rectangle:return "2 2 10 7 re ";
    case contour::open_eight:
        return "2 2 m 5 2 l 9 2 l 10 4 l 10 6 l 8 9 l 5 9 l 3 6 l ";
    }
    return "";
}

static std::string page_content(const clip_pair& pair,profile kind,
                                 bool one_leaf_control=false)
{
    std::string content="q ";
    content+=contour_source(pair.first);
    content+="W n ";
    if(!one_leaf_control) {
        /* Independent definition-time CTMs, restored before any paint.
         * Canonical empty geometry must not retain these authorial points. */
        content+="1 0 0 1 1 1 cm ";
        content+=contour_source(pair.second);
        content+="W* n 1 0 0 1 -1 -1 cm ";
    }
    if(kind!=profile::paths)
        content+="q 8 0 0 6 4 3 cm /I Do Q ";
    if(kind!=profile::image)
        content+="0 0 1 rg 4 3 7 5 re f* ";
    content+="Q ";
    if(kind!=profile::image)
        content+="0 1 0 rg 12 9 3 2 re f ";
    return content;
}

static std::string original_pdf(const std::string& content,profile kind,
                                 bool physical,size_t selected_index=2u)
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
    auto resources=Obj::newDictionary();
    if(kind!=profile::paths) {
        auto image=pdf->newStream(std::string("\xff\0\0",3));
        auto dict=image.getDict();
        dict.replaceKey("/Type",Obj::newName("/XObject"));
        dict.replaceKey("/Subtype",Obj::newName("/Image"));
        dict.replaceKey("/Width",Obj::newInteger(1));
        dict.replaceKey("/Height",Obj::newInteger(1));
        dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
        dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
        auto images=Obj::newDictionary();
        images.replaceKey("/I",image);
        resources.replaceKey("/XObject",images);
    }
    selected.replaceKey("/Resources",resources);
    selected.replaceKey("/Contents",pdf->newStream(content));
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
static bool identity(const quantapdf_affine_transform& t)
{
    return t.a==1.0f && t.b==0.0f && t.c==0.0f &&
           t.d==1.0f && t.e==0.0f && t.f==0.0f;
}
static bool point_is(const quantapdf_point& point,const float expected[2])
{
    return point.x==expected[0] && point.y==expected[1];
}

/* All expected geometry is authored independently of production. The
 * second leaf includes translation (1,1). The second outer group also
 * includes Rotate90 then UserUnit1.25, applied to BOTH frozen leaves. */
static const float polygon_points[2][2][4][2]={
    {{{2,10},{10,10},{12,3},{4,3}},{{3,9},{11,9},{13,2},{5,2}}},
    {{{2.5f,2.5f},{2.5f,12.5f},{11.25f,15},{11.25f,5}},
     {{3.75f,3.75f},{3.75f,13.75f},{12.5f,16.25f},{12.5f,6.25f}}}
};
static const float rectangle_points[2][2][4][2]={
    {{{2,10},{12,10},{12,3},{2,3}},{{3,9},{13,9},{13,2},{3,2}}},
    {{{2.5f,2.5f},{2.5f,15},{11.25f,15},{11.25f,2.5f}},
     {{3.75f,3.75f},{3.75f,16.25f},{12.5f,16.25f},{12.5f,3.75f}}}
};
static const float reduced_points[2][4][2]={
    {{3,3},{12,3},{12,9},{3,9}},
    {{11.25f,3.75f},{11.25f,15},{3.75f,15},{3.75f,3.75f}}
};
static const float eight_points[2][8][2]={
    {{3,9},{6,9},{10,9},{11,7},{11,5},{9,2},{6,2},{4,5}},
    {{3.75f,3.75f},{3.75f,7.5f},{3.75f,12.5f},{6.25f,13.75f},
     {8.75f,13.75f},{12.5f,11.25f},{12.5f,7.5f},{8.75f,5}}
};
static const float paint_points[2][2][4][2]={
    {{{4,9},{11,9},{11,4},{4,4}},{{12,3},{15,3},{15,1},{12,1}}},
    {{{3.75f,5},{3.75f,13.75f},{10,13.75f},{10,5}},
     {{11.25f,15},{11.25f,18.75f},{13.75f,18.75f},{13.75f,15}}}
};

static bool leaf_geometry(const quantapdf_composer_clip_state& clip,
    contour shape,size_t leaf,bool physical,bool is_reduced)
{
    const bool empty=empty_contour(shape) && !is_reduced;
    const size_t points=empty?3u:shape==contour::open_eight?8u:4u;
    CHECK(clip.kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
          clip.commands!=nullptr && clip.command_count==points+1u &&
          clip.members==nullptr && clip.member_count==0u &&
          identity(clip.transform) &&
          clip.fill_rule==(leaf==0u?QUANTAPDF_COMPOSER_FILL_NONZERO:
                                   QUANTAPDF_COMPOSER_FILL_EVEN_ODD));
    for(size_t i=0u;i<points;++i) {
        const auto& command=clip.commands[i];
        CHECK(command.kind==(i==0u?QUANTAPDF_COMPOSER_PATH_MOVE_TO:
                                  QUANTAPDF_COMPOSER_PATH_LINE_TO));
        if(empty) {
            /* Canonical empty points begin at Source(0,0), regardless
             * of original path/CTM, and undergo the normal physical
             * projection. Unused point fields must remain all zero. */
            const float expected[2]={physical?15.0f:0.0f,0.0f};
            CHECK(point_is(command.point1,expected));
        } else if(is_reduced) {
            CHECK(point_is(command.point1,reduced_points[physical?1u:0u][i]));
        } else if(shape==contour::open_eight) {
            CHECK(leaf==1u && point_is(command.point1,eight_points[physical?1u:0u][i]));
        } else if(shape==contour::rectangle) {
            CHECK(point_is(command.point1,rectangle_points[physical?1u:0u][leaf][i]));
        } else {
            CHECK(point_is(command.point1,polygon_points[physical?1u:0u][leaf][i]));
        }
        CHECK(cleared(command.point2) && cleared(command.point3));
    }
    const auto& close=clip.commands[points];
    CHECK(close.kind==QUANTAPDF_COMPOSER_PATH_CLOSE &&
          cleared(close.point1) && cleared(close.point2) && cleared(close.point3));
    return true;
}

static bool owned_graph(const quantapdf_ir& ir,const clip_pair& pair,
    profile kind,bool physical,bool one_leaf_control=false)
{
    const bool one=one_leaf_control || reduced(pair);
    const size_t leaves=one?1u:2u;
    CHECK(ir.pages!=nullptr && ir.page_count==1u &&
          ir.pages[0].width_points==(physical?15.0f:16.0f) &&
          ir.pages[0].height_points==(physical?20.0f:12.0f) &&
          ir.pages[0].background_argb==UINT32_C(0xffffffff));
    CHECK(ir.operations!=nullptr && ir.operation_count==operation_count(kind));
    const auto& r=ir.resources;
    CHECK(r.fonts==nullptr && r.font_count==0u && r.font_capacity==0u &&
          r.forms==nullptr && r.form_count==0u && r.form_capacity==0u &&
          r.paints==nullptr && r.paint_count==0u && r.paint_capacity==0u &&
          r.soft_masks==nullptr && r.soft_mask_count==0u &&
          r.soft_mask_capacity==0u && ir.links==nullptr &&
          ir.link_count==0u && ir.outlines==nullptr && ir.outline_count==0u);
    if(kind==profile::paths) {
        CHECK(r.images==nullptr && r.image_count==0u && r.image_capacity==0u);
    } else {
        CHECK(r.images!=nullptr && r.image_count==1u && r.image_capacity==1u);
        const auto& image=r.images[0];
        CHECK(image.format==QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW &&
              image.data!=nullptr && image.size==3u &&
              image.width==1u && image.height==1u && image.components==3 &&
              image.data[0]==255u && image.data[1]==0u && image.data[2]==0u &&
              image.alpha_data==nullptr && image.alpha_size==0u && image.has_alpha==0);
    }
    CHECK(r.clips!=nullptr && r.clip_count==(one?1u:3u) &&
          r.clip_capacity==r.clip_count && r.graphics_states!=nullptr &&
          r.graphics_state_count==1u && r.graphics_state_capacity==1u);
    const auto& gs=r.graphics_states[0];
    CHECK(gs.fill_alpha==1.0f && gs.stroke_alpha==1.0f &&
          gs.blend_mode==QUANTAPDF_COMPOSER_BLEND_NORMAL &&
          gs.clip_id==(one?1u:3u) && gs.soft_mask_id==0u);
    CHECK(leaf_geometry(r.clips[0],pair.first,0u,physical,
                        reduced(pair) && !one_leaf_control));
    if(!one) {
        CHECK(leaf_geometry(r.clips[1],pair.second,1u,physical,false));
        CHECK(r.clips[0].commands!=r.clips[1].commands);
        const auto& root=r.clips[2];
        CHECK(root.kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL &&
              root.commands==nullptr && root.command_count==0u &&
              root.members!=nullptr && root.member_count==2u &&
              root.members[0]==1u && root.members[1]==2u);
    }
    for(size_t i=0u;i<ir.operation_count;++i) {
        const auto& op=ir.operations[i];
        const bool image=kind!=profile::paths && i==0u;
        const bool outside=kind!=profile::image && i+1u==ir.operation_count;
        const size_t state=outside?0u:1u;
        CHECK(op.page_index==0u && op.graphics_state_id==state);
        if(image) {
            CHECK(op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
                  op.value.image.image_id==1u &&
                  op.value.image.options.graphics_state_id==1u &&
                  op.bounds.x0==(physical?3.75f:4.0f) &&
                  op.bounds.y0==(physical?5.0f:3.0f) &&
                  op.bounds.x1==(physical?11.25f:12.0f) &&
                  op.bounds.y1==(physical?15.0f:9.0f));
            continue;
        }
        CHECK(op.kind==QUANTAPDF_COMPOSER_OPERATION_PATH);
        const auto& path=op.value.path;
        CHECK(path.commands!=nullptr && path.command_count==5u &&
              path.dash_lengths==nullptr && path.dash_count==0u &&
              path.options.fill==1 && path.options.stroke==0 &&
              path.options.graphics_state_id==state &&
              path.options.fill_argb==(outside?UINT32_C(0xff00ff00):UINT32_C(0xff0000ff)) &&
              path.options.fill_rule==(outside?QUANTAPDF_COMPOSER_FILL_NONZERO:
                                               QUANTAPDF_COMPOSER_FILL_EVEN_ODD) &&
              cleared(path.options.transform));
        for(size_t p=0u;p<4u;++p)
            CHECK(path.commands[p].kind==(p==0u?QUANTAPDF_COMPOSER_PATH_MOVE_TO:
                                               QUANTAPDF_COMPOSER_PATH_LINE_TO) &&
                  point_is(path.commands[p].point1,paint_points[physical?1u:0u][outside?1u:0u][p]));
        CHECK(path.commands[4].kind==QUANTAPDF_COMPOSER_PATH_CLOSE);
        for(size_t leaf=0u;leaf<leaves;++leaf)CHECK(path.commands!=r.clips[leaf].commands);
        for(size_t earlier=0u;earlier<i;++earlier)
            if(ir.operations[earlier].kind==QUANTAPDF_COMPOSER_OPERATION_PATH)
                CHECK(path.commands!=ir.operations[earlier].value.path.commands);
    }
    return true;
}

static void append_bytes(std::vector<unsigned char>& bytes,const void *data,size_t size)
{
    if(size==0u)return;
    const auto *first=static_cast<const unsigned char*>(data);
    bytes.insert(bytes.end(),first,first+size);
}
static std::vector<unsigned char> snapshot(const quantapdf_ir& ir)
{
    std::vector<unsigned char> bytes;
    append_bytes(bytes,&ir,sizeof(ir));
    append_bytes(bytes,ir.pages,ir.page_count*sizeof(*ir.pages));
    append_bytes(bytes,ir.operations,ir.operation_count*sizeof(*ir.operations));
    const auto& r=ir.resources;
    append_bytes(bytes,r.images,r.image_count*sizeof(*r.images));
    for(size_t i=0u;i<r.image_count;++i)append_bytes(bytes,r.images[i].data,r.images[i].size);
    append_bytes(bytes,r.graphics_states,r.graphics_state_count*sizeof(*r.graphics_states));
    append_bytes(bytes,r.clips,r.clip_count*sizeof(*r.clips));
    for(size_t i=0u;i<r.clip_count;++i) {
        const auto& clip=r.clips[i];
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
    const auto *p=image.pixels+y*image.stride+x*4u;
    return (static_cast<uint32_t>(p[0])<<24u) |
           (static_cast<uint32_t>(p[1])<<16u) |
           (static_cast<uint32_t>(p[2])<<8u) | p[3];
}
static quantapdf_status render(const quantapdf_ir& ir,profile kind,
                                quantapdf_ir_raster_image& image)
{
    if(kind==profile::paths)
        return quantapdf_native_device_ir_render_v1_clipped_paths(
            &ir,96u,&raster_budget,&image);
    if(ir.resources.clip_count==3u)
        return quantapdf_native_device_ir_render_v1_two_clip(
            &ir,96u,&raster_budget,&image);
    return quantapdf_native_device_ir_render_v1(&ir,96u,&raster_budget,&image);
}

static bool detached_render(const clip_pair& pair,profile kind,bool physical)
{
    quantapdf_ir ir{};
    {
        std::string pdf=original_pdf(page_content(pair,kind),kind,physical);
        CHECK(source(pdf,source_budget,ir)==QUANTAPDF_OK &&
              owned_graph(ir,pair,kind,physical));
        pdf.assign(pdf.size(),'x');
    }
    /* Original bytes and all QPDF/staging owners have retired. Snapshot
     * every reachable descriptor, owner pointer, command and member ID. */
    const auto before=snapshot(ir);
    quantapdf_ir_raster_image first{},again{};
    CHECK(render(ir,kind,first)==QUANTAPDF_OK);
    CHECK(first.pixels!=nullptr && first.width==(physical?20u:22u) &&
          first.height==(physical?27u:16u) && first.stride==first.width*4u &&
          first.size==first.stride*first.height);
    const bool empty=empty_contour(pair.first) || empty_contour(pair.second);
    const uint32_t covered=empty?UINT32_C(0xffffffff):
        kind==profile::image?UINT32_C(0xff0000ff):UINT32_C(0x0000ffff);
    CHECK(rgba_at(first,physical?8u:9u,physical?11u:9u)==covered);
    if(kind!=profile::image)
        CHECK(rgba_at(first,physical?16u:18u,physical?22u:2u)==UINT32_C(0x00ff00ff));
    if(empty) {
        for(size_t i=0u;i<first.size;i+=4u) {
            const auto *p=first.pixels+i;
            /* Empty intersections suppress every clipped Image/PATH.
             * Only the restored green fill (with white AA) may remain. */
            CHECK(p[1]==255u && p[3]==255u && p[0]==p[2]);
            if(kind==profile::image)CHECK(p[0]==255u && p[2]==255u);
        }
    }
    CHECK(render(ir,kind,again)==QUANTAPDF_OK);
    CHECK(again.pixels!=first.pixels && again.width==first.width &&
          again.height==first.height && again.stride==first.stride &&
          again.size==first.size && std::memcmp(first.pixels,again.pixels,first.size)==0 &&
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
    c_fail_index=fail;c_allocations=0u;c_faults=0u;
    track_c_allocation=true;
}
static bool allocation_matrix(const clip_pair& pair,profile kind)
{
    const std::string pdf=original_pdf(page_content(pair,kind),kind,true);
    quantapdf_ir clean{};
    begin_allocation_count(0u);
    const auto baseline=source(pdf,source_budget,clean);
    track_c_allocation=false;
    const size_t allocations=c_allocations;
    CHECK(baseline==QUANTAPDF_OK && owned_graph(clean,pair,kind,true) &&
          allocations==(kind==profile::mixed?12u:9u) && c_faults==0u);
    quantapdf_ir_dispose(&clean);
    CHECK(cleared(clean));
    for(size_t fail=1u;fail<=allocations;++fail) {
        quantapdf_ir ir;
        std::memset(&ir,0xA5,sizeof(ir));
        begin_allocation_count(fail);
        const auto status=source(pdf,source_budget,ir);
        track_c_allocation=false;
        if(status!=QUANTAPDF_ERROR_NOMEM || !cleared(ir) ||
           c_faults!=1u || c_allocations!=fail) {
            std::fprintf(stderr,
                "bounded Clip pair profile=%u leaves=%u/%u allocation=%zu/%zu"
                " status=%d faults=%zu allocations=%zu cleared=%d\n",
                static_cast<unsigned>(kind),static_cast<unsigned>(pair.first),
                static_cast<unsigned>(pair.second),fail,allocations,
                static_cast<int>(status),c_faults,c_allocations,cleared(ir)?1:0);
            return false;
        }
        quantapdf_ir recovered{};
        CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
              owned_graph(recovered,pair,kind,true));
        quantapdf_ir_dispose(&recovered);
        CHECK(cleared(recovered));
    }
    std::fprintf(stderr,"[native.bounded_clip_pairs_oom] profile=%u leaves=%u/%u"
        " exhausted %zu C allocations\n",static_cast<unsigned>(kind),
        static_cast<unsigned>(pair.first),static_cast<unsigned>(pair.second),allocations);
    return true;
}

static bool canonical_empty_staging_failure()
{
    const clip_pair pair={contour::polygon,contour::empty};
    const std::string pdf=original_pdf(page_content(pair,profile::mixed),profile::mixed,true);
    quantapdf_ir ir;
    std::memset(&ir,0xA5,sizeof(ir));
    stage_target=8u;stage_observed=0u;stage_faults=0u;stage_armed=false;
    begin_allocation_count(0u);
    const auto status=source(pdf,source_budget,ir);
    track_c_allocation=false;
    const unsigned observed=stage_observed,faults=stage_faults;
    const bool remained_armed=stage_armed;
    stage_target=0u;stage_armed=false;
    if(status!=QUANTAPDF_ERROR_NOMEM || !cleared(ir) ||
       observed!=8u || faults!=1u || remained_armed ||
       c_allocations!=0u || c_faults!=0u) {
        std::fprintf(stderr,"bounded Clip pair stage8 status=%d observed=%u"
            " faults=%u armed=%d C allocations=%zu C faults=%zu cleared=%d\n",
            static_cast<int>(status),observed,faults,remained_armed?1:0,
            c_allocations,c_faults,cleared(ir)?1:0);
        return false;
    }
    quantapdf_ir recovered{};
    CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
          owned_graph(recovered,pair,profile::mixed,true));
    quantapdf_ir_dispose(&recovered);
    CHECK(cleared(recovered));
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
static bool source_failures(const clip_pair& pair)
{
    const std::string content=page_content(pair,profile::mixed);
    const std::string pdf=original_pdf(content,profile::mixed,true);
    auto budget=source_budget;
    budget.max_pdf_bytes=pdf.size()-1u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_content_bytes=16u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_page_pixels=299u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_operations=2u;
    CHECK(atomic_rejection(pdf,budget));
    /* Original 8x6 image fits. Physical ceil(7.5)x10 is billed in full,
     * even when an empty Clip makes all its pixels invisible. */
    budget=source_budget;budget.max_drawn_pixels=79u;
    CHECK(atomic_rejection(pdf,budget));
    CHECK(atomic_rejection(original_pdf(content+"BT ET",profile::mixed,true),source_budget));
    CHECK(atomic_rejection(original_pdf(content+"/Unqualified gs",profile::mixed,true),source_budget));
    CHECK(atomic_rejection(original_pdf(content+"q 2 2 8 7 re W n Q",profile::mixed,true),source_budget));
    quantapdf_ir recovered{};
    CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
          owned_graph(recovered,pair,profile::mixed,true));
    quantapdf_ir_dispose(&recovered);
    CHECK(cleared(recovered));
    return true;
}

static bool older_source_isolation()
{
    using source_function=quantapdf_status (*)(const unsigned char*,size_t,
        const quantapdf_qpdf_source_raw_image_limits*,quantapdf_ir*);
    const source_function older[]={quantapdf_qpdf_source_scoped_mixed_v8_to_ir,
        quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir};
    const clip_pair pair={contour::polygon,contour::polygon};
    /* Page zero, ordinary geometry, all-LINE contours, one Image/two
     * PATHs with actual Q restoration: the single-Clip control succeeds
     * in V8/V9. The otherwise identical two-Clip original must refuse. */
    const std::string control=original_pdf(
        page_content(pair,profile::mixed,true),profile::mixed,false,0u);
    const std::string pdf=original_pdf(
        page_content(pair,profile::mixed),profile::mixed,false,0u);
    for(source_function old_source:older) {
        quantapdf_ir admitted{};
        CHECK(old_source(reinterpret_cast<const unsigned char*>(control.data()),
            control.size(),&source_budget,&admitted)==QUANTAPDF_OK &&
            owned_graph(admitted,pair,profile::mixed,false,true));
        quantapdf_ir_dispose(&admitted);
        CHECK(cleared(admitted));
        quantapdf_ir refused;
        std::memset(&refused,0xA5,sizeof(refused));
        begin_allocation_count(0u);
        const auto status=old_source(reinterpret_cast<const unsigned char*>(pdf.data()),
            pdf.size(),&source_budget,&refused);
        track_c_allocation=false;
        CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && cleared(refused) &&
              c_allocations==0u && c_faults==0u);
    }
    quantapdf_ir accepted{};
    CHECK(source(pdf,source_budget,accepted,0u)==QUANTAPDF_OK &&
          owned_graph(accepted,pair,profile::mixed,false));
    quantapdf_ir_dispose(&accepted);
    CHECK(cleared(accepted));
    return true;
}

static bool run_tests()
{
    std::vector<clip_pair> pairs={
        {contour::polygon,contour::polygon},
        {contour::polygon,contour::open_polygon},
        {contour::polygon,contour::open_eight}
    };
    for(contour empty:{contour::empty,contour::move,contour::open_line}) {
        pairs.push_back({contour::polygon,empty});
        pairs.push_back({empty,contour::polygon});
        pairs.push_back({empty,empty});
    }
    pairs.push_back({contour::rectangle,contour::empty});
    pairs.push_back({contour::empty,contour::rectangle});
    pairs.push_back({contour::rectangle,contour::rectangle});
    for(const auto& pair:pairs)
        for(profile kind:{profile::paths,profile::image,profile::mixed})
            for(bool physical:{false,true}) {
                if(!detached_render(pair,kind,physical)) {
                    std::fprintf(stderr,"bounded Clip pair positive profile=%u"
                        " leaves=%u/%u physical=%d\n",static_cast<unsigned>(kind),
                        static_cast<unsigned>(pair.first),static_cast<unsigned>(pair.second),
                        physical?1:0);
                    return false;
                }
            }
    const clip_pair fault_pairs[]={
        {contour::polygon,contour::polygon},
        {contour::empty,contour::polygon},
        {contour::polygon,contour::open_line},
        {contour::move,contour::move}
    };
    for(const auto& pair:fault_pairs) {
        for(profile kind:{profile::paths,profile::image,profile::mixed})
            CHECK(allocation_matrix(pair,kind));
        CHECK(source_failures(pair));
    }
    CHECK(canonical_empty_staging_failure());
    CHECK(older_source_isolation());
    return true;
}

int main()
{
    if(!run_tests())return EXIT_FAILURE;
    std::fprintf(stderr,"[native.bounded_clip_pairs_oom] completed\n");
    return EXIT_SUCCESS;
}
