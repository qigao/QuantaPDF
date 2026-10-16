/* #269: TEST ONLY. Diagnostic PDFium-style rectangle quantization:
 * nearest integer-sized Fill box; outward integer Clip box.
 * The 96 original Q4 frames are an already observed family and NOT independent.
 * A separate six-original 96DPI phase falsification family is below.
 * No general rectangle optimization, production renderer or PDFium fallback.
 */
struct qp269_rect_quant_box_v0 { int x0,y0,x1,y1; };
struct qp269_rect_quant_score_v0 {
    uint64_t under_rgb_abs=0u,over_rgb_abs=0u;
    size_t under=0u,over=0u;
    uint64_t rgb_abs() const {return under_rgb_abs+over_rgb_abs;}
};
static void qp269_rect_quant_add_v0(qp269_rect_quant_score_v0 *a,
                                    const qp269_rect_quant_score_v0& b)
{
    a->under+=b.under;a->over+=b.over;
    a->under_rgb_abs+=b.under_rgb_abs;a->over_rgb_abs+=b.over_rgb_abs;
}
static void qp269_rect_quant_print_v0(FILE *out,
                                       const qp269_rect_quant_score_v0& s)
{
    std::fprintf(out,
        "{\"under\":%zu,\"over\":%zu,\"under_rgb_abs\":%llu,"
        "\"over_rgb_abs\":%llu,\"rgb_abs\":%llu}",
        s.under,s.over,(unsigned long long)s.under_rgb_abs,
        (unsigned long long)s.over_rgb_abs,(unsigned long long)s.rgb_abs());
}
static int qp269_rect_closest_left_v0(double low,double high,int *right)
{
    CHECK(right && std::isfinite(low) && std::isfinite(high) &&
          low>=-65536.0 && high<=65536.0 && high>low);
    const int length=(int)std::ceil(high-low);
    const int down=(int)std::floor(low),up=(int)std::ceil(low);
    const double e_down=low-(double)down+
        std::fabs(high-(double)down-(double)length);
    const double e_up=(double)up-low+
        std::fabs(high-(double)up-(double)length);
    const int chosen=e_down>e_up?up:down;
    CHECK(chosen>=-65536 && length>0 && length<=65536);
    *right=chosen+length;
    return chosen;
}
static qp269_rect_quant_box_v0 qp269_rect_closest_v0(
    const qp269_float_box_v0& r)
{
    qp269_rect_quant_box_v0 out{};
    out.x0=qp269_rect_closest_left_v0(r.x0,r.x1,&out.x1);
    out.y0=qp269_rect_closest_left_v0(r.y0,r.y1,&out.y1);
    return out;
}
static qp269_rect_quant_box_v0 qp269_rect_outer_v0(
    const qp269_float_box_v0& r)
{
    CHECK(std::isfinite(r.x0) && std::isfinite(r.y0) &&
          std::isfinite(r.x1) && std::isfinite(r.y1) &&
          r.x0>=-65536.0 && r.y0>=-65536.0 &&
          r.x1<=65536.0 && r.y1<=65536.0 &&
          r.x1>r.x0 && r.y1>r.y0);
    return {(int)std::floor(r.x0),(int)std::floor(r.y0),
            (int)std::ceil(r.x1),(int)std::ceil(r.y1)};
}
static bool qp269_rect_integer_contains_v0(
    const qp269_rect_quant_box_v0& r,int x,int y)
{
    return x>=r.x0 && x<r.x1 && y>=r.y0 && y<r.y1;
}
static void qp269_rect_quant_add_pixel_v0(
    qp269_rect_quant_score_v0 *score,unsigned predicted_cov,
    const unsigned char *original)
{
    CHECK(score && original && predicted_cov<=255u &&
          original[0]==255u && original[1]==original[2] &&
          (original[1]==0u || original[1]==255u));
    const unsigned reference_cov=255u-(unsigned)original[1];
    if(predicted_cov<reference_cov){
        ++score->under;
        score->under_rgb_abs+=2u*(uint64_t)(reference_cov-predicted_cov);
    }else if(predicted_cov>reference_cov){
        ++score->over;
        score->over_rgb_abs+=2u*(uint64_t)(predicted_cov-reference_cov);
    }
}
static void qp269_rect_quant_models_v0(
    const qp269_float_box_v0& fill,const qp269_float_box_v0& clip,
    const unsigned char *ref,size_t w,size_t h,size_t stride,
    qp269_rect_quant_score_v0 *q4,qp269_rect_quant_score_v0 *candidate)
{
    CHECK(q4 && candidate && ref && w>0u && h>0u &&
          w<=512u && h<=512u && w<=stride/3u);
    const auto nearest=qp269_rect_closest_v0(fill);
    const auto outer=qp269_rect_outer_v0(clip);
    for(size_t y=0u;y<h;++y)
      for(size_t x=0u;x<w;++x){
        unsigned hits=0u;
        for(unsigned sy=0u;sy<4u;++sy)
          for(unsigned sx=0u;sx<4u;++sx){
            const double px=(double)x+((double)sx+0.5)/4.0;
            const double py=(double)y+((double)sy+0.5)/4.0;
            hits+=(unsigned)(qp269_float_inside_v0(fill,px,py) &&
                             qp269_float_inside_v0(clip,px,py));
          }
        const unsigned q4_alpha=(255u*hits+8u)/16u;
        const unsigned snap_alpha=qp269_rect_integer_contains_v0(
            nearest,(int)x,(int)y) &&
            qp269_rect_integer_contains_v0(outer,(int)x,(int)y)?255u:0u;
        const auto *pixel=ref+y*stride+x*3u;
        qp269_rect_quant_add_pixel_v0(q4,q4_alpha,pixel);
        qp269_rect_quant_add_pixel_v0(candidate,snap_alpha,pixel);
      }
}
static qp269_float_box_v0 qp269_rect_rotate90_device_v0(
    const qp269_heldout_rect_v0& src,uint32_t width,uint32_t height)
{
    /* 90°/CropBox=[1.25,2.5,25.25,20.5], positive Y display mapping.
     * Source PDF literals, not a projection produced from Native or PDFium. */
    return {(src.y0-2.5)*(double)width/18.0,
            (src.x0-1.25)*(double)height/24.0,
            (src.y1-2.5)*(double)width/18.0,
            (src.x1-1.25)*(double)height/24.0};
}
static void qp269_rect_phase_source_v0(
    const char *path,unsigned phase,bool clipped)
{
    /* Vary PAINTED Y edge: Rotate 90 projects it to device X.
     * Three phases 6.484375, 6.5 and 6.515625 px at 96 DPI. */
    const char *const fill_y[3]={
        "7.36328125","7.375","7.38671875"};
    CHECK(path && phase<3u);
    std::string commands;
    if(clipped)
        commands="6.125 5.3125 9 13.5 re W n ";
    commands+="1 0 0 rg 4.4375 ";
    commands+=fill_y[phase];
    commands+=" 12.75 9 re f";
    auto doc=QPDF::create();
    doc->emptyPDF();
    auto pages=doc->getRoot().getKey("/Pages");
    auto page=doc->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    pages.replaceKey("/Kids",QPDFObjectHandle::newArray({page}));
    pages.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
    pages.replaceKey("/MediaBox",
        QPDFObjectHandle::parse("[0 0 30 24]"));
    page.replaceKey("/CropBox",
        QPDFObjectHandle::parse("[1.25 2.5 25.25 20.5]"));
    page.replaceKey("/Rotate",QPDFObjectHandle::newInteger(90));
    page.replaceKey("/UserUnit",QPDFObjectHandle::newInteger(1));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/Contents",doc->newStream(commands));
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
static void qp269_rect_phase_falsifiers_v0(FILE *out,const char *source)
{
    /* Three independent PAINTED Y-edge phases projected to device X:
     * 6.484375, 6.5 and 6.515625 pixels at 96 DPI under Rotate 90.
     * Fill-only versus Fill+fixed W Clip: six distinct source byte streams.
     * These are test-generated, not third-party source families.
     * Exact half-pixel ties must falsify the integer-rectangle hypothesis;
     * both +/-1/64px flanks must remain independent controls. */
    const unsigned dpis[3]={72u,96u,144u};
    const double fill_starts[3]={7.36328125,7.375,7.38671875};
    const qp269_heldout_rect_v0 clip={6.125,5.3125,15.125,18.8125};
    const qp269_heldout_rect_v0 complete={1.25,2.5,25.25,20.5};
    uint64_t distinct_hashes[6]={};
    size_t sources=0u,verified=0u,frames=0u,regressed96=0u,flank96=0u;
    qp269_rect_quant_score_v0 total_q4{},total_snap{};
    std::fprintf(out,"{\"source_kind\":\"six_new_test_generated_original_pdf\","
        "\"external_native_positive_increment\":0,"
        "\"native_renderer_modified\":false,"
        "\"modeled_q4_not_production_native\":true,"
        "\"phase_boundary_is_pdfium_oracle_not_fallback\":true,"
        "\"samples\":[");
    for(unsigned phase=0u;phase<3u;++phase)
      for(unsigned mode=0u;mode<2u;++mode){
        qp269_rect_phase_source_v0(source,phase,mode==1u);
        const auto original=decimal360_read_bytes(source);
        const uint64_t source_hash=decimal360_fnv64(original);
        for(size_t n=0u;n<sources;++n)
            CHECK(source_hash!=distinct_hashes[n]);
        CHECK(sources<6u);
        distinct_hashes[sources]=source_hash;
        const qp269_heldout_rect_v0 fill={
            4.4375,fill_starts[phase],17.1875,fill_starts[phase]+9.0};
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        OK(quantapdf_open(source,nullptr,&doc));
        OK(quantapdf_load_page(doc,0u,&page));
        ++sources;
        for(unsigned j=0u;j<3u;++j){
            const unsigned dpi=dpis[j];
            const uint32_t w=dpi==72u?18u:(dpi==96u?24u:36u);
            const uint32_t h=dpi==72u?24u:(dpi==96u?32u:48u);
            const auto box_fill=qp269_rect_rotate90_device_v0(fill,w,h);
            const auto box_clip=qp269_rect_rotate90_device_v0(
                mode?clip:complete,w,h);
            const quantapdf_render_options opts={
                sizeof(quantapdf_render_options),(float)dpi,0.0f,
                0,{0},0};
            quantapdf_bitmap *bmp=nullptr,*again=nullptr;
            OK(quantapdf_render_page_with_options(page,&opts,&bmp));
            OK(quantapdf_render_page_with_options(page,&opts,&again));
            int bw=0,bh=0,bs=0,bc=0,rw=0,rh=0,rs=0,rc=0;
            OK(quantapdf_bitmap_dimensions(bmp,&bw,&bh,&bs,&bc));
            OK(quantapdf_bitmap_dimensions(again,&rw,&rh,&rs,&rc));
            CHECK(bw==(int)w && bh==(int)h && bc==3 &&
                  rw==bw && rh==bh && rs==bs && rc==bc &&
                  bs>=(int)(3u*w) && bs<=((int)(3u*w)+4));
            const unsigned char *pixels=nullptr,*repeat=nullptr;
            size_t bytes=0u,rebytes=0u;
            OK(quantapdf_bitmap_data(bmp,&pixels,&bytes));
            OK(quantapdf_bitmap_data(again,&repeat,&rebytes));
            CHECK(pixels && repeat && bytes==(size_t)bs*h &&
                  rebytes==bytes && std::memcmp(pixels,repeat,bytes)==0);
            qp269_rect_quant_score_v0 q4{},snap{};
            qp269_rect_quant_models_v0(box_fill,box_clip,pixels,w,h,bs,
                                      &q4,&snap);
            qp269_rect_quant_add_v0(&total_q4,q4);
            qp269_rect_quant_add_v0(&total_snap,snap);
            if(dpi==96u && phase==1u){
                /* Two distinct PDFs pin the exact half-pixel tie. */
                CHECK(snap.rgb_abs()>q4.rgb_abs() &&
                      snap.under_rgb_abs>0u && snap.over_rgb_abs>0u);
                ++regressed96;
            }else if(dpi==96u){
                /* Four +/-1/64px flank observations protect against
                 * generalizing the exact-tie mismatch across phases. */
                CHECK(snap.rgb_abs()==0u && snap.under==0u &&
                      snap.over==0u);
                ++flank96;
            }
            std::fprintf(out,
                "{\"frame\":%zu,\"phase\":%u,\"mode\":\"%s\","
                "\"dpi\":%u,\"rotation\":90,\"source_fnv64\":\"%016llx\","
                "\"source_fill_y\":%.8f,\"device_x_left_96\":%.8f,"
                "\"q4\":",frames,phase,mode?"fill_and_w_clip":"fill_only",
                dpi,(unsigned long long)source_hash,fill_starts[phase],
                (fill_starts[phase]-2.5)*24.0/18.0);
            qp269_rect_quant_print_v0(out,q4);
            std::fprintf(out,",\"closest_fill_outer_clip\":");
            qp269_rect_quant_print_v0(out,snap);
            std::fprintf(out,"}%s",frames==17u?"":",");
            ++frames;
            quantapdf_drop_bitmap(again);
            quantapdf_drop_bitmap(bmp);
        }
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        CHECK(decimal360_read_bytes(source)==original);
        CHECK(std::remove(source)==0);
        ++verified;
      }
    CHECK(sources==6u && verified==6u && frames==18u &&
          regressed96==2u && flank96==4u &&
          corpus_fixture_executions==0u);
    std::fprintf(out,"],\"source_count\":%zu,\"source_bytes_verified\":%zu,"
        "\"reference_frames\":%zu,\"pinned_96dpi_regression_frames\":%zu,"
        "\"pinned_96dpi_flank_controls\":%zu,"
        "\"q4_total\":",sources,verified,frames,regressed96,flank96);
    qp269_rect_quant_print_v0(out,total_q4);
    std::fprintf(out,",\"nearest_fill_outer_clip_total\":");
    qp269_rect_quant_print_v0(out,total_snap);
    std::fprintf(out,"}");
}
