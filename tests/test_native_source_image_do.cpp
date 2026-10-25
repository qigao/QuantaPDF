/* Original PDF -> strict native Image Do IR -> detached pure-C11 Raster.
 * Original, unchanged PDF bytes are rendered independently by test-only
 * PDFium, not by exporting or normalizing Native IR back into PDF.
 * The production public page renderer remains PDFium-backed. */
#include "backend/qpdf_native_image_ir.h"
#include "render/ir_raster_image_v1.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"%s:%d: source image Do: %s\n",__FILE__,__LINE__,#e); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

namespace {
using Obj=QPDFObjectHandle;
constexpr int W=32,H=24;
const quantapdf_qpdf_source_raw_image_limits source_limits={
    1048576u,16384u,3u,4u,4096u,4096u,4096u
};
const quantapdf_ir_raster_image_v1_limits raster_limits={
    4096u,4u,4096u,4096u
};

struct scene {
    std::string content_a;
    std::string content_b;
    bool split=false;
    bool remove_resources=false;
    bool wrong_subtype=false;
    bool soft_mask=false;
    bool wrong_color=false;
};

std::vector<unsigned char> make_original(const scene& sc)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    Obj pages=pdf->getRoot().getKey("/Pages");
    Obj page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24] >>"));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",pages);

    Obj resources=Obj::newDictionary(),objects=Obj::newDictionary();
    const std::string first(
        "\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    Obj img=pdf->newStream(first);
    img.getDict().replaceKey("/Type",Obj::newName("/XObject"));
    img.getDict().replaceKey("/Subtype",Obj::newName(
        sc.wrong_subtype?"/Form":"/Image"));
    img.getDict().replaceKey("/Width",Obj::newInteger(2));
    img.getDict().replaceKey("/Height",Obj::newInteger(2));
    img.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    img.getDict().replaceKey("/ColorSpace",Obj::newName(
        sc.wrong_color?"/DeviceCMYK":"/DeviceRGB"));
    if(sc.soft_mask)
        img.getDict().replaceKey("/SMask",Obj::newName("/None"));
    objects.replaceKey("/I",img);

    const std::string second("\0\0\xff",3);
    Obj blue=pdf->newStream(second);
    blue.getDict().replaceKey("/Type",Obj::newName("/XObject"));
    blue.getDict().replaceKey("/Subtype",Obj::newName("/Image"));
    blue.getDict().replaceKey("/Width",Obj::newInteger(1));
    blue.getDict().replaceKey("/Height",Obj::newInteger(1));
    blue.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    blue.getDict().replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
    objects.replaceKey("/J",blue);
    resources.replaceKey("/XObject",objects);
    if(!sc.remove_resources) page.replaceKey("/Resources",resources);

    if(sc.split)
        page.replaceKey("/Contents",Obj::newArray({
            pdf->newStream(sc.content_a),pdf->newStream(sc.content_b)}));
    else
        page.replaceKey("/Contents",pdf->newStream(sc.content_a));

    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    auto buffer=writer.getBufferSharedPointer();
    return std::vector<unsigned char>(
        buffer->getBuffer(),buffer->getBuffer()+buffer->getSize());
}

std::vector<unsigned char> direct_pdfium(const std::vector<unsigned char>& original)
{
    FPDF_InitLibrary();
    FPDF_DOCUMENT pdf=FPDF_LoadMemDocument(original.data(),
        static_cast<int>(original.size()),nullptr);
    if(!pdf)return {};
    FPDF_PAGE page=FPDF_LoadPage(pdf,0);
    if(!page) {
        FPDF_CloseDocument(pdf);
        return {};
    }
    FPDF_BITMAP bitmap=FPDFBitmap_CreateEx(W,H,FPDFBitmap_BGR,nullptr,0);
    if(!bitmap) {
        FPDF_ClosePage(page);FPDF_CloseDocument(pdf);return {};
    }
    FPDFBitmap_FillRect(bitmap,0,0,W,H,0xffffffffu);
    FPDF_RenderPageBitmap(bitmap,page,0,0,W,H,0,0);
    const unsigned char *buffer=static_cast<const unsigned char *>(
        FPDFBitmap_GetBuffer(bitmap));
    const int stride=FPDFBitmap_GetStride(bitmap);
    std::vector<unsigned char> result(static_cast<size_t>(W*H*3));
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        const unsigned char *bgr=buffer+static_cast<size_t>(y)*stride+x*3;
        unsigned char *rgb=result.data()+static_cast<size_t>(y*W+x)*3;
        rgb[0]=bgr[2];rgb[1]=bgr[1];rgb[2]=bgr[0];
    }
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(pdf);
    FPDF_DestroyLibrary();
    return result;
}

bool matches(const unsigned char *a,const unsigned char *b,int tolerance)
{
    for(int i=0;i<3;++i)
        if(std::abs(int(a[i])-int(b[i]))>tolerance)return false;
    return true;
}

int test_original_pixel_parity()
{
    scene sc;
    sc.split=true;
    sc.content_a="q 16 0 0 12 2 3 cm /I";
    sc.content_b=" Do Q q 4 0 0 4 22 8 cm /J Do Q "
                 "q 8 0 0 8 22 1 cm /I Do Q";
    auto original=make_original(sc);
    CHECK(!original.empty());
    auto independent=original;
    quantapdf_ir ir{};
    OK(quantapdf_qpdf_source_raw_images_to_ir(
        original.data(),original.size(),&source_limits,&ir));
    CHECK(ir.page_count==1u && ir.operation_count==3u &&
          ir.resources.image_count==3u);
    CHECK(ir.resources.images[0].format==QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW &&
          ir.resources.images[0].width==2u &&
          ir.resources.images[0].height==2u &&
          ir.resources.images[0].components==3);
    CHECK(ir.operations[0].bounds.x0==2.0f &&
          ir.operations[0].bounds.y0==9.0f &&
          ir.operations[0].bounds.x1==18.0f &&
          ir.operations[0].bounds.y1==21.0f);
    /* Original PDF and qpdf source provider can disappear after lowering:
     * detached IR must own all image samples and operation placement. */
    std::fill(original.begin(),original.end(),0u);
    quantapdf_ir_raster_image native{},again{};
    OK(quantapdf_ir_raster_raw_images_v1(&ir,0,&raster_limits,&native));
    quantapdf_ir_dispose(&ir);
    CHECK(ir.operations==nullptr && ir.resources.images==nullptr);
    auto oracle=direct_pdfium(independent);
    CHECK(oracle.size()==static_cast<size_t>(W*H*3));
    CHECK(native.width==(size_t)W && native.height==(size_t)H &&
          native.stride==(size_t)W*4u);
    struct probe {int x,y;unsigned char r,g,b;};
    const probe expected[]={
        {4,11,255,0,0},{14,11,0,255,0},
        {4,18,0,0,255},{14,18,255,255,0},
        {23,13,0,0,255},{24,17,255,0,0},
        {31,23,255,255,255}};
    for(const auto& c:expected) {
        const unsigned char *ref=oracle.data()+((size_t)c.y*W+c.x)*3u;
        const unsigned char *out=native.pixels+
            (size_t)c.y*native.stride+(size_t)c.x*4u;
        const unsigned char want[]={c.r,c.g,c.b};
        if(!matches(ref,want,10) || !matches(out,want,10))
            std::fprintf(stderr,
                "[source-image-do] probe=(%d,%d) expected=(%u,%u,%u) "
                "PDFium=(%u,%u,%u) Native=(%u,%u,%u)\\n",
                c.x,c.y,unsigned(c.r),unsigned(c.g),unsigned(c.b),
                unsigned(ref[0]),unsigned(ref[1]),unsigned(ref[2]),
                unsigned(out[0]),unsigned(out[1]),unsigned(out[2]));
        CHECK(matches(ref,want,10) && matches(out,want,10));
    }
    double error=0.0;size_t gross=0u;
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        const unsigned char *ref=oracle.data()+((size_t)y*W+x)*3u;
        const unsigned char *out=native.pixels+
            (size_t)y*native.stride+(size_t)x*4u;
        unsigned largest=0u;
        for(int c=0;c<3;++c) {
            const unsigned d=static_cast<unsigned>(
                std::abs(int(ref[c])-int(out[c])));
            error+=d;largest=std::max(largest,d);
        }
        if(largest>96u)++gross;
    }
    std::fprintf(stderr,
        "[source-image-do] original PDFium/native mae=%.4f gross=%zu\n",
        error/(W*H*3.0),gross);
    CHECK(error/(W*H*3.0)<=12.0 && gross<=W*H/10);
    OK(quantapdf_qpdf_source_raw_images_to_ir(
        independent.data(),independent.size(),&source_limits,&ir));
    OK(quantapdf_ir_raster_raw_images_v1(&ir,0,&raster_limits,&again));
    CHECK(again.size==native.size &&
          memcmp(again.pixels,native.pixels,native.size)==0);
    quantapdf_ir_dispose(&ir);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&again);
    return EXIT_SUCCESS;
}

int test_negative_and_recovery()
{
    scene good;
    good.content_a="q 4 0 0 4 4 4 cm /I Do Q";
    scene negatives[10];
    negatives[0]=good;negatives[0].content_a+=" BT ET";
    negatives[1]=good;negatives[1].content_a+=" 0 0 10 10 re f";
    negatives[2]=good;negatives[2].content_a+=" /Unknown Do";
    negatives[3]=good;negatives[3].content_a+=" q 1 0.1 0 1 1 1 cm /I Do Q";
    negatives[4]=good;negatives[4].content_a+=" q 4 0 0 4 40 40 cm /I Do Q";
    negatives[5]=good;negatives[5].wrong_subtype=true;
    negatives[6]=good;negatives[6].remove_resources=true;
    negatives[7]=good;negatives[7].soft_mask=true;
    negatives[8]=good;negatives[8].wrong_color=true;
    negatives[9]=good;negatives[9].content_a+=" BI /W 1 /H 1 ID x EI";
    for(size_t i=0;i<10u;++i) {
        auto pdf=make_original(negatives[i]);
        quantapdf_ir refused;
        memset(&refused,0xab,sizeof(refused));
        auto actual=quantapdf_qpdf_source_raw_images_to_ir(
            pdf.data(),pdf.size(),&source_limits,&refused);
        if(actual==QUANTAPDF_OK) {
            fprintf(stderr,"negative source case=%zu accepted unexpectedly\n",i);
            quantapdf_ir_dispose(&refused);
        }
        CHECK(actual==QUANTAPDF_ERROR_UNSUPPORTED ||
              actual==QUANTAPDF_ERROR_FORMAT);
        CHECK(refused.pages==nullptr && refused.page_count==0u &&
              refused.operations==nullptr && refused.operation_count==0u &&
              refused.resources.images==nullptr &&
              refused.resources.image_count==0u);
        quantapdf_ir_dispose(&refused);
    }
    auto pdf=make_original(good);
    const auto short_budget=quantapdf_qpdf_source_raw_image_limits{
        1048576u,16384u,3u,4u,4096u,4096u,8u};
    quantapdf_ir denied{};
    CHECK(quantapdf_qpdf_source_raw_images_to_ir(
        pdf.data(),pdf.size(),&short_budget,&denied)==
        QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(denied.pages==nullptr && denied.operations==nullptr &&
          denied.resources.images==nullptr);
    quantapdf_ir accepted{};
    OK(quantapdf_qpdf_source_raw_images_to_ir(
        pdf.data(),pdf.size(),&source_limits,&accepted));
    quantapdf_ir_raster_image native{};
    OK(quantapdf_ir_raster_raw_images_v1(&accepted,0,&raster_limits,&native));
    CHECK(native.pixels!=nullptr && native.size==W*H*4u);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_dispose(&accepted);
    return EXIT_SUCCESS;
}

} // namespace

int main()
{
    if(test_original_pixel_parity()!=EXIT_SUCCESS ||
       test_negative_and_recovery()!=EXIT_SUCCESS) return EXIT_FAILURE;
    std::puts("Original PDF Image Do -> detached owned RAW IR -> "
              "provider-free C11 raster PASS");
    return EXIT_SUCCESS;
}
