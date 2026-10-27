/* Native-only public SDK acceptance; never imports PDFium and never
 * creates an IR re-export oracle. Source PDF is authored with QPDF, while
 * the expected RGB samples are fixed independent render semantics. */
#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr,"native public render failed at %d: %s\n",__LINE__,#expr); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(expr) CHECK((expr)==QUANTAPDF_OK)

using Obj = QPDFObjectHandle;

static std::string create_source(bool late_text)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);

    const std::string raw("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    auto image=pdf->newStream(raw);
    image.getDict().replaceKey("/Type",Obj::newName("/XObject"));
    image.getDict().replaceKey("/Subtype",Obj::newName("/Image"));
    image.getDict().replaceKey("/Width",Obj::newInteger(2));
    image.getDict().replaceKey("/Height",Obj::newInteger(2));
    image.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    image.getDict().replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
    auto objects=Obj::newDictionary(),resources=Obj::newDictionary();
    objects.replaceKey("/I",image);
    resources.replaceKey("/XObject",objects);
    page.replaceKey("/Resources",resources);
    const std::string source=
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q "
        "Q 0.875 g 24 17 5 3 re f"
        +(late_text ? std::string(" BT ET") : std::string());
    page.replaceKey("/Contents",pdf->newStream(source));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char *>(blob->getBuffer()),
                       blob->getSize());
}

static bool write_pdf(const std::string& filename,const std::string& content)
{
    FILE *f=std::fopen(filename.c_str(),"wb");
    if(!f)return false;
    const bool good=std::fwrite(content.data(),1u,content.size(),f)==content.size();
    return std::fclose(f)==0 && good;
}

static int probe_rgb(const unsigned char *data,int stride,int x,int y,
                     unsigned red,unsigned green,unsigned blue)
{
    const auto *px=data+(size_t)y*(size_t)stride+(size_t)x*3u;
    return px[0]==red && px[1]==green && px[2]==blue;
}

int main()
{
    const std::string filename="native-standalone-smoke.pdf";
    CHECK(write_pdf(filename,create_source(false)));

    quantapdf_context *context=nullptr;
    quantapdf_document *document=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr,*second=nullptr;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(context,filename.c_str(),nullptr,&document));
    CHECK(std::remove(filename.c_str())==0);
    int count=0;
    OK(quantapdf_page_count(document,&count));
    CHECK(count==1);
    OK(quantapdf_load_page(document,0,&page));

    OK(quantapdf_render_page(page,&bitmap));
    CHECK(bitmap!=nullptr);
    int width=0,height=0,stride=0,components=0;
    const unsigned char *data=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_dimensions(bitmap,&width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(bitmap,&data,&size));
    CHECK(width==32 && height==24 && components==3 && stride==96 &&
          size==32u*24u*3u && data!=nullptr);
    CHECK(probe_rgb(data,stride,13,11,255u,0u,0u));
    CHECK(probe_rgb(data,stride,26,5,223u,223u,223u));
    CHECK(probe_rgb(data,stride,2,2,255u,255u,255u));

    quantapdf_render_options opts{};
    opts.struct_size=sizeof(opts);
    opts.dpi=72.0f;
    OK(quantapdf_render_page_with_options(page,&opts,&second));
    CHECK(second!=nullptr);
    const unsigned char *bytes2=nullptr;
    size_t size2=0;
    OK(quantapdf_bitmap_data(second,&bytes2,&size2));
    CHECK(size2==size && std::memcmp(data,bytes2,size)==0);
    quantapdf_drop_bitmap(second);second=nullptr;

    opts.dpi=96.0f;
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_page_with_options(page,&opts,&second)==
          QUANTAPDF_ERROR_UNSUPPORTED && second==nullptr);
    opts.dpi=72.0f;opts.rotation_degrees=90.0f;
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_page_with_options(page,&opts,&second)==
          QUANTAPDF_ERROR_UNSUPPORTED && second==nullptr);
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_thumbnail(page,16,12,&second)==
          QUANTAPDF_ERROR_UNSUPPORTED && second==nullptr);

    /* The output owns its detached native bytes and retains Context.
     * Only the returned Bitmap remains after the source owners go away. */
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_context(context);
    data=nullptr;size=0;
    OK(quantapdf_bitmap_data(bitmap,&data,&size));
    CHECK(probe_rgb(data,stride,13,11,255u,0u,0u));
    quantapdf_drop_bitmap(bitmap);

    CHECK(write_pdf(filename,create_source(true)));
    document=nullptr;page=nullptr;second=nullptr;
    OK(quantapdf_open(filename.c_str(),nullptr,&document));
    CHECK(std::remove(filename.c_str())==0);
    OK(quantapdf_load_page(document,0,&page));
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_page(page,&second)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(second==nullptr); /* no raster fallback / partial publication */
    quantapdf_drop_page(page);
    quantapdf_close(document);
    std::puts("[native-public-render] source path/image and strict refusal passed");
    return EXIT_SUCCESS;
}
