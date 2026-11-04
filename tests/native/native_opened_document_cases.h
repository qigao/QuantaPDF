#ifndef QUANTAPDF_NATIVE_OPENED_DOCUMENT_CASES_H
#define QUANTAPDF_NATIVE_OPENED_DOCUMENT_CASES_H
#include "backend/builtin_page_view.h"
#include "backend/qpdf_document_internal.h"
#include "document/document_internal.h"

static std::string opened_document_fixture(bool encrypted)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto image=pdf->newStream(std::string(1,char(128)));
    image.getDict().replaceKey("/Subtype",Obj::newName("/Image"));
    image.getDict().replaceKey("/Width",Obj::newInteger(1));
    image.getDict().replaceKey("/Height",Obj::newInteger(1));
    image.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    image.getDict().replaceKey("/ColorSpace",Obj::newName("/DeviceGray"));
    auto objects=Obj::newDictionary(),resources=Obj::newDictionary();
    objects.replaceKey("/I",image);
    resources.replaceKey("/XObject",objects);
    root.replaceKey("/Resources",resources);
    root.replaceKey("/MediaBox",Obj::parse("[0 0 12 9]"));
    const char *contents[]={"1 0 0 rg 0 0 12 9 re f",
        "q 12 0 0 9 0 0 cm /I Do Q",
        "1 0 0 rg 0 0 12 9 re f q 12 0 0 9 0 0 cm /Missing Do Q",
        "1 0 0 rg 0 0 12 9 re f 0 0 12 9 re S"};
    std::vector<Obj> pages;
    for(const auto *content:contents) {
        auto page=pdf->makeIndirectObject(Obj::parse("<< /Type /Page >>"));
        page.replaceKey("/Parent",root);
        page.replaceKey("/Contents",pdf->newStream(content));
        pages.push_back(page);
    }
    root.replaceKey("/Kids",Obj::newArray(pages));
    root.replaceKey("/Count",Obj::newInteger(4));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",
        true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();
    const auto buffer=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
}

static int opened_bitmap_color(quantapdf_bitmap *bitmap,unsigned dpi,
    unsigned red,unsigned green,unsigned blue)
{
    int width=0,height=0,stride=0,components=0;
    const unsigned char *pixels=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_dimensions(bitmap,&width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
    CHECK(width==int(12u*dpi/72u) && height==int(9u*dpi/72u) &&
        stride==width*3 && components==3 && size==size_t(stride)*size_t(height));
    for(size_t i=0u;i<size;i+=3u)
        CHECK(pixels[i]==red && pixels[i+1u]==green && pixels[i+2u]==blue);
    return EXIT_SUCCESS;
}

static int opened_document_cases(const std::string& filename)
{
    for(int mode:{0,1,2}) {
        const bool encrypted=mode!=0;
        CHECK(write_pdf(filename,opened_document_fixture(encrypted)));
        quantapdf_context *context=nullptr;
        quantapdf_document *document=nullptr;
        size_t finalizations=0u;
        OK(quantapdf_context_create(&context));
        context->test_finalizations=&finalizations;
        if(encrypted)for(const char *password:{static_cast<const char*>(nullptr),"wrong"}) {
            document=reinterpret_cast<quantapdf_document*>(uintptr_t(1));
            CHECK(quantapdf_open_with_context(context,filename.c_str(),password,&document)==QUANTAPDF_ERROR_PASSWORD);
            CHECK(document==nullptr && context->references==1u);
        }
        OK(quantapdf_open_with_context(context,filename.c_str(),
            mode==0?nullptr:mode==1?"reader":"owner",&document));
        CHECK(std::remove(filename.c_str())==0);
        int count=0;
        OK(quantapdf_page_count(document,&count));
        CHECK(count==4);
        quantapdf_page *pages[4]={};
        for(int index=0;index<4;++index)OK(quantapdf_load_page(document,index,&pages[index]));
        quantapdf_page *invalid=reinterpret_cast<quantapdf_page*>(uintptr_t(1));
        CHECK(quantapdf_load_page(document,4,&invalid)==QUANTAPDF_ERROR_ARGUMENT && invalid==nullptr);
        CHECK(document->references==5u);
        quantapdf_close(document);document=nullptr;
        quantapdf_drop_context(context);
        CHECK(finalizations==0u);

        std::vector<quantapdf_bitmap*> held;
        for(unsigned dpi:{72u,96u,144u}) {
            const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            for(int index:{1,0}) {
                quantapdf_bitmap *bitmap=nullptr;
                OK(quantapdf_render_page_with_options(pages[index],&options,&bitmap));
                CHECK(opened_bitmap_color(bitmap,dpi,index==0?255u:128u,
                    index==0?0u:128u,index==0?0u:128u)==EXIT_SUCCESS);
                held.push_back(bitmap);
            }
        }
        // These are unsaved changes to the existing qpdf objects. A byte reopen
        // would paint the original red page and original gray image instead.
        const auto view=quantapdf_builtin_page_borrow(pages[0]->backend);
        quantapdf_qpdf_page_geometry geometry{};
        OK(quantapdf_qpdf_load_page_geometry(*view.document->pdf,0,&geometry));
        geometry.page.replaceKey("/Contents",view.document->pdf->newStream("0 0 1 rg 0 0 12 9 re f"));
        auto image=quantapdf_qpdf_inherited_value(geometry.page,"/Resources")
            .getKey("/XObject").getKey("/I");
        image.replaceStreamData(std::string(1,char(32)),Obj::newNull(),Obj::newNull());
        for(int index:{0,1}) {
            quantapdf_bitmap *bitmap=nullptr;
            OK(quantapdf_render_page(pages[index],&bitmap));
            CHECK(opened_bitmap_color(bitmap,72u,index==0?0u:32u,
                index==0?0u:32u,index==0?255u:32u)==EXIT_SUCCESS);
            quantapdf_drop_bitmap(bitmap);
        }
        for(int index:{2,3}) {
            const size_t references=context->references;
            quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_page(pages[index],&bitmap)==
                (index==2?QUANTAPDF_ERROR_FORMAT:QUANTAPDF_ERROR_UNSUPPORTED));
            CHECK(bitmap==nullptr && context->references==references);
        }
        image.replaceStreamData("invalid deflate data",Obj::newName("/FlateDecode"),Obj::newNull());
        const size_t references=context->references;
        quantapdf_bitmap *failed=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        CHECK(quantapdf_render_page(pages[1],&failed)==QUANTAPDF_ERROR_FORMAT);
        CHECK(failed==nullptr && context->references==references);
        for(auto *page:pages)quantapdf_drop_page(page);
        CHECK(finalizations==0u && context->references==held.size());
        for(size_t i=0u;i<held.size();++i) {
            const unsigned dpi=i<2u?72u:i<4u?96u:144u;
            CHECK(opened_bitmap_color(held[i],dpi,i%2u?255u:128u,
                i%2u?0u:128u,i%2u?0u:128u)==EXIT_SUCCESS);
            quantapdf_drop_bitmap(held[i]);
        }
        CHECK(finalizations==1u);
    }
    // Encrypted embedded font, Image and Clip use the same independently
    // authored original-quadratic/analytic full-RGB reference as plain PDFs.
    for(int mode:{99,100}) {
        const auto original=native_ttf_fixture(mode);
        auto pdf=QPDF::create();
        pdf->processMemoryFile("encrypted native font",original.data(),original.size());
        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
        writer.write();
        const auto buffer=writer.getBufferSharedPointer();
        CHECK(write_pdf(filename,std::string(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize())));
        quantapdf_document *document=nullptr;
        quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),"reader",&document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,0,&page));
        quantapdf_close(document);
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;
            const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            OK(quantapdf_render_page_with_options(page,&options,&bitmap));
            const auto expected=native_ttf_fractional_expected(mode,dpi);
            const unsigned char *pixels=nullptr;
            size_t size=0u;
            OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            CHECK(size==expected.rgb.size() && std::memcmp(pixels,expected.rgb.data(),size)==0);
            quantapdf_drop_bitmap(bitmap);
        }
        quantapdf_drop_page(page);
    }
    std::puts("Opened/decrypted qpdf pages: selection, object identity, resources, failures, full RGB and owner retirement passed");
    return EXIT_SUCCESS;
}
#endif
