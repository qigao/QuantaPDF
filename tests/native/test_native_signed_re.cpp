/* Original PDF signed 're' rectangle admission for public Native V10.
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
    std::fprintf(stderr,"Native signed re failed line %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-original-signed-rect.pdf";

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
static std::string rect(int x,int y,int w,int h,int direction)
{
    switch(direction) {
    case 0:break;
    case 1:x+=w;w=-w;break;
    case 2:y+=h;h=-h;break;
    case 3:x+=w;y+=h;w=-w;h=-h;break;
    default:return {};
    }
    return std::to_string(x)+" "+std::to_string(y)+" "+
        std::to_string(w)+" "+std::to_string(h)+" re";
}

static std::string operators(int kind,int direction)
{
    const auto red=rect(4,4,8,6,direction);
    const auto blue=rect(12,10,7,5,direction);
    const auto clip=rect(12,6,8,9,direction);
    const auto gray=rect(24,17,5,3,direction);
    const std::string draw="q 16 0 0 12 8 3 cm /I Do Q";
    if(kind==0)return "1 0 0 rg "+red+" f";
    if(kind==1)return "0 0 1 rg "+red+" f*";
    if(kind==2)return "1 0 0 rg "+red+" f 0 0 1 rg "+blue+" f";
    if(kind==3)return "q "+clip+" W n "+draw+" Q";
    if(kind==4)return "q "+clip+" W* n "+draw+" Q";
    if(kind==5)return "q "+clip+" W n "+draw+
        " Q 0.875 g "+gray+" f";
    if(kind==6)return draw+" 1 0 0 rg "+red+" f";
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
        std::fprintf(stderr,"signed re rejected quarters=%d "
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
    for(int kind=0;kind<=6;++kind)
    for(int quarter_unit:{4,5})
    for(int angle:{0,90})
    for(int dpi:{72,96,144}) {
        const bool image=kind>=3;
        std::vector<unsigned char> reference;
        for(int direction=0;direction<4;++direction) {
            const auto pdf=original(operators(kind,direction),
                                     image,quarter_unit,angle);
            std::vector<unsigned char> got;
            CHECK(rendered_rgb(pdf,quarter_unit,angle,dpi,got)==
                  EXIT_SUCCESS);
            bool has_ink=false,has_white=false;
            for(size_t i=0u;i<got.size();i+=3u) {
                const bool white=got[i]==255u &&
                    got[i+1u]==255u && got[i+2u]==255u;
                has_ink|=!white;has_white|=white;
            }
            CHECK(has_ink && has_white);
            if(direction==0)reference=std::move(got);
            else CHECK(reference.size()==got.size() &&
                       std::memcmp(reference.data(),got.data(),
                                   got.size())==0);
        }
    }
    return EXIT_SUCCESS;
}

static int negative(void)
{
    const char *const invalid[]={
        "1 0 0 rg 4 4 0 6 re f",
        "1 0 0 rg 4 4 8 0 re f",
        "1 0 0 rg 4 4 -0 6 re f",
        "1 0 0 rg 4 4 -8 6 re f", /* outside strict MediaBox */
        "1 0 0 rg 4 4 8 -6 re f", /* outside strict MediaBox */
        "1 0 0 rg 12 4 -8.125 6 re f", /* nonquarter */
        "1 0 0 rg 12 4 -8 6 re f BT ET",
        "1 0 0 rg 12 4 -8 6 re f 4 4 8 6 re S",
        "1 0 0 rg 12 4 -8 6 re f Q",
        ("q 20 6 -8 9 re W n "
         "q 16 0 0 12 8 3 cm /Missing Do Q Q")
    };
    for(const char *content:invalid)
    for(int dpi:{72,96,144}) {
        const bool needs_image=std::strstr(content,"/Missing")!=nullptr;
        CHECK(write_pdf(original(content,needs_image,4,0)));
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
