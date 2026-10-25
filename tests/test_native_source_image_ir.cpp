/* Independent original-PDFium vs detached Native C11 Image Do admission.
 * qpdf only authors/reads the ORIGINAL test PDF and resolves structural
 * source resources; it is not a raster fallback or pixel oracle. */
#include "backend/qpdf_image_ir.h"
#include "render/ir_raster_image_v1.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <fpdfview.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define CHECK(e) do { if (!(e)) { \
    std::fprintf(stderr, "%s:%d: native Image Do: %s\n", \
        __FILE__, __LINE__, #e); std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

namespace {
using Obj = QPDFObjectHandle;
constexpr size_t WIDTH = 64u, HEIGHT = 48u;
constexpr quantapdf_qpdf_source_image_ir_limits SOURCE_LIMITS{
    4u, WIDTH * HEIGHT, 64u, 8u
};
constexpr quantapdf_ir_raster_image_v1_limits RASTER_LIMITS{
    WIDTH * HEIGHT, 4u, 64u, WIDTH * HEIGHT * 4u
};

Obj make_image(QPDF& pdf, const std::string& bytes,
               int width, int height, const char *color)
{
    Obj item = pdf.newStream(bytes);
    Obj dictionary = item.getDict();
    dictionary.replaceKey("/Type", Obj::newName("/XObject"));
    dictionary.replaceKey("/Subtype", Obj::newName("/Image"));
    dictionary.replaceKey("/Width", Obj::newInteger(width));
    dictionary.replaceKey("/Height", Obj::newInteger(height));
    dictionary.replaceKey("/BitsPerComponent", Obj::newInteger(8));
    dictionary.replaceKey("/ColorSpace", Obj::newName(color));
    return item;
}

std::vector<unsigned char> original_pdf(
    const char *source_ops, bool split_contents,
    const char *extra_resource = nullptr,
    const char *page_key = nullptr, bool softmask = false,
    bool optional_content = false, bool external_file = false)
{
    QPDF pdf;
    pdf.emptyPDF();
    Obj page = pdf.makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 64 48] >>"));
    pdf.addPage(page, false);
    Obj group = Obj::newDictionary();
    const std::string rgb = {
        static_cast<char>(255),0,0, 0,static_cast<char>(255),0,
        0,0,static_cast<char>(255),
        static_cast<char>(255),static_cast<char>(255),0
    };
    Obj color = make_image(pdf,rgb,2,2,"/DeviceRGB");
    if (softmask) color.getDict().replaceKey(
        "/SMask",make_image(pdf,std::string(1,'\x80'),1,1,"/DeviceGray"));
    if (optional_content)
        color.getDict().replaceKey("/OC",pdf.makeIndirectObject(
            Obj::parse("<< /Type /OCG /Name (Hidden) >>")));
    if (external_file)
        color.getDict().replaceKey("/F",Obj::newString("external-image.dat"));
    group.replaceKey("/Im0",color);
    Obj gray = make_image(pdf,std::string({0,static_cast<char>(255)}),
                          2,1,"/DeviceGray");
    group.replaceKey("/Im1",gray);
    if (extra_resource)
        group.replaceKey("/Fm",pdf.newStream(std::string("q Q")));
    Obj res = Obj::newDictionary();
    res.replaceKey("/XObject",group);
    page.replaceKey("/Resources",res);
    if (page_key) {
        if (std::strcmp(page_key,"/Annots")==0)
            page.replaceKey("/Annots",Obj::parse("[<< /Subtype /Text >>]"));
        else if (std::strcmp(page_key,"/Group")==0)
            page.replaceKey("/Group",Obj::parse("<< /S /Transparency >>"));
    }
    if (split_contents) {
        const std::string op(source_ops);
        auto mid = op.find("/Im0");
        CHECK(mid != std::string::npos);
        page.replaceKey("/Contents",Obj::newArray({
            pdf.newStream(op.substr(0,mid+4)),
            pdf.newStream(op.substr(mid+4))
        }));
    } else {
        page.replaceKey("/Contents",pdf.newStream(source_ops));
    }
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.setDeterministicID(true);
    writer.write();
    auto buffer = std::unique_ptr<Buffer>(writer.getBuffer());
    CHECK(buffer != nullptr && buffer->getSize() > 0u);
    const unsigned char *ptr = buffer->getBuffer();
    CHECK(ptr != nullptr);
    std::vector<unsigned char> bytes(ptr,ptr+buffer->getSize());
    FILE *f = nullptr;
#if defined(_MSC_VER)
    CHECK(fopen_s(&f, NATIVE_SOURCE_IMAGE_PDF, "wb") == 0);
#else
    f = std::fopen(NATIVE_SOURCE_IMAGE_PDF, "wb");
#endif
    CHECK(f != nullptr);
    CHECK(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size());
    CHECK(std::fclose(f)==0);
    return bytes;
}

quantapdf_status owned_image(const std::vector<unsigned char>& bytes,
                            quantapdf_ir *out,
                            const quantapdf_qpdf_source_image_ir_limits& limits =
                                SOURCE_LIMITS)
{
    quantapdf_qpdf_document *source = nullptr;
    OK(quantapdf_qpdf_open_memory(bytes.data(),bytes.size(),nullptr,&source));
    CHECK(source != nullptr);
    const quantapdf_status code =
        quantapdf_qpdf_source_image_ir(source,0,&limits,out);
    quantapdf_qpdf_close(source);
    return code; /* qpdf graph and source owner no longer exist. */
}

std::vector<unsigned char> reference_pdfium()
{
    FPDF_InitLibrary();
    FPDF_DOCUMENT doc = FPDF_LoadDocument(NATIVE_SOURCE_IMAGE_PDF,nullptr);
    CHECK(doc != nullptr && FPDF_GetPageCount(doc)==1);
    FPDF_PAGE page = FPDF_LoadPage(doc,0);
    CHECK(page != nullptr);
    CHECK(static_cast<int>(FPDF_GetPageWidthF(page)) == static_cast<int>(WIDTH));
    CHECK(static_cast<int>(FPDF_GetPageHeightF(page)) == static_cast<int>(HEIGHT));
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(
        static_cast<int>(WIDTH),static_cast<int>(HEIGHT),
        FPDFBitmap_BGR,nullptr,0);
    CHECK(bitmap != nullptr);
    FPDFBitmap_FillRect(bitmap,0,0,
        static_cast<int>(WIDTH),static_cast<int>(HEIGHT),0xffffffffu);
    FPDF_RenderPageBitmap(bitmap,page,0,0,
        static_cast<int>(WIDTH),static_cast<int>(HEIGHT),0,FPDF_ANNOT);
    const auto *src = static_cast<const unsigned char *>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    CHECK(src != nullptr && stride >= static_cast<int>(WIDTH)*3);
    std::vector<unsigned char> rgb(WIDTH*HEIGHT*3u);
    for(size_t y=0u;y<HEIGHT;++y)for(size_t x=0u;x<WIDTH;++x) {
        size_t pos = (y*WIDTH+x)*3u;
        rgb[pos] = src[y*static_cast<size_t>(stride)+3u*x+2u];
        rgb[pos+1u] = src[y*static_cast<size_t>(stride)+3u*x+1u];
        rgb[pos+2u] = src[y*static_cast<size_t>(stride)+3u*x];
    }
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();
    return rgb;
}

void positive_original(const char *content, bool split, bool repeated = false)
{
    const auto original=original_pdf(content,split);
    quantapdf_ir ir{};
    const quantapdf_status lower = owned_image(original,&ir);
    if (lower != QUANTAPDF_OK)
        std::fprintf(stderr,"[source-image-do] lowering rejected status=%d\n",
            static_cast<int>(lower));
    CHECK(lower == QUANTAPDF_OK);
    CHECK(ir.pages!=nullptr && ir.page_count==1u);
    CHECK(ir.operation_count==(repeated ? 3u : 2u) &&
          ir.resources.image_count==ir.operation_count);
    quantapdf_ir_raster_image native{}, repeat{};
    OK(quantapdf_ir_raster_raw_images_v1(&ir,0,&RASTER_LIMITS,&native));
    OK(quantapdf_ir_raster_raw_images_v1(&ir,0,&RASTER_LIMITS,&repeat));
    CHECK(native.pixels && native.width==WIDTH && native.height==HEIGHT &&
          native.size==WIDTH*HEIGHT*4u &&
          native.size==repeat.size &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages==nullptr && ir.operations==nullptr &&
          ir.resources.images==nullptr);
    auto reference=reference_pdfium();
    size_t gross=0u;
    uint64_t total=0u;
    for(size_t y=0;y<HEIGHT;++y)for(size_t x=0;x<WIDTH;++x){
        size_t idx=y*WIDTH+x,off=idx*3u;
        unsigned delta_max=0u;
        for(size_t c=0u;c<3u;++c) {
            unsigned lhs=native.pixels[idx*4u+c],rhs=reference[off+c];
            unsigned delta=lhs>rhs?lhs-rhs:rhs-lhs;
            if(delta>delta_max)delta_max=delta;
            total+=delta;
        }
        if(delta_max>64u)++gross;
    }
    const double mae=static_cast<double>(total)/(WIDTH*HEIGHT*3u);
    std::fprintf(stderr,"[source-image-do] MAE=%.4f gross=%zu\n",mae,gross);
    CHECK(mae<=4.0 && gross<=WIDTH*HEIGHT/30u);
    auto sample=[&](size_t x,size_t y,unsigned r,unsigned g,unsigned b){
        size_t idx=y*WIDTH+x;
        const unsigned want[]={r,g,b};
        for(size_t c=0;c<3;++c) {
            CHECK(std::abs(int(native.pixels[idx*4u+c])-int(want[c]))<=3);
            CHECK(std::abs(int(reference[idx*3u+c])-int(want[c]))<=3);
        }
    };
    sample(0,0,255,255,255);
    sample(6,9,255,0,0);
    sample(20,9,0,255,0);
    sample(6,25,0,0,255);
    sample(20,25,255,255,0);
    sample(32,31,0,0,0);
    sample(42,31,255,255,255);
    if(repeated) sample(23,26,255,0,0); /* Repeat Do painted over yellow. */
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&repeat);
}

void rejected(const char *content, bool split=false,
              const char *key=nullptr,bool mask=false,
              bool optional_content=false,bool external_file=false)
{
    const auto original=original_pdf(
        content,split,nullptr,key,mask,optional_content,external_file);
    for(int trial=0;trial<2;++trial) {
        quantapdf_ir failed;
        std::memset(&failed,0xa5,sizeof(failed));
        const auto status=owned_image(original,&failed);
        CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED ||
              status==QUANTAPDF_ERROR_FORMAT);
        CHECK(failed.pages==nullptr && failed.page_count==0u &&
              failed.operations==nullptr && failed.operation_count==0u &&
              failed.resources.images==nullptr &&
              failed.resources.image_count==0u);
        quantapdf_ir_dispose(&failed);
    }
}

}

int main()
{
    const char *supported =
        "q 20 0 0 20 4 20 cm /Im0 Do Q "
        "q 16 0 0 8 30 10 cm /Im1 Do Q";
    positive_original(supported,false);
    positive_original(supported,true);
    const char *repeated =
        "q 20 0 0 20 4 20 cm /Im0 Do Q "
        "q 16 0 0 8 30 10 cm /Im1 Do Q "
        "q 8 0 0 8 22 15 cm /Im0 Do Q";
    positive_original(repeated,true,true);
    const char *negative[] = {
        "q 20 0 0 20 4 20 cm /Im0 Do Q 0 0 1 rg",
        "q 20 0 0 20 4 20 cm /Im0 Do Q BT ET",
        "q 20 0 0 20 4 20 cm /Im0 Do Q /Fm Do",
        "q 20 0 0 20 4 20 cm /Im0 Do Q /Missing Do",
        "q 20 1 0 20 4 20 cm /Im0 Do Q",
        "q 20 0 0 20 4.5 20 cm /Im0 Do Q",
        "q 20 0 0 20 50 20 cm /Im0 Do Q",
        "q 0 0 0 0 4 20 cm /Im0 Do Q",
        "q 20 0 0 20 4 20 cm /Im0 Do",
        "q 20 0 0 20 4 20 cm /Im0 Do Q BI /W 1 /H 1 /BPC 8 /CS /RGB ID x EI"
    };
    for(const char *ops:negative)rejected(ops);
    rejected(supported,false,"/Group");
    rejected(supported,false,"/Annots");
    rejected(supported,false,nullptr,true);
    /* Optional-content and external image streams can change the visible
     * PDF pixels. Strict source painting must not omit their semantics. */
    rejected(supported,false,nullptr,false,true);
    rejected(supported,false,nullptr,false,false,true);
    /* A selected original must fail fully on caller resource/pixel limits,
     * even when all original PDF commands are independently supported. */
    {
        const auto original = original_pdf(supported,false);
        const auto bad_limit = [&](quantapdf_qpdf_source_image_ir_limits limits,
                                   quantapdf_status expect) {
            quantapdf_ir refused;
            std::memset(&refused,0xa5,sizeof(refused));
            const quantapdf_status actual=owned_image(original,&refused,limits);
            CHECK(actual==expect && refused.pages==nullptr &&
                  refused.operations==nullptr && refused.resources.images==nullptr &&
                  refused.operation_count==0u && refused.page_count==0u);
        };
        auto cap=SOURCE_LIMITS;
        cap.max_operations=1u;
        bad_limit(cap,QUANTAPDF_ERROR_UNSUPPORTED);
        cap=SOURCE_LIMITS;
        cap.max_source_pixels=5u;
        bad_limit(cap,QUANTAPDF_ERROR_UNSUPPORTED);
        cap=SOURCE_LIMITS;
        cap.max_page_pixels=WIDTH*HEIGHT-1u;
        bad_limit(cap,QUANTAPDF_ERROR_UNSUPPORTED);
        cap=SOURCE_LIMITS;
        cap.max_parent_steps=0u;
        bad_limit(cap,QUANTAPDF_ERROR_ARGUMENT);
    }
    // A successful source after failures cannot inherit stale state.
    positive_original(supported,true);
    CHECK(std::remove(NATIVE_SOURCE_IMAGE_PDF)==0);
    std::puts("Native Image Do -> owned RAW IR -> C11 Raster source admission PASS");
    return EXIT_SUCCESS;
}
