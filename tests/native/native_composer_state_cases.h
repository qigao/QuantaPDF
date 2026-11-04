#ifndef QUANTAPDF_NATIVE_COMPOSER_STATE_CASES_H
#define QUANTAPDF_NATIVE_COMPOSER_STATE_CASES_H
#include "core/context_internal.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

namespace native_composer_state {
inline std::array<quantapdf_composer_path_command,5> rectangle(float x0,float y0,float x1,float y1)
{
    std::array<quantapdf_composer_path_command,5> c{};
    c[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;c[0].point1={x0,y0};
    c[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;c[1].point1={x1,y0};
    c[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;c[2].point1={x1,y1};
    c[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;c[3].point1={x0,y1};
    c[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;return c;
}
inline quantapdf_status form_content(quantapdf_composer *composer,size_t page,void *failure)
{
    if(failure!=nullptr)return *static_cast<quantapdf_status*>(failure);
    const auto commands=rectangle(0.0f,0.0f,4.0f,4.0f);
    quantapdf_composer_path_options paint{};
    paint.fill=1;paint.fill_argb=UINT32_C(0xffffffff);paint.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    return quantapdf_composer_draw_path(composer,page,commands.data(),commands.size(),&paint);
}
inline std::vector<unsigned char> rgb(size_t page,unsigned dpi,bool clipped)
{
    const size_t width_points=page==0u?12u:18u,height_points=page==0u?12u:16u;
    const size_t width=(width_points*dpi+71u)/72u,height=(height_points*dpi+71u)/72u;
    const double scale=double(dpi)/72.0;
    const unsigned colors[2][3]={{51u,102u,153u},{204u,51u,102u}};
    std::vector<unsigned char> pixels(width*height*3u,255u);
    // Original Roboto 'l': x=156..341, y=0..1536, unitsPerEm=2048.
    // Composer glyph origins are baselines in its Y-down coordinates.
    for(size_t g=0u;g<2u;++g)for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x) {
        const double origin_x=g==0u?1.25:6.25,baseline=g==0u?4.25:7.25;
        const double x0=(origin_x+156.0/512.0)*scale,x1=(origin_x+341.0/512.0)*scale;
        const double y0=(baseline-3.0)*scale,y1=baseline*scale;unsigned hits=0u;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const double px=double(x)+(double(sx)+0.5)/4.0,py=double(y)+(double(sy)+0.5)/4.0;
            const bool in_clip=!clipped || g!=0u || (px>=scale && px<2.0*scale && py>=2.0*scale && py<3.0*scale);
            hits+=unsigned(px>=x0 && px<x1 && py>=y0 && py<y1 && in_clip);
        }
        for(size_t c=0u;c<3u;++c) {auto& value=pixels[(y*width+x)*3u+c];value=static_cast<unsigned char>((colors[g][c]*hits+unsigned(value)*(16u-hits)+8u)/16u);}
    }
    return pixels;
}
inline int svg_records(const std::string& filename)
{
    const char *draws[]={
        "<rect width=\"100\" height=\"100\" fill=\"red\" fill-opacity=\"0.5\" clip-path=\"url(#c)\" mask=\"url(#m)\"/>",
        "<g opacity=\"0.5\" clip-path=\"url(#c)\" mask=\"url(#m)\"><rect width=\"100\" height=\"100\" fill=\"red\"/></g>",
        "<use href=\"#s\" width=\"100\" height=\"100\" clip-path=\"url(#c)\" mask=\"url(#m)\"/>",
        "<rect width=\"100\" height=\"100\" fill=\"blue\" mask=\"url(#m)\"/>"};
    for(size_t i=0u;i<4u;++i) {
        const std::string svg=std::string("<svg viewBox=\"0 0 100 100\"><defs>")+
            "<symbol id=\"s\" viewBox=\"0 0 100 100\"><rect width=\"100\" height=\"100\" fill=\"red\"/></symbol>"
            "<clipPath id=\"c\" clipPathUnits=\"userSpaceOnUse\"><rect width=\"80\" height=\"80\"/></clipPath>"
            "<mask id=\"m\" maskUnits=\"userSpaceOnUse\" maskContentUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\" width=\"100\" height=\"100\" mask-type=\""+
            (i==3u?"luminance":"alpha")+"\"><rect width=\"50\" height=\"100\" fill=\"white\"/></mask></defs>"+draws[i]+"</svg>";
        quantapdf_composer *composer=nullptr;OK(quantapdf_composer_create(nullptr,&composer));
        const quantapdf_composer_page_options page_options{sizeof(page_options),100.0f,100.0f,UINT32_C(0xffffffff)};
        size_t index=SIZE_MAX;OK(quantapdf_composer_add_page(composer,&page_options,&index));
        const quantapdf_rect bounds{0,0,100,100};const quantapdf_composer_svg_options options{sizeof(options)};
        OK(quantapdf_composer_draw_svg(composer,index,reinterpret_cast<const unsigned char*>(svg.data()),svg.size(),&bounds,&options));
        quantapdf_output *output=nullptr;OK(quantapdf_composer_finish(composer,&output));
        const unsigned char *data=nullptr;size_t size=0u;OK(quantapdf_output_data(output,&data,&size));
        const std::string bytes(reinterpret_cast<const char*>(data),size);quantapdf_drop_output(output);quantapdf_drop_composer(composer);
        auto pdf=QPDF::create();pdf->processMemoryFile("current-svg-state",bytes.data(),bytes.size());
        auto page=pdf->getRoot().getKey("/Pages").getKey("/Kids").getArrayItem(0);
        auto states=page.getKey("/Resources").getKey("/ExtGState");size_t masks=0u;
        for(const auto& key:states.getKeys()) {
            auto gs=states.getKey(key),mask=gs.getKey("/SMask");if(mask.isNull())continue;
            CHECK(mask.getKey("/S").isNameAndEquals(i==3u?"/Luminosity":"/Alpha") && mask.getKey("/G").isStream());
            CHECK(gs.getKey("/ca").getNumericValue()==(i<2u?0.5:1.0));++masks;
        }
        CHECK(masks==1u && !pdf->anyWarnings());pdf.reset();CHECK(write_pdf(filename,bytes));
        quantapdf_document *document=nullptr;quantapdf_page *native_page=nullptr;quantapdf_bitmap *bitmap=nullptr;
        OK(quantapdf_open(filename.c_str(),nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,0,&native_page));quantapdf_close(document);
        CHECK(quantapdf_render_page(native_page,&bitmap)==QUANTAPDF_ERROR_UNSUPPORTED && bitmap==nullptr);
        quantapdf_drop_page(native_page);
    }
    return EXIT_SUCCESS;
}
}

static int composer_state_cases(const std::string& filename)
{
    using namespace native_composer_state;
    size_t renders=0u,refusals=0u,serialized=0u;
    const auto font_path=std::filesystem::path(__FILE__).parent_path().parent_path()/"fixtures"/"fonts"/"Roboto-Regular.ttf";
    std::ifstream font_file(font_path,std::ios::binary);
    const std::vector<unsigned char> font{std::istreambuf_iterator<char>(font_file),{}};CHECK(!font.empty());
    for(unsigned mode=0u;mode<6u;++mode) {
        quantapdf_context *context=nullptr;quantapdf_composer *composer=nullptr;
        size_t finalized=0u;OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        OK(quantapdf_composer_create_with_context(context,nullptr,&composer));
        for(size_t page=0u;page<2u;++page) {
            const quantapdf_composer_page_options options{sizeof(options),page==0u?12.0f:18.0f,page==0u?12.0f:16.0f,UINT32_C(0xffffffff)};
            size_t index=SIZE_MAX;OK(quantapdf_composer_add_page(composer,&options,&index));CHECK(index==page);
        }
        quantapdf_composer_clip_options clip_options{};clip_options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        const auto full=rectangle(0.0f,0.0f,12.0f,12.0f),cut=rectangle(0.0f,0.0f,1.0f,1.0f);
        quantapdf_composer_clip_id full_id=0u,cut_id=0u;
        OK(quantapdf_composer_add_clip_path(composer,full.data(),full.size(),&clip_options,&full_id));
        clip_options.transform={1,0,0,1,1,2};
        OK(quantapdf_composer_add_clip_path(composer,cut.data(),cut.size(),&clip_options,&cut_id));CHECK(cut_id!=full_id);
        quantapdf_composer_soft_mask_id alpha=0u,luminosity=0u,translated=0u;
        if(mode>=4u) {
            quantapdf_composer_form_options form{};form.struct_size=sizeof(form);form.width_points=form.height_points=4.0f;
            form.flags=QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP|QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
            quantapdf_composer_form_id group=99u,ordinary=0u;
            auto failure=QUANTAPDF_ERROR_FORMAT;
            CHECK(quantapdf_composer_add_form(composer,&form,form_content,&failure,&group)==QUANTAPDF_ERROR_FORMAT && group==0u && context->references==2u);
            OK(quantapdf_composer_add_form(composer,&form,form_content,nullptr,&group));
            form.flags=0u;OK(quantapdf_composer_add_form(composer,&form,form_content,nullptr,&ordinary));
            quantapdf_composer_soft_mask_options mask{QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA,{1,0,0,1,0,0}};
            quantapdf_composer_soft_mask_id invalid=99u,duplicate=0u;
            CHECK(quantapdf_composer_add_soft_mask(composer,ordinary,&mask,&invalid)==QUANTAPDF_ERROR_ARGUMENT && invalid==0u);
            OK(quantapdf_composer_add_soft_mask(composer,group,&mask,&alpha));
            OK(quantapdf_composer_add_soft_mask(composer,group,&mask,&duplicate));CHECK(duplicate==alpha);
            mask.mode=QUANTAPDF_COMPOSER_SOFT_MASK_LUMINOSITY;OK(quantapdf_composer_add_soft_mask(composer,group,&mask,&luminosity));
            mask.mode=QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA;mask.transform.e=1.25f;mask.transform.f=2.5f;
            OK(quantapdf_composer_add_soft_mask(composer,group,&mask,&translated));CHECK(alpha!=luminosity && translated!=alpha && translated!=luminosity);
            mask.mode=static_cast<quantapdf_composer_soft_mask_mode>(99);invalid=99u;
            CHECK(quantapdf_composer_add_soft_mask(composer,group,&mask,&invalid)==QUANTAPDF_ERROR_ARGUMENT && invalid==0u);
        }
        const quantapdf_composer_graphics_state_options state{
            mode==2u?0.5f:1.0f,mode==2u?0.25f:1.0f,
            mode==3u?QUANTAPDF_COMPOSER_BLEND_MULTIPLY:QUANTAPDF_COMPOSER_BLEND_NORMAL,
            mode==0u?0u:cut_id,mode==4u?translated:mode==5u?luminosity:0u};
        auto invalid=state;quantapdf_composer_graphics_state_id rejected=99u,id=0u,duplicate=0u;
        invalid.clip_id=SIZE_MAX;CHECK(quantapdf_composer_add_graphics_state(composer,&invalid,&rejected)==QUANTAPDF_ERROR_ARGUMENT && rejected==0u);
        invalid=state;invalid.soft_mask_id=SIZE_MAX;rejected=99u;
        CHECK(quantapdf_composer_add_graphics_state(composer,&invalid,&rejected)==QUANTAPDF_ERROR_ARGUMENT && rejected==0u);
        invalid=state;invalid.fill_alpha=std::numeric_limits<float>::quiet_NaN();rejected=99u;
        CHECK(quantapdf_composer_add_graphics_state(composer,&invalid,&rejected)==QUANTAPDF_ERROR_ARGUMENT && rejected==0u);
        OK(quantapdf_composer_add_graphics_state(composer,&state,&id));OK(quantapdf_composer_add_graphics_state(composer,&state,&duplicate));CHECK(id==duplicate);
        quantapdf_composer_font_options font_options{};font_options.struct_size=sizeof(font_options);quantapdf_composer_font_id font_id=0u;
        OK(quantapdf_composer_add_font(composer,font.data(),font.size(),&font_options,&font_id));
        const quantapdf_composer_glyph glyph{81u,243.0f,0.0f,0.0f,0.0f,0u,1u};
        for(size_t page=0u;page<2u;++page)for(unsigned g=0u;g<2u;++g) {
            quantapdf_composer_glyph_run_options text{};text.struct_size=sizeof(text);text.font_id=font_id;text.font_size=4.0f;
            text.argb=g==0u?UINT32_C(0xff336699):UINT32_C(0xffcc3366);text.graphics_state_id=g==0u?id:0u;
            const quantapdf_point origin{g==0u?1.25f:6.25f,g==0u?4.25f:7.25f};
            OK(quantapdf_composer_draw_glyph_run(composer,page,origin,&glyph,1u,"l",1u,&text));
        }
        quantapdf_output *output=nullptr;const unsigned char *data=nullptr;size_t size=0u;
        OK(quantapdf_composer_finish(composer,&output));OK(quantapdf_output_data(output,&data,&size));
        const std::string bytes(reinterpret_cast<const char*>(data),size);quantapdf_drop_output(output);quantapdf_drop_composer(composer);
        auto pdf=QPDF::create();pdf->processMemoryFile("current-composer-state",bytes.data(),bytes.size());
        const auto kids=pdf->getRoot().getKey("/Pages").getKey("/Kids");CHECK(kids.getArrayNItems()==2);
        Obj previous_gs=Obj::newNull(),previous_group=Obj::newNull(),previous_source=Obj::newNull();
        for(size_t page=0u;page<2u;++page) {
            const auto resources=kids.getArrayItem(int(page)).getKey("/Resources");const auto states=resources.getKey("/ExtGState");
            CHECK(states.isDictionary() && states.getKeys().size()==1u);
            auto gs=states.getKey("/GS"+std::to_string(id));CHECK(gs.isDictionary() && gs.getKey("/Type").isNameAndEquals("/ExtGState"));
            CHECK(gs.getKey("/ca").getNumericValue()==state.fill_alpha && gs.getKey("/CA").getNumericValue()==state.stroke_alpha &&
                gs.getKey("/BM").isNameAndEquals(mode==3u?"/Multiply":"/Normal"));
            if(mode>=4u) {
                auto mask=gs.getKey("/SMask");CHECK(mask.isDictionary() && mask.getKey("/S").isNameAndEquals(mode==4u?"/Alpha":"/Luminosity"));
                auto group=mask.getKey("/G");CHECK(group.isStream());auto dict=group.getDict(),bbox=dict.getKey("/BBox");
                CHECK(bbox.getArrayItem(2).getNumericValue()==(page==0u?12.0:18.0) && bbox.getArrayItem(3).getNumericValue()==(page==0u?12.0:16.0));
                CHECK(dict.getKey("/Group").getKey("/S").isNameAndEquals("/Transparency") && dict.getKey("/Group").getKey("/I").getBoolValue());
                auto source=dict.getKey("/Resources").getKey("/XObject").getKey("/MaskSource");CHECK(source.isStream());
                CHECK(source.getDict().getKey("/BBox").getArrayItem(2).getNumericValue()==4.0 && source.getDict().getKey("/Group").getKey("/I").getBoolValue());
                const auto buffer=group.getStreamData(qpdf_dl_generalized);const std::string content(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
                CHECK(content==(mode==4u?(page==0u?"q 1 0 0 1 1.25 5.5 cm /MaskSource Do Q\n":"q 1 0 0 1 1.25 9.5 cm /MaskSource Do Q\n"):
                    (page==0u?"q 1 0 0 1 0 8 cm /MaskSource Do Q\n":"q 1 0 0 1 0 12 cm /MaskSource Do Q\n")));
                if(page!=0u)CHECK(gs.getObjGen()!=previous_gs.getObjGen() && group.getObjGen()!=previous_group.getObjGen() && source.getObjGen()==previous_source.getObjGen());
                previous_group=group;previous_source=source;
            } else {CHECK(gs.getKey("/SMask").isNull());if(page!=0u)CHECK(gs.getObjGen()==previous_gs.getObjGen());}
            previous_gs=gs;++serialized;
        }
        CHECK(!pdf->anyWarnings());pdf.reset();CHECK(write_pdf(filename,bytes));
        quantapdf_document *document=nullptr;std::array<quantapdf_page*,2> pages{};
        OK(quantapdf_open_with_context(context,filename.c_str(),nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        for(int page=0;page<2;++page)OK(quantapdf_load_page(document,page,&pages[size_t(page)]));
        quantapdf_close(document);quantapdf_drop_context(context);
        struct held {quantapdf_bitmap *bitmap;size_t page;unsigned dpi;};std::vector<held> bitmaps;
        for(size_t page=0u;page<2u;++page)for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(pages[page],&options,&bitmap);
            if(mode<2u) {
                if(status!=QUANTAPDF_OK)std::fprintf(stderr,"Composer state mode=%u page=%zu dpi=%u status=%d\n",mode,page,dpi,int(status));
                CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
                const unsigned char *pixels=nullptr;size_t n=0u;OK(quantapdf_bitmap_data(bitmap,&pixels,&n));const auto expected=rgb(page,dpi,mode==1u);
                if(n==expected.size() && std::memcmp(pixels,expected.data(),n)!=0)for(size_t i=0u;i<n;++i)if(pixels[i]!=expected[i]) {std::fprintf(stderr,"Composer RGB mode=%u page=%zu dpi=%u byte=%zu got=%u want=%u\n",mode,page,dpi,i,unsigned(pixels[i]),unsigned(expected[i]));break;}
                CHECK(n==expected.size() && std::memcmp(pixels,expected.data(),n)==0);bitmaps.push_back({bitmap,page,dpi});++renders;
            } else {CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && bitmap==nullptr);++refusals;}
        }
        for(auto page:pages)quantapdf_drop_page(page);
        CHECK(finalized==(bitmaps.empty()?1u:0u));
        for(const auto& value:bitmaps) {
            const unsigned char *pixels=nullptr;size_t n=0u;OK(quantapdf_bitmap_data(value.bitmap,&pixels,&n));const auto expected=rgb(value.page,value.dpi,mode==1u);
            CHECK(n==expected.size() && std::memcmp(pixels,expected.data(),n)==0);quantapdf_drop_bitmap(value.bitmap);
        }
        CHECK(finalized==1u);
    }
    CHECK(renders==12u && refusals==24u && serialized==12u);
    CHECK(svg_records(filename)==EXIT_SUCCESS);
    std::printf("Current Composer state records: %zu reopened page resource graphs, %zu analytic RGB renders, %zu honest native alpha/blend/SMask refusals; clip/mask IDs, transforms, dedup, builder errors and retired owners passed\n",serialized,renders,refusals);
    std::puts("SVG path/group/use alpha masks and path luminosity mask retain current graphics-state fields in reopened PDFs; native transparency explicitly unsupported");
    return EXIT_SUCCESS;
}
#endif
