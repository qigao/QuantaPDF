#ifndef QUANTAPDF_NATIVE_TTF_PUBLIC_CASES_H
#define QUANTAPDF_NATIVE_TTF_PUBLIC_CASES_H
#include "native_ttf_fixture.h"
#include "native_page_text_fixture.h"
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

/* New supported sequences reuse the independently qualified old layouts.
 * Five H shows equal one five-glyph show; remote rectangles/triangles cannot
 * touch the original Hello/accent outlines, so their sample grid is analytic. */
static std::vector<unsigned char> native_ttf_expanded_expected(int mode,unsigned dpi)
{
    if(mode==25) {
        // 96 DPI has a fractional page extent: original Rotate moves the
        // sample lattice before ceil, so a rotated old Bitmap is incorrect.
        return native_page_text::hello(dpi,90u);
    }
    const size_t width=mode==26?(257u*dpi+143u)/144u:(128u*dpi+71u)/72u;
    const size_t height=(64u*dpi+71u)/72u;
    std::vector<unsigned char> out(width*height*3u,255u);
    if(mode==26) {
        const size_t old_width=(128u*dpi+71u)/72u;
        const auto path=std::filesystem::path(__FILE__).parent_path()/"goldens"/
            ("roboto-hello-"+std::to_string(dpi)+"-m0.rgba");
        std::ifstream file(path,std::ios::binary);const std::vector<unsigned char> old{std::istreambuf_iterator<char>(file),{}};
        if(old.size()!=old_width*height*4u)throw std::runtime_error("missing original Hello golden");
        // The unchanged original Hello lies left of 128pt. The extra half
        // point adds white viewport columns, never resizes its glyphs.
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<old_width;++x)
            std::memcpy(out.data()+3u*(y*width+x),old.data()+4u*(y*old_width+x),3u);
        return out;
    }
    if(mode==23)return out;
    if(mode==48) {
        const auto goldens=std::filesystem::path(__FILE__).parent_path()/"goldens";
        std::ifstream masks_file(goldens/("roboto-hello-"+std::to_string(dpi)+"-m2b2i.mask16"),std::ios::binary);
        const std::vector<unsigned char> masks{std::istreambuf_iterator<char>(masks_file),{}};
        std::ifstream old_file(goldens/("roboto-hello-"+std::to_string(dpi)+"-m0.rgba"),std::ios::binary);
        const std::vector<unsigned char> old{std::istreambuf_iterator<char>(old_file),{}};
        const size_t pixels=width*height;
        if(masks.size()!=pixels*10u || old.size()!=pixels*4u)throw std::runtime_error("missing original glyph masks");
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x) {
            unsigned hits=0u;
            const uint64_t unit=8u*dpi;
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const uint64_t xx=(uint64_t(x)*8u+2u*sx+1u)*72u;
                const uint64_t yy=(uint64_t(y)*8u+2u*sy+1u)*72u;
                hits+=unsigned(8u*xx>=201u*unit && 8u*xx<301u*unit &&
                    4u*yy>=145u*unit && 4u*yy<183u*unit);
            }
            const size_t i=y*width+x;
            out[3u*i]=out[3u*i+1u]=static_cast<unsigned char>((255u*(16u-hits)+8u)/16u);
            unsigned gray=255u;
            for(size_t g=0u;g<5u;++g) {
                const size_t at=2u*(g*pixels+i);
                const unsigned mask=unsigned(masks[at])|(unsigned(masks[at+1u])<<8u);
                unsigned covered=0u;for(unsigned bit=0u;bit<16u;++bit)covered+=(mask>>bit)&1u;
                gray=(gray*(16u-covered)+8u)/16u;
                for(size_t c=0u;c<3u;++c)out[3u*i+c]=static_cast<unsigned char>((unsigned(out[3u*i+c])*(16u-covered)+8u)/16u);
            }
            if(old[4u*i]!=gray || old[4u*i+1u]!=gray || old[4u*i+2u]!=gray || old[4u*i+3u]!=255u)
                throw std::runtime_error("original glyph mask/golden mismatch");
        }
        return out;
    }
    if(mode==47) {
        const double scale=double(dpi)/72.0;
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x) {
            unsigned hits=0u;
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const double px=(double(x)+(double(sx)+0.5)/4.0)/scale;
                const double py=64.0-(double(y)+(double(sy)+0.5)/4.0)/scale;
                hits+=unsigned(px>=25.125 && px<37.625 && py>=18.25 && py<27.75);
            }
            out[(y*width+x)*3u]=out[(y*width+x)*3u+1u]=static_cast<unsigned char>((255u*(16u-hits)+8u)/16u);
        }
        return out;
    }
    if(mode==35 || mode==40 || mode==43 || mode==45) {
        const auto path=std::filesystem::path(__FILE__).parent_path()/"goldens"/
            ((mode==35 || mode==40?"roboto-mixed-":"roboto-hello-")+std::to_string(dpi)+
             (mode==35 || mode==40?"-m1.rgba":"-m0.rgba"));
        std::ifstream file(path,std::ios::binary);const std::vector<unsigned char> rgba{std::istreambuf_iterator<char>(file),{}};
        if(rgba.size()!=width*height*4u)throw std::runtime_error("missing original mixed golden");
        for(size_t i=0u;i<width*height;++i)std::memcpy(out.data()+3u*i,rgba.data()+4u*i,3u);
        if(mode==45)for(size_t i=0u;i<width*height;++i)out[3u*i+2u]=255u;
    } else {
        const auto original=native_ttf_fixture(mode);auto pdf=QPDF::create();
        pdf->processMemoryFile("expanded-text-reference",original.data(),original.size());
        auto page=pdf->getRoot().getKey("/Pages").getKey("/Kids").getArrayItem(0);
        const std::string content=mode==22?
            "BT /F 24 Tf 12 20 Td <002d002d002d002d002d> Tj ET":
            "q 0.875 g 0 0 128 64 re f Q q 0 0 1 rg 25.125 18.25 12.5 9.5 re f* Q "
            "q BT /F 24 Tf 12 20 Td <002d004a005100510054> Tj ET Q";
        page.replaceKey("/Contents",pdf->newStream(content));QPDFWriter writer(*pdf);writer.setOutputMemory();writer.write();
        const auto buffer=writer.getBufferSharedPointer();const std::string bytes(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
        const char *path="native-ttf-expanded-reference.pdf";
        if(!write_native_ttf_pdf(path,bytes))throw std::runtime_error("write expanded reference");
        const auto require=[](quantapdf_status st) {if(st!=QUANTAPDF_OK)throw std::runtime_error("render expanded reference");};
        quantapdf_document *document=nullptr;quantapdf_page *selected=nullptr;quantapdf_bitmap *bitmap=nullptr;
        require(quantapdf_open(path,nullptr,&document));if(std::remove(path)!=0)throw std::runtime_error("remove expanded reference");
        require(quantapdf_load_page(document,0,&selected));quantapdf_close(document);
        const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
        require(quantapdf_render_page_with_options(selected,&options,&bitmap));quantapdf_drop_page(selected);
        const unsigned char *pixels=nullptr;size_t size=0u;require(quantapdf_bitmap_data(bitmap,&pixels,&size));
        if(size!=out.size())throw std::runtime_error("expanded reference dimensions");
        std::memcpy(out.data(),pixels,size);quantapdf_drop_bitmap(bitmap);
    }
    if(mode==35 || mode==46)for(unsigned rectangle=0u;rectangle<(mode==46?2u:1u);++rectangle) {
        const unsigned left=1u+2u*rectangle,bottom=1u+2u*rectangle;
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x) {
            unsigned hits=0u;
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const uint64_t xx=(uint64_t(x)*8u+2u*sx+1u)*72u;
                const uint64_t yy=(uint64_t(y)*8u+2u*sy+1u)*72u;
                const uint64_t unit=8u*dpi;
                hits+=unsigned(xx>=left*unit && xx<(left+2u)*unit &&
                    yy>=(64u-bottom-2u)*unit && yy<(64u-bottom)*unit);
            }
            for(size_t c=0u;c<3u;++c) {
                auto& value=out[(y*width+x)*3u+c];
                const unsigned color=mode==35 && c==2u?255u:0u;
                value=static_cast<unsigned char>((color*hits+unsigned(value)*(16u-hits)+8u)/16u);
            }
        }
    }
    if(mode==40 || mode==43 || mode==45) {
        for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x) {
            unsigned hits=0u;const uint64_t unit=8u*dpi;
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const uint64_t xx=(uint64_t(x)*8u+2u*sx+1u)*72u;
                const uint64_t yy=(uint64_t(y)*8u+2u*sy+1u)*72u;
                hits+=unsigned(yy>=61u*unit && yy<63u*unit && (mode==43?
                    xx>=unit && xx<3u*unit:
                    2u*xx+yy>=65u*unit && 2u*xx+57u*unit<yy));
            }
            for(size_t c=0u;c<3u;++c) {
                auto& value=out[(y*width+x)*3u+c];
                const unsigned color=mode==45 && c==2u?255u:0u;
                value=static_cast<unsigned char>((color*hits+unsigned(value)*(16u-hits)+8u)/16u);
            }
        }
    }
    return out;
}

static int native_ttf_public_cases(const std::string& filename,int first=0,int last=31)
{
    if(first==32) {CHECK(composer_ttf_original(false,true).empty());CHECK(composer_ttf_original(true,true).empty());}
    const auto goldens=std::filesystem::path(__FILE__).parent_path()/"goldens";
    quantapdf_context *ctx=nullptr;OK(quantapdf_context_create(&ctx));
    /* Each positive is a fresh ORIGINAL; this restores real text observation
     * coverage in the existing native-only public target as well. */
    for(int mode=first;mode<=last;++mode) {
        const bool positive=mode<=9 || mode==11 || (mode>=32 && mode<=34) || mode==37 || mode==44 || mode==49 || mode==22 || mode==23 || mode==24 || mode==25 || mode==26 || mode==35 || mode==40 || mode==43 || mode==45 || mode==46 || mode==47 || mode==48;
        const auto original=(mode==44 || mode==49)?composer_ttf_original(mode==49):native_ttf_fixture(mode);
        const char *text_value=(mode==49 || mode==24)?"H":"Hello";
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
            quantapdf_render_options options{};options.dpi=(float)dpi;
            quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            const auto status=quantapdf_render_page_with_options(page,&options,&bitmap);
            if(!positive) {
                if(status==QUANTAPDF_OK || bitmap!=nullptr)std::fprintf(stderr,"Unexpected TTF admission mode=%d dpi=%d status=%d\n",mode,dpi,int(status));
                CHECK(status!=QUANTAPDF_OK && bitmap==nullptr);
                CHECK(status==(mode==12 || mode==13 || mode==17 || mode==18 || mode==27 || mode==30 || mode==31 || mode==36 || mode==39?QUANTAPDF_ERROR_FORMAT:QUANTAPDF_ERROR_UNSUPPORTED));
                continue;
            }
            if(status!=QUANTAPDF_OK || bitmap==nullptr)
                std::fprintf(stderr,"TTF public mode=%d dpi=%d status=%d bitmap=%p\n",mode,dpi,(int)status,(void*)bitmap);
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,components=0;const unsigned char *pixels=nullptr;size_t n=0u;
            OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));
            OK(quantapdf_bitmap_data(bitmap,&pixels,&n));
            CHECK(w==(mode==25?(64*dpi+71)/72:mode==26?(257*dpi+143)/144:(128*dpi+71)/72) &&
                h==(mode==25?(128*dpi+71)/72:(64*dpi+71)/72) && stride==w*3 && components==3 && n==(size_t)stride*(size_t)h);
            if(mode==22 || mode==23 || mode==25 || mode==26 || mode==35 || mode==40 || mode==43 || mode==45 || mode==46 || mode==47 || mode==48) {
                const auto expected=native_ttf_expanded_expected(mode,unsigned(dpi));
                CHECK(expected.size()==n && std::memcmp(pixels,expected.data(),n)==0);
            } else if(mode==3 || mode==24) {for(size_t i=0u;i<n;++i)CHECK(pixels[i]==255u);}
            else {
                const char *name=mode==11?"late":(mode==32 || mode==37)?"mixed":mode==33?"reverse":(mode==34 || mode==44 || mode==49)?"background":mode==1?"layers":mode==2?"tj":mode==5?"fringe":"hello";
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
