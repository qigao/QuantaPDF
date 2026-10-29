/* Native-only PUBLIC Source V10 acceptance of stroking-only PDF
 * graphics-state setters w/J/j. These do not affect opaque Fill or
 * RAW Image rendering, while actual Stroke remains fail-closed.
 * Original PDFs come from QPDF; no PDFium or second raster oracle. */
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
    std::fprintf(stderr,"Native stroke-state failed line %d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-original-stroke-state.pdf";

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

static std::string original(const std::string& operators,
                            bool with_image,int quarters,int rotation)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/Rotate",Obj::newInteger(rotation));
    page.replaceKey("/UserUnit",Obj::parse(
        std::to_string((double)quarters/4.0)));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    if(!operators.empty())
        page.replaceKey("/Contents",pdf->newStream(operators));
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
    const auto status=quantapdf_render_page_with_options(
        page,&opts,&bitmap);
    if(status!=QUANTAPDF_OK)
        std::fprintf(stderr,
            "stroke-state render rejected quarters=%d rotation=%d "
            "dpi=%d status=%d\n",quarters,rotation,dpi,(int)status);
    CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
    int width=0,height=0,stride=0,channels=0;
    const unsigned char *pixels=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_dimensions(
        bitmap,&width,&height,&stride,&channels));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
    const int sw=(rotation==90)?24:32;
    const int sh=(rotation==90)?32:24;
    const int expected_w=(sw*quarters*dpi+287)/288;
    const int expected_h=(sh*quarters*dpi+287)/288;
    CHECK(width==expected_w && height==expected_h &&
          stride==width*3 && channels==3 &&
          pixels!=nullptr && size==(size_t)stride*(size_t)height);
    rgb.assign(pixels,pixels+size);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    quantapdf_drop_context(context);
    const unsigned char *owned=nullptr;
    size_t n=0u;
    OK(quantapdf_bitmap_data(bitmap,&owned,&n));
    CHECK(owned!=nullptr && n==rgb.size() &&
          std::memcmp(rgb.data(),owned,n)==0);
    quantapdf_drop_bitmap(bitmap);
    return EXIT_SUCCESS;
}

struct case_data {
    const char *plain;
    const char *stroke_only_state;
    bool image;
    bool blank;
};
static const case_data cases[]={
    {"", "0 w 0 J 0 j", false,true},
    {"1 0 0 rg 4 4 8 6 re f",
     "0 w 2 J 1 j 1 0 0 rg 4 4 8 6 re f",false,false},
    {"0.5 g 4 4 8 6 re f",
     "2.5 w 1 J 2 j 0.5 g 4 4 8 6 re f",false,false},
    {"q 1 0 0 rg 4 4 8 6 re f Q "
     "0 0 1 rg 12 10 7 5 re f",
     "q 0 w 2 J 1 j 1 0 0 rg 4 4 8 6 re f Q "
     "8 w 0 J 0 j 0 0 1 rg 12 10 7 5 re f",false,false},
    {"q 16 0 0 12 8 3 cm /I Do Q",
     "0 w 0 J 1 j q 16 0 0 12 8 3 cm /I Do Q",true,false},
    {"q 16 0 0 12 8 3 cm /I Do Q "
     "1 0 0 rg 4 4 8 6 re f",
     "2 w 2 J 1 j q 16 0 0 12 8 3 cm /I Do Q "
     "0 w 0 J 0 j 1 0 0 rg 4 4 8 6 re f",true,false},
    {"q 12 6 8 9 re W* n "
     "q 16 0 0 12 8 3 cm /I Do Q Q",
     "2 w 1 J 2 j q 12 6 8 9 re W* n "
     "0 w 2 J 1 j q 16 0 0 12 8 3 cm /I Do Q Q",true,false},
    {"q 12 6 8 9 re W n "
     "q 16 0 0 12 8 3 cm /I Do Q "
     "Q 0.875 g 24 17 5 3 re f",
     "q 2 w 1 J 2 j 12 6 8 9 re W n "
     "q 16 0 0 12 8 3 cm /I Do Q "
     "Q 0 w 0 J 0 j 0.875 g 24 17 5 3 re f",true,false}
};

static int positive()
{
    for(size_t kind=0u;kind<sizeof(cases)/sizeof(cases[0]);++kind)
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144}) {
        const case_data& data=cases[kind];
        std::vector<unsigned char> ref,actual;
        CHECK(render(original(data.plain,data.image,quarters,rotation),
                     quarters,rotation,dpi,ref)==EXIT_SUCCESS);
        CHECK(render(original(data.stroke_only_state,data.image,
                              quarters,rotation),
                     quarters,rotation,dpi,actual)==EXIT_SUCCESS);
        CHECK(ref.size()==actual.size() &&
              std::memcmp(ref.data(),actual.data(),ref.size())==0);
        bool has_ink=false,all_white=true;
        for(size_t i=0u;i<actual.size();i+=3u) {
            const bool white=actual[i]==255u &&
                actual[i+1u]==255u && actual[i+2u]==255u;
            all_white&=white;has_ink|=!white;
        }
        CHECK(data.blank?all_white:has_ink);
    }
    return EXIT_SUCCESS;
}
static int negative()
{
    const char *const bad[]={
        "w","1 2 w","-1 w","1000001 w","true w",
        "3 J","-1 J","1.5 J","/Cap J",
        "3 j","-1 j","1.5 j","true j",
        "1 w 1 0 0 rg 4 4 8 6 re S",
        "1 J 1 0 0 rg 4 4 8 6 re B",
        "0 j BT ET",
        "0 w 0 J 1 j Q",
        "4 4 8 6 re 1 w f",
        "0 w /Missing Do"
    };
    for(const char* ops:bad)
    for(int dpi:{72,96,144}) {
        CHECK(write_pdf(original(ops,false,4,0)));
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
