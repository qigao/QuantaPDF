/* Original-PDF compound Clip ownership and allocation rollback. Compile
 * the EXACT production Source unit with its existing isolated C hooks;
 * consume its detached owners through the canonical private C11 device.
 * Expected contour coordinates and interior colors are authored here,
 * independently of Source projection and the production mask builder. */
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
    std::fprintf(stderr,"compound Clip Source ownership:%d: %s\n",__LINE__,#x); \
    return false; \
} } while(0)

using Obj=QPDFObjectHandle;
using Command=quantapdf_composer_path_command;
enum class shape { triangles8,rectangle_triangle9,triangle_rectangle9,
                   curve_triangle8,degenerate8 };
enum class graph { single,root,states };
enum class profile { paths,image,mixed };
enum class paint { red_path,blue_path,magenta_path,green_path,
                   red_image,cyan_image,cyan_small_image };
struct step {paint kind;unsigned state;};
struct clip_pair {shape first,second;};
struct scenario {
    const char *name;
    graph ownership;
    profile kind;
    std::vector<step> steps;
    size_t allocations;
};

static constexpr uint32_t white=UINT32_C(0xffffffff);
static constexpr uint32_t red=UINT32_C(0xff0000ff);
static constexpr uint32_t blue=UINT32_C(0x0000ffff);
static constexpr uint32_t cyan=UINT32_C(0x00ffffff);
static constexpr uint32_t magenta=UINT32_C(0xff00ffff);
static constexpr uint32_t green=UINT32_C(0x00ff00ff);

/* The one-GS mixed originals retain a real restored GS0 suffix. Their
 * original three-paint cardinality was already admitted before compounds.
 * The two-GS profiles paint both identities and restore first then none. */
static const scenario cases[]={
    {"single_path",graph::single,profile::paths,
        {{paint::blue_path,1u},{paint::green_path,0u}},7u},
    {"single_image",graph::single,profile::image,
        {{paint::red_image,1u}},7u},
    {"single_mixed",graph::single,profile::mixed,
        {{paint::red_image,1u},{paint::blue_path,1u},{paint::green_path,0u}},10u},
    {"root_path",graph::root,profile::paths,
        {{paint::blue_path,2u},{paint::green_path,0u}},9u},
    {"root_image",graph::root,profile::image,
        {{paint::red_image,2u}},9u},
    {"root_mixed",graph::root,profile::mixed,
        {{paint::red_image,2u},{paint::blue_path,2u},{paint::green_path,0u}},12u},
    {"states_path",graph::states,profile::paths,
        {{paint::red_path,1u},{paint::blue_path,2u},
         {paint::magenta_path,1u},{paint::green_path,0u}},11u},
    {"states_image",graph::states,profile::image,
        {{paint::red_image,1u},{paint::cyan_image,2u}},10u},
    {"states_mixed",graph::states,profile::mixed,
        {{paint::red_image,1u},{paint::blue_path,2u},
         {paint::cyan_small_image,1u},{paint::green_path,0u}},13u}
};
static const clip_pair pairs[]={
    {shape::triangles8,shape::rectangle_triangle9},
    {shape::rectangle_triangle9,shape::triangles8},
    {shape::triangle_rectangle9,shape::curve_triangle8},
    {shape::curve_triangle8,shape::triangle_rectangle9},
    {shape::triangles8,shape::curve_triangle8},
    {shape::rectangle_triangle9,shape::triangle_rectangle9},
    {shape::degenerate8,shape::rectangle_triangle9},
    {shape::rectangle_triangle9,shape::degenerate8}
};

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
    case paint::magenta_path:return "1 0 1 rg 2 4 2 2 re f ";
    case paint::green_path:return "0 1 0 rg 12 9 3 2 re f ";
    case paint::red_image:return "q 14 0 0 9 1 1 cm /R Do Q ";
    case paint::cyan_image:return "q 14 0 0 9 1 1 cm /C Do Q ";
    case paint::cyan_small_image:return "q 2 0 0 2 2 4 cm /C Do Q ";
    }
    return "";
}
static std::string contour_source(shape geometry,bool implicit_last,
                                  bool single_contour=false)
{
    std::string content;
    switch(geometry) {
    case shape::triangles8:
    case shape::triangle_rectangle9:
        content="2 2 m 12 2 l 2 10 l h ";break;
    case shape::rectangle_triangle9:
        content="2 2 10 7 re ";break;
    case shape::curve_triangle8:
        content="2 2 m 12 2 l 12 10 2 10 2 2 c h ";break;
    case shape::degenerate8:
        content="3 4 m 3 4 l 3 4 l h ";break;
    }
    if(single_contour)return content;
    if(geometry==shape::triangle_rectangle9)
        return content+"4 3 3 2 re ";
    content+=geometry==shape::degenerate8 ?
        "6 7 m 6 7 l 6 7 l " : "4 3 m 7 3 l 4 6 l ";
    if(!implicit_last)content+="h ";
    return content;
}
static std::string page_content(const scenario& sc,const clip_pair& pair,
    unsigned rules,bool implicit_last,bool single_contour=false)
{
    std::string content="q ";
    content+=contour_source(pair.first,implicit_last,single_contour);
    content+=(rules&1u)!=0u?"W* n ":"W n ";
    size_t i=0u;
    while(i<sc.steps.size() && sc.steps[i].state==1u)
        content+=paint_source(sc.steps[i++].kind);
    if(sc.ownership!=graph::single) {
        /* The two-GS version saves the first Clip before defining its
         * intersection; both definition-time CTMs must remain frozen. */
        if(sc.ownership==graph::states)content+="q ";
        content+="1 0 0 1 2 0 cm ";
        content+=contour_source(pair.second,implicit_last);
        content+=(rules&2u)!=0u?"W* n ":"W n ";
        content+="1 0 0 1 -2 0 cm ";
        while(i<sc.steps.size() && sc.steps[i].state==2u)
            content+=paint_source(sc.steps[i++].kind);
        if(sc.ownership==graph::states) {
            content+="Q ";
            while(i<sc.steps.size() && sc.steps[i].state==1u)
                content+=paint_source(sc.steps[i++].kind);
        }
    }
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

/* Authored tables: physical/leaf/vertex/xy. Leaf 2 is defined at a +2
 * source-x CTM, then the CTM is reset before paint. The physical table
 * independently records Rotate90 and UserUnit1.25, including all cubic
 * controls. CLOSE commands have no geometry and retain zero fields. */
static const float outer_triangle[2][2][3][2]={
    {{{2,10},{12,10},{2,2}},{{4,10},{14,10},{4,2}}},
    {{{2.5f,2.5f},{2.5f,15},{12.5f,2.5f}},
     {{2.5f,5},{2.5f,17.5f},{12.5f,5}}}
};
static const float outer_rectangle[2][2][4][2]={
    {{{2,10},{12,10},{12,3},{2,3}},{{4,10},{14,10},{14,3},{4,3}}},
    {{{2.5f,2.5f},{2.5f,15},{11.25f,15},{11.25f,2.5f}},
     {{2.5f,5},{2.5f,17.5f},{11.25f,17.5f},{11.25f,5}}}
};
static const float outer_curve[2][2][5][2]={
    {{{2,10},{12,10},{12,2},{2,2},{2,10}},
     {{4,10},{14,10},{14,2},{4,2},{4,10}}},
    {{{2.5f,2.5f},{2.5f,15},{12.5f,15},{12.5f,2.5f},{2.5f,2.5f}},
     {{2.5f,5},{2.5f,17.5f},{12.5f,17.5f},{12.5f,5},{2.5f,5}}}
};
static const float inner_triangle[2][2][3][2]={
    {{{4,9},{7,9},{4,6}},{{6,9},{9,9},{6,6}}},
    {{{3.75f,5},{3.75f,8.75f},{7.5f,5}},
     {{3.75f,7.5f},{3.75f,11.25f},{7.5f,7.5f}}}
};
static const float inner_rectangle[2][2][4][2]={
    {{{4,9},{7,9},{7,7},{4,7}},{{6,9},{9,9},{9,7},{6,7}}},
    {{{3.75f,5},{3.75f,8.75f},{6.25f,8.75f},{6.25f,5}},
     {{3.75f,7.5f},{3.75f,11.25f},{6.25f,11.25f},{6.25f,7.5f}}}
};
static const float degenerate_points[2][2][2][2]={
    {{{3,8},{6,5}},{{5,8},{8,5}}},
    {{{5,3.75f},{8.75f,7.5f}},{{5,6.25f},{8.75f,10}}}
};
static const float paint_points[2][3][4][2]={
    {{{1,11},{15,11},{15,2},{1,2}},
     {{2,8},{4,8},{4,6},{2,6}},
     {{12,3},{15,3},{15,1},{12,1}}},
    {{{1.25f,1.25f},{1.25f,18.75f},{12.5f,18.75f},{12.5f,1.25f}},
     {{5,2.5f},{5,5},{7.5f,5},{7.5f,2.5f}},
     {{11.25f,15},{11.25f,18.75f},{13.75f,18.75f},{13.75f,15}}}
};
static const float image_bounds[2][2][4]={
    {{1,2,15,11},{2,6,4,8}},
    {{1.25f,1.25f,12.5f,18.75f},{5,2.5f,7.5f,5}}
};

static void add_close(std::vector<Command>& out)
{
    Command command{};
    command.kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    out.push_back(command);
}
static void add_lines(std::vector<Command>& out,const float (*points)[2],size_t count)
{
    for(size_t i=0u;i<count;++i) {
        Command command{};
        command.kind=i==0u?QUANTAPDF_COMPOSER_PATH_MOVE_TO:
                          QUANTAPDF_COMPOSER_PATH_LINE_TO;
        command.point1={points[i][0],points[i][1]};
        out.push_back(command);
    }
    add_close(out);
}
static std::vector<Command> expected_leaf(shape geometry,size_t leaf,
    bool physical,bool single_contour=false)
{
    const size_t p=physical?1u:0u;
    std::vector<Command> out;
    if(geometry==shape::degenerate8) {
        for(size_t contour=0u;contour<(single_contour?1u:2u);++contour) {
            const auto& point=degenerate_points[p][leaf][contour];
            const float repeated[3][2]={{point[0],point[1]},
                {point[0],point[1]},{point[0],point[1]}};
            add_lines(out,repeated,3u);
        }
        return out;
    }
    if(geometry==shape::rectangle_triangle9)
        add_lines(out,outer_rectangle[p][leaf],4u);
    else if(geometry==shape::curve_triangle8) {
        const auto& points=outer_curve[p][leaf];
        for(size_t i=0u;i<2u;++i) {
            Command command{};
            command.kind=i==0u?QUANTAPDF_COMPOSER_PATH_MOVE_TO:
                              QUANTAPDF_COMPOSER_PATH_LINE_TO;
            command.point1={points[i][0],points[i][1]};
            out.push_back(command);
        }
        Command cubic{};
        cubic.kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
        cubic.point1={points[2][0],points[2][1]};
        cubic.point2={points[3][0],points[3][1]};
        cubic.point3={points[4][0],points[4][1]};
        out.push_back(cubic);
        add_close(out);
    } else add_lines(out,outer_triangle[p][leaf],3u);
    if(!single_contour) {
        if(geometry==shape::triangle_rectangle9)
            add_lines(out,inner_rectangle[p][leaf],4u);
        else add_lines(out,inner_triangle[p][leaf],3u);
    }
    return out;
}
static bool same_command(const Command& actual,const Command& expected)
{
    return actual.kind==expected.kind &&
        actual.point1.x==expected.point1.x && actual.point1.y==expected.point1.y &&
        actual.point2.x==expected.point2.x && actual.point2.y==expected.point2.y &&
        actual.point3.x==expected.point3.x && actual.point3.y==expected.point3.y;
}
static bool leaf_geometry(const quantapdf_composer_clip_state& clip,
    shape geometry,size_t leaf,bool physical,unsigned rules,bool single_contour)
{
    const auto expected=expected_leaf(geometry,leaf,physical,single_contour);
    CHECK(clip.kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
          clip.commands!=nullptr && clip.command_count==expected.size() &&
          clip.members==nullptr && clip.member_count==0u && identity(clip.transform) &&
          clip.fill_rule==(((rules>>leaf)&1u)!=0u?QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
                                                        QUANTAPDF_COMPOSER_FILL_NONZERO));
    if(!single_contour)CHECK(clip.command_count==8u || clip.command_count==9u);
    for(size_t i=0u;i<expected.size();++i)CHECK(same_command(clip.commands[i],expected[i]));
    return true;
}

static bool owned_graph(const quantapdf_ir& ir,const scenario& sc,
    const clip_pair& pair,bool physical,unsigned rules,bool single_contour=false)
{
    const bool single=sc.ownership==graph::single,two_states=sc.ownership==graph::states;
    const size_t images=image_count(sc),leaves=single?1u:2u;
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
    CHECK(r.clips!=nullptr && r.clip_count==(single?1u:3u) &&
          r.clip_capacity==r.clip_count && r.graphics_states!=nullptr &&
          r.graphics_state_count==(two_states?2u:1u) &&
          r.graphics_state_capacity==r.graphics_state_count);
    for(size_t i=0u;i<r.graphics_state_count;++i) {
        const auto& gs=r.graphics_states[i];
        const size_t clip=single?1u:two_states?(i==0u?1u:3u):3u;
        CHECK(gs.fill_alpha==1.0f && gs.stroke_alpha==1.0f &&
              gs.blend_mode==QUANTAPDF_COMPOSER_BLEND_NORMAL &&
              gs.clip_id==clip && gs.soft_mask_id==0u);
    }
    CHECK(leaf_geometry(r.clips[0],pair.first,0u,physical,rules,single_contour));
    if(!single) {
        CHECK(leaf_geometry(r.clips[1],pair.second,1u,physical,rules,false) &&
              r.clips[0].commands!=r.clips[1].commands);
        const auto& root=r.clips[2];
        CHECK(root.kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL &&
              root.commands==nullptr && root.command_count==0u &&
              root.members!=nullptr && root.member_count==2u &&
              root.members[0]==1u && root.members[1]==2u && cleared(root.transform));
        for(size_t leaf=0u;leaf<leaves;++leaf)
            CHECK(static_cast<const void*>(root.members)!=r.clips[leaf].commands);
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
                  op.value.image.options.fit==QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH &&
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
              path.options.struct_size==QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE &&
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
                  point_is(path.commands[p].point1,paint_points[physical?1u:0u][rectangle][p]) &&
                  cleared(path.commands[p].point2) && cleared(path.commands[p].point3));
        CHECK(path.commands[4].kind==QUANTAPDF_COMPOSER_PATH_CLOSE &&
              cleared(path.commands[4].point1) && cleared(path.commands[4].point2) &&
              cleared(path.commands[4].point3));
        for(size_t leaf=0u;leaf<leaves;++leaf)CHECK(path.commands!=r.clips[leaf].commands);
        if(!single)CHECK(static_cast<const void*>(path.commands)!=r.clips[2].members);
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
    if(sc.ownership==graph::states)
        return quantapdf_native_device_ir_render_v1_compound_clip_states(
            &ir,96u,&raster_budget,&image);
    if(sc.kind==profile::paths)
        return quantapdf_native_device_ir_render_v1_clipped_paths(&ir,96u,&raster_budget,&image);
    if(sc.ownership==graph::root)
        return quantapdf_native_device_ir_render_v1_two_clip(&ir,96u,&raster_budget,&image);
    return quantapdf_native_device_ir_render_v1(&ir,96u,&raster_budget,&image);
}

/* Five independently selected pixel interiors: left-only/restored small
 * paint, intersection outside either hole, first-hole-only, second-hole-
 * only, and unclipped green. All sixteen subpixel samples at each probe
 * have the same membership; even-odd therefore changes an exact color. */
static const size_t probes[2][5][2]={
    {{4u,9u},{6u,12u},{6u,10u},{10u,11u},{18u,2u}},
    {{8u,5u},{4u,8u},{6u,8u},{5u,13u},{16u,22u}}
};
static uint32_t expected_color(const scenario& sc,const clip_pair& pair,
                               unsigned rules,size_t probe)
{
    if(probe==4u)return sc.kind==profile::image?white:green;
    const bool first=pair.first!=shape::degenerate8 &&
        !(probe==2u && (rules&1u)!=0u);
    const bool second=probe!=0u && pair.second!=shape::degenerate8 &&
        !(probe==3u && (rules&2u)!=0u);
    if(!first)return white;
    if(sc.ownership==graph::single)return sc.kind==profile::image?red:blue;
    if(sc.ownership==graph::root)
        return second?(sc.kind==profile::image?red:blue):white;
    if(probe==0u) {
        if(sc.kind==profile::paths)return magenta;
        if(sc.kind==profile::mixed)return cyan;
        return red;
    }
    return second?(sc.kind==profile::image?cyan:blue):red;
}
static bool detached_render(const scenario& sc,const clip_pair& pair,
                            bool physical,unsigned rules)
{
    quantapdf_ir ir{};
    {
        std::string pdf=original_pdf(page_content(sc,pair,rules,physical),sc,physical);
        CHECK(source(pdf,source_budget,ir)==QUANTAPDF_OK &&
              owned_graph(ir,sc,pair,physical,rules));
        pdf.assign(pdf.size(),'x');
    }
    const auto before=snapshot(ir);
    if(sc.ownership==graph::states) {
        quantapdf_ir_raster_image refused;
        std::memset(&refused,0xA5,sizeof(refused));
        CHECK(quantapdf_native_device_ir_render_v1_clip_states(
            &ir,96u,&raster_budget,&refused)==QUANTAPDF_ERROR_UNSUPPORTED && cleared(refused));
    }
    quantapdf_ir_raster_image first{},again{};
    CHECK(render(ir,sc,first)==QUANTAPDF_OK);
    CHECK(first.pixels!=nullptr && first.width==(physical?20u:22u) &&
          first.height==(physical?27u:16u) && first.stride==first.width*4u &&
          first.size==first.stride*first.height);
    for(size_t probe=0u;probe<5u;++probe) {
        const auto& point=probes[physical?1u:0u][probe];
        const uint32_t expected=expected_color(sc,pair,rules,probe);
        if(rgba_at(first,point[0],point[1])!=expected) {
            std::fprintf(stderr,"compound Clip Source %s probe=%zu expected=%08x actual=%08x\n",
                sc.name,probe,static_cast<unsigned>(expected),
                static_cast<unsigned>(rgba_at(first,point[0],point[1])));
            return false;
        }
    }
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
static bool allocation_matrix(const scenario& sc,const clip_pair& pair)
{
    const unsigned rules=3u;
    const std::string pdf=original_pdf(page_content(sc,pair,rules,true),sc,true);
    quantapdf_ir clean{};
    begin_allocation_count(0u);
    const auto baseline=source(pdf,source_budget,clean);
    track_c_allocation=false;
    const size_t allocations=c_allocations;
    /* The same real allocation sites own 8/9 commands. A two-leaf graph
     * has separately allocated leaves and root members; two GS records
     * still use one calloc. Every image and painted PATH owner is billed. */
    CHECK(baseline==QUANTAPDF_OK && owned_graph(clean,sc,pair,true,rules) &&
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
            std::fprintf(stderr,"compound Clip Source %s allocation=%zu/%zu status=%d"
                " faults=%zu allocations=%zu cleared=%d\n",sc.name,fail,allocations,
                static_cast<int>(status),c_faults,c_allocations,cleared(ir)?1:0);
            return false;
        }
        quantapdf_ir recovered{};
        CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
              owned_graph(recovered,sc,pair,true,rules));
        quantapdf_ir_dispose(&recovered);
        CHECK(cleared(recovered));
    }
    std::fprintf(stderr,"[native.compound_clip_source_oom] %s exhausted %zu C allocations\n",
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
static bool source_failures(const scenario& sc)
{
    const auto& pair=pairs[5];
    const std::string content=page_content(sc,pair,3u,true);
    const std::string pdf=original_pdf(content,sc,true);
    auto budget=source_budget;
    budget.max_pdf_bytes=pdf.size()-1u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_content_bytes=16u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_page_pixels=299u;
    CHECK(atomic_rejection(pdf,budget));
    budget=source_budget;budget.max_operations=sc.steps.size()-1u;
    CHECK(atomic_rejection(pdf,budget));
    /* Image placements are charged before clipping, including holes:
     * ceil(11.25)*ceil(17.5)=216; restored small Image adds ceil(2.5)^2. */
    budget=source_budget;
    budget.max_drawn_pixels=sc.ownership==graph::states?224u:215u;
    CHECK(atomic_rejection(pdf,budget));
    if(sc.ownership==graph::states) {
        budget=source_budget;budget.max_source_pixels=1u;
        CHECK(atomic_rejection(pdf,budget));
    }
    CHECK(atomic_rejection(original_pdf(content+"BT ET",sc,true),source_budget));
    CHECK(atomic_rejection(original_pdf(content+"/Unqualified gs",sc,true),source_budget));
    quantapdf_ir recovered{};
    CHECK(source(pdf,source_budget,recovered)==QUANTAPDF_OK &&
          owned_graph(recovered,sc,pair,true,3u));
    quantapdf_ir_dispose(&recovered);
    CHECK(cleared(recovered));
    return true;
}
static bool malformed_failures()
{
    const char *invalid[]={
        /* Starting contour 2 requires an explicitly closed contour 1. */
        "2 2 m 12 2 l 2 10 l 4 3 m 7 3 l 4 6 l h W n",
        /* Each compound contour independently needs two actual edges. */
        "2 2 m 12 2 l 2 10 l h 4 3 m 7 3 l h W n",
        "2 2 m 12 2 l 2 10 l h 4 3 m W n",
        "2 2 m 12 2 l 2 10 l h 4 3 m 7 3 l W n",
        /* Two re contours spend ten commands, beyond the shared nine. */
        "2 2 10 7 re 4 3 3 2 re W n",
        "2 2 m 12 2 l 2 10 l h 4 3 m 7 3 l 4 6 l h "
            "8 3 m 8 4 l 9 3 l h W n",
        /* Compound geometry is only a Clip admission, not painted PATH
         * or a plain-n path-discard extension. */
        "2 2 10 7 re 4 3 m 7 3 l 4 6 l h f",
        "2 2 10 7 re 4 3 m 7 3 l 4 6 l h n"
    };
    for(const char *content:invalid)
        CHECK(atomic_rejection(original_pdf(content,cases[0],false),source_budget));
    return true;
}

static bool older_source_isolation()
{
    using source_function=quantapdf_status (*)(const unsigned char*,size_t,
        const quantapdf_qpdf_source_raw_image_limits*,quantapdf_ir*);
    const source_function older[]={quantapdf_qpdf_source_scoped_mixed_v8_to_ir,
        quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir};
    const auto& sc=cases[2];
    const auto& pair=pairs[1];
    /* Ordinary page zero; one Image, two PATHs, one real restored GS0
     * suffix. The control has only the same outer re. Adding its second
     * all-LINE contour is the sole newly qualified feature. */
    const std::string control=original_pdf(page_content(sc,pair,1u,false,true),sc,false,0u);
    const std::string compound=original_pdf(page_content(sc,pair,1u,false),sc,false,0u);
    for(source_function old_source:older) {
        quantapdf_ir admitted{};
        CHECK(old_source(reinterpret_cast<const unsigned char*>(control.data()),control.size(),
            &source_budget,&admitted)==QUANTAPDF_OK && owned_graph(admitted,sc,pair,false,1u,true));
        quantapdf_ir_dispose(&admitted);
        CHECK(cleared(admitted));
        quantapdf_ir refused;
        std::memset(&refused,0xA5,sizeof(refused));
        begin_allocation_count(0u);
        const auto status=old_source(reinterpret_cast<const unsigned char*>(compound.data()),
            compound.size(),&source_budget,&refused);
        track_c_allocation=false;
        CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && cleared(refused) &&
              c_allocations==0u && c_faults==0u);
    }
    quantapdf_ir accepted{};
    CHECK(source(compound,source_budget,accepted,0u)==QUANTAPDF_OK &&
          owned_graph(accepted,sc,pair,false,1u));
    quantapdf_ir_dispose(&accepted);
    CHECK(cleared(accepted));
    return true;
}

static bool run_tests()
{
    size_t positive_count=0u,fault_count=0u;
    for(size_t pair=0u;pair<sizeof(pairs)/sizeof(pairs[0]);++pair)
        for(size_t scenario_index=0u;scenario_index<sizeof(cases)/sizeof(cases[0]);++scenario_index)
            for(bool physical:{false,true}) {
                const unsigned rules=static_cast<unsigned>((pair+scenario_index)%4u);
                if(!detached_render(cases[scenario_index],pairs[pair],physical,rules)) {
                    std::fprintf(stderr,"compound Clip Source %s pair=%zu physical=%d rules=%u\n",
                        cases[scenario_index].name,pair,physical?1:0,rules);
                    return false;
                }
                ++positive_count;
            }
    /* Both leaves use all nine commands; independently toggle each
     * whole-leaf winding rule in both viewport projections. */
    for(unsigned rules=0u;rules<4u;++rules)
        for(bool physical:{false,true}) {
            CHECK(detached_render(cases[8],pairs[5],physical,rules));
            ++positive_count;
        }
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i) {
        CHECK(allocation_matrix(cases[i],pairs[i%6u]));
        fault_count+=cases[i].allocations;
    }
    for(size_t i:{2u,5u,8u})CHECK(source_failures(cases[i]));
    CHECK(malformed_failures() && older_source_isolation());
    CHECK(positive_count==152u && fault_count==88u);
    std::fprintf(stderr,"[native.compound_clip_source_oom] %zu detached originals, %zu renders, "
        "%zu C allocation faults, 30 Source refusals, V8/V9 controls completed\n",
        positive_count,2u*positive_count,fault_count);
    return true;
}

int main()
{
    return run_tests()?EXIT_SUCCESS:EXIT_FAILURE;
}
