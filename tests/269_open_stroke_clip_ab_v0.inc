/* #269 TEST ONLY: single open-segment Stroke, three real Native/PDFium controls.
 * Unlike the closed triangle in #378, this has no joins or miter geometry.
 * No Clip, full-page W, and restrictive W are all expected to be admitted.
 */
namespace {
struct qp379_line_capture {
    std::vector<unsigned char> ref_rgb;
    std::vector<unsigned char> native_rgba;
    uint64_t source_fnv=0u;
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
    writer.setOutputMemory();
    writer.write();
    auto data=writer.getBufferSharedPointer();
    CHECK(data && data->getSize()>0u && data->getSize()<=8192u);
    FILE *out=std::fopen(path,"wb");CHECK(out);
    CHECK(std::fwrite(data->getBuffer(),1,data->getSize(),out)==data->getSize());
    CHECK(std::fclose(out)==0);
}

static qp379_line_capture qp379_capture_line(const char *source,
                                              unsigned expected_clips)
{
    constexpr size_t W=100u,H=50u,P=W*H;
    const auto original=decimal360_read_bytes(source);
    CHECK(!original.empty() && original.size()<=8192u);
    qp379_line_capture out{};
    out.source_fnv=decimal360_fnv64(original);
    const rgb_image ref=render_original_pdfium(0u,source);
    const rgb_image ref_again=render_original_pdfium(0u,source);
    CHECK(ref.width==(int)W && ref.height==(int)H && ref.components==3 &&
          ref.stride>=3*(int)W && ref.pixels.size()>=(size_t)ref.stride*H &&
          ref.width==ref_again.width && ref.height==ref_again.height &&
          ref.stride==ref_again.stride && ref.pixels==ref_again.pixels);
    out.ref_rgb.resize(P*3u);
    for(size_t y=0;y<H;++y)
        std::memcpy(out.ref_rgb.data()+y*W*3u,ref.at(0u,y),W*3u);

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
    CHECK(decimal360_read_bytes(source)==original);
    return out;
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
              rows[i].native_rgba.size()==rgba_n);
    auto native_white=[](const qp379_line_capture& r,size_t p,unsigned c) {
        return (unsigned char)on_white(&r.native_rgba[p*4u],c);
    };
    CHECK(std::memcmp(rows[0].ref_rgb.data(),rows[1].ref_rgb.data(),rgb_n)==0);
    for(size_t p=0;p<P;++p)for(unsigned c=0;c<3u;++c)
        CHECK(native_white(rows[0],p,c)==native_white(rows[1],p,c));

    size_t ref_restrict_changed=0,native_restrict_changed=0;
    size_t ref_native_errors[3]{},rgb_abs[3]{};
    for(unsigned i=0;i<3u;++i){
        for(size_t p=0;p<P;++p){
            bool pixel_error=false;
            for(unsigned c=0;c<3u;++c){
                const int a=rows[i].ref_rgb[p*3u+c];
                const int b=(int)native_white(rows[i],p,c);
                pixel_error|=a!=b;
                rgb_abs[i]+=(uint64_t)std::abs(a-b);
            }
            ref_native_errors[i]+=pixel_error?1u:0u;
        }
    }
    for(size_t p=0;p<P;++p){
        bool r=false,n=false;
        for(unsigned c=0;c<3u;++c){
            r|=rows[0].ref_rgb[p*3u+c]!=rows[2].ref_rgb[p*3u+c];
            n|=native_white(rows[0],p,c)!=native_white(rows[2],p,c);
        }
        ref_restrict_changed+=r?1u:0u;
        native_restrict_changed+=n?1u:0u;
    }
    CHECK(ref_restrict_changed>0u && native_restrict_changed>0u);
    FILE *out=std::fopen(report_path,"wb");CHECK(out);
    std::fprintf(out,
      "{\"schema_version\":1,\"open_line_only\":true,"
      "\"no_clip_and_fullpage_w_reference_equal\":true,"
      "\"no_clip_and_fullpage_w_native_equal\":true,"
      "\"all_three_native_supported\":true,"
      "\"restrictive_w_changes_reference\":%zu,"
      "\"restrictive_w_changes_native\":%zu,"
      "\"cases\":[",ref_restrict_changed,native_restrict_changed);
    for(unsigned i=0;i<3u;++i)
        std::fprintf(out,"%s{\"name\":\"%s\",\"source_fnv64\":\"%016llx\","
          "\"different_pixels\":%zu,\"rgb_abs\":%llu}",
          i?",":"",i==0u?"no_clip":i==1u?"fullpage_w":"restrictive_w",
          (unsigned long long)hashes[i],ref_native_errors[i],
          (unsigned long long)rgb_abs[i]);
    std::fprintf(out,"]}\n");
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
    CHECK(corpus_fixture_executions==0u);
    return EXIT_SUCCESS;
}
} /* open Stroke/Clip A/B */
