#ifndef QUANTAPDF_NATIVE_SCENE_PARITY_CASES_H
#define QUANTAPDF_NATIVE_SCENE_PARITY_CASES_H
#include "native_ordered_scene_fixture.h"

static int scene_parity_cases(const std::string& filename)
{
    size_t renders=0u,failures=0u;
    for(size_t count:{1u,5u,17u,65u})for(unsigned clip:{0u,1u,2u,3u,4u})
    for(bool restore:{false,true})for(bool encrypted:{false,true}) {
        const size_t restored=restore?count/2u:SIZE_MAX;
        const auto plain=native_ordered_scene::create(count,0u,unsigned(count%6u),encrypted,0u,0,clip,restored,false,
            native_ordered_scene::text_presence::none);
        const auto empty=native_ordered_scene::create(count,0u,unsigned(count%6u),encrypted,0u,0,clip,restored,false,
            native_ordered_scene::text_presence::empty);
        CHECK(plain.glyphs==0u && empty.glyphs==0u && plain.paints.size()==empty.paints.size());
        quantapdf_context *context=nullptr;OK(quantapdf_context_create(&context));
        size_t finalized=0u;context->test_finalizations=&finalized;std::array<quantapdf_page*,2> pages{};
        for(size_t variant=0u;variant<2u;++variant) {
            CHECK(write_pdf(filename,variant==0u?plain.bytes:empty.bytes));quantapdf_document *document=nullptr;
            if(encrypted)CHECK(quantapdf_open_with_context(context,filename.c_str(),"wrong",&document)==QUANTAPDF_ERROR_PASSWORD && document==nullptr);
            OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
            CHECK(std::remove(filename.c_str())==0);
            quantapdf_page *blank=nullptr;OK(quantapdf_load_page(document,0,&blank));
            OK(quantapdf_load_page(document,1,&pages[variant]));quantapdf_close(document);
            quantapdf_bitmap *bitmap=nullptr;OK(quantapdf_render_page(blank,&bitmap));quantapdf_drop_page(blank);
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            for(size_t i=0u;i<size;++i)CHECK(pixels[i]==255u);quantapdf_drop_bitmap(bitmap);
        }
        quantapdf_drop_context(context);CHECK(finalized==0u);
        struct held {quantapdf_bitmap *bitmap;unsigned dpi;};std::vector<held> bitmaps;
        for(unsigned dpi:{72u,96u,144u})for(size_t variant=0u;variant<2u;++variant) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(pages[variant],&options,&bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"scene parity count=%zu clip=%u restore=%d encrypted=%d text=%zu dpi=%u status=%d\n",
                count,clip,int(restore),int(encrypted),variant,dpi,int(status));
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,components=0;OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));
            CHECK(w==int(12u*dpi/72u) && h==w && stride==3*w && components==3);
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            const auto expected=native_ordered_scene::rgb(plain,dpi);
            if(size==expected.size() && std::memcmp(pixels,expected.data(),size)!=0)for(size_t i=0u;i<size;++i)if(pixels[i]!=expected[i]) {
                std::fprintf(stderr,"scene parity count=%zu clip=%u restore=%d text=%zu dpi=%u byte=%zu got=%u expected=%u\n",
                    count,clip,int(restore),variant,dpi,i,unsigned(pixels[i]),unsigned(expected[i]));break;
            }
            CHECK(size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);
            bitmaps.push_back({bitmap,dpi});++renders;
        }
        for(auto page:pages)quantapdf_drop_page(page);CHECK(finalized==0u);
        for(const auto& held:bitmaps) {
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(held.bitmap,&pixels,&size));
            const auto expected=native_ordered_scene::rgb(plain,held.dpi);
            CHECK(size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);quantapdf_drop_bitmap(held.bitmap);
        }
        CHECK(finalized==1u);
    }
    for(bool encrypted:{false,true})for(int invalid:{1,2,3}) {
        const auto sample=native_ordered_scene::create(17u,0u,2u,encrypted,0u,invalid,1u,8u,false,
            native_ordered_scene::text_presence::none);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        CHECK(quantapdf_render_page(page,&bitmap)==(invalid==3?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT) && bitmap==nullptr);
        quantapdf_drop_page(page);++failures;
    }
    CHECK(renders==480u && failures==6u);
    std::printf("Unified scene: %zu analytic RGB renders with/without empty Text, 1/5/17/65 images plus fills, W/W*/polygon/empty Clip and Q restoration, selected AES-256 pages, retired owners and %zu atomic late failures\n",renders,failures);
    return EXIT_SUCCESS;
}
#endif
