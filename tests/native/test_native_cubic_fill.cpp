/* Original PDF cubic Bezier c opaque Fill for public Native Source V10.
 * Valid negative widths/heights preserve the author's m/l/l/l/h winding
 * and must paint identically to rectangles with the same area. Original
 * PDF source is authored by QPDF; there is ONE pure C11 raster producer,
 * no PDFium, fallback, IR re-export or second rendering oracle. */
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
    std::fprintf(stderr,"Native cubic Fill failed line %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-original-cubic-fill.pdf";

static bool write_pdf(const std::string& content)
{
    FILE *f=nullptr;
#ifdef _WIN32
    if(fopen_s(&f,filename,"wb")!=0)return false;
#else
    f=std::fopen(filename,"wb");
#endif
    if(f==nullptr)return false;
    const bool complete=
        std::fwrite(content.data(),1u,content.size(),f)==content.size();
    return std::fclose(f)==0 && complete;
}
static std::string operators(int kind,bool even_odd)
{
    const std::string paint=even_odd?" f*":" f";
    const std::string arch="4 5 m 4 18 20 18 20 5 c "
        "20 4 l 4 4 l h";
    const std::string lens="4 5 m 4 18 20 18 20 5 c "
        "20 2 4 2 4 5 c h";
    const std::string image="q 16 0 0 12 8 3 cm /I Do Q";
    if(kind==0)return "1 0 0 rg "+arch+paint;
    if(kind==1)return "0.5 g "+lens+paint;
    if(kind==2)return "q 0 w 2 J 1 j 3 M [1 2] 0 d "
        "1 G 0 1 0 RG 0 0 0 1 K 1 0 0 rg "+
        arch+paint+" Q";
    if(kind==3)return "1 0 0 rg "+arch+paint+
        " 0 0 1 rg 24 17 5 3 re f";
    if(kind==4)return image+" 1 0 0 rg "+arch+paint;
    if(kind==5)return "1 0 0 rg "+arch+paint+" "+image;
    if(kind==6)return "4 5 m 4 18 20 18 20 5 c "
        "20 4 l 4 4 l h n 1 0 0 rg "+arch+paint;
    if(kind==7)return "q 1 0 0 1 2 1 cm 1 0 0 rg "+
        arch+paint+" Q";
    if(kind==8)return "1 0 0 rg 4 5 m "
        "4 11.5 8 14.75 12 14.75 c "
        "16 14.75 20 11.5 20 5 c "
        "20 4 l 4 4 l h"+paint;
    return {};
}

static std::string original(const std::string& contents,
                            bool with_image,int quarter_unit,int rotation)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/UserUnit",Obj::parse(
        std::to_string((double)quarter_unit/4.0)));
    page.replaceKey("/Rotate",Obj::newInteger(rotation));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Contents",pdf->newStream(contents));
    if(with_image) {
        const std::string pixels(
            "\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
        auto image=pdf->newStream(pixels);
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
    return std::string(reinterpret_cast<const char*>(blob->getBuffer()),
                       blob->getSize());
}
static int rendered_rgb(const std::string& pdf,int quarters,int angle,int dpi,
                        std::vector<unsigned char>& rgb)
{
    CHECK(write_pdf(pdf));
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
    options.dpi=(float)dpi;
    const auto status=quantapdf_render_page_with_options(
        page,&options,&bitmap);
    if(status!=QUANTAPDF_OK)
        std::fprintf(stderr,"cubic Fill rejected quarters=%d "
            "original_rotate=%d dpi=%d status=%d\n",
            quarters,angle,dpi,(int)status);
    CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
    int width=0,height=0,stride=0,channels=0;
    const unsigned char *pixels=nullptr;
    size_t bytes=0u;
    OK(quantapdf_bitmap_dimensions(
        bitmap,&width,&height,&stride,&channels));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&bytes));
    const int source_w=(angle==90||angle==270)?24:32;
    const int source_h=(angle==90||angle==270)?32:24;
    const int expected_w=(source_w*quarters*dpi+287)/288;
    const int expected_h=(source_h*quarters*dpi+287)/288;
    CHECK(width==expected_w && height==expected_h &&
          stride==width*3 && channels==3 &&
          bytes==(size_t)stride*(size_t)height && pixels!=nullptr);
    rgb.assign(pixels,pixels+bytes);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    quantapdf_drop_context(context);
    /* The detached Context-owned public Bitmap is still live after
     * all source/owner handles and original file have retired. */
    const unsigned char *retained=nullptr;
    size_t retained_bytes=0u;
    OK(quantapdf_bitmap_data(bitmap,&retained,&retained_bytes));
    CHECK(retained!=nullptr && retained_bytes==rgb.size() &&
          std::memcmp(rgb.data(),retained,retained_bytes)==0);
    quantapdf_drop_bitmap(bitmap);
    return EXIT_SUCCESS;
}
static int positive(void)
{
    for(int kind=0;kind<=8;++kind)
    for(int quarters:{4,5})
    for(int angle:{0,90})
    for(int dpi:{72,96,144}) {
        const bool with_image=(kind==4 || kind==5);
        const auto even=original(operators(kind,true),
                                 with_image,quarters,angle);
        const auto nonzero=original(operators(kind,false),
                                    with_image,quarters,angle);
        std::vector<unsigned char> reference,got;
        CHECK(rendered_rgb(nonzero,quarters,angle,dpi,reference)
              ==EXIT_SUCCESS);
        CHECK(rendered_rgb(even,quarters,angle,dpi,got)
              ==EXIT_SUCCESS);
        CHECK(reference.size()==got.size() &&
              std::memcmp(reference.data(),got.data(),got.size())==0);
        bool has_ink=false,has_white=false;
        for(size_t i=0u;i<got.size();i+=3u) {
            const bool white=got[i]==255u &&
                got[i+1u]==255u && got[i+2u]==255u;
            has_ink|=!white;has_white|=white;
        }
        CHECK(has_ink && has_white);
        /* Actual curved interior versus exterior, not merely any
         * generic nonblank rectangle. Source is 32x24 at 72 DPI. */
        if(kind==0 && quarters==4 && angle==0 && dpi==72) {
            const size_t center=(13u*32u+12u)*3u;
            const size_t above=(3u*32u+12u)*3u;
            CHECK(reference[center]==255u &&
                  reference[center+1u]==0u &&
                  reference[center+2u]==0u);
            CHECK(reference[above]==255u &&
                  reference[above+1u]==255u &&
                  reference[above+2u]==255u);
        }
    }
    return EXIT_SUCCESS;
}

static int negative(void)
{
    const char *const invalid[]={
        "4 5 m 4 18 20 18 20 5 c S",
        "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h B",
        "4 18 20 18 20 5 c 20 4 l 4 4 l h f",
        "4 5 m c",
        "4 5 m 4 18 20 18 20 c 20 4 l 4 4 l h f",
        "4 5 m true 18 20 18 20 5 c 20 4 l 4 4 l h f",
        "4 5 m 4 18 20 18 20 5 0 c 20 4 l 4 4 l h f",
        "4 5 m 4.125 18 20 18 20 5 c 20 4 l 4 4 l h f",
        "4 5 m 4 -1 20 18 20 5 c 20 4 l 4 4 l h f",
        "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h q f Q",
        "4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h 4 18 20 18 20 5 c f",
        "q 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h W n Q",
        "q 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h W* n Q",
        "4 5 m 4 18 20 18 20 5 v 20 4 l 4 4 l h f",
        "4 5 m 4 18 20 18 20 5 y 20 4 l 4 4 l h f",
        "1 0 0 rg 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h f BT ET",
        "1 0 0 rg 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h f 4 5 m 4 18 20 18 20 5 c S",
        "1 0 0 rg 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h f 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h W n",
        "q 16 0 0 12 8 3 cm /I Do Q 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h f Q",
        "q 16 0 0 12 8 3 cm /Missing Do Q 1 0 0 rg 4 5 m 4 18 20 18 20 5 c 20 4 l 4 4 l h f"
    };
    for(const char *content:invalid)
    for(int dpi:{72,96,144}) {
        const bool with_image=
            std::strstr(content,"/I Do")!=nullptr ||
            std::strstr(content,"/Missing Do")!=nullptr;
        CHECK(write_pdf(original(content,with_image,4,0)));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=
            reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_open(filename,nullptr,&doc));
        CHECK(std::remove(filename)==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options options{};
        options.struct_size=sizeof(options);
        options.dpi=(float)dpi;
        const auto status=quantapdf_render_page_with_options(
            page,&options,&bitmap);
        CHECK(status!=QUANTAPDF_OK && bitmap==nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    return EXIT_SUCCESS;
}
int main()
{
    CHECK(positive()==EXIT_SUCCESS);
    CHECK(negative()==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
