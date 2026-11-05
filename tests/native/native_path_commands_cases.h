#ifndef QUANTAPDF_NATIVE_PATH_COMMANDS_CASES_H
#define QUANTAPDF_NATIVE_PATH_COMMANDS_CASES_H
#include "native_path_commands_fixture.h"
#include "document/document_internal.h"

static int path_commands_cases(const std::string& filename,bool multi=false,unsigned selection=0u)
{
    size_t renders=0u,failures=0u;
    const auto run=[&](size_t repeats,unsigned primitive,unsigned clip,bool even,
                       bool restore,bool encrypted,size_t zero_commands,unsigned layout=0u,unsigned closing=0u,bool winding_path=false)->int {
        const auto sample=selection>=6u?native_path_commands::create_states(repeats,primitive,clip,even,restore,encrypted,zero_commands,layout):multi && !winding_path?native_path_commands::create_contours(repeats,primitive,clip,even,restore,encrypted,zero_commands,layout,closing,0,selection==3u):
            native_path_commands::create(repeats,primitive,clip,even,restore,encrypted,zero_commands,0,selection==3u);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_context *context=nullptr;OK(quantapdf_context_create(&context));
        size_t finalized=0u;context->test_finalizations=&finalized;quantapdf_document *document=nullptr;
        if(encrypted)CHECK(quantapdf_open_with_context(context,filename.c_str(),"wrong",&document)==QUANTAPDF_ERROR_PASSWORD && document==nullptr);
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
        const auto input_matches=[&](const quantapdf_document *opened) {
            return opened->source_size==sample.bytes.size() &&
                std::memcmp(opened->source_data,sample.bytes.data(),sample.bytes.size())==0;
        };
        if(!input_matches(document)) {
            std::fprintf(stderr,"opened input differs from authored PDF: count=%zu primitive=%u clip=%u even=%d layout=%u closing=%u encrypted=%d authored=%zu opened=%zu\n",
                repeats,primitive,clip,int(even),layout,closing,int(encrypted),sample.bytes.size(),document->source_size);
            CHECK(write_pdf(filename+".authored.pdf",sample.bytes));
            CHECK(write_pdf(filename+".opened.pdf",std::string(reinterpret_cast<const char*>(document->source_data),document->source_size)));
            quantapdf_close(document);quantapdf_drop_context(context);return EXIT_FAILURE;
        }
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
            CHECK(input_matches(page->document));
            const auto error=quantapdf_render_page_with_options(page,&options,&held[slot]);
            if(error!=QUANTAPDF_OK)std::fprintf(stderr,"long path repeats=%zu primitive=%u clip=%u even=%d restore=%d zeros=%zu dpi=%u status=%d\n",repeats,primitive,clip,int(even),int(restore),zero_commands,dpi,int(error));
            CHECK(error==QUANTAPDF_OK);OK(quantapdf_bitmap_dimensions(held[slot],&w,&h,&stride,&components));
            CHECK(w==int(12u*dpi/72u) && h==w && stride==w*3 && components==3);
            OK(quantapdf_bitmap_data(held[slot],&data,&size));const auto expected=native_path_commands::rgb(sample,dpi);
            if(size==expected.size() && std::memcmp(data,expected.data(),size)!=0)for(size_t i=0u;i<size;++i)if(data[i]!=expected[i]) {
                std::fprintf(stderr,"long path repeats=%zu primitive=%u clip=%u even=%d layout=%u closing=%u encrypted=%d dpi=%u byte=%zu got=%u expected=%u\n",repeats,primitive,clip,int(even),layout,closing,int(encrypted),dpi,i,unsigned(data[i]),unsigned(expected[i]));break;
            }
            if(size!=expected.size() || std::memcmp(data,expected.data(),size)!=0) {
                CHECK(write_pdf(filename+".failed.pdf",sample.bytes));
                CHECK(write_pdf(filename+".opened.pdf",std::string(reinterpret_cast<const char*>(page->document->source_data),page->document->source_size)));
                CHECK(write_pdf(filename+".actual.rgb",std::string(reinterpret_cast<const char*>(data),size)));
                CHECK(write_pdf(filename+".expected.rgb",std::string(reinterpret_cast<const char*>(expected.data()),expected.size())));
                std::fprintf(stderr,"failed original/RGB saved: %s.failed.pdf, dpi=%u dimensions=%dx%d\n",filename.c_str(),dpi,w,h);
                for(auto bitmap:held)quantapdf_drop_bitmap(bitmap);
                quantapdf_drop_page(page);return EXIT_FAILURE;
            }
            ++slot;++renders;
        }
        quantapdf_drop_page(page);CHECK(finalized==0u);slot=0u;
        for(unsigned dpi:{72u,96u,144u}) {
            OK(quantapdf_bitmap_data(held[slot],&data,&size));const auto expected=native_path_commands::rgb(sample,dpi);
            CHECK(size==expected.size() && std::memcmp(data,expected.data(),size)==0);quantapdf_drop_bitmap(held[slot++]);
        }
        CHECK(finalized==1u);return EXIT_SUCCESS;
    };
    if(selection>=6u) {
        const unsigned first=4u*(selection-6u);
        for(unsigned kind=first;kind<first+4u;++kind)for(unsigned primitive:{0u,1u,2u,3u})
        for(unsigned clip:{0u,1u,2u})for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(1u,primitive,clip,even,true,encrypted,0u,kind)==EXIT_SUCCESS);
        if(selection==6u) {
            for(unsigned kind:{0u,1u,2u,3u,13u})for(unsigned clip:{0u,1u,2u})
            for(bool even:{false,true})for(bool encrypted:{false,true})
                CHECK(run(1u,0u,clip,even,false,encrypted,0u,kind)==EXIT_SUCCESS);
        }
        if(selection==8u) {
            for(unsigned kind:{4u,8u})for(unsigned primitive:{0u,1u,2u,3u})
            for(unsigned clip:{0u,1u})for(bool even:{false,true})for(bool encrypted:{false,true})
                CHECK(run(17u,primitive,clip,even,true,encrypted,0u,kind)==EXIT_SUCCESS);
            for(unsigned primitive:{1u,2u,3u})for(bool even:{false,true})for(bool encrypted:{false,true})
                CHECK(run(1u,primitive,1u,even,true,encrypted,0u,12u)==EXIT_SUCCESS);
        }
    } else if(selection==3u) {
        for(size_t count:{1u,2u,3u,17u,65u})for(unsigned layout:{0u,1u,3u})
        for(unsigned closing:{0u,1u,2u,3u})for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(count,0u,0u,even,true,encrypted,0u,layout,closing)==EXIT_SUCCESS);
        for(unsigned primitive:{1u,2u,3u})for(unsigned layout:{0u,3u})
        for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(17u,primitive,0u,even,true,encrypted,0u,layout,1u)==EXIT_SUCCESS);
        for(size_t count:{2u,3u,65u})for(unsigned primitive:{0u,1u,2u,3u})
        for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(count,primitive,0u,even,true,encrypted,0u,0u,0u,true)==EXIT_SUCCESS);
        for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(3u,1u,0u,even,true,encrypted,8192u,0u,1u)==EXIT_SUCCESS);
    } else if(selection==1u) {
        for(size_t count:{1u,2u})for(unsigned primitive:{0u,1u,2u,3u})
        for(unsigned clip:{0u,2u,4u,5u})for(unsigned closing:{0u,1u,2u})
        for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(count,primitive,clip,even,true,encrypted,0u,3u,closing)==EXIT_SUCCESS);
    } else if(selection==2u) {
        for(unsigned round=0u;round<16u;++round)for(unsigned layout:{0u,1u})
        for(unsigned closing:{0u,1u,2u})for(bool encrypted:{false,true})
            CHECK(run(65u,0u,4u,true,true,encrypted,0u,layout,closing)==EXIT_SUCCESS);
    } else if(multi) {
        // Keep the complete matrix in bounded CTest groups under the existing
        // timeout, including sanitizer runs. Each group owns its PDF filename.
        const size_t count=selection==4u?3u:selection==5u?65u:2u;
        for(unsigned layout:{0u,1u,2u})for(unsigned closing:{0u,1u,2u})
        for(unsigned clip:{0u,1u,2u,3u,4u})for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(count,0u,clip,even,true,encrypted,0u,layout,closing)==EXIT_SUCCESS);
        if(selection==0u) {
        for(unsigned primitive:{1u,2u,3u})for(unsigned layout:{0u,2u})for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(17u,primitive,1u,even,true,encrypted,0u,layout,1u)==EXIT_SUCCESS);
        for(size_t count:{2u,65u})for(unsigned layout:{0u,1u})for(unsigned clip:{0u,1u})
        for(bool even:{false,true})for(bool encrypted:{false,true})
            CHECK(run(count,0u,clip,even,true,encrypted,0u,layout,3u)==EXIT_SUCCESS);
        for(bool even:{false,true})for(bool encrypted:{false,true}) {
            CHECK(run(128u,0u,0u,even,false,encrypted,0u,0u,1u)==EXIT_SUCCESS);
            CHECK(run(3u,0u,1u,even,true,encrypted,8192u,0u,1u)==EXIT_SUCCESS);
        }
        // Reuse allocator addresses across fresh Context/Document owners at
        // the dense even-odd/intersection case observed during development.
        for(unsigned round=0u;round<6u;++round)for(unsigned layout:{0u,1u})
        for(unsigned closing:{0u,1u,2u})for(bool encrypted:{false,true})
            CHECK(run(65u,0u,4u,true,true,encrypted,0u,layout,closing)==EXIT_SUCCESS);
        }
    } else {
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
    }
    for(bool encrypted:{false,true})for(int invalid:{1,2,3,4,5}) {
        const auto sample=selection>=6u?native_path_commands::create_states(invalid==5?129u:17u,0u,0u,false,false,encrypted,0u,4u,invalid):multi?native_path_commands::create_contours(invalid==5?129u:17u,0u,0u,false,false,encrypted,0u,0u,1u,invalid,selection==3u):
            native_path_commands::create(invalid==5?129u:17u,0u,0u,false,false,encrypted,0u,invalid);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,1,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        CHECK(quantapdf_render_page(page,&bitmap)==(invalid>=4?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT) && bitmap==nullptr);
        quantapdf_drop_page(page);++failures;
    }
    std::printf("%s Fill paths: %zu independent RGB renders, m/l/c/v/y/h, f/f*, long winding/zero edges, Clip/Q, selected AES-256 pages, detached owners; %zu atomic errors\n",selection>=6u?"Path state":selection==3u?"Dynamic Clip":selection==1u?"Boundary":selection==2u?"Parity stress":selection==4u?"Three-contour":selection==5u?"Dense-contour":multi?"Multi-contour":"Dynamic",renders,failures);
    return EXIT_SUCCESS;
}
#endif
