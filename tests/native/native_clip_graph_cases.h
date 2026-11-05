#ifndef QUANTAPDF_NATIVE_CLIP_GRAPH_CASES_H
#define QUANTAPDF_NATIVE_CLIP_GRAPH_CASES_H
#include "native_clip_graph_fixture.h"
#include "document/document_internal.h"
#include <memory>

static int clip_graph_cases(const std::string& filename,unsigned group=0u)
{
    using bitmap_owner=std::unique_ptr<quantapdf_bitmap,decltype(&quantapdf_drop_bitmap)>;
    size_t renders=0u,errors=0u;
    const auto exercise=[&](native_clip_graph::options opt,bool encrypted)->int {
        const auto sample=native_clip_graph::create(opt,encrypted);
        if(opt.sparse && opt.leaves>=65u && !opt.empty) {
            auto truncated=sample;
            for(auto& p:truncated.paints)if(p.clips.size()==opt.leaves)p.clips.pop_back();
            for(unsigned dpi:{72u,96u,144u})
                CHECK(native_clip_graph::rgb(sample,dpi)!=native_clip_graph::rgb(truncated,dpi));
        }
        CHECK(write_pdf(filename,sample.bytes));
        size_t finalized=0u;quantapdf_context *raw_context=nullptr;
        OK(quantapdf_context_create(&raw_context));raw_context->test_finalizations=&finalized;
        std::unique_ptr<quantapdf_context,decltype(&quantapdf_drop_context)> context(raw_context,quantapdf_drop_context);
        quantapdf_document *raw_document=nullptr;
        if(encrypted)CHECK(quantapdf_open_with_context(context.get(),filename.c_str(),"wrong",&raw_document)==QUANTAPDF_ERROR_PASSWORD && raw_document==nullptr);
        OK(quantapdf_open_with_context(context.get(),filename.c_str(),encrypted?"reader":nullptr,&raw_document));
        std::unique_ptr<quantapdf_document,decltype(&quantapdf_close)> document(raw_document,quantapdf_close);
        CHECK(document->source_size==sample.bytes.size() && std::memcmp(document->source_data,sample.bytes.data(),sample.bytes.size())==0);
        CHECK(std::remove(filename.c_str())==0);
        quantapdf_page *raw_blank=nullptr;OK(quantapdf_load_page(document.get(),0,&raw_blank));
        std::unique_ptr<quantapdf_page,decltype(&quantapdf_drop_page)> blank(raw_blank,quantapdf_drop_page);
        quantapdf_bitmap *raw_empty=nullptr;OK(quantapdf_render_page(blank.get(),&raw_empty));
        bitmap_owner empty(raw_empty,quantapdf_drop_bitmap);blank.reset();
        int w=0,h=0,stride=0,components=0;const unsigned char *data=nullptr;size_t size=0u;
        OK(quantapdf_bitmap_dimensions(empty.get(),&w,&h,&stride,&components));
        OK(quantapdf_bitmap_data(empty.get(),&data,&size));CHECK(w==5 && h==4 && stride==15 && components==3 && size==60u);
        for(size_t i=0u;i<size;++i)CHECK(data[i]==255u);empty.reset();
        quantapdf_page *raw_page=nullptr;OK(quantapdf_load_page(document.get(),1,&raw_page));
        std::unique_ptr<quantapdf_page,decltype(&quantapdf_drop_page)> page(raw_page,quantapdf_drop_page);
        document.reset();context.reset();CHECK(finalized==0u);
        std::vector<bitmap_owner> held;
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *raw_bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(page.get(),&options,&raw_bitmap);
            bitmap_owner bitmap(raw_bitmap,quantapdf_drop_bitmap);
            if(status!=QUANTAPDF_OK) {
                std::fprintf(stderr,"Clip graph scene=%u order=%u primitive=%u empty=%d prefix=%d unused=%zu contours=%zu zeros=%zu encrypted=%d dpi=%u status=%d\n",opt.scene,opt.order,opt.primitive,int(opt.empty),int(opt.prefix),opt.unused,opt.contours,opt.zeros,int(encrypted),dpi,int(status));
                CHECK(write_pdf(filename+".failed.pdf",sample.bytes));return EXIT_FAILURE;
            }
            CHECK(bitmap!=nullptr);
            OK(quantapdf_bitmap_dimensions(bitmap.get(),&w,&h,&stride,&components));
            OK(quantapdf_bitmap_data(bitmap.get(),&data,&size));
            const auto expected=native_clip_graph::rgb(sample,dpi);
            if(size!=expected.size() || std::memcmp(data,expected.data(),size)!=0) {
                for(size_t i=0u;i<std::min(size,expected.size());++i)if(data[i]!=expected[i]) {
                    std::fprintf(stderr,"Clip graph RGB scene=%u order=%u primitive=%u empty=%d prefix=%d unused=%zu contours=%zu dpi=%u byte=%zu got=%u expected=%u\n",opt.scene,opt.order,opt.primitive,int(opt.empty),int(opt.prefix),opt.unused,opt.contours,dpi,i,unsigned(data[i]),unsigned(expected[i]));break;
                }
                CHECK(write_pdf(filename+".failed.pdf",sample.bytes));
                CHECK(write_pdf(filename+".actual.rgb",std::string(reinterpret_cast<const char*>(data),size)));
                CHECK(write_pdf(filename+".expected.rgb",std::string(reinterpret_cast<const char*>(expected.data()),expected.size())));
                return EXIT_FAILURE;
            }
            CHECK(w==int(12u*dpi/72u) && h==w && stride==w*3 && components==3);
            held.push_back(std::move(bitmap));++renders;
        }
        page.reset();CHECK(finalized==0u);
        for(size_t i=0u;i<held.size();++i) {
            OK(quantapdf_bitmap_data(held[i].get(),&data,&size));
            const auto expected=native_clip_graph::rgb(sample,i==0u?72u:i==1u?96u:144u);
            CHECK(size==expected.size() && std::memcmp(data,expected.data(),size)==0);held[i].reset();
        }
        CHECK(finalized==1u);return EXIT_SUCCESS;
    };
    if(group==0u) {
        for(unsigned scene:{0u,1u,2u,3u})for(unsigned order=0u;order<6u;++order)
        for(unsigned primitive:{0u,1u})for(bool encrypted:{false,true}) {
            native_clip_graph::options opt;opt.scene=scene;opt.order=order;opt.primitive=primitive;
            opt.leaves=scene==1u?3u:4u;opt.prefix=order%2u==0u;
            CHECK(exercise(opt,encrypted)==EXIT_SUCCESS);
        }
    } else if(group==1u) {
        for(unsigned scene:{0u,1u,2u,3u})for(unsigned order:{1u,4u})
        for(unsigned primitive:{2u,3u,4u})for(bool empty:{false,true})for(bool encrypted:{false,true}) {
            native_clip_graph::options opt;opt.scene=scene;opt.order=order;opt.primitive=primitive;
            opt.leaves=scene==2u?3u:4u;opt.empty=empty;opt.prefix=order%2u==0u;
            CHECK(exercise(opt,encrypted)==EXIT_SUCCESS);
        }
    } else if(group==2u) {
        for(unsigned scene:{0u,2u})for(unsigned primitive:{0u,1u,2u,3u,4u})for(bool encrypted:{false,true}) {
            native_clip_graph::options opt;opt.scene=scene;opt.primitive=primitive;opt.order=2u;
            opt.contours=65u;opt.zeros=primitive==2u?8192u:0u;
            CHECK(exercise(opt,encrypted)==EXIT_SUCCESS);
        }
        for(size_t unused:{255u,257u})for(bool encrypted:{false,true}) {
            native_clip_graph::options opt;opt.scene=3u;opt.unused=unused;
            CHECK(exercise(opt,encrypted)==EXIT_SUCCESS);
        }
        for(size_t depth:{129u,4097u})for(bool encrypted:{false,true}) {
            native_clip_graph::options opt;opt.scene=2u;opt.depth=depth;
            CHECK(exercise(opt,encrypted)==EXIT_SUCCESS);
        }
        for(bool encrypted:{false,true}) {
            native_clip_graph::options opt;opt.leaves=5u;
            CHECK(exercise(opt,encrypted)==EXIT_SUCCESS);
        }
    } else {
        for(unsigned leaves:{5u,9u,17u,33u,65u})for(unsigned primitive:{0u,1u,4u})
        for(bool empty:{false,true})for(bool encrypted:{false,true}) {
            native_clip_graph::options opt;opt.leaves=leaves;opt.primitive=primitive;
            opt.sparse=true;opt.empty=empty;opt.order=leaves%6u;
            CHECK(exercise(opt,encrypted)==EXIT_SUCCESS);
        }
        for(unsigned scene:{1u,2u})for(unsigned primitive:{0u,2u})for(bool encrypted:{false,true}) {
            native_clip_graph::options opt;opt.scene=scene;opt.leaves=9u;opt.primitive=primitive;
            CHECK(exercise(opt,encrypted)==EXIT_SUCCESS);
        }
    }
    if(group==0u)for(bool encrypted:{false,true})for(int invalid=1;invalid<=5;++invalid) {
        native_clip_graph::options opt;opt.scene=0u;
        const auto sample=native_clip_graph::create(opt,encrypted,invalid);
        CHECK(write_pdf(filename,sample.bytes));quantapdf_document *raw_document=nullptr;
        OK(quantapdf_open(filename.c_str(),encrypted?"reader":nullptr,&raw_document));
        std::unique_ptr<quantapdf_document,decltype(&quantapdf_close)> document(raw_document,quantapdf_close);
        CHECK(std::remove(filename.c_str())==0);quantapdf_page *raw_page=nullptr;
        OK(quantapdf_load_page(document.get(),1,&raw_page));
        std::unique_ptr<quantapdf_page,decltype(&quantapdf_drop_page)> page(raw_page,quantapdf_drop_page);document.reset();
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(page.get(),&options,&bitmap);
            CHECK(status==(invalid>=1 && invalid<=3?QUANTAPDF_ERROR_FORMAT:QUANTAPDF_ERROR_UNSUPPORTED) && bitmap==nullptr);++errors;
        }
    }
    CHECK(renders==(group==3u?204u:group==2u?90u:288u) && errors==(group==0u?30u:0u));
    std::printf("Clip graph group %u: %zu independent RGB renders, nested/sibling/fan-out/mixed roots, real Text/Fill/Image, definition CTM, wide identities, selected encrypted split pages and detached Bitmaps; %zu atomic errors\n",group,renders,errors);
    return EXIT_SUCCESS;
}
#endif
