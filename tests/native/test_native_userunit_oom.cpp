/* Private TEST-ONLY deterministic Bitmap allocation failure on true
 * original QPDF Source V10 UserUnit pages. No PDFium or alternate renderer.
 * Public/installed SDK headers and ABI remain unchanged. */
#include <quantapdf/quantapdf.h>
#include "core/context_internal.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(expr) do { if(!(expr)) { \
    std::fprintf(stderr,"native UserUnit OOM at %d: %s\n",__LINE__,#expr); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(expr) CHECK((expr)==QUANTAPDF_OK)
using Obj=QPDFObjectHandle;

static bool author_original_pdf(const char *filename,int quarters)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24]"
        " /CropBox [0 0 16.5 12.25] /Rotate 90 >>"));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/UserUnit",
        Obj::parse(std::to_string((double)quarters/4.0)));
    page.replaceKey("/Contents",pdf->newStream(
        "1 0 0 rg 0 0 32 24 re f"));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    std::FILE *out=std::fopen(filename,"wb");
    if(out==nullptr)return false;
    const bool good=std::fwrite(
        blob->getBuffer(),1u,blob->getSize(),out)==blob->getSize();
    const bool closed=std::fclose(out)==0;
    return good&&closed;
}

int main()
{
    const char *const filename="native-original-userunit-oom.pdf";
    for(int quarters:{2,5,8,16}) {
        CHECK(author_original_pdf(filename,quarters));
        quantapdf_context *context=nullptr;
        quantapdf_document *document=nullptr;
        quantapdf_page *page=nullptr;
        size_t finalizations=0u;
        OK(quantapdf_context_create(&context));
        CHECK(context!=nullptr && context->references==1u);
        context->test_finalizations=&finalizations;
        OK(quantapdf_open_with_context(
            context,filename,nullptr,&document));
        CHECK(std::remove(filename)==0);
        OK(quantapdf_load_page(document,0,&page));
        const size_t baseline_references=context->references;
        CHECK(baseline_references>=3u);
        for(int dpi:{72,96,144}) {
            quantapdf_render_options options{};
            options.struct_size=sizeof(options);
            options.dpi=(float)dpi;
            context->test_fail_allocation=1;
            quantapdf_bitmap *denied=
                reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            const auto failure=quantapdf_render_page_with_options(
                page,&options,&denied);
            CHECK(failure==QUANTAPDF_ERROR_NOMEM && denied==nullptr &&
                  context->references==baseline_references &&
                  context->test_fail_allocation==0 &&
                  finalizations==0u);
            quantapdf_bitmap *bitmap=nullptr;
            OK(quantapdf_render_page_with_options(
                page,&options,&bitmap));
            CHECK(bitmap!=nullptr &&
                  context->references==baseline_references+1u);
            int w=0,h=0,stride=0,components=0;
            const unsigned char *pixels=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_dimensions(
                bitmap,&w,&h,&stride,&components));
            OK(quantapdf_bitmap_data(bitmap,&pixels,&bytes));
            /* Rotated original Crop 12.25x16.5, times quarter factor.
             * Integer ceilings on exact sixteenth-point geometry. */
            const int exact_w=(49*quarters*dpi+1151)/1152;
            const int exact_h=(66*quarters*dpi+1151)/1152;
            CHECK(w==exact_w && h==exact_h && components==3 &&
                  stride==w*3 && bytes==(size_t)stride*(size_t)h &&
                  pixels!=nullptr);
            const size_t sample=2u*(size_t)stride+2u*3u;
            CHECK(sample+2u<bytes && pixels[sample]==255u &&
                  pixels[sample+1u]==0u && pixels[sample+2u]==0u);
            const std::vector<unsigned char> golden(pixels,pixels+bytes);
            quantapdf_drop_bitmap(bitmap);
            CHECK(context->references==baseline_references);
            /* A failure cannot poison the page, leak the owner lease,
             * reuse partial pixels or trigger fallback on next render. */
            quantapdf_bitmap *repeat=nullptr;
            OK(quantapdf_render_page_with_options(
                page,&options,&repeat));
            const unsigned char *other=nullptr;
            size_t other_size=0u;
            OK(quantapdf_bitmap_data(repeat,&other,&other_size));
            CHECK(other!=nullptr && other_size==golden.size() &&
                  std::memcmp(other,golden.data(),other_size)==0);
            quantapdf_drop_bitmap(repeat);
            CHECK(context->references==baseline_references);
        }
        /* A successfully owned Bitmap stays valid after its Page,
         * Document and caller's Context leases have been retired. */
        quantapdf_bitmap *retained=nullptr;
        OK(quantapdf_render_page(page,&retained));
        CHECK(retained!=nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(document);
        quantapdf_drop_context(context);
        CHECK(finalizations==0u);
        const unsigned char *pixels=nullptr;
        size_t length=0u;
        OK(quantapdf_bitmap_data(retained,&pixels,&length));
        CHECK(pixels!=nullptr && length!=0u);
        quantapdf_drop_bitmap(retained);
        CHECK(finalizations==1u);
    }
    return EXIT_SUCCESS;
}
