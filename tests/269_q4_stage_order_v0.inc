/* #269 independent, TEST-ONLY source PDF Q4 stage-order experiment.
 * 18 distinct test-generated original QPDF byte streams, 54 PDFium frames:
 * three shapes (Fill, Fill+W, W-boundary), three dyadic phases, two
 * rotations (0/90), three DPI (72/96/144).
 * Not an independently published third-party corpus or Native cutover.
 */
namespace {
struct qp375_box { double x0,y0,x1,y1; };
static const char *const qp375_names[8]={
    "source_double_q4_fill_q4_clip",
    "source_double_q4_fill_outer_clip",
    "source_double_closest_fill_outer_clip_lower_tie",
    "source_double_closest_fill_outer_clip_upper_tie",
    "float_coeff_closest_fill_outer_clip_lower_tie",
    "float_coeff_closest_fill_outer_clip_upper_tie",
    "float_vertex_closest_fill_outer_clip_lower_tie",
    "float_vertex_closest_fill_outer_clip_upper_tie"
};
static qp375_box qp375_source_rect(unsigned family,unsigned phase,bool clip)
{
    CHECK(family<3u && phase<3u);
    const double y[3]={7.36328125,7.375,7.38671875};
    if(family==0u) {
        if(clip)return {1.25,2.5,25.25,20.5};
        return {4.4375,y[phase],17.1875,y[phase]+9.0};
    }
    if(family==1u) {
        if(clip)return {6.125,5.3125,15.125,18.8125};
        return {4.4375,y[phase],17.1875,y[phase]+9.0};
    }
    if(clip)return {5.0,y[phase],17.0,y[phase]+9.0};
    return {3.0,3.0,23.0,19.0};
}
static void qp375_write_pdf(const char *path,unsigned family,
                            unsigned phase,int rotation)
{
    CHECK(path && *path && family<3u && phase<3u &&
          (rotation==0 || rotation==90));
    const char *const ys[3]={"7.36328125","7.375","7.38671875"};
    std::string source;
    if(family==1u)source="6.125 5.3125 9 13.5 re W n ";
    if(family==2u){
        source="5 ";source+=ys[phase];source+=" 12 9 re W n ";
    }
    source+="1 0 0 rg ";
    if(family==2u)source+="3 3 20 16 re f";
    else{
        source+="4.4375 ";source+=ys[phase];source+=" 12.75 9 re f";
    }
    auto doc=QPDF::create();
    doc->emptyPDF();
    auto pages=doc->getRoot().getKey("/Pages");
    auto page=doc->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Page >>"));
    pages.replaceKey("/Kids",QPDFObjectHandle::newArray({page}));
    pages.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
    pages.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 30 24]"));
    page.replaceKey("/CropBox",QPDFObjectHandle::parse("[1.25 2.5 25.25 20.5]"));
    page.replaceKey("/Rotate",QPDFObjectHandle::newInteger(rotation));
    page.replaceKey("/UserUnit",QPDFObjectHandle::newInteger(1));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/Contents",doc->newStream(source));
    QPDFWriter writer(*doc);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    CHECK(bytes && bytes->getSize()>0u && bytes->getSize()<=8192u);
    FILE *f=std::fopen(path,"wb");CHECK(f);
    CHECK(std::fwrite(bytes->getBuffer(),1,bytes->getSize(),f)==bytes->getSize());
    CHECK(std::fclose(f)==0);
}
static qp269_float_box_v0 qp375_device_rect(
    const qp375_box& source,unsigned w,unsigned h,int rotation,
    unsigned precision)
{
    CHECK((rotation==0||rotation==90) && precision<3u &&
          w>0u && h>0u && w<=512u && h<=512u);
    const double sx=(double)w/24.0,sy=(double)h/18.0;
    double m[6];
    if(rotation==0){
        m[0]=sx;m[1]=0;m[2]=0;m[3]=-sy;
        m[4]=-1.25*sx;m[5]=20.5*sy;
    }else{
        m[0]=0;m[1]=(double)h/24.0;
        m[2]=(double)w/18.0;m[3]=0;
        m[4]=-2.5*m[2];m[5]=-1.25*m[1];
    }
    /* Float coefficient/vertex narrowing here is a GEOMETRIC MODEL,
     * not a claim about the PDFium internal CTM or a Native backend. */
    if(precision!=0u)
        for(unsigned k=0u;k<6u;++k)m[k]=(double)(float)m[k];
    const double xs[2]={source.x0,source.x1};
    const double ys[2]={source.y0,source.y1};
    qp269_float_box_v0 box{};
    for(unsigned i=0u;i<4u;++i){
        const double x=precision==0u?xs[i%2u]:(double)(float)xs[i%2u];
        const double y=precision==0u?ys[i/2u]:(double)(float)ys[i/2u];
        double px=m[0]*x+m[2]*y+m[4];
        double py=m[1]*x+m[3]*y+m[5];
        if(precision==2u){px=(double)(float)px;py=(double)(float)py;}
        CHECK(std::isfinite(px) && std::isfinite(py) &&
              px>=-1.0 && py>=-1.0 && px<=(double)w+1.0 &&
              py<=(double)h+1.0);
        if(i==0u)box={px,py,px,py};
        else{
            box.x0=std::min(box.x0,px);box.x1=std::max(box.x1,px);
            box.y0=std::min(box.y0,py);box.y1=std::max(box.y1,py);
        }
    }
    CHECK(box.x1>box.x0 && box.y1>box.y0);
    return box;
}
static int qp375_closest_start(double lo,double hi,bool high_tie,int *end)
{
    CHECK(end && std::isfinite(lo) && std::isfinite(hi) &&
          lo>=-65536.0 && hi<=65536.0 && hi>lo);
    const int extent=(int)std::ceil(hi-lo);
    const int down=(int)std::floor(lo),up=(int)std::ceil(lo);
    const double e_down=lo-down+std::fabs(hi-down-extent);
    const double e_up=up-lo+std::fabs(hi-up-extent);
    const int start=(high_tie?e_down>=e_up:e_down>e_up)?up:down;
    CHECK(extent>0 && extent<=65536);
    *end=start+extent;
    return start;
}
static qp269_rect_quant_box_v0 qp375_closest(
    const qp269_float_box_v0& box,bool high_tie)
{
    qp269_rect_quant_box_v0 out{};
    out.x0=qp375_closest_start(box.x0,box.x1,high_tie,&out.x1);
    out.y0=qp375_closest_start(box.y0,box.y1,high_tie,&out.y1);
    return out;
}
static void qp375_evaluate(const qp375_box& f,const qp375_box& c,
    int rotation,unsigned w,unsigned h,const unsigned char *ref,
    size_t stride,qp269_rect_quant_score_v0 scores[8],size_t changes[3])
{
    CHECK(ref && stride>=3u*w && w*h<=4096u);
    qp269_float_box_v0 fb[3]={},cb[3]={};
    qp269_rect_quant_box_v0 outer[3]={},closest[3][2]={};
    for(unsigned p=0u;p<3u;++p){
        fb[p]=qp375_device_rect(f,w,h,rotation,p);
        cb[p]=qp375_device_rect(c,w,h,rotation,p);
        outer[p]=qp269_rect_outer_v0(cb[p]);
        closest[p][0]=qp375_closest(fb[p],false);
        closest[p][1]=qp375_closest(fb[p],true);
    }
    for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x){
        unsigned hits=0u,out_hits=0u;
        for(unsigned j=0u;j<4u;++j)
          for(unsigned i=0u;i<4u;++i){
            const double px=(double)x+((double)i+0.5)/4.0;
            const double py=(double)y+((double)j+0.5)/4.0;
            const bool f_hit=qp269_float_inside_v0(fb[0],px,py);
            const bool c_hit=qp269_float_inside_v0(cb[0],px,py);
            hits+=(unsigned)(f_hit && c_hit);
            out_hits+=(unsigned)(f_hit &&
                qp269_rect_integer_contains_v0(outer[0],(int)x,(int)y));
          }
        unsigned a[8]={};
        a[0]=(255u*hits+8u)/16u;
        a[1]=(255u*out_hits+8u)/16u;
        for(unsigned p=0u;p<3u;++p)
          for(unsigned t=0u;t<2u;++t)
            a[2u+2u*p+t]=(qp269_rect_integer_contains_v0(
                 closest[p][t],(int)x,(int)y) &&
                 qp269_rect_integer_contains_v0(
                 outer[p],(int)x,(int)y))?255u:0u;
        changes[0]+=(unsigned)(a[2]!=a[3]);
        changes[1]+=(unsigned)(a[2]!=a[4]);
        changes[2]+=(unsigned)(a[4]!=a[6]);
        const unsigned char *rgb=ref+y*stride+x*3u;
        for(unsigned k=0u;k<8u;++k)
            qp269_rect_quant_add_pixel_v0(&scores[k],a[k],rgb);
    }
}
static int test_269_q4_stage_order_originals(const char *report_path)
{
    CHECK(report_path && *report_path);
    const char *source=GENERATED_269_Q4_STAGE_ORDER_PDF;
    const std::string temp=std::string(report_path)+".tmp";
    CHECK(source && *source && std::strcmp(source,report_path)!=0 &&
          temp!=source && corpus_fixture_executions==0u);
    (void)std::remove(temp.c_str());(void)std::remove(report_path);
    FILE *out=std::fopen(temp.c_str(),"wb");CHECK(out);
    qp269_rect_quant_score_v0 totals[8]={},per_dpi[3][8]={};
    size_t worse[8]={},better[8]={},same[8]={},flips[3]={};
    uint64_t hashes[18]={};
    size_t sources=0u,frames=0u,verified=0u,ref_repeats=0u;
    size_t ref_partial=0u;
    const unsigned dpis[3]={72u,96u,144u};
    std::fprintf(out,
      "{\"schema_version\":1,"
      "\"source_kind\":\"new_test_generated_original_pdf_not_external\","
      "\"reference_only_not_native_renderer\":true,"
      "\"independent_from_374_six_source_geometry\":true,"
      "\"no_renderer_or_sdk_mutation\":true,"
      "\"external_native_positive_increment\":0,"
      "\"models\":[");
    for(unsigned k=0u;k<8u;++k)
        std::fprintf(out,"\"%s\"%s",qp375_names[k],k==7u?"":",");
    std::fprintf(out,"],\"frames\":[");
    for(unsigned family=0u;family<3u;++family)
      for(unsigned phase=0u;phase<3u;++phase)
        for(unsigned ri=0u;ri<2u;++ri){
          const int rot=ri==0u?0:90;
          qp375_write_pdf(source,family,phase,rot);
          const auto original=decimal360_read_bytes(source);
          const uint64_t hash=decimal360_fnv64(original);
          for(size_t n=0u;n<sources;++n)CHECK(hashes[n]!=hash);
          CHECK(sources<18u);
          hashes[sources]=hash;
          quantapdf_document *doc=nullptr;
          quantapdf_page *page=nullptr;
          OK(quantapdf_open(source,nullptr,&doc));
          OK(quantapdf_load_page(doc,0u,&page));
          const qp375_box f=qp375_source_rect(family,phase,false);
          const qp375_box c=qp375_source_rect(family,phase,true);
          for(unsigned j=0u;j<3u;++j){
            const unsigned dpi=dpis[j];
            const unsigned w=(ri==0u?24u:18u)*dpi/72u;
            const unsigned h=(ri==0u?18u:24u)*dpi/72u;
            CHECK(w>0u && h>0u && w*h<=4096u);
            const quantapdf_render_options opts={
              sizeof(quantapdf_render_options),(float)dpi,0.0f,
              0,{0},0};
            quantapdf_bitmap *ref=nullptr,*again=nullptr;
            OK(quantapdf_render_page_with_options(page,&opts,&ref));
            OK(quantapdf_render_page_with_options(page,&opts,&again));
            int rw=0,rh=0,rs=0,rc=0,aw=0,ah=0,as=0,ac=0;
            OK(quantapdf_bitmap_dimensions(ref,&rw,&rh,&rs,&rc));
            OK(quantapdf_bitmap_dimensions(again,&aw,&ah,&as,&ac));
            CHECK(rw==(int)w && rh==(int)h && rc==3 &&
                  aw==rw && ah==rh && as==rs && ac==rc &&
                  rs>=(int)(w*3u) && rs<=(int)(w*3u+4u));
            const unsigned char *rgb=nullptr,*re_rgb=nullptr;
            size_t bytes=0u,re_bytes=0u;
            OK(quantapdf_bitmap_data(ref,&rgb,&bytes));
            OK(quantapdf_bitmap_data(again,&re_rgb,&re_bytes));
            CHECK(rgb && re_rgb && bytes==(size_t)rs*h &&
                  bytes==re_bytes && std::memcmp(rgb,re_rgb,bytes)==0);
            ++ref_repeats;
            for(size_t y=0u;y<h;++y)
              for(size_t x=0u;x<w;++x){
                const unsigned char *px=rgb+y*(size_t)rs+x*3u;
                CHECK(px[0]==255u && px[1]==px[2]);
                ref_partial+=(unsigned)(px[1]!=0u && px[1]!=255u);
              }
            CHECK(ref_partial==0u);
            qp269_rect_quant_score_v0 scores[8]={};
            size_t changes[3]={};
            qp375_evaluate(f,c,rot,w,h,rgb,(size_t)rs,scores,changes);
            for(unsigned k=0u;k<8u;++k){
                qp269_rect_quant_add_v0(&totals[k],scores[k]);
                qp269_rect_quant_add_v0(&per_dpi[j][k],scores[k]);
                if(scores[k].rgb_abs()>scores[0].rgb_abs())++worse[k];
                else if(scores[k].rgb_abs()<scores[0].rgb_abs())++better[k];
                else ++same[k];
            }
            for(unsigned k=0u;k<3u;++k)flips[k]+=changes[k];
            std::fprintf(out,
                "%s{\"frame\":%zu,\"source_index\":%zu,"
                "\"family\":%u,\"phase\":%u,\"rotation\":%d,"
                "\"dpi\":%u,\"source_fnv64\":\"%016llx\","
                "\"grid\":[%u,%u],\"partial\":0,"
                "\"tie_low_vs_high_pixels\":%zu,"
                "\"double_vs_float_coeff_pixels\":%zu,"
                "\"float_coeff_vs_vertex_pixels\":%zu,"
                "\"scores\":[",frames?",":"",frames,sources,
                family,phase,rot,dpi,(unsigned long long)hash,w,h,
                changes[0],changes[1],changes[2]);
            for(unsigned k=0u;k<8u;++k){
                std::fprintf(out,"%s{\"method\":\"%s\",",k?",":"",qp375_names[k]);
                const auto& s=scores[k];
                std::fprintf(out,
                    "\"under\":%zu,\"over\":%zu,"
                    "\"under_rgb_abs\":%llu,\"over_rgb_abs\":%llu,"
                    "\"rgb_abs\":%llu}",s.under,s.over,
                    (unsigned long long)s.under_rgb_abs,
                    (unsigned long long)s.over_rgb_abs,
                    (unsigned long long)s.rgb_abs());
            }
            std::fprintf(out,"]}");
            ++frames;
            quantapdf_drop_bitmap(again);
            quantapdf_drop_bitmap(ref);
          }
          quantapdf_drop_page(page);
          quantapdf_close(doc);
          CHECK(decimal360_read_bytes(source)==original);
          CHECK(std::remove(source)==0);
          ++verified;
          ++sources;
        }
    CHECK(sources==18u && verified==18u && frames==54u &&
          ref_repeats==54u && ref_partial==0u &&
          corpus_fixture_executions==0u);
    std::fprintf(out,
      "],\"source_count\":%zu,\"source_bytes_verified\":%zu,"
      "\"reference_repeat_identical\":%zu,\"frame_count\":%zu,"
      "\"reference_partial_pixels\":%zu,\"tie_sensitive_pixels\":%zu,"
      "\"double_to_float_coeff_pixels\":%zu,"
      "\"float_coeff_to_vertex_pixels\":%zu,\"global_scores\":[",
      sources,verified,ref_repeats,frames,ref_partial,
      flips[0],flips[1],flips[2]);
    for(unsigned k=0u;k<8u;++k){
        const auto& s=totals[k];
        std::fprintf(out,
          "%s{\"method\":\"%s\",\"under\":%zu,\"over\":%zu,"
          "\"under_rgb_abs\":%llu,\"over_rgb_abs\":%llu,"
          "\"rgb_abs\":%llu,\"worse_than_q4\":%zu,"
          "\"better_than_q4\":%zu,\"equal_q4\":%zu}",
          k?",":"",qp375_names[k],s.under,s.over,
          (unsigned long long)s.under_rgb_abs,
          (unsigned long long)s.over_rgb_abs,
          (unsigned long long)s.rgb_abs(),worse[k],better[k],same[k]);
    }
    std::fprintf(out,"],\"by_dpi\":[");
    for(unsigned j=0u;j<3u;++j){
        std::fprintf(out,"%s[",j?",":"");
        for(unsigned k=0u;k<8u;++k){
            std::fprintf(out,"%s",k?",":"");
            qp269_rect_quant_print_v0(out,per_dpi[j][k]);
        }
        std::fprintf(out,"]");
    }
    std::fprintf(out,"]}\n");
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
    CHECK(std::rename(temp.c_str(),report_path)==0);
    return EXIT_SUCCESS;
}
} /* #269 new test-generated source stage-order observation */
