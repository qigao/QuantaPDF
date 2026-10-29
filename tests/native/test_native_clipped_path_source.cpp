/* Public Source V10 original-PDF clipped PATH-only conformance.
 * Every original below contains opaque f/f* paints and NO Image,
 * XObject or artificial image resource. QPDF authors the original PDF;
 * the public API and the existing pure C11 raster produce every pixel. */
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
    std::fprintf(stderr,"Native clipped PATH Source failed at %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-clipped-path-source.pdf";
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
 * A separate 32x24 original deliberately exceeds that budget below.
 * selected=2 walks past a blank leaf and an unsupported Text leaf in
 * a nested PageTree. No Image /Resources dictionary is ever created. */
static std::string original(const std::string& first,
                            const configuration& cfg,
                            const std::string& second={})
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    const std::string page_dictionary=cfg.large_media?
        "<< /Type /Page /MediaBox [0 0 32 24] >>":
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
        std::fprintf(stderr,"clipped PATH rejected: quarters=%d "
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
    const int full_w=cfg.large_media?128:64;
    const int full_h=cfg.large_media?96:48;
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
                    std::optional<quantapdf_status> expected_status={})
{
    CHECK(write_pdf(original(content,cfg)));
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
        std::fprintf(stderr,"clipped PATH unexpected acceptance: "
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

static const std::string curve_a=
    "2 3 m 2 11 10 11 10 3 c 10 2 l 2 2 l h";
static const std::string curve_b=
    "6 3 m 6 11 14 11 14 3 c 14 2 l 6 2 l h";
static const std::string outer_curve=
    "0 3 m 0 12 14 12 14 3 c 14 1 l 0 1 l h";
static const std::string full_red="1 0 0 rg 0 0 16 12 re f ";
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

/* The reference is an independently authored UNCLIPPED PATH-only PDF
 * painting the known intersection. It exercises the older public path
 * producer, without a Clip graph, Image, export or alternate renderer. */
static int exact_geometry_oracles()
{
    struct example { std::string first,second,intersection; };
    const example cases[]={
        {"4 3 8 4 re","","4 3 8 4 re"},
        {curve_a,"",curve_a},
        {curve_a,curve_a,curve_a},
        {outer_curve,curve_a,curve_a},
        {"0 0 16 12 re",curve_a,curve_a},
        {curve_a,"5 3 3 2 re","5 3 3 2 re"},
        {"2 2 8 7 re","6 4 8 6 re","6 4 4 5 re"}
    };
    for(int shape=0;shape<7;++shape)
    for(int dpi:{72,96,144})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=quarters;
        cfg.rotation=((shape+quarters-4)%4)*90;
        cfg.crop=(shape+quarters-4)%4;
        cfg.selected=shape%2?2:0;
        const auto& item=cases[shape];
        const bool even_a=(shape&1)!=0;
        const bool even_b=quarters==5;
        const std::string fill=shape%2?
            "1 0 0 rg 0 0 16 12 re f* ":full_red;
        const std::string prefix=item.second.empty()?
            one_prefix(item.first,even_a):
            pair_prefix(item.first,item.second,even_a,even_b);
        rendered reference,actual;
        CHECK(render(original("1 0 0 rg "+item.intersection+" f",cfg),
                     cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(prefix+fill+"Q",cfg),cfg,actual)
              ==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
        if(!item.second.empty()) {
            rendered swapped;
            CHECK(render(original(pair_prefix(item.second,item.first,
                                              even_b,even_a)+fill+"Q",cfg),
                         cfg,swapped)==EXIT_SUCCESS);
            CHECK(equal(swapped,reference));
        }
        if(shape==0 && dpi==72 && quarters==4) {
            CHECK(pixel(actual,6,7)==UINT32_C(0xff0000));
            CHECK(pixel(actual,2,7)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

static std::string ordered_fills(const std::string& prefix,
                                const std::vector<int>& order,size_t inside,
                                const std::vector<std::string>& paints)
{
    std::string content=prefix;
    for(size_t i=0u;i<order.size();++i) {
        if(i==inside)content+="Q ";
        content+=paints[static_cast<size_t>(order[i])];
    }
    if(inside==order.size())content+="Q";
    return content;
}
static std::string intersection_fills(
    const std::vector<int>& order,size_t inside,
    const std::vector<std::string>& original_paints,
    const std::vector<std::string>& clipped_paints)
{
    std::string content;
    for(size_t i=0u;i<order.size();++i)
        content+=(i<inside?clipped_paints:original_paints)
            [static_cast<size_t>(order[i])];
    return content;
}

/* One through four ordered fills, both entirely clipped and with a
 * contiguous Q-restored suffix. Independent originals explicitly paint
 * rectangle intersections for GS1 operations and full shapes for GS0.
 * A second, containing cubic leaf must leave those exact pixels intact. */
static int ordered_layers_and_q()
{
    const std::string rectangle="4 3 8 4 re";
    const std::string container=
        "0 2 m 0 12 16 12 16 2 c 16 0 l 0 0 l h";
    const std::vector<std::string> paints={
        "1 0 0 rg 0 0 12 10 re f ",
        "0 0 1 rg 6 2 10 8 re f* ",
        "0 1 0 rg 2 6 12 6 re f ",
        "1 1 0 rg 8 0 8 8 re f* "
    };
    const std::vector<std::string> intersections={
        "1 0 0 rg 4 3 8 4 re f ",
        "0 0 1 rg 6 3 6 4 re f ",
        "0 1 0 rg 4 6 8 1 re f ",
        "1 1 0 rg 8 3 4 4 re f "
    };
    const std::string single=one_prefix(rectangle,true);
    const std::string paired=pair_prefix(container,rectangle,false,true);
    for(int count=1;count<=4;++count)
    for(bool restore:{false,true}) {
        if(count==1 && restore)continue;
        std::vector<int> order;
        for(int i=0;i<count;++i)order.push_back(i);
        const size_t inside=restore?static_cast<size_t>(count/2):
                                    static_cast<size_t>(count);
        for(int dpi:{72,96,144}) {
            configuration cfg;
            cfg.dpi=dpi;cfg.quarters=4+(count%2);
            cfg.rotation=(count%4)*90;
            const int dpi_index=dpi==72?0:(dpi==96?1:2);
            cfg.crop=(dpi_index+count)%4;
            cfg.selected=count==4?2:0;
            rendered reference,one,two;
            CHECK(render(original(intersection_fills(
                                  order,inside,paints,intersections),cfg),
                         cfg,reference)==EXIT_SUCCESS);
            CHECK(render(original(ordered_fills(single,order,inside,paints),
                                  cfg),cfg,one)==EXIT_SUCCESS);
            CHECK(render(original(ordered_fills(paired,order,inside,paints),
                                  cfg),cfg,two)==EXIT_SUCCESS);
            CHECK(equal(one,reference) && equal(two,reference));
            CHECK(ink_and_white(reference));
            if(count==4 && dpi==72) {
                if(restore) {
                    CHECK(pixel(two,14,8)==UINT32_C(0xffff00));
                    CHECK(pixel(two,2,0)==UINT32_C(0x00ff00));
                } else {
                    CHECK(pixel(two,4,8)==UINT32_C(0xff0000));
                    CHECK(pixel(two,7,7)==UINT32_C(0x0000ff));
                    CHECK(pixel(two,5,5)==UINT32_C(0x00ff00));
                    CHECK(pixel(two,9,6)==UINT32_C(0xffff00));
                    CHECK(pixel(two,1,8)==UINT32_C(0xffffff));
                    const std::vector<int> reverse={3,2,1,0};
                    rendered reverse_reference,reverse_one,reverse_two;
                    CHECK(render(original(intersection_fills(
                                          reverse,4u,paints,intersections),
                                          cfg),cfg,reverse_reference)
                          ==EXIT_SUCCESS);
                    CHECK(render(original(ordered_fills(
                                          single,reverse,4u,paints),cfg),
                                 cfg,reverse_one)==EXIT_SUCCESS);
                    CHECK(render(original(ordered_fills(
                                          paired,reverse,4u,paints),cfg),
                                 cfg,reverse_two)==EXIT_SUCCESS);
                    CHECK(equal(reverse_one,reverse_reference) &&
                          equal(reverse_two,reverse_reference) &&
                          !equal(reverse_two,two));
                }
            }
        }
    }
    return EXIT_SUCCESS;
}

/* A nine-command contour traverses the curved region twice. Nonzero
 * winding must match one independently authored traversal; even-odd
 * must be white, whether that rule belongs to W/W* or the f/f* paint.
 * Check either owned leaf and both one-Clip/two-Clip paint profiles. */
static int clip_and_paint_winding()
{
    const std::string twice=
        "2 3 m 2 11 10 11 10 3 c 10 2 l 2 2 l 2 3 l "
        "2 11 10 11 10 3 c 10 2 l 2 2 l h";
    const std::string box="0 0 16 12 re";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;cfg.selected=2;
        cfg.rotation=dpi==72?0:(dpi==96?90:270);
        cfg.crop=dpi==72?0:(dpi==96?2:3);
        rendered nonzero,empty;
        CHECK(render(original("1 0 0 rg "+curve_a+" f",cfg),cfg,nonzero)
              ==EXIT_SUCCESS);
        CHECK(render(original(full_white,cfg),cfg,empty)==EXIT_SUCCESS);
        CHECK(ink_and_white(nonzero) && all_white(empty));
        for(bool even:{false,true}) {
            const rendered& reference=even?empty:nonzero;
            const std::string paint="1 0 0 rg "+twice+
                                     (even?" f* Q":" f Q");
            const std::string cases[]={
                one_prefix(twice,even)+full_red+"Q",
                pair_prefix(twice,box,even,false)+full_red+"Q",
                pair_prefix(box,twice,false,even)+full_red+"Q",
                one_prefix(outer_curve)+paint,
                pair_prefix(box,outer_curve,false,true)+paint
            };
            for(const auto& content:cases) {
                rendered actual;
                CHECK(render(original(content,cfg),cfg,actual)==EXIT_SUCCESS);
                CHECK(equal(actual,reference));
            }
        }
    }
    return EXIT_SUCCESS;
}

/* Define the v leaf under +1 and the y leaf under +2; paint red under
 * +1 and blue inside another +1 q scope, then restore an unmasked green
 * strip. The independent reference spells out c controls and absolute
 * coordinates for both leaves AND fills. Split immediately before n
 * after the second W*: no parser or Clip state can restart there. */
static int frozen_ctms_shorthand_and_streams()
{
    const std::string authored_a=
        "1 3 m 9 11 9 3 v 9 2 l 1 2 l h";
    const std::string authored_b=
        "5 3 m 5 11 13 3 y 13 2 l 5 2 l h";
    const std::string explicit_a=
        "2 3 m 2 3 10 11 10 3 c 10 2 l 2 2 l h";
    const std::string explicit_b=
        "7 3 m 7 11 15 3 15 3 c 15 2 l 7 2 l h";
    const std::string first="q 1 0 0 1 1 0 cm "+authored_a+
        " W n 1 0 0 1 1 0 cm "+authored_b+" W*";
    const std::string restored="Q 0 1 0 rg 0 11 16 1 re f";
    const std::string second="n 1 0 0 1 -1 0 cm "
        "1 0 0 rg 1 1 12 9 re f "
        "q 1 0 0 1 1 0 cm 0 0 1 rg 6 0 2 10 re f* Q "+restored;
    const std::string reference_content=
        pair_prefix(explicit_a,explicit_b,false,true)+
        "1 0 0 rg 2 1 12 9 re f "
        "0 0 1 rg 8 0 2 10 re f* "+restored;
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=rotation==270?2:0;
        rendered reference,actual,split;
        CHECK(render(original(reference_content,cfg),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(first+" "+second,cfg),cfg,actual)
              ==EXIT_SUCCESS);
        CHECK(render(original(first,cfg,second),cfg,split)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(split,reference) &&
              ink_and_white(actual));
        if(dpi==72 && rotation==0 && quarters==4) {
            CHECK(pixel(actual,7,9)==UINT32_C(0xff0000));
            CHECK(pixel(actual,8,9)==UINT32_C(0x0000ff));
            CHECK(pixel(actual,0,0)==UINT32_C(0x00ff00));
            CHECK(pixel(actual,0,9)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

/* Crossing leaves have genuinely different filled regions. The probes
 * are chosen from original curve geometry, including a partially
 * covered curved edge and a point inside the overlapping bounding boxes
 * but outside one leaf. Swapping leaves and rerendering preserve RGB. */
static int crossing_curves()
{
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;
        const std::string content=pair_prefix(curve_a,curve_b,false,true)+
                                  full_red+"Q";
        rendered actual,swapped,again;
        CHECK(render(original(content,cfg),cfg,actual)==EXIT_SUCCESS);
        CHECK(render(original(pair_prefix(curve_b,curve_a,true,false)+
                              full_red+"Q",cfg),cfg,swapped)==EXIT_SUCCESS);
        CHECK(render(original(content,cfg),cfg,again)==EXIT_SUCCESS);
        CHECK(equal(actual,swapped) && equal(actual,again) &&
              ink_and_white(actual));
        if(dpi==72) {
            CHECK(pixel(actual,8,7)==UINT32_C(0xff0000));
            CHECK(pixel(actual,3,7)==UINT32_C(0xffffff));
            CHECK(pixel(actual,12,7)==UINT32_C(0xffffff));
            CHECK(pixel(actual,6,3)==UINT32_C(0xffffff));
            const uint32_t edge=pixel(actual,6,6);
            const unsigned green=(edge>>8u)&255u;
            CHECK((edge>>16u)==255u && green==(edge&255u) &&
                  green>0u && green<255u);
        }
    }
    return EXIT_SUCCESS;
}

/* Empty original W, closed zero-edge cubic, canonical empty first leaf,
 * disjoint two-rectangle reduction and disjoint actual curves all have
 * zero filled area. A real Q subsequently restores an unmasked PATH. */
static int empty_clips_and_restoration()
{
    const std::string zero=
        "0 0 m 0 0 0 0 0 0 c 0 0 l 0 0 l h";
    const std::string left_curve=
        "1 3 m 1 10 5 10 5 3 c 5 2 l 1 2 l h";
    const std::string right_curve=
        "11 3 m 11 10 15 10 15 3 c 15 2 l 11 2 l h";
    const std::string cases[]={
        "q W n "+full_red+"Q",
        one_prefix(zero,true)+full_red+"Q",
        "q W* n "+curve_a+" W n "+full_red+"Q",
        pair_prefix("1 2 4 8 re","11 2 4 8 re",false,true)+full_red+"Q",
        pair_prefix(left_curve,right_curve,true,false)+full_red+"Q"
    };
    const std::string green="0 1 0 rg 4 3 6 4 re f*";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;cfg.rotation=180;
        cfg.crop=dpi==72?0:(dpi==96?2:3);
        cfg.selected=2;
        rendered white;
        CHECK(render(original(full_white,cfg),cfg,white)==EXIT_SUCCESS);
        CHECK(all_white(white));
        for(const auto& content:cases) {
            rendered actual;
            CHECK(render(original(content,cfg),cfg,actual)==EXIT_SUCCESS);
            CHECK(equal(actual,white));
        }
        rendered restored,reference;
        CHECK(render(original("q W n "+curve_a+" W* n "+
                              full_red+"Q "+green,cfg),cfg,restored)
              ==EXIT_SUCCESS);
        CHECK(render(original(green,cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(equal(restored,reference) && ink_and_white(restored));
    }
    return EXIT_SUCCESS;
}

/* Both collinear-cubic leaves individually cover half the samples of
 * device pixel (7,4) at 72 DPI/UserUnit 1.25, but cover opposite halves.
 * Their PATH intersection is white, disproving averaged-mask products. */
static int same_sample_intersection()
{
    const std::string left=
        "0 0 m 6 0 l 6 4 6 8 6 12 c 0 12 l h";
    const std::string right=
        "6 0 m 16 0 l 16 4 16 8 16 12 c 6 12 l h";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;
        rendered actual;
        CHECK(render(original(pair_prefix(left,right,false,true)+
                              full_red+"Q",cfg),cfg,actual)==EXIT_SUCCESS);
        CHECK(all_white(actual));
        if(dpi==72) {
            for(const auto& path:{left,right}) {
                rendered single;
                CHECK(render(original(one_prefix(path)+full_red+"Q",cfg),
                             cfg,single)==EXIT_SUCCESS);
                CHECK(pixel(single,7,4)==UINT32_C(0xff7f7f));
            }
            CHECK(pixel(actual,7,4)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

/* A single original W does not require an artificial q/Q pair; its
 * immutable Clip may stay live at EOF when the actual stack is balanced. */
static int live_single_clip()
{
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=90;cfg.quarters=5;cfg.crop=3;
        rendered reference,actual;
        CHECK(render(original("1 0 0 rg "+curve_a+" f",cfg),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(render(original(curve_a+" W* n "+full_red,cfg),cfg,actual)
              ==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* The same four clipped fills succeed on the compact page. At 2.5x
 * scale each leaf has 16 cubic segments plus three closing edges.
 * The compact layer charges (4+19+19)*16*40*30 = 806,400 tests.
 * Enlarging only MediaBox to 32x24 charges 3,225,600 per layer, above
 * the unchanged 8,000,000/4 allowance of 2,000,000. Preserve the exact
 * UNSUPPORTED/null-Bitmap result rather than enlarging that budget. */
static int fixed_sample_budget()
{
    const std::string content=pair_prefix(curve_a,curve_b)+
        full_red+full_red+full_red+full_red+"Q";
    configuration cfg;
    cfg.dpi=144;cfg.quarters=5;cfg.selected=2;
    rendered compact;
    CHECK(render(original(content,cfg),cfg,compact)==EXIT_SUCCESS);
    CHECK(ink_and_white(compact));
    cfg.large_media=true;
    CHECK(rejected(content,cfg,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

/* State/ownership, paint cardinality, malformed paths, coordinate and
 * source-parser quotas must still reject the complete original, even
 * after earlier valid fills. Every error clears a nonnull Bitmap sentinel. */
static int atomic_refusals()
{
    const std::string prefix=pair_prefix(curve_a,curve_b);
    const std::string ready=prefix+full_red+"Q";
    const std::vector<std::string> bad={
        prefix+full_red+full_red+full_red+full_red+full_red+"Q",
        one_prefix(curve_a)+"Q",
        prefix+"Q",
        one_prefix(curve_a)+"Q "+full_red,
        prefix+"Q "+full_red,
        ready+" BT ET",
        ready+" 2 2 m 10 8 l S",
        /* Root then restored-first painting is admitted, but a fifth
         * PATH still exceeds the unchanged four-layer ceiling. */
        curve_a+" W n q "+curve_b+" W* n "+full_red+"Q "+
            full_red+full_red+full_red+full_red,
        one_prefix(curve_a)+"Q "+one_prefix(curve_b)+full_red+"Q",
        one_prefix(curve_a)+"q "+curve_b+" W n "+full_red+
            "Q "+curve_a+" W n Q", /* third Clip after Q to leaf 1 */
        one_prefix(curve_a)+full_red+curve_b+" W n "+full_red+
            "Q "+one_prefix(curve_a)+full_red+"Q", /* redefinition after Q to 0 */
        full_red+one_prefix(curve_a)+full_red+"Q",
        prefix+curve_a+" W n "+full_red+"Q",
        ready+" "+one_prefix(curve_a)+full_red+"Q",
        one_prefix(curve_a)+"W* 1 n "+full_red+"Q",
        "q W n 4 3 8 4 re W* n W n "+full_red+"Q",
        "q 2 2 m 10 2 l 8 9 l 2 8 l h W n "
            "4 3 8 4 re W* n "+full_red+"Q S",
        "q "+curve_a+" W "+full_red+"Q",
        "q "+curve_a+" W 1 n "+full_red+"Q",
        one_prefix(curve_a)+"6 3 m 6 11 14 11 14 3 c "
            "14 2 l 13 2 l 12 2 l 11 2 l 10 2 l 9 2 l 6 2 l "
            "W n "+full_red+"Q",
        prefix+"1 0 0 rg 2 3 m 2 11 10 11 10 c "
            "10 2 l 2 2 l h f Q",
        prefix+"1 0 0 rg 2 3 m 2.125 11 10 11 10 3 c "
            "10 2 l 2 2 l h f Q",
        prefix+"1 0 0 rg 2 3 m 2 -1000001 10 11 10 3 c "
            "10 2 l 2 2 l h f Q",
        prefix+"1 0 0 rg 2 3 m 2 11 10 11 10 3 c "
            "10 2 l 9 2 l 8 2 l 7 2 l 6 2 l 5 2 l 2 2 l h f Q",
        ready+" Q",
        prefix+full_red,
        ready+" q",
        prefix+full_red+"2 g Q"
    };
    for(const auto& content:bad)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;
        cfg.crop=dpi==72?0:(dpi==96?2:3);
        cfg.selected=dpi==144?2:0;
        CHECK(rejected(content,cfg)==EXIT_SUCCESS);
    }
    std::string over_operators;
    for(int i=0;i<256;++i)over_operators+="1 w ";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;
        CHECK(rejected(over_operators+ready,cfg,
                       QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
        CHECK(rejected(prefix+std::string(65536u,' ')+full_red+"Q",cfg,
                       QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(exact_geometry_oracles()==EXIT_SUCCESS);
    CHECK(ordered_layers_and_q()==EXIT_SUCCESS);
    CHECK(clip_and_paint_winding()==EXIT_SUCCESS);
    CHECK(frozen_ctms_shorthand_and_streams()==EXIT_SUCCESS);
    CHECK(crossing_curves()==EXIT_SUCCESS);
    CHECK(empty_clips_and_restoration()==EXIT_SUCCESS);
    CHECK(same_sample_intersection()==EXIT_SUCCESS);
    CHECK(live_single_clip()==EXIT_SUCCESS);
    CHECK(fixed_sample_budget()==EXIT_SUCCESS);
    CHECK(atomic_refusals()==EXIT_SUCCESS);
    std::printf("clipped PATH-only original PDFs: %zu successful renders, "
                "%zu atomic refusals\n",positive_calls,negative_calls);
    return EXIT_SUCCESS;
}
