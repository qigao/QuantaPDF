#ifndef QUANTAPDF_NATIVE_ORDERED_TEXT_CASES_H
#define QUANTAPDF_NATIVE_ORDERED_TEXT_CASES_H
#include "native_ordered_text_fixture.h"
static int ordered_text_cases(const std::string& filename)
{
    size_t renders=0u,errors=0u,extractions=0u;
    const auto exercise=[&](const native_ordered_text::original& sample,bool encrypted)->int {
        CHECK(write_pdf(filename,sample.bytes));
        quantapdf_context *context=nullptr;quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        size_t finalized=0u;OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);OK(quantapdf_load_page(document,1,&page));
        quantapdf_close(document);quantapdf_drop_context(context);
        if(sample.restored_text_state) {
            char *text=nullptr;size_t size=0u,letters=0u;
            OK(quantapdf_extract_text(page,&text,&size));CHECK(text!=nullptr);
            for(size_t i=0u;i<size;++i) {if(text[i]=='l')++letters;else CHECK(text[i]==' ' || text[i]=='\n' || text[i]=='\r');}
            CHECK(letters==3u);quantapdf_free(text);++extractions;
        }
        std::vector<quantapdf_bitmap*> held;
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"Text paints=%zu glyphs=%zu encrypted=%d dpi=%u status=%d\n",sample.operations,sample.glyphs,int(encrypted),dpi,int(status));
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,components=0;const unsigned char *pixels=nullptr;size_t size=0u;
            OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            const auto expected=native_ordered_text::rgb(sample,dpi);
            if(size==expected.size() && std::memcmp(pixels,expected.data(),size)!=0) {
                for(size_t i=0u;i<size;++i)if(pixels[i]!=expected[i]) {std::fprintf(stderr,"Text RGB dpi=%u paints=%zu glyphs=%zu byte=%zu got=%u want=%u\n",dpi,sample.operations,sample.glyphs,i,unsigned(pixels[i]),unsigned(expected[i]));break;}
            }
            CHECK(w==int(12u*dpi/72u) && h==w && stride==w*3 && components==3 &&
                size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);
            held.push_back(bitmap);++renders;
        }
        quantapdf_drop_page(page);CHECK(finalized==0u);
        for(size_t i=0u;i<held.size();++i) {
            const auto expected=native_ordered_text::rgb(sample,i==0u?72u:i==1u?96u:144u);
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(held[i],&pixels,&size));
            CHECK(size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);quantapdf_drop_bitmap(held[i]);
        }
        CHECK(finalized==1u);return EXIT_SUCCESS;
    };
    for(size_t count:{1u,4u,5u,17u,69u})for(int kind:{0,1,2,3})for(bool encrypted:{false,true})
        CHECK(exercise(native_ordered_text::create(count,kind,encrypted),encrypted)==EXIT_SUCCESS);
    for(size_t depth:{2u,16u,17u,129u,257u,4097u})for(bool encrypted:{false,true})
        CHECK(exercise(native_ordered_text::create(2u,4,encrypted,depth),encrypted)==EXIT_SUCCESS);
    for(bool encrypted:{false,true})for(int invalid:{1,2,3,4}) {
        const auto sample=native_ordered_text::create(69u,1,encrypted,0u,invalid);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        const auto status=quantapdf_render_page(page,&bitmap);
        CHECK(status==(invalid==3?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT) && bitmap==nullptr);
        quantapdf_drop_page(page);++errors;
    }
    CHECK(renders==156u && errors==8u && extractions==12u);
    std::printf("Ordered Text/Fill: %zu analytic full RGB renders, 1/4/5/17/69 paints and glyphs, deep post-BT q/Q and text extraction, encrypted split selected pages, detached owners, %zu late errors\n",renders,errors);
    return EXIT_SUCCESS;
}
#endif
