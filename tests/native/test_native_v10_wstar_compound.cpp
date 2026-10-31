/* Original test-authored PDF W/W* two-contour winding admission.
 * No original yWorks source is modified or claimed pixel-qualified.
 * This is one narrow public Source V10 => unique C11 V5/V1 raster
 * capability, not a PDFium oracle or a second Clip implementation.
 */
#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(v) do { if(!(v)) { \
    std::fprintf(stderr,"native two-contour Wstar:%d: %s\n", \
                 __LINE__,#v); return EXIT_FAILURE; } } while(0)
#define OK(v) CHECK((v)==QUANTAPDF_OK)
using Obj=QPDFObjectHandle;
static constexpr const char* temp_file="native-v10-wstar-two-contour.pdf";

/* Both triangles wind the same way. The second lies entirely inside
 * the first. W* cuts its interior; W fills it. Original page point Y
 * is projected by the Source to inverted device Y. */
static const std::string outer=
    "4 4 m 28 4 l 16 28 l h ";
static const std::string inner=
    "12 10 m 20 10 l 16 18 l h ";
static const std::string inner_reverse=
    "12 10 m 16 18 l 20 10 l h ";
static const std::string third=
    "8 8 m 12 8 l 10 12 l h ";
static const std::string fill_red=
    "1 0 0 rg 0 0 32 32 re f ";
static const std::string fill_blue=
    "0 0 1 rg 0 0 32 32 re f ";

static std::string authored(const std::string& contents)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    root.replaceKey("/MediaBox",Obj::parse("[0 0 32 32]"));
    auto page=pdf->makeIndirectObject(Obj::parse("<< /Type /Page >>"));
    page.replaceKey("/Parent",root);
    page.replaceKey("/Contents",pdf->newStream(contents));
    page.replaceKey("/Resources",Obj::newDictionary());
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(bytes->getBuffer()),
        bytes->getSize());
}
static bool write_file(const std::string& contents)
{
    const std::string pdf=authored(contents);
    FILE* file=nullptr;
#ifdef _WIN32
    if(fopen_s(&file,temp_file,"wb")!=0)return false;
#else
    file=std::fopen(temp_file,"wb");
#endif
    if(file==nullptr)return false;
    const bool ok=std::fwrite(pdf.data(),1u,pdf.size(),file)==pdf.size();
    return std::fclose(file)==0 && ok;
}

struct rgb_image {
    int w=0,h=0;
    std::vector<unsigned char> rgb;
};
static uint32_t color(const rgb_image& image,int x,int y)
{
    const size_t pos=(static_cast<size_t>(y)*
       static_cast<size_t>(image.w)+static_cast<size_t>(x))*3u;
    return (static_cast<uint32_t>(image.rgb.at(pos))<<16u)|
           (static_cast<uint32_t>(image.rgb.at(pos+1u))<<8u)|
            static_cast<uint32_t>(image.rgb.at(pos+2u));
}

struct case_entry {
    const char* name;
    std::string contents;
    quantapdf_status expected;
    uint32_t center;
    uint32_t outer_point;
    bool blue;
};
static std::vector<case_entry> make_cases()
{
    const std::string two=outer+inner;
    return {
        {"even-odd-hole-red",two+"W* n "+fill_red,QUANTAPDF_OK,
            UINT32_C(0xffffff),UINT32_C(0xff0000),false},
        {"nonzero-same-winding",two+"W n "+fill_red,QUANTAPDF_OK,
            UINT32_C(0xff0000),UINT32_C(0xff0000),false},
        {"nonzero-reversed-inner",outer+inner_reverse+"W n "+fill_red,
            QUANTAPDF_OK,UINT32_C(0xffffff),UINT32_C(0xff0000),false},
        {"evenodd-hole-blue",two+"W* n "+fill_blue,QUANTAPDF_OK,
            UINT32_C(0xffffff),UINT32_C(0x0000ff),true},
        {"evenodd-one-contour",outer+"W* n "+fill_red,QUANTAPDF_OK,
            UINT32_C(0xff0000),UINT32_C(0xff0000),false},
        {"evenodd-hole-repeat",two+"W* n "+fill_red,QUANTAPDF_OK,
            UINT32_C(0xffffff),UINT32_C(0xff0000),false},
        {"reject-third-contour",outer+inner+third+"W* n "+fill_red,
            QUANTAPDF_ERROR_UNSUPPORTED,0u,0u,false},
        {"reject-late-text",two+"W* n "+fill_red+"BT ET ",
            QUANTAPDF_ERROR_UNSUPPORTED,0u,0u,false},
        {"reject-early-text",two+"W* n BT ET "+fill_red,
            QUANTAPDF_ERROR_UNSUPPORTED,0u,0u,false},
        {"reject-late-stroke",two+"W* n "+fill_red+
            "4 4 m 28 4 l 16 28 l s ",
            QUANTAPDF_ERROR_UNSUPPORTED,0u,0u,false},
        {"reject-third-clip",two+"W* n "+fill_red+
            "0 0 32 32 re W n ",
            QUANTAPDF_ERROR_UNSUPPORTED,0u,0u,false},
        {"reject-missing-gs",two+"W* n /NotThere gs "+fill_red,
            QUANTAPDF_ERROR_UNSUPPORTED,0u,0u,false},
        {"reject-shading",two+"W* n /Missing sh "+fill_red,
            QUANTAPDF_ERROR_UNSUPPORTED,0u,0u,false}
    };
}
static int test()
{
    const auto cases=make_cases();
    quantapdf_context* ctx=nullptr;
    quantapdf_bitmap* held=nullptr;
    std::vector<unsigned char> held_copy;
    rgb_image first{};
    size_t positives=0u,negatives=0u;
    OK(quantapdf_context_create(&ctx));
    for(size_t i=0u;i<cases.size();++i) {
        const auto& entry=cases[i];
        CHECK(write_file(entry.contents));
        quantapdf_document* doc=nullptr;
        quantapdf_page* page=nullptr;
        quantapdf_bitmap* bitmap=
            reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_open_with_context(ctx,temp_file,nullptr,&doc));
        CHECK(doc!=nullptr);
        CHECK(std::remove(temp_file)==0);
        int page_count=0;
        OK(quantapdf_page_count(doc,&page_count));
        CHECK(page_count==1);
        OK(quantapdf_load_page(doc,0,&page));
        CHECK(page!=nullptr);
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);
        opts.dpi=72.0f;
        const quantapdf_status status=
            quantapdf_render_page_with_options(page,&opts,&bitmap);
        if(status!=entry.expected ||
           (status==QUANTAPDF_OK?bitmap==nullptr:bitmap!=nullptr)) {
            std::fprintf(stderr,
                "two-contour Source case=%s status=%d expected=%d "
                "bitmap=%p\n",entry.name,static_cast<int>(status),
                static_cast<int>(entry.expected),
                static_cast<void*>(bitmap));
            CHECK(false);
        }
        if(status==QUANTAPDF_OK) {
            rgb_image result;
            const unsigned char* bytes=nullptr;
            size_t count=0u;
            int stride=0,channels=0;
            OK(quantapdf_bitmap_dimensions(bitmap,&result.w,
                    &result.h,&stride,&channels));
            OK(quantapdf_bitmap_data(bitmap,&bytes,&count));
            CHECK(result.w==32 && result.h==32 && channels==3 &&
                  stride==result.w*3 && bytes!=nullptr &&
                  count==static_cast<size_t>(stride)*
                         static_cast<size_t>(result.h));
            result.rgb.assign(bytes,bytes+count);
            if(color(result,2,2)!=UINT32_C(0xffffff) ||
               color(result,16,18)!=entry.center ||
               color(result,16,8)!=entry.outer_point ||
               color(result,30,30)!=UINT32_C(0xffffff)) {
                std::fprintf(stderr,
                    "two-contour pixels case=%s outside=%06x "
                    "center=%06x expected=%06x lobe=%06x expected=%06x\n",
                    entry.name,
                    static_cast<unsigned>(color(result,2,2)),
                    static_cast<unsigned>(color(result,16,18)),
                    static_cast<unsigned>(entry.center),
                    static_cast<unsigned>(color(result,16,8)),
                    static_cast<unsigned>(entry.outer_point));
                CHECK(false);
            }
            if(held==nullptr) {
                held=bitmap;
                held_copy=result.rgb;
                first=result;
            } else {
                if(i==5u)CHECK(result.rgb==first.rgb);
                quantapdf_drop_bitmap(bitmap);
            }
            ++positives;
        } else ++negatives;
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        if(held!=nullptr) {
            const unsigned char* data=nullptr;
            size_t size=0u;
            OK(quantapdf_bitmap_data(held,&data,&size));
            CHECK(data!=nullptr && size==held_copy.size() &&
                  std::memcmp(data,held_copy.data(),size)==0);
        }
    }
    quantapdf_drop_context(ctx);
    CHECK(held!=nullptr);
    const unsigned char* bytes=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_data(held,&bytes,&size));
    CHECK(bytes!=nullptr && size==held_copy.size() &&
          std::memcmp(bytes,held_copy.data(),size)==0);
    quantapdf_drop_bitmap(held);
    CHECK(positives==6u && negatives==7u);
    std::printf("Original generated W/W* two-contour: 6 exact RGB "
                "positives, 7 atomic negatives, single C11 renderer\n");
    return EXIT_SUCCESS;
}
int main()
{
    return test();
}
