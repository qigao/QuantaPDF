#ifndef QUANTAPDF_NATIVE_PAGE_TEXT_CASES_H
#define QUANTAPDF_NATIVE_PAGE_TEXT_CASES_H
#include "native_page_text_fixture.h"
#include <memory>

static int page_text_curve_cases(const std::string& filename)
{
    size_t renders=0u,turns=0u;
    for(unsigned kind=0u;kind<7u;++kind)for(int rotation:{0,450,-180,-90})
    for(bool crop:{false,true})for(bool encrypted:{false,true}) {
        const auto sample=native_page_text::create(kind,rotation,crop,encrypted);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *raw_document=nullptr;
        if(encrypted)CHECK(quantapdf_open(filename.c_str(),"wrong",&raw_document)==QUANTAPDF_ERROR_PASSWORD && raw_document==nullptr);
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&raw_document));
        std::unique_ptr<quantapdf_document,decltype(&quantapdf_close)> document(raw_document,quantapdf_close);
        CHECK(std::remove(filename.c_str())==0);quantapdf_page *raw_page=nullptr;
        OK(quantapdf_load_page(document.get(),0,&raw_page));
        std::unique_ptr<quantapdf_page,decltype(&quantapdf_drop_page)> page(raw_page,quantapdf_drop_page);document.reset();
        quantapdf_rect bounds{};OK(quantapdf_page_bounds(page.get(),&bounds));
        CHECK(bounds.x0==0.0f && bounds.y0==0.0f && bounds.x1==sample.width_points && bounds.y1==sample.height_points);
        char *text=nullptr;size_t text_size=0u;OK(quantapdf_extract_text(page.get(),&text,&text_size));
        CHECK(std::string(text,text_size)=="Hello");quantapdf_free(text);
        std::vector<std::unique_ptr<quantapdf_bitmap,decltype(&quantapdf_drop_bitmap)>> held;
        std::vector<std::vector<unsigned char>> expected;
        for(unsigned display:{0u,90u,180u,270u}) {
            const auto reference=native_page_text::turn(sample.expected,sample.width,sample.height,display);
            const bool swap=display==90u || display==270u;
            const size_t w=swap?sample.height:sample.width,h=swap?sample.width:sample.height;
            const quantapdf_render_options options{float(sample.dpi),float(display),0,{},0};
            quantapdf_bitmap *raw=nullptr;
            const auto status=quantapdf_render_page_with_options(page.get(),&options,&raw);
            std::unique_ptr<quantapdf_bitmap,decltype(&quantapdf_drop_bitmap)> bitmap(raw,quantapdf_drop_bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"curved page text kind=%u rotate=%d crop=%d encrypted=%d dpi=%u display=%u status=%d\n",kind,rotation,int(crop),int(encrypted),sample.dpi,display,int(status));
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int width=0,height=0,stride=0,components=0;const unsigned char *pixels=nullptr;size_t bytes=0u;
            OK(quantapdf_bitmap_dimensions(bitmap.get(),&width,&height,&stride,&components));
            OK(quantapdf_bitmap_data(bitmap.get(),&pixels,&bytes));
            CHECK(width==int(w) && height==int(h) && stride==3*width && components==3 && bytes==reference.size());
            if(std::memcmp(pixels,reference.data(),bytes)!=0) {
                for(size_t i=0u;i<bytes;++i)if(pixels[i]!=reference[i]) {
                    std::fprintf(stderr,"curved page text RGB kind=%u rotate=%d crop=%d dpi=%u display=%u byte=%zu got=%u expected=%u\n",kind,rotation,int(crop),sample.dpi,display,i,unsigned(pixels[i]),unsigned(reference[i]));break;
                }
                return EXIT_FAILURE;
            }
            held.push_back(std::move(bitmap));expected.push_back(reference);
            if(display==0u)++renders;else ++turns;
        }
        page.reset();
        for(size_t i=0u;i<held.size();++i) {
            const unsigned char *pixels=nullptr;size_t bytes=0u;OK(quantapdf_bitmap_data(held[i].get(),&pixels,&bytes));
            CHECK(bytes==expected[i].size() && std::memcmp(pixels,expected[i].data(),bytes)==0);
        }
    }
    CHECK(renders==112u && turns==336u);
    std::printf("Original curved Hello: %zu independent RGB results and %zu display turns, inherited signed Crop/Rotate, seven UserUnits, password and detached Bitmaps\n",renders,turns);
    return EXIT_SUCCESS;
}
#endif
