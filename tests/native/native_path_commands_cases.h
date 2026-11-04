#ifndef QUANTAPDF_NATIVE_PATH_COMMANDS_CASES_H
#define QUANTAPDF_NATIVE_PATH_COMMANDS_CASES_H
#include "native_path_commands_fixture.h"

static int path_commands_cases(const std::string& filename)
{
    size_t renders=0u,failures=0u;
    const auto run=[&](size_t repeats,unsigned primitive,unsigned clip,bool even,
                       bool restore,bool encrypted,size_t zero_commands)->int {
        const auto sample=native_path_commands::create(repeats,primitive,clip,even,restore,encrypted,zero_commands);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_context *context=nullptr;OK(quantapdf_context_create(&context));
        size_t finalized=0u;context->test_finalizations=&finalized;quantapdf_document *document=nullptr;
        if(encrypted)CHECK(quantapdf_open_with_context(context,filename.c_str(),"wrong",&document)==QUANTAPDF_ERROR_PASSWORD && document==nullptr);
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);quantapdf_page *blank=nullptr,*page=nullptr;
        OK(quantapdf_load_page(document,0,&blank));OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *empty=nullptr;OK(quantapdf_render_page(blank,&empty));quantapdf_drop_page(blank);
        int w=0,h=0,stride=0,components=0;OK(quantapdf_bitmap_dimensions(empty,&w,&h,&stride,&components));
        CHECK(w==5 && h==4 && stride==15 && components==3);
        const unsigned char *data=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(empty,&data,&size));
        for(size_t i=0u;i<size;++i)CHECK(data[i]==255u);quantapdf_drop_bitmap(empty);
        quantapdf_drop_context(context);CHECK(finalized==0u);
        std::array<quantapdf_bitmap*,3> held{};size_t slot=0u;
        for(unsigned dpi:{72u,96u,144u}) {
            const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto error=quantapdf_render_page_with_options(page,&options,&held[slot]);
            if(error!=QUANTAPDF_OK)std::fprintf(stderr,"long path repeats=%zu primitive=%u clip=%u even=%d restore=%d zeros=%zu dpi=%u status=%d\n",repeats,primitive,clip,int(even),int(restore),zero_commands,dpi,int(error));
            CHECK(error==QUANTAPDF_OK);OK(quantapdf_bitmap_dimensions(held[slot],&w,&h,&stride,&components));
            CHECK(w==int(12u*dpi/72u) && h==w && stride==w*3 && components==3);
            OK(quantapdf_bitmap_data(held[slot],&data,&size));const auto expected=native_path_commands::rgb(sample,dpi);
            if(size==expected.size() && std::memcmp(data,expected.data(),size)!=0)for(size_t i=0u;i<size;++i)if(data[i]!=expected[i]) {
                std::fprintf(stderr,"long path repeats=%zu primitive=%u clip=%u even=%d dpi=%u byte=%zu got=%u expected=%u\n",repeats,primitive,clip,int(even),dpi,i,unsigned(data[i]),unsigned(expected[i]));break;
            }
            CHECK(size==expected.size() && std::memcmp(data,expected.data(),size)==0);++slot;++renders;
        }
        quantapdf_drop_page(page);CHECK(finalized==0u);slot=0u;
        for(unsigned dpi:{72u,96u,144u}) {
            OK(quantapdf_bitmap_data(held[slot],&data,&size));const auto expected=native_path_commands::rgb(sample,dpi);
            CHECK(size==expected.size() && std::memcmp(data,expected.data(),size)==0);quantapdf_drop_bitmap(held[slot++]);
        }
        CHECK(finalized==1u);return EXIT_SUCCESS;
    };
    for(size_t repeats:{1u,2u,3u,17u,65u})for(unsigned clip:{0u,1u,2u,3u,4u})
    for(bool even:{false,true})for(bool restore:{false,true})for(bool encrypted:{false,true})
        CHECK(run(repeats,0u,clip,even,restore,encrypted,0u)==EXIT_SUCCESS);
    for(unsigned primitive:{1u,2u,3u})for(size_t repeats:{3u,65u})for(unsigned clip:{0u,1u})
    for(bool even:{false,true})for(bool encrypted:{false,true})
        CHECK(run(repeats,primitive,clip,even,true,encrypted,0u)==EXIT_SUCCESS);
    for(unsigned primitive:{0u,1u})for(bool even:{false,true})for(bool encrypted:{false,true})
        CHECK(run(128u,primitive,0u,even,false,encrypted,0u)==EXIT_SUCCESS);
    for(size_t zeros:{4096u,8192u,16384u})for(unsigned primitive:{0u,1u})
    for(unsigned clip:{0u,1u})for(bool encrypted:{false,true})
        CHECK(run(3u,primitive,clip,false,true,encrypted,zeros)==EXIT_SUCCESS);
    for(bool encrypted:{false,true})for(int invalid:{1,2,3,4,5}) {
        const auto sample=native_path_commands::create(invalid==5?129u:17u,0u,0u,false,false,encrypted,0u,invalid);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        CHECK(quantapdf_render_page(page,&bitmap)==(invalid>=4?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT) && bitmap==nullptr);
        quantapdf_drop_page(page);++failures;
    }
    std::printf("Dynamic Fill paths: %zu independent RGB renders, m/l/c/v/y/h, f/f*, long winding/zero edges, Clip/Q, selected AES-256 pages, detached owners; %zu atomic errors\n",renders,failures);
    return EXIT_SUCCESS;
}
#endif
