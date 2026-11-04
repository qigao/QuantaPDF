#ifndef QUANTAPDF_NATIVE_COMPOSER_IMAGE_CASES_H
#define QUANTAPDF_NATIVE_COMPOSER_IMAGE_CASES_H

namespace native_composer_image {
struct placement {double x,y,w,h;};
inline placement fitted(unsigned fit,bool tall)
{
    if(fit==0u)return tall?placement{4.5,3,3,6}:placement{3,4.5,6,3};
    if(fit==1u)return tall?placement{3,0,6,12}:placement{0,3,12,6};
    return {3,3,6,6};
}
inline std::vector<unsigned char> rgb(unsigned fit,bool tall,bool clipped,size_t page,unsigned dpi)
{
    const size_t w=((page==0u?12u:16u)*dpi+71u)/72u,h=((page==0u?12u:14u)*dpi+71u)/72u;
    const double scale=double(dpi)/72.0;
    const placement draws[2]={fitted(fit,tall),{1,9,4,2}};
    const unsigned colors[2][3]={{51u,102u,153u},{204u,51u,102u}};
    std::vector<unsigned char> pixels(w*h*3u,255u);
    for(size_t draw=0u;draw<2u;++draw)for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
        const auto p=draws[draw];unsigned hits=0u;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const double px=(double(x)+(double(sx)+0.5)/4.0)/scale;
            const double py=(double(y)+(double(sy)+0.5)/4.0)/scale;
            const bool scope=draw!=0u || !clipped || (px>=3.0 && px<11.0 && py>=5.0 && py<9.0);
            const bool cover=draw!=0u || fit!=1u || (px>=3.0 && px<9.0 && py>=3.0 && py<9.0);
            hits+=unsigned(px>=p.x && px<p.x+p.w && py>=p.y && py<p.y+p.h && scope && cover);
        }
        const double coordinate=tall?((double(y)+0.5)/scale-p.y)/p.h:((double(x)+0.5)/scale-p.x)/p.w;
        const size_t sample=coordinate<0.5?0u:1u;
        for(size_t c=0u;c<3u;++c) {
            auto& value=pixels[(y*w+x)*3u+c];
            value=static_cast<unsigned char>((colors[sample][c]*hits+unsigned(value)*(16u-hits)+8u)/16u);
        }
    }
    return pixels;
}
}

static int composer_image_cases(const std::string& filename)
{
    size_t graphs=0u,renders=0u,refusals=0u;
    for(bool tall:{false,true})for(unsigned state_mode:{0u,1u,2u})for(bool encrypted:{false,true}) {
        quantapdf_context *context=nullptr;quantapdf_composer *composer=nullptr;size_t finalized=0u;
        OK(quantapdf_context_create(&context));context->test_finalizations=&finalized;
        OK(quantapdf_composer_create_with_context(context,nullptr,&composer));
        for(size_t fit=0u;fit<3u;++fit)for(size_t page=0u;page<2u;++page) {
            const quantapdf_composer_page_options options{sizeof(options),page==0u?12.0f:16.0f,page==0u?12.0f:14.0f,UINT32_C(0xffffffff)};
            size_t index=SIZE_MAX;OK(quantapdf_composer_add_page(composer,&options,&index));CHECK(index==2u*fit+page);
        }
        unsigned char samples[]={51,102,153,204,51,102};
        quantapdf_composer_raster raster{};raster.struct_size=sizeof(raster);
        raster.format=QUANTAPDF_COMPOSER_RASTER_RGB24;raster.width=tall?1u:2u;raster.height=tall?2u:1u;
        raster.stride=tall?3u:6u;raster.size=sizeof(samples);raster.pixels=samples;
        quantapdf_composer_image_id image_id=0u;OK(quantapdf_composer_add_raster(composer,&raster,&image_id));
        std::memset(samples,0,sizeof(samples));
        quantapdf_composer_graphics_state_id state_id=0u;
        if(state_mode!=0u) {
            auto commands=native_composer_state::rectangle(2,4,10,8);
            const quantapdf_composer_clip_options clip{QUANTAPDF_COMPOSER_FILL_NONZERO,{1,0,0,1,1,1}};
            quantapdf_composer_clip_id clip_id=0u;OK(quantapdf_composer_add_clip_path(composer,commands.data(),commands.size(),&clip,&clip_id));
            commands.fill({});
            const quantapdf_composer_graphics_state_options state{state_mode==2u?0.5f:1.0f,1.0f,QUANTAPDF_COMPOSER_BLEND_NORMAL,clip_id,0u};
            OK(quantapdf_composer_add_graphics_state(composer,&state,&state_id));
        }
        const quantapdf_rect bounds{3,3,9,9},restored{1,9,5,11};
        for(unsigned fit=0u;fit<3u;++fit)for(size_t page=0u;page<2u;++page) {
            const size_t index=2u*fit+page;
            quantapdf_composer_image_options options{static_cast<quantapdf_composer_image_fit>(fit),state_id};
            auto invalid=options;invalid.fit=static_cast<quantapdf_composer_image_fit>(-1);
            CHECK(quantapdf_composer_draw_image(composer,index,image_id,&bounds,&invalid)==QUANTAPDF_ERROR_ARGUMENT);
            invalid=options;invalid.graphics_state_id=SIZE_MAX;
            CHECK(quantapdf_composer_draw_image(composer,index,image_id,&bounds,&invalid)==QUANTAPDF_ERROR_ARGUMENT);
            OK(quantapdf_composer_draw_image(composer,index,image_id,&bounds,&options));
            options={QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH,0u};
            OK(quantapdf_composer_draw_image(composer,index,image_id,&restored,&options));
            options={static_cast<quantapdf_composer_image_fit>(-1),SIZE_MAX};
        }
        quantapdf_output *output=nullptr;OK(quantapdf_composer_finish(composer,&output));
        const unsigned char *data=nullptr;size_t size=0u;OK(quantapdf_output_data(output,&data,&size));
        std::string bytes(reinterpret_cast<const char*>(data),size);quantapdf_drop_output(output);quantapdf_drop_composer(composer);
        auto pdf=QPDF::create();pdf->processMemoryFile("current-composer-image",bytes.data(),bytes.size());
        auto kids=pdf->getRoot().getKey("/Pages").getKey("/Kids");CHECK(kids.getArrayNItems()==6);
        QPDFObjGen shared;
        for(unsigned fit=0u;fit<3u;++fit)for(size_t page=0u;page<2u;++page) {
            auto object=kids.getArrayItem(int(2u*fit+page)),resources=object.getKey("/Resources");
            auto images=resources.getKey("/XObject");CHECK(images.getKeys().size()==1u);
            auto image=images.getKey("/Im"+std::to_string(image_id));CHECK(image.isStream());
            if(fit==0u && page==0u)shared=image.getObjGen();else CHECK(image.getObjGen()==shared);
            CHECK(image.getDict().getKey("/Width").getIntValue()==(tall?1:2) && image.getDict().getKey("/Height").getIntValue()==(tall?2:1));
            CHECK(image.getDict().getKey("/ColorSpace").isNameAndEquals("/DeviceRGB") && image.getDict().getKey("/SMask").isNull());
            const auto decoded=image.getStreamData(qpdf_dl_generalized);const unsigned char original[]={51,102,153,204,51,102};
            CHECK(decoded->getSize()==sizeof(original) && std::memcmp(decoded->getBuffer(),original,sizeof(original))==0);
            native_composer_geometry::content_reader reader;QPDFPageObjectHelper(object).parseContents(&reader);
            size_t matrices=0u,clips=0u,states=0u,draws=0u;
            for(const auto& instruction:reader.operations) {
                const auto& args=instruction.args;
                if(instruction.op=="cm") {
                    CHECK(args.size()==6u && matrices<2u);
                    const auto p=matrices==0u?native_composer_image::fitted(fit,tall):native_composer_image::placement{1,9,4,2};
                    const double expected[]={p.w,0,0,p.h,p.x,(page==0u?12.0:14.0)-p.y-p.h};
                    for(size_t i=0u;i<6u;++i)CHECK(args[i].getNumericValue()==expected[i]);++matrices;
                } else if(instruction.op=="W" || instruction.op=="W*")++clips;
                else if(instruction.op=="gs") {CHECK(args.size()==1u && args[0].isNameAndEquals("/GS"+std::to_string(state_id)));++states;}
                else if(instruction.op=="Do") {CHECK(args.size()==1u && args[0].isNameAndEquals("/Im"+std::to_string(image_id)));++draws;}
            }
            CHECK(matrices==2u && draws==2u && states==(state_mode==0u?0u:1u) && clips==unsigned(fit==1u)+unsigned(state_mode!=0u));
            if(state_mode!=0u)CHECK(resources.getKey("/ExtGState").getKey("/GS"+std::to_string(state_id)).getKey("/ca").getNumericValue()==(state_mode==2u?0.5:1.0));
            ++graphs;
        }
        CHECK(!pdf->anyWarnings());
        if(encrypted) {
            QPDFWriter writer(*pdf);writer.setOutputMemory();writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);writer.write();
            const auto buffer=writer.getBufferSharedPointer();bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
        }
        pdf.reset();CHECK(write_pdf(filename,bytes));
        quantapdf_document *document=nullptr;std::array<quantapdf_page*,6> pages{};
        if(encrypted)CHECK(quantapdf_open_with_context(context,filename.c_str(),"wrong",&document)==QUANTAPDF_ERROR_PASSWORD && document==nullptr);
        OK(quantapdf_open_with_context(context,filename.c_str(),encrypted?"reader":nullptr,&document));CHECK(std::remove(filename.c_str())==0);
        for(size_t page=6u;page>0u;--page)OK(quantapdf_load_page(document,int(page-1u),&pages[page-1u]));
        quantapdf_close(document);quantapdf_drop_context(context);CHECK(finalized==0u);
        struct held {quantapdf_bitmap *bitmap;unsigned fit,dpi;size_t page;};std::vector<held> bitmaps;
        for(unsigned fit=0u;fit<3u;++fit)for(size_t page=0u;page<2u;++page)for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_bitmap *bitmap=nullptr;const quantapdf_render_options options{float(dpi),0.0f,0,{},0};
            const auto status=quantapdf_render_page_with_options(pages[2u*fit+page],&options,&bitmap);
            if(state_mode==2u) {CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && bitmap==nullptr);++refusals;continue;}
            if(status!=QUANTAPDF_OK)std::fprintf(stderr,"image fit=%u tall=%d state=%u encrypted=%d page=%zu dpi=%u status=%d\n",fit,int(tall),state_mode,int(encrypted),page,dpi,int(status));
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,components=0;OK(quantapdf_bitmap_dimensions(bitmap,&w,&h,&stride,&components));
            CHECK(w==int(((page==0u?12u:16u)*dpi+71u)/72u) && h==int(((page==0u?12u:14u)*dpi+71u)/72u) && stride==3*w && components==3);
            const unsigned char *pixels=nullptr;size_t count=0u;OK(quantapdf_bitmap_data(bitmap,&pixels,&count));
            const auto expected=native_composer_image::rgb(fit,tall,state_mode==1u,page,dpi);
            CHECK(count==expected.size() && std::memcmp(pixels,expected.data(),count)==0);
            bitmaps.push_back({bitmap,fit,dpi,page});++renders;
        }
        for(auto page:pages)quantapdf_drop_page(page);CHECK(finalized==(state_mode==2u?1u:0u));
        for(const auto& held:bitmaps) {
            const unsigned char *pixels=nullptr;size_t count=0u;OK(quantapdf_bitmap_data(held.bitmap,&pixels,&count));
            const auto expected=native_composer_image::rgb(held.fit,tall,state_mode==1u,held.page,held.dpi);
            CHECK(count==expected.size() && std::memcmp(pixels,expected.data(),count)==0);quantapdf_drop_bitmap(held.bitmap);
        }
        CHECK(finalized==1u);
    }
    CHECK(graphs==72u && renders==144u && refusals==72u);
    std::printf("Current Composer Image: %zu reopened shared-resource graphs, %zu independent RGB renders, %zu alpha refusals; three fit modes, transformed Clip/Q, selected encrypted pages and retired owners\n",graphs,renders,refusals);
    return EXIT_SUCCESS;
}
#endif
