#ifndef QUANTAPDF_NATIVE_ORDERED_PAINTS_CASES_H
#define QUANTAPDF_NATIVE_ORDERED_PAINTS_CASES_H
#include "native_ordered_paints_fixture.h"
static int ordered_paints_cases(const std::string& filename)
{
    size_t renders=0u,failures=0u;
    for(size_t count:{1u,4u,5u,17u,65u})for(int kind:{0,1,2})for(bool encrypted:{false,true})
    for(unsigned quarters:{4u,5u})for(int rotation:{0,90}) {
        const auto sample=native_ordered_fixture::create(count,kind,encrypted,quarters,rotation);
        CHECK(write_pdf(filename,sample.bytes));
        quantapdf_context *context=nullptr;quantapdf_document *document=nullptr;quantapdf_page *page=nullptr,*blank=nullptr;
        size_t finalized=0u;OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);OK(quantapdf_load_page(document,0,&blank));OK(quantapdf_load_page(document,1,&page));
        quantapdf_close(document);quantapdf_drop_context(context);
        quantapdf_bitmap *empty=nullptr;OK(quantapdf_render_page(blank,&empty));quantapdf_drop_page(blank);
        const unsigned char *white=nullptr;size_t white_size=0u;OK(quantapdf_bitmap_data(empty,&white,&white_size));
        for(size_t i=0u;i<white_size;++i)CHECK(white[i]==255u);
        quantapdf_drop_bitmap(empty);
        std::vector<quantapdf_bitmap*> held;
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"ordered count=%zu kind=%d encrypted=%d unit=%u rotate=%d dpi=%u status=%d\n",count,kind,int(encrypted),quarters,rotation,dpi,int(status));
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,components=0;const unsigned char *pixels=nullptr;size_t size=0u;
            OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            const auto expected=native_ordered_fixture::rgb(sample,dpi,quarters,rotation);
            CHECK(w==int(native_ordered_fixture::side(12u,dpi,quarters)) &&
                h==int(native_ordered_fixture::side(12u,dpi,quarters)) && stride==w*3 && components==3 &&
                size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);
            held.push_back(bitmap);++renders;
        }
        quantapdf_drop_page(page);CHECK(finalized==0u);
        for(size_t i=0u;i<held.size();++i) {
            const auto expected=native_ordered_fixture::rgb(sample,i==0u?72u:i==1u?96u:144u,quarters,rotation);
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(held[i],&pixels,&size));
            CHECK(size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);quantapdf_drop_bitmap(held[i]);
        }
        CHECK(finalized==1u);
    }
    for(bool encrypted:{false,true})for(int invalid:{1,2,3}) {
        const auto sample=native_ordered_fixture::create(65u,2,encrypted,4u,0,invalid);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        const auto status=quantapdf_render_page(page,&bitmap);
        CHECK(status==(invalid==2?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT) && bitmap==nullptr);
        quantapdf_drop_page(page);++failures;
    }
    CHECK(renders==360u && failures==6u);
    std::printf("Ordered paints: %zu full RGB renders, 1/4/5/17/65 Fill/Image/mixed, physical DPI/rotation/unit, encrypted selected-page lifetimes, %zu atomic late errors\n",renders,failures);
    return EXIT_SUCCESS;
}
#endif
