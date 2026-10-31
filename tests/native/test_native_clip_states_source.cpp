/* Public Source V10 original-PDF restored Clip-state conformance.
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
    std::fprintf(stderr,"Native restored Clip state failed at %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-clip-states-source.pdf";
static size_t positive_calls=0u,negative_calls=0u;

struct configuration {
    int quarters=4;
    int rotation=0;
    int dpi=72;
    int crop=0;
    int selected=0;
    bool large_media=false;
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
    const std::string page_dictionary=cfg.large_media?
        "<< /Type /Page /MediaBox [0 0 56 42] >>":
        "<< /Type /Page /MediaBox [0 0 16 12] >>";
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
        std::fprintf(stderr,"restored Clip state rejected: quarters=%d "
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
    const int full_w=cfg.large_media?224:64;
    const int full_h=cfg.large_media?168:48;
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
        std::fprintf(stderr,"restored Clip state unexpected acceptance: "
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
static bool ink_and_white(const rendered& image)
{
    bool ink=false,white=false;
    for(size_t i=0u;i<image.rgb.size();i+=3u) {
        const bool blank=image.rgb[i]==255u && image.rgb[i+1u]==255u &&
                         image.rgb[i+2u]==255u;
        ink|=!blank;
        white|=blank;
    }
    return ink && white;
}

static const std::string green_box="3 3 2 2 re";
static const std::string yellow_box="12 8 3 2 re";
static const std::string rectangle_a="2 2 10 8 re";
static const std::string rectangle_b="6 4 8 6 re";
static const std::string rectangle_i="6 4 6 6 re";
static const std::string triangle_a="2 2 m 14 2 l 2 10 l h";
static const std::string triangle_b="2 2 m 14 2 l 14 10 l h";
static const std::string triangle_i="2 2 m 14 2 l 8 6 l h";
static const std::string cubic_a=
    "2 3 m 2 11 10 11 10 3 c 10 2 l 2 2 l h";
/* Exact right half of cubic_a, independently split at Bezier t=1/2. */
static const std::string cubic_i=
    "6 9 m 8 9 10 7 10 3 c 10 2 l 6 2 l h";
static const std::string full_red="1 0 0 rg 0 0 16 12 re f ";
static const std::string full_blue="0 0 1 rg 0 0 16 12 re f* ";
static const std::string full_white="1 1 1 rg 0 0 16 12 re f ";
static const std::string patterned="q 16 0 0 12 0 0 cm /I Do Q ";
static const std::string red_image="q 16 0 0 12 0 0 cm /R Do Q ";
static const std::string blue_image="q 16 0 0 12 0 0 cm /B Do Q ";
static const std::string small_red="q 2 0 0 2 3 3 cm /R Do Q ";
static const std::string small_green="q 2 0 0 2 3 3 cm /G Do Q ";

struct geometry { std::string a,b,intersection; };
static const geometry shapes[]={
    {rectangle_a,rectangle_b,rectangle_i},
    {triangle_a,triangle_b,triangle_i},
    {cubic_a,"6 2 8 8 re",cubic_i},
    {cubic_a,"",""},
    {"",rectangle_b,""}
};

static std::string one_prefix(const std::string& path,bool even=false)
{
    return "q "+path+(even?" W* n ":" W n ");
}
static std::string paint(const char *color,const std::string& path)
{
    return path.empty()?std::string{}:std::string(color)+" "+path+" f ";
}
static std::string reference_four(const std::string& a,const std::string& i)
{
    return paint("1 0 0 rg",a)+paint("0 0 1 rg",i)+
           (a.empty()?std::string{}:paint("0 1 0 rg",green_box))+
           paint("1 1 0 rg",yellow_box);
}
static std::string state_four(const std::string& a,const std::string& b,
                              bool even_a=false,bool even_b=false)
{
    /* Inner Q restores GREEN and Clip1; outer Q restores YELLOW and
     * unclipped state. Neither restored fill repeats its color operator. */
    return "1 1 0 rg "+one_prefix(a,even_a)+full_red+"0 1 0 rg "+
           one_prefix(b,even_b)+full_blue+"Q "+green_box+
           " f Q "+yellow_box+" f";
}

/* Four paints exercise FIRST -> INTERSECTION -> FIRST -> NONE.
 * References paint independently authored known geometry with no Clip
 * resources: rectangle/rectangle, crossing lines, genuine split cubic,
 * empty second restoring a visible first leaf, and empty first. */
static int four_path_state_references()
{
    for(int type=0;type<5;++type)
    for(int dpi:{72,96,144})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=quarters;
        cfg.rotation=((type+quarters-4)%4)*90;
        cfg.crop=(type+quarters-4)%4;cfg.selected=type%2?2:0;
        const auto& g=shapes[type];
        rendered reference,actual;
        CHECK(render(original(reference_four(g.a,g.intersection),cfg),
                     cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(state_four(g.a,g.b,false,true),cfg),cfg,actual)
              ==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
        if(type==0 && dpi==72 && quarters==4) {
            CHECK(pixel(actual,3,3)==UINT32_C(0xff0000));
            CHECK(pixel(actual,8,5)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,3,8)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,13,3)==UINT32_C(0xffff00));
            CHECK(pixel(actual,0,6)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

/* Cover two/three/four paints and both sides of the state transition.
 * In particular sequence 2->1 has NO first-scope paint before the second
 * W, and the first W is outside q: reducing its rectangles at definition
 * time would erase the later green rectangle outside their intersection. */
static int path_state_sequences()
{
    const std::string red_a=paint("1 0 0 rg",rectangle_a);
    const std::string red_i=paint("1 0 0 rg",rectangle_i);
    const std::string blue_i=paint("0 0 1 rg",rectangle_i);
    const std::string green=paint("0 1 0 rg",green_box);
    const std::string yellow=paint("1 1 0 rg",yellow_box);
    struct scene { std::string content,reference; };
    const scene cases[]={
        {one_prefix(rectangle_a)+full_red+rectangle_b+" W* n "+
             full_blue+"Q",red_a+blue_i},
        {rectangle_a+" W n 0 1 0 rg "+one_prefix(rectangle_b,true)+
             full_blue+"Q "+green_box+" f",blue_i+green},
        {one_prefix(rectangle_a)+full_red+"0 1 0 rg "+
             one_prefix(rectangle_b)+full_blue+"Q "+green_box+" f Q",
             red_a+blue_i+green},
        {one_prefix(rectangle_a)+full_red+rectangle_b+" W n "+
             full_blue+"Q "+yellow,red_a+blue_i+yellow},
        {one_prefix(rectangle_a)+full_red+green+rectangle_b+" W n "+
             full_blue+"Q "+yellow,red_a+green+blue_i+yellow},
        {one_prefix(rectangle_a)+"0 1 0 rg "+one_prefix(rectangle_b)+
             full_red+full_blue+"Q "+green_box+" f Q "+yellow,
             red_i+blue_i+green+yellow}
    };
    for(int type=0;type<6;++type)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+type%2;
        cfg.rotation=(type%4)*90;cfg.crop=type%4;
        cfg.selected=type>=3?2:0;
        rendered reference,actual;
        CHECK(render(original(cases[type].reference,cfg),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(cases[type].content,cfg),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* Constant-color original RAW Images have independent PATH geometry
 * references. Two Images exercise 1->2 and 2->1, including empty second
 * restoration and first Clip outside q. No expected output uses two GS. */
static int image_state_references()
{
    const geometry cases[]={
        {rectangle_a,rectangle_b,rectangle_i},
        {rectangle_a,"",""},{"",rectangle_b,""}
    };
    for(int type=0;type<3;++type)
    for(int direction=0;direction<2;++direction)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=dpi==96?5:4;
        cfg.rotation=((type+direction)%4)*90;
        cfg.crop=(type+direction)%4;cfg.selected=direction?2:0;
        const auto& g=cases[type];
        std::string content,expected;
        if(direction==0) {
            content=one_prefix(g.a)+red_image+g.b+" W* n "+blue_image+"Q";
            expected=paint("1 0 0 rg",g.a)+paint("0 0 1 rg",g.intersection);
        } else {
            content=g.a+" W n "+one_prefix(g.b,true)+blue_image+"Q "+small_red;
            expected=paint("0 0 1 rg",g.intersection)+
                     (g.a.empty()?std::string{}:paint("1 0 0 rg",green_box));
        }
        if(expected.empty())expected=full_white;
        rendered reference,actual;
        CHECK(render(original(expected,cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(content,cfg,true),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference));
        if(g.a.empty())CHECK(all_white(actual));
        else CHECK(ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* Patterned Image, clipped blue PATH, restored first-scope green Image,
 * and unclipped yellow PATH use all four operations with two Images and
 * two PATHs. The reference has one Clip ONLY for its first Image, then
 * explicit intersection geometry and three GS0 paints (the old profile). */
static int patterned_mixed_state_references()
{
    const geometry cases[]={
        {rectangle_a,"6 4 4 4 re","6 4 4 4 re"},
        {triangle_a,triangle_b,triangle_i},
        {cubic_a,"6 2 8 8 re",cubic_i}
    };
    for(int type=0;type<3;++type)
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=rotation==270?2:0;
        const auto& g=cases[type];
        const std::string content="1 1 0 rg "+one_prefix(g.a)+patterned+
            one_prefix(g.b,true)+full_blue+"Q "+small_green+"Q "+
            yellow_box+" f";
        const std::string expected=one_prefix(g.a)+patterned+"Q "+
            paint("0 0 1 rg",g.intersection)+small_green+
            paint("1 1 0 rg",yellow_box);
        rendered reference,actual;
        CHECK(render(original(expected,cfg,true),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(content,cfg,true),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
        if(type==0 && dpi==72 && rotation==0 && quarters==4) {
            CHECK(pixel(actual,4,3)==UINT32_C(0xff0000));
            CHECK(pixel(actual,10,3)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,6,9)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,10,9)==UINT32_C(0xffff00));
            CHECK(pixel(actual,8,5)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,3,8)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,13,3)==UINT32_C(0xffff00));
        }
    }
    return EXIT_SUCCESS;
}

/* Both leaves are nine-command, twice-wound rectangles. The first W*
 * makes both scopes empty; only the second W* empties the intersection
 * while Q must recover a visible first leaf. All four rules are checked. */
static int winding_and_empty_restoration()
{
    const std::string a="2 2 m 12 2 l 12 10 l 2 10 l 2 2 l "
                        "12 2 l 12 10 l 2 10 l h";
    const std::string b="6 4 m 14 4 l 14 10 l 6 10 l 6 4 l "
                        "14 4 l 14 10 l 6 10 l h";
    for(int rules=0;rules<4;++rules)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;cfg.rotation=90*rules;
        cfg.crop=rules;cfg.selected=2;
        const bool first_even=(rules&1)!=0,second_even=(rules&2)!=0;
        const std::string first=first_even?"":rectangle_a;
        const std::string intersection=(first_even || second_even)?"":rectangle_i;
        rendered reference,actual;
        CHECK(render(original(reference_four(first,intersection),cfg),
                     cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(state_four(a,b,first_even,second_even),cfg),
                     cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* If no paint uses the first leaf, nested scopes still produce the old
 * one-GS root-only profile. Exercise all three old materializers and
 * both q-before-first and q-between-definitions layouts. */
static int nested_root_only_profiles()
{
    for(int profile=0;profile<3;++profile)
    for(int nesting=0;nesting<2;++nesting)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+profile%2;
        cfg.rotation=((profile+nesting)%4)*90;
        cfg.crop=(profile+nesting)%4;cfg.selected=nesting?2:0;
        const auto& g=shapes[profile];
        const bool with_image=profile!=1;
        const std::string blue_half="0 0 1 rg 0 0 8 12 re f ";
        const std::string paints=profile==0?patterned:
            (profile==1?full_red+blue_half:patterned+blue_half);
        const std::string content=nesting==0?
            one_prefix(g.a)+one_prefix(g.b,true)+paints+"Q Q":
            "q "+one_prefix(g.a)+g.b+" W* n "+paints+"Q Q";
        const std::string expected=one_prefix(g.intersection)+paints+"Q";
        rendered reference,actual;
        CHECK(render(original(expected,cfg,with_image),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(content,cfg,with_image),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* First definition CTM+1, second definition CTM+2, intersection paint
 * CTM0, restored first paint CTM+1, and final paint CTM0. Both Q restores
 * also recover implicit fill color. Independent unclipped coordinates
 * are the reference; split Contents immediately after the second W*. */
static int frozen_ctms_and_streams()
{
    const std::string first="1 1 0 rg q 1 0 0 1 1 0 cm "
        "1 2 10 8 re W n 1 0 0 rg q 1 0 0 1 -1 0 cm "
        "0 0 16 12 re f Q 0 1 0 rg q 1 0 0 1 1 0 cm "
        "4 4 8 6 re W*";
    const std::string second="n 1 0 0 1 -2 0 cm "+full_blue+
        "Q 2 3 2 2 re f Q "+yellow_box+" f";
    const std::string expected=reference_four(rectangle_a,rectangle_i);
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=rotation==270?2:0;
        rendered reference,actual,split;
        CHECK(render(original(expected,cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(first+" "+second,cfg),cfg,actual)==EXIT_SUCCESS);
        CHECK(render(original(first,cfg,false,second),cfg,split)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(split,reference) &&
              ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* The rectangle pair has empty intersection but a nonempty first leaf.
 * Before final Q, an intersection paint must contribute nothing. After
 * Q, the same red paint must again cover exactly half of pixel(7,4).
 * Averaged masks, permanent rectangle reduction, or wrong Q identity
 * each produce a different pixel from the old single-Clip reference. */
static int same_sample_and_first_restoration()
{
    const std::string first="0 0 6 12 re",second="6 0 10 12 re";
    const std::string green="0 1 0 rg 0 0 2 2 re f ";
    const std::string content=one_prefix(first)+green+
        one_prefix(second,true)+full_red+"Q "+full_red+"Q";
    const std::string expected=one_prefix(first)+green+full_red+"Q";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;
        rendered reference,actual;
        CHECK(render(original(expected,cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(content,cfg),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
        if(dpi==72)CHECK(pixel(actual,7,4)==UINT32_C(0xff7f7f));
    }
    return EXIT_SUCCESS;
}

/* Full-canvas billing is unchanged. At 2.5x on 56x42 Media, first-leaf
 * rectangle paints cost 7*16*140*105=1,646,400 samples. The intersection
 * paint costs 10*16*140*105=2,352,000, exceeding 8m/4=2,000,000. The same
 * four-state original on 16x12 Media is a valid compact companion. */
static int fixed_sample_budget()
{
    const std::string content=state_four(triangle_a,triangle_b);
    configuration cfg;
    cfg.dpi=144;cfg.quarters=5;cfg.selected=2;
    rendered compact;
    CHECK(render(original(content,cfg),cfg,compact)==EXIT_SUCCESS);
    CHECK(ink_and_white(compact));
    cfg.large_media=true;
    CHECK(rejected(content,cfg,false,QUANTAPDF_ERROR_UNSUPPORTED)
          ==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

/* New scope transitions do not permit a third definition, reentry
 * after restored state 0, an unused second Clip,
 * malformed paths, or widened Image/mixed/path and work budgets. */
static int atomic_refusals()
{
    const std::string a=one_prefix(rectangle_a);
    const std::string b=one_prefix(rectangle_b,true);
    const std::string ready=a+full_red+b+full_blue+"Q Q";
    const std::vector<std::string> bad={
        a+full_red+b+full_blue+rectangle_a+" W n Q Q",
        a+full_red+b+full_blue+"Q "+rectangle_a+" W n "+full_red+"Q",
        a+full_red+"Q "+b+full_blue+"Q",
        /* Prefix, first, intersection, restored first, and suffix are
         * five paints, beyond the unchanged four-layer ceiling. */
        full_red+a+full_red+b+full_blue+"Q "+full_red+"Q "+full_red,
        a+full_red+b+"Q "+full_red+"Q", /* second Clip never painted */
        a+full_red+"q W n Q "+full_red+"Q", /* empty second still unused */
        a+rectangle_b+" W n Q", /* no paint in either Clip */
        a+b+patterned+full_red+patterned+"Q Q", /* old root-only 3 mixed */
        a+red_image+b+blue_image+"Q "+red_image+"Q", /* third Image */
        a+full_red+b+full_blue+full_red+"Q "+full_red+full_red+"Q",
        a+full_red+b+full_blue+patterned+"Q "+full_red+"Q", /* 3PATH+Image */
        a+full_red+b+full_blue+"Q "+full_red+patterned+"Q", /* late Image */
        a+patterned+b+full_blue+patterned+"Q "+patterned+"Q",
        ready+" BT ET",
        ready+" 2 2 m 10 8 l S",
        ready+" 2 g",
        ready+" Q",
        ready+" q",
        a+full_red+"q "+rectangle_b+" W "+full_blue+"Q Q",
        a+full_red+"q W* 1 n "+full_blue+"Q Q",
        a+full_red+"q 2 2 m 14 2 l 8 10 l h h W n "+full_blue+"Q Q",
        a+full_red+"q 2.125 2 m 14 2 l 8 10 l W n "+full_blue+"Q Q",
        a+full_red+"q 2 2 m 14 2 l 8 -1000001 l W n "+full_blue+"Q Q",
        a+full_red+"q 2 2 m 8 2 l 12 4 l 14 8 l 10 10 l "
            "4 10 l 2 8 l 1 4 l 1 3 l W n "+full_blue+"Q Q"
    };
    for(const auto& content:bad)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.crop=dpi==72?0:(dpi==96?2:3);
        cfg.selected=dpi==144?2:0;
        CHECK(rejected(content,cfg,content.find(" Do")!=std::string::npos)
              ==EXIT_SUCCESS);
    }
    std::string over_operators;
    for(int i=0;i<256;++i)over_operators+="1 w ";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;
        CHECK(rejected(over_operators+ready,cfg,false,
                       QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
        CHECK(rejected(a+std::string(65536u,' ')+full_red+b+full_blue+"Q Q",
                       cfg,false,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(four_path_state_references()==EXIT_SUCCESS);
    CHECK(path_state_sequences()==EXIT_SUCCESS);
    CHECK(image_state_references()==EXIT_SUCCESS);
    CHECK(patterned_mixed_state_references()==EXIT_SUCCESS);
    CHECK(winding_and_empty_restoration()==EXIT_SUCCESS);
    CHECK(nested_root_only_profiles()==EXIT_SUCCESS);
    CHECK(frozen_ctms_and_streams()==EXIT_SUCCESS);
    CHECK(same_sample_and_first_restoration()==EXIT_SUCCESS);
    CHECK(fixed_sample_budget()==EXIT_SUCCESS);
    CHECK(atomic_refusals()==EXIT_SUCCESS);
    std::printf("restored Clip-state original PDFs: %zu successful renders, "
                "%zu atomic refusals\n",positive_calls,negative_calls);
    return EXIT_SUCCESS;
}

