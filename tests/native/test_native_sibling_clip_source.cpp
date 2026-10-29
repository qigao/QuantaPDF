/* Public Source V10 original-PDF sibling Clip-scope conformance.
 * QPDF authors only source bytes. Independent per-layer geometry
 * supply original-PDF references; every pixel comes through the public
 * API and the same C11 raster, never exported IR or an alternate renderer. */
#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"Native sibling Clip failed at %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-sibling-clip-source.pdf";
static size_t positive_calls=0u,negative_calls=0u;

struct configuration {
    int quarters=4;
    int rotation=0;
    int dpi=72;
    int crop=0;
    int selected=0;
    int media_w=16,media_h=12;
};
struct rendered {
    int width=0,height=0;
    std::vector<unsigned char> rgb;
};

static bool write_pdf(const std::string& bytes)
{
    FILE *file=nullptr;
#ifdef _WIN32
    if(fopen_s(&file,filename,"wb")!=0)return false;
#else
    file=std::fopen(filename,"wb");
#endif
    if(file==nullptr)return false;
    const bool complete=std::fwrite(bytes.data(),1u,bytes.size(),file)
                         ==bytes.size();
    return std::fclose(file)==0 && complete;
}

/* Compact 16x12 originals bound even four independently sampled layers
 * at 144 DPI/UserUnit 1.25 within the unchanged 8m global work budget.
 * A separate 56x42 original deliberately exceeds that budget below.
 * selected=2 walks past a blank leaf and an unsupported Text leaf in
 * a nested PageTree. Only Image profiles receive an Image resource. */
static std::string original(const std::string& first,
                            const configuration& cfg,bool with_image=false,
                            const std::string& second={})
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    const std::string page_dictionary="<< /Type /Page /MediaBox [0 0 "+
        std::to_string(cfg.media_w)+" "+std::to_string(cfg.media_h)+"] >>";
    auto page=pdf->makeIndirectObject(Obj::parse(page_dictionary));
    auto parent=pages;
    if(cfg.selected==2) {
        auto blank=pdf->makeIndirectObject(Obj::parse(page_dictionary));
        blank.replaceKey("/Parent",pages);
        blank.replaceKey("/Contents",pdf->newStream(""));
        auto text=pdf->makeIndirectObject(Obj::parse(page_dictionary));
        text.replaceKey("/Contents",pdf->newStream("BT ET"));
        parent=pdf->makeIndirectObject(Obj::parse("<< /Type /Pages >>"));
        parent.replaceKey("/Parent",pages);
        parent.replaceKey("/Kids",Obj::newArray({text,page}));
        parent.replaceKey("/Count",Obj::newInteger(2));
        text.replaceKey("/Parent",parent);
        pages.replaceKey("/Kids",Obj::newArray({blank,parent}));
        pages.replaceKey("/Count",Obj::newInteger(3));
    } else {
        pages.replaceKey("/Kids",Obj::newArray({page}));
        pages.replaceKey("/Count",Obj::newInteger(1));
    }
    page.replaceKey("/Parent",parent);
    page.replaceKey("/UserUnit",Obj::parse(
        std::to_string(static_cast<double>(cfg.quarters)/4.0)));
    page.replaceKey("/Rotate",Obj::newInteger(cfg.rotation));
    if(cfg.crop==1)
        page.replaceKey("/CropBox",Obj::parse("[3 2 13 10]"));
    else if(cfg.crop==2)
        parent.replaceKey("/CropBox",Obj::parse("[3 2 13 10]"));
    else if(cfg.crop==3)
        page.replaceKey("/CropBox",Obj::parse("[2.75 1.5 13.25 10.25]"));
    if(second.empty())
        page.replaceKey("/Contents",pdf->newStream(first));
    else
        page.replaceKey("/Contents",Obj::newArray({
            pdf->newStream(first),pdf->newStream(second)}));
    if(with_image) {
        const std::string samples(
            "\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
        auto image=pdf->newStream(samples);
        auto dict=image.getDict();
        dict.replaceKey("/Type",Obj::newName("/XObject"));
        dict.replaceKey("/Subtype",Obj::newName("/Image"));
        dict.replaceKey("/Width",Obj::newInteger(2));
        dict.replaceKey("/Height",Obj::newInteger(2));
        dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
        dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
        auto xobjects=Obj::newDictionary();
        xobjects.replaceKey("/I",image);
        /* Solid Image colors have independent original PATH oracles;
         * /I remains patterned for the mixed-profile sampling checks. */
        const char *const names[]={"/R","/B","/G"};
        const std::string colors[]={
            std::string("\xff\0\0",3),std::string("\0\0\xff",3),
            std::string("\0\xff\0",3)
        };
        for(size_t i=0u;i<3u;++i) {
            auto solid=pdf->newStream(colors[i]);
            auto solid_dict=solid.getDict();
            solid_dict.replaceKey("/Type",Obj::newName("/XObject"));
            solid_dict.replaceKey("/Subtype",Obj::newName("/Image"));
            solid_dict.replaceKey("/Width",Obj::newInteger(1));
            solid_dict.replaceKey("/Height",Obj::newInteger(1));
            solid_dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
            solid_dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
            xobjects.replaceKey(names[i],solid);
        }
        auto resources=Obj::newDictionary();
        resources.replaceKey("/XObject",xobjects);
        page.replaceKey("/Resources",resources);
    }
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char *>(blob->getBuffer()),
                       blob->getSize());
}

static int render(const std::string& bytes,const configuration& cfg,
                  rendered& out)
{
    CHECK(write_pdf(bytes));
    quantapdf_context *context=nullptr;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(context,filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);
    OK(quantapdf_load_page(doc,cfg.selected,&page));
    quantapdf_render_options options{};
    options.struct_size=sizeof(options);
    options.dpi=static_cast<float>(cfg.dpi);
    const auto status=quantapdf_render_page_with_options(
        page,&options,&bitmap);
    if(status!=QUANTAPDF_OK || bitmap==nullptr)
        std::fprintf(stderr,"sibling Clip rejected: quarters=%d "
            "rotate=%d dpi=%d crop=%d page=%d status=%d\n",
            cfg.quarters,cfg.rotation,cfg.dpi,cfg.crop,cfg.selected,
            static_cast<int>(status));
    CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
    int stride=0,channels=0;
    const unsigned char *pixels=nullptr;
    size_t count=0u;
    OK(quantapdf_bitmap_dimensions(bitmap,
         &out.width,&out.height,&stride,&channels));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&count));
    const int full_w=cfg.media_w*4;
    const int full_h=cfg.media_h*4;
    const int span_w=cfg.crop==0?full_w:(cfg.crop==3?42:40);
    const int span_h=cfg.crop==0?full_h:(cfg.crop==3?35:32);
    const bool sideways=cfg.rotation==90 || cfg.rotation==270;
    const int source_w=sideways?span_h:span_w;
    const int source_h=sideways?span_w:span_h;
    const int expected_w=(source_w*cfg.quarters*cfg.dpi+1151)/1152;
    const int expected_h=(source_h*cfg.quarters*cfg.dpi+1151)/1152;
    CHECK(out.width==expected_w && out.height==expected_h &&
          stride==out.width*3 && channels==3 && pixels!=nullptr &&
          count==static_cast<size_t>(stride)*
                 static_cast<size_t>(out.height));
    out.rgb.assign(pixels,pixels+count);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    quantapdf_drop_context(context);
    /* The source file and every SDK owner except this Bitmap are gone. */
    const unsigned char *retained=nullptr;
    size_t retained_count=0u;
    OK(quantapdf_bitmap_data(bitmap,&retained,&retained_count));
    CHECK(retained!=nullptr && retained_count==out.rgb.size() &&
          std::memcmp(retained,out.rgb.data(),retained_count)==0);
    quantapdf_drop_bitmap(bitmap);
    ++positive_calls;
    return EXIT_SUCCESS;
}

static int rejected(const std::string& content,const configuration& cfg,
                    bool with_image=false,
                    std::optional<quantapdf_status> expected_status={})
{
    CHECK(write_pdf(original(content,cfg,with_image)));
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=
        reinterpret_cast<quantapdf_bitmap *>(uintptr_t(1));
    OK(quantapdf_open(filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);
    OK(quantapdf_load_page(doc,cfg.selected,&page));
    quantapdf_render_options options{};
    options.struct_size=sizeof(options);
    options.dpi=static_cast<float>(cfg.dpi);
    const auto status=quantapdf_render_page_with_options(
        page,&options,&bitmap);
    if(status==QUANTAPDF_OK || bitmap!=nullptr)
        std::fprintf(stderr,"sibling Clip unexpected acceptance: "
            "dpi=%d crop=%d status=%d bitmap=%p content=%s\n",
            cfg.dpi,cfg.crop,static_cast<int>(status),
            static_cast<void *>(bitmap),content.c_str());
    CHECK(status!=QUANTAPDF_OK && bitmap==nullptr);
    if(expected_status)CHECK(status==*expected_status);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    ++negative_calls;
    return EXIT_SUCCESS;
}

static bool equal(const rendered& a,const rendered& b)
{
    return a.width==b.width && a.height==b.height && a.rgb==b.rgb;
}
static uint32_t pixel(const rendered& image,int x,int y)
{
    const size_t at=(static_cast<size_t>(y)*
                     static_cast<size_t>(image.width)+
                     static_cast<size_t>(x))*3u;
    return (static_cast<uint32_t>(image.rgb.at(at))<<16u) |
           (static_cast<uint32_t>(image.rgb.at(at+1u))<<8u) |
            static_cast<uint32_t>(image.rgb.at(at+2u));
}
static bool all_white(const rendered& image)
{
    for(unsigned char c:image.rgb)if(c!=255u)return false;
    return !image.rgb.empty();
}

static const std::string full_red="1 0 0 rg 0 0 16 12 re f ";
static const std::string full_blue="0 0 1 rg 0 0 16 12 re f* ";
static const std::string full_green="0 1 0 rg 0 0 16 12 re f ";
static const std::string full_white="1 1 1 rg 0 0 16 12 re f ";
static const std::string green_prefix="0 1 0 rg 0 0 5 4 re f ";
static const std::string yellow_middle="1 1 0 rg 0 0 5 4 re f ";
static const std::string magenta_prefix="1 0 1 rg 10 7 5 4 re f ";
static const std::string cyan_suffix="0 1 1 rg 5 0 3 3 re f ";
static const std::string patterned="q 16 0 0 12 0 0 cm /I Do Q ";
static const std::string red_image="q 16 0 0 12 0 0 cm /R Do Q ";
static const std::string blue_image="q 16 0 0 12 0 0 cm /B Do Q ";
static const std::string green_small="q 2 0 0 2 13 0 cm /G Do Q ";
static const std::string rectangle_a="2 2 8 7 re";
static const std::string rectangle_b="6 4 8 6 re";
static const std::string blue_rectangle="q 8 0 0 6 6 4 cm /B Do Q ";
static const std::string green_a_cell="0 1 0 rg 3 3 2 2 re f ";
static const std::string yellow_b_cell="1 1 0 rg 11 5 2 2 re f ";
static const std::string green_b_cell="0 1 0 rg 11 5 2 2 re f ";
static const std::string green_b_image="q 2 0 0 2 11 5 cm /G Do Q ";
static const std::string curve_a="2 3 m 2 11 10 11 10 3 c 10 2 l 2 2 l h";
static const std::string curve_b="6 3 m 6 11 14 11 14 3 c 14 2 l 6 2 l h";
static const std::string outer_triangle="2 2 m 14 2 l 8 10 l h";
static const std::string inner_triangle="6 4 m 10 4 l 8 7 l h";
static const std::string compound=outer_triangle+" "+inner_triangle;
/* The identical-endpoint bridge cancels after every projection. This
 * old nine-command single-contour PATH expresses the compound W* hole. */
static const std::string hole_outline=
    "2 2 m 6 4 l 8 7 l 10 4 l 6 4 l 2 2 l 14 2 l 8 10 l h";
static const std::string outer_rectangle="2 2 12 8 re";
static const std::string notch_triangle="2 2 m 8 2 l 5 6 l h";
static const std::string notch_outline=
    "2 2 m 5 6 l 8 2 l 14 2 l 14 10 l 2 10 l h";
static const std::string empty_point="11 8 m 11 8 l 11 8 l h";

static std::string one_prefix(const std::string& clip,bool even=false)
{
    return "q "+clip+(even?" W* n ":" W n ");
}
static std::string scope(const std::string& clip,const std::string& draw,
                         bool even=false)
{
    return one_prefix(clip,even)+draw+"Q ";
}
static std::string paint(const char *color,const std::string& path)
{
    return path.empty()?std::string{}:std::string(color)+" "+path+" f ";
}
/* Existing one-Clip/GS1 followed by GS0 provides mixed references
 * without any sibling-scope admission or new graphics-state selection. */
static std::string old_first_paint(const std::string& first)
{
    return "q 0 0 16 12 re W n "+first+"Q ";
}
static int compare(const std::string& content,const std::string& expected,
                   const configuration& cfg,bool image_actual=false,
                   bool image_reference=false,bool expect_white=false,
                   rendered *observed=nullptr)
{
    rendered reference,actual;
    CHECK(render(original(expected,cfg,image_reference),cfg,reference)
          ==EXIT_SUCCESS);
    CHECK(render(original(content,cfg,image_actual),cfg,actual)==EXIT_SUCCESS);
    CHECK(equal(actual,reference));
    CHECK(expect_white?all_white(actual):!all_white(actual));
    if(observed!=nullptr)*observed=actual;
    return EXIT_SUCCESS;
}

/* Each sibling paints its own independently authored geometry. The
 * second leaf deliberately extends beyond the first, or into its hole.
 * Reversing scopes reverses compositing, including identical contours;
 * neither a rectangle intersection nor a union of the masks is valid. */
static int independent_sibling_geometry()
{
    struct shape {
        std::string first,second,first_w,first_even,second_w,second_even;
    };
    const std::string triangle_left="1 2 m 6 2 l 3 9 l h";
    const std::string triangle_right="10 2 m 15 2 l 12 9 l h";
    const std::string hole_cell="7 4 1 1 re";
    const shape cases[]={
        {rectangle_a,rectangle_b,rectangle_a,rectangle_a,
            rectangle_b,rectangle_b},
        {triangle_left,triangle_right,triangle_left,triangle_left,
            triangle_right,triangle_right},
        {curve_a,curve_b,curve_a,curve_a,curve_b,curve_b},
        {compound,hole_cell,outer_triangle,hole_outline,hole_cell,hole_cell},
        {compound,"14 10 -12 -8 re "+notch_triangle,
            outer_triangle,hole_outline,outer_rectangle,notch_outline},
        {curve_a,curve_a,curve_a,curve_a,curve_a,curve_a},
        {"",curve_b,"","",curve_b,curve_b},
        {empty_point+" "+outer_triangle,"",outer_triangle,outer_triangle,"",""}
    };
    for(int type=0;type<8;++type)
    for(bool even:{false,true})
    for(bool reverse:{false,true})
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+(type+(even?1:0))%2;
        cfg.rotation=((type+(reverse?1:0))%4)*90;
        cfg.crop=(type+(dpi==96?1:0))%4;
        cfg.selected=type%3==0?2:0;
        if((type==0 || type==3) && !reverse && dpi==72) {
            cfg.quarters=4;cfg.rotation=0;cfg.crop=0;
        }
        const auto& item=cases[type];
        const std::string a=scope(item.first,full_red,even);
        const std::string b=scope(item.second,full_blue,!even);
        const std::string red=paint("1 0 0 rg",even?item.first_even:item.first_w);
        const std::string blue=paint("0 0 1 rg",even?item.second_w:item.second_even);
        rendered actual;
        CHECK(compare(reverse?b+a:a+b,reverse?blue+red:red+blue,cfg,
                      false,false,false,&actual)==EXIT_SUCCESS);
        if(type==0 && !reverse && dpi==72) {
            CHECK(pixel(actual,12,5)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,3,8)==UINT32_C(0xff0000));
            CHECK(pixel(actual,8,5)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,0,0)==UINT32_C(0xffffff));
        }
        if(type==3 && !reverse && dpi==72) {
            CHECK(pixel(actual,5,7)==UINT32_C(0xff0000));
            CHECK(pixel(actual,7,7)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,8,7)==(even?UINT32_C(0xffffff):
                                        UINT32_C(0xff0000)));
        }
    }
    return EXIT_SUCCESS;
}

/* Image-only has exactly two paints. Mixed scenes retain at most two
 * Images and two PATHs, including patterned RAW sampling, either mixed
 * prefix order, homogeneous Image prefixes, and restored GS0 suffixes.
 * All references use old unclipped PATHs or old one-Clip mixed profiles. */
static int paint_profiles()
{
    const std::string red=paint("1 0 0 rg",rectangle_a);
    const std::string blue=paint("0 0 1 rg",rectangle_b);
    const std::string pattern_a=scope(rectangle_a,patterned);
    struct scene { std::string content,reference;bool image_reference; };
    const scene cases[]={
        {scope(rectangle_a,red_image)+scope(rectangle_b,blue_image,true),
            red+blue,false},
        {pattern_a+scope(rectangle_b,blue_image),pattern_a+blue,true},
        {pattern_a+scope(rectangle_b,full_blue),pattern_a+blue,true},
        {scope(rectangle_a,full_red)+scope(rectangle_b,blue_image),
            red+blue,false},
        {scope(rectangle_a,full_red+green_a_cell)+
            scope(rectangle_b,blue_image),red+green_a_cell+blue,false},
        {patterned+green_prefix+scope(rectangle_a,full_red)+
            scope(rectangle_b,blue_image),
            old_first_paint(patterned)+green_prefix+red+blue_rectangle,true},
        {patterned+green_small+scope(rectangle_a,full_red)+
            scope(rectangle_b,full_blue),
            old_first_paint(patterned)+green_small+red+blue,true},
        {full_green+scope(rectangle_a,red_image)+
            scope(rectangle_b,blue_image)+cyan_suffix,
            full_green+red+blue+cyan_suffix,false},
        {pattern_a+scope(rectangle_b,blue_image+green_b_cell),
            pattern_a+blue_rectangle+green_b_cell,true},
        {scope(rectangle_a,full_red+patterned)+
            scope(rectangle_b,full_blue+green_b_image),
            scope(rectangle_a,full_red+patterned)+blue+green_b_image,true},
        {full_red+green_small+scope(rectangle_a,full_green)+
            scope(rectangle_b,blue_image),old_first_paint(full_red)+
            green_small+paint("0 1 0 rg",rectangle_a)+blue_rectangle,true}
    };
    for(int type=0;type<11;++type)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+type%2;cfg.rotation=(type%4)*90;
        /* Keep the added PATH->Image prefix's green cell in the viewport. */
        cfg.crop=type==10?0:type%4;cfg.selected=type%2?2:0;
        CHECK(compare(cases[type].content,cases[type].reference,cfg,true,
                      cases[type].image_reference)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

/* Every legal GS0 phase placement fits the fixed four-paint ceiling.
 * A nested Q restores leaf 1, a later nested Q restores leaf 2, and
 * leaf 2 may be defined outside q and remain active at balanced EOF. */
static int scope_phases()
{
    const std::string a=scope(rectangle_a,full_red);
    const std::string b=scope(rectangle_b,full_blue);
    const std::string red=paint("1 0 0 rg",rectangle_a);
    const std::string blue=paint("0 0 1 rg",rectangle_b);
    struct scene { std::string content,reference;bool white; };
    const scene cases[]={
        {scope("",full_red)+scope("",full_blue,true),full_white,true},
        {full_green+a+b,full_green+red+blue,false},
        {a+yellow_middle+b,red+yellow_middle+blue,false},
        {a+b+cyan_suffix,red+blue+cyan_suffix,false},
        {full_green+a+b+cyan_suffix,full_green+red+blue+cyan_suffix,false},
        {full_green+a+yellow_middle+b,full_green+red+yellow_middle+blue,false},
        {a+yellow_middle+b+cyan_suffix,red+yellow_middle+blue+cyan_suffix,false},
        {full_green+magenta_prefix+a+b,full_green+magenta_prefix+red+blue,false},
        {one_prefix(rectangle_a)+"q "+full_red+"Q "+green_a_cell+"Q "+b+
            cyan_suffix,red+green_a_cell+blue+cyan_suffix,false},
        {a+rectangle_b+" W* n q "+full_blue+"Q "+yellow_b_cell,
            red+blue+yellow_b_cell,false}
    };
    for(int type=0;type<10;++type)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+type%2;cfg.rotation=(type%4)*90;
        cfg.crop=type%4;cfg.selected=type%2?2:0;
        CHECK(compare(cases[type].content,cases[type].reference,cfg,
                      false,false,cases[type].white)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

/* Definition CTMs are independently +1 and +2; Image placement returns
 * to the original frame. Each real outer Q restores green and identity
 * CTM for an unmasked PATH. Split Contents after Q or between W* and n.
 * The reference has one old Clip for the RAW pattern and three GS0
 * paints with explicit geometry, never a sibling-state graph. */
static int projection_ctms_and_streams()
{
    const std::string first="0 1 0 rg q 1 0 0 1 1 0 cm "
        "1 2 8 8 re W n 1 0 0 rg q 1 0 0 1 -1 0 cm "
        "16 0 0 12 0 0 cm /I Do Q Q";
    const std::string middle="0 0 2 2 re f q 1 0 0 1 2 0 cm "
        "8 3 4 4 re W*";
    const std::string last="n 0 0 1 rg q 1 0 0 1 -2 0 cm "
        "16 0 0 12 0 0 cm /B Do Q Q 14 9 1 1 re f";
    const std::string expected=scope("2 2 8 8 re",patterned)+
        "0 1 0 rg 0 0 2 2 re f q 4 0 0 4 10 3 cm /B Do Q "
        "0 1 0 rg 14 9 1 1 re f";
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;cfg.selected=2;
        rendered actual;
        CHECK(compare(first+" "+middle+" "+last,expected,cfg,true,true,
                      false,&actual)==EXIT_SUCCESS);
        const bool pending=rotation==90 || rotation==270;
        rendered split;
        CHECK(render(original(pending?first+" "+middle:first,cfg,true,
                              pending?last:middle+" "+last),cfg,split)
              ==EXIT_SUCCESS);
        CHECK(equal(split,actual));
        if(dpi==72 && rotation==0 && quarters==4) {
            CHECK(pixel(actual,3,3)==UINT32_C(0xff0000));
            CHECK(pixel(actual,8,3)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,3,8)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,8,8)==UINT32_C(0xffff00));
            CHECK(pixel(actual,12,6)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,1,10)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,14,2)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,0,0)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

/* These are the three exact formerly refused original contents, now
 * qualified positively. Their older refusal slots append a third Clip.
 * The references retain two independent layers even when the later
 * same-colored geometry contains the earlier compound hole. */
static int formerly_refused_originals()
{
    const std::string old_a="2 2 10 8 re";
    const std::string red_a=paint("1 0 0 rg",old_a);
    const std::string blue_a=paint("0 0 1 rg",old_a);
    const std::string blue_b=paint("0 0 1 rg",rectangle_b);
    struct scene { std::string content,reference; };
    const scene cases[]={
        {one_prefix(old_a)+full_red+"Q "+
            one_prefix(rectangle_b,true)+full_blue+"Q",red_a+blue_b},
        {one_prefix(compound,true)+full_red+"Q "+
            one_prefix(compound)+full_red+"Q",
            paint("1 0 0 rg",hole_outline)+paint("1 0 0 rg",outer_triangle)},
        {full_red+one_prefix(old_a)+full_blue+"Q "+
            one_prefix(rectangle_b)+full_blue+"Q",
            full_red+blue_a+blue_b}
    };
    for(const auto& item:cases)
    for(int dpi:{72,96,144}) {
        configuration cfg;cfg.dpi=dpi;
        CHECK(compare(item.content,item.reference,cfg)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

/* Touching siblings remain two alpha-composited paint layers. At the
 * half-covered boundary they must match two old independent PATHs in
 * source order, rather than a single mask made from their geometric union. */
static int touching_layers()
{
    const std::string left="0 0 6 12 re",right="6 0 10 12 re";
    const std::string a=scope(left,full_red),b=scope(right,full_blue);
    const std::string red=paint("1 0 0 rg",left);
    const std::string blue=paint("0 0 1 rg",right);
    for(bool reverse:{false,true})
    for(int dpi:{72,96,144}) {
        configuration cfg;cfg.dpi=dpi;cfg.quarters=5;
        rendered actual;
        CHECK(compare(reverse?b+a:a+b,reverse?blue+red:red+blue,cfg,
                      false,false,false,&actual)==EXIT_SUCCESS);
        if(dpi==72)
            CHECK(pixel(actual,7,4)==(reverse?UINT32_C(0xbf3f7f):
                                            UINT32_C(0x7f3fbf)));
    }
    return EXIT_SUCCESS;
}

/* The global 8m budget is still divided equally over four layers.
 * At 144 DPI/UserUnit 1.25 on 56x42 Media, the canvas is 140x105.
 * The second leaf has six edges plus four paint edges, so its charge
 * is 10*16*140*105=2,352,000 > 8m/4=2,000,000. The first leaf costs
 * 7*16*140*105=1,646,400. No production quota is adjusted for this case. */
static int fixed_sample_budget()
{
    const std::string content=full_green+scope(outer_triangle,full_red)+
        scope(compound,full_blue,true)+cyan_suffix;
    configuration cfg;cfg.dpi=144;cfg.quarters=5;cfg.selected=2;
    rendered compact;
    CHECK(render(original(content,cfg),cfg,compact)==EXIT_SUCCESS);
    CHECK(!all_white(compact));
    cfg.media_w=56;cfg.media_h=42;
    CHECK(rejected(content,cfg,false,QUANTAPDF_ERROR_UNSUPPORTED)
          ==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

static int atomic_refusals()
{
    const std::string a=scope(rectangle_a,full_red);
    const std::string b=scope(rectangle_b,full_blue);
    const std::string ready=a+b;
    const std::string first=one_prefix(rectangle_a);
    const std::string second=one_prefix(rectangle_b);
    const std::vector<std::string> bad={
        first+"Q "+b, /* first leaf never painted */
        a+second+"Q", /* second leaf never painted */
        first+"Q "+second+"Q",
        one_prefix("")+"Q "+b, /* emptiness does not waive the used-state gate */
        ready+scope(outer_triangle,full_green), /* third definition */
        a+second+full_blue+outer_triangle+" W n "+full_green+"Q",
        ready+scope(rectangle_a,full_red), /* identical third leaf still forbidden */
        full_green+a+yellow_middle+b+cyan_suffix, /* five paint phases */
        ready+green_prefix+yellow_middle+cyan_suffix, /* fifth late PATH */
        red_image+scope(rectangle_a,red_image)+scope(rectangle_b,blue_image),
        scope(rectangle_a,red_image)+scope(rectangle_b,blue_image)+red_image,
        scope(rectangle_a,full_red+green_a_cell)+
            scope(rectangle_b,full_blue+blue_image), /* late Image after 3PATH */
        scope(rectangle_a,red_image+patterned)+
            scope(rectangle_b,blue_image+full_blue), /* three Images + PATH */
        a+second+"0 0 1 rg "+compound+" f Q", /* compound painted PATH */
        a+second+compound+" n "+full_blue+"Q", /* compound discarded path */
        a+"q 2 2 12 8 re 6 4 4 3 re W n "+full_blue+"Q", /* 10 commands */
        a+"q "+compound+" "+empty_point+" W n "+full_blue+"Q",
        a+"q 2 2 m 14 2 l 8 10 l h h W n "+full_blue+"Q",
        a+"q "+outer_triangle+" 8 8 m 10 8 l h W n "+full_blue+"Q",
        a+"q 2.125 2 m 14 2 l 8 10 l h W n "+full_blue+"Q",
        a+"q 2 2 m 14 2 l 8 -1000001 l h W n "+full_blue+"Q",
        a+"q 2 2 m 2 8 8 8 12 c 12 2 l h W n "+full_blue+"Q",
        a+"q "+rectangle_b+" W "+full_blue+"Q",
        a+"q "+rectangle_b+" W 1 n "+full_blue+"Q",
        a+"q "+rectangle_b+" W W* n "+full_blue+"Q",
        scope(rectangle_a,red_image)+second+
            "q 16 0 0 12 0 0 cm /Missing Do Q Q",
        ready+" BT ET",
        ready+" 2 2 m 10 8 l S",
        ready+" 2 g",
        ready+" Q",
        ready+" q"
    };
    for(const auto& content:bad)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.crop=dpi==72?0:(dpi==96?2:3);
        cfg.selected=dpi==144?2:0;
        CHECK(rejected(content,cfg,content.find(" Do")!=std::string::npos)
              ==EXIT_SUCCESS);
    }
    std::string operators;
    for(int i=0;i<256;++i)operators+="1 w ";
    for(int dpi:{72,96,144}) {
        configuration cfg;cfg.dpi=dpi;
        CHECK(rejected(operators+ready,cfg,false,
                       QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
        CHECK(rejected(a+std::string(65536u,' ')+b,cfg,false,
                       QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(independent_sibling_geometry()==EXIT_SUCCESS);
    CHECK(paint_profiles()==EXIT_SUCCESS);
    CHECK(scope_phases()==EXIT_SUCCESS);
    CHECK(projection_ctms_and_streams()==EXIT_SUCCESS);
    CHECK(formerly_refused_originals()==EXIT_SUCCESS);
    CHECK(touching_layers()==EXIT_SUCCESS);
    CHECK(fixed_sample_budget()==EXIT_SUCCESS);
    CHECK(atomic_refusals()==EXIT_SUCCESS);
    CHECK(positive_calls==421u && negative_calls==100u);
    std::printf("Native sibling Clip scopes: %zu rendered, %zu atomic refusals\n",
                positive_calls,negative_calls);
    return EXIT_SUCCESS;
}

