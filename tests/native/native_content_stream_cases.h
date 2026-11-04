#ifndef QUANTAPDF_NATIVE_CONTENT_STREAM_CASES_H
#define QUANTAPDF_NATIVE_CONTENT_STREAM_CASES_H
#include "native_content_stream_fixture.h"

static int content_stream_bitmap(quantapdf_bitmap *bitmap,unsigned dpi)
{
    int width=0,height=0,stride=0,components=0;
    const unsigned char *pixels=nullptr;size_t size=0u;
    OK(quantapdf_bitmap_dimensions(bitmap,&width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
    CHECK(width==int(12u*dpi/72u) && height==int(9u*dpi/72u) &&
        stride==width*3 && components==3 && size==size_t(stride)*size_t(height));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        CHECK(probe_rgb(pixels,stride,x,y,x<width/2?0u:255u,0u,x<width/2?255u:0u));
    return EXIT_SUCCESS;
}
static int content_stream_cases(const std::string& filename)
{
    size_t renders=0u;
    for(unsigned encoding=0u;encoding<8u;++encoding)
    for(bool encrypted:{false,true})for(size_t count:{1u,3u,17u}) {
        const size_t payload=encoding==0u && count==1u?3u*1024u*1024u:0u;
        const auto sample=native_content_fixture::create(encoding,count,encrypted,0,payload);
        CHECK(payload==0u || sample.bytes.size()>payload);
        CHECK(count!=17u || sample.decoded_bytes>65536u);
        CHECK(write_pdf(filename,sample.bytes));
        quantapdf_context *context=nullptr;quantapdf_document *document=nullptr;
        quantapdf_page *page=nullptr;size_t finalized=0u;
        OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,0,&page));
        quantapdf_close(document);quantapdf_drop_context(context);
        std::vector<quantapdf_bitmap*> held;
        for(unsigned dpi:{72u,96u,144u}) {
            const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            quantapdf_bitmap *bitmap=nullptr;
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"content stream encoding=%u count=%zu encrypted=%d dpi=%u status=%d\n",encoding,count,int(encrypted),dpi,int(status));
            CHECK(status==QUANTAPDF_OK && content_stream_bitmap(bitmap,dpi)==EXIT_SUCCESS);
            held.push_back(bitmap);++renders;
        }
        quantapdf_drop_page(page);CHECK(finalized==0u);
        for(size_t i=0u;i<held.size();++i) {
            CHECK(content_stream_bitmap(held[i],i==0u?72u:i==1u?96u:144u)==EXIT_SUCCESS);
            quantapdf_drop_bitmap(held[i]);
        }
        CHECK(finalized==1u);
    }
    for(bool encrypted:{false,true})for(int invalid:{1,2,3,4,5,6,7}) {
        const auto sample=native_content_fixture::create(5u,3u,encrypted,invalid);
        CHECK(write_pdf(filename,sample.bytes));
        quantapdf_document *document=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,0,&page));quantapdf_close(document);
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        const auto status=quantapdf_render_page(page,&bitmap);
        const auto expected=(invalid==2 || invalid==6)?QUANTAPDF_ERROR_UNSUPPORTED:QUANTAPDF_ERROR_FORMAT;
        if(status!=expected)std::fprintf(stderr,"invalid content case=%d encrypted=%d status=%d expected=%d\n",invalid,int(encrypted),int(status),int(expected));
        CHECK(status==expected && bitmap==nullptr);quantapdf_drop_page(page);
    }
    CHECK(renders==144u);
    std::puts("Original qpdf Contents: RAW/Flate/ASCIIHex/ASCII85/RunLength/filter chains, 17 streams, AES-256, full RGB and atomic errors passed");
    return EXIT_SUCCESS;
}
#endif
