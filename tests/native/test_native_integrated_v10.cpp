/* Public Native V10 joint Source admission for six grammar additions without
 * implementing Stroke. QPDF independently authors ORIGINAL source PDFs;
 * a single owned C11 raster is the only actual pixel producer. */
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
    std::fprintf(stderr,"Native V10 integration failed line %d: %s\n", \
                 __LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-original-v10-integration.pdf";

static bool write_pdf(const std::string& bytes)
{
    FILE *f=nullptr;
#ifdef _WIN32
    if(fopen_s(&f,filename,"wb")!=0)return false;
#else
    f=std::fopen(filename,"wb");
#endif
    if(f==nullptr)return false;
    const bool wrote=
        std::fwrite(bytes.data(),1u,bytes.size(),f)==bytes.size();
    return std::fclose(f)==0 && wrote;
}

static std::string original(const std::string& operators,
                            bool with_image,int quarters,int rotation)
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
    if(!operators.empty())
        page.replaceKey("/Contents",pdf->newStream(operators));
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
        auto objects=Obj::newDictionary();
        objects.replaceKey("/I",image);
        auto resources=Obj::newDictionary();
        resources.replaceKey("/XObject",objects);
        page.replaceKey("/Resources",resources);
    }
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
}

static int render(const std::string& bytes,int quarters,int rotation,
                  int dpi,std::vector<unsigned char>& rgb)
{
    CHECK(write_pdf(bytes));
    quantapdf_context *context=nullptr;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(
        context,filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);
    OK(quantapdf_load_page(doc,0,&page));
    quantapdf_render_options opts{};
    opts.struct_size=sizeof(opts);
    opts.dpi=(float)dpi;
    CHECK(quantapdf_render_page_with_options(
        page,&opts,&bitmap)==QUANTAPDF_OK && bitmap!=nullptr);
    int width=0,height=0,stride=0,channels=0;
    const unsigned char *pixels=nullptr;
    size_t bytes_count=0u;
    OK(quantapdf_bitmap_dimensions(
        bitmap,&width,&height,&stride,&channels));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&bytes_count));
    const int sw=(rotation==90)?24:32;
    const int sh=(rotation==90)?32:24;
    const int expected_w=(sw*quarters*dpi+287)/288;
    const int expected_h=(sh*quarters*dpi+287)/288;
    CHECK(width==expected_w && height==expected_h &&
          stride==width*3 && channels==3 && pixels!=nullptr &&
          bytes_count==(size_t)stride*(size_t)height);
    rgb.assign(pixels,pixels+bytes_count);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    quantapdf_drop_context(context);
    /* Public Bitmap owns RGB independently of retired input handles. */
    const unsigned char *retained=nullptr;
    size_t retained_size=0u;
    OK(quantapdf_bitmap_data(bitmap,&retained,&retained_size));
    CHECK(retained!=nullptr && retained_size==rgb.size() &&
          std::memcmp(retained,rgb.data(),retained_size)==0);
    quantapdf_drop_bitmap(bitmap);
    return EXIT_SUCCESS;
}

struct case_data {
    const char *plain;
    const char *decorated;
    bool image;
    bool blank;
};
static const case_data cases[]={
    {"",
     "0 w 0 J 0 j 1 M [] 0 d 0 G 0 0 0 RG 0 0 0 1 K 2 2 3 3 re n",false,true},
    {"1 0 0 rg 4 4 8 6 re f",
     "0 w 1 J 2 j 5 M [1 2] 1 d 0.3 G 0 1 0 RG 0 0 0 1 K 2 2 3 3 re n 1 0 0 rg 12 10 -8 -6 re f",false,false},
    {"0.5 g 4 4 8 6 re f",
     "2.25 w 2 J 1 j 10 M [2 0 3] 0 d 1 G 1 0 1 RG 0 1 0 1 K 0.5 g 4 10 8 -6 re f",false,false},
    {"q 1 0 0 rg 4 4 8 6 re f Q 0 0 1 rg 12 10 7 5 re f",
     "q 0 w 2 J 1 j 1 M [1] 0 d 0 G 1 1 0 RG 1 0 0 rg 12 4 -8 6 re f Q 0 0 0 1 K 0 0 1 rg 19 15 -7 -5 re f",false,false},
    {"q 16 0 0 12 8 3 cm /I Do Q",
     "1 w 0 J 0 j 2 M [1 2] 0 d 0.2 G 0.1 0.2 0.3 RG 0 0 0 0 K 2 2 3 3 re n q 16 0 0 12 8 3 cm /I Do Q",true,false},
    {"q 16 0 0 12 8 3 cm /I Do Q 1 0 0 rg 4 4 8 6 re f",
     "0 w 1 J 1 j 1 M [] 0 d 0 G 0 0 0 RG q 16 0 0 12 8 3 cm /I Do Q 0 0 0 1 K 1 0 0 rg 12 10 -8 -6 re f",true,false},
    {"q 12 6 8 9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q",
     "1 w 2 J 1 j 4 M [1 3] 0 d 1 G 0 1 0 RG 2 2 3 3 re n q 20 15 -8 -9 re W* n 0 0 0 1 K q 16 0 0 12 8 3 cm /I Do Q Q",true,false},
    {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q 0.875 g 24 17 5 3 re f",
     "q 0 w 0 J 0 j 5 M [1 0 2] 1 d 0.5 G 20 15 -8 -9 re W n 1 0 1 RG q 16 0 0 12 8 3 cm /I Do Q Q 0 0 0 1 K 0.875 g 29 20 -5 -3 re f",true,false},
    {"1 0 0 rg 4 4 8 6 re f q 16 0 0 12 8 3 cm /I Do Q",
     "0 w 2 J 1 j 1 M [] 0 d 0 G 0 0 0 RG 0 0 0 1 K 2 2 3 3 re n 1 0 0 rg 12 10 -8 -6 re f q 16 0 0 12 8 3 cm /I Do Q",true,false}
};

static int positive()
{
    for(const auto& data:cases)
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144}) {
        std::vector<unsigned char> plain,got;
        CHECK(render(original(data.plain,data.image,quarters,rotation),
                     quarters,rotation,dpi,plain)==EXIT_SUCCESS);
        CHECK(render(original(data.decorated,data.image,quarters,rotation),
                     quarters,rotation,dpi,got)==EXIT_SUCCESS);
        CHECK(plain.size()==got.size() &&
              std::memcmp(plain.data(),got.data(),got.size())==0);
        bool all_white=true,has_ink=false;
        for(size_t k=0;k<got.size();k+=3u) {
            const bool white=got[k]==255u &&
                got[k+1u]==255u && got[k+2u]==255u;
            all_white&=white;has_ink|=!white;
        }
        CHECK(data.blank?all_white:has_ink);
    }
    return EXIT_SUCCESS;
}

static int negative()
{
    const char *const bad[]={
        "1 0 0 rg 12 10 -8 -6 re f 1 w 2 G 1 M [1] 0 d S",
        "1 0 0 rg 4 4 8 6 re f 12 10 -8 -6 re 0.3 w f",
        "0 w 0 G 0 0 0 RG 0 0 0 1 K 1 M [] 0 d 12 10 -8 -6 re f BT ET",
        "0 w 1 0 0 rg 12 10 -8 -6 re f 1000001 M",
        "0 w 12 10 -8 -6 re f [0 0] 0 d",
        "0 G 2 2 3 3 re n 12 10 -8 -6 re f 2 G",
        "0 w 0 G 1 0 0 rg 12 10 -8 -6 re f 0 -0.1 0 RG",
        "0 w 12 10 -8 -6 re f Q",
        "12 10 -8 -6 re W 1 M n",
        "q 20 15 -8 -9 re W n q 16 0 0 12 8 3 cm /Missing Do Q Q",
        "12 10 0 -6 re f",
        "0 G 2 2 3 3 re n 12 10 -8 -6 re f 1 n",
        "1 0 0 rg 12 10 -8 -6 re f q 16 0 0 12 8 3 cm /I Do Q S",
        "q 20 15 -8 -9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q 0 0 2 RG",
        "1 0 0 rg 12 10 -8 -6 re f 0 0 0 1 K 0 1 1 0 k",
        "1 0 0 rg 12 10 -8 -6 re f q 16 0 0 12 8 3 cm /I Do Q [0 0] 0 d"
    };
    for(const char *ops:bad)
    for(int dpi:{72,96,144}) {
        const bool with_image=std::strstr(ops,"/I Do")!=nullptr;
        CHECK(write_pdf(original(ops,with_image,4,0)));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=
            reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_open(filename,nullptr,&doc));
        CHECK(std::remove(filename)==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);
        opts.dpi=(float)dpi;
        const auto status=quantapdf_render_page_with_options(
            page,&opts,&bitmap);
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
