#ifndef QUANTAPDF_NATIVE_ORDERED_SCENE_CASES_H
#define QUANTAPDF_NATIVE_ORDERED_SCENE_CASES_H
#include "native_ordered_scene_fixture.h"
static int ordered_scene_cases(const std::string& filename,unsigned path_kind=0u)
{
    const bool path_mode=path_kind!=0u;
    size_t renders=0u,errors=0u;
    const auto exercise=[&](const native_ordered_scene::original& sample,bool encrypted)->int {
        CHECK(write_pdf(filename,sample.bytes));
        quantapdf_context *context=nullptr;quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        size_t finalized=0u;OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        if(path_mode && encrypted)CHECK(quantapdf_open_with_context(context,filename.c_str(),"wrong",&document)==QUANTAPDF_ERROR_PASSWORD && document==nullptr);
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);OK(quantapdf_load_page(document,1,&page));
        quantapdf_close(document);quantapdf_drop_context(context);
        std::vector<quantapdf_bitmap*> held;
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"Scene paints=%zu glyphs=%zu encrypted=%d dpi=%u status=%d\n",sample.operations,sample.glyphs,int(encrypted),dpi,int(status));
            if(status!=QUANTAPDF_OK) {
                for(auto held_bitmap:held)quantapdf_drop_bitmap(held_bitmap);
                quantapdf_drop_page(page);return EXIT_FAILURE;
            }
            CHECK(bitmap!=nullptr);
            int w=0,h=0,stride=0,components=0;const unsigned char *pixels=nullptr;size_t size=0u;
            OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            const auto expected=native_ordered_scene::rgb(sample,dpi);
            if(size==expected.size() && std::memcmp(pixels,expected.data(),size)!=0) {
                for(size_t i=0u;i<size;++i)if(pixels[i]!=expected[i]) {std::fprintf(stderr,"Scene RGB dpi=%u paints=%zu glyphs=%zu byte=%zu got=%u want=%u\n",dpi,sample.operations,sample.glyphs,i,unsigned(pixels[i]),unsigned(expected[i]));break;}
            }
            CHECK(w==int(12u*dpi/72u) && h==w && stride==w*3 && components==3 &&
                size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);
            held.push_back(bitmap);++renders;
        }
        quantapdf_drop_page(page);CHECK(finalized==0u);
        for(size_t i=0u;i<held.size();++i) {
            const auto expected=native_ordered_scene::rgb(sample,i==0u?72u:i==1u?96u:144u);
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(held[i],&pixels,&size));
            CHECK(size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);quantapdf_drop_bitmap(held[i]);
        }
        CHECK(finalized==1u);return EXIT_SUCCESS;
    };
    if(path_mode) {
        for(unsigned order=0u;order<6u;++order)
        for(unsigned clip:{0u,1u,3u,4u})for(bool encrypted:{false,true})
            CHECK(exercise(native_ordered_scene::create(1u,1u,order,encrypted,0u,0,clip,SIZE_MAX,false,native_ordered_scene::text_presence::glyphs,path_kind,3u,0u,true),encrypted)==EXIT_SUCCESS);
        if(path_kind<=4u)for(bool encrypted:{false,true})
            CHECK(exercise(native_ordered_scene::create(1u,1u,2u,encrypted,0u,0,0u,SIZE_MAX,false,native_ordered_scene::text_presence::glyphs,path_kind,65u,8192u,true),encrypted)==EXIT_SUCCESS);
    } else {
    for(size_t count:{1u,5u,17u,33u,65u})for(unsigned order=0u;order<6u;++order)for(bool encrypted:{false,true})
        CHECK(exercise(native_ordered_scene::create(count,1u,order,encrypted),encrypted)==EXIT_SUCCESS);
    for(size_t count:{1u,17u})for(unsigned order=0u;order<6u;++order)for(bool encrypted:{false,true})
        CHECK(exercise(native_ordered_scene::create(count,69u,order,encrypted),encrypted)==EXIT_SUCCESS);
    for(unsigned order=0u;order<6u;++order)for(bool encrypted:{false,true})
        CHECK(exercise(native_ordered_scene::create(1u,0u,order,encrypted),encrypted)==EXIT_SUCCESS);
    for(size_t depth:{129u,4097u})for(bool encrypted:{false,true})
        CHECK(exercise(native_ordered_scene::create(5u,1u,3u,encrypted,depth),encrypted)==EXIT_SUCCESS);
    }
    for(bool encrypted:{false,true})for(int invalid=1;invalid<=(path_mode?8:4);++invalid) {
        const auto sample=native_ordered_scene::create(17u,1u,2u,encrypted,0u,invalid,0u,SIZE_MAX,false,native_ordered_scene::text_presence::glyphs,path_mode?3u:0u,1u,0u,path_mode);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        const auto status=quantapdf_render_page(page,&bitmap);
        CHECK(status==((invalid==3 || invalid==4 || invalid==6)?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT) && bitmap==nullptr);
        quantapdf_drop_page(page);++errors;
    }
    CHECK(renders==(path_mode?(path_kind==5u?144u:150u):300u) && errors==(path_mode?16u:8u));
    if(path_mode)std::printf("BT Fill path kind=%u: %zu independent full RGB renders, six orders, single-root Clip and empty Clip, encrypted split selected pages, detached owners, %zu late errors\n",path_kind,renders,errors);
    else std::printf("Ordered Text/Fill/Image: %zu analytic full RGB renders, six orders, 1/5/17/33/65 Images, 69 glyphs, RGB/Gray RAW/Flate, unused resources, deep q/Q, encrypted split selected pages, detached owners, %zu late errors\n",renders,errors);
    return EXIT_SUCCESS;
}
#endif
