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
        "q "+path+" W n "+path+" W* n "
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

int main()
{
    CHECK(equivalent_original_clips()==EXIT_SUCCESS);
    CHECK(q_restoration()==EXIT_SUCCESS);
    CHECK(definition_time_ctm()==EXIT_SUCCESS);
    CHECK(split_original_contents()==EXIT_SUCCESS);
    CHECK(atomic_rejections()==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
