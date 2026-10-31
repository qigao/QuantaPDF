/* Native Source V10 original curved c/v/y W/W* Clip (issue #522).
 * QPDF only authors original source; the existing pure C11 V5 raster
 * produces ALL pixels. No PDFium, fallback, export or second renderer. */
#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"Native curved Clip failed at %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-original-curved-clip.pdf";

static bool write_pdf(const std::string& bytes)
{
    FILE *f=nullptr;
#ifdef _WIN32
    if(fopen_s(&f,filename,"wb")!=0)return false;
#else
    f=std::fopen(filename,"wb");
#endif
    if(f==nullptr)return false;
    const bool complete=std::fwrite(bytes.data(),1u,bytes.size(),f)
                         ==bytes.size();
    return std::fclose(f)==0 && complete;
}

static std::string original(const std::string& first,
                            bool with_image,int quarters,int rotation,
                            int crop_mode=0,
                            const std::string& second={})
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/UserUnit",Obj::parse(
        std::to_string((double)quarters/4.0)));
    page.replaceKey("/Rotate",Obj::newInteger(rotation));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    if(crop_mode==1)
        page.replaceKey("/CropBox",Obj::parse("[8 4 24 20]"));
    else if(crop_mode==2)
        pages.replaceKey("/CropBox",Obj::parse("[8 4 24 20]"));
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

static int render(const std::string& input,int quarters,int rotation,
                  int dpi,int crop_mode,std::vector<unsigned char>& rgb)
{
    CHECK(write_pdf(input));
    quantapdf_context *context=nullptr;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(context,filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);
    OK(quantapdf_load_page(doc,0,&page));
    quantapdf_render_options options{};
    options.struct_size=sizeof(options);
    options.dpi=static_cast<float>(dpi);
    const auto status=quantapdf_render_page_with_options(
        page,&options,&bitmap);
    if(status!=QUANTAPDF_OK)
        std::fprintf(stderr,"curved W/W* rejected quarters=%d rotate=%d "
            "dpi=%d crop=%d status=%d\n",quarters,rotation,dpi,crop_mode,
            static_cast<int>(status));
    CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
    int width=0,height=0,stride=0,channels=0;
    const unsigned char *pixels=nullptr;
    size_t count=0u;
    OK(quantapdf_bitmap_dimensions(bitmap,
         &width,&height,&stride,&channels));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&count));
    const int span_w=crop_mode==0?32:16;
    const int span_h=crop_mode==0?24:16;
    const int src_w=rotation==90?span_h:span_w;
    const int src_h=rotation==90?span_w:span_h;
    const int expected_w=(src_w*quarters*dpi+287)/288;
    const int expected_h=(src_h*quarters*dpi+287)/288;
    CHECK(width==expected_w && height==expected_h &&
          stride==width*3 && channels==3 && pixels!=nullptr &&
          count==static_cast<size_t>(stride)*static_cast<size_t>(height));
    rgb.assign(pixels,pixels+count);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    quantapdf_drop_context(context);
    /* Source, Page, Document and Context have all retired. */
    const unsigned char *retained=nullptr;
    size_t retained_count=0u;
    OK(quantapdf_bitmap_data(bitmap,&retained,&retained_count));
    CHECK(retained!=nullptr && retained_count==rgb.size() &&
          std::memcmp(rgb.data(),retained,retained_count)==0);
    quantapdf_drop_bitmap(bitmap);
    return EXIT_SUCCESS;
}

static int rejected(const std::string& contents,
                    bool with_image,int dpi,int crop_mode=0)
{
    const auto input=original(contents,with_image,4,0,crop_mode);
    CHECK(write_pdf(input));
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=
        reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    OK(quantapdf_open(filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);
    OK(quantapdf_load_page(doc,0,&page));
    quantapdf_render_options options{};
    options.struct_size=sizeof(options);
    options.dpi=static_cast<float>(dpi);
    const auto status=quantapdf_render_page_with_options(
        page,&options,&bitmap);
    if(status==QUANTAPDF_OK || bitmap!=nullptr)
        std::fprintf(stderr,
            "unexpected curved-Clip negative: status=%d bitmap=%p "
            "dpi=%d crop=%d content=%s\n",
            static_cast<int>(status),static_cast<void*>(bitmap),
            dpi,crop_mode,contents.c_str());
    CHECK(status!=QUANTAPDF_OK && bitmap==nullptr);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return EXIT_SUCCESS;
}

static std::string clip_image(const std::string& path,
                              const char *rule)
{
    return "q "+path+" "+rule+
        " n q 16 0 0 12 8 3 cm /I Do Q Q";
}
static bool is_white(const std::vector<unsigned char>& rgb,size_t at)
{
    return rgb[at]==255u && rgb[at+1u]==255u && rgb[at+2u]==255u;
}

/* Single non-self-intersecting cubic contours: standard cubic versus
 * equivalent v/y shorthand, nonzero W versus even-odd W*, and the
 * original Media and inherited/local smaller Crop viewport. */
static int equivalent_original_clips()
{
    struct shape {
        const char *expanded;
        const char *abbreviated;
    };
    const shape contour[]={
        {"4 5 m 4 5 20 18 20 5 c 20 4 l 4 4 l h",
         "4 5 m 20 18 20 5 v 20 4 l 4 4 l h"},
        {"4 5 m 4 18 20 5 20 5 c 20 4 l 4 4 l h",
         "4 5 m 4 18 20 5 y 20 4 l 4 4 l h"},
        {"4 5 m 4 14 8 16 12 9 c 12 9 20 16 20 5 c "
         "20 4 l 4 4 l h",
         "4 5 m 4 14 8 16 12 9 c 20 16 20 5 v "
         "20 4 l 4 4 l h"},
        {"4 5 m 4 14 8 16 12 9 c 15 16 20 5 20 5 c "
         "20 4 l 4 4 l h",
         "4 5 m 4 14 8 16 12 9 c 15 16 20 5 y "
         "20 4 l 4 4 l h"}
    };
    for(size_t type=0u;type<4u;++type)
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop_mode=0;crop_mode<=2;++crop_mode) {
        std::vector<unsigned char> reference;
        for(int variant=0;variant<4;++variant) {
            const char *const path=(variant<2)?
                contour[type].expanded:contour[type].abbreviated;
            const auto input=original(
                clip_image(path,variant%2?"W*":"W"),
                true,quarters,rotation,crop_mode);
            std::vector<unsigned char> got;
            CHECK(render(input,quarters,rotation,dpi,crop_mode,got)
                  ==EXIT_SUCCESS);
            if(variant==0)reference=got;
            else CHECK(got.size()==reference.size() &&
                 std::memcmp(got.data(),reference.data(),got.size())==0);
            bool ink=false,white=false;
            for(size_t i=0u;i<got.size();i+=3u) {
                ink|=!is_white(got,i);
                white|=is_white(got,i);
            }
            CHECK(ink && white);
            /* Verify geometry, not merely that some image was drawn:
             * both probes lie within the UNCLIPPED source Image. */
            if(type==0u && quarters==4 && rotation==0 &&
               dpi==72 && crop_mode==0) {
                CHECK(!is_white(got,(16u*32u+12u)*3u));
                CHECK(is_white(got,(10u*32u+19u)*3u));
            }
        }
    }
    return EXIT_SUCCESS;
}

/* A real Q restores the prior unmasked PATH state. Gray Fill outside
 * the frozen clip must remain visible even though the preceding Image
 * was clipped. W/W* must agree for this one simple contour. */
static int q_restoration()
{
    const std::string path=
        "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h";
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144}) {
        std::vector<unsigned char> reference;
        for(bool even:{false,true}) {
            const auto input=original(
                clip_image(path,even?"W*":"W")+
                " 0.875 g 24 17 5 3 re f",
                true,quarters,rotation,0);
            std::vector<unsigned char> got;
            CHECK(render(input,quarters,rotation,dpi,0,got)==EXIT_SUCCESS);
            if(!even)reference=got;
            else CHECK(reference.size()==got.size() &&
                  std::memcmp(reference.data(),got.data(),got.size())==0);
            if(quarters==4 && rotation==0 && dpi==72) {
                /* This location is outside the image and curved Clip. */
                CHECK(!is_white(got,(5u*32u+26u)*3u));
            }
        }
    }
    return EXIT_SUCCESS;
}

/* Clip path definition CTM is frozen at W n. Inverse cm restores the
 * image placement transform, not the frozen Clip. Compare to the same
 * original path with all its authored x coordinates shifted by +2. */
static int definition_time_ctm()
{
    const std::string moved=
        "q 6 5 m 6 18 22 18 22 5 c 22 4 l 6 4 l h ";
    const std::string original_path=
        "q 1 0 0 1 2 0 cm "
        "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h ";
    const std::string draw="q 16 0 0 12 8 3 cm /I Do Q Q";
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(bool even:{false,true}) {
        const std::string w=even?"W* n ":"W n ";
        const auto a=original(moved+w+draw,true,quarters,rotation);
        const auto b=original(original_path+w+
            "1 0 0 1 -2 0 cm "+draw,true,quarters,rotation);
        std::vector<unsigned char> reference,got;
        CHECK(render(a,quarters,rotation,dpi,0,reference)==EXIT_SUCCESS);
        CHECK(render(b,quarters,rotation,dpi,0,got)==EXIT_SUCCESS);
        CHECK(reference.size()==got.size() &&
              std::memcmp(reference.data(),got.data(),got.size())==0);
    }
    return EXIT_SUCCESS;
}

/* A source content stream boundary does not retire q, pending W, or
 * the already frozen Clip. Keep one reader/owner and one object budget
 * for the whole original Page, never independently paint each stream. */
static int split_original_contents()
{
    const std::string path=
        "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h";
    const std::string draw="n q 16 0 0 12 8 3 cm /I Do Q Q";
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(bool even:{false,true}) {
        const auto prefix="q "+path+(even?" W*":" W");
        const auto single=original(prefix+" "+draw,true,quarters,rotation,1);
        const auto split=original(prefix,true,quarters,rotation,1,draw);
        std::vector<unsigned char> reference,got;
        CHECK(render(single,quarters,rotation,dpi,1,reference)==EXIT_SUCCESS);
        CHECK(render(split,quarters,rotation,dpi,1,got)==EXIT_SUCCESS);
        CHECK(reference.size()==got.size() &&
              std::memcmp(reference.data(),got.data(),got.size())==0);
    }
    return EXIT_SUCCESS;
}

/* The strict full-Media profile excludes authored negative controls,
 * but a truly reduced original CropBox may contain a curve whose
 * control extends outside BOTH Crop and Media. Those source points
 * are bounded but must not be artificially clamped or rejected:
 * the single C11 raster clips visible samples after geometry staging.
 * Compare local/inherited CropBox and W/W* byte-for-byte, while
 * preserving full-Media fail-closed behavior for the same original. */
static int bounded_control_outside_crop()
{
    const std::string path=
        "4 5 m -1 18 20 18 20 5 c 20 4 l 4 4 l h";
    const std::string draw=
        "n q 16 0 0 12 8 3 cm /I Do Q Q";
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144}) {
        std::vector<unsigned char> reference;
        for(int crop_mode:{1,2})
        for(bool even:{false,true}) {
            const auto content="q "+path+
                (even?" W* ":" W ")+draw;
            const auto bytes=original(content,true,quarters,
                                      rotation,crop_mode);
            std::vector<unsigned char> got;
            CHECK(render(bytes,quarters,rotation,dpi,crop_mode,got)
                  ==EXIT_SUCCESS);
            bool ink=false,white=false;
            for(size_t i=0u;i<got.size();i+=3u) {
                ink|=!is_white(got,i);
                white|=is_white(got,i);
            }
            CHECK(ink && white);
            if(reference.empty())reference=got;
            else CHECK(reference.size()==got.size() &&
                 std::memcmp(reference.data(),got.data(),got.size())==0);
        }
    }
    /* With no effective CropBox the older full-Media original-
     * geometry contract must still reject the negative x control
     * atomically, regardless of W versus W* and actual DPI. */
    for(bool even:{false,true})
    for(int dpi:{72,96,144}) {
        const auto content="q "+path+(even?" W* ":" W ")+draw;
        CHECK(rejected(content,true,dpi,0)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

/* Unsupported and malformed Source MUST refuse the ENTIRE page,
 * even if a clipped Image has already been staged, never return
 * partially painted RGB. Both W and W* are checked at real DPI. */
static int atomic_rejections()
{
    const std::string path=
        "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h";
    const std::string ready="q "+path+
        " W n q 16 0 0 12 8 3 cm /I Do Q Q";
    const std::vector<std::string> bad={
        "q "+path+" W q 16 0 0 12 8 3 cm /I Do Q Q",
        /* Two identical owned curved leaves are now qualified; a third
         * definition must still reject the complete original page. */
        "q "+path+" W n "+path+" W* n "+path+" W n "
           "q 16 0 0 12 8 3 cm /I Do Q Q",
        "q "+path+" W* n q 16 0 0 12 8 3 cm /Missing Do Q Q",
        ready+" S",
        ready+" BT ET",
        ready+" 2 G",
        ready+" 1 0 0 rg 4 4 8 6 re f 1 0 0 rg 4 4 8 6 re S",
        ready+" 4 4 8 6 re W n",
        ready+" q 4 4 8 6 re W n Q",
        ready+" Q",
        ready+" q",
        "q 4 5 m 4.125 18 20 18 20 5 c 20 4 l 4 4 l h W n "
          "q 16 0 0 12 8 3 cm /I Do Q Q",
        /* Cropped source legitimately admits geometry outside the CropBox;
         * the actual strict SOURCE +/-1,000,000 coordinate cap must fail
         * for both full-Media and reduced-CropBox original pages. */
        "q 4 5 m 4 -1000001 20 18 20 5 c 20 4 l 4 4 l h W* n "
          "q 16 0 0 12 8 3 cm /I Do Q Q",
        "q 4 5 m 4 18 20 18 20 c 20 4 l 4 4 l h W n "
          "q 16 0 0 12 8 3 cm /I Do Q Q",
        "q 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h W 1 n "
          "q 16 0 0 12 8 3 cm /I Do Q Q",
        "q "+path+" W* n q 16 0 0 12 8 3 cm /I Do Q "
            "Q 4 5 m 4 18 20 18 20 5 c S",
        "q "+path+" W n q 16 0 0 12 8 3 cm /I Do Q "
            "Q 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h W n",
        "q "+path+" W* n q 16 0 0 12 8 3 cm /I Do Q "
            "Q 1 0 0 rg 4 4 8 6 re f BT ET"
    };
    for(const auto& content:bad)
    for(int dpi:{72,96,144})
    for(int crop_mode:{0,1}) {
        const bool image=
            content.find("/I Do")!=std::string::npos ||
            content.find("/Missing Do")!=std::string::npos;
        CHECK(rejected(content,image,dpi,crop_mode)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}


/* Original empty W/W*: zero filled area under both winding rules.
 * The C11 Clip consumer's zero-edge region must suppress every Image
 * sample. The real original stream is written by QPDF, not IR export. */
static int empty_clip_pixels()
{
    const std::string empty_shapes[]={
        "", "4 5 m", "4 5 m 20 15 l",
        "4 5 m 4 5 l 4 5 l",
        "4 5 m 4 5 4 5 4 5 c 4 5 l"
    };
    const std::string draw="q 16 0 0 12 8 3 cm /I Do Q Q";
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop:{0,1,2}) {
        std::vector<unsigned char> reference;
        for(const auto& shape:empty_shapes)
        for(bool even:{false,true}) {
            const auto pdf=original(
                "q "+shape+(even?" W* n ":" W n ")+draw,
                true,quarters,rotation,crop);
            std::vector<unsigned char> got;
            CHECK(render(pdf,quarters,rotation,dpi,crop,got)==EXIT_SUCCESS);
            CHECK(!got.empty() && got.size()%3u==0u);
            for(size_t i=0u;i<got.size();i+=3u)
                CHECK(is_white(got,i));
            if(reference.empty())reference=got;
            else CHECK(reference.size()==got.size() &&
                std::memcmp(reference.data(),got.data(),got.size())==0);
        }
    }
    return EXIT_SUCCESS;
}

/* A real nonempty Clip may also have an empty INTERSECTION with the
 * Image/visible CropBox. An unrelated unpainted n with NO W must not
 * suppress Image ink. Byte-level baseline and all-white checks. */
static int disjoint_clip_and_unclipped_image()
{
    const std::string draw="q 16 0 0 12 8 3 cm /I Do Q";
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop:{0,1,2}) {
        std::vector<unsigned char> expected,got;
        CHECK(render(original(draw,true,quarters,rotation,crop),
                     quarters,rotation,dpi,crop,expected)==EXIT_SUCCESS);
        CHECK(render(original("q 4 5 m n "+draw+" Q",
                              true,quarters,rotation,crop),
                     quarters,rotation,dpi,crop,got)==EXIT_SUCCESS);
        CHECK(expected.size()==got.size() &&
              std::memcmp(expected.data(),got.data(),got.size())==0);
        bool ink=false,white=false;
        for(size_t i=0u;i<got.size();i+=3u) {
            ink|=!is_white(got,i);
            white|=is_white(got,i);
        }
        CHECK(ink && white);
        for(bool even:{false,true}) {
            const auto pdf=original(
                "q 25 18 3 3 re "+std::string(even?"W*":"W")+
                " n "+draw+" Q",true,quarters,rotation,crop);
            CHECK(render(pdf,quarters,rotation,dpi,crop,got)==EXIT_SUCCESS);
            for(size_t i=0u;i<got.size();i+=3u)
                CHECK(is_white(got,i));
        }
    }
    return EXIT_SUCCESS;
}

/* Real Q restores unmasked painting after an Image suppressed by
 * either an original empty W or non-overlapping nonempty W.
 * The exact RGB must equal the independently authored PATH-only PDF. */
static int empty_or_disjoint_q_restoration()
{
    const std::string draw="q 16 0 0 12 8 3 cm /I Do Q Q";
    const std::string fill="1 0 0 rg 8 6 8 6 re f";
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop:{0,1,2}) {
        std::vector<unsigned char> expected;
        CHECK(render(original(fill,false,quarters,rotation,crop),
                     quarters,rotation,dpi,crop,expected)==EXIT_SUCCESS);
        bool ink=false,white=false;
        for(size_t i=0u;i<expected.size();i+=3u) {
            ink|=!is_white(expected,i);
            white|=is_white(expected,i);
        }
        CHECK(ink && white);
        for(const std::string path:{"","25 18 3 3 re"})
        for(bool even:{false,true}) {
            const auto pdf=original(
                "q "+path+(even?" W* n ":" W n ")+
                draw+" "+fill,true,quarters,rotation,crop);
            std::vector<unsigned char> got;
            CHECK(render(pdf,quarters,rotation,dpi,crop,got)==EXIT_SUCCESS);
            CHECK(expected.size()==got.size() &&
                  std::memcmp(expected.data(),got.data(),got.size())==0);
        }
    }
    return EXIT_SUCCESS;
}

/* Malformed or unsupported content, even AFTER a correctly suppressed
 * Image, must not publish partial RGB. Clip redefinition after Q restores
 * no Clip, and W without n, remain unsupported without a fallback. */
static int empty_clip_atomic_refusals()
{
    const std::string draw="q 16 0 0 12 8 3 cm /I Do Q Q";
    const std::vector<std::string> bad={
        "q W "+draw,
        "q W* q 16 0 0 12 8 3 cm /I Do Q Q",
        "q W 4 5 m n "+draw,
        "q W 1 n "+draw,
        "q W n q 16 0 0 12 8 3 cm /I Do Q",
        "q W n "+draw+" Q",
        "q W n q 16 0 0 12 8 3 cm /Missing Do Q Q",
        "q W n "+draw+" BT ET",
        "q W* n "+draw+" 4 4 m S",
        "q W n "+draw+" q 4 4 8 6 re W* n Q",
        "q W n "+draw+" 1 0 0 rg 8 6 8 6 re f BT ET",
        "q 4 5 m 4 18 20 18 20 5 c W n "+draw,
        "q W n "+draw+" 1 0 0 rg 8 6 8 6 re f "
            "q 4 4 8 6 re W n Q"
    };
    for(const auto& content:bad)
    for(int crop:{0,1})
    for(int dpi:{72,96,144})
        CHECK(rejected(content,true,dpi,crop)==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}


/* Two authored rectangular W/W* definitions in one original q scope,
 * before ANY paint, have the same exact 4x4 sample membership as
 * their rectangle intersection. Only that strictly qualified subset
 * is collapsed by Source into ONE owned Clip for the existing C11
 * renderer. The reference is a separate real PDF, not a second
 * renderer or a Composer IR re-export. */
static int two_rectangle_clip_intersections()
{
    struct pair {
        const char *first;
        const char *second;
        const char *intersection;
        const char *before_second;
        const char *after_second;
        bool empty;
    };
    const pair shapes[]={
        {"8 4 12 12 re","12 6 12 12 re",
         "12 6 8 10 re","","",false},
        {"12 6 12 12 re","8 4 12 12 re",
         "12 6 8 10 re","","",false},
        {"4 4 24 16 re","12 6 8 10 re",
         "12 6 8 10 re","","",false},
        {"20 16 -12 -12 re","24 18 -12 -12 re",
         "12 6 8 10 re","","",false},
        {"8 4 12 12 re","8 6 12 12 re",
         "12 6 8 10 re",
         "1 0 0 1 4 0 cm ","1 0 0 1 -4 0 cm ",false},
        {"4 4 8 6 re","18 14 8 6 re",
         "","","",true},
        {"4 4 8 8 re","12 6 8 10 re",
         "","","",true},
        {"8 4 12 8 re","12 12 8 8 re",
         "","","",true}
    };
    const std::string image="q 16 0 0 12 8 3 cm /I Do Q Q";
    for(const auto& shape:shapes)
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop:{0,1,2}) {
        const auto expected=original(
            std::string("q ")+shape.intersection+
            (shape.empty?"W n ":" W n ")+image,
            true,quarters,rotation,crop);
        std::vector<unsigned char> reference;
        CHECK(render(expected,quarters,rotation,dpi,crop,reference)
              ==EXIT_SUCCESS);
        bool has_ink=false,white=false;
        for(size_t p=0u;p<reference.size();p+=3u) {
            has_ink|=!is_white(reference,p);
            white|=is_white(reference,p);
        }
        CHECK(white);
        if(shape.empty)CHECK(!has_ink);
        else CHECK(has_ink);
        for(bool first_even:{false,true})
        for(bool second_even:{false,true}) {
            const std::string input=
                std::string("q ")+shape.first+
                (first_even?" W* n ":" W n ")+
                shape.before_second+shape.second+
                (second_even?" W* n ":" W n ")+
                shape.after_second+image;
            std::vector<unsigned char> got;
            CHECK(render(original(input,true,quarters,rotation,crop),
                         quarters,rotation,dpi,crop,got)==EXIT_SUCCESS);
            CHECK(reference.size()==got.size() &&
                  std::memcmp(reference.data(),got.data(),
                              got.size())==0);
            if(!shape.empty && quarters==4 && rotation==0 &&
               dpi==72 && crop==0) {
                CHECK(!is_white(got,(14u*32u+14u)*3u));
                CHECK(is_white(got,(14u*32u+10u)*3u));
            }
        }
    }
    return EXIT_SUCCESS;
}

/* A disjoint or merely touching 2-W intersection cannot leak image
 * pixels, but a REAL Q must restore the outer unmasked graphics
 * state. Independent original PATH-only bytes are the RGB oracle. */
static int two_rectangle_q_restoration()
{
    const std::string image="q 16 0 0 12 8 3 cm /I Do Q Q";
    const std::string fill="1 0 0 rg 8 6 8 6 re f";
    const char *const pair_shapes[][2]={
        {"8 4 12 12 re","12 6 12 12 re"},
        {"4 4 8 6 re","18 14 8 6 re"},
        {"4 4 8 8 re","12 6 8 10 re"}
    };
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop:{0,1,2}) {
        std::vector<unsigned char> path_only;
        CHECK(render(original(fill,false,quarters,rotation,crop),
                     quarters,rotation,dpi,crop,path_only)
              ==EXIT_SUCCESS);
        bool ink=false,white=false;
        for(size_t i=0u;i<path_only.size();i+=3u) {
            ink|=!is_white(path_only,i);
            white|=is_white(path_only,i);
        }
        CHECK(ink && white);
        for(size_t k=0u;k<3u;++k)
        for(bool even_a:{false,true})
        for(bool even_b:{false,true}) {
            const std::string input=std::string("q ")+
                pair_shapes[k][0]+(even_a?" W* n ":" W n ")+
                pair_shapes[k][1]+(even_b?" W* n ":" W n ")+
                image+" "+fill;
            std::vector<unsigned char> got;
            CHECK(render(original(input,true,quarters,rotation,crop),
                         quarters,rotation,dpi,crop,got)==EXIT_SUCCESS);
            if(k!=0u) {
                /* Empty intersection: Q-restored fill must be the
                 * only visible paint, byte-identical to the original
                 * PATH-only PDF with no Image or Clip resources. */
                CHECK(got.size()==path_only.size() &&
                      std::memcmp(got.data(),path_only.data(),
                                  got.size())==0);
            } else {
                /* A nonempty intersected Image must not be erased
                 * by the out-of-Clip subsequent red Fill either. */
                bool has_nonred=false;
                for(size_t i=0u;i<got.size();i+=3u)
                    has_nonred|=!is_white(got,i) &&
                        !(got[i]==255u && got[i+1u]==0u &&
                          got[i+2u]==0u);
                CHECK(has_nonred);
            }
        }
    }
    return EXIT_SUCCESS;
}

/* Split a REAL /Contents array between the second original W and its
 * n. The parser's bounded whole-page lease, pending-Clip ownership,
 * q/Q and source operator quota must never restart at the boundary. */
static int two_rectangle_split_contents()
{
    const std::string first="q 8 4 12 12 re W n 12 6 12 12 re W*";
    const std::string second="n q 16 0 0 12 8 3 cm /I Do Q Q";
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop:{0,1,2}) {
        const auto plain=original(first+" "+second,true,
                                  quarters,rotation,crop);
        const auto split=original(first,true,quarters,rotation,
                                  crop,second);
        std::vector<unsigned char> reference,got;
        CHECK(render(plain,quarters,rotation,dpi,crop,reference)
              ==EXIT_SUCCESS);
        CHECK(render(split,quarters,rotation,dpi,crop,got)
              ==EXIT_SUCCESS);
        CHECK(reference.size()==got.size() &&
              std::memcmp(reference.data(),got.data(),got.size())==0);
    }
    return EXIT_SUCCESS;
}

/* Observed first leaves retain their owned identity instead of rectangle
 * reduction. Image cardinality, malformed pending operands and third
 * definitions still refuse atomically after the admitted states. */
static int two_rectangle_atomic_refusals()
{
    const std::string first="8 4 12 12 re";
    const std::string second="12 6 12 12 re";
    const std::string image="q 16 0 0 12 8 3 cm /I Do Q Q";
    const std::vector<std::string> bad={
        /* First W outside q and two Images in states 2,1 now qualify.
         * A third Image after that real restoration still refuses. */
        first+" W n q "+second+" W* n "+image+
            " q 16 0 0 12 8 3 cm /I Do Q"
            " q 16 0 0 12 8 3 cm /I Do Q",
        "q "+first+" W n "+second+" W* "+image,
        "q "+first+" W n "+second+" W* W n "+image,
        "q "+first+" W n "+second+" W* n "+second+
            " W n "+image,
        /* Nested q with root-only paint now qualifies; redefining a
         * third Clip after Q restores leaf 1 still does not. */
        "q "+first+" W n q "+second+" W* n "+
            "q 16 0 0 12 8 3 cm /I Do Q Q "+first+" W n Q",
        "q "+first+" W n Q q "+second+" W* n "+image,
        "q "+first+" W n q 16 0 0 12 8 3 cm /I Do Q "+
            second+" W* n Q",
        /* A rectangle plus a valid cubic now has positive coverage in
         * test_native_two_curved_source; off-quarter controls do not. */
        "q "+first+" W n 4 5 m 4.125 18 20 18 20 5 c "+
            "20 4 l 4 4 l h W n "+image,
        "q "+first+" W n 12 6 m 12 16 l 20 16 l "+
            "20 6 l h W* n W n "+image,
        "q W n "+second+" W* n W n "+image,
        "q "+first+" W n W* 1 n "+image,
        "q "+first+" W n "+second+" W n "+
            "q 16 0 0 12 8 3 cm /Missing Do Q Q",
        "q "+first+" W n "+second+" W* n "+image+" BT ET",
        "q "+first+" W n "+second+" W n "+image+" S",
        "q "+first+" W n "+second+" W n "+image+" Q",
        "q "+first+" W n 12.125 6 12 12 re W n "+image,
        "q "+first+" W n 12 6 0 12 re W n "+image
    };
    for(const auto& input:bad)
    for(int crop:{0,1})
    for(int dpi:{72,96,144}) {
        const bool with_image=input.find("/I Do")!=std::string::npos ||
             input.find("/Missing Do")!=std::string::npos;
        CHECK(rejected(input,with_image,dpi,crop)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(equivalent_original_clips()==EXIT_SUCCESS);
    CHECK(q_restoration()==EXIT_SUCCESS);
    CHECK(definition_time_ctm()==EXIT_SUCCESS);
    CHECK(split_original_contents()==EXIT_SUCCESS);
    CHECK(bounded_control_outside_crop()==EXIT_SUCCESS);
    CHECK(atomic_rejections()==EXIT_SUCCESS);
    CHECK(empty_clip_pixels()==EXIT_SUCCESS);
    CHECK(disjoint_clip_and_unclipped_image()==EXIT_SUCCESS);
    CHECK(empty_or_disjoint_q_restoration()==EXIT_SUCCESS);
    CHECK(empty_clip_atomic_refusals()==EXIT_SUCCESS);
    CHECK(two_rectangle_clip_intersections()==EXIT_SUCCESS);
    CHECK(two_rectangle_q_restoration()==EXIT_SUCCESS);
    CHECK(two_rectangle_split_contents()==EXIT_SUCCESS);
    CHECK(two_rectangle_atomic_refusals()==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
