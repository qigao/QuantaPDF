/* The unchanged upstream 514-byte original W/n + closed blue
 * one-pixel triangle Stroke, finally through PUBLIC Source V10.
 * C11 V1's existing clip-guarded segment sampler is the only painter.
 * No PDFium oracle, graph fallback, second raster or widened quota. */
#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"native original clipped Stroke:%d: %s\n", \
                 __LINE__,#e);return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static constexpr const char* temporary="native-v10-clipped-stroke-input.pdf";
static const std::string rect_clip=
    "10 15 m 40 15 l 40 35 l 10 35 l W n ";
static const std::string blue_triangle=
    "0 0 1 RG 10 10 m 25 40 l 40 10 l s ";
static const std::string original_ops=rect_clip+blue_triangle;

struct rendered {
    int w=0,h=0;
    std::vector<unsigned char> rgb;
};
static bool write_pdf(const std::string& bytes)
{
    FILE* f=nullptr;
#ifdef _WIN32
    if(fopen_s(&f,temporary,"wb")!=0)return false;
#else
    f=std::fopen(temporary,"wb");
#endif
    if(f==nullptr)return false;
    const bool ok=std::fwrite(bytes.data(),1u,bytes.size(),f)==bytes.size();
    return std::fclose(f)==0 && ok;
}
static std::string authored(const std::string& content,
                            bool scaled_userunit=false)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 100 50] >>"));
    page.replaceKey("/Parent",root);
    page.replaceKey("/Contents",pdf->newStream(content));
    if(scaled_userunit)
        page.replaceKey("/UserUnit",Obj::parse("1.25"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    root.replaceKey("/Resources",Obj::newDictionary());
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(blob->getBuffer()),
                       blob->getSize());
}

static uint32_t pixel(const rendered& img,int x,int y)
{
    const size_t i=(static_cast<size_t>(y)*static_cast<size_t>(img.w)+
                    static_cast<size_t>(x))*3u;
    return (static_cast<uint32_t>(img.rgb.at(i))<<16u)|
           (static_cast<uint32_t>(img.rgb.at(i+1u))<<8u)|
            static_cast<uint32_t>(img.rgb.at(i+2u));
}
static bool blue_on_white(const rendered& out)
{
    if(out.w!=100 || out.h!=50 || out.rgb.size()!=15000u ||
       pixel(out,0,0)!=UINT32_C(0xffffff) ||
       pixel(out,25,25)!=UINT32_C(0xffffff) ||
       pixel(out,99,49)!=UINT32_C(0xffffff))return false;
    size_t blue=0u,white=0u,other=0u;
    for(size_t i=0u;i<out.rgb.size();i+=3u) {
        const unsigned r=out.rgb[i],g=out.rgb[i+1u],b=out.rgb[i+2u];
        if(b!=255u || r!=g)return false;
        if(r<240u)++blue;
        else if(r==255u)++white;
        else ++other;
    }
    /* The original Clip contains two segments of the triangle but
     * not its authored vertices/joins. Interior must show real blue
     * pixels, while white outside the Clip stays untouched. */
    return blue>=15u && blue<400u && white>4000u &&
           blue+white+other==5000u;
}

static int check(const std::filesystem::path& assets)
{
    quantapdf_context* context=nullptr;
    quantapdf_bitmap* retained=nullptr;
    std::vector<unsigned char> copy;
    rendered golden{};
    OK(quantapdf_context_create(&context));
    const std::string original=(assets/"pdfium.clip-path-v1.pdf").string();
    size_t accepted=0u,refused=0u;

    struct sample {
        const char* id;
        const char* source;
        quantapdf_status expected;
    };
    const sample tests[]={
        {"external-original",nullptr,QUANTAPDF_OK},
        {"same-blue-triangle",nullptr,QUANTAPDF_OK}, /* special authored below */
        {"stroke-width-2", "2 w ",QUANTAPDF_ERROR_UNSUPPORTED},
        {"round-cap","1 J ",QUANTAPDF_ERROR_UNSUPPORTED},
        {"bevel-join","2 j ",QUANTAPDF_ERROR_UNSUPPORTED},
        {"nondefault-miter","11 M ",QUANTAPDF_ERROR_UNSUPPORTED},
        {"dash","[2 2] 0 d ",QUANTAPDF_ERROR_UNSUPPORTED},
        {"gray-stroke","0.5 G ",QUANTAPDF_ERROR_UNSUPPORTED},
        {"cmyk-stroke","0 0 0 1 K ",QUANTAPDF_ERROR_UNSUPPORTED},
        {"q-scoped-stroke","q ",QUANTAPDF_ERROR_UNSUPPORTED},
        {"late-text","",QUANTAPDF_ERROR_UNSUPPORTED}, /* special */
        {"late-stroke","",QUANTAPDF_ERROR_UNSUPPORTED}, /* special */
        {"no-clip","",QUANTAPDF_ERROR_UNSUPPORTED}, /* special */
        {"open-stroke","",QUANTAPDF_ERROR_UNSUPPORTED}, /* special */
        {"unsafe-clip-near-joins","",QUANTAPDF_ERROR_UNSUPPORTED},
        {"scaled-centerline-width","",QUANTAPDF_ERROR_UNSUPPORTED},
        {"scaled-original-UserUnit","",QUANTAPDF_ERROR_UNSUPPORTED},
        {"nonisometric-96-dpi","",QUANTAPDF_ERROR_UNSUPPORTED}
    };

    for(size_t case_index=0u;case_index<sizeof(tests)/sizeof(tests[0]);
        ++case_index) {
        const auto& test=tests[case_index];
        std::string contents;
        const bool exact_external=case_index==0u;
        if(!exact_external) {
            if(case_index==1u)contents=original_ops;
            else if(case_index==7u || case_index==8u)
                contents=rect_clip+"0 0 1 RG "+test.source+
                         "10 10 m 25 40 l 40 10 l s ";
            else if(case_index==9u)
                contents=rect_clip+"q "+blue_triangle+"Q ";
            else if(case_index==10u)
                contents=original_ops+"BT ET";
            else if(case_index==11u)
                contents=original_ops+"10 10 m 25 40 l 40 10 l s";
            else if(case_index==12u)
                contents=blue_triangle;
            else if(case_index==13u)
                contents=rect_clip+"0 0 1 RG 10 10 m 25 40 l 40 10 l S";
            else if(case_index==14u)
                contents="5 5 m 45 5 l 45 45 l 5 45 l W n "+blue_triangle;
            else if(case_index==15u)
                contents=rect_clip+"0.5 0 0 0.5 0 0 cm "+blue_triangle;
            else if(case_index==16u || case_index==17u)
                contents=original_ops;
            else
                contents=rect_clip+test.source+blue_triangle;
            CHECK(write_pdf(authored(contents,case_index==16u)));
        }
        const std::string filename=exact_external?original:
            std::string(temporary);
        quantapdf_document* doc=nullptr;
        quantapdf_page* page=nullptr;
        quantapdf_bitmap* bitmap=
            reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_open_with_context(
            context,filename.c_str(),nullptr,&doc));
        CHECK(doc!=nullptr);
        if(!exact_external)CHECK(std::remove(temporary)==0);
        OK(quantapdf_load_page(doc,0,&page));
        CHECK(page!=nullptr);
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);
        opts.dpi=case_index==17u?96.0f:72.0f;
        const auto status=quantapdf_render_page_with_options(
            page,&opts,&bitmap);
        if(status!=test.expected ||
           (status==QUANTAPDF_OK?bitmap==nullptr:bitmap!=nullptr)) {
            std::fprintf(stderr,
                "native original stroke case=%s got=%d expected=%d bitmap=%p\n",
                test.id,static_cast<int>(status),
                static_cast<int>(test.expected),
                static_cast<void*>(bitmap));
            CHECK(false);
        }
        if(status==QUANTAPDF_OK) {
            rendered actual;
            const unsigned char* bytes=nullptr;
            size_t bytes_count=0u;
            int stride=0,channels=0;
            OK(quantapdf_bitmap_dimensions(bitmap,
                &actual.w,&actual.h,&stride,&channels));
            OK(quantapdf_bitmap_data(bitmap,&bytes,&bytes_count));
            CHECK(stride==actual.w*3 && channels==3 &&
                  bytes!=nullptr &&
                  bytes_count==static_cast<size_t>(stride)*
                               static_cast<size_t>(actual.h));
            actual.rgb.assign(bytes,bytes+bytes_count);
            if(!blue_on_white(actual)){
                std::fprintf(stderr,"native original stroke RGB rejected: "
                    "source=%s size=%dx%d sample(17,25)=%06x\n",
                    test.id,actual.w,actual.h,
                    static_cast<unsigned>(pixel(actual,17,25)));
                CHECK(false);
            }
            if(retained==nullptr) {
                retained=bitmap;
                copy=actual.rgb;
                golden=actual;
            } else {
                CHECK(golden.w==actual.w && golden.h==actual.h &&
                      golden.rgb==actual.rgb);
                quantapdf_drop_bitmap(bitmap);
            }
            ++accepted;
        } else ++refused;
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        if(retained!=nullptr){
            const unsigned char* still=nullptr;
            size_t size=0u;
            OK(quantapdf_bitmap_data(retained,&still,&size));
            CHECK(still!=nullptr && size==copy.size() &&
                  std::memcmp(still,copy.data(),size)==0);
        }
    }
    quantapdf_drop_context(context);
    CHECK(retained!=nullptr);
    const unsigned char* still=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_data(retained,&still,&size));
    CHECK(still!=nullptr && size==copy.size() &&
          std::memcmp(still,copy.data(),size)==0);
    quantapdf_drop_bitmap(retained);
    CHECK(accepted==2u && refused==16u);
    std::printf("Original public V10 clipped Stroke: 2 100x50 blue on white "
                "positives, 16 exact atomic source/C11 refusals\n");
    return EXIT_SUCCESS;
}
int main(int argc,char** argv)
{
    if(argc!=2 || !argv[1])return EXIT_FAILURE;
    return check(std::filesystem::path(argv[1]));
}
