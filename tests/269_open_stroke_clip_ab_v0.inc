/* #269 TEST ONLY: single open-segment Stroke, three Native/direct-PDFium controls.
 * Unlike the closed triangle in #378, this has no joins or miter geometry.
 * The 72-DPI reference is rendered directly from the unchanged source via
 * PDFium C API. The 4x box-average remains a separate public-renderer
 * resampling sensitivity diagnostic, never PDFium internal coverage.
 */
namespace {
static std::string qp379_sha256(const std::vector<unsigned char>& input)
{
    static const uint32_t k[64]={
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,
      0x92722c85u,0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,
      0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,
      0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,
      0xc67178f2u};
    auto rotr=[](uint32_t x,unsigned n){return (x>>n)|(x<<(32u-n));};
    std::vector<unsigned char> msg=input;
    const uint64_t bits=(uint64_t)msg.size()*8u;
    msg.push_back(0x80u);
    while((msg.size()%64u)!=56u)msg.push_back(0u);
    for(int i=7;i>=0;--i)msg.push_back((unsigned char)(bits>>(unsigned(i)*8u)));
    uint32_t h[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                   0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    for(size_t off=0;off<msg.size();off+=64u){
        uint32_t w[64]{};
        for(unsigned i=0;i<16u;++i){
            const size_t p=off+(size_t)i*4u;
            w[i]=(uint32_t(msg[p])<<24)|(uint32_t(msg[p+1])<<16)|
                 (uint32_t(msg[p+2])<<8)|uint32_t(msg[p+3]);
        }
        for(unsigned i=16u;i<64u;++i){
            const uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            const uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for(unsigned i=0;i<64u;++i){
            const uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25);
            const uint32_t ch=(e&f)^((~e)&g);
            const uint32_t t1=z+s1+ch+k[i]+w[i];
            const uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22);
            const uint32_t maj=(a&b)^(a&c)^(b&c);
            const uint32_t t2=s0+maj;
            z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    }
    char out[65];
    std::snprintf(out,sizeof(out),
      "%08x%08x%08x%08x%08x%08x%08x%08x",
      h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7]);
    return std::string(out);
}
struct qp379_line_capture {
    std::vector<unsigned char> ref_rgb;
    std::vector<unsigned char> ref_rgb_4x_box;
    std::vector<unsigned char> native_rgba;
    uint64_t source_fnv=0u;
    std::string source_sha256_before;
    std::string source_sha256_after;
    size_t source_size_bytes_before=0u;
    size_t source_size_bytes_after=0u;
};
static const char *const qp379_line_paint =
    "0 0 1 RG\n10 10 m\n40 35 l\nS\n";
static const char *const qp379_line_full_w = "0 0 100 50 re W n\n";
static const char *const qp379_line_restrict_w =
    "18 15 m\n35 15 l\n35 30 l\n18 30 l\nW n\n";

static void qp379_write_line_pdf(const char *path,unsigned variant)
{
    CHECK(path && *path && variant<=2u);
    std::string commands;
    if(variant==1u)commands=qp379_line_full_w;
    if(variant==2u)commands=qp379_line_restrict_w;
    commands+=qp379_line_paint;
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    pages.replaceKey("/Kids",QPDFObjectHandle::newArray({page}));
    pages.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
    pages.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 100 50]"));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/Contents",pdf->newStream(commands));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.7");
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setOutputMemory();
    writer.write();
    auto data=writer.getBufferSharedPointer();
    CHECK(data && data->getSize()>0u && data->getSize()<=8192u);
    FILE *out=std::fopen(path,"wb");CHECK(out);
    CHECK(std::fwrite(data->getBuffer(),1,data->getSize(),out)==data->getSize());
    CHECK(std::fclose(out)==0);
}

/* Test-only sampling probe: render through quantapdf_render_page_with_options
 * at 4x DPI, then box-average each 4x4 RGB block to the original 100x50 grid.
 * The 72-DPI baseline is rendered by the direct-PDFium helper; only this
 * 4x sensitivity path calls the public QuantaPDF renderer. It is not PDFium
 * internal coverage data. */
static std::vector<unsigned char> qp379_render_4x_box_reference(const char *source)
{
    constexpr size_t W=100u,H=50u,S=4u,OUT_N=W*H*3u;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    const unsigned char *bytes=nullptr;
    size_t size=0u;
    int width=0,height=0,stride=0,components=0;
    quantapdf_render_options options{};
    options.struct_size=sizeof(options);
    options.dpi=288.0f;
    options.rotation_degrees=0.0f;
    options.clip_enabled=0;
    options.alpha=0;
    OK(quantapdf_open(source,nullptr,&doc));
    OK(quantapdf_load_page(doc,0u,&page));
    OK(quantapdf_render_page_with_options(page,&options,&bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap,&width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(bitmap,&bytes,&size));
    CHECK(width==(int)(W*S) && height==(int)(H*S) && components==3 &&
          stride>=width*3 && size>=(size_t)stride*(size_t)height);
    std::vector<unsigned char> out(OUT_N);
    for(size_t y=0;y<H;++y)for(size_t x=0;x<W;++x)for(size_t c=0;c<3u;++c){
        unsigned sum=0u;
        for(size_t sy=0;sy<S;++sy)for(size_t sx=0;sx<S;++sx){
            const size_t hi_x=x*S+sx,hi_y=y*S+sy;
            sum+=(unsigned)bytes[hi_y*(size_t)stride+hi_x*3u+c];
        }
        out[(y*W+x)*3u+c]=(unsigned char)((sum+8u)/16u);
    }
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return out;
}

static qp379_line_capture qp379_capture_line(const char *source,
                                              unsigned expected_clips)
{
    constexpr size_t W=100u,H=50u,P=W*H;
    const auto original=decimal360_read_bytes(source);
    CHECK(!original.empty() && original.size()<=8192u);
    qp379_line_capture out{};
    out.source_fnv=decimal360_fnv64(original);
    out.source_sha256_before=qp379_sha256(original);
    out.source_size_bytes_before=original.size();
    const rgb_image ref=render_direct_pdfium_reference(0u,source);
    const rgb_image ref_again=render_direct_pdfium_reference(0u,source);
    CHECK(ref.width==(int)W && ref.height==(int)H && ref.components==3 &&
          ref.stride>=3*(int)W && ref.pixels.size()>=(size_t)ref.stride*H &&
          ref.width==ref_again.width && ref.height==ref_again.height &&
          ref.stride==ref_again.stride && ref.pixels==ref_again.pixels);
    out.ref_rgb.resize(P*3u);
    for(size_t y=0;y<H;++y)
        std::memcpy(out.ref_rgb.data()+y*W*3u,ref.at(0u,y),W*3u);
    out.ref_rgb_4x_box=qp379_render_4x_box_reference(source);
    CHECK(out.ref_rgb_4x_box.size()==P*3u);

    quantapdf_ir detached{};
    OK(snapshot_lower(&detached,0u,1u,2u,source));
    CHECK(detached.page_count==1u && detached.pages &&
          detached.pages[0].width_points==(float)W &&
          detached.pages[0].height_points==(float)H &&
          detached.pages[0].suppress_background==1 &&
          detached.operations && detached.operation_count==1u &&
          detached.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
          detached.operations[0].value.path.options.fill==0 &&
          detached.operations[0].value.path.options.stroke==1 &&
          detached.operations[0].value.path.options.stroke_width==1.0f &&
          detached.operations[0].value.path.options.stroke_argb==
              UINT32_C(0xff0000ff) &&
          detached.resources.clip_count==expected_clips &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.soft_mask_count==0u &&
          detached.resources.form_count==0u &&
          detached.resources.paint_count==0u);
    const quantapdf_ir_raster_v1_limits limits={P,1u,128u,5000000u};

    /* Fail-closed regression: the scoped API only accepts integral point
     * dimensions. Seed every output field with nonzero sentinels, pass an
     * unsupported fractional page size, and require a fully cleared result
     * with no published/partial pixel buffer. Restore the detached IR before
     * the supported rendering calls below. */
    quantapdf_ir_raster_image rejected;
    std::memset(&rejected,0xa5,sizeof(rejected));
    const float supported_width=detached.pages[0].width_points;
    detached.pages[0].width_points=supported_width+0.25f;
    const quantapdf_status rejected_status=
        quantapdf_ir_raster_scoped_operations_v1(
            &detached,0u,&limits,&rejected);
    detached.pages[0].width_points=supported_width;
    CHECK(rejected_status==QUANTAPDF_ERROR_UNSUPPORTED &&
          rejected.pixels==nullptr && rejected.width==0u &&
          rejected.height==0u && rejected.stride==0u && rejected.size==0u);

    quantapdf_ir_raster_image native{},again{};
    const quantapdf_status s1=quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&native);
    const quantapdf_status s2=quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&again);
    CHECK(s1==QUANTAPDF_OK && s2==QUANTAPDF_OK &&
          native.pixels && again.pixels && native.width==W &&
          native.height==H && native.stride==W*4u &&
          native.size==P*4u && again.size==native.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0);
    out.native_rgba.assign(native.pixels,native.pixels+native.size);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_dispose(&detached);
    const auto final_bytes=decimal360_read_bytes(source);
    out.source_sha256_after=qp379_sha256(final_bytes);
    out.source_size_bytes_after=final_bytes.size();
    CHECK(final_bytes==original && out.source_size_bytes_before==out.source_size_bytes_after &&
          out.source_sha256_before==out.source_sha256_after);
    return out;
}


/* Independent test-side geometry model for diagnosis only. This models a
 * 4x4 grid of subpixel centers against the one open 1pt segment. It does not
 * assert that PDFium uses this antialiasing rule or alter either renderer. */
struct qp379_sample_prediction {
    unsigned raw_hits;
    unsigned clipped_hits;
    double projection;
    double perpendicular_distance;
    bool clip_center_included;
    const char *endpoint_region;
};
static qp379_sample_prediction qp379_predict_samples(size_t x,size_t y,
                                                       unsigned clip_state)
{
    constexpr double ax=10.0, ay=10.0, bx=40.0, by=35.0;
    constexpr double dx=bx-ax, dy=by-ay, len2=dx*dx+dy*dy;
    const double cx=(double)x+0.5;
    const double cy_pdf=50.0-((double)y+0.5);
    const double vx=cx-ax, vy=cy_pdf-ay;
    const double projection=(vx*dx+vy*dy)/len2;
    const double clamped=std::max(0.0,std::min(1.0,projection));
    const double qx=ax+clamped*dx, qy=ay+clamped*dy;
    const double ex=cx-qx, ey=cy_pdf-qy;
    const double distance=std::sqrt(ex*ex+ey*ey);
    const bool center_clip = clip_state==0u ||
        (clip_state==1u) ||
        (cx>=18.0 && cx<=35.0 && cy_pdf>=15.0 && cy_pdf<=30.0);
    unsigned raw=0u, clipped=0u;
    for(unsigned sy=0;sy<4u;++sy)for(unsigned sx=0;sx<4u;++sx){
        const double px=(double)x+((double)sx+0.5)/4.0;
        const double py=50.0-((double)y+((double)sy+0.5)/4.0);
        const double ux=px-ax, uy=py-ay;
        const double t=(ux*dx+uy*dy)/len2;
        if(t<0.0 || t>1.0)continue; /* Butt-cap segment extent. */
        const double tx=ax+t*dx, ty=ay+t*dy;
        const double ddx=px-tx, ddy=py-ty;
        if(ddx*ddx+ddy*ddy<=0.25){
            ++raw;
            const bool in_clip=clip_state!=2u ||
                (px>=18.0 && px<=35.0 && py>=15.0 && py<=30.0);
            if(in_clip)++clipped;
        }
    }
    const char *endpoint=projection<0.0?"before_start":
        (projection>1.0?"after_end":"segment_interior");
    return {raw,clipped,projection,distance,center_clip,endpoint};
}

static int test_269_open_stroke_clip_ab(const char *published_unused,
                                        const char *report_path)
{
    (void)published_unused;
    CHECK(report_path && *report_path && corpus_fixture_executions==0u);
    constexpr size_t P=5000u;
    const char *path=GENERATED_269_OPEN_STROKE_AB_PDF;
    qp379_line_capture rows[3];
    uint64_t hashes[3]{};
    for(unsigned i=0;i<3u;++i){
        qp379_write_line_pdf(path,i);
        rows[i]=qp379_capture_line(path,i==0u?0u:1u);
        hashes[i]=rows[i].source_fnv;
        CHECK(std::remove(path)==0);
        for(unsigned j=0;j<i;++j)CHECK(hashes[i]!=hashes[j]);
    }
    const size_t rgb_n=P*3u,rgba_n=P*4u;
    for(unsigned i=0;i<3u;++i)
        CHECK(rows[i].ref_rgb.size()==rgb_n &&
              rows[i].ref_rgb_4x_box.size()==rgb_n &&
              rows[i].native_rgba.size()==rgba_n);
    auto native_white=[](const qp379_line_capture& r,size_t p,unsigned c) {
        return (unsigned char)on_white(&r.native_rgba[p*4u],c);
    };
    CHECK(std::memcmp(rows[0].ref_rgb.data(),rows[1].ref_rgb.data(),rgb_n)==0);
    for(size_t p=0;p<P;++p)for(unsigned c=0;c<3u;++c)
        CHECK(native_white(rows[0],p,c)==native_white(rows[1],p,c));

    size_t ref_restrict_changed=0,native_restrict_changed=0;
    uint64_t ref_restrict_rgb_abs=0,native_restrict_rgb_abs=0;
    size_t ref_native_errors[3]{},rgb_abs[3]{};
    size_t highres_changed_vs_72[3]{},native_errors_vs_highres[3]{};
    uint64_t highres_rgb_abs_vs_72[3]{},native_rgb_abs_vs_highres[3]{};
    size_t err_min_x[3]={100u,100u,100u},err_min_y[3]={50u,50u,50u};
    size_t err_max_x[3]{},err_max_y[3]{};
    size_t err_rows[3][50]{},err_cols[3][100]{};
    for(unsigned i=0;i<3u;++i){
        for(size_t p=0;p<P;++p){
            bool pixel_error=false,highres_changed=false,native_highres_error=false;
            for(unsigned c=0;c<3u;++c){
                const int a=rows[i].ref_rgb[p*3u+c];
                const int b=(int)native_white(rows[i],p,c);
                pixel_error|=a!=b;
                rgb_abs[i]+=(uint64_t)std::abs(a-b);
                const int hi=(int)rows[i].ref_rgb_4x_box[p*3u+c];
                highres_changed|=a!=hi;
                native_highres_error|=b!=hi;
                highres_rgb_abs_vs_72[i]+=(uint64_t)std::abs(a-hi);
                native_rgb_abs_vs_highres[i]+=(uint64_t)std::abs(b-hi);
            }
            if(highres_changed)++highres_changed_vs_72[i];
            if(native_highres_error)++native_errors_vs_highres[i];
            if(pixel_error){
                const size_t x=p%100u,y=p/100u;
                if(x<err_min_x[i])err_min_x[i]=x;
                if(y<err_min_y[i])err_min_y[i]=y;
                if(x>err_max_x[i])err_max_x[i]=x;
                if(y>err_max_y[i])err_max_y[i]=y;
                ++err_rows[i][y];
                ++err_cols[i][x];
                ++ref_native_errors[i];
            }
        }
    }
    size_t ref_clip_min_x=100u,ref_clip_min_y=50u,ref_clip_max_x=0u,ref_clip_max_y=0u;
    size_t native_clip_min_x=100u,native_clip_min_y=50u,native_clip_max_x=0u,native_clip_max_y=0u;
    size_t ref_clip_rows[50]{},ref_clip_cols[100]{};
    size_t native_clip_rows[50]{},native_clip_cols[100]{};
    for(size_t p=0;p<P;++p){
        bool r=false,n=false;
        for(unsigned c=0;c<3u;++c){
            r|=rows[0].ref_rgb[p*3u+c]!=rows[2].ref_rgb[p*3u+c];
            n|=native_white(rows[0],p,c)!=native_white(rows[2],p,c);
            ref_restrict_rgb_abs+=(uint64_t)std::abs((int)rows[0].ref_rgb[p*3u+c]-(int)rows[2].ref_rgb[p*3u+c]);
            native_restrict_rgb_abs+=(uint64_t)std::abs((int)native_white(rows[0],p,c)-(int)native_white(rows[2],p,c));
        }
        if(r){
            const size_t x=p%100u,y=p/100u;
            if(x<ref_clip_min_x)ref_clip_min_x=x;
            if(y<ref_clip_min_y)ref_clip_min_y=y;
            if(x>ref_clip_max_x)ref_clip_max_x=x;
            if(y>ref_clip_max_y)ref_clip_max_y=y;
            ++ref_clip_rows[y];++ref_clip_cols[x];
            ++ref_restrict_changed;
        }
        if(n){
            const size_t x=p%100u,y=p/100u;
            if(x<native_clip_min_x)native_clip_min_x=x;
            if(y<native_clip_min_y)native_clip_min_y=y;
            if(x>native_clip_max_x)native_clip_max_x=x;
            if(y>native_clip_max_y)native_clip_max_y=y;
            ++native_clip_rows[y];++native_clip_cols[x];
            ++native_restrict_changed;
        }
    }
    CHECK(ref_restrict_changed>0u && native_restrict_changed>0u);
    const std::string staging_path=std::string(report_path)+".tmp";
    errno=0;
    if(std::remove(report_path)!=0 && errno!=ENOENT)return EXIT_FAILURE;
    errno=0;
    if(std::remove(staging_path.c_str())!=0 && errno!=ENOENT)return EXIT_FAILURE;
    FILE *out=std::fopen(staging_path.c_str(),"wb");
    if(!out)return EXIT_FAILURE;
    std::fprintf(out,
      "{\"schema_version\":1,\"open_line_only\":true,"
      "\"reference_render_route\":\"direct_pdfium_api\","
      "\"reference_4x_render_route\":\"quantapdf_render_page_with_options\","
      "\"reference_is_independent_pdfium_oracle\":true,"
      "\"reference_independence_verified\":true,"
      "\"pdfium_bitmap_width\":100,"
      "\"pdfium_bitmap_height\":50,"
      "\"pdfium_bitmap_rgb_stride\":300,"
      "\"pdfium_bitmap_format\":\"FPDFBitmap_BGR normalized to RGB\","
      "\"pdfium_alpha_policy\":\"opaque RGB output\","
      "\"pdfium_background\":\"opaque white\","
      "\"pdfium_render_flags\":\"FPDF_ANNOT\","
      "\"direct_pdfium_repeat_identical\":true,"
      "\"native_repeat_rgba_identical\":true,"
      "\"source_bytes_unchanged_after_reference_and_native\":true,"
      "\"no_clip_and_fullpage_w_reference_equal\":true,"
      "\"no_clip_and_fullpage_w_native_equal\":true,"
      "\"all_three_native_supported\":true,"
      "\"restrictive_w_changes_reference\":%zu,"
      "\"restrictive_w_changes_native\":%zu,"
      "\"restrictive_w_reference_rgb_abs_delta\":%llu,"
      "\"restrictive_w_native_rgb_abs_delta\":%llu,"
      "\"restrictive_w_reference_bounds\":[%zu,%zu,%zu,%zu],"
      "\"restrictive_w_native_bounds\":[%zu,%zu,%zu,%zu],"
      "\"cases\":[",ref_restrict_changed,native_restrict_changed,
      (unsigned long long)ref_restrict_rgb_abs,
      (unsigned long long)native_restrict_rgb_abs,
      ref_clip_min_x,ref_clip_min_y,ref_clip_max_x,ref_clip_max_y,
      native_clip_min_x,native_clip_min_y,native_clip_max_x,native_clip_max_y);
    for(unsigned i=0;i<3u;++i){
        std::fprintf(out,"%s{\"name\":\"%s\",\"source_fnv64\":\"%016llx\",\"source_size_bytes_before\":%zu,\"source_size_bytes_after\":%zu,\"source_sha256_before\":\"%s\",\"source_sha256_after\":\"%s\","
          "\"different_pixels\":%zu,\"rgb_abs\":%llu,"
          "\"reference_4x_box_changed_pixels_vs_72dpi\":%zu,"
          "\"reference_4x_box_rgb_abs_delta_vs_72dpi\":%llu,"
          "\"native_different_pixels_vs_4x_box\":%zu,"
          "\"native_rgb_abs_delta_vs_4x_box\":%llu,"
          "\"different_pixel_bounds\":[%zu,%zu,%zu,%zu],\"error_rows\":[",
          i?",":"",i==0u?"no_clip":i==1u?"fullpage_w":"restrictive_w",
          (unsigned long long)hashes[i],rows[i].source_size_bytes_before,
          rows[i].source_size_bytes_after,rows[i].source_sha256_before.c_str(),
          rows[i].source_sha256_after.c_str(),ref_native_errors[i],
          (unsigned long long)rgb_abs[i],
          highres_changed_vs_72[i],
          (unsigned long long)highres_rgb_abs_vs_72[i],
          native_errors_vs_highres[i],
          (unsigned long long)native_rgb_abs_vs_highres[i],
          err_min_x[i],err_min_y[i],err_max_x[i],err_max_y[i]);
        for(size_t y=0;y<50u;++y)std::fprintf(out,"%s%zu",y?",":"",err_rows[i][y]);
        std::fprintf(out,"],\"error_cols\":[");
        for(size_t x=0;x<100u;++x)std::fprintf(out,"%s%zu",x?",":"",err_cols[i][x]);
        std::fprintf(out,"],\"mismatch_pixels\":[");
        bool first_mismatch=true;
        for(size_t p=0;p<P;++p){
            bool mismatch=false;
            for(unsigned ch=0;ch<3u;++ch)
                mismatch|=rows[i].ref_rgb[p*3u+ch]!=native_white(rows[i],p,ch);
            if(!mismatch)continue;
            if(!first_mismatch)std::fprintf(out,",");
            first_mismatch=false;
            std::fprintf(out,"{\"x\":%zu,\"y\":%zu,\"reference_rgb\":[%u,%u,%u],"
              "\"native_rgb_on_white\":[%u,%u,%u],\"native_rgba\":[%u,%u,%u,%u]}",
              p%100u,p/100u,
              (unsigned)rows[i].ref_rgb[p*3u],
              (unsigned)rows[i].ref_rgb[p*3u+1u],
              (unsigned)rows[i].ref_rgb[p*3u+2u],
              (unsigned)native_white(rows[i],p,0u),
              (unsigned)native_white(rows[i],p,1u),
              (unsigned)native_white(rows[i],p,2u),
              (unsigned)rows[i].native_rgba[p*4u],
              (unsigned)rows[i].native_rgba[p*4u+1u],
              (unsigned)rows[i].native_rgba[p*4u+2u],
              (unsigned)rows[i].native_rgba[p*4u+3u]);
        }
        std::fprintf(out,"],\"error_map\":[");
        for(size_t y=0;y<50u;++y){
            std::fprintf(out,"%s\"",y?",":"");
            for(size_t x=0;x<100u;++x){
                const size_t p=y*100u+x;
                bool mismatch=false;
                for(unsigned ch=0;ch<3u;++ch)
                    mismatch|=rows[i].ref_rgb[p*3u+ch]!=native_white(rows[i],p,ch);
                std::fputc(mismatch?'X':'.',out);
            }
            std::fputc('\"',out);
        }
        std::fprintf(out,"]}");
    }
    auto write_clip_map=[&](bool reference){
        std::fprintf(out,"[");
        for(size_t y=0;y<50u;++y){
            std::fprintf(out,"%s\"",y?",":"");
            for(size_t x=0;x<100u;++x){
                const size_t p=y*100u+x;
                bool changed=false;
                for(unsigned ch=0;ch<3u;++ch){
                    if(reference)
                        changed|=rows[0].ref_rgb[p*3u+ch]!=rows[2].ref_rgb[p*3u+ch];
                    else
                        changed|=native_white(rows[0],p,ch)!=native_white(rows[2],p,ch);
                }
                std::fputc(changed?'X':'.',out);
            }
            std::fputc('\"',out);
        }
        std::fprintf(out,"]");
    };
    std::fprintf(out,"],\"restrictive_w_reference_changed_map\":");
    write_clip_map(true);
    std::fprintf(out,",\"restrictive_w_native_changed_map\":");
    write_clip_map(false);
    std::fprintf(out,",\"restrictive_w_reference_changed_rows\":[");
    for(size_t y=0;y<50u;++y)std::fprintf(out,"%s%zu",y?",":"",ref_clip_rows[y]);
    std::fprintf(out,"],\"restrictive_w_reference_changed_cols\":[");
    for(size_t x=0;x<100u;++x)std::fprintf(out,"%s%zu",x?",":"",ref_clip_cols[x]);
    std::fprintf(out,"],\"restrictive_w_native_changed_rows\":[");
    for(size_t y=0;y<50u;++y)std::fprintf(out,"%s%zu",y?",":"",native_clip_rows[y]);
    std::fprintf(out,"],\"restrictive_w_native_changed_cols\":[");
    for(size_t x=0;x<100u;++x)std::fprintf(out,"%s%zu",x?",":"",native_clip_cols[x]);
    std::fprintf(out,"],\"per_pixel_diagnostic\":[");
    bool first_diag=true;
    for(unsigned i=0;i<3u;++i){
        const unsigned clip_state=i;
        for(size_t p=0;p<P;++p){
            const size_t x=p%100u,y=p/100u;
            const auto pred=qp379_predict_samples(x,y,clip_state);
            const unsigned alpha=rows[i].native_rgba[p*4u+3u];
            const unsigned proxy=255u-(unsigned)rows[i].ref_rgb[p*3u];
            const int coverage_residual=(int)alpha-(int)proxy;
            const int residual_r=(int)native_white(rows[i],p,0u)-(int)rows[i].ref_rgb[p*3u];
            const int residual_g=(int)native_white(rows[i],p,1u)-(int)rows[i].ref_rgb[p*3u+1u];
            const int residual_b=(int)native_white(rows[i],p,2u)-(int)rows[i].ref_rgb[p*3u+2u];
            if(!first_diag)std::fprintf(out,",");
            first_diag=false;
            std::fprintf(out,
              "{\"case\":\"%s\",\"x\":%zu,\"y\":%zu,"
              "\"sample_hits_4x4_raw\":%u,\"sample_hits_4x4_after_predicted_clip\":%u,"
              "\"clip_state\":\"%s\",\"predicted_clip_center_included\":%s,"
              "\"segment_projection\":%.9g,\"perpendicular_distance\":%.9g,"
              "\"endpoint_region\":\"%s\",\"native_rgba\":[%u,%u,%u,%u],"
              "\"native_rgb_on_white\":[%u,%u,%u],\"reference_rgb\":[%u,%u,%u],"
              "\"signed_rgb_residual\":[%d,%d,%d],"
              "\"coverage_proxy_approximate\":true,\"coverage_proxy_255_minus_reference_red\":%u,"
              "\"coverage_residual_alpha_minus_proxy\":%d,"
              "\"reference_4x_box_is_resampling_diagnostic\":true,"
              "\"reference_4x_box_rgb\":[%u,%u,%u],"
              "\"native_minus_4x_box_rgb\":[%d,%d,%d]}",
              i==0u?"no_clip":i==1u?"fullpage_w":"restrictive_w",x,y,
              pred.raw_hits,pred.clipped_hits,
              i==0u?"none":i==1u?"fullpage_w":"restrictive_w",
              pred.clip_center_included?"true":"false",
              pred.projection,pred.perpendicular_distance,pred.endpoint_region,
              (unsigned)rows[i].native_rgba[p*4u],
              (unsigned)rows[i].native_rgba[p*4u+1u],
              (unsigned)rows[i].native_rgba[p*4u+2u],alpha,
              (unsigned)native_white(rows[i],p,0u),
              (unsigned)native_white(rows[i],p,1u),
              (unsigned)native_white(rows[i],p,2u),
              (unsigned)rows[i].ref_rgb[p*3u],
              (unsigned)rows[i].ref_rgb[p*3u+1u],
              (unsigned)rows[i].ref_rgb[p*3u+2u],
              residual_r,residual_g,residual_b,proxy,coverage_residual,
              (unsigned)rows[i].ref_rgb_4x_box[p*3u],
              (unsigned)rows[i].ref_rgb_4x_box[p*3u+1u],
              (unsigned)rows[i].ref_rgb_4x_box[p*3u+2u],
              (int)native_white(rows[i],p,0u)-(int)rows[i].ref_rgb_4x_box[p*3u],
              (int)native_white(rows[i],p,1u)-(int)rows[i].ref_rgb_4x_box[p*3u+1u],
              (int)native_white(rows[i],p,2u)-(int)rows[i].ref_rgb_4x_box[p*3u+2u]);
        }
    }
    std::fprintf(out,"]}\n");
    const bool report_write_ok=std::ferror(out)==0;
    const bool report_close_ok=std::fclose(out)==0;
    if(!report_write_ok || !report_close_ok){
        (void)std::remove(staging_path.c_str());
        return EXIT_FAILURE;
    }
    /* Publish only a complete report. Remove any stale destination first so
     * rename has the same fail-closed behavior on Windows and POSIX. */
    errno=0;
    if(std::remove(report_path)!=0 && errno!=ENOENT){
        (void)std::remove(staging_path.c_str());
        return EXIT_FAILURE;
    }
    if(std::rename(staging_path.c_str(),report_path)!=0){
        (void)std::remove(staging_path.c_str());
        (void)std::remove(report_path);
        return EXIT_FAILURE;
    }
    CHECK(corpus_fixture_executions==0u);
    return EXIT_SUCCESS;
}
} /* open Stroke/Clip A/B */
