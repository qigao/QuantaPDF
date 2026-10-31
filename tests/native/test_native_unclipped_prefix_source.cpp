/* Public Source V10 original-PDF unclipped-prefix conformance.
 * QPDF authors only source bytes. Independent analytical intersections
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
    std::fprintf(stderr,"Native unclipped prefix failed at %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-unclipped-prefix-source.pdf";
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
        std::fprintf(stderr,"unclipped prefix rejected: quarters=%d "
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
        std::fprintf(stderr,"unclipped prefix unexpected acceptance: "
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
static const std::string full_green="0 1 0 rg 0 0 16 12 re f ";
static const std::string full_blue="0 0 1 rg 0 0 16 12 re f* ";
static const std::string green_prefix="0 1 0 rg 0 0 5 4 re f ";
static const std::string yellow_prefix="1 1 0 rg 10 7 5 4 re f ";
static const std::string cyan_suffix="0 1 1 rg 5 0 3 3 re f ";
static const std::string magenta_suffix="1 0 1 rg 0 7 3 3 re f ";
static const std::string patterned="q 16 0 0 12 0 0 cm /I Do Q ";
static const std::string pattern_small="q 8 0 0 6 8 0 cm /I Do Q ";
static const std::string red_image="q 16 0 0 12 0 0 cm /R Do Q ";
static const std::string blue_image="q 16 0 0 12 0 0 cm /B Do Q ";
static const std::string green_small="q 2 0 0 2 13 0 cm /G Do Q ";
static const std::string blue_rectangle="q 10 0 0 8 2 2 cm /B Do Q ";
static const std::string rectangle_a="2 2 10 8 re";
static const std::string rectangle_b="6 4 8 6 re";
static const std::string rectangle_i="6 4 6 6 re";
static const std::string selector="11 3 1 1 re";
static const std::string triangle_a="2 2 m 14 2 l 2 10 l h";
static const std::string triangle_b="2 2 m 14 2 l 14 10 l h";
static const std::string triangle_i="2 2 m 14 2 l 8 6 l h";
static const std::string curve_a="2 3 m 2 11 10 11 10 3 c 10 2 l 2 2 l h";
static const std::string curve_half="6 9 m 8 9 10 7 10 3 c 10 2 l 6 2 l h";
static const std::string outer_triangle="2 2 m 14 2 l 8 10 l h";
static const std::string inner_triangle="6 4 m 10 4 l 8 7 l h";
static const std::string compound=outer_triangle+" "+inner_triangle;
/* One old single contour: the bridge uses identical endpoints in both
 * directions, preserving exact cancellation after device projection. */
static const std::string hole_outline=
    "2 2 m 6 4 l 8 7 l 10 4 l 6 4 l 2 2 l 14 2 l 8 10 l h";

static std::string one_prefix(const std::string& clip,bool even=false)
{
    return "q "+clip+(even?" W* n ":" W n ");
}
static std::string paint(const char *color,const std::string& path)
{
    return path.empty()?std::string{}:std::string(color)+" "+path+" f ";
}
/* This existing profile has its first paint under a full-Media Clip,
 * then GS0 paints after Q. It provides old accepted mixed references
 * without relying on the new unclipped-prefix admission being tested. */
static std::string old_first_paint(const std::string& first)
{
    return "q 0 0 16 12 re W n "+first+"Q ";
}
static int compare(const std::string& content,const std::string& expected,
                   const configuration& cfg,bool image_actual=false,
                   bool image_reference=false,rendered *observed=nullptr)
{
    rendered reference,actual;
    CHECK(render(original(expected,cfg,image_reference),cfg,reference)
          ==EXIT_SUCCESS);
    CHECK(render(original(content,cfg,image_actual),cfg,actual)==EXIT_SUCCESS);
    /* Every authored prefix has visible nonwhite coverage. A full-page
     * colored prefix intentionally removes white from many viewports. */
    CHECK(equal(actual,reference) && !all_white(actual));
    if(observed!=nullptr)*observed=actual;
    return EXIT_SUCCESS;
}

/* One GS, one/two leaves: the initial red PATH remains outside every
 * later mask and shows through real holes and empty intersections.
 * The independently authored blue PATH reference has no Clip resources. */
static int path_prefix_geometries()
{
    struct shape {
        std::string first,second,nonzero,even_odd;
        bool two;
    };
    const shape cases[]={
        {rectangle_a,"",rectangle_a,rectangle_a,false},
        {curve_a,"",curve_a,curve_a,false},
        {compound,"",outer_triangle,hole_outline,false},
        {rectangle_a,rectangle_b,rectangle_i,rectangle_i,true},
        {triangle_a,triangle_b,triangle_i,triangle_i,true},
        {curve_a,"6 2 8 8 re",curve_half,curve_half,true},
        {"",rectangle_a,"","",true},
        {rectangle_a,"","","",true},
        {compound,compound,outer_triangle,hole_outline,true},
        {"","","","",false}
    };
    for(int type=0;type<10;++type)
    for(bool even:{false,true})
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+(type+(even?1:0))%2;
        cfg.rotation=(type%4)*90;
        cfg.crop=(type+(dpi==96?1:0))%4;cfg.selected=type%3==0?2:0;
        if((type==0 || type==2) && dpi==72) {
            cfg.quarters=4;cfg.rotation=0;cfg.crop=0;
        }
        const auto& item=cases[type];
        std::string content=full_red+one_prefix(item.first,even);
        /* A prefix GS0 paint does not observe leaf 1. The ordinary
         * rectangle pair remains safely reducible. This saved-q
         * control instead preserves both original leaves. */
        const bool saved_first=type==3 && dpi==96;
        if(saved_first)content+="q ";
        if(item.two)content+=item.second+(even?" W* n ":" W n ");
        content+=full_blue+(saved_first?"Q Q":"Q");
        const std::string expected=full_red+
            paint("0 0 1 rg",even?item.even_odd:item.nonzero);
        rendered actual;
        CHECK(compare(content,expected,cfg,false,false,&actual)==EXIT_SUCCESS);
        if(type==0 && dpi==72) {
            CHECK(pixel(actual,0,0)==UINT32_C(0xff0000));
            CHECK(pixel(actual,5,5)==UINT32_C(0x0000ff));
        }
        if(type==2 && dpi==72) {
            CHECK(pixel(actual,0,0)==UINT32_C(0xff0000));
            CHECK(pixel(actual,5,7)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,7,7)==(even?UINT32_C(0xff0000):
                                        UINT32_C(0x0000ff)));
        }
    }
    return EXIT_SUCCESS;
}

/* Two/three homogeneous prefix PATHs and real Q-restored suffixes all
 * share the existing four-layer ceiling. One layout defines its first
 * Clip outside q and ends with that Clip still active. */
static int homogeneous_prefix_and_suffix()
{
    const std::string blue=paint("0 0 1 rg",rectangle_a);
    const std::string inner_yellow="1 1 0 rg 6 4 3 3 re f ";
    struct scene { std::string content,reference; };
    const scene cases[]={
        {full_red+green_prefix+rectangle_a+" W n "+full_blue,
            full_red+green_prefix+blue},
        {full_red+green_prefix+yellow_prefix+one_prefix(rectangle_a)+
            full_blue+"Q",full_red+green_prefix+yellow_prefix+blue},
        {full_red+one_prefix(rectangle_a)+full_blue+"Q "+cyan_suffix,
            full_red+blue+cyan_suffix},
        {full_red+green_prefix+one_prefix(rectangle_a)+full_blue+"Q "+
            cyan_suffix,full_red+green_prefix+blue+cyan_suffix},
        {full_red+one_prefix(rectangle_a)+full_blue+inner_yellow+"Q "+
            cyan_suffix,full_red+blue+inner_yellow+cyan_suffix},
        {full_red+one_prefix(rectangle_a)+full_blue+"Q "+cyan_suffix+
            magenta_suffix,full_red+blue+cyan_suffix+magenta_suffix}
    };
    for(int type=0;type<6;++type)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+type%2;
        cfg.rotation=(type%4)*90;cfg.crop=type%4;cfg.selected=type>2?2:0;
        CHECK(compare(cases[type].content,cases[type].reference,cfg)
              ==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

/* Prefix staging retains homogeneous IMAGE/PATH and one-of-each in
 * either order. Solid Images have independent PATH references; RAW
 * patterned Images use old unclipped mixed or old GS1/GS0 references.
 * The final two cases use both prefix kinds and a Q-restored suffix. */
static int images_and_mixed_prefixes()
{
    const std::string blue=paint("0 0 1 rg",rectangle_a);
    const std::string blue_small=paint("0 0 1 rg",selector);
    struct scene { std::string content,reference;bool image_reference; };
    const scene cases[]={
        {red_image+one_prefix(rectangle_a)+blue_image+"Q",
            full_red+blue,false},
        {patterned+one_prefix(rectangle_a)+full_blue+"Q",
            patterned+blue,true},
        {full_red+one_prefix(rectangle_a)+blue_image+"Q",
            full_red+blue,false},
        {patterned+green_small+one_prefix(rectangle_a)+full_blue+"Q",
            old_first_paint(patterned)+green_small+blue,true},
        {full_red+green_prefix+one_prefix(rectangle_a)+blue_image+"Q",
            full_red+green_prefix+blue,false},
        {patterned+green_prefix+one_prefix(rectangle_a)+full_blue+"Q",
            old_first_paint(patterned)+green_prefix+blue,true},
        {full_red+pattern_small+one_prefix(selector)+full_blue+"Q",
            old_first_paint(full_red)+pattern_small+blue_small,true},
        {patterned+green_prefix+one_prefix(rectangle_a)+blue_image+"Q "+
            cyan_suffix,old_first_paint(patterned)+green_prefix+
            blue_rectangle+cyan_suffix,true},
        {full_red+pattern_small+one_prefix(rectangle_a)+full_blue+"Q "+
            green_small,old_first_paint(full_red)+pattern_small+blue+
            green_small,true}
    };
    for(int type=0;type<9;++type)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+type%2;
        cfg.rotation=(type%4)*90;cfg.crop=type%4;cfg.selected=type%2?2:0;
        CHECK(compare(cases[type].content,cases[type].reference,cfg,true,
                      cases[type].image_reference)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

/* Both nonzero states remain used. All legal phase combinations fit
 * at most four paints: 0,1,2; 0,1,2,0; 0,2,1; 0,2,1,0; 0,1,2,1;
 * 0,0,1,2. First-only painting may begin after Q restores that leaf.
 * Empty/compound leaves still affect only their original paint states. */
static int two_state_prefixes()
{
    const std::string green_a=paint("0 1 0 rg",rectangle_a);
    const std::string blue_i=paint("0 0 1 rg",rectangle_i);
    const std::string green_curve_cell="0 1 0 rg 3 3 2 2 re f ";
    const std::string yellow_hole="1 1 0 rg 7 4 1 1 re f ";
    struct scene { std::string content,reference; };
    const scene cases[]={
        {full_red+rectangle_a+" W n "+full_green+
            one_prefix(rectangle_b,true)+full_blue+"Q",
            full_red+green_a+blue_i},
        {full_red+one_prefix(compound,true)+full_green+selector+
            " W n "+full_blue+"Q "+cyan_suffix,
            full_red+paint("0 1 0 rg",hole_outline)+
            paint("0 0 1 rg",selector)+cyan_suffix},
        {full_red+one_prefix(curve_a)+one_prefix("6 2 8 8 re")+
            full_blue+"Q "+green_curve_cell+"Q",
            full_red+paint("0 0 1 rg",curve_half)+green_curve_cell},
        {full_red+one_prefix(rectangle_a)+one_prefix("")+full_blue+
            "Q "+full_green+"Q "+cyan_suffix,
            full_red+green_a+cyan_suffix},
        {full_red+one_prefix(compound)+full_green+
            one_prefix(compound,true)+full_blue+"Q "+yellow_hole+"Q",
            full_red+paint("0 1 0 rg",outer_triangle)+
            paint("0 0 1 rg",hole_outline)+yellow_hole},
        {full_red+green_prefix+one_prefix("")+full_green+curve_a+
            " W* n "+full_blue+"Q",full_red+green_prefix}
    };
    for(int type=0;type<6;++type)
    for(int dpi:{72,96,144})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=quarters;
        cfg.rotation=((type+quarters-4)%4)*90;
        cfg.crop=(type+quarters-4)%4;cfg.selected=type%2?2:0;
        CHECK(compare(cases[type].content,cases[type].reference,cfg)
              ==EXIT_SUCCESS);
    }
    /* Mixed 0,2,1,0: a blue Image sees a compound hole, the restored
     * green Image paints INSIDE that hole, and the cyan PATH follows Q
     * to no Clip. The oracle uses four old unclipped PATHs only. */
    const std::string content=full_red+one_prefix(compound)+
        one_prefix(compound,true)+blue_image+
        "Q q 1 0 0 1 7 4 cm /G Do Q Q "+cyan_suffix;
    const std::string expected=full_red+paint("0 0 1 rg",hole_outline)+
        "0 1 0 rg 7 4 1 1 re f "+cyan_suffix;
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;cfg.rotation=270;
        cfg.crop=dpi==96?2:0;cfg.selected=2;
        CHECK(compare(content,expected,cfg,true)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

/* Prefix RAW Image -> first-Clip solid Image -> intersection PATH ->
 * Q-restored PATH. Definition CTMs are +1/+2, both Image placements
 * and the intersection fill are in the original frame, and final Q
 * recovers cyan without a repeated color operator. Split Contents after
 * the prefix paint or between the first W and n. */
static int projection_ctms_and_streams()
{
    const std::string prefix="0 1 1 rg "+patterned;
    const std::string first_clip="q 1 0 0 1 1 0 cm 1 2 10 8 re W";
    const std::string tail="n q 1 0 0 1 -1 0 cm "
        "16 0 0 12 0 0 cm /R Do Q q 1 0 0 1 1 0 cm "
        "4 4 8 6 re W* n 1 0 0 1 -2 0 cm "+full_blue+
        "Q Q 5 0 3 3 re f";
    const std::string expected=old_first_paint(patterned)+
        "q 10 0 0 8 2 2 cm /R Do Q "+paint("0 0 1 rg",rectangle_i)+
        cyan_suffix;
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=rotation==270?2:0;
        rendered reference,actual,split;
        CHECK(render(original(expected,cfg,true),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(prefix+first_clip+" "+tail,cfg,true),cfg,actual)
              ==EXIT_SUCCESS);
        const bool after_prefix=rotation==0 || rotation==180;
        const std::string first=after_prefix?prefix:prefix+first_clip;
        const std::string second=after_prefix?first_clip+" "+tail:tail;
        CHECK(render(original(first,cfg,true,second),cfg,split)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(split,reference) &&
              !all_white(actual));
        if(dpi==72 && rotation==0 && quarters==4) {
            CHECK(pixel(actual,1,1)==UINT32_C(0xff0000));
            CHECK(pixel(actual,15,1)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,1,10)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,15,10)==UINT32_C(0xffff00));
            CHECK(pixel(actual,3,8)==UINT32_C(0xff0000));
            CHECK(pixel(actual,8,5)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,6,10)==UINT32_C(0x00ffff));
            CHECK(pixel(actual,6,9)==UINT32_C(0x00ffff));
        }
    }
    return EXIT_SUCCESS;
}

/* These are the four exact previously refused original content strings
 * whose older fixtures now retain concrete late-error/layer refusals.
 * Keep their admission positive even where repeated red is visually
 * redundant; the other groups independently distinguish all masks. */
static int formerly_refused_originals()
{
    const std::string small_red="1 0 0 rg 4 4 8 6 re f ";
    const std::string small_blue="0 0 1 rg 4 4 8 6 re f ";
    struct scene { std::string content,reference; };
    const scene cases[]={
        {small_red+"q 4 4 8 6 re W n "+small_blue+"Q",
            small_red+small_blue},
        {full_red+one_prefix(curve_a)+full_red+"Q",full_red},
        {full_red+one_prefix(rectangle_a)+full_red+
            one_prefix(rectangle_b,true)+full_blue+"Q Q",
            full_red+paint("1 0 0 rg",rectangle_a)+
            paint("0 0 1 rg",rectangle_i)},
        {full_red+one_prefix(compound,true)+full_red+"Q",full_red}
    };
    for(int type=0;type<4;++type)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;
        if(type==0){cfg.media_w=32;cfg.media_h=24;}
        CHECK(compare(cases[type].content,cases[type].reference,cfg)
              ==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

/* The compound left half and the right rectangle touch at device x=7.5
 * at 72 DPI/UserUnit 1.25. Their true intersection covers no sample, so
 * the earlier green prefix must survive unchanged. A product of two
 * averaged half-masks would incorrectly tint that green boundary. */
static int same_sample_prefix_survival()
{
    const std::string left=
        "0 0 m 6 0 l 0 12 l h 6 0 m 6 12 l 0 12 l h";
    const std::string prefix=full_green+one_prefix(left)+
        "6 0 10 12 re W* n ";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;
        rendered reference;
        CHECK(render(original(full_green,cfg),cfg,reference)==EXIT_SUCCESS);
        for(bool with_image:{false,true}) {
            rendered actual;
            CHECK(render(original(prefix+(with_image?red_image:full_red)+"Q",
                                  cfg,with_image),cfg,actual)==EXIT_SUCCESS);
            CHECK(equal(actual,reference) && !all_white(actual));
            if(dpi==72)CHECK(pixel(actual,7,4)==UINT32_C(0x00ff00));
        }
    }
    return EXIT_SUCCESS;
}

/* The new prefix is still one of the SAME four sampled layers. On
 * 56x42 Media at 2.5x the 140x105 prefix costs 4*16*14700=940,800;
 * the first-Clip layer costs 7*16*14700=1,646,400; intersection costs
 * 10*16*14700=2,352,000, exceeding the unchanged 8m/4=2,000,000. */
static int fixed_sample_budget()
{
    const std::string content=full_red+one_prefix(triangle_a)+full_green+
        triangle_b+" W* n "+full_blue+"Q "+cyan_suffix;
    configuration cfg;
    cfg.dpi=144;cfg.quarters=5;cfg.selected=2;
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
    const std::string first=one_prefix(rectangle_a);
    const std::string second=one_prefix(rectangle_b,true);
    const std::string ready=full_red+first+full_blue+"Q";
    const std::vector<std::string> bad={
        full_red+first+"Q", /* prefix does not qualify an unused Clip */
        red_image+first+"Q",
        full_red+first+"Q "+full_blue,
        ready+" "+one_prefix(rectangle_b)+full_blue+"Q", /* reentry */
        full_red+first+full_green+second+full_blue+
            rectangle_b+" W n Q Q", /* third definition */
        full_red+first+full_green+second+"Q Q", /* second Clip unused */
        /* Prefix+first+intersection+restored-first+suffix is FIVE. */
        full_red+first+full_green+second+full_blue+
            "Q "+full_green+"Q "+cyan_suffix,
        full_red+full_red+full_red+full_red+first+full_blue+"Q",
        full_red+full_red+full_red+first+full_blue+"Q "+cyan_suffix,
        /* Two-GS IMAGE-only plus a prefix would require three Images. */
        red_image+first+red_image+second+blue_image+"Q Q",
        red_image+red_image+first+blue_image+"Q",
        red_image+red_image+red_image+first+full_blue+"Q",
        full_red+full_red+full_red+first+blue_image+"Q",
        /* These fit final 2-Image/2-PATH limits but stage THREE mixed
         * prefix paints, outside the retained unclipped forms. */
        red_image+full_red+red_image+first+full_blue+"Q",
        full_red+red_image+full_red+first+blue_image+"Q",
        red_image+full_red+first+full_blue+"Q "+cyan_suffix,
        full_red+red_image+first+blue_image+"Q "+red_image,
        red_image+red_image+red_image+red_image+first+full_blue+"Q",
        "1 0 0 rg "+compound+" f "+first+full_blue+"Q",
        compound+" n "+full_red+first+full_blue+"Q",
        full_red+"q 2 2 10 8 re 6 4 4 3 re W n "+full_blue+"Q",
        full_red+"q "+compound+" 1 1 m 2 1 l 1 2 l h W n "+
            full_blue+"Q",
        full_red+"q 2.125 2 m 14 2 l 8 10 l h W n "+full_blue+"Q",
        full_red+"q 2 2 m 14 2 l 8 -1000001 l h W n "+full_blue+"Q",
        full_red+"q "+rectangle_a+" W "+full_blue+"Q",
        full_red+"q "+rectangle_a+" W 1 n "+full_blue+"Q",
        "q 16 0 0 12 0 0 cm /Missing Do Q "+first+full_blue+"Q",
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
        configuration cfg;
        cfg.dpi=dpi;
        CHECK(rejected(operators+ready,cfg,false,QUANTAPDF_ERROR_UNSUPPORTED)
              ==EXIT_SUCCESS);
        CHECK(rejected(full_red+std::string(65536u,' ')+first+full_blue+"Q",
                       cfg,false,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(path_prefix_geometries()==EXIT_SUCCESS);
    CHECK(homogeneous_prefix_and_suffix()==EXIT_SUCCESS);
    CHECK(images_and_mixed_prefixes()==EXIT_SUCCESS);
    CHECK(two_state_prefixes()==EXIT_SUCCESS);
    CHECK(projection_ctms_and_streams()==EXIT_SUCCESS);
    CHECK(formerly_refused_originals()==EXIT_SUCCESS);
    CHECK(same_sample_prefix_survival()==EXIT_SUCCESS);
    CHECK(fixed_sample_budget()==EXIT_SUCCESS);
    CHECK(atomic_refusals()==EXIT_SUCCESS);
    std::printf("unclipped-prefix original PDFs: %zu successful renders, "
                "%zu atomic refusals\n",positive_calls,negative_calls);
    return EXIT_SUCCESS;
}
