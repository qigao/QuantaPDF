/* #269 TEST ONLY: controlled original-source blue stroke/W Clip causal A/B.
 *
 * Anchor is the unchanged independently published PDFium clip_path.pdf:
 *    10 15 m 40 15 l 40 35 l 10 35 l W n
 *    0 0 1 RG 10 10 m 25 40 l 40 10 l s
 *
 * Four test-generated source PDFs share EXACTLY the same blue Stroke:
 * no W, full-page W, the original W, and a narrower still-admitted W.
 * C11's triangle Stroke intentionally rejects the first two: their joins
 * require an unimplemented general-miter model. Those are bounded, strict
 * zero-output negatives, NOT hypothetical Native pixel positives.
 * A/B comparison of actual Native pixels uses only two admitted Clips.
 * Generated controls do NOT count as published external source PDFs.
 */
namespace {
struct qp378_real_pair {
    std::vector<unsigned char> ref_rgb,native_rgba;
    qp377_external_signed_ink_v0 signed_ink{};
    uint64_t source_fnv=0u;
    bool native_supported=false;
};
struct qp378_clip_effect {
    size_t ref_changed_pixels=0u,native_changed_pixels=0u;
    size_t both_changed_pixels=0u,changed_disagreement_pixels=0u;
    size_t clipped_error_pixels=0u;
    size_t clip_error_also_other_clip=0u;
    size_t clip_error_without_other_clip=0u;
    size_t other_clip_error_suppressed_by_original=0u;
    uint64_t clip_effect_rgb_abs=0u;
};
static const char *const qp378_variant_name[5]={
    "independent_original_clipped",
    "generated_unclipped_same_stroke_UNSUPPORTED",
    "generated_fullpage_W_same_stroke_UNSUPPORTED",
    "generated_original_W_same_stroke",
    "generated_narrower_admitted_W_same_stroke"
};
static const char *const qp378_source_paint =
    "0 0 1 RG\n10 10 m\n25 40 l\n40 10 l\ns\n";
static const char *const qp378_source_clip =
    "10 15 m\n40 15 l\n40 35 l\n10 35 l\nW n\n";
static const char *const qp378_full_page_clip =
    "0 0 100 50 re W n\n";
static const char *const qp378_narrow_clip =
    "10 16 m\n40 16 l\n40 34 l\n10 34 l\nW n\n";
static void qp378_write_control_pdf(const char *path,unsigned variant)
{
    CHECK(path && *path && variant>=1u && variant<=4u);
    std::string commands;
    if(variant==2u)commands=qp378_full_page_clip;
    else if(variant==3u)commands=qp378_source_clip;
    else if(variant==4u)commands=qp378_narrow_clip;
    commands+=qp378_source_paint;
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
static qp378_real_pair qp378_capture(
    const char *source,bool expect_clip,bool expect_native)
{
    CHECK(source && *source && corpus_fixture_executions==0u);
    constexpr size_t W=100u,H=50u;
    const auto original=decimal360_read_bytes(source);
    CHECK(original.size()>0u && original.size()<=8192u);
    qp378_real_pair out{};
    out.source_fnv=decimal360_fnv64(original);

    /* Two ORIGINAL PDFium references, rendered independently of Native. */
    const rgb_image reference=render_public_reference(0u,source);
    const rgb_image again=render_public_reference(0u,source);
    CHECK(reference.width==(int)W && reference.height==(int)H &&
          reference.components==3 && reference.stride>=3*(int)W &&
          reference.pixels.size()>=(size_t)reference.stride*H &&
          reference.width==again.width && reference.height==again.height &&
          reference.stride==again.stride &&
          reference.pixels==again.pixels);
    out.ref_rgb.resize(W*H*3u);
    for(size_t y=0u;y<H;++y)
        std::memcpy(out.ref_rgb.data()+y*W*3u,
                    reference.at(0u,y),W*3u);

    /* Real Source->Graph->owned IR, source/Graph closed before native. */
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
          detached.resources.clip_count==(expect_clip?1u:0u) &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.soft_mask_count==0u &&
          detached.resources.form_count==0u &&
          detached.resources.paint_count==0u);
    const quantapdf_ir_raster_v1_limits bounds={W*H,1u,128u,5000000u};
    quantapdf_ir_raster_image native{},repeat{};
    const quantapdf_status first=quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&bounds,&native);
    const quantapdf_status second=quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&bounds,&repeat);
    if(expect_native){
        CHECK(first==QUANTAPDF_OK && second==QUANTAPDF_OK &&
              native.pixels && repeat.pixels && native.width==W &&
              native.height==H && native.stride==W*4u &&
              native.size==W*H*4u && repeat.size==native.size &&
              std::memcmp(native.pixels,repeat.pixels,native.size)==0);
        out.native_rgba.assign(native.pixels,native.pixels+native.size);
        out.native_supported=true;
    }else{
        /* Contract, not a missing sample to replace with a fake bitmap:
         * general closed-triangle miter joins are outside C11 Raster's
         * admitted subset, even when an all-page Clip is nonrestrictive. */
        CHECK(first==QUANTAPDF_ERROR_UNSUPPORTED &&
              second==QUANTAPDF_ERROR_UNSUPPORTED &&
              native.pixels==nullptr && native.size==0u &&
              native.width==0u && native.height==0u &&
              repeat.pixels==nullptr && repeat.size==0u &&
              repeat.width==0u && repeat.height==0u);
    }
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_dispose(&detached);
    CHECK(decimal360_read_bytes(source)==original);

    if(out.native_supported){
        for(size_t p=0u;p<W*H;++p)
            out.signed_ink.observe(&out.native_rgba[p*4u],
                                   &out.ref_rgb[p*3u],true);
        out.signed_ink.assert_conserved(W*H,
            (double)out.signed_ink.total_rgb_abs,out.signed_ink.gross);
    }
    CHECK(corpus_fixture_executions==0u);
    return out;
}
static qp378_clip_effect qp378_compare_clip_transfer(
    const qp378_real_pair& original,const qp378_real_pair& narrower)
{
    constexpr size_t P=5000u;
    CHECK(original.native_supported && narrower.native_supported &&
          original.ref_rgb.size()==P*3u && narrower.ref_rgb.size()==P*3u &&
          original.native_rgba.size()==P*4u &&
          narrower.native_rgba.size()==P*4u);
    qp378_clip_effect result{};
    for(size_t i=0u;i<P;++i){
        bool ref_changed=false,native_changed=false;
        bool original_error=false,narrow_error=false;
        uint64_t effect_delta=0u;
        for(unsigned c=0u;c<3u;++c){
            const int ref_old=original.ref_rgb[i*3u+c];
            const int ref_narrow=narrower.ref_rgb[i*3u+c];
            const int native_old=(int)on_white(
                &original.native_rgba[i*4u],c);
            const int native_narrow=(int)on_white(
                &narrower.native_rgba[i*4u],c);
            ref_changed|=ref_old!=ref_narrow;
            native_changed|=native_old!=native_narrow;
            original_error|=ref_old!=native_old;
            narrow_error|=ref_narrow!=native_narrow;
            effect_delta+=(uint64_t)std::abs(
                (ref_old-ref_narrow)-(native_old-native_narrow));
        }
        result.ref_changed_pixels+=(size_t)ref_changed;
        result.native_changed_pixels+=(size_t)native_changed;
        result.both_changed_pixels+=(size_t)(ref_changed&&native_changed);
        result.changed_disagreement_pixels+=(size_t)(
            ref_changed!=native_changed);
        result.clip_effect_rgb_abs+=effect_delta;
        result.clipped_error_pixels+=(size_t)original_error;
        result.clip_error_also_other_clip+=(size_t)(
            original_error&&narrow_error);
        result.clip_error_without_other_clip+=(size_t)(
            original_error&&!narrow_error);
        result.other_clip_error_suppressed_by_original+=(size_t)(
            narrow_error&&!original_error);
    }
    CHECK(result.clipped_error_pixels==original.signed_ink.different &&
          result.clip_error_also_other_clip+
          result.clip_error_without_other_clip==
              result.clipped_error_pixels &&
          result.ref_changed_pixels>0u &&
          result.ref_changed_pixels<=P &&
          result.native_changed_pixels<=P &&
          result.both_changed_pixels<=
              std::min(result.ref_changed_pixels,
                       result.native_changed_pixels));
    return result;
}
static void qp378_print_effect(FILE *out,const qp378_clip_effect& e)
{
    std::fprintf(out,
      "{\"reference_changed_pixels\":%zu,"
      "\"native_changed_pixels\":%zu,"
      "\"both_changed_pixels\":%zu,"
      "\"membership_change_disagreement_pixels\":%zu,"
      "\"original_clip_error_pixels\":%zu,"
      "\"original_error_also_narrow_clip\":%zu,"
      "\"original_error_without_narrow_clip\":%zu,"
      "\"narrow_clip_error_absent_from_original\":%zu,"
      "\"clip_difference_of_differences_rgb_abs\":%llu}",
      e.ref_changed_pixels,e.native_changed_pixels,e.both_changed_pixels,
      e.changed_disagreement_pixels,e.clipped_error_pixels,
      e.clip_error_also_other_clip,e.clip_error_without_other_clip,
      e.other_clip_error_suppressed_by_original,
      (unsigned long long)e.clip_effect_rgb_abs);
}
static void qp378_print_pixel_mismatches(
    FILE *out,const qp378_real_pair& sample,unsigned variant)
{
    constexpr size_t W=100u,H=50u;
    CHECK(out && sample.native_supported &&
          sample.ref_rgb.size()==W*H*3u &&
          sample.native_rgba.size()==W*H*4u);
    bool first=true;
    for(size_t y=0u;y<H;++y){
        for(size_t x=0u;x<W;++x){
            const size_t p=y*W+x;
            int ref[3],native[3],delta[3];
            bool mismatch=false;
            for(unsigned c=0u;c<3u;++c){
                ref[c]=(int)sample.ref_rgb[p*3u+c];
                native[c]=(int)on_white(&sample.native_rgba[p*4u],c);
                delta[c]=ref[c]-native[c];
                mismatch|=delta[c]!=0;
            }
            if(!mismatch)continue;
            std::fprintf(out,
              "%s{\"variant\":%u,\"x\":%zu,\"y\":%zu,"
              "\"reference_rgb\":[%d,%d,%d],"
              "\"native_rgb_on_white\":[%d,%d,%d],"
              "\"reference_minus_native_rgb\":[%d,%d,%d]}",
              first?"":",",variant,x,y,
              ref[0],ref[1],ref[2],native[0],native[1],native[2],
              delta[0],delta[1],delta[2]);
            first=false;
        }
    }
}
static int test_269_real_stroke_clip_causal_ab(
    const char *published,const char *report_path)
{
    CHECK(published && *published && report_path && *report_path &&
          std::strcmp(published,report_path)!=0 &&
          corpus_fixture_executions==0u);
    const char *generated=GENERATED_269_WCLIP_STROKE_AB_PDF;
    CHECK(generated && *generated &&
          std::strcmp(generated,published)!=0 &&
          std::strcmp(generated,report_path)!=0);
    const std::string staged=std::string(report_path)+".tmp";
    CHECK(staged!=generated && staged!=published);
    const auto immutable_original=decimal360_read_bytes(published);
    CHECK(immutable_original.size()==514u);
    (void)std::remove(staged.c_str());
    (void)std::remove(report_path);

    qp378_real_pair observed[5];
    observed[0]=qp378_capture(published,true,true);
    CHECK(observed[0].signed_ink.compared_pixels==5000u &&
          observed[0].signed_ink.different==100u &&
          observed[0].signed_ink.under==100u &&
          observed[0].signed_ink.over==0u &&
          observed[0].signed_ink.color_only==0u &&
          observed[0].signed_ink.total_rgb_abs==2480u &&
          observed[0].signed_ink.gross==0u);
    /* Deterministic IDs and disabled object streams should make each
     * generated control byte-reproducible on its own platform. Verify by
     * regenerating each PDF and comparing the complete bytes; do not bake
     * platform-specific serializer hashes into the test. */
    uint64_t generated_hashes[4]={};
    for(unsigned i=1u;i<5u;++i){
        qp378_write_control_pdf(generated,i);
        const bool expect_native=i==3u || i==4u;
        observed[i]=qp378_capture(generated,i!=1u,expect_native);
        CHECK(observed[i].native_supported==expect_native);
        const auto first_generated_bytes=decimal360_read_bytes(generated);
        CHECK(!first_generated_bytes.empty() &&
              observed[i].source_fnv!=0u);
        qp378_write_control_pdf(generated,i);
        CHECK(decimal360_read_bytes(generated)==first_generated_bytes);
        CHECK(std::remove(generated)==0);
        for(unsigned prev=1u;prev<i;++prev)
            CHECK(observed[i].source_fnv!=generated_hashes[prev-1u]);
        CHECK(observed[i].source_fnv!=observed[0].source_fnv);
        generated_hashes[i-1u]=observed[i].source_fnv;
    }
    CHECK(decimal360_read_bytes(published)==immutable_original);

    /* The no-Clip and full-page W originals have equal PDFium RGB pixels.
     * Both must be strictly unsupported by real Native due to missing
     * modeled arbitrary miter joins; never fabricate a Native bitmap.
     * The generated exact original W control equals published original
     * in both PDFium RGB and real Native RGBA. */
    const size_t reference_bytes=5000u*3u,native_bytes=5000u*4u;
    CHECK(observed[1].ref_rgb.size()==reference_bytes &&
          observed[2].ref_rgb.size()==reference_bytes &&
          observed[0].ref_rgb.size()==reference_bytes &&
          observed[3].ref_rgb.size()==reference_bytes &&
          observed[4].ref_rgb.size()==reference_bytes &&
          observed[1].native_rgba.empty() &&
          observed[2].native_rgba.empty() &&
          !observed[1].native_supported &&
          !observed[2].native_supported &&
          observed[0].native_supported &&
          observed[3].native_supported &&
          observed[4].native_supported &&
          observed[0].native_rgba.size()==native_bytes &&
          observed[3].native_rgba.size()==native_bytes &&
          observed[4].native_rgba.size()==native_bytes &&
          std::memcmp(observed[1].ref_rgb.data(),
                      observed[2].ref_rgb.data(),reference_bytes)==0 &&
          std::memcmp(observed[0].ref_rgb.data(),
                      observed[3].ref_rgb.data(),reference_bytes)==0 &&
          std::memcmp(observed[0].native_rgba.data(),
                      observed[3].native_rgba.data(),native_bytes)==0);

    /* Only compare TWO admitted rectangular Clips with identical Stroke.
     * This is a real Native/PDFium difference-of-differences, not a
     * hypothetical unsupported no-Clip Native rendering. */
    const qp378_clip_effect diff=qp378_compare_clip_transfer(
        observed[0],observed[4]);
    FILE *report=std::fopen(staged.c_str(),"wb");CHECK(report);
    std::fprintf(report,
      "{\"schema_version\":2,"
      "\"source_kind\":\"one_unchanged_external_plus_four_test_generated\","
      "\"published_document_id\":\"pdfium.clip-path-v1\","
      "\"published_original_provenance_kept\":true,"
      "\"actual_source_to_owned_ir_to_c11\":true,"
      "\"pdfium_fallback_used\":false,"
      "\"native_raster_mutated\":false,"
      "\"public_sdk_backend_switched\":false,"
      "\"independent_external_positive_increment\":0,"
      "\"published_source_bytes\":514,"
      "\"fullpage_and_no_clip_reference_equal\":true,"
      "\"fullpage_and_no_clip_native_fails_closed\":true,"
      "\"published_and_generated_same_clip_reference_equal\":true,"
      "\"published_and_generated_same_clip_native_equal\":true,"
      "\"admitted_narrower_W_uses_same_Stroke\":true,"
      "\"causal_comparison_uses_only_admitted_native_clips\":true,"
      "\"variants\":[");
    for(unsigned i=0u;i<5u;++i){
        const std::string signed_json=observed[i].native_supported?
            qp377_signed_ink_json_v0(observed[i].signed_ink,"blue"):
            std::string("null");
        std::fprintf(report,
          "%s{\"index\":%u,\"name\":\"%s\","
          "\"source_kind\":\"%s\","
          "\"source_fnv64\":\"%016llx\","
          "\"source_pages\":1,"
          "\"source_clip_count\":%u,"
          "\"original_reference_repeat_identical\":true,"
          "\"native_outcome\":\"%s\","
          "\"native_rgba_repeat_identical\":%s,"
          "\"native_rejection_zero_output\":%s,"
          "\"signed_ink_proxy\":%s}",
          i?",":"",i,qp378_variant_name[i],
          i==0u?"independently_published_original":"test_generated_control",
          (unsigned long long)observed[i].source_fnv,
          i==1u?0u:1u,
          observed[i].native_supported?"supported":"unsupported_no_partial_output",
          observed[i].native_supported?"true":"false",
          observed[i].native_supported?"false":"true",
          signed_json.c_str());
    }
    std::fprintf(report,"],\"admitted_clip_transfer\":");
    qp378_print_effect(report,diff);
    std::fprintf(report,",\"pixel_mismatches\":[");
    qp378_print_pixel_mismatches(report,observed[0],0u);
    std::fprintf(report,",");
    qp378_print_pixel_mismatches(report,observed[4],4u);
    std::fprintf(report,"]}\n");
    CHECK(std::ferror(report)==0 && std::fclose(report)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0)CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(staged.c_str(),report_path)==0);
    CHECK(decimal360_read_bytes(published)==immutable_original);
    CHECK(corpus_fixture_executions==0u);
    std::fprintf(stderr,
      "[q269-stroke-clip-ab] passed: noClip/fullpage C11 UNSUPPORTED,"
      " original/narrow clipped Native supported,"
      " original diff=%zu, narrow diff=%zu,"
      " original-only-error=%zu, shared-error=%zu, clip-effectRGB=%llu,"
      " no external increment or Raster cutover\n",
      observed[0].signed_ink.different,
      observed[4].signed_ink.different,
      diff.clip_error_without_other_clip,
      diff.clip_error_also_other_clip,
      (unsigned long long)diff.clip_effect_rgb_abs);
    return EXIT_SUCCESS;
}
} /* private original-PDF W Clip stroke A/B */
