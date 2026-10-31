#ifndef QUANTAPDF_NATIVE_TTF_PUBLIC_CASES_H
#define QUANTAPDF_NATIVE_TTF_PUBLIC_CASES_H
#include "native_ttf_fixture.h"
#include <quantapdf/quantapdf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
inline bool write_native_ttf_pdf(const std::string& filename,const std::string& data)
{std::ofstream file(filename,std::ios::binary);file.write(data.data(),static_cast<std::streamsize>(data.size()));return file.good();}
static std::string composer_ttf_original()
{
    const auto require=[](quantapdf_status s) {if(s!=QUANTAPDF_OK)throw std::runtime_error("Composer TTF roundtrip failed");};
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/"fixtures"/"fonts"/"Roboto-Regular.ttf";
    std::ifstream file(path,std::ios::binary);const std::vector<unsigned char> font{std::istreambuf_iterator<char>(file),{}};
    quantapdf_composer *composer=nullptr;quantapdf_output *output=nullptr;
    quantapdf_composer_options limits{};limits.struct_size=sizeof(limits);
    limits.max_pages=1u;limits.max_operations=4u;limits.max_resource_bytes=2000000u;limits.max_navigation_items=1u;
    require(quantapdf_composer_create(&limits,&composer));
    quantapdf_composer_page_options page{sizeof(page),128.0f,64.0f,UINT32_C(0xffdfdfdf)};size_t index=0u;
    require(quantapdf_composer_add_page(composer,&page,&index));
    quantapdf_composer_font_id id=0u;quantapdf_composer_font_options options{sizeof(options)};
    require(quantapdf_composer_add_font(composer,font.data(),font.size(),&options,&id));
    quantapdf_composer_embedded_text_options text{};text.struct_size=sizeof(text);text.font_id=id;
    text.font_size=24.0f;text.argb=UINT32_C(0xff000000);text.line_height_multiplier=1.0f;
    const quantapdf_rect bounds{12.0f,20.0f,120.0f,60.0f};
    require(quantapdf_composer_draw_embedded_text(composer,index,"Hello",&bounds,&text));
    require(quantapdf_composer_finish(composer,&output));
    const unsigned char *bytes=nullptr;size_t size=0u;require(quantapdf_output_data(output,&bytes,&size));
    const std::string result(reinterpret_cast<const char*>(bytes),size);
    quantapdf_drop_output(output);quantapdf_drop_composer(composer);return result;
}

static int native_ttf_public_cases(const std::string& filename,int first=0,int last=31)
{
    const auto goldens=std::filesystem::path(__FILE__).parent_path()/"goldens";
    quantapdf_context *ctx=nullptr;OK(quantapdf_context_create(&ctx));
    /* Each positive is a fresh ORIGINAL; this restores real text observation
     * coverage in the existing native-only public target as well. */
    for(int mode=first;mode<=last;++mode) {
        const bool positive=mode<=9 || mode==11 || (mode>=32 && mode<=34) || mode==44;
        const auto original=mode==44?composer_ttf_original():native_ttf_fixture(mode);
        CHECK(write_native_ttf_pdf(filename,original));
        quantapdf_document *doc=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open_with_context(ctx,filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_text_page *text=nullptr;
        if(mode==0 || mode==4 || mode==44) {
            char *utf8=nullptr;size_t size=0u;
            OK(quantapdf_extract_text(page,&utf8,&size));
            CHECK(utf8!=nullptr && std::string(utf8,size)=="Hello");
            quantapdf_free(utf8);
            OK(quantapdf_extract_structured_text(page,&text));
            size_t count=0u;OK(quantapdf_text_block_count(text,&count));CHECK(count==1u);
            OK(quantapdf_text_line_count(text,0u,&count));CHECK(count==1u);
            OK(quantapdf_text_span_count(text,0u,0u,&count));CHECK(count==1u);
            const char *span=nullptr;size_t n=0u;
            OK(quantapdf_text_span_text(text,0u,0u,0u,&span,&n));
            CHECK(span!=nullptr && std::string(span,n)=="Hello");
            quantapdf_search_result hit{};hit.struct_size=sizeof(hit);
            OK(quantapdf_text_search(text,"ell",&hit,1u,&count));CHECK(count==1u);
        }
        quantapdf_bitmap *held=nullptr;std::vector<unsigned char> retained;
        for(int dpi:{72,96,144}) {
            quantapdf_render_options options{};options.struct_size=sizeof(options);options.dpi=(float)dpi;
            quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(!positive) {
                CHECK(status!=QUANTAPDF_OK && bitmap==nullptr);
                CHECK(status==(mode==13 || mode==17 || mode==18 || mode==27 || mode==30 || mode==31 || mode==39?QUANTAPDF_ERROR_FORMAT:QUANTAPDF_ERROR_UNSUPPORTED));
                continue;
            }
            if(status!=QUANTAPDF_OK || bitmap==nullptr)
                std::fprintf(stderr,"TTF public mode=%d dpi=%d status=%d bitmap=%p\n",mode,dpi,(int)status,(void*)bitmap);
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,components=0;const unsigned char *pixels=nullptr;size_t n=0u;
            OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));
            OK(quantapdf_bitmap_data(bitmap,&pixels,&n));
            CHECK(w==(128*dpi+71)/72 && h==(64*dpi+71)/72 && stride==w*3 && components==3 && n==(size_t)stride*(size_t)h);
            if(mode==3) {for(size_t i=0u;i<n;++i)CHECK(pixels[i]==255u);}
            else {
                const char *name=mode==11?"late":mode==32?"mixed":mode==33?"reverse":(mode==34 || mode==44)?"background":mode==1?"layers":mode==2?"tj":mode==5?"fringe":"hello";
                std::ifstream file(goldens/(std::string("roboto-")+name+"-"+std::to_string(dpi)+(mode==11 || mode>=32?"-m1.rgba":"-m0.rgba")),std::ios::binary);
                const std::vector<unsigned char> expected{std::istreambuf_iterator<char>(file),{}};
                CHECK(expected.size()==(size_t)w*(size_t)h*4u);
                for(size_t i=0u;i<(size_t)w*(size_t)h;++i) {
                    if(std::memcmp(pixels+3u*i,expected.data()+4u*i,3u)!=0) {
                        std::fprintf(stderr,"TTF RGB mismatch mode=%d dpi=%d pixel=%zu\n",mode,dpi,i);return EXIT_FAILURE;
                    }
                }
            }
            quantapdf_bitmap *repeat=nullptr;OK(quantapdf_render_page_with_options(page,&options,&repeat));
            const unsigned char *again=nullptr;size_t again_n=0u;
            OK(quantapdf_bitmap_data(repeat,&again,&again_n));
            CHECK(again_n==n && std::memcmp(again,pixels,n)==0);quantapdf_drop_bitmap(repeat);
            if(dpi==96) {held=bitmap;retained.assign(pixels,pixels+n);}else quantapdf_drop_bitmap(bitmap);
        }
        quantapdf_bitmap *small=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        const auto thumb=quantapdf_render_thumbnail(page,64,32,&small);
        if(positive) {CHECK(thumb==QUANTAPDF_OK && small!=nullptr);quantapdf_drop_bitmap(small);}
        else CHECK(thumb!=QUANTAPDF_OK && small==nullptr);
        quantapdf_drop_page(page);quantapdf_close(doc);
        if(text!=nullptr) {
            size_t count=0u;OK(quantapdf_text_search(text,"Hello",nullptr,0u,&count));CHECK(count==1u);
            quantapdf_drop_text_page(text);
        }
        if(held!=nullptr) {
            const unsigned char *pixels=nullptr;size_t n=0u;OK(quantapdf_bitmap_data(held,&pixels,&n));
            CHECK(n==retained.size() && std::memcmp(pixels,retained.data(),n)==0);
            quantapdf_drop_bitmap(held);
        }
    }
    /* A refused text page cannot poison a reopened good original on the
     * same Context. Its final Bitmap also outlives that Context. */
    CHECK(write_native_ttf_pdf(filename,native_ttf_fixture()));
    quantapdf_document *doc=nullptr;quantapdf_page *page=nullptr;quantapdf_bitmap *bm=nullptr;
    OK(quantapdf_open_with_context(ctx,filename.c_str(),nullptr,&doc));
    CHECK(std::remove(filename.c_str())==0);OK(quantapdf_load_page(doc,0,&page));OK(quantapdf_render_page(page,&bm));
    const unsigned char *pixels=nullptr;size_t n=0u;OK(quantapdf_bitmap_data(bm,&pixels,&n));
    const std::vector<unsigned char> stable(pixels,pixels+n);
    quantapdf_drop_page(page);quantapdf_close(doc);quantapdf_drop_context(ctx);
    OK(quantapdf_bitmap_data(bm,&pixels,&n));CHECK(n==stable.size() && std::memcmp(pixels,stable.data(),n)==0);
    quantapdf_drop_bitmap(bm);return EXIT_SUCCESS;
}

#endif
