/* #269: NEW test-only, mirrored crop/rotation falsification of the
 * eight independently defined Q4/rectangle/float32 models in #375.
 * This fixture does not promote a native renderer or count as a new
 * third-party published/redistributable original PDF.
 */
namespace {
static qp375_box qp376_source_rect(unsigned family,unsigned phase,
                                    int rotation,bool clip)
{
    CHECK(family<3u && phase<3u &&
          (rotation==180 || rotation==270));
    const double delta[3]={-3.0/256.0,0.0,3.0/256.0};
    if(rotation==180){
        const double b=20.25+delta[phase];
        if(family==0u)
            return clip?qp375_box{1.125,2.375,25.125,20.375}:
                        qp375_box{b,4.5,b+4.5,19.0};
        if(family==1u)
            return clip?qp375_box{19.125,5.125,25.125,16.125}:
                        qp375_box{b,4.5,b+4.5,19.0};
        return clip?qp375_box{b,5.0,b+4.5,18.0}:
                    qp375_box{2.0,3.0,24.0,20.0};
    }
    const double b=15.5+delta[phase];
    if(family==0u)
        return clip?qp375_box{1.125,2.375,25.125,20.375}:
                    qp375_box{3.5,b,18.0,b+4.5};
    if(family==1u)
        return clip?qp375_box{5.125,15.0,16.125,20.375}:
                    qp375_box{3.5,b,18.0,b+4.5};
    return clip?qp375_box{5.0,b,18.0,b+4.5}:
                qp375_box{2.0,3.0,24.0,20.0};
}
static std::string qp376_rect_literal(qp375_box box)
{
    char buf[160]={};
    CHECK(box.x1>box.x0 && box.y1>box.y0);
    const int len=std::snprintf(buf,sizeof(buf),
        "%.8f %.8f %.8f %.8f re",
        box.x0,box.y0,box.x1-box.x0,box.y1-box.y0);
    CHECK(len>0 && (size_t)len<sizeof(buf));
    return std::string(buf);
}
static void qp376_write_source_pdf(const char *path,unsigned family,
                                    unsigned phase,int rotation)
{
    CHECK(path && *path);
    const auto fill=qp376_source_rect(family,phase,rotation,false);
    const auto clip=qp376_source_rect(family,phase,rotation,true);
    std::string command;
    if(family!=0u)
        command=qp376_rect_literal(clip)+" W n ";
    command+="1 0 0 rg "+qp376_rect_literal(fill)+" f";
    auto doc=QPDF::create();
    doc->emptyPDF();
    auto pages=doc->getRoot().getKey("/Pages");
    auto page=doc->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    pages.replaceKey("/Kids",QPDFObjectHandle::newArray({page}));
    pages.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
    pages.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 30 24]"));
    page.replaceKey("/CropBox",
                     QPDFObjectHandle::parse("[1.125 2.375 25.125 20.375]"));
    page.replaceKey("/Rotate",QPDFObjectHandle::newInteger(rotation));
    page.replaceKey("/UserUnit",QPDFObjectHandle::newInteger(1));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/Contents",doc->newStream(command));
    QPDFWriter writer(*doc);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    CHECK(bytes && bytes->getSize()>0u && bytes->getSize()<=8192u);
    FILE *f=std::fopen(path,"wb");CHECK(f);
    CHECK(std::fwrite(bytes->getBuffer(),1,bytes->getSize(),f)==
          bytes->getSize());
    CHECK(std::fclose(f)==0);
}
static qp269_float_box_v0 qp376_project_box(qp375_box src,
    unsigned w,unsigned h,int rotation,unsigned precision)
{
    CHECK(precision<3u && (rotation==180 || rotation==270) &&
          w>0u && h>0u && w*h<=4096u);
    const double crop_x1=25.125,crop_y0=2.375,crop_y1=20.375;
    double m[6]={};
    if(rotation==180){
        m[0]=-(double)w/24.0;
        m[3]=(double)h/18.0;
        m[4]=crop_x1*(double)w/24.0;
        m[5]=-crop_y0*(double)h/18.0;
    }else{
        m[1]=-(double)h/24.0;
        m[2]=-(double)w/18.0;
        m[4]=crop_y1*(double)w/18.0;
        m[5]=crop_x1*(double)h/24.0;
    }
    /* p1: float32 source and matrix coefficients; p2 additionally
     * stores the transformed vertices as float32. Neither is sourced
     * from PDFium reference pixels nor claims native CTM identity. */
    if(precision!=0u)
      for(unsigned k=0u;k<6u;++k)m[k]=(double)(float)m[k];
    double xs[2]={src.x0,src.x1},ys[2]={src.y0,src.y1};
    if(precision!=0u){
        for(double& x:xs)x=(double)(float)x;
        for(double& y:ys)y=(double)(float)y;
    }
    qp269_float_box_v0 box{};
    for(unsigned v=0u;v<4u;++v){
        const double x=xs[v%2u],y=ys[v/2u];
        double dx=m[0]*x+m[2]*y+m[4];
        double dy=m[1]*x+m[3]*y+m[5];
        if(precision==2u){
            dx=(double)(float)dx;
            dy=(double)(float)dy;
        }
        CHECK(std::isfinite(dx) && std::isfinite(dy) &&
              dx>=-1.0 && dy>=-1.0 &&
              dx<=(double)w+1.0 && dy<=(double)h+1.0);
        if(v==0u)box={dx,dy,dx,dy};
        else{
            box.x0=std::min(box.x0,dx);
            box.y0=std::min(box.y0,dy);
            box.x1=std::max(box.x1,dx);
            box.y1=std::max(box.y1,dy);
        }
    }
    CHECK(box.x1>box.x0 && box.y1>box.y0);
    return box;
}
static void qp376_evaluate(qp375_box fill,qp375_box clip,int rotation,
    unsigned w,unsigned h,const unsigned char *rgb,size_t stride,
    qp269_rect_quant_score_v0 scores[8],size_t flips[3])
{
    CHECK(rgb && w>0u && h>0u && w*h<=4096u &&
          stride>=3u*(size_t)w);
    qp269_float_box_v0 fr[3]={},cr[3]={};
    qp269_rect_quant_box_v0 outer[3]={},nearest[3][2]={};
    for(unsigned p=0u;p<3u;++p){
        fr[p]=qp376_project_box(fill,w,h,rotation,p);
        cr[p]=qp376_project_box(clip,w,h,rotation,p);
        outer[p]=qp269_rect_outer_v0(cr[p]);
        nearest[p][0]=qp375_closest(fr[p],false);
        nearest[p][1]=qp375_closest(fr[p],true);
    }
    for(size_t y=0u;y<h;++y)
      for(size_t x=0u;x<w;++x){
        unsigned full_hits=0u,outer_hits=0u;
        const bool clip_integer=qp269_rect_integer_contains_v0(
            outer[0],(int)x,(int)y);
        for(unsigned j=0u;j<4u;++j)
          for(unsigned i=0u;i<4u;++i){
            const double px=(double)x+((double)i+0.5)/4.0;
            const double py=(double)y+((double)j+0.5)/4.0;
            const bool fill_hit=qp269_float_inside_v0(fr[0],px,py);
            const bool clip_hit=qp269_float_inside_v0(cr[0],px,py);
            full_hits+=(unsigned)(fill_hit&&clip_hit);
            outer_hits+=(unsigned)(fill_hit&&clip_integer);
          }
        unsigned coverage[8]={
            (255u*full_hits+8u)/16u,(255u*outer_hits+8u)/16u,
            0u,0u,0u,0u,0u,0u};
        for(unsigned p=0u;p<3u;++p)
          for(unsigned t=0u;t<2u;++t){
            coverage[2u+2u*p+t]=
              qp269_rect_integer_contains_v0(nearest[p][t],(int)x,(int)y) &&
              qp269_rect_integer_contains_v0(outer[p],(int)x,(int)y)
                  ?255u:0u;
          }
        flips[0]+=(unsigned)(coverage[2]!=coverage[3]);
        flips[1]+=(unsigned)(coverage[2]!=coverage[4]);
        flips[2]+=(unsigned)(coverage[4]!=coverage[6]);
        const auto*pixel=rgb+y*stride+x*3u;
        for(unsigned method=0u;method<8u;++method)
            qp269_rect_quant_add_pixel_v0(&scores[method],
                coverage[method],pixel);
      }
}
static int test_269_q4_mirror_originals(const char *report_path)
{
    CHECK(report_path && *report_path &&
          corpus_fixture_executions==0u);
    const char *source=GENERATED_269_Q4_MIRROR_PDF;
    const std::string tmp=std::string(report_path)+".tmp";
    CHECK(source && *source && tmp!=source &&
          std::strcmp(source,report_path)!=0);
    (void)std::remove(report_path);
    (void)std::remove(tmp.c_str());
    FILE *out=std::fopen(tmp.c_str(),"wb");CHECK(out);
    const unsigned dpis[3]={72u,96u,144u};
    qp269_rect_quant_score_v0 totals[8]={},by_dpi[3][8]={};
    size_t better[8]={},worse[8]={},equal[8]={};
    size_t flips[3]={};
    uint64_t source_hashes[18]={};
    size_t sources=0u,verified=0u,frames=0u,ref_repeats=0u;
    size_t expected_pin_count=0u;
    std::fprintf(out,
        "{\"schema_version\":1,"
        "\"source_kind\":\"new_mirrored_test_generated_original_pdf\","
        "\"source_from_ir_export\":false,"
        "\"reference_only_not_native\":true,"
        "\"public_sdk_backend_switched\":false,"
        "\"native_renderer_modified\":false,"
        "\"external_native_positive_increment\":0,"
        "\"models\":[");
    for(unsigned method=0u;method<8u;++method)
        std::fprintf(out,"\"%s\"%s",qp375_names[method],
                     method==7u?"":",");
    std::fprintf(out,"],\"frames\":[");
    for(unsigned family=0u;family<3u;++family)
      for(unsigned phase=0u;phase<3u;++phase)
        for(unsigned rotation_index=0u;rotation_index<2u;
            ++rotation_index){
          const int rotation=rotation_index==0u?180:270;
          qp376_write_source_pdf(source,family,phase,rotation);
          const auto original=decimal360_read_bytes(source);
          const uint64_t hash=decimal360_fnv64(original);
          for(size_t i=0u;i<sources;++i)
              CHECK(source_hashes[i]!=hash);
          CHECK(sources<18u);
          source_hashes[sources]=hash;
          const auto fill=qp376_source_rect(
              family,phase,rotation,false);
          const auto clip=qp376_source_rect(
              family,phase,rotation,true);
          quantapdf_document *doc=nullptr;
          quantapdf_page *page=nullptr;
          OK(quantapdf_open(source,nullptr,&doc));
          OK(quantapdf_load_page(doc,0u,&page));
          ++sources;
          for(unsigned dpi_index=0u;dpi_index<3u;++dpi_index){
            const unsigned dpi=dpis[dpi_index];
            const unsigned w=(rotation==180?24u:18u)*dpi/72u;
            const unsigned h=(rotation==180?18u:24u)*dpi/72u;
            CHECK(w*h<=4096u && w>0u && h>0u);
            const quantapdf_render_options opts={
                sizeof(quantapdf_render_options),(float)dpi,0.0f,
                0,{0},0};
            quantapdf_bitmap *ref=nullptr,*repeated=nullptr;
            OK(quantapdf_render_page_with_options(page,&opts,&ref));
            OK(quantapdf_render_page_with_options(
                page,&opts,&repeated));
            int rw=0,rh=0,rs=0,rc=0,tw=0,th=0,ts=0,tc=0;
            OK(quantapdf_bitmap_dimensions(ref,&rw,&rh,&rs,&rc));
            OK(quantapdf_bitmap_dimensions(
                repeated,&tw,&th,&ts,&tc));
            CHECK(rw==(int)w && rh==(int)h && rc==3 &&
                  tw==rw && th==rh && ts==rs && tc==rc &&
                  rs>=(int)(w*3u) && rs<=(int)(w*3u+4u));
            const unsigned char*rgb=nullptr,*other=nullptr;
            size_t count=0u,other_count=0u;
            OK(quantapdf_bitmap_data(ref,&rgb,&count));
            OK(quantapdf_bitmap_data(
                repeated,&other,&other_count));
            CHECK(rgb && other && count==(size_t)rs*h &&
                  other_count==count &&
                  std::memcmp(rgb,other,count)==0);
            ++ref_repeats;
            qp269_rect_quant_score_v0 scores[8]={};
            size_t local_flips[3]={};
            qp376_evaluate(fill,clip,rotation,w,h,rgb,(size_t)rs,
                           scores,local_flips);
            for(unsigned k=0u;k<8u;++k){
                qp269_rect_quant_add_v0(&totals[k],scores[k]);
                qp269_rect_quant_add_v0(&by_dpi[dpi_index][k],scores[k]);
                if(scores[k].rgb_abs()>scores[0].rgb_abs())++worse[k];
                else if(scores[k].rgb_abs()<scores[0].rgb_abs())++better[k];
                else ++equal[k];
            }
            for(unsigned k=0u;k<3u;++k)flips[k]+=local_flips[k];
            const bool pin=(family==0u && rotation==180 &&
                dpi==96u && (phase==1u||phase==2u));
            if(pin && scores[6].rgb_abs()>scores[0].rgb_abs() &&
               scores[6].under_rgb_abs>0u &&
               scores[6].over_rgb_abs>0u)
                ++expected_pin_count;
            std::fprintf(out,
                "%s{\"frame\":%zu,\"source_index\":%zu,"
                "\"family\":%u,\"phase\":%u,\"rotation\":%d,"
                "\"dpi\":%u,\"source_fnv64\":\"%016llx\","
                "\"grid\":[%u,%u],"
                "\"stage_mask_flips\":[%zu,%zu,%zu],"
                "\"scores\":[",frames?",":"",frames,sources-1u,
                family,phase,rotation,dpi,(unsigned long long)hash,
                w,h,local_flips[0],local_flips[1],local_flips[2]);
            for(unsigned k=0u;k<8u;++k){
                const auto& s=scores[k];
                std::fprintf(out,
                    "%s{\"method\":\"%s\","
                    "\"under\":%zu,\"over\":%zu,"
                    "\"under_rgb_abs\":%llu,\"over_rgb_abs\":%llu,"
                    "\"rgb_abs\":%llu}",k?",":"",qp375_names[k],
                    s.under,s.over,
                    (unsigned long long)s.under_rgb_abs,
                    (unsigned long long)s.over_rgb_abs,
                    (unsigned long long)s.rgb_abs());
            }
            std::fprintf(out,"]}");
            ++frames;
            quantapdf_drop_bitmap(repeated);
            quantapdf_drop_bitmap(ref);
          }
          quantapdf_drop_page(page);
          quantapdf_close(doc);
          CHECK(decimal360_read_bytes(source)==original);
          CHECK(std::remove(source)==0);
          ++verified;
        }
    CHECK(sources==18u && verified==18u && frames==54u &&
          ref_repeats==54u && corpus_fixture_executions==0u);
    std::fprintf(out,
        "],\"source_count\":%zu,\"source_bytes_verified\":%zu,"
        "\"reference_repeat_identical\":%zu,\"frame_count\":%zu,"
        "\"new_f32_vertex_96dpi_mirrored_falsifiers\":%zu,"
        "\"stage_mask_flips\":[%zu,%zu,%zu],"
        "\"global_scores\":[",
        sources,verified,ref_repeats,frames,expected_pin_count,
        flips[0],flips[1],flips[2]);
    for(unsigned k=0u;k<8u;++k){
        const auto& s=totals[k];
        std::fprintf(out,
            "%s{\"method\":\"%s\",\"under\":%zu,\"over\":%zu,"
            "\"under_rgb_abs\":%llu,\"over_rgb_abs\":%llu,"
            "\"rgb_abs\":%llu,\"better\":%zu,\"worse\":%zu,"
            "\"equal\":%zu}",k?",":"",qp375_names[k],
            s.under,s.over,
            (unsigned long long)s.under_rgb_abs,
            (unsigned long long)s.over_rgb_abs,
            (unsigned long long)s.rgb_abs(),
            better[k],worse[k],equal[k]);
    }
    std::fprintf(out,"],\"by_dpi\":[");
    for(unsigned j=0u;j<3u;++j){
        std::fprintf(out,"%s[",j?",":"");
        for(unsigned k=0u;k<8u;++k){
            std::fprintf(out,"%s",k?",":"");
            qp269_rect_quant_print_v0(out,by_dpi[j][k]);
        }
        std::fprintf(out,"]");
    }
    std::fprintf(out,"]}\n");
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
    CHECK(std::rename(tmp.c_str(),report_path)==0);
    return EXIT_SUCCESS;
}
} /* new mirrored test-owned Q4 stage-order observations */
