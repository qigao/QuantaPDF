/* Original-PDF public Source V10 two-owned curved Clip conformance.
 * QPDF authors source bytes only. Every pixel below comes through the
 * public render API and the existing pure C11 raster, with no PDFium,
 * exported IR, alternate renderer or production admission shortcuts. */
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
    std::fprintf(stderr,"Native two-curved Source failed at %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-two-curved-source.pdf";
static size_t positive_calls=0u,negative_calls=0u;

struct configuration {
    int quarters=4;
    int rotation=0;
    int dpi=72;
    int crop=0;
    int selected=0;
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

/* selected=2 walks a nested original PageTree past both a blank leaf
 * and a Text-only leaf that this native renderer cannot admit. Correct
 * pixels therefore require selecting the actual third source page.
 * Crop mode 2 inherits from the selected leaf's /Pages parent; mode 3
 * has a genuinely fractional origin AND a 16.75-point viewport. */
static std::string original(const std::string& first,
                            const configuration& cfg,bool with_image=true,
                            const std::string& second={})
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    auto parent=pages;
    if(cfg.selected==2) {
        auto blank=pdf->makeIndirectObject(
            Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
        blank.replaceKey("/Parent",pages);
        blank.replaceKey("/Contents",pdf->newStream(""));
        auto text=pdf->makeIndirectObject(
            Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
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
        page.replaceKey("/CropBox",Obj::parse("[8 4 24 20]"));
    else if(cfg.crop==2)
        parent.replaceKey("/CropBox",Obj::parse("[8 4 24 20]"));
    else if(cfg.crop==3)
        page.replaceKey("/CropBox",Obj::parse("[7.75 3.5 24.5 20.25]"));
    if(second.empty())
        page.replaceKey("/Contents",pdf->newStream(first));
    else
        page.replaceKey("/Contents",Obj::newArray({
            pdf->newStream(first),pdf->newStream(second)}));
    if(with_image) {
        /* Four different source texels, not a solid-color surrogate. */
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
        std::fprintf(stderr,"two-curved source rejected: quarters=%d "
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
    const int span_w=cfg.crop==0?128:(cfg.crop==3?67:64);
    const int span_h=cfg.crop==0?96:(cfg.crop==3?67:64);
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
    /* Every positive also exercises Bitmap lifetime after the source
     * file, Page, Document and Context owners have all retired. */
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
        std::fprintf(stderr,"two-curved unexpected acceptance: "
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

static const std::string image_draw="q 32 0 0 24 0 0 cm /I Do Q";
static const std::string curve_a=
    "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h";
static const std::string curve_b=
    "12 5 m 12 18 28 18 28 5 c 28 4 l 12 4 l h";

static std::string pair_prefix(const std::string& a,const std::string& b,
                               bool even_a=false,bool even_b=false)
{
    return "q "+a+(even_a?" W* n ":" W n ")+b+
           (even_b?" W* n ":" W n ");
}
static std::string pair_image(const std::string& a,const std::string& b,
                              bool even_a=false,bool even_b=false)
{
    return pair_prefix(a,b,even_a,even_b)+image_draw+" Q";
}
static std::string single_image(const std::string& path)
{
    return "q "+path+" W n "+image_draw+" Q";
}

/* Independent original one-Clip pages are semantic oracles for strict
 * containment, not outputs derived from the new two-leaf graph. Both
 * authored leaf orders must agree, including distinct identical leaves,
 * rectangle+curve, curve+curve, and a rectangle contained by a curve. */
static int containment_and_identity()
{
    const std::string inner=
        "8 6 m 8 18 20 18 20 6 c 20 4 l 8 4 l h";
    const std::string outer=
        "4 6 m 4 22 24 22 24 6 c 24 2 l 4 2 l h";
    struct example { std::string a,b,intersection; };
    const example cases[]={
        {inner,inner,inner},
        {outer,inner,inner},
        {"4 2 24 20 re",inner,inner},
        {inner,"10 6 8 4 re","10 6 8 4 re"}
    };
    for(int shape=0;shape<4;++shape)
    for(int dpi:{72,96,144})
    for(int rules=0;rules<4;++rules) {
        configuration cfg;
        cfg.dpi=dpi;
        cfg.rotation=90*rules;
        cfg.quarters=4+(rules+shape)%2;
        cfg.crop=(rules+shape)%4;
        cfg.selected=(rules+shape)%2?2:0;
        const auto& item=cases[shape];
        rendered reference;
        CHECK(render(original(single_image(item.intersection),cfg),
                     cfg,reference)==EXIT_SUCCESS);
        CHECK(ink_and_white(reference));
        for(bool swapped:{false,true}) {
            const auto content=swapped?
                pair_image(item.b,item.a,(rules&2)!=0,(rules&1)!=0):
                pair_image(item.a,item.b,(rules&1)!=0,(rules&2)!=0);
            rendered actual;
            CHECK(render(original(content,cfg),cfg,actual)==EXIT_SUCCESS);
            CHECK(equal(actual,reference));
            if(shape==0 && rules==0 && dpi==72) {
                /* Original 2x2 RAW texels survive real clipping and
                 * Image sampling: red, green, blue and yellow. */
                CHECK(pixel(actual,12,10)==UINT32_C(0xff0000));
                CHECK(pixel(actual,17,11)==UINT32_C(0x00ff00));
                CHECK(pixel(actual,12,16)==UINT32_C(0x0000ff));
                CHECK(pixel(actual,17,16)==UINT32_C(0xffff00));
            }
        }
    }
    return EXIT_SUCCESS;
}

/* Neither leaf contains the other. Geometric probes are selected from
 * the authored curves, independently of the renderer: overlap, each
 * leaf alone, a corner inside both bounding boxes but outside a curve,
 * and a partially covered curved-edge pixel. */
static int crossing_curves()
{
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=quarters==5?2:0;
        const bool even_a=rotation==90 || rotation==270;
        const bool even_b=quarters==5;
        const auto content=pair_image(curve_a,curve_b,even_a,even_b);
        rendered actual,swapped,again;
        CHECK(render(original(content,cfg),cfg,actual)==EXIT_SUCCESS);
        CHECK(render(original(pair_image(curve_b,curve_a,even_b,even_a),
                              cfg),cfg,swapped)==EXIT_SUCCESS);
        CHECK(render(original(content,cfg),cfg,again)==EXIT_SUCCESS);
        CHECK(equal(actual,swapped) && equal(actual,again));
        CHECK(ink_and_white(actual));
        if(dpi==72 && rotation==0 && quarters==4) {
            CHECK(pixel(actual,16,16)==UINT32_C(0xffff00));
            CHECK(pixel(actual,8,16)==UINT32_C(0xffffff));
            CHECK(pixel(actual,24,16)==UINT32_C(0xffffff));
            CHECK(pixel(actual,13,10)==UINT32_C(0xffffff));
            const uint32_t edge=pixel(actual,16,10);
            const unsigned red=(edge>>16u)&255u;
            const unsigned green=(edge>>8u)&255u;
            const unsigned blue=edge&255u;
            CHECK(green==255u && red==blue && red>0u && red<255u);
        }
    }
    return EXIT_SUCCESS;
}

/* The nine-command contour traverses one curved region twice in the
 * same direction. W must match a separately authored one-turn Clip;
 * W* must be empty. Test this rule on either owned leaf, not merely
 * equivalent W/W* spelling on simple non-self-intersecting contours. */
static int winding_rules()
{
    const std::string twice=
        "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l 4 5 l "
        "4 18 20 18 20 5 c 20 4 l 4 4 l h";
    const std::string container="0 0 32 24 re";
    for(int dpi:{72,96,144})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=quarters;
        cfg.rotation=quarters==4?0:270;
        cfg.crop=quarters==4?0:3;
        cfg.selected=quarters==5?2:0;
        rendered reference;
        CHECK(render(original(single_image(curve_a),cfg),cfg,reference)
              ==EXIT_SUCCESS);
        CHECK(ink_and_white(reference));
        for(bool swapped:{false,true})
        for(bool even:{false,true}) {
            const auto content=swapped?
                pair_image(container,twice,true,even):
                pair_image(twice,container,even,true);
            rendered actual;
            CHECK(render(original(content,cfg),cfg,actual)==EXIT_SUCCESS);
            if(even)CHECK(all_white(actual));
            else CHECK(equal(actual,reference));
        }
    }
    return EXIT_SUCCESS;
}

/* Each shorthand expands in its own original definition-time state:
 * v supplies the current point as control 1; y repeats its endpoint
 * as control 2. The comparison originals spell both controls out. */
static int shorthand_originals()
{
    const std::string expanded_a=
        "4 5 m 4 5 20 18 20 5 c 20 4 l 4 4 l h";
    const std::string short_a=
        "4 5 m 20 18 20 5 v 20 4 l 4 4 l h";
    const std::string expanded_b=
        "12 5 m 12 18 28 5 28 5 c 28 4 l 12 4 l h";
    const std::string short_b=
        "12 5 m 12 18 28 5 y 28 4 l 12 4 l h";
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=rotation==180?2:0;
        rendered reference,actual;
        CHECK(render(original(pair_image(expanded_a,expanded_b,true,false),
                              cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(pair_image(short_a,short_b,true,false),cfg),
                     cfg,actual)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && ink_and_white(actual));
    }
    return EXIT_SUCCESS;
}

/* The leaves are defined under CTMs translated by +2 and +4, then
 * painted under identity. Reference PDFs use independently translated
 * original coordinates. /Contents is also split exactly between the
 * second W* and n. A real outer Q must restore the later green PATH,
 * and changing Image/PATH source order must change overlapping RGB. */
static int frozen_ctms_and_order()
{
    const std::string explicit_a=
        "6 5 m 6 18 22 18 22 5 c 22 4 l 6 4 l h";
    const std::string before_shift_b=
        "8 5 m 8 18 24 18 24 5 c 24 4 l 8 4 l h";
    const std::string transformed_prefix="q 1 0 0 1 2 0 cm "+
        curve_a+" W n 1 0 0 1 2 0 cm "+before_shift_b+" W*";
    const std::string blue="0 0 1 rg 0 0 18 24 re f ";
    const std::string restored=" Q 0 1 0 rg 0 22 32 2 re f";
    for(int dpi:{72,96,144})
    for(int rotation:{0,90,180,270})
    for(int quarters:{4,5}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.rotation=rotation;cfg.quarters=quarters;
        cfg.crop=(rotation/90+quarters-4)%4;
        cfg.selected=rotation==270?2:0;
        const std::string paints=image_draw+" "+blue+restored;
        const std::string tail="n 1 0 0 1 -4 0 cm "+paints;
        if(dpi==144 && rotation==270 && quarters==5 && cfg.crop==0) {
            /* The fixed 8,000,000 sample-test budget is divided among
             * three layers: 2,666,666 each. At 2.5x scale the full
             * 60x80 canvas and two 35-edge leaves charge each clipped
             * layer (4+35+35)*16*4800 = 5,683,200 tests. Preserve its
             * exact UNSUPPORTED/null-Bitmap result, then qualify the
             * same geometry and source order through the fractional
             * viewport: (4+4+35+35)*16*42*42 = 2,201,472 per layer. */
            CHECK(rejected(pair_prefix(explicit_a,curve_b,false,true)+
                           paints,cfg,QUANTAPDF_ERROR_UNSUPPORTED)
                  ==EXIT_SUCCESS);
            cfg.crop=3;
        }
        rendered reference,actual,split;
        CHECK(render(original(pair_prefix(explicit_a,curve_b,false,true)+
                              paints,cfg),cfg,reference)==EXIT_SUCCESS);
        CHECK(render(original(transformed_prefix+" "+tail,cfg),
                     cfg,actual)==EXIT_SUCCESS);
        CHECK(render(original(transformed_prefix,cfg,true,tail),
                     cfg,split)==EXIT_SUCCESS);
        CHECK(equal(actual,reference) && equal(actual,split));
        CHECK(ink_and_white(actual));
        if(rotation==0 && quarters==4) {
            rendered reversed,reverse_reference;
            const std::string reverse_paints=blue+image_draw+restored;
            CHECK(render(original(pair_prefix(explicit_a,curve_b,false,true)+
                                  reverse_paints,cfg),cfg,reverse_reference)
                  ==EXIT_SUCCESS);
            CHECK(render(original(transformed_prefix+
                                  " n 1 0 0 1 -4 0 cm "+reverse_paints,cfg),
                         cfg,reversed)==EXIT_SUCCESS);
            CHECK(equal(reversed,reverse_reference));
            CHECK(!equal(actual,reversed));
            if(dpi==72) {
                CHECK(pixel(actual,16,16)==UINT32_C(0x0000ff));
                CHECK(pixel(reversed,16,16)==UINT32_C(0xffff00));
                CHECK(pixel(actual,1,0)==UINT32_C(0x00ff00));
                CHECK(pixel(reversed,1,0)==UINT32_C(0x00ff00));
            }
        }
    }
    return EXIT_SUCCESS;
}

/* Touching collinear-cubic rectangles meet at original x=10, which is
 * device x=12.5 at 72 DPI and UserUnit=1.25. Each separate Clip covers
 * half of a pixel's samples, but their geometric intersection covers
 * none. Multiplying averaged masks would incorrectly leave ink. */
static int empty_and_same_sample_intersection()
{
    const std::string left=
        "0 0 m 10 0 l 10 8 10 16 10 24 c 0 24 l h";
    const std::string right=
        "10 0 m 32 0 l 32 8 32 16 32 24 c 10 24 l h";
    const std::string fill="0 1 0 rg 8 6 8 6 re f";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;
        rendered both,empty_first,restored,fill_only;
        CHECK(render(original(pair_image(left,right,false,true),cfg),
                     cfg,both)==EXIT_SUCCESS);
        CHECK(all_white(both));
        CHECK(render(original("q W n "+curve_a+" W* n "+
                              image_draw+" Q",cfg),cfg,empty_first)
              ==EXIT_SUCCESS);
        CHECK(all_white(empty_first));
        CHECK(render(original("q W* n "+curve_a+" W n "+
                              image_draw+" Q "+fill,cfg),cfg,restored)
              ==EXIT_SUCCESS);
        CHECK(render(original(fill,cfg,false),cfg,fill_only)==EXIT_SUCCESS);
        CHECK(equal(restored,fill_only) && ink_and_white(restored));
        if(dpi==72) {
            for(const auto& path:{left,right}) {
                rendered single;
                CHECK(render(original(single_image(path),cfg),cfg,single)
                      ==EXIT_SUCCESS);
                CHECK(pixel(single,12,4)==UINT32_C(0xff7f7f));
            }
            CHECK(pixel(both,12,4)==UINT32_C(0xffffff));
        }
    }
    return EXIT_SUCCESS;
}

/* Two clipped Images keep the existing image-only cardinality. Their
 * source order is exercised by two different original placements of
 * the patterned RAW XObject; an independently authored one-Clip PDF
 * supplies exact containment pixels. */
static int two_image_profile()
{
    const std::string outer="0 0 32 24 re";
    const std::string second_image="q 16 0 0 12 8 4 cm /I Do Q";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;cfg.quarters=5;cfg.rotation=180;cfg.crop=2;
        cfg.selected=2;
        for(bool reverse:{false,true}) {
            const std::string paints=reverse?
                second_image+" "+image_draw:image_draw+" "+second_image;
            rendered actual,reference;
            CHECK(render(original(pair_prefix(curve_a,outer,true,false)+
                                  paints+" Q",cfg),cfg,actual)==EXIT_SUCCESS);
            CHECK(render(original("q "+curve_a+" W n "+paints+" Q",cfg),
                         cfg,reference)==EXIT_SUCCESS);
            CHECK(equal(actual,reference) && ink_and_white(actual));
        }
    }
    return EXIT_SUCCESS;
}

/* Refusals include source state/ownership, malformed geometry, the
 * original command/paint/operator caps, and late errors after paints
 * have already been staged. Each must clear a nonnull output sentinel. */
static int atomic_refusals()
{
    const std::string ready=pair_image(curve_a,curve_b);
    const std::string prefix=pair_prefix(curve_a,curve_b);
    const std::string path_fill="0 0 1 rg 4 4 8 6 re f ";
    const std::vector<std::string> bad={
        "q "+curve_a+" W n "+curve_b+" W* "+image_draw+" Q",
        "q "+curve_a+" W n "+curve_b+" W* W n "+image_draw+" Q",
        prefix+curve_a+" W n "+image_draw+" Q",
        "q "+curve_a+" W n "+image_draw+" "+curve_b+" W n Q",
        /* Painting before the second W now qualifies; three PATHs
         * plus one Image still exceed the retained mixed PATH limit. */
        "q "+curve_a+" W n "+path_fill+curve_b+" W n "+image_draw+" "+
            path_fill+path_fill+"Q",
        /* Two Images in states 2,1 qualify; the third remains outside
         * the Image-only limit even after restoring the first Clip. */
        curve_a+" W n q "+curve_b+" W n "+image_draw+" Q "+
            image_draw+" "+image_draw,
        "q "+curve_a+" W n Q q "+curve_b+" W n "+image_draw+" Q",
        "q "+curve_a+" W n q "+curve_b+" W n "+image_draw+
            " Q "+curve_a+" W n Q", /* third definition after Q to leaf 1 */
        /* Empty second leaves and empty+rectangle now qualify; malformed
         * n operands and zero-width original re still fail atomically. */
        "q "+curve_a+" W n W* 1 n "+image_draw+" Q",
        "q W n 8 4 0 12 re W* n "+image_draw+" Q",
        /* Straight pairs now qualify, but l cannot follow a closed h. */
        "q 4 4 m 20 4 l 16 16 l 4 12 l h 8 8 l W n "
            "8 4 12 12 re W* n "+image_draw+" Q",
        /* Implicit closing does not enlarge the nine-command ceiling. */
        "q "+curve_a+" W n 12 5 m 12 18 28 18 28 5 c "
            "28 4 l 26 4 l 24 4 l 22 4 l 20 4 l 18 4 l 12 4 l "
            "W n "+image_draw+" Q",
        "q "+curve_a+" W n 12 5 m 12 18 28 18 28 c "
            "28 4 l 12 4 l h W n "+image_draw+" Q",
        "q "+curve_a+" W n 12 5 m 12.125 18 28 18 28 5 c "
            "28 4 l 12 4 l h W n "+image_draw+" Q",
        "q "+curve_a+" W n 12 5 m 12 -1000001 28 18 28 5 c "
            "28 4 l 12 4 l h W n "+image_draw+" Q",
        /* Ten commands would exceed the existing nine-command cap. */
        "q "+curve_a+" W n 12 5 m 12 18 28 18 28 5 c "
            "28 4 l 26 4 l 24 4 l 22 4 l 20 4 l 18 4 l 12 4 l h "
            "W n "+image_draw+" Q",
        ready+" BT ET",
        ready+" 4 4 m 20 18 20 18 20 4 c S",
        prefix+"q 32 0 0 24 0 0 cm /Missing Do Q Q",
        ready+" Q",
        ready+" q",
        prefix+image_draw+" "+image_draw+" "+image_draw+" Q",
        /* Three all-clipped paints remain outside the mixed profile. */
        prefix+image_draw+" "+path_fill+image_draw+" Q",
        /* Widened PATH-only staging must not admit a late first Image
         * after three PATHs through the unchanged mixed preflight. */
        prefix+path_fill+path_fill+path_fill+image_draw+" Q",
        prefix+image_draw+" "+path_fill+" Q "+image_draw+" "+
            path_fill+path_fill,
        /* PATH-only under the final Clip is now qualified separately;
         * a fifth opaque PATH still exceeds the fixed layer ceiling. */
        prefix+path_fill+path_fill+path_fill+path_fill+path_fill+" Q",
        ready+" q "+curve_a+" W n "+image_draw+" Q",
        prefix+curve_a+" W 1 n "+image_draw+" Q"
    };
    for(const auto& content:bad)
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;
        cfg.crop=dpi==72?0:(dpi==96?2:3);
        cfg.selected=dpi==144?2:0;
        CHECK(rejected(content,cfg)==EXIT_SUCCESS);
    }
    /* Harmless original stroke-state settings still spend the same
     * finite parser operand/operator quota before either Clip exists. */
    std::string over_operators;
    for(int i=0;i<256;++i)over_operators+="1 w ";
    for(int dpi:{72,96,144}) {
        configuration cfg;
        cfg.dpi=dpi;
        CHECK(rejected(over_operators+ready,cfg)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(containment_and_identity()==EXIT_SUCCESS);
    CHECK(crossing_curves()==EXIT_SUCCESS);
    CHECK(winding_rules()==EXIT_SUCCESS);
    CHECK(shorthand_originals()==EXIT_SUCCESS);
    CHECK(frozen_ctms_and_order()==EXIT_SUCCESS);
    CHECK(empty_and_same_sample_intersection()==EXIT_SUCCESS);
    CHECK(two_image_profile()==EXIT_SUCCESS);
    CHECK(atomic_refusals()==EXIT_SUCCESS);
    std::printf("two-curved original PDFs: %zu successful renders, "
                "%zu atomic refusals\n",positive_calls,negative_calls);
    return EXIT_SUCCESS;
}
