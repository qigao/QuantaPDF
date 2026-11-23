#ifndef QUANTAPDF_NATIVE_TTF_GRAPH_CASES_H
#define QUANTAPDF_NATIVE_TTF_GRAPH_CASES_H
#include "native_ttf_graph_reference.h"
#include <algorithm>
#include <memory>

static int ttf_clip_graph_originals(const std::string& filename)
{
    using bitmap_owner=std::unique_ptr<quantapdf_bitmap,decltype(&quantapdf_drop_bitmap)>;
    quantapdf_context *raw_context=nullptr;OK(quantapdf_context_create(&raw_context));
    std::unique_ptr<quantapdf_context,decltype(&quantapdf_drop_context)> context(raw_context,quantapdf_drop_context);
    struct held {bitmap_owner bitmap;std::vector<unsigned char> expected;unsigned dpi;};
    std::vector<held> retained;
    size_t analytic=0u,equivalent=0u;
    const auto render=[&](int mode,unsigned dpi,bool normalized,bitmap_owner& bitmap)->int {
        CHECK(write_pdf(filename,native_ttf_fixture(mode,normalized)));
        quantapdf_document *raw_document=nullptr;
        OK(quantapdf_open_with_context(context.get(),filename.c_str(),nullptr,&raw_document));
        std::unique_ptr<quantapdf_document,decltype(&quantapdf_close)> document(raw_document,quantapdf_close);
        CHECK(std::remove(filename.c_str())==0);quantapdf_page *raw_page=nullptr;
        OK(quantapdf_load_page(document.get(),0,&raw_page));
        std::unique_ptr<quantapdf_page,decltype(&quantapdf_drop_page)> page(raw_page,quantapdf_drop_page);
        document.reset();quantapdf_render_options options{};options.dpi=float(dpi);
        quantapdf_bitmap *raw_bitmap=nullptr;
        const auto status=quantapdf_render_page_with_options(page.get(),&options,&raw_bitmap);
        bitmap.reset(raw_bitmap);
        if(status!=QUANTAPDF_OK)std::fprintf(stderr,"Clip graph original mode=%d dpi=%u normalized=%d status=%d\n",mode,dpi,int(normalized),int(status));
        CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);return EXIT_SUCCESS;
    };
    const auto matches=[](const held& sample)->bool {
        int w=0,h=0,stride=0,components=0;const unsigned char *bytes=nullptr;size_t size=0;
        return quantapdf_bitmap_dimensions(sample.bitmap.get(),&w,&h,&stride,&components)==QUANTAPDF_OK &&
            quantapdf_bitmap_data(sample.bitmap.get(),&bytes,&size)==QUANTAPDF_OK &&
            w==int((128u*sample.dpi+71u)/72u) && h==int((64u*sample.dpi+71u)/72u) &&
            stride==w*3 && components==3 && size==sample.expected.size() &&
            std::memcmp(bytes,sample.expected.data(),size)==0;
    };
    for(int mode=0;mode<170;++mode) {
        if(!native_ttf_graph_supported(mode))continue;
        for(unsigned dpi:{72u,96u,144u}) {
            bitmap_owner bitmap(nullptr,quantapdf_drop_bitmap);
            CHECK(render(mode,dpi,false,bitmap)==EXIT_SUCCESS);
            std::vector<unsigned char> expected;
            if(native_ttf_graph_equivalent(mode)) {
                bitmap_owner peer(nullptr,quantapdf_drop_bitmap);
                CHECK(render(mode,dpi,true,peer)==EXIT_SUCCESS);
                const unsigned char *bytes=nullptr;size_t size=0;
                OK(quantapdf_bitmap_data(peer.get(),&bytes,&size));expected.assign(bytes,bytes+size);++equivalent;
            } else {expected=native_ttf_graph_expected(mode,dpi).rgb;++analytic;}
            CHECK(!expected.empty());held sample{std::move(bitmap),std::move(expected),dpi};
            if(!matches(sample)) {
                const unsigned char *bytes=nullptr;size_t size=0;OK(quantapdf_bitmap_data(sample.bitmap.get(),&bytes,&size));
                for(size_t i=0;i<std::min(size,sample.expected.size());++i)if(bytes[i]!=sample.expected[i]) {
                    std::fprintf(stderr,"Clip graph original RGB mode=%d dpi=%u byte=%zu got=%u expected=%u\n",mode,dpi,i,unsigned(bytes[i]),unsigned(sample.expected[i]));break;
                }
                return EXIT_FAILURE;
            }
            retained.push_back(std::move(sample));
        }
    }
    context.reset();
    for(const auto& sample:retained)CHECK(matches(sample));
    CHECK(analytic==90u && equivalent==12u && retained.size()==102u);
    std::printf("Promoted original Clip graphs: %zu independent full RGB, %zu authored geometry equivalents; 102 detached Bitmaps at three DPI\n",analytic,equivalent);
    return EXIT_SUCCESS;
}
#endif
