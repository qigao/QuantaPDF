/* Original PDF n (end path without painting) — native-only acceptance.
 * No PDFium, no IR export oracle, no secondary renderer and no SDK API.
 * Compare two independently QPDF-authored original files byte-for-byte
 * through the public Source V10 -> owned IR -> one C11 Raster pipeline. */
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
    std::fprintf(stderr,"original n Native failed at %d: %s\n", \
                 __LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char* const filename="native-original-unpainted-n.pdf";

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

static std::string original_pdf(const std::string& operators,
                                bool with_image)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);
    if(!operators.empty())
        page.replaceKey("/Contents",pdf->newStream(operators));
    if(with_image) {
        const std::string red_green_blue(
            "\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
        auto image=pdf->newStream(red_green_blue);
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
    const auto result=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(result->getBuffer()),
        result->getSize());
}

static int render(const std::string& source,int dpi,
                  std::vector<unsigned char>& rgb)
{
    CHECK(write_pdf(source));
    quantapdf_context *context=nullptr;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(
        context,filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);
    OK(quantapdf_load_page(doc,0,&page));
    quantapdf_render_options options{};
    options.struct_size=sizeof(options);
    options.dpi=(float)dpi;
    OK(quantapdf_render_page_with_options(page,&options,&bitmap));
    CHECK(bitmap!=nullptr);
    int width=0,height=0,stride=0,channels=0;
    const unsigned char *pixels=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_dimensions(bitmap,&width,&height,&stride,
                                   &channels));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
    const int ew=(32*dpi+71)/72,eh=(24*dpi+71)/72;
    CHECK(width==ew && height==eh &&
          stride==width*3 && channels==3 && pixels!=nullptr &&
          size==(size_t)stride*(size_t)height);
    rgb.assign(pixels,pixels+size);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    quantapdf_drop_context(context);
    const unsigned char *retained=nullptr;
    size_t retained_size=0u;
    OK(quantapdf_bitmap_data(bitmap,&retained,&retained_size));
    CHECK(retained!=nullptr && retained_size==rgb.size() &&
          std::memcmp(retained,rgb.data(),rgb.size())==0);
    quantapdf_drop_bitmap(bitmap);
    return EXIT_SUCCESS;
}

struct positive_case {
    const char *original;
    const char *with_n;
    bool image;
    bool blank;
};
static const positive_case cases[]={
    /* A standalone n and a no-paint rectangle are legitimate blanks. */
    {"","n",false,true},
    {"","4 4 8 6 re n",false,true},
    /* Discard an unpainted PATH before the real single PATH. */
    {"1 0 0 rg 4 4 8 6 re f",
     "1 0 0 rg 12 10 7 5 re n 4 4 8 6 re f",false,false},
    /* Discard between two actual painted PATHs without a hidden layer. */
    {"1 0 0 rg 4 4 8 6 re f "
     "0 0 1 rg 12 10 7 5 re f",
     "1 0 0 rg 4 4 8 6 re f 8 5 3 4 re n "
     "0 0 1 rg 12 10 7 5 re f",false,false},
    /* A throwaway PATH cannot change RAW Image coordinate mapping. */
    {"q 16 0 0 12 8 3 cm /I Do Q",
     "8 6 4 4 re n q 16 0 0 12 8 3 cm /I Do Q",true,false},
    /* Existing frozen W/W* Clip definition remains an actual Clip. */
    {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q",
     "1 1 3 2 re n "
     "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q",
     true,false},
    /* Restore a clipped Image, then render a PATH outside the Clip. */
    {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
     "Q 0.875 g 24 17 5 3 re f",
     "1 1 3 2 re n "
     "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
     "Q 0.875 g 24 17 5 3 re f",
     true,false}
};
static int positive()
{
    for(size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
    for(int dpi:{72,96,144}) {
        std::vector<unsigned char> expected,actual;
        CHECK(render(original_pdf(cases[i].original,cases[i].image),
                     dpi,expected)==EXIT_SUCCESS);
        CHECK(render(original_pdf(cases[i].with_n,cases[i].image),
                     dpi,actual)==EXIT_SUCCESS);
        CHECK(expected.size()==actual.size() &&
              std::memcmp(expected.data(),actual.data(),
                          expected.size())==0);
        bool white=true,ink=false;
        for(size_t pos=0u;pos<actual.size();pos+=3u) {
            const bool pixel_white=actual[pos]==255u &&
                actual[pos+1u]==255u && actual[pos+2u]==255u;
            white=white && pixel_white;
            ink=ink || !pixel_white;
        }
        CHECK(cases[i].blank ? white : ink);
    }
    return EXIT_SUCCESS;
}

static int negative()
{
    /* n never silently drops operands, late Text/Stroke, or an
     * unbalanced q/Q; every invalid original is refused atomically. */
    const char *const invalid[]={
        "1 n",
        "4 4 8 6 re n BT ET",
        "4 4 8 6 re n 4 4 8 6 re S",
        "4 4 8 6 re n Q",
        "4 4 8 6 re n 1 n",
        "4 4 8 6 re n /Missing Do"
    };
    for(const char *operators:invalid)
    for(int dpi:{72,96,144}) {
        CHECK(write_pdf(original_pdf(operators,false)));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *out=
            reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_open(filename,nullptr,&doc));
        CHECK(std::remove(filename)==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options options{};
        options.struct_size=sizeof(options);
        options.dpi=(float)dpi;
        const auto status=quantapdf_render_page_with_options(
            page,&options,&out);
        CHECK(status!=QUANTAPDF_OK && out==nullptr);
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
