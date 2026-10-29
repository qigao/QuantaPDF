/* Public Source V10 original-PDF compound Clip-contour conformance.
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
    std::fprintf(stderr,"Native compound Clip failed at %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-compound-clip-source.pdf";
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
 * A separate 44x32 original deliberately exceeds that budget below.
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
        "<< /Type /Page /MediaBox [0 0 44 32] >>":
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
        std::fprintf(stderr,"compound Clip rejected: quarters=%d "
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
    const int full_w=cfg.large_media?176:64;
    const int full_h=cfg.large_media?128:48;
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
        std::fprintf(stderr,"compound Clip unexpected acceptance: "
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

static const std::string full_red="1 0 0 rg 0 0 16 12 re f ";
static const std::string full_blue="0 0 1 rg 0 0 16 12 re f* ";
static const std::string full_white="1 1 1 rg 0 0 16 12 re f ";
static const std::string patterned="q 16 0 0 12 0 0 cm /I Do Q ";
static const std::string red_image="q 16 0 0 12 0 0 cm /R Do Q ";
static const std::string blue_image="q 16 0 0 12 0 0 cm /B Do Q ";
static const std::string green_image="q 1 0 0 1 10 5 cm /G Do Q ";
static const std::string yellow_box="14 9 1 1 re";
static const std::string selector="11 3 1 1 re";
static const std::string outside_hole="10 5 1 1 re";
static const std::string inside_hole="7 4 1 1 re";

static const std::string triangle_a="2 2 m 10 2 l 6 10 l h";
static const std::string triangle_b="6 2 m 14 2 l 10 10 l h";
static const std::string triangle_b_reverse="6 2 m 10 10 l 14 2 l h";
static const std::string triangle_a_reverse="2 2 m 6 10 l 10 2 l h";
/* Analytically derived union and symmetric difference of the preceding
 * overlapping triangles. Each reference is one old, bounded contour.
 * The repeated contact point in xor_outline preserves shared samples;
 * separately compositing its two lobes would not be an exact oracle. */
static const std::string union_outline=
    "2 2 m 14 2 l 10 10 l 8 6 l 6 10 l h";
static const std::string xor_outline=
    "2 2 m 6 2 l 8 6 l 10 2 l 14 2 l 10 10 l 8 6 l 6 10 l h";
static const std::string outer_triangle="2 2 m 14 2 l 8 10 l h";
static const std::string inner_triangle="6 4 m 10 4 l 8 7 l h";
static const std::string inner_reverse="6 4 m 8 7 l 10 4 l h";
/* A retraced bridge joins the outer triangle to the oppositely wound
 * hole. This independent nine-command SINGLE contour is accepted by
 * the old PATH and Clip profiles, including fractional viewport edges. */
static const std::string hole_outline=
    "2 2 m 6 4 l 8 7 l 10 4 l 6 4 l 2 2 l 14 2 l 8 10 l h";
static const std::string outer_rectangle="2 2 12 8 re";
static const std::string notch_triangle="2 2 m 8 2 l 5 6 l h";
static const std::string notch_outline=
    "2 2 m 5 6 l 8 2 l 14 2 l 14 10 l 2 10 l h";
static const std::string empty_point="11 8 m 11 8 l 11 8 l h";
static const std::string empty_line="11 2 m 13 4 l 11 2 l h";

static std::string one_prefix(const std::string& path,bool even=false)
{
    return "q "+path+(even?" W* n ":" W n ");
}
static std::string paint(const char *color,const std::string& path)
{
    return path.empty()?std::string{}:std::string(color)+" "+path+" f ";
}
static std::string red_reference(const std::string& path)
{
    return path.empty()?full_white:paint("1 0 0 rg",path);
}
static std::string two_contours(const std::string& a,const std::string& b)
{
    return a+" "+b;
}

/* Genuine holes, overlapping and disjoint components, coincident
 * components, and opposite winding all have explicit old PATH oracles.
 * No reference draws white over red: that construction can be wrong
 * when a fractional viewport intersects both coverage boundaries. */
static int independent_topologies()
{
    const std::string disjoint_a="1 2 m 5 2 l 3 6 l h";
    const std::string disjoint_b="11 2 m 15 2 l 13 6 l h";
    const std::string disjoint_outline=
        "1 2 m 5 2 l 3 6 l 1 2 l 11 2 l 15 2 l 13 6 l 11 2 l h";
    struct topology { std::string first,second,nonzero,even_odd; };
    const topology cases[]={
        {triangle_a,triangle_b,union_outline,xor_outline},
        {triangle_a,triangle_b_reverse,xor_outline,xor_outline},
        {outer_triangle,inner_triangle,outer_triangle,hole_outline},
        {outer_triangle,inner_reverse,hole_outline,hole_outline},
        {disjoint_a,disjoint_b,disjoint_outline,disjoint_outline},
        {triangle_a,triangle_a,triangle_a,""},
        {triangle_a,triangle_a_reverse,"",""},
        {outer_rectangle,notch_triangle,outer_rectangle,notch_outline},
        {"14 2 -12 8 re",notch_triangle,notch_outline,notch_outline},
        {"14 10 -12 -8 re",notch_triangle,outer_rectangle,notch_outline}
    };
    for(int type=0;type<10;++type)
    for(bool even:{false,true})
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+(type+(even?1:0))%2;
        cfg.rotation=(type%4)*90;cfg.crop=(type+(even?1:0))%4;
        cfg.selected=type%3==0?2:0;
        if(type==0 && dpi==72) {
            cfg.quarters=4;cfg.rotation=0;cfg.crop=0;
        }
        const auto& item=cases[type];
        /* At 96 DPI the nine-command rectangle cases put re SECOND,
         * after an explicit triangle h, rather than only testing m. */
        const std::string clip=type>=7 && dpi==96?
            two_contours(item.second,item.first):
            two_contours(item.first,item.second);
        const std::string& expected=even?item.even_odd:item.nonzero;
        rendered reference,path,image;
        CHECK(render(original(red_reference(expected),cfg),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(one_prefix(clip,even)+full_red+"Q",cfg),
                     cfg,path)==EXIT_SUCCESS);
        CHECK(render(original(one_prefix(clip,even)+red_image+"Q",cfg,true),
                     cfg,image)==EXIT_SUCCESS);
        CHECK(equal(path,reference) && equal(image,reference));
        if(expected.empty())CHECK(all_white(path));
        else CHECK(ink_and_white(path));
        if(type==0 && dpi==72) {
            CHECK(pixel(path,4,7)==UINT32_C(0xff0000));
            CHECK(pixel(path,8,8)==(even?UINT32_C(0xffffff):
                                      UINT32_C(0xff0000)));
            /* Pixel (8,7) spans source x=8..9,y=4..5 and crosses the
             * overlap edge 2x+y=22. Exactly four of its sixteen
             * samples lie in the XOR region; it is not all white. */
            CHECK(pixel(path,8,7)==(even?UINT32_C(0xffbfbf):
                                      UINT32_C(0xff0000)));
            CHECK(pixel(path,0,0)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

/* Compound first, compound second, and two separately owned compounds
 * all retain the one-GS root-only profile. Image-only, PATH-only, and
 * two-inside Image+PATH originals use independent old single-Clip
 * references. The second leaf also exercises implicit final closure. */
static int compound_leaf_positions()
{
    struct shape { std::string a,b,outline;bool even; };
    const shape cases[]={
        {outer_triangle,inner_triangle,hole_outline,true},
        {triangle_a,triangle_b,union_outline,false},
        {outer_rectangle,notch_triangle,notch_outline,true}
    };
    for(int type=0;type<3;++type)
    for(int position=0;position<3;++position)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+(type+position)%2;
        cfg.rotation=((type+position)%4)*90;
        cfg.crop=(type+position)%4;cfg.selected=position==2?2:0;
        const auto& item=cases[type];
        const std::string compound=two_contours(item.a,item.b);
        const std::string swapped=two_contours(item.b,item.a);
        const std::string first=position==1?selector:compound;
        std::string second=position==0?selector:
                           (position==1?compound:swapped);
        if(position==1 && dpi==96)second.pop_back();
        const bool first_even=position==1?false:item.even;
        const bool second_even=position==0?false:item.even;
        const std::string expected=position==2?item.outline:selector;
        const bool with_image=position!=1;
        const std::string draw=position==2?
            patterned+"0 0 1 rg 0 0 8 12 re f ":
            (with_image?patterned:full_red);
        const std::string content=one_prefix(first,first_even)+second+
            (second_even?" W* n ":" W n ")+draw+"Q";
        const std::string oracle=with_image?
            one_prefix(expected)+draw+"Q":red_reference(expected);
        rendered reference,actual;
        CHECK(render(original(oracle,cfg,with_image),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(content,cfg,with_image),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    /* Two disconnected components on EACH leaf cross geometrically.
     * Their independently derived intersection is two smaller triangles,
     * encoded as one old nine-command retraced-bridge contour. */
    const std::string a="1 2 m 7 2 l 1 10 l h 9 2 m 15 2 l 9 10 l h";
    const std::string b="1 2 m 7 2 l 7 10 l h 9 2 m 15 2 l 15 10 l h";
    const std::string intersection=
        "1 2 m 7 2 l 4 6 l 1 2 l 9 2 l 15 2 l 12 6 l 9 2 l h";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;cfg.selected=2;
        cfg.rotation=dpi==72?0:(dpi==96?90:270);
        cfg.crop=dpi==72?0:(dpi==96?2:3);
        rendered reference,actual,swapped;
        CHECK(render(original(red_reference(intersection),cfg),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(one_prefix(a)+b+" W* n "+red_image+"Q",
                              cfg,true),cfg,actual)==EXIT_SUCCESS);
        CHECK(render(original(one_prefix(b,true)+a+" W n "+red_image+"Q",
                              cfg,true),cfg,swapped)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(swapped,reference));
    }
    return EXIT_SUCCESS;
}

static std::string four_states(const std::string& first,bool first_even,
                               const std::string& second,bool second_even,
                               const std::string& green)
{
    return "1 1 0 rg "+one_prefix(first,first_even)+full_red+"0 1 0 rg "+
        one_prefix(second,second_even)+full_blue+"Q "+green+
        " f Q "+yellow_box+" f";
}
static std::string four_reference(const std::string& first,
                                  const std::string& intersection,
                                  const std::string& green)
{
    return paint("1 0 0 rg",first)+paint("0 0 1 rg",intersection)+
        paint("0 1 0 rg",green)+paint("1 1 0 rg",yellow_box);
}

/* First -> intersection -> restored first -> none uses exactly four
 * PATHs. Compound leaves occur first, second, and in BOTH positions.
 * An inner hole under GS2 becomes green only after Q restores GS1. */
static int restored_compound_states()
{
    const std::string compound=two_contours(outer_triangle,inner_triangle);
    for(int position=0;position<3;++position)
    for(int dpi:{72,96,144})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=quarters;
        cfg.rotation=((position+quarters-4)%4)*90;
        cfg.crop=(position+quarters-4)%4;cfg.selected=position?2:0;
        const std::string first=position==1?outer_triangle:compound;
        const std::string second=position==0?selector:compound;
        const std::string first_outline=position==0?hole_outline:outer_triangle;
        const std::string intersection=position==0?selector:hole_outline;
        const std::string green=position==0?outside_hole:inside_hole;
        rendered reference,actual;
        CHECK(render(original(four_reference(first_outline,intersection,green),
                              cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(four_states(first,position==0,second,true,green),
                              cfg),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    /* Image-only cardinality stays exactly two. The first W is outside
     * q, and its first paint occurs only AFTER an intersection Image.
     * The second layout restores a visible area from inside GS2's hole. */
    for(int layout=0;layout<2;++layout)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+layout;
        cfg.rotation=layout?270:0;cfg.crop=dpi==96?2:0;cfg.selected=2;
        const std::string first=layout?outer_triangle:compound;
        const std::string second=layout?compound:selector;
        const std::string intersection=layout?hole_outline:selector;
        const std::string small=layout?inside_hole:outside_hole;
        const std::string small_image=layout?
            "q 1 0 0 1 7 4 cm /R Do Q":"q 1 0 0 1 10 5 cm /R Do Q";
        const std::string content=first+" W* n "+one_prefix(second,true)+
            blue_image+"Q "+small_image;
        const std::string expected=paint("0 0 1 rg",intersection)+
            paint("1 0 0 rg",small);
        rendered reference,actual;
        CHECK(render(original(expected,cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(content,cfg,true),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* Both compound leaves freeze different definition-time CTMs. The
 * first patterned Image, blue intersection PATH, restored green Image,
 * and final yellow PATH retain the existing 2 Image + 2 PATH maximum.
 * Reference: old single-Clip patterned Image and three GS0 paints.
 * Split Contents either between contours, or between second W* and n. */
static int transformed_pattern_and_streams()
{
    const std::string first="1 1 0 rg q 1 0 0 1 1 0 cm "
        "1 2 12 8 re 1 2 m 7 2 l 4 6 l h W* n "
        "q 1 0 0 1 -1 0 cm 16 0 0 12 0 0 cm /I Do Q "
        "q 1 0 0 1 1 0 cm 9 3 1 1 re";
    const std::string middle="10 8 m 10 8 l 10 8 l h W*";
    const std::string last="n 1 0 0 1 -2 0 cm "+full_blue+
        "Q q 1 0 0 1 9 5 cm /G Do Q Q "+yellow_box+" f";
    const std::string expected=one_prefix(notch_outline)+patterned+"Q "+
        paint("0 0 1 rg",selector)+green_image+paint("1 1 0 rg",yellow_box);
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=rotation==270?2:0;
        rendered reference,actual,split;
        CHECK(render(original(expected,cfg,true),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(first+" "+middle+" "+last,cfg,true),cfg,actual)
              ==EXIT_SUCCESS);
        const bool between_contours=rotation==0 || rotation==180;
        const std::string split_first=between_contours?first:first+" "+middle;
        const std::string split_last=between_contours?middle+" "+last:last;
        CHECK(render(original(split_first,cfg,true,split_last),cfg,split)
              ==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(split,reference) &&
              ink_and_white(actual));
        if(dpi==72 && rotation==0 && quarters==4) {
            CHECK(pixel(actual,3,3)==UINT32_C(0xff0000));
            CHECK(pixel(actual,11,3)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,2,7)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,8,9)==UINT32_C(0xffff00));
            CHECK(pixel(actual,4,9)==UINT32_C(0xffffff));
            CHECK(pixel(actual,11,8)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,10,6)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,14,2)==UINT32_C(0xffff00));
        }
    }
    return EXIT_SUCCESS;
}

/* Each five-command curved contour plus one four-command triangle
 * exactly reaches nine commands. References use an independently
 * authored one-contour curved notch, or the original unnotched cap.
 * v/y are expanded to c ONLY in the reference. Alternate contour order
 * checks that shorthand starts from the new contour's MOVE point.
 * At 96 DPI the final h is implicit: eight commands become exactly nine. */
static int curved_contours_and_implicit_close()
{
    struct curve { const char *authored;const char *expanded; };
    const curve curves[]={
        {"2 10 10 10 10 2 c","2 10 10 10 10 2 c"},
        {"10 10 10 2 v","2 2 10 10 10 2 c"},
        {"2 10 10 2 y","2 10 10 2 10 2 c"}
    };
    const std::string notch="2 2 m 6 2 l 4 3 l h";
    const std::string reversed="2 2 m 4 3 l 6 2 l h";
    for(int kind=0;kind<3;++kind)
    for(bool even:{false,true})
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+kind%2;
        cfg.rotation=kind*90;cfg.crop=(kind+(even?1:0))%4;cfg.selected=2;
        const auto& curve=curves[kind];
        const std::string curved=std::string("2 2 m ")+curve.authored+
            " 10 2 l 2 2 l h";
        /* The cap is clockwise, so the reversed triangle has the SAME
         * orientation. The other two cases subtract under nonzero W. */
        const bool same_winding=kind==1;
        const std::string inner=same_winding?reversed:notch;
        std::string compound=(kind+(even?1:0))%2==0?
            two_contours(curved,inner):two_contours(inner,curved);
        if(dpi==96)compound.pop_back();
        const std::string outline=std::string("2 2 m ")+curve.expanded+
            ((even || !same_winding)?" 6 2 l 4 3 l h":" 2 2 l h");
        const bool with_image=kind==1;
        const std::string draw=with_image?red_image:full_red;
        rendered reference,actual;
        CHECK(render(original(red_reference(outline),cfg),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(one_prefix(compound,even)+draw+"Q",
                              cfg,with_image),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* An explicitly closed degenerate contour contributes zero winding;
 * it must not erase its nonempty sibling or create phantom edges.
 * These are actual M + two LINE + CLOSE contours, not consecutive MOVE
 * commands or zero-width re operands outside the bounded contract. */
static int degenerate_contour_membership()
{
    const std::string paths[]={
        two_contours(outer_triangle,empty_point),
        two_contours(empty_line,outer_triangle),
        two_contours(empty_point,empty_line)
    };
    for(int type=0;type<3;++type)
    for(bool even:{false,true})
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;cfg.rotation=type*90;
        cfg.crop=(type+(even?1:0))%4;cfg.selected=2;
        const bool with_image=type!=1;
        const std::string draw=with_image?red_image:full_red;
        rendered reference,actual;
        CHECK(render(original(type==2?full_white:red_reference(outer_triangle),
                              cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(one_prefix(paths[type],even)+draw+"Q",
                              cfg,with_image),cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference));
        if(type==2)CHECK(all_white(actual));
        else CHECK(ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* Two triangular contours partition a rectangle. At pixel (7,7), the
 * contours cover complementary samples. Union/parity must be resolved
 * before averaging; two separately averaged masks would leave a seam. */
static int shared_sample_contours()
{
    const std::string compound=
        "2 2 m 10 2 l 2 10 l h 10 2 m 10 10 l 2 10 l h";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;
        rendered reference;
        CHECK(render(original(red_reference("2 2 8 8 re"),cfg),cfg,reference)
              ==EXIT_SUCCESS);
        for(bool even:{false,true}) {
            rendered actual;
            CHECK(render(original(one_prefix(compound,even)+red_image+"Q",
                                  cfg,true),cfg,actual)==EXIT_SUCCESS);
            CHECK(equal(actual,reference));
            if(dpi==72)CHECK(pixel(actual,7,7)==UINT32_C(0xff0000));
        }
    }
    return EXIT_SUCCESS;
}

/* Four layers retain the fixed 8m/4=2m sample allowance. On 44x32
 * Media at scale 2.5, the 110x80 first-scope rectangle costs
 * (6+4)*16*8800=1,408,000. The two-compound intersection costs
 * (6+6+4)*16*8800=2,252,800 and must refuse atomically. */
static int fixed_sample_budget()
{
    const std::string compound=two_contours(outer_triangle,inner_triangle);
    const std::string content=four_states(compound,false,compound,true,
                                           inside_hole);
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

/* New contour staging applies only to W/W* n. Painted PATH and plain n
 * keep their old single-contour admission; every late error clears a
 * nonnull Bitmap sentinel. Quotas and graph/cardinality gates stay fixed. */
static int atomic_refusals()
{
    const std::string compound=two_contours(outer_triangle,inner_triangle);
    const std::string prefix=one_prefix(compound,true);
    const std::string ready=prefix+full_red+"Q";
    const std::vector<std::string> bad={
        /* Two re contours are ten commands, even though each is valid. */
        "q 2 2 12 8 re 6 4 4 3 re W n "+full_red+"Q",
        "q "+outer_rectangle+" 6 4 m 10 4 l 8 7 l 7 6 l h W n "+
            full_red+"Q",
        "q "+outer_rectangle+" 6 4 m 10 4 l 8 7 l 7 6 l W n "+
            full_red+"Q", /* implicit close would be command ten */
        "q "+compound+" "+empty_point+" W n "+full_red+"Q",
        "q 2 2 m 14 2 l 8 10 l "+inner_triangle+" W n "+full_red+"Q",
        "q "+outer_triangle+" 8 8 m h W n "+full_red+"Q",
        "q "+outer_triangle+" 8 8 m 10 8 l h W n "+full_red+"Q",
        "q "+outer_triangle+" 8 8 m 10 8 l W n "+full_red+"Q",
        "q 4 4 m 8 8 m W n "+full_red+"Q",
        "q "+outer_triangle+" 4 4 l W n "+full_red+"Q",
        "q "+compound+" h W n "+full_red+"Q",
        "q "+outer_triangle+" W "+inner_triangle+" n "+full_red+"Q",
        "q "+compound+" W 1 n "+full_red+"Q",
        "q "+outer_triangle+" 6 4 0 3 re W n "+full_red+"Q",
        "q "+outer_triangle+" 6.125 4 m 10 4 l 8 7 l h W n "+
            full_red+"Q",
        "q "+outer_triangle+" 6 4 m 10 4 l 8 -1000001 l h W n "+
            full_red+"Q",
        "1 0 0 rg "+compound+" f", /* compound unclipped PATH */
        one_prefix(outer_rectangle)+"1 0 0 rg "+compound+" f* Q",
        compound+" n "+full_red, /* compound ignored path */
        one_prefix(outer_rectangle)+compound+" n "+full_red+"Q",
        prefix+full_red+compound+" W n Q", /* unused second Clip */
        prefix+full_red+compound+" W n "+full_blue+compound+" W n Q",
        ready+" "+one_prefix(compound)+full_red+"Q", /* reentry after 0 */
        full_red+prefix+full_red+"Q", /* paint before first Clip */
        prefix+full_red+full_red+full_red+full_red+full_red+"Q",
        prefix+red_image+red_image+red_image+"Q",
        prefix+full_red+full_red+full_red+red_image+"Q",
        ready+" BT ET",
        ready+" 2 2 m 10 8 l S",
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
    std::string over_operators;
    for(int i=0;i<256;++i)over_operators+="1 w ";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;
        CHECK(rejected(over_operators+ready,cfg,false,
                       QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
        CHECK(rejected(prefix+std::string(65536u,' ')+full_red+"Q",cfg,false,
                       QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(independent_topologies()==EXIT_SUCCESS);
    CHECK(compound_leaf_positions()==EXIT_SUCCESS);
    CHECK(restored_compound_states()==EXIT_SUCCESS);
    CHECK(transformed_pattern_and_streams()==EXIT_SUCCESS);
    CHECK(curved_contours_and_implicit_close()==EXIT_SUCCESS);
    CHECK(degenerate_contour_membership()==EXIT_SUCCESS);
    CHECK(shared_sample_contours()==EXIT_SUCCESS);
    CHECK(fixed_sample_budget()==EXIT_SUCCESS);
    CHECK(atomic_refusals()==EXIT_SUCCESS);
    std::printf("compound Clip-contour original PDFs: %zu successful renders, "
                "%zu atomic refusals\n",positive_calls,negative_calls);
    return EXIT_SUCCESS;
}

