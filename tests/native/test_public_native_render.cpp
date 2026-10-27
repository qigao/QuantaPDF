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

/* Legitimate native zero-paint pages: Contents absent, an exact
 * empty stream, an empty Contents array, or balanced no-ink q/Q.
 * Unsupported text and even an empty /Annots key still refuse. */
static std::string create_blank_source(int mode)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);
    if(mode==1)page.replaceKey("/Contents",pdf->newStream(""));
    if(mode==2)page.replaceKey("/Contents",Obj::parse("[]"));
    if(mode==3)page.replaceKey("/Contents",
        pdf->newStream("q 1 0 0 1 10 0 cm Q"));
    if(mode==4)page.replaceKey("/Contents",
        pdf->newStream("BT ET"));
    if(mode==5)page.replaceKey("/Annots",Obj::parse("[]"));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(blob->getBuffer()),blob->getSize());
}


/* Native-only Path Fill originals with no /Resources or Image XObjects:
 * 1 PATH, 2 ordered PATHs, grayscale, and complete-source failures. */
static std::string create_path_only_source(int mode)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);
    std::string source;
    if(mode==0)
        source="1 0 0 rg 4 4 8 6 re f";
    if(mode==1)
        source="q 1 0 0 rg 4 4 8 6 re f Q "
               "0 0 1 rg 12 10 7 5 re f";
    if(mode==2)
        source="0.5 g 4 4 8 6 re f";
    if(mode==3)
        source="1 0 0 rg 4 4 8 6 re f BT ET";
    if(mode==4)
        source="1 0 0 rg 4 4 8 6 re f 4 4 8 6 re S";
    if(mode==5)
        source="q 4 4 8 6 re W n 1 0 0 rg 4 4 8 6 re f Q";
    if(mode==6)
        source="1 0 0 rg 4 4 8 6 re f "
               "0 0 1 rg 12 10 7 5 re f "
               "0 1 0 rg 20 3 4 4 re f";
    page.replaceKey("/Contents",pdf->newStream(source));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
}

static bool write_pdf(const std::string& filename,const std::string& content)
{
    FILE *f=nullptr;
#ifdef _WIN32
    if(fopen_s(&f,filename.c_str(),"wb")!=0)return false;
#else
    f=std::fopen(filename.c_str(),"wb");
#endif
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
    quantapdf_bitmap *bitmap=nullptr,*second=nullptr,*thumbnail=nullptr;
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
    /* A native 72-DPI image is the sole source for bounded thumbnail
     * area resampling; every 2x2 RGB box has an exact arithmetic mean,
     * with no PDFium or rescaling-specific source traversal. */
    OK(quantapdf_render_thumbnail(page,16,12,&thumbnail));
    CHECK(thumbnail!=nullptr);
    int tw=0,th=0,ts=0,tc=0;
    const unsigned char *tp=nullptr;
    size_t tn=0u;
    OK(quantapdf_bitmap_dimensions(thumbnail,&tw,&th,&ts,&tc));
    OK(quantapdf_bitmap_data(thumbnail,&tp,&tn));
    CHECK(tw==16 && th==12 && ts==48 && tc==3 &&
          tn==16u*12u*3u && tp!=nullptr);
    for(int y=0;y<12;++y)for(int x=0;x<16;++x)
        for(int channel=0;channel<3;++channel) {
            unsigned sum=0u;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                sum+=data[(size_t)(2*y+dy)*(size_t)stride+
                          (size_t)(2*x+dx)*3u+(size_t)channel];
            CHECK(tp[(size_t)y*(size_t)ts+(size_t)x*3u+
                     (size_t)channel]==(sum+2u)/4u);
        }
    /* Scale to the limiting height using an uneven fractional ratio. */
    OK(quantapdf_render_thumbnail(page,7,5,&second));
    OK(quantapdf_bitmap_dimensions(second,&tw,&th,&ts,&tc));
    CHECK(tw==6 && th==5 && ts==18 && tc==3);
    OK(quantapdf_bitmap_data(second,&tp,&tn));
    CHECK(tp!=nullptr && tn==6u*5u*3u &&
          probe_rgb(tp,ts,0,0,255u,255u,255u));
    quantapdf_drop_bitmap(second);second=nullptr;

    /* Exact 1x1 area mean must account for ALL original image pixels. */
    OK(quantapdf_render_thumbnail(page,1,1,&second));
    OK(quantapdf_bitmap_dimensions(second,&tw,&th,&ts,&tc));
    CHECK(tw==1 && th==1 && ts==3 && tc==3);
    OK(quantapdf_bitmap_data(second,&tp,&tn));
    CHECK(tp!=nullptr && tn==3u);
    for(int channel=0;channel<3;++channel) {
        unsigned sum=0u;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
            sum+=data[(size_t)y*(size_t)stride+
                      (size_t)x*3u+(size_t)channel];
        CHECK(tp[channel]==(sum+(unsigned)(width*height)/2u)/
                           (unsigned)(width*height));
    }
    quantapdf_drop_bitmap(second);second=nullptr;

    /* A larger bound never upscales, returns byte-identical Native RGB. */
    OK(quantapdf_render_thumbnail(page,64,64,&second));
    OK(quantapdf_bitmap_dimensions(second,&tw,&th,&ts,&tc));
    CHECK(tw==32 && th==24 && ts==96 && tc==3);
    OK(quantapdf_bitmap_data(second,&tp,&tn));
    CHECK(tn==size && std::memcmp(tp,data,size)==0);
    quantapdf_drop_bitmap(second);second=nullptr;

    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_thumbnail(page,0,12,&second)==
          QUANTAPDF_ERROR_ARGUMENT && second==nullptr);

    /* The output owns its detached native bytes and retains Context.
     * Only the returned Bitmap remains after the source owners go away. */
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_context(context);
    data=nullptr;size=0;
    OK(quantapdf_bitmap_data(bitmap,&data,&size));
    CHECK(probe_rgb(data,stride,13,11,255u,0u,0u));
    OK(quantapdf_bitmap_data(thumbnail,&tp,&tn));
    CHECK(tp!=nullptr && tn==16u*12u*3u &&
          probe_rgb(tp,48,1,1,255u,255u,255u));
    quantapdf_drop_bitmap(thumbnail);
    quantapdf_drop_bitmap(bitmap);

    CHECK(write_pdf(filename,create_source(true)));
    document=nullptr;page=nullptr;second=nullptr;
    OK(quantapdf_open(filename.c_str(),nullptr,&document));
    CHECK(std::remove(filename.c_str())==0);
    OK(quantapdf_load_page(document,0,&page));
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_page(page,&second)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(second==nullptr); /* no raster fallback / partial publication */
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_thumbnail(page,16,12,&second)==
          QUANTAPDF_ERROR_UNSUPPORTED && second==nullptr);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    /* The PDF structure may legitimately have no painting. Native V10
     * must return an opaque white page, not a failed Source profile or
     * a guessed incomplete white page after unsupported operators. */
    for(int mode=0;mode<6;++mode) {
        CHECK(write_pdf(filename,create_blank_source(mode)));
        quantapdf_context *blank_context=nullptr;
        quantapdf_document *blank_document=nullptr;
        quantapdf_page *blank_page=nullptr;
        quantapdf_bitmap *blank=nullptr,*small=nullptr;
        OK(quantapdf_context_create(&blank_context));
        OK(quantapdf_open_with_context(
            blank_context,filename.c_str(),nullptr,&blank_document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(blank_document,0,&blank_page));
        const quantapdf_status st=quantapdf_render_page(
            blank_page,&blank);
        if(mode>=4) {
            CHECK(st==QUANTAPDF_ERROR_UNSUPPORTED && blank==nullptr);
            small=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_thumbnail(blank_page,16,12,&small)==
                  QUANTAPDF_ERROR_UNSUPPORTED && small==nullptr);
        } else {
            CHECK(st==QUANTAPDF_OK && blank!=nullptr);
            int w=0,h=0,s=0,c=0;
            const unsigned char *buffer=nullptr;
            size_t n=0u;
            OK(quantapdf_bitmap_dimensions(blank,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(blank,&buffer,&n));
            CHECK(w==32 && h==24 && s==96 && c==3 &&
                  n==32u*24u*3u && buffer!=nullptr);
            for(size_t i=0u;i<n;++i)CHECK(buffer[i]==255u);
            OK(quantapdf_render_thumbnail(blank_page,16,12,&small));
            OK(quantapdf_bitmap_dimensions(small,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(small,&buffer,&n));
            CHECK(w==16 && h==12 && s==48 && c==3 &&
                  n==16u*12u*3u);
            for(size_t i=0u;i<n;++i)CHECK(buffer[i]==255u);
        }
        quantapdf_drop_page(blank_page);
        quantapdf_close(blank_document);
        quantapdf_drop_context(blank_context);
        if(blank!=nullptr) {
            const unsigned char *bytes=nullptr;
            size_t length=0u;
            OK(quantapdf_bitmap_data(blank,&bytes,&length));
            CHECK(bytes!=nullptr && length==32u*24u*3u &&
                  bytes[0]==255u && bytes[length-1u]==255u);
        }
        quantapdf_drop_bitmap(small);
        quantapdf_drop_bitmap(blank);
    }


    /* One or two opaque PATHs, no Image/Clip resources. Compare fixed
     * independently declared RGB goldens, NOT any PDFium oracle. Later
     * unsupported Text/Stroke/Clip/third PATH must refuse completely. */
    for(int mode=0;mode<7;++mode) {
        CHECK(write_pdf(filename,create_path_only_source(mode)));
        quantapdf_context *path_context=nullptr;
        quantapdf_document *path_document=nullptr;
        quantapdf_page *path_page=nullptr;
        quantapdf_bitmap *filled=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1)),*repeated=nullptr,*small=nullptr;
        OK(quantapdf_context_create(&path_context));
        OK(quantapdf_open_with_context(
            path_context,filename.c_str(),nullptr,&path_document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(path_document,0,&path_page));
        const auto st=quantapdf_render_page(path_page,&filled);
        if(mode>=3) {
            CHECK(st==QUANTAPDF_ERROR_UNSUPPORTED && filled==nullptr);
            small=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_thumbnail(path_page,16,12,&small)==
                  QUANTAPDF_ERROR_UNSUPPORTED && small==nullptr);
        } else {
            CHECK(st==QUANTAPDF_OK && filled!=nullptr);
            int w=0,h=0,s=0,c=0;
            const unsigned char *px=nullptr,*rp=nullptr;
            size_t n=0u,rn=0u;
            OK(quantapdf_bitmap_dimensions(filled,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(filled,&px,&n));
            CHECK(w==32 && h==24 && s==96 && c==3 &&
                  n==32u*24u*3u && px!=nullptr);
            const unsigned red=mode==2 ? 128u : 255u;
            const unsigned green=mode==2 ? 128u : 0u;
            const unsigned blue=mode==2 ? 128u : 0u;
            CHECK(probe_rgb(px,s,6,17,red,green,blue));
            CHECK(probe_rgb(px,s,1,1,255u,255u,255u));
            CHECK(probe_rgb(px,s,14,11,
                mode==1?0u:255u,
                mode==1?0u:255u,
                255u));
            OK(quantapdf_render_page(path_page,&repeated));
            OK(quantapdf_bitmap_data(repeated,&rp,&rn));
            CHECK(rn==n && rp!=nullptr && std::memcmp(px,rp,n)==0);
            quantapdf_drop_bitmap(repeated);repeated=nullptr;
            OK(quantapdf_render_thumbnail(path_page,16,12,&small));
            OK(quantapdf_bitmap_dimensions(small,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(small,&rp,&rn));
            CHECK(w==16 && h==12 && s==48 && c==3 &&
                  rn==16u*12u*3u && rp!=nullptr);
            CHECK(probe_rgb(rp,s,3,8,red,green,blue));
            CHECK(probe_rgb(rp,s,7,5,
                mode==1?0u:255u,
                mode==1?0u:255u,
                255u));
        }
        quantapdf_drop_page(path_page);
        quantapdf_close(path_document);
        quantapdf_drop_context(path_context);
        if(filled!=nullptr) {
            const unsigned char *owned=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_data(filled,&owned,&bytes));
            CHECK(owned!=nullptr && bytes==32u*24u*3u &&
                  probe_rgb(owned,96,6,17,
                            mode==2?128u:255u,
                            mode==2?128u:0u,
                            mode==2?128u:0u));
        }
        quantapdf_drop_bitmap(small);
        quantapdf_drop_bitmap(filled);
    }

    std::puts("[native-public-render] PATH-only fills, Path/Image, blank pages and strict refusal passed");
    return EXIT_SUCCESS;
}
