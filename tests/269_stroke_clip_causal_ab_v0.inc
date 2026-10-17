/* #269 TEST ONLY: controlled original-source blue stroke/W Clip causal A/B.
 *
 * Anchor is the unchanged independently published PDFium clip_path.pdf:
 *    10 15 m 40 15 l 40 35 l 10 35 l W n
 *    0 0 1 RG 10 10 m 25 40 l 40 10 l s
 *
 * Three separately generated source PDFs retain the EXACT same paint tokens:
 * stroke_only, full_page_W, and identical_original_W. Neither generation
 * nor comparison reads or alters PDFium's reference RGB in deciding pixels.
 * This test measures real Source->Graph->owned IR->C11 Native RGBA.
 * Generated controls do NOT count as independently published source PDFs.
 */
namespace {
struct qp378_real_pair {
    std::vector<unsigned char> ref_rgb,native_rgba;
    qp377_external_signed_ink_v0 signed_ink{};
    uint64_t source_fnv=0u;
};
struct qp378_clip_effect {
    size_t ref_changed_pixels=0u,native_changed_pixels=0u;
    size_t both_changed_pixels=0u,changed_disagreement_pixels=0u;
    size_t clipped_error_pixels=0u;
    size_t clip_error_also_unclipped=0u;
    size_t clip_error_without_unclipped=0u;
    size_t unclipped_error_suppressed_by_clip=0u;
    uint64_t clip_effect_rgb_abs=0u;
};
static const char *const qp378_variant_name[4]={
    "independent_original_clipped",
    "generated_unclipped_same_stroke",
    "generated_fullpage_W_same_stroke",
    "generated_original_W_same_stroke"
};
static const char *const qp378_source_paint =
    "0 0 1 RG\n10 10 m\n25 40 l\n40 10 l\ns\n";
static const char *const qp378_source_clip =
    "10 15 m\n40 15 l\n40 35 l\n10 35 l\nW n\n";
static const char *const qp378_full_page_clip =
    "0 0 100 50 re W n\n";
static void qp378_write_control_pdf(const char *path,unsigned variant)
{
    CHECK(path && *path && variant>=1u && variant<=3u);
    std::string commands;
    if(variant==2u)commands=qp378_full_page_clip;
    else if(variant==3u)commands=qp378_source_clip;
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
    writer.setOutputMemory();
    writer.write();
    auto data=writer.getBufferSharedPointer();
    CHECK(data && data->getSize()>0u && data->getSize()<=8192u);
    FILE *out=std::fopen(path,"wb");CHECK(out);
    CHECK(std::fwrite(data->getBuffer(),1,data->getSize(),out)==data->getSize());
    CHECK(std::fclose(out)==0);
}
static qp378_real_pair qp378_capture(const char *source,bool expect_clip)
{
    CHECK(source && *source && corpus_fixture_executions==0u);
    constexpr size_t W=100u,H=50u;
    const auto original=decimal360_read_bytes(source);
    CHECK(original.size()>0u && original.size()<=8192u);
    qp378_real_pair out{};
    out.source_fnv=decimal360_fnv64(original);

    /* ORIGINAL RGB is rendered independently, without Native result input. */
    const rgb_image reference=render_original_pdfium(0u,source);
    CHECK(reference.width==(int)W && reference.height==(int)H &&
          reference.components==3 && reference.stride>=3*(int)W &&
          reference.pixels.size()>=(size_t)reference.stride*H);
    out.ref_rgb.resize(W*H*3u);
    for(size_t y=0u;y<H;++y)
        std::memcpy(out.ref_rgb.data()+y*W*3u,
                    reference.at(0u,y),W*3u);

    /* No source/Graph owner survives into Native. No PDFium fallback. */
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
    const quantapdf_ir_raster_v1_limits bounds={
        W*H,1u,128u,5000000u};
    quantapdf_ir_raster_image native{},repeat{};
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&bounds,&native));
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&bounds,&repeat));
    CHECK(native.pixels && repeat.pixels && native.width==W &&
          native.height==H && native.size==W*H*4u &&
          native.stride==W*4u && repeat.size==native.size &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    out.native_rgba.assign(native.pixels,native.pixels+native.size);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_dispose(&detached);
    CHECK(decimal360_read_bytes(source)==original);

    for(size_t p=0u;p<W*H;++p)
        out.signed_ink.observe(&out.native_rgba[p*4u],
                               &out.ref_rgb[p*3u],true);
    out.signed_ink.assert_conserved(W*H,
        (double)out.signed_ink.total_rgb_abs,out.signed_ink.gross);
    CHECK(corpus_fixture_executions==0u);
    return out;
}
static unsigned qp378_delta(unsigned x,unsigned y)
{
    return x>y?x-y:y-x;
}
static qp378_clip_effect qp378_compare_clip_transfer(
    const qp378_real_pair& clip,const qp378_real_pair& unclip)
{
    constexpr size_t P=5000u;
    CHECK(clip.ref_rgb.size()==P*3u && unclip.ref_rgb.size()==P*3u &&
          clip.native_rgba.size()==P*4u && unclip.native_rgba.size()==P*4u);
    qp378_clip_effect result{};
    for(size_t i=0u;i<P;++i){
        bool ref_changed=false,native_changed=false;
        bool clip_error=false,no_clip_error=false;
        uint64_t effect_delta=0u;
        for(unsigned c=0u;c<3u;++c){
            const int ref_clip=clip.ref_rgb[i*3u+c];
            const int ref_full=unclip.ref_rgb[i*3u+c];
            const int native_clip=(int)on_white(
                &clip.native_rgba[i*4u],c);
            const int native_full=(int)on_white(
                &unclip.native_rgba[i*4u],c);
            ref_changed|=ref_clip!=ref_full;
            native_changed|=native_clip!=native_full;
            clip_error|=ref_clip!=native_clip;
            no_clip_error|=ref_full!=native_full;
            effect_delta+=(uint64_t)std::abs(
                (ref_clip-ref_full)-(native_clip-native_full));
        }
        result.ref_changed_pixels+=(size_t)ref_changed;
        result.native_changed_pixels+=(size_t)native_changed;
        result.both_changed_pixels+=(size_t)(ref_changed&&native_changed);
        result.changed_disagreement_pixels+=(size_t)(
            ref_changed!=native_changed);
        result.clip_effect_rgb_abs+=effect_delta;
        result.clipped_error_pixels+=(size_t)clip_error;
        result.clip_error_also_unclipped+=(size_t)(
            clip_error&&no_clip_error);
        result.clip_error_without_unclipped+=(size_t)(
            clip_error&&!no_clip_error);
        result.unclipped_error_suppressed_by_clip+=(size_t)(
            no_clip_error&&!clip_error);
    }
    CHECK(result.clipped_error_pixels==
          clip.signed_ink.different &&
          result.clip_error_also_unclipped+
          result.clip_error_without_unclipped==
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
      "\"clipped_error_pixels\":%zu,"
      "\"clipped_error_also_unclipped\":%zu,"
      "\"clipped_error_without_unclipped\":%zu,"
      "\"unclipped_error_suppressed_by_clip\":%zu,"
      "\"clip_difference_of_differences_rgb_abs\":%llu}",
      e.ref_changed_pixels,e.native_changed_pixels,e.both_changed_pixels,
      e.changed_disagreement_pixels,e.clipped_error_pixels,
      e.clip_error_also_unclipped,e.clip_error_without_unclipped,
      e.unclipped_error_suppressed_by_clip,
      (unsigned long long)e.clip_effect_rgb_abs);
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

    qp378_real_pair observed[4];
    observed[0]=qp378_capture(published,true);
    CHECK(observed[0].signed_ink.compared_pixels==5000u &&
          observed[0].signed_ink.different==100u &&
          observed[0].signed_ink.under==100u &&
          observed[0].signed_ink.over==0u &&
          observed[0].signed_ink.color_only==0u &&
          observed[0].signed_ink.total_rgb_abs==2480u &&
          observed[0].signed_ink.gross==0u);
    uint64_t generated_hashes[3]={};
    for(unsigned i=1u;i<4u;++i){
        qp378_write_control_pdf(generated,i);
        observed[i]=qp378_capture(generated,i!=1u);
        for(unsigned prev=1u;prev<i;++prev)
            CHECK(observed[i].source_fnv!=generated_hashes[prev-1u]);
        CHECK(observed[i].source_fnv!=observed[0].source_fnv);
        generated_hashes[i-1u]=observed[i].source_fnv;
        CHECK(std::remove(generated)==0);
    }
    CHECK(decimal360_read_bytes(published)==immutable_original);

    /* Same stroke, full-page W has NO geometry restriction: either a
     * Native or PDFium change would invalidate the controlled baseline.
     * The generated original-W is a byte-distinct source whose *pixel*
     * semantics must match unchanged independently published original.
     * Neither generated variant is promoted to independent corpus counts. */
    const size_t reference_bytes=5000u*3u,native_bytes=5000u*4u;
    const size_t no_clip_ref_different=std::memcmp(
        observed[1].ref_rgb.data(),observed[2].ref_rgb.data(),
        reference_bytes)!=0;
    const size_t no_clip_native_different=std::memcmp(
        observed[1].native_rgba.data(),observed[2].native_rgba.data(),
        native_bytes)!=0;
    const size_t anchored_ref_different=std::memcmp(
        observed[0].ref_rgb.data(),observed[3].ref_rgb.data(),
        reference_bytes)!=0;
    const size_t anchored_native_different=std::memcmp(
        observed[0].native_rgba.data(),observed[3].native_rgba.data(),
        native_bytes)!=0;
    CHECK(no_clip_ref_different==0u && no_clip_native_different==0u &&
          anchored_ref_different==0u && anchored_native_different==0u);

    const qp378_clip_effect diff=qp378_compare_clip_transfer(
        observed[0],observed[1]);
    FILE *report=std::fopen(staged.c_str(),"wb");CHECK(report);
    std::fprintf(report,
      "{\"schema_version\":1,"
      "\"source_kind\":\"one_unchanged_external_plus_three_test_generated\","
      "\"published_document_id\":\"pdfium.clip-path-v1\","
      "\"published_original_provenance_kept\":true,"
      "\"actual_source_to_owned_ir_to_c11\":true,"
      "\"pdfium_fallback_used\":false,"
      "\"native_raster_mutated\":false,"
      "\"public_sdk_backend_switched\":false,"
      "\"independent_external_positive_increment\":0,"
      "\"published_source_bytes\":514,"
      "\"fullpage_and_no_clip_reference_equal\":true,"
      "\"fullpage_and_no_clip_native_equal\":true,"
      "\"published_and_generated_same_clip_reference_equal\":true,"
      "\"published_and_generated_same_clip_native_equal\":true,"
      "\"variants\":[");
    for(unsigned i=0u;i<4u;++i){
        std::fprintf(report,
          "%s{\"index\":%u,\"name\":\"%s\","
          "\"source_kind\":\"%s\","
          "\"source_fnv64\":\"%016llx\","
          "\"source_pages\":1,"
          "\"source_clip_count\":%u,"
          "\"original_reference_repeat_identical\":true,"
          "\"native_rgba_repeat_identical\":true,"
          "\"signed_ink_proxy\":%s}",
          i?",":"",i,qp378_variant_name[i],
          i==0u?"independently_published_original":"test_generated_control",
          (unsigned long long)observed[i].source_fnv,
          i==1u?0u:1u,
          qp377_signed_ink_json_v0(
              observed[i].signed_ink,"blue").c_str());
    }
    std::fprintf(report,"],\"clip_transfer\":");
    qp378_print_effect(report,diff);
    std::fprintf(report,"}\n");
    CHECK(std::ferror(report)==0 && std::fclose(report)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0)CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(staged.c_str(),report_path)==0);
    CHECK(decimal360_read_bytes(published)==immutable_original);
    CHECK(corpus_fixture_executions==0u);
    std::fprintf(stderr,
      "[q269-stroke-clip-ab] passed reference/native equivalences,"
      " original clip errors=%zu, unclipped=%zu,"
      " clipped-only-error=%zu, overlap=%zu, effectRGB=%llu,"
      " no source admission or Raster cutover\n",
      observed[0].signed_ink.different,
      observed[1].signed_ink.different,
      diff.clip_error_without_unclipped,
      diff.clip_error_also_unclipped,
      (unsigned long long)diff.clip_effect_rgb_abs);
    return EXIT_SUCCESS;
}
} /* private original-PDF W Clip stroke A/B */
