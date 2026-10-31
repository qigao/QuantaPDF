#ifndef QUANTAPDF_NATIVE_TTF_PUBLIC_CASES_H
#define QUANTAPDF_NATIVE_TTF_PUBLIC_CASES_H
#include "native_ttf_fixture.h"
#include <quantapdf/quantapdf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
inline bool write_native_ttf_pdf(const std::string& filename,const std::string& data)
{std::ofstream file(filename,std::ios::binary);file.write(data.data(),static_cast<std::streamsize>(data.size()));return file.good();}
/* Inspect the serialized program, not a reconstructed test font. */
static bool composer_cid_program_valid(const std::string& original,bool glyph_run)
{
    auto pdf=QPDF::create();pdf->processMemoryFile("Composer CID program",original.data(),original.size());
    auto page=pdf->getRoot().getKey("/Pages").getKey("/Kids").getArrayItem(0);
    auto fonts=page.getKey("/Resources").getKey("/Font").getDictAsMap();
    if(fonts.size()!=1u)return false;
    auto font=fonts.begin()->second;
    if(!font.getKey("/Subtype").isNameAndEquals("/Type0") ||
       !font.getKey("/ToUnicode").isStream())return false;
    auto cid=font.getKey("/DescendantFonts").getArrayItem(0);
    if(!cid.getKey("/Subtype").isNameAndEquals("/CIDFontType2"))return false;
    auto map=cid.getKey("/CIDToGIDMap");
    if(glyph_run?!map.isStream():!map.isNameAndEquals("/Identity"))return false;
    if(glyph_run) {
        const auto mapping=map.getStreamData(qpdf_dl_generalized);
        if(mapping->getSize()!=4u || mapping->getBuffer()[2]!=0u || mapping->getBuffer()[3]!=45u)
            return false;
    }
    auto program=cid.getKey("/FontDescriptor").getKey("/FontFile2");
    if(!program.isStream())return false;
    const auto data=program.getStreamData(qpdf_dl_generalized);
    auto length=program.getDict().getKey("/Length1");
    if(!length.isInteger() || length.getIntValue()<0 ||
       (size_t)length.getIntValue()!=data->getSize() || data->getSize()<12u || data->getSize()%4u!=0u)
        return false;
    const auto p=data->getBuffer();
    const size_t count=(size_t)p[4]*256u+p[5];
    if(count>(data->getSize()-12u)/16u)return false;
    for(size_t i=0u;i<count;++i)
        if(std::memcmp(p+12u+16u*i,"cmap",4u)==0)return false;
    uint32_t sum=0u;
    for(size_t at=0u;at<data->getSize();at+=4u) {
        uint32_t word=0u;for(size_t j=0u;j<4u;++j)word=(word<<8u)|p[at+j];sum+=word;
    }
    return sum==UINT32_C(0xb1b0afba);
}

static std::string composer_ttf_original(bool glyph_run=false,bool cycle=false)
{
    const auto require=[](quantapdf_status s) {if(s!=QUANTAPDF_OK)throw std::runtime_error("Composer TTF roundtrip failed");};
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/"fixtures"/"fonts"/"Roboto-Regular.ttf";
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> font{std::istreambuf_iterator<char>(file),{}};
    if(cycle) {
        /* Original composite e-acute refers to itself. Registration checks
         * cmap/metrics; subset component closure must reject at finish. */
        const auto u16=[&](size_t at) {return (unsigned)font.at(at)*256u+font.at(at+1u);};
        const auto u32=[&](size_t at) {return (size_t)u16(at)*65536u+u16(at+2u);};
        size_t glyf=0u,loca=0u,head=0u;
        for(unsigned i=0u;i<u16(4u);++i) {
            const size_t at=12u+16u*i;
            if(std::memcmp(font.data()+at,"glyf",4u)==0)glyf=u32(at+8u);
            if(std::memcmp(font.data()+at,"loca",4u)==0)loca=u32(at+8u);
            if(std::memcmp(font.data()+at,"head",4u)==0)head=u32(at+8u);
        }
        const size_t glyph=glyf+(u16(head+50u)==0u?2u*u16(loca+2317u*2u):u32(loca+2317u*4u));
        font.at(glyph+12u)=0x09u;font.at(glyph+13u)=0x0du;
    }
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
    if(glyph_run) {
        const quantapdf_composer_glyph glyph={cycle?2317u:45u,713.0f,0.0f,0.0f,0.0f,0u,cycle?2u:1u};
        quantapdf_composer_glyph_run_options run{};run.struct_size=sizeof(run);run.font_id=id;
        run.font_size=24.0f;run.argb=UINT32_C(0xff000000);
        const quantapdf_point origin{12.0f,44.0f};
        require(quantapdf_composer_draw_glyph_run(composer,index,origin,&glyph,1u,cycle?"\xc3\xa9":"H",cycle?2u:1u,&run));
    } else require(quantapdf_composer_draw_embedded_text(composer,index,cycle?"\xc3\xa9":"Hello",&bounds,&text));
    output=reinterpret_cast<quantapdf_output*>(uintptr_t(1));
    const auto finished=quantapdf_composer_finish(composer,&output);
    if(cycle) {
        const bool rejected=finished==QUANTAPDF_ERROR_FORMAT && output==nullptr;
        if(finished==QUANTAPDF_OK)quantapdf_drop_output(output);
        quantapdf_drop_composer(composer);
        if(!rejected)throw std::runtime_error("cyclic CID font published an output");
        return {};
    }
    require(finished);
    const unsigned char *bytes=nullptr;size_t size=0u;require(quantapdf_output_data(output,&bytes,&size));
    const std::string result(reinterpret_cast<const char*>(bytes),size);
    quantapdf_drop_output(output);quantapdf_drop_composer(composer);
    if(!composer_cid_program_valid(result,glyph_run))throw std::runtime_error("nonconforming Composer CID FontFile2");
    return result;
}

static int native_ttf_public_cases(const std::string& filename,int first=0,int last=31)
{
    if(first==32) {CHECK(composer_ttf_original(false,true).empty());CHECK(composer_ttf_original(true,true).empty());}
    const auto goldens=std::filesystem::path(__FILE__).parent_path()/"goldens";
    quantapdf_context *ctx=nullptr;OK(quantapdf_context_create(&ctx));
    /* Each positive is a fresh ORIGINAL; this restores real text observation
     * coverage in the existing native-only public target as well. */
    for(int mode=first;mode<=last;++mode) {
        const bool positive=mode<=9 || mode==11 || (mode>=32 && mode<=34) || mode==44 || mode==49;
        const auto original=(mode==44 || mode==49)?composer_ttf_original(mode==49):native_ttf_fixture(mode);
        const char *text_value=mode==49?"H":"Hello";
        CHECK(write_native_ttf_pdf(filename,original));
        quantapdf_document *doc=nullptr;quantapdf_page *page=nullptr;
        OK(quantapdf_open_with_context(ctx,filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_text_page *text=nullptr;
        if(mode==0 || mode==4 || mode==44 || mode==49) {
            char *utf8=nullptr;size_t size=0u;
            OK(quantapdf_extract_text(page,&utf8,&size));
            CHECK(utf8!=nullptr && std::string(utf8,size)==text_value);
            quantapdf_free(utf8);
            OK(quantapdf_extract_structured_text(page,&text));
            size_t count=0u;OK(quantapdf_text_block_count(text,&count));CHECK(count==1u);
            OK(quantapdf_text_line_count(text,0u,&count));CHECK(count==1u);
            OK(quantapdf_text_span_count(text,0u,0u,&count));CHECK(count==1u);
            const char *span=nullptr;size_t n=0u;
            OK(quantapdf_text_span_text(text,0u,0u,0u,&span,&n));
            CHECK(span!=nullptr && std::string(span,n)==text_value);
            quantapdf_search_result hit{};hit.struct_size=sizeof(hit);
            OK(quantapdf_text_search(text,mode==49?"H":"ell",&hit,1u,&count));CHECK(count==1u);
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
                const char *name=mode==11?"late":mode==32?"mixed":mode==33?"reverse":(mode==34 || mode==44 || mode==49)?"background":mode==1?"layers":mode==2?"tj":mode==5?"fringe":"hello";
                std::ifstream file(goldens/(std::string("roboto-")+name+"-"+std::to_string(dpi)+(mode==11 || mode>=32?"-m1.rgba":"-m0.rgba")),std::ios::binary);
                const std::vector<unsigned char> expected{std::istreambuf_iterator<char>(file),{}};
                CHECK(expected.size()==(size_t)w*(size_t)h*4u);
                for(size_t i=0u;i<(size_t)w*(size_t)h;++i) {
                    /* H ends before x=29pt; e starts after it. Reuse the
                     * independently authored H region. Elsewhere the gray
                     * rectangle still has the original physical viewport's
                     * fractional right/bottom coverage, notably at 96 DPI. */
                    if(mode==49 && i%(size_t)w>=(size_t)(29*dpi/72)) {
                        const size_t x=i%(size_t)w,y=i/(size_t)w;
                        unsigned covered=0u;
                        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx)
                            if((8u*x+2u*sx+1u)*72u<(size_t)8u*128u*(size_t)dpi &&
                               (8u*y+2u*sy+1u)*72u<(size_t)8u*64u*(size_t)dpi)++covered;
                        const unsigned gray=(0xdfu*covered+255u*(16u-covered)+8u)/16u;
                        if(pixels[3u*i]!=gray || pixels[3u*i+1u]!=gray || pixels[3u*i+2u]!=gray)
                            std::fprintf(stderr,"glyph-run background dpi=%d pixel=%zu expected=%u RGB=%u/%u/%u\n",
                                dpi,i,gray,(unsigned)pixels[3u*i],(unsigned)pixels[3u*i+1u],(unsigned)pixels[3u*i+2u]);
                        CHECK(pixels[3u*i]==gray && pixels[3u*i+1u]==gray && pixels[3u*i+2u]==gray);
                        continue;
                    }
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
            size_t count=0u;OK(quantapdf_text_search(text,text_value,nullptr,0u,&count));CHECK(count==1u);
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
