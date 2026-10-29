/* Original-PDF first-Clip/intersection/Q restoration ownership.
 * Compile the EXACT production Source unit with its existing isolated
 * C-allocation hooks. The canonical private device bridge/C11 raster
 * consumes detached immutable PATH/Image/Clip/GS owners. */
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
    std::fprintf(stderr,"Clip states Source ownership:%d: %s\n",__LINE__,#x); \
    return false; \
} } while(0)

using Obj=QPDFObjectHandle;
enum class shape { polygon,rectangle,empty_second };
enum class paint { red_path,blue_path,magenta_path,green_path,
                   red_image,cyan_image,cyan_small_image };
enum class graph { clip_states,root_only,first_only_control };
struct step {paint kind;unsigned state;};
struct scenario {
    const char *name;
    std::vector<step> steps;
    size_t allocations;
    uint32_t left,intersection,empty_intersection,outside;
    graph ownership;
};

static constexpr uint32_t white=UINT32_C(0xffffffff);
static constexpr uint32_t red=UINT32_C(0xff0000ff);
static constexpr uint32_t blue=UINT32_C(0x0000ffff);
static constexpr uint32_t cyan=UINT32_C(0x00ffffff);
static constexpr uint32_t magenta=UINT32_C(0xff00ffff);
static constexpr uint32_t green=UINT32_C(0x00ff00ff);

/* Pixel expectations below are authored for three interior probe points.
 * The left point belongs only to leaf 1, the middle point to both leaves,
 * and the green point lies outside either Clip. */
static const scenario cases[]={
    {"path2",{{paint::red_path,1u},{paint::blue_path,2u}},
        9u,red,blue,red,white,graph::clip_states},
    {"path3",{{paint::red_path,1u},{paint::blue_path,2u},{paint::magenta_path,1u}},
        10u,magenta,blue,red,white,graph::clip_states},
    {"path4",{{paint::red_path,1u},{paint::blue_path,2u},
        {paint::magenta_path,1u},{paint::green_path,0u}},
        11u,magenta,blue,red,green,graph::clip_states},
    {"path_two_root_paints",{{paint::red_path,1u},{paint::blue_path,2u},
        {paint::blue_path,2u},{paint::magenta_path,1u}},
        11u,magenta,blue,red,white,graph::clip_states},
    {"image12",{{paint::red_image,1u},{paint::cyan_image,2u}},
        10u,red,cyan,red,white,graph::clip_states},
    {"image21",{{paint::red_image,2u},{paint::cyan_small_image,1u}},
        10u,cyan,red,white,white,graph::clip_states},
    {"mixed2",{{paint::red_path,1u},{paint::cyan_image,2u}},
        11u,red,cyan,red,white,graph::clip_states},
    {"mixed3",{{paint::red_image,1u},{paint::blue_path,2u},{paint::magenta_path,1u}},
        12u,magenta,blue,red,white,graph::clip_states},
    {"mixed4",{{paint::red_image,1u},{paint::blue_path,2u},
        {paint::cyan_small_image,1u},{paint::green_path,0u}},
        13u,cyan,blue,red,green,graph::clip_states}
};
static const scenario root_only={"root_only",{{paint::cyan_image,2u}},
    9u,white,cyan,white,white,graph::root_only};

static const quantapdf_qpdf_source_raw_image_limits source_budget={
    2u*1024u*1024u,65536u,4u,4u,1000000u,2u,4000000u,8u
};
static const quantapdf_ir_raster_scoped_mixed_v5_limits raster_budget={
    1024u,4u,512u,8000000u,2u,4096u
};

static bool is_image(paint kind)
{
    return kind==paint::red_image || kind==paint::cyan_image ||
           kind==paint::cyan_small_image;
}
static size_t image_count(const scenario& sc)
{
    size_t count=0u;
    for(const auto& item:sc.steps)if(is_image(item.kind))++count;
    return count;
}
static const char *paint_source(paint kind)
{
    switch(kind) {
    case paint::red_path:return "1 0 0 rg 1 1 14 9 re f ";
    case paint::blue_path:return "0 0 1 rg 1 1 14 9 re f* ";
    case paint::magenta_path:return "1 0 1 rg 2 2 3 3 re f ";
    case paint::green_path:return "0 1 0 rg 12 9 3 2 re f ";
    case paint::red_image:return "q 14 0 0 9 1 1 cm /R Do Q ";
    case paint::cyan_image:return "q 14 0 0 9 1 1 cm /C Do Q ";
    case paint::cyan_small_image:return "q 3 0 0 3 2 2 cm /C Do Q ";
    }
    return "";
}
static const char *clip_source(shape geometry,bool second)
{
    if(geometry==shape::empty_second && second)return "";
    if(geometry==shape::rectangle)return "2 2 10 7 re ";
    return "2 2 m 10 2 l 12 9 l 4 9 l h ";
}

static std::string page_content(const scenario& sc,shape geometry,
                                 bool one_leaf_control=false)
{
    std::string content="q ";
    content+=clip_source(geometry,false);
    content+="W n ";
    size_t i=0u;
    while(i<sc.steps.size() && sc.steps[i].state==1u)
        content+=paint_source(sc.steps[i++].kind);
    /* This q saves the FIRST Clip and its CTM, not merely a bool that
     * some Clip is active. Reset the second definition's translated CTM
     * before painting, then Q must still restore the first Clip identity. */
    content+="q ";
    if(!one_leaf_control) {
        content+="1 0 0 1 3 0 cm ";
        content+=clip_source(geometry,true);
        content+="W* n 1 0 0 1 -3 0 cm ";
    }
    while(i<sc.steps.size() && sc.steps[i].state==2u)
        content+=paint_source(sc.steps[i++].kind);
    content+="Q ";
    while(i<sc.steps.size() && sc.steps[i].state==1u)
        content+=paint_source(sc.steps[i++].kind);
    content+="Q ";
    while(i<sc.steps.size())content+=paint_source(sc.steps[i++].kind);
    return content;
}

static std::string original_pdf(const std::string& content,const scenario& sc,
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
    auto resources=Obj::newDictionary(),images=Obj::newDictionary();
    bool need_red=false,need_cyan=false;
    for(const auto& item:sc.steps) {
        if(item.kind==paint::red_image)need_red=true;
        if(item.kind==paint::cyan_image || item.kind==paint::cyan_small_image)need_cyan=true;
    }
    for(bool red_image:{true,false}) {
        if((red_image && !need_red) || (!red_image && !need_cyan))continue;
        const std::string pixels=red_image?std::string("\xff\0\0",3):
                                            std::string("\0\xff\xff",3);
        auto image=pdf->newStream(pixels);
        auto dict=image.getDict();
        dict.replaceKey("/Type",Obj::newName("/XObject"));
        dict.replaceKey("/Subtype",Obj::newName("/Image"));
        dict.replaceKey("/Width",Obj::newInteger(1));
        dict.replaceKey("/Height",Obj::newInteger(1));
        dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
        dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
        images.replaceKey(red_image?"/R":"/C",image);
    }
    if(need_red || need_cyan)resources.replaceKey("/XObject",images);
    selected.replaceKey("/Resources",resources);
    selected.replaceKey("/Contents",pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(bytes->getBuffer()),bytes->getSize());
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
static bool point_is(const quantapdf_point& p,const float expected[2])
{
    return p.x==expected[0] && p.y==expected[1];
}

/* Authored coordinates: geometry/physical/leaf/vertex. Rectangle pairs
 * MUST retain original leaf 1 here; replacing it by an AABB intersection
 * would move its first vertex and corrupt pre-second/Q-restored paint. */
static const float clip_points[2][2][2][4][2]={
    {
        {{{2,10},{10,10},{12,3},{4,3}},{{5,10},{13,10},{15,3},{7,3}}},
        {{{2.5f,2.5f},{2.5f,12.5f},{11.25f,15},{11.25f,5}},
         {{2.5f,6.25f},{2.5f,16.25f},{11.25f,18.75f},{11.25f,8.75f}}}
    },
    {
        {{{2,10},{12,10},{12,3},{2,3}},{{5,10},{15,10},{15,3},{5,3}}},
        {{{2.5f,2.5f},{2.5f,15},{11.25f,15},{11.25f,2.5f}},
         {{2.5f,6.25f},{2.5f,18.75f},{11.25f,18.75f},{11.25f,6.25f}}}
    }
};
static const float paint_points[2][3][4][2]={
    {
        {{1,11},{15,11},{15,2},{1,2}},
        {{2,10},{5,10},{5,7},{2,7}},
        {{12,3},{15,3},{15,1},{12,1}}
    },
    {
        {{1.25f,1.25f},{1.25f,18.75f},{12.5f,18.75f},{12.5f,1.25f}},
        {{2.5f,2.5f},{2.5f,6.25f},{6.25f,6.25f},{6.25f,2.5f}},
        {{11.25f,15},{11.25f,18.75f},{13.75f,18.75f},{13.75f,15}}
    }
};
static const float image_bounds[2][2][4]={
    {{1,2,15,11},{2,7,5,10}},
    {{1.25f,1.25f,12.5f,18.75f},{2.5f,2.5f,6.25f,6.25f}}
};

static bool leaf_geometry(const quantapdf_composer_clip_state& clip,
                           shape geometry,size_t leaf,bool physical)
{
    const bool empty=geometry==shape::empty_second && leaf==1u;
    const size_t points=empty?3u:4u;
    CHECK(clip.kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
          clip.commands!=nullptr && clip.command_count==points+1u &&
          clip.members==nullptr && clip.member_count==0u && identity(clip.transform) &&
          clip.fill_rule==(leaf==0u?QUANTAPDF_COMPOSER_FILL_NONZERO:
                                   QUANTAPDF_COMPOSER_FILL_EVEN_ODD));
    for(size_t i=0u;i<points;++i) {
        const auto& cmd=clip.commands[i];
        CHECK(cmd.kind==(i==0u?QUANTAPDF_COMPOSER_PATH_MOVE_TO:
                              QUANTAPDF_COMPOSER_PATH_LINE_TO));
        if(empty) {
            const float expected[2]={physical?15.0f:0.0f,0.0f};
            CHECK(point_is(cmd.point1,expected));
        } else {
            CHECK(point_is(cmd.point1,
                clip_points[geometry==shape::rectangle?1u:0u][physical?1u:0u][leaf][i]));
        }
        CHECK(cleared(cmd.point2) && cleared(cmd.point3));
    }
    const auto& close=clip.commands[points];
    CHECK(close.kind==QUANTAPDF_COMPOSER_PATH_CLOSE &&
          cleared(close.point1) && cleared(close.point2) && cleared(close.point3));
    return true;
}

static bool owned_graph(const quantapdf_ir& ir,const scenario& sc,
                         shape geometry,bool physical)
{
    const bool first_only=sc.ownership==graph::first_only_control;
    const bool two_states=sc.ownership==graph::clip_states;
    const size_t images=image_count(sc),leaves=first_only?1u:2u;
    CHECK(ir.pages!=nullptr && ir.page_count==1u &&
          ir.pages[0].width_points==(physical?15.0f:16.0f) &&
          ir.pages[0].height_points==(physical?20.0f:12.0f) &&
          ir.pages[0].background_argb==UINT32_C(0xffffffff));
    CHECK(ir.operations!=nullptr && ir.operation_count==sc.steps.size());
    const auto& r=ir.resources;
    CHECK(r.image_count==images && r.image_capacity==images &&
          (images==0u?r.images==nullptr:r.images!=nullptr));
    CHECK(r.fonts==nullptr && r.font_count==0u && r.font_capacity==0u &&
          r.forms==nullptr && r.form_count==0u && r.form_capacity==0u &&
          r.paints==nullptr && r.paint_count==0u && r.paint_capacity==0u &&
          r.soft_masks==nullptr && r.soft_mask_count==0u && r.soft_mask_capacity==0u &&
          ir.links==nullptr && ir.link_count==0u && ir.outlines==nullptr && ir.outline_count==0u);
    CHECK(r.clips!=nullptr && r.clip_count==(first_only?1u:3u) &&
          r.clip_capacity==r.clip_count && r.graphics_states!=nullptr &&
          r.graphics_state_count==(two_states?2u:1u) &&
          r.graphics_state_capacity==r.graphics_state_count);
    for(size_t i=0u;i<r.graphics_state_count;++i) {
        const auto& gs=r.graphics_states[i];
        const size_t clip=first_only?1u:two_states?(i==0u?1u:3u):3u;
        CHECK(gs.fill_alpha==1.0f && gs.stroke_alpha==1.0f &&
              gs.blend_mode==QUANTAPDF_COMPOSER_BLEND_NORMAL &&
              gs.clip_id==clip && gs.soft_mask_id==0u);
    }
    CHECK(leaf_geometry(r.clips[0],geometry,0u,physical));
    if(!first_only) {
        CHECK(leaf_geometry(r.clips[1],geometry,1u,physical) &&
              r.clips[0].commands!=r.clips[1].commands);
        const auto& root=r.clips[2];
        CHECK(root.kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL &&
              root.commands==nullptr && root.command_count==0u &&
              root.members!=nullptr && root.member_count==2u &&
              root.members[0]==1u && root.members[1]==2u);
    }
    size_t image_index=0u;
    unsigned states_used=0u;
    for(size_t i=0u;i<sc.steps.size();++i) {
        const auto& authored=sc.steps[i];
        const auto& op=ir.operations[i];
        const size_t state=two_states?authored.state:(authored.state==0u?0u:1u);
        CHECK(op.page_index==0u && op.graphics_state_id==state);
        states_used|=1u<<static_cast<unsigned>(state);
        if(is_image(authored.kind)) {
            CHECK(op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
                  op.value.image.image_id==image_index+1u &&
                  op.value.image.options.struct_size==QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE &&
                  op.value.image.options.graphics_state_id==state);
            const auto& img=r.images[image_index];
            const bool red_image=authored.kind==paint::red_image;
            CHECK(img.format==QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW &&
                  img.data!=nullptr && img.size==3u && img.width==1u && img.height==1u &&
                  img.components==3 && img.has_alpha==0 && img.alpha_data==nullptr &&
                  img.alpha_size==0u && img.data[0]==(red_image?255u:0u) &&
                  img.data[1]==(red_image?0u:255u) && img.data[2]==(red_image?0u:255u));
            for(size_t j=0u;j<image_index;++j)CHECK(img.data!=r.images[j].data);
            const auto& bounds=image_bounds[physical?1u:0u]
                [authored.kind==paint::cyan_small_image?1u:0u];
            CHECK(op.bounds.x0==bounds[0] && op.bounds.y0==bounds[1] &&
                  op.bounds.x1==bounds[2] && op.bounds.y1==bounds[3]);
            ++image_index;
            continue;
        }
        CHECK(op.kind==QUANTAPDF_COMPOSER_OPERATION_PATH);
        const auto& path=op.value.path;
        const uint32_t color=authored.kind==paint::red_path?UINT32_C(0xffff0000):
            authored.kind==paint::blue_path?UINT32_C(0xff0000ff):
            authored.kind==paint::magenta_path?UINT32_C(0xffff00ff):UINT32_C(0xff00ff00);
        CHECK(path.commands!=nullptr && path.command_count==5u &&
              path.dash_lengths==nullptr && path.dash_count==0u && path.dash_phase==0.0f &&
              path.options.fill==1 && path.options.stroke==0 &&
              path.options.graphics_state_id==state && path.options.fill_argb==color &&
              path.options.fill_rule==(authored.kind==paint::blue_path?
                  QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO) &&
              path.options.fill_paint_id==0u && path.options.stroke_paint_id==0u &&
              cleared(path.options.transform));
        const size_t rectangle=authored.kind==paint::magenta_path?1u:
            authored.kind==paint::green_path?2u:0u;
        for(size_t p=0u;p<4u;++p)
            CHECK(path.commands[p].kind==(p==0u?QUANTAPDF_COMPOSER_PATH_MOVE_TO:
                                               QUANTAPDF_COMPOSER_PATH_LINE_TO) &&
                  point_is(path.commands[p].point1,paint_points[physical?1u:0u][rectangle][p]));
        CHECK(path.commands[4].kind==QUANTAPDF_COMPOSER_PATH_CLOSE);
        for(size_t leaf=0u;leaf<leaves;++leaf)CHECK(path.commands!=r.clips[leaf].commands);
        for(size_t j=0u;j<i;++j)
            if(ir.operations[j].kind==QUANTAPDF_COMPOSER_OPERATION_PATH)
                CHECK(path.commands!=ir.operations[j].value.path.commands);
    }
    CHECK(image_index==images);
    if(two_states)CHECK((states_used&6u)==6u);
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
static quantapdf_status render(const quantapdf_ir& ir,const scenario& sc,
                                quantapdf_ir_raster_image& image)
{
    if(sc.ownership==graph::root_only)
        return quantapdf_native_device_ir_render_v1_two_clip(&ir,96u,&raster_budget,&image);
    return quantapdf_native_device_ir_render_v1_clip_states(&ir,96u,&raster_budget,&image);
}

static bool detached_render(const scenario& sc,shape geometry,bool physical)
{
    quantapdf_ir ir{};
    {
        std::string pdf=original_pdf(page_content(sc,geometry),sc,physical);
        CHECK(source(pdf,source_budget,ir)==QUANTAPDF_OK && owned_graph(ir,sc,geometry,physical));
        pdf.assign(pdf.size(),'x');
    }
    const auto before=snapshot(ir);
    quantapdf_ir_raster_image first{},again{};
    CHECK(render(ir,sc,first)==QUANTAPDF_OK);
    CHECK(first.pixels!=nullptr && first.width==(physical?20u:22u) &&
          first.height==(physical?27u:16u) && first.stride==first.width*4u &&
          first.size==first.stride*first.height);
    CHECK(rgba_at(first,5u,physical?6u:11u)==sc.left);
    CHECK(rgba_at(first,10u,physical?13u:8u)==
          (geometry==shape::empty_second?sc.empty_intersection:sc.intersection));
    CHECK(rgba_at(first,physical?16u:18u,physical?22u:2u)==sc.outside);
    CHECK(render(ir,sc,again)==QUANTAPDF_OK);
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
static bool allocation_matrix(const scenario& sc)
{
    const std::string pdf=original_pdf(page_content(sc,shape::polygon),sc,true);
    quantapdf_ir clean{};
    begin_allocation_count(0u);
    const auto baseline=source(pdf,source_budget,clean);
    track_c_allocation=false;
    const size_t allocations=c_allocations;
    /* Two GS records use one real calloc, followed by three descriptors,
     * two independent leaf buffers, and the owned two-member root. */
    CHECK(baseline==QUANTAPDF_OK && owned_graph(clean,sc,shape::polygon,true) &&
          allocations==sc.allocations && c_faults==0u);
    quantapdf_ir_dispose(&clean);
    CHECK(cleared(clean));
    for(size_t fail=1u;fail<=allocations;++fail) {
        quantapdf_ir ir;
        std::memset(&ir,0xA5,sizeof(ir));
        begin_allocation_count(fail);
        const auto status=source(pdf,source_budget,ir);
        track_c_allocation=false;
        if(status!=QUANTAPDF_ERROR_NOMEM || !cleared(ir) || c_faults!=1u || c_allocations!=fail) {
            std::fprintf(stderr,"Clip states Source %s allocation=%zu/%zu status=%d"
                " faults=%zu allocations=%zu cleared=%d\n",sc.name,fail,allocations,
                static_cast<int>(status),c_faults,c_allocations,cleared(ir)?1:0);
            return false;
        }
        quantapdf_ir recovered{};
        CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
              owned_graph(recovered,sc,shape::polygon,true));
        quantapdf_ir_dispose(&recovered);
        CHECK(cleared(recovered));
    }
    std::fprintf(stderr,"[native.clip_states_source_oom] %s exhausted %zu C allocations\n",
        sc.name,allocations);
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
static bool source_failures(shape geometry)
{
    const auto& sc=cases[8];
    const std::string content=page_content(sc,geometry);
    const std::string pdf=original_pdf(content,sc,true);
    auto budget=source_budget;
    budget.max_pdf_bytes=pdf.size()-1u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_content_bytes=16u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_page_pixels=299u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_operations=3u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_source_pixels=1u;
    CHECK(atomic_rejection(pdf,budget));
    /* Both physically scaled Image placements are billed, including an
     * empty intersection: ceil(11.25)*ceil(17.5)+ceil(3.75)^2=232. */
    budget=source_budget;budget.max_drawn_pixels=231u;
    CHECK(atomic_rejection(pdf,budget));
    CHECK(atomic_rejection(original_pdf(content+"BT ET",sc,true),source_budget));
    CHECK(atomic_rejection(original_pdf(content+"/Unqualified gs",sc,true),source_budget));
    CHECK(atomic_rejection(original_pdf(content+"q 2 2 8 7 re W n Q",sc,true),source_budget));
    const scenario unused={"unused_intersection",{{paint::red_path,1u},{paint::magenta_path,1u}},
        0u,white,white,white,white,graph::clip_states};
    CHECK(atomic_rejection(original_pdf(page_content(unused,geometry),unused,true),source_budget));
    quantapdf_ir recovered{};
    CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK && owned_graph(recovered,sc,geometry,true));
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
    const auto& sc=cases[8];
    scenario one=sc;
    one.ownership=graph::first_only_control;
    /* Ordinary page zero, all-LINE contours, two Images/two PATHs and a
     * real outside-Q paint meet the existing V8/V9 mixed cardinalities.
     * Their one-Clip controls MUST succeed before second-Clip refusal. */
    const std::string control=original_pdf(page_content(sc,shape::polygon,true),sc,false,0u);
    const std::string pdf=original_pdf(page_content(sc,shape::polygon),sc,false,0u);
    for(source_function old_source:older) {
        quantapdf_ir admitted{};
        CHECK(old_source(reinterpret_cast<const unsigned char*>(control.data()),control.size(),
            &source_budget,&admitted)==QUANTAPDF_OK && owned_graph(admitted,one,shape::polygon,false));
        quantapdf_ir_dispose(&admitted);
        CHECK(cleared(admitted));
        quantapdf_ir refused;
        std::memset(&refused,0xA5,sizeof(refused));
        begin_allocation_count(0u);
        const auto status=old_source(reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),
            &source_budget,&refused);
        track_c_allocation=false;
        CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && cleared(refused) && c_allocations==0u && c_faults==0u);
    }
    quantapdf_ir accepted{};
    CHECK(source(pdf,source_budget,accepted,0u)==QUANTAPDF_OK && owned_graph(accepted,sc,shape::polygon,false));
    quantapdf_ir_dispose(&accepted);
    CHECK(cleared(accepted));
    return true;
}

static bool run_tests()
{
    for(shape geometry:{shape::polygon,shape::rectangle,shape::empty_second}) {
        for(const auto& sc:cases)
            for(bool physical:{false,true}) {
                if(!detached_render(sc,geometry,physical)) {
                    std::fprintf(stderr,"Clip states Source positive %s geometry=%u physical=%d\n",
                        sc.name,static_cast<unsigned>(geometry),physical?1:0);
                    return false;
                }
            }
        /* Nested q retains both original leaves even for rectangle pairs;
         * if no first-state paint occurred, Source publishes the previous
         * one-GS(root3) contract and the original two-Clip device consumes it. */
        for(bool physical:{false,true})CHECK(detached_render(root_only,geometry,physical));
        CHECK(source_failures(geometry));
    }
    for(const auto& sc:cases)CHECK(allocation_matrix(sc));
    CHECK(older_source_isolation());
    return true;
}

int main()
{
    if(!run_tests())return EXIT_FAILURE;
    std::fprintf(stderr,"[native.clip_states_source_oom] completed\n");
    return EXIT_SUCCESS;
}
