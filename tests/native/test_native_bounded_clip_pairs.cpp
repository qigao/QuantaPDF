/* Public Source V10 original-PDF bounded two-Clip conformance.
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
    std::fprintf(stderr,"Native bounded Clip pair failed at %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-bounded-clip-pairs.pdf";
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
 * A separate 64x48 original deliberately exceeds that budget below.
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
        "<< /Type /Page /MediaBox [0 0 64 48] >>":
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
        std::fprintf(stderr,"bounded Clip pair rejected: quarters=%d "
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
    const int full_w=cfg.large_media?256:64;
    const int full_h=cfg.large_media?192:48;
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
        std::fprintf(stderr,"bounded Clip pair unexpected acceptance: "
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

static const std::string triangle_a="2 2 m 14 2 l 2 10 l h";
static const std::string triangle_b="2 2 m 14 2 l 14 10 l h";
static const std::string triangle_intersection="2 2 m 14 2 l 8 6 l h";
static const std::string full_box="0 0 16 12 re";
static const std::string image_draw="q 16 0 0 12 0 0 cm /I Do Q ";
static const std::string full_red="1 0 0 rg 0 0 16 12 re f ";
static const std::string full_blue="0 0 1 rg 0 0 16 12 re f* ";
static const std::string full_green="0 1 0 rg 0 0 16 12 re f ";
static const std::string full_yellow="1 1 0 rg 0 0 16 12 re f* ";
static const std::string full_white="1 1 1 rg 0 0 16 12 re f ";

static std::string one_prefix(const std::string& path,bool even=false)
{
    return "q "+path+(even?" W* n ":" W n ");
}
static std::string pair_prefix(const std::string& a,const std::string& b,
                               bool even_a=false,bool even_b=false)
{
    return one_prefix(a,even_a)+b+(even_b?" W* n ":" W n ");
}
static std::string intersection_reference(const std::string& path,
                                           bool with_image)
{
    return with_image?one_prefix(path)+image_draw+"Q":
                      "1 0 0 rg "+path+" f";
}

/* These intersections are independently derived from original line
 * equations. In particular the crossing triangles meet at (8,6), the
 * concave L stays concave, and the bow tie's upper half is one triangle.
 * Image references have ONE Clip; PATH references have NO Clip at all. */
static int analytic_intersections()
{
    struct example { std::string a,b,intersection; };
    const example cases[]={
        {triangle_a,triangle_b,triangle_intersection},
        {triangle_a,"2 2 6 8 re","2 2 m 8 2 l 8 6 l 2 10 l h"},
        {"2 2 m 14 2 l 14 5 l 6 5 l 6 10 l 2 10 l h",
         "4 3 8 6 re","4 3 m 12 3 l 12 5 l 6 5 l 6 9 l 4 9 l h"},
        {"2 2 m 2 10 l 14 10 l 14 2 l h",full_box,"2 2 12 8 re"},
        {"2 2 m 14 10 l 2 10 l 14 2 l h","0 6 16 6 re",
         "8 6 m 14 10 l 2 10 l h"}
    };
    for(int shape=0;shape<5;++shape)
    for(int dpi:{72,96,144})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=quarters;
        cfg.rotation=((shape+quarters-4)%4)*90;
        cfg.crop=(shape+quarters-4)%4;
        cfg.selected=shape%2?2:0;
        if(shape==3 && quarters==4) {cfg.rotation=0;cfg.crop=0;}
        const auto& item=cases[shape];
        const bool even_a=(shape&1)!=0,even_b=quarters==5;
        rendered image_reference,image_actual,swapped;
        CHECK(render(original(intersection_reference(item.intersection,true),
                              cfg,true),cfg,image_reference)==EXIT_SUCCESS);
        CHECK(render(original(pair_prefix(item.a,item.b,even_a,even_b)+
                              image_draw+"Q",cfg,true),cfg,image_actual)
              ==EXIT_SUCCESS);
        CHECK(render(original(pair_prefix(item.b,item.a,even_b,even_a)+
                              image_draw+"Q",cfg,true),cfg,swapped)
              ==EXIT_SUCCESS);
        CHECK(equal(image_actual,image_reference) &&
              equal(swapped,image_reference) && ink_and_white(image_actual));
        rendered path_reference,path_actual;
        CHECK(render(original(intersection_reference(item.intersection,false),
                              cfg),cfg,path_reference)==EXIT_SUCCESS);
        CHECK(render(original(pair_prefix(item.a,item.b,even_a,even_b)+
                              full_red+"Q",cfg),cfg,path_actual)==EXIT_SUCCESS);
        CHECK(equal(path_actual,path_reference) && ink_and_white(path_actual));
        if(shape==0 && dpi==72 && quarters==4) {
            CHECK(pixel(image_actual,8,8)==UINT32_C(0xffff00));
            CHECK(pixel(image_actual,3,4)==UINT32_C(0xffffff));
            CHECK(pixel(image_actual,12,4)==UINT32_C(0xffffff));
            CHECK(pixel(image_actual,2,5)==UINT32_C(0xffffff));
            const uint32_t edge=pixel(image_actual,5,7);
            const unsigned red=(edge>>16u)&255u;
            CHECK((edge&255u)==255u && ((edge>>8u)&255u)==red &&
                  red>0u && red<255u);
        }
        if(shape==3 && dpi==72 && quarters==4) {
            CHECK(pixel(image_actual,4,4)==UINT32_C(0xff0000));
            CHECK(pixel(image_actual,10,4)==UINT32_C(0x00ff00));
            CHECK(pixel(image_actual,4,8)==UINT32_C(0x0000ff));
            CHECK(pixel(image_actual,10,8)==UINT32_C(0xffff00));
        }
    }
    return EXIT_SUCCESS;
}

/* These all-LINE paths make W and W* observably different. One repeats
 * a triangle (seven commands); the other repeats a rectangle (nine).
 * Preserve the rule on either owned leaf, with all four rule pairs and
 * both Image-only and PATH-only source profiles represented. */
static int double_winding_rules()
{
    struct example { std::string doubled,other,intersection; };
    const example cases[]={
        {"2 2 m 14 2 l 8 10 l 2 2 l 14 2 l 8 10 l h",
         full_box,"2 2 m 14 2 l 8 10 l h"},
        {"2 2 m 14 2 l 14 10 l 2 10 l 2 2 l "
         "14 2 l 14 10 l 2 10 l h",
         "2 2 m 14 2 l 8 10 l h","2 2 m 14 2 l 8 10 l h"}
    };
    for(int shape=0;shape<2;++shape)
    for(int dpi:{72,96,144})
    for(int rules=0;rules<4;++rules) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=90*rules;
        cfg.quarters=4+(shape+rules)%2;
        cfg.crop=(shape+rules)%4;cfg.selected=rules==3?2:0;
        const auto& item=cases[shape];
        const bool even=(rules&1)!=0,other_even=(rules&2)!=0;
        const bool with_image=(rules&2)!=0;
        const std::string paint=with_image?image_draw:full_red;
        rendered reference,actual,swapped;
        CHECK(render(original(even?full_white:
                              intersection_reference(item.intersection,
                                                     with_image),
                              cfg,with_image && !even),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(pair_prefix(item.doubled,item.other,
                                          even,other_even)+paint+"Q",
                              cfg,with_image),cfg,actual)==EXIT_SUCCESS);
        CHECK(render(original(pair_prefix(item.other,item.doubled,
                                          other_even,even)+paint+"Q",
                              cfg,with_image),cfg,swapped)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(swapped,reference));
        if(even)CHECK(all_white(actual));
        else CHECK(ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* Literal empty, MOVE-only and open MOVE+LINE each canonicalize to a
 * bounded zero-edge leaf. Check first, second and BOTH positions under
 * Image-only, four-PATH-only and Image+PATH/Q-restored-PATH profiles.
 * The restored green rectangle lies outside each nonempty partner. */
static int empty_leaf_positions()
{
    const std::string empty[]={"","4 3 m","4 3 m 10 7 l"};
    const std::string partner[]={triangle_a,
        "2 2 m 2 8 l 10 8 l 10 2 l h","2 2 8 6 re"};
    const std::string restored="0 1 0 rg 11 8 2 2 re f";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;cfg.selected=2;
        cfg.rotation=dpi==72?0:(dpi==96?90:270);
        cfg.crop=dpi==72?0:(dpi==96?2:3);
        rendered green_reference;
        CHECK(render(original(restored,cfg),cfg,green_reference)==EXIT_SUCCESS);
        CHECK(ink_and_white(green_reference));
        for(int shape=0;shape<3;++shape)
        for(int position=0;position<3;++position)
        for(int profile=0;profile<3;++profile) {
            const std::string a=position==1?partner[shape]:empty[shape];
            const std::string b=position==0?partner[shape]:empty[shape];
            std::string content=pair_prefix(a,b,(shape&1)!=0,
                                            (position&1)!=0);
            if(profile==0)content+=image_draw+"Q";
            else if(profile==1)
                content+=full_red+full_blue+full_green+full_yellow+"Q";
            else content+=image_draw+full_blue+"Q "+restored;
            rendered actual;
            CHECK(render(original(content,cfg,profile!=1),cfg,actual)
                  ==EXIT_SUCCESS);
            if(profile==2)CHECK(equal(actual,green_reference));
            else CHECK(all_white(actual));
        }
    }
    return EXIT_SUCCESS;
}

/* Implicit closing applies to the SECOND contour with exactly the same
 * bound as explicit h. The octagon has eight pre-close commands, so its
 * accepted owned leaf has nine. Include an actual open cubic regression
 * and an open canonical rectangle that still takes exact reduction.
 * Split every case between its second W/W* and the required n. */
static int implicit_second_closure()
{
    const std::string open[]={
        "2 2 m 14 2 l 2 10 l",
        "2 2 m 14 2 l 14 5 l 6 5 l 6 10 l 2 10 l",
        "2 2 m 8 2 l 12 4 l 14 8 l 10 10 l 4 10 l 2 8 l 1 4 l",
        "2 3 m 2 11 10 11 10 3 c 10 2 l 2 2 l",
        "2 2 m 14 2 l 14 10 l 2 10 l"
    };
    for(int shape=0;shape<5;++shape)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=4+shape%2;
        cfg.rotation=(shape%4)*90;
        cfg.crop=(shape+(dpi==144?1:0))%4;
        cfg.selected=dpi==96?2:0;
        const bool with_image=shape%2==0;
        const std::string closed=open[shape]+" h";
        const std::string first=one_prefix(full_box)+open[shape]+
                                 (shape%2?" W":" W*");
        const std::string second="n "+
            (with_image?image_draw:full_red)+"Q";
        rendered reference,actual,split;
        CHECK(render(original(intersection_reference(closed,with_image),
                              cfg,with_image),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(first+" "+second,cfg,with_image),cfg,actual)
              ==EXIT_SUCCESS);
        CHECK(render(original(first,cfg,with_image,second),cfg,split)
              ==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(split,reference) &&
              !all_white(actual));
    }
    return EXIT_SUCCESS;
}

/* The first triangle is defined under +1, the second under +2 and is
 * implicitly closed. Paint under identity, with a real Q-restored green
 * suffix. The independent original uses the analytically derived single
 * intersection triangle, not either input leaf or a projected IR graph. */
static int frozen_ctms_mixed_and_streams()
{
    const std::string first="q 1 0 0 1 1 0 cm "
        "1 2 m 13 2 l 1 10 l h W n 1 0 0 1 1 0 cm "
        "0 2 m 12 2 l 12 10 l W*";
    const std::string paints=image_draw+
        "0 0 1 rg 0 0 10 12 re f Q 0 1 0 rg 0 10 16 2 re f";
    const std::string second="n 1 0 0 1 -2 0 cm "+paints;
    const std::string reference_content=one_prefix(triangle_intersection)+paints;
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=rotation==270?2:0;
        rendered reference,actual,split;
        CHECK(render(original(reference_content,cfg,true),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(first+" "+second,cfg,true),cfg,actual)
              ==EXIT_SUCCESS);
        CHECK(render(original(first,cfg,true,second),cfg,split)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(split,reference) &&
              ink_and_white(actual));
        if(dpi==72 && rotation==0 && quarters==4) {
            CHECK(pixel(actual,9,8)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,11,9)==UINT32_C(0xffff00));
            CHECK(pixel(actual,0,0)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,0,7)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

/* One through four actual, nonempty PATH layers. Each reference paints
 * the analytically known intersection without ANY Clip. The four-layer
 * variant also restores only its final yellow PATH with Q, preserving
 * the unchanged contiguous GS1 -> GS0 membership rule. */
static int ordered_path_layers()
{
    const std::string paints[]={
        full_red,"0 0 1 rg 0 0 8 12 re f ",
        "0 1 0 rg 8 0 8 12 re f* ","1 1 0 rg 0 4 16 8 re f "
    };
    const std::string intersections[]={
        "1 0 0 rg 2 2 m 14 2 l 8 6 l h f ",
        "0 0 1 rg 2 2 m 8 2 l 8 6 l h f ",
        "0 1 0 rg 8 2 m 14 2 l 8 6 l h f* ",
        "1 1 0 rg 5 4 m 11 4 l 8 6 l h f "
    };
    for(int count=1;count<=4;++count)
    for(bool restore:{false,true}) {
        if(restore && count!=4)continue;
        for(int dpi:{72,96,144}) {
            configuration cfg;
            cfg.dpi=dpi;cfg.quarters=4+count%2;
            cfg.rotation=(count%4)*90;
            const int index=dpi==72?0:(dpi==96?1:2);
            cfg.crop=(count+index)%4;cfg.selected=count==4?2:0;
            std::string actual_content=pair_prefix(triangle_a,triangle_b,
                                                   true,false);
            std::string reference_content;
            for(int i=0;i<count;++i) {
                if(restore && i==count-1)actual_content+="Q ";
                actual_content+=paints[i];
                reference_content+=(restore && i==count-1)?
                    paints[i]:intersections[i];
            }
            if(!restore)actual_content+="Q";
            rendered reference,actual;
            CHECK(render(original(reference_content,cfg),cfg,reference)
                  ==EXIT_SUCCESS);
            CHECK(render(original(actual_content,cfg),cfg,actual)==EXIT_SUCCESS);
            CHECK(equal(actual,reference) && ink_and_white(actual));
            if(count==4 && dpi==72) {
                CHECK(pixel(actual,4,9)==UINT32_C(0x0000ff));
                CHECK(pixel(actual,11,9)==UINT32_C(0x00ff00));
                CHECK(pixel(actual,8,7)==UINT32_C(0xffff00));
                CHECK(pixel(actual,0,0)==(restore?
                      UINT32_C(0xffff00):UINT32_C(0xffffff)));
            }
        }
    }
    return EXIT_SUCCESS;
}

/* Authored vertical-first straight rectangles do NOT take the canonical
 * re reduction. Their separate owned leaves meet at device x=7.5, each
 * covering eight different samples of pixel (7,4). Both Image and PATH
 * intersections must be white, not a product of averaged coverages. */
static int same_sample_intersection()
{
    const std::string left="0 0 m 0 12 l 6 12 l 6 0 l h";
    const std::string right="6 0 m 6 12 l 16 12 l 16 0 l h";
    for(int dpi:{72,96,144})
    for(bool with_image:{false,true}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;
        const std::string paint=with_image?image_draw:full_red;
        rendered actual;
        CHECK(render(original(pair_prefix(left,right,false,true)+paint+"Q",
                              cfg,with_image),cfg,actual)==EXIT_SUCCESS);
        CHECK(all_white(actual));
        if(dpi==72) {
            for(const auto& path:{left,right}) {
                rendered single;
                CHECK(render(original(one_prefix(path)+paint+"Q",cfg,
                                      with_image),cfg,single)==EXIT_SUCCESS);
                CHECK(pixel(single,7,4)==UINT32_C(0xff7f7f));
            }
            CHECK(pixel(actual,7,4)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

/* Two three-edge triangles plus a four-edge paint cost ten edge tests
 * per sample. Four compact layers fit: 10*16*40*30=192,000 each. Changing
 * only MediaBox to 64x48 gives 10*16*160*120=3,072,000 per layer, beyond
 * the unchanged 8,000,000/4=2,000,000 allowance. Refuse atomically. */
static int fixed_sample_budget()
{
    const std::string content=pair_prefix(triangle_a,triangle_b)+
        full_red+full_red+full_red+full_red+"Q";
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

/* New contour closure/empty normalization must not relax source scopes,
 * malformed operators, the nine-command ceiling, resource cardinalities
 * or whole-page budgets. A late error clears a nonnull Bitmap sentinel. */
static int atomic_refusals()
{
    const std::string prefix=pair_prefix(triangle_a,triangle_b);
    const std::string ready=prefix+full_red+"Q";
    const std::vector<std::string> bad={
        "q "+triangle_a+" W n "+triangle_b+" W* "+full_red+"Q",
        "q "+triangle_a+" W n W* 1 n "+full_red+"Q",
        "q "+triangle_a+" W n "+triangle_b+" W* W n "+full_red+"Q",
        prefix+"W n "+full_red+"Q",
        prefix+triangle_a+" W n "+full_red+"Q",
        one_prefix(triangle_a)+full_red+triangle_b+" W n "+full_red+"Q",
        one_prefix(triangle_a)+image_draw+triangle_b+" W n Q",
        triangle_a+" W n q "+triangle_b+" W n "+full_red+"Q "+full_red,
        one_prefix(triangle_a)+"Q "+one_prefix(triangle_b)+full_red+"Q",
        one_prefix(triangle_a)+"q "+triangle_b+" W n "+full_red+"Q Q",
        ready+" "+one_prefix(triangle_a)+full_red+"Q",
        prefix+"Q",
        prefix+"Q "+full_red,
        one_prefix(triangle_a)+"4 4 l W n "+full_red+"Q",
        one_prefix(triangle_a)+"4 4 m 8 8 m W n "+full_red+"Q",
        one_prefix(triangle_a)+"h W n "+full_red+"Q",
        one_prefix(triangle_a)+"2 3 m 2 11 10 11 10 3 c W n "+full_red+"Q",
        one_prefix(triangle_a)+"2 3 m 2 11 10 11 10 c 10 2 l W n "+
            full_red+"Q",
        one_prefix(triangle_a)+"2.125 2 m 14 2 l 8 10 l W n "+full_red+"Q",
        one_prefix(triangle_a)+"2 2 m 14 2 l 8 -1000001 l W n "+full_red+"Q",
        /* Nine pre-close commands would require a forbidden tenth. */
        one_prefix(triangle_a)+"2 2 m 8 2 l 12 4 l 14 8 l 10 10 l "
            "4 10 l 2 8 l 1 4 l 1 3 l W n "+full_red+"Q",
        one_prefix(triangle_a)+"2 2 m 14 2 l 8 10 l h h W n "+full_red+"Q",
        "q W n 2 2 0 8 re W* n "+full_red+"Q",
        ready+" BT ET",
        ready+" 2 2 m 10 8 l S",
        ready+" Q",
        prefix+full_red,
        prefix+full_red+full_red+full_red+full_red+full_red+"Q",
        prefix+image_draw+image_draw+image_draw+"Q",
        prefix+image_draw+full_red+image_draw+"Q",
        prefix+full_red+full_red+full_red+image_draw+"Q"
    };
    for(const auto& content:bad)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.crop=dpi==72?0:(dpi==96?2:3);
        cfg.selected=dpi==144?2:0;
        CHECK(rejected(content,cfg,content.find("/I Do")!=std::string::npos)
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
    CHECK(analytic_intersections()==EXIT_SUCCESS);
    CHECK(double_winding_rules()==EXIT_SUCCESS);
    CHECK(empty_leaf_positions()==EXIT_SUCCESS);
    CHECK(implicit_second_closure()==EXIT_SUCCESS);
    CHECK(frozen_ctms_mixed_and_streams()==EXIT_SUCCESS);
    CHECK(ordered_path_layers()==EXIT_SUCCESS);
    CHECK(same_sample_intersection()==EXIT_SUCCESS);
    CHECK(fixed_sample_budget()==EXIT_SUCCESS);
    CHECK(atomic_refusals()==EXIT_SUCCESS);
    std::printf("bounded two-Clip original PDFs: %zu successful renders, "
                "%zu atomic refusals\n",positive_calls,negative_calls);
    return EXIT_SUCCESS;
}

