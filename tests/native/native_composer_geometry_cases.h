#ifndef QUANTAPDF_NATIVE_COMPOSER_GEOMETRY_CASES_H
#define QUANTAPDF_NATIVE_COMPOSER_GEOMETRY_CASES_H
#include <qpdf/QPDFPageObjectHelper.hh>

namespace native_composer_geometry {
struct instruction {std::string op;std::vector<Obj> args;};
class content_reader final : public Obj::ParserCallbacks {
public:
    std::vector<instruction> operations;
    void handleObject(Obj item) override {
        if(item.isOperator()) {operations.push_back({item.getOperatorValue(),args});args.clear();}
        else args.push_back(item);
    }
    void handleEOF() override {}
private:
    std::vector<Obj> args;
};
inline std::vector<unsigned char> rgb(size_t page,unsigned dpi,unsigned mode)
{
    const size_t w=((page==0u?12u:18u)*dpi+71u)/72u,h=((page==0u?12u:16u)*dpi+71u)/72u;
    std::vector<unsigned char> pixels(w*h*3u,255u);const double scale=double(dpi)/72.0;
    const double x0=mode==0u?1.0:mode==1u?2.0:3.0,x1=mode==0u?4.0:mode==1u?5.0:9.0;
    const double y0=mode==0u?2.0:3.0,y1=mode==0u?5.0:6.0;
    for(size_t g=0u;g<2u;++g)for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
        unsigned hits=0u;const unsigned colors[2][3]={{51u,102u,153u},{204u,51u,102u}};
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const double px=double(x)+(double(sx)+0.5)/4.0,py=double(y)+(double(sy)+0.5)/4.0;
            hits+=unsigned(px>=(g==0u?x0:0.0)*scale && px<(g==0u?x1:3.0)*scale &&
                py>=(g==0u?y0:8.0)*scale && py<(g==0u?y1:10.0)*scale);
        }
        // The current pure-Fill scene publishes sampled coverage as 8-bit alpha
        // before source-over. General sampling/composition remains A2 work.
        const unsigned alpha=(hits*255u+8u)/16u;
        for(size_t c=0u;c<3u;++c) {auto& value=pixels[(y*w+x)*3u+c];value=static_cast<unsigned char>((colors[g][c]*alpha+unsigned(value)*(255u-alpha)+127u)/255u);}
    }
    return pixels;
}
}

static int composer_geometry_cases(const std::string& filename)
{
    using namespace native_composer_geometry;
    size_t renders=0u,refusals=0u,graphs=0u;
    for(unsigned mode=0u;mode<4u;++mode) {
        quantapdf_context *context=nullptr;quantapdf_composer *composer=nullptr;size_t finalized=0u;
        OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        OK(quantapdf_composer_create_with_context(context,nullptr,&composer));
        for(size_t page=0u;page<2u;++page) {
            const quantapdf_composer_page_options options{sizeof(options),page==0u?12.0f:18.0f,page==0u?12.0f:16.0f,UINT32_C(0xffffffff)};
            size_t index=SIZE_MAX;OK(quantapdf_composer_add_page(composer,&options,&index));CHECK(index==page);
        }
        quantapdf_composer_graphics_state_id state_id=0u;quantapdf_composer_paint_id fill_id=0u,stroke_id=0u;
        if(mode==3u) {
            auto commands=native_composer_state::rectangle(0,0,2,2);
            const quantapdf_composer_clip_options clip{QUANTAPDF_COMPOSER_FILL_EVEN_ODD,{1,0,0,1,2,3}};
            quantapdf_composer_clip_id clip_id=0u;OK(quantapdf_composer_add_clip_path(composer,commands.data(),commands.size(),&clip,&clip_id));
            commands.fill({});
            const quantapdf_composer_graphics_state_options state{0.5f,0.25f,QUANTAPDF_COMPOSER_BLEND_NORMAL,clip_id,0u};
            OK(quantapdf_composer_add_graphics_state(composer,&state,&state_id));
            quantapdf_composer_gradient_stop stops[]={{0.0f,UINT32_C(0xffff0000)},{1.0f,UINT32_C(0xff0000ff)}};
            quantapdf_composer_linear_gradient_options gradient{};gradient.struct_size=sizeof(gradient);
            gradient.start={0,0};gradient.end={5,0};gradient.stops=stops;gradient.stop_count=2u;
            OK(quantapdf_composer_add_linear_gradient(composer,&gradient,&fill_id));
            stops[0].argb=UINT32_C(0xff00ff00);OK(quantapdf_composer_add_linear_gradient(composer,&gradient,&stroke_id));CHECK(fill_id!=stroke_id);
            stops[0]=stops[1]={};
        }
        quantapdf_composer_path_options path{};path.fill=1;path.fill_argb=UINT32_C(0xff336699);
        path.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        if(mode!=0u)path.transform={mode==2u?2.0f:1.0f,0,0,1,1,1};
        if(mode==3u) {
            path.stroke=1;path.stroke_width=1.5f;path.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
            path.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL;path.miter_limit=7.0f;path.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
            path.graphics_state_id=state_id;path.fill_paint_id=fill_id;path.stroke_paint_id=stroke_id;path.transform={1,0.25f,0.5f,1,1,2};
        }
        auto commands=native_composer_state::rectangle(1,2,4,5);
        auto invalid=path;invalid.transform.a=std::numeric_limits<float>::quiet_NaN();
        CHECK(quantapdf_composer_draw_path(composer,0u,commands.data(),commands.size(),&invalid)==QUANTAPDF_ERROR_ARGUMENT);
        invalid=path;invalid.graphics_state_id=SIZE_MAX;
        CHECK(quantapdf_composer_draw_path(composer,0u,commands.data(),commands.size(),&invalid)==QUANTAPDF_ERROR_ARGUMENT);
        invalid=path;invalid.fill_paint_id=SIZE_MAX;
        CHECK(quantapdf_composer_draw_path(composer,0u,commands.data(),commands.size(),&invalid)==QUANTAPDF_ERROR_ARGUMENT);
        if(mode==3u) {commands[2].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;commands[2].point1={5,1};commands[2].point2={5,4};commands[2].point3={4,5};}
        for(size_t page=0u;page<2u;++page) {
            if(mode==3u) {
                float lengths[]={2,1,3};const quantapdf_composer_dash_pattern dash{lengths,3u,0.5f};
                auto bad=dash;bad.phase=std::numeric_limits<float>::quiet_NaN();
                CHECK(quantapdf_composer_draw_path_dashed(composer,page,commands.data(),commands.size(),&path,&bad)==QUANTAPDF_ERROR_ARGUMENT);
                OK(quantapdf_composer_draw_path_dashed(composer,page,commands.data(),commands.size(),&path,&dash));
                lengths[0]=lengths[1]=lengths[2]=99.0f;
            } else OK(quantapdf_composer_draw_path(composer,page,commands.data(),commands.size(),&path));
            const auto second=native_composer_state::rectangle(0,8,3,10);quantapdf_composer_path_options plain{};
            plain.fill=1;plain.fill_argb=UINT32_C(0xffcc3366);plain.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
            OK(quantapdf_composer_draw_path(composer,page,second.data(),second.size(),&plain));
        }
        commands.fill({});path={};
        quantapdf_output *output=nullptr;OK(quantapdf_composer_finish(composer,&output));
        const unsigned char *data=nullptr;size_t size=0u;OK(quantapdf_output_data(output,&data,&size));
        std::string bytes(reinterpret_cast<const char*>(data),size);quantapdf_drop_output(output);quantapdf_drop_composer(composer);
        auto pdf=QPDF::create();pdf->processMemoryFile("current-composer-geometry",bytes.data(),bytes.size());
        auto kids=pdf->getRoot().getKey("/Pages").getKey("/Kids");CHECK(kids.getArrayNItems()==2);
        for(size_t page=0u;page<2u;++page) {
            auto object=kids.getArrayItem(int(page));content_reader reader;QPDFPageObjectHelper(object).parseContents(&reader);
            size_t matrices=0u,dashes=0u,combined=0u,curves=0u,clips=0u,states=0u,fill_paints=0u,stroke_paints=0u,fills=0u,moves=0u;
            for(const auto& op:reader.operations) {
                const auto& args=op.args;
                if(op.op=="cm") {
                    CHECK(args.size()==6u);const double height=page==0u?12.0:16.0;
                    const double expected[6]={mode==2u?2.0:1.0,mode==3u?-0.25:0.0,mode==3u?-0.5:0.0,1.0,mode==3u?0.5*height+1.0:1.0,mode==3u?-2.0:-1.0};
                    for(size_t i=0u;i<6u;++i)CHECK(args[i].getNumericValue()==expected[i]);++matrices;
                } else if(op.op=="d") {
                    CHECK(args.size()==2u && args[0].isArray() && args[0].getArrayNItems()==3 && args[1].getNumericValue()==0.5);
                    for(int i=0;i<3;++i)CHECK(args[0].getArrayItem(i).getNumericValue()==(i==0?2.0:i==1?1.0:3.0));++dashes;
                } else if(op.op=="B*")++combined;
                else if(op.op=="f")++fills;
                else if(op.op=="m")++moves;
                else if(op.op=="c") {CHECK(args.size()==6u && args[0].getNumericValue()==5.0 && args[1].getNumericValue()==(page==0u?11.0:15.0));++curves;}
                else if(op.op=="W*")++clips;
                else if(op.op=="gs") {CHECK(args.size()==1u && args[0].isNameAndEquals("/GS"+std::to_string(state_id)));++states;}
                else if(op.op=="scn") {CHECK(args.size()==1u && args[0].isNameAndEquals("/P"+std::to_string(fill_id)));++fill_paints;}
                else if(op.op=="SCN") {CHECK(args.size()==1u && args[0].isNameAndEquals("/P"+std::to_string(stroke_id)));++stroke_paints;}
                else if(op.op=="w")CHECK(args.size()==1u && args[0].getNumericValue()==1.5);
                else if(op.op=="J" || op.op=="j")CHECK(args.size()==1u && args[0].getIntValue()==2);
                else if(op.op=="M")CHECK(args.size()==1u && args[0].getNumericValue()==7.0);
            }
            CHECK(matrices==(mode==0u?0u:1u));
            CHECK(fills==(mode==3u?2u:3u) && moves==(mode==3u?3u:2u));
            CHECK(dashes==(mode==3u?1u:0u) && combined==dashes && curves==dashes && clips==dashes && states==dashes && fill_paints==dashes && stroke_paints==dashes);
            if(mode==3u) {
                auto resources=object.getKey("/Resources"),patterns=resources.getKey("/Pattern");CHECK(patterns.getKeys().size()==2u);
                for(auto id:{fill_id,stroke_id}) {
                    auto pattern=patterns.getKey("/P"+std::to_string(id)),shading=pattern.getKey("/Shading"),function=shading.getKey("/Function");
                    CHECK(pattern.getKey("/PatternType").getIntValue()==2 && shading.getKey("/ShadingType").getIntValue()==2 && function.getKey("/FunctionType").getIntValue()==2);
                    for(int channel=0;channel<3;++channel) {
                        CHECK(function.getKey("/C0").getArrayItem(channel).getNumericValue()==(channel==(id==fill_id?0:1)?1.0:0.0));
                        CHECK(function.getKey("/C1").getArrayItem(channel).getNumericValue()==(channel==2?1.0:0.0));
                    }
                }
                auto gs=resources.getKey("/ExtGState").getKey("/GS"+std::to_string(state_id));CHECK(gs.getKey("/ca").getNumericValue()==0.5 && gs.getKey("/CA").getNumericValue()==0.25);
            }
            ++graphs;
        }
        CHECK(!pdf->anyWarnings());
        if(mode==2u) {
            QPDFWriter writer(*pdf);writer.setOutputMemory();writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);writer.write();
            const auto buffer=writer.getBufferSharedPointer();bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
        }
        pdf.reset();CHECK(write_pdf(filename,bytes));quantapdf_document *document=nullptr;std::array<quantapdf_page*,2> pages{};
        if(mode==2u)CHECK(quantapdf_open_with_context(context,filename.c_str(),"wrong",&document)==QUANTAPDF_ERROR_PASSWORD && document==nullptr);
        OK(quantapdf_open_with_context(context,filename.c_str(),mode==2u?"reader":nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        for(int page:{1,0})OK(quantapdf_load_page(document,page,&pages[size_t(page)]));quantapdf_close(document);quantapdf_drop_context(context);
        struct held {quantapdf_bitmap *bitmap;size_t page;unsigned dpi;};std::vector<held> bitmaps;
        for(size_t page:{1u,0u})for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(pages[page],&options,&bitmap);
            if(mode==3u) {CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && bitmap==nullptr);++refusals;continue;}
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"Composer geometry mode=%u page=%zu dpi=%u status=%d\n",mode,page,dpi,int(status));
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,components=0;OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));
            CHECK(w==int(((page==0u?12u:18u)*dpi+71u)/72u) && h==int(((page==0u?12u:16u)*dpi+71u)/72u) && stride==3*w && components==3);
            bitmaps.push_back({bitmap,page,dpi});++renders;
        }
        for(auto page:pages)quantapdf_drop_page(page);CHECK(finalized==(bitmaps.empty()?1u:0u));
        for(const auto& value:bitmaps) {
            const unsigned char *pixels=nullptr;size_t size=0u;OK(quantapdf_bitmap_data(value.bitmap,&pixels,&size));const auto expected=rgb(value.page,value.dpi,mode);
            if(size==expected.size() && std::memcmp(pixels,expected.data(),size)!=0)for(size_t i=0u;i<size;++i)if(pixels[i]!=expected[i]) {
                std::fprintf(stderr,"Composer geometry RGB mode=%u page=%zu dpi=%u byte=%zu got=%u want=%u\n",mode,value.page,value.dpi,i,unsigned(pixels[i]),unsigned(expected[i]));break;
            }
            CHECK(size==expected.size() && std::memcmp(pixels,expected.data(),size)==0);quantapdf_drop_bitmap(value.bitmap);
        }
        CHECK(finalized==1u);
    }
    CHECK(graphs==8u && renders==18u && refusals==6u);
    std::puts("Current Composer Path/Clip/dash: 8 reopened page graphs, 18 analytic RGB renders including AES-256, 6 honest complex paint refusals; transforms, GS/paint IDs, curve/stroke/dash fields, retired input buffers and owner lifetimes passed");
    return EXIT_SUCCESS;
}
#endif
