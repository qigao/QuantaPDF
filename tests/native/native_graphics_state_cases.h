#ifndef QUANTAPDF_NATIVE_GRAPHICS_STATE_CASES_H
#define QUANTAPDF_NATIVE_GRAPHICS_STATE_CASES_H
#include "native_graphics_state_fixture.h"
static int graphics_state_cases(const std::string& filename)
{
    size_t renders=0u,failures=0u;
    for(size_t depth:{2u,16u,17u,257u,4097u})for(bool clipped:{false,true})
    for(bool encrypted:{false,true})for(int kind:{0,1,2,3}) {
        const bool images=kind==1;
        const auto sample=native_graphics_fixture::create(depth,clipped,images,encrypted,kind==2,0,kind==3);
        CHECK(write_pdf(filename,sample.bytes));
        quantapdf_context *context=nullptr;quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        size_t finalized=0u;OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);quantapdf_drop_context(context);
        std::vector<quantapdf_bitmap*> held;
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"state depth=%zu clipped=%d image=%d late=%d encrypted=%d dpi=%u status=%d\n",depth,int(clipped),int(images),kind==2,int(encrypted),dpi,int(status));
            CHECK(status==QUANTAPDF_OK);
            int w=0,h=0,stride=0,components=0;const unsigned char *pixels=nullptr;size_t size=0u;
            OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));
            OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            const auto expected=native_graphics_fixture::rgb(dpi,clipped,images);
            CHECK(w==int(12u*dpi/72u) && h==int(9u*dpi/72u) && stride==w*3 && components==3 &&
                size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);
            held.push_back(bitmap);++renders;
        }
        quantapdf_drop_page(page);CHECK(finalized==0u);
        for(size_t i=0u;i<held.size();++i) {
            const auto expected=native_graphics_fixture::rgb(i==0u?72u:i==1u?96u:144u,clipped,images);
            const unsigned char *pixels=nullptr;size_t size=0u;
            OK(quantapdf_bitmap_data(held[i],&pixels,&size));
            CHECK(size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);
            quantapdf_drop_bitmap(held[i]);
        }
        CHECK(finalized==1u);
    }
    for(bool encrypted:{false,true})for(bool clipped:{false,true})for(int invalid:{1,2,3,4}) {
        const auto sample=native_graphics_fixture::create(257u,clipped,false,encrypted,true,invalid);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        CHECK(quantapdf_render_page(page,&bitmap)==(invalid==4?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT) && bitmap==nullptr);
        quantapdf_drop_page(page);++failures;
    }
    CHECK(renders==240u && failures==16u);
    std::printf("Dynamic q/Q: %zu full RGB renders, 4097 frames, long state traffic, split/encrypted streams, %zu atomic errors\n",renders,failures);
    return EXIT_SUCCESS;
}
#endif
