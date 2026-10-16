/* #269: Held-out, test-owned ORIGINAL PDF edge-coverage A/B.
 * Two previously UNUSED source Fill/W shapes, four inherited/leaf CropBoxes,
 * four PDF rotations and 72/96/144 DPI. QPDF makes an original PDF byte
 * stream, never a Composer-IR re-export. PDFium is a reference, not fallback.
 *
 * This is an OBSERVATION. Thresholded-Q4 and center decisions below do not
 * write native pixels, call a provider, or change the scoped C11 renderer.
 */
namespace {
struct qp269_heldout_rect_v0 { double x0,y0,x1,y1; };
struct qp269_heldout_shape_v0 {
    const char *source_commands;
    qp269_heldout_rect_v0 clip,fill;
};
static const qp269_heldout_shape_v0 qp269_holdout_shapes_v0[2]={
    {"2.875 4.125 7.25 5.25 re W n "
     "1 0 0 rg 4.375 3.625 5.5 6 re f",
     {2.875,4.125,10.125,9.375},{4.375,3.625,9.875,9.625}},
    {"3.125 3.875 6.75 6.25 re W n "
     "1 0 0 rg 3.875 4.625 6.5 4.5 re f",
     {3.125,3.875,9.875,10.125},{3.875,4.625,10.375,9.125}}
};
#include "269_heldout_q4_float_handoff_v0.inc"
#include "269_q4_rect_clip_quantization_falsifiers_v0.inc"
static const char *const qp269_holdout_method_v0[7]={
    "native_q4_white_composite",
    "q4_alpha_threshold_1_of_16",
    "q4_alpha_threshold_4_of_16",
    "q4_alpha_threshold_8_of_16",
    "q4_alpha_threshold_12_of_16",
    "q4_alpha_threshold_16_of_16",
    "independent_pixel_center"
};
struct qp269_score_v0 {
    uint64_t rgb_abs=0u;
    size_t different=0u,gross=0u;
};
/* Signed model-vs-original coverage: original PDFium reference bitmap
 * stays immutable and is checked independently per rendered source. */
struct qp269_heldout_signed_v0 {
    size_t under=0u,over=0u,color_only=0u;
    uint64_t under_rgb_abs=0u,over_rgb_abs=0u,color_only_rgb_abs=0u;
};
static void qp269_heldout_signed_add_v0(qp269_heldout_signed_v0 *dst,
    const qp269_heldout_signed_v0& src)
{
    dst->under+=src.under;
    dst->over+=src.over;
    dst->color_only+=src.color_only;
    dst->under_rgb_abs+=src.under_rgb_abs;
    dst->over_rgb_abs+=src.over_rgb_abs;
    dst->color_only_rgb_abs+=src.color_only_rgb_abs;
}
static void qp269_heldout_signed_print_v0(FILE *out,
    const qp269_heldout_signed_v0 signed_scores[7])
{
    std::fprintf(out,"[");
    for(unsigned m=0u;m<7u;++m){
        const auto& d=signed_scores[m];
        std::fprintf(out,
            "{\"method\":\"%s\",\"under\":%zu,\"over\":%zu,"
            "\"color_only\":%zu,\"under_rgb_abs\":%llu,"
            "\"over_rgb_abs\":%llu,\"color_only_rgb_abs\":%llu}%s",
            qp269_holdout_method_v0[m],
            d.under,d.over,d.color_only,
            (unsigned long long)d.under_rgb_abs,
            (unsigned long long)d.over_rgb_abs,
            (unsigned long long)d.color_only_rgb_abs,
            m==6u?"":",");
    }
    std::fprintf(out,"]");
}
static void qp269_score_add_v0(qp269_score_v0 *dst,
    const qp269_score_v0& src)
{
    dst->rgb_abs+=src.rgb_abs;
    dst->different+=src.different;
    dst->gross+=src.gross;
}
static unsigned qp269_cutoff_v0(unsigned method)
{
    const unsigned hits[5]={1u,4u,8u,12u,16u};
    CHECK(method>=1u && method<=5u);
    return (255u*hits[method-1u]+8u)/16u;
}
static bool qp269_in_rect_v0(double x,double y,
    const qp269_heldout_rect_v0& r)
{
    return x>=r.x0 && x<r.x1 && y>=r.y0 && y<r.y1;
}
static bool qp269_source_center_v0(
    const quantapdf_graph_device_grid& grid,
    size_t px,size_t py,double *out_x,double *out_y)
{
    CHECK(out_x && out_y && grid.width>0u && grid.height>0u &&
          px<(size_t)grid.width && py<(size_t)grid.height);
    const double dx=(double)px+0.5,dy=(double)py+0.5;
    const double *t=grid.from_source;
    double x=0.0,y=0.0;
    if(t[0]!=0.0 && t[3]!=0.0 && t[1]==0.0 && t[2]==0.0){
        x=(dx-t[4])/t[0]; y=(dy-t[5])/t[3];
    }else if(t[0]==0.0 && t[3]==0.0 &&
             t[1]!=0.0 && t[2]!=0.0){
        x=(dy-t[5])/t[1]; y=(dx-t[4])/t[2];
    }else return false;
    if(!std::isfinite(x)||!std::isfinite(y))return false;
    *out_x=x;*out_y=y;
    return true;
}
static bool qp269_center_boundary_tie_v0(double x,double y,
    const qp269_heldout_rect_v0& r)
{
    /* This is only a reportable tie detector, NEVER a pixel epsilon snap.
     * If the source center coincides with the edge, the model's documented
     * [min,max) half-open tie policy is observed without PDFium assistance. */
    return std::fabs(x-r.x0)<1e-10 ||
           std::fabs(x-r.x1)<1e-10 ||
           std::fabs(y-r.y0)<1e-10 ||
           std::fabs(y-r.y1)<1e-10;
}
static void qp269_write_holdout_original_v0(
    const char *path,unsigned shape,unsigned crop,int rotation)
{
    CHECK(path && *path && shape<2u && crop<4u &&
        (rotation==0||rotation==90||rotation==180||rotation==270));
    const char *const crops[4]={
        "[2 3 12 11]",
        "[2.125 3 12.125 11]",
        "[2 3.125 12 11.125]",
        "[2.375 3.375 12.375 11.375]"
    };
    auto doc=QPDF::create();
    doc->emptyPDF();
    auto pages=doc->getRoot().getKey("/Pages");
    auto page=doc->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    pages.replaceKey("/Kids",QPDFObjectHandle::newArray({page}));
    pages.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
    pages.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 20 30]"));
    const auto crop_box=QPDFObjectHandle::parse(crops[crop]);
    if(rotation==0||rotation==180)
        pages.replaceKey("/CropBox",crop_box);
    else page.replaceKey("/CropBox",crop_box);
    if(rotation==0||rotation==90)
        pages.replaceKey("/Rotate",QPDFObjectHandle::newInteger(rotation));
    else page.replaceKey("/Rotate",QPDFObjectHandle::newInteger(rotation));
    page.replaceKey("/UserUnit",QPDFObjectHandle::newInteger(7));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/Contents",
        doc->newStream(qp269_holdout_shapes_v0[shape].source_commands));
    QPDFWriter writer(*doc);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    FILE *f=std::fopen(path,"wb"); CHECK(f!=nullptr);
    CHECK(std::fwrite(bytes->getBuffer(),1,bytes->getSize(),f)==
          bytes->getSize());
    CHECK(std::fclose(f)==0);
}
static void qp269_score_print_v0(FILE *out,
    const qp269_score_v0 score[7])
{
    std::fprintf(out,"[");
    for(unsigned m=0u;m<7u;++m){
        std::fprintf(out,
            "{\"method\":\"%s\",\"rgb_abs\":%llu,"
            "\"different\":%zu,\"gross\":%zu}%s",
            qp269_holdout_method_v0[m],
            (unsigned long long)score[m].rgb_abs,
            score[m].different,score[m].gross,
            m==6u?"":",");
    }
    std::fprintf(out,"]");
}
static int test_269_heldout_q4_threshold_center_ab(const char *report_path)
{
    CHECK(report_path && *report_path);
    const char *source=GENERATED_269_Q4_HOLDOUT_PDF;
    const std::string stage=std::string(report_path)+".tmp";
    CHECK(source && *source &&
          std::strcmp(source,report_path)!=0 && stage!=source);
    (void)std::remove(report_path);
    (void)std::remove(stage.c_str());
    const unsigned dpis[3]={72u,96u,144u};
    const quantapdf_ir_raster_v1_limits caps={
        22000u,1u,64u,4000000u
    };
    qp269_score_v0 total[7]={},by_dpi[3][7]={},by_shape[2][7]={};
    qp269_heldout_signed_v0 signed_total[7]={},signed_dpi[3][7]={};
    size_t signed_regression_144_cases=0u;
    size_t handoff_frames=0u,handoff_changed=0u;
    size_t quant_legacy_zero=0u;
    qp269_rect_quant_score_v0 quant_q4_total{},quant_closest_total{};
    uint64_t handoff_exact_rgb=0u,handoff_pre_rgb=0u,handoff_post_rgb=0u;
    size_t improved[7]={},equal[7]={},regressed[7]={};
    size_t ref_nonbinary[3]={},center_edge_ties[3]={};
    size_t original_sources=0u,source_verified=0u,frames=0u;
    size_t pdfium_repeats=0u,native_repeats=0u,legacy_rgba_equal=0u;
    size_t old_size_mismatches=0u,original_source_pixels=0u;
    size_t q4_actual_alpha_bins[17]={},off_q4_alpha=0u;
    FILE *out=std::fopen(stage.c_str(),"wb");CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n \"schema_version\":1,\n"
        " \"source_kind\":\"test_generated_original_pdf\",\n"
        " \"source_provenance\":\"new_heldout_qpdf_original_not_ir_export\",\n"
        " \"source_shapes_holdout_from_269_370\":true,\n"
        " \"pixel_center_uses_value_grid_without_pdfium_pixels\":true,\n"
        " \"binary_center_uses_half_open_source_rectangles\":true,\n"
        " \"thresholds_are_offline_observation_only\":true,\n"
        " \"signed_model_audit_test_only\":true,\n"
        " \"float_handoff_test_only\":true,\n"
        " \"native_renderer_mutated\":false,\n"
        " \"pdfium_fallback_used\":false,\n"
        " \"third_party_native_positive_increment\":0,\n"
        " \"public_sdk_backend_switched\":false,\n"
        " \"dpis\":[72,96,144],\n"
        " \"frames\":[\n");
    for(unsigned shape=0u;shape<2u;++shape)
      for(unsigned crop=0u;crop<4u;++crop)
        for(int rotation=0;rotation<360;rotation+=90){
            const size_t source_number=shape*16u+crop*4u+
                (size_t)rotation/90u;
            CHECK(source_number==original_sources);
            qp269_write_holdout_original_v0(source,shape,crop,rotation);
            const auto original=decimal360_read_bytes(source);
            const uint64_t source_hash=decimal360_fnv64(original);
            quantapdf_document *doc=nullptr;
            quantapdf_graph_snapshot *snapshot=nullptr;
            quantapdf_ir detached{};
            quantapdf_graph_page_geometry geometry{};
            quantapdf_graph_device_grid exact[3]={};
            OK(quantapdf_open(source,nullptr,&doc));
            OK(quantapdf_document_capture_graph(
                doc,&graph_limits,&snapshot));
            quantapdf_close(doc);
            CHECK(snapshot && snapshot->graph);
            OK(quantapdf_graph_query_page_geometry(
                snapshot->graph,0u,4u,&geometry));
            CHECK(geometry.rotation==rotation && geometry.user_unit==7.0);
            OK(quantapdf_graph_snapshot_page_composer_ir(
                snapshot,0u,4u,&decode_limits,&parse_limits,1u,
                &lower_limits,&detached));
            CHECK(detached.page_count==1u &&
                  detached.operation_count==1u &&
                  detached.resources.clip_count==1u &&
                  detached.resources.graphics_state_count==1u);
            for(unsigned j=0u;j<3u;++j){
                OK(quantapdf_graph_page_decimal_handoff_v0(
                    snapshot->graph,0u,4u,dpis[j],22000u,70000u,
                    &exact[j]));
            }
            /* No source/PDFium-owned node reaches the native raster:
             * the sealed Graph owner is dropped before native rendering. */
            quantapdf_graph_snapshot_drop(snapshot);
            snapshot=nullptr;
            ++original_sources;

            quantapdf_document *reference_doc=nullptr;
            quantapdf_page *reference_page=nullptr;
            OK(quantapdf_open(source,nullptr,&reference_doc));
            OK(quantapdf_load_page(reference_doc,0u,&reference_page));
            for(unsigned j=0u;j<3u;++j){
                const unsigned dpi=dpis[j];
                const auto& grid=exact[j];
                const size_t pxcount=(size_t)grid.width*
                    (size_t)grid.height;
                CHECK(pxcount>0u && pxcount<=22000u &&
                      grid.output_bytes==pxcount*3u);
                quantapdf_graph_device_grid old{};
                OK(quantapdf_graph_page_device_grid(
                    &geometry,dpi,22000u,70000u,&old));
                const bool old_size_match=old.width==grid.width &&
                    old.height==grid.height;
                if(!old_size_match)++old_size_mismatches;

                quantapdf_ir_raster_image native{},again{},old_native{};
                OK(quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
                    &detached,0u,&geometry,&grid,dpi,&caps,
                    70000u,&native));
                OK(quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
                    &detached,0u,&geometry,&grid,dpi,&caps,
                    70000u,&again));
                CHECK(native.pixels && again.pixels &&
                      native.width==(size_t)grid.width &&
                      native.height==(size_t)grid.height &&
                      native.stride==native.width*4u &&
                      native.size==pxcount*4u &&
                      again.width==native.width &&
                      again.height==native.height &&
                      again.stride==native.stride &&
                      again.size==native.size &&
                      std::memcmp(native.pixels,again.pixels,
                                  native.size)==0);
                ++native_repeats;
                if(old_size_match){
                    OK(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
                        &detached,0u,&geometry,dpi,&caps,70000u,&old_native));
                    CHECK(old_native.size==native.size &&
                          std::memcmp(old_native.pixels,native.pixels,
                                      native.size)==0);
                    ++legacy_rgba_equal;
                }

                const quantapdf_render_options opts={
                    sizeof(quantapdf_render_options),(float)dpi,0.0f,
                    0,{0},0
                };
                quantapdf_bitmap *ref=nullptr,*repeat=nullptr;
                OK(quantapdf_render_page_with_options(
                    reference_page,&opts,&ref));
                OK(quantapdf_render_page_with_options(
                    reference_page,&opts,&repeat));
                int w=0,h=0,stride=0,components=0;
                int rw=0,rh=0,rstride=0,rcomponents=0;
                OK(quantapdf_bitmap_dimensions(
                    ref,&w,&h,&stride,&components));
                OK(quantapdf_bitmap_dimensions(
                    repeat,&rw,&rh,&rstride,&rcomponents));
                CHECK(w==(int)grid.width && h==(int)grid.height &&
                      components==3 && stride>=w*3 && stride<=w*3+4 &&
                      rw==w && rh==h && rstride==stride &&
                      rcomponents==components);
                const unsigned char *ref_bytes=nullptr,*repeat_bytes=nullptr;
                size_t ref_size=0u,repeat_size=0u;
                OK(quantapdf_bitmap_data(ref,&ref_bytes,&ref_size));
                OK(quantapdf_bitmap_data(
                    repeat,&repeat_bytes,&repeat_size));
                CHECK(ref_bytes && repeat_bytes &&
                      ref_size==(size_t)stride*(size_t)h &&
                      ref_size==repeat_size &&
                      std::memcmp(ref_bytes,repeat_bytes,ref_size)==0);
                ++pdfium_repeats;
                const auto handoff=qp269_audit_handoff_v0(
                    detached,geometry,grid,shape,crop,rotation,
                    ref_bytes,stride,native);
                qp269_rect_quant_score_v0 quant_q4{},quant_closest{};
                qp269_rect_quant_models_v0(handoff.fill.post_box,
                    handoff.clip.post_box,ref_bytes,(size_t)w,(size_t)h,
                    (size_t)stride,&quant_q4,&quant_closest);

                qp269_score_v0 local[7]={};
                qp269_heldout_signed_v0 local_signed[7]={};
                size_t ties=0u,partials=0u;
                for(size_t py=0u;py<(size_t)h;++py)
                  for(size_t px=0u;px<(size_t)w;++px){
                    const size_t index=py*(size_t)w+px;
                    const unsigned char *rgba=native.pixels+index*4u;
                    const unsigned char *rgb=
                        ref_bytes+py*(size_t)stride+px*3u;
                    CHECK(rgb[0]==255u && rgb[1]==rgb[2]);
                    if(rgb[1]!=0u && rgb[1]!=255u)++partials;
                    const unsigned a=rgba[3];
                    bool found_lattice=false;
                    for(unsigned k=0u;k<=16u;++k)
                        if((255u*k+8u)/16u==a){
                            ++q4_actual_alpha_bins[k];
                            found_lattice=true;
                            break;
                        }
                    if(!found_lattice)++off_q4_alpha;

                    double sx=0.0,sy=0.0;
                    CHECK(qp269_source_center_v0(
                        grid,px,py,&sx,&sy));
                    if(qp269_center_boundary_tie_v0(
                        sx,sy,qp269_holdout_shapes_v0[shape].fill) ||
                       qp269_center_boundary_tie_v0(
                        sx,sy,qp269_holdout_shapes_v0[shape].clip))
                        ++ties;
                    const bool center_hit=
                        qp269_in_rect_v0(
                            sx,sy,qp269_holdout_shapes_v0[shape].fill) &&
                        qp269_in_rect_v0(
                            sx,sy,qp269_holdout_shapes_v0[shape].clip);
                    for(unsigned method=0u;method<7u;++method){
                        const bool binary=method!=0u;
                        const bool paint=method==6u?center_hit:
                            method!=0u && a>=qp269_cutoff_v0(method);
                        unsigned rgb_pred[3]={255u,255u,255u};
                        if(!binary){
                            for(unsigned channel=0u;channel<3u;++channel)
                                rgb_pred[channel]=on_white(rgba,channel);
                        }else if(paint){
                            rgb_pred[1]=rgb_pred[2]=0u;
                        }
                        unsigned greatest=0u;
                        uint64_t pixel_rgb_abs=0u;
                        for(unsigned channel=0u;channel<3u;++channel){
                            const unsigned left=rgb_pred[channel];
                            const unsigned right=(unsigned)rgb[channel];
                            const unsigned delta=left>right?
                                left-right:right-left;
                            local[method].rgb_abs+=(uint64_t)delta;
                            pixel_rgb_abs+=(uint64_t)delta;
                            greatest=std::max(greatest,delta);
                        }
                        if(greatest!=0u){
                            ++local[method].different;
                            /* The selected original red-on-white rectangles
                             * have binary PDFium coverage. We never threshold
                             * the reference; every candidate is measured. */
                            CHECK(rgb[1]==0u || rgb[1]==255u);
                            const unsigned ref_cov=255u-(unsigned)rgb[1];
                            const unsigned model_cov=255u-rgb_pred[1];
                            auto& side=local_signed[method];
                            if(ref_cov==255u && model_cov<255u){
                                ++side.under;
                                side.under_rgb_abs+=pixel_rgb_abs;
                            }else if(ref_cov==0u && model_cov>0u){
                                ++side.over;
                                side.over_rgb_abs+=pixel_rgb_abs;
                            }else{
                                ++side.color_only;
                                side.color_only_rgb_abs+=pixel_rgb_abs;
                            }
                        }
                        if(greatest>64u)++local[method].gross;
                    }
                }
                CHECK(handoff.post_rgb_abs==local[0].rgb_abs &&
                      quant_q4.rgb_abs()==local[0].rgb_abs &&
                      quant_closest.rgb_abs()==0u);
                ++quant_legacy_zero;
                qp269_rect_quant_add_v0(&quant_q4_total,quant_q4);
                qp269_rect_quant_add_v0(&quant_closest_total,quant_closest);
                ++handoff_frames;
                handoff_exact_rgb+=handoff.exact_rgb_abs;
                handoff_pre_rgb+=handoff.pre_rgb_abs;
                handoff_post_rgb+=handoff.post_rgb_abs;
                if(handoff.exact_rgb_abs!=handoff.post_rgb_abs)
                    ++handoff_changed;
                ref_nonbinary[j]+=partials;
                center_edge_ties[j]+=ties;
                const size_t frame=source_number*3u+j;
                CHECK(frame==frames);
                for(unsigned method=0u;method<7u;++method){
                    qp269_score_add_v0(&total[method],local[method]);
                    qp269_score_add_v0(&by_dpi[j][method],local[method]);
                    qp269_heldout_signed_add_v0(
                        &signed_total[method],local_signed[method]);
                    qp269_heldout_signed_add_v0(
                        &signed_dpi[j][method],local_signed[method]);
                    const auto& side=local_signed[method];
                    CHECK(side.under+side.over+side.color_only==
                              local[method].different &&
                          side.under_rgb_abs+side.over_rgb_abs+
                              side.color_only_rgb_abs==
                              local[method].rgb_abs);
                    qp269_score_add_v0(&by_shape[shape][method],local[method]);
                    if(local[method].rgb_abs<local[0].rgb_abs)
                        ++improved[method];
                    else if(local[method].rgb_abs>local[0].rgb_abs)
                        ++regressed[method];
                    else ++equal[method];
                }
                /* Pinned historical counterexamples, not tuning
                 * criteria for selecting a new threshold on this corpus. */
                if(frame==68u || frame==71u){
                    CHECK(shape==1u && crop==1u && dpi==144u &&
                          (rotation==180 || rotation==270) &&
                          local[1].rgb_abs>local[0].rgb_abs &&
                          local[2].rgb_abs>local[0].rgb_abs);
                    ++signed_regression_144_cases;
                }
                original_source_pixels+=pxcount;
                std::fprintf(out,
                    "  {\"frame\":%zu,\"shape\":%u,\"crop\":%u,"
                    "\"rotation\":%d,\"dpi\":%u,"
                    "\"source_fnv64\":\"%016llx\","
                    "\"exact_grid\":[%u,%u],\"legacy_grid\":[%u,%u],"
                    "\"legacy_size_same\":%s,"
                    "\"reference_partial_pixels\":%zu,"
                    "\"center_boundary_ties\":%zu,\"scores\":",
                    frame,shape,crop,rotation,dpi,
                    (unsigned long long)source_hash,
                    grid.width,grid.height,old.width,old.height,
                    old_size_match?"true":"false",partials,ties);
                qp269_score_print_v0(out,local);
                std::fprintf(out,",\"signed_scores\":");
                qp269_heldout_signed_print_v0(out,local_signed);
                std::fprintf(out,",\"float_handoff\":");
                qp269_float_handoff_print_v0(out,handoff);
                std::fprintf(out,",\"rect_closest_outer\":");
                qp269_rect_quant_print_v0(out,quant_closest);
                std::fprintf(out,"}%s\n",frame==95u?"":",");
                quantapdf_drop_bitmap(repeat);
                quantapdf_drop_bitmap(ref);
                quantapdf_ir_raster_image_dispose(&old_native);
                quantapdf_ir_raster_image_dispose(&again);
                quantapdf_ir_raster_image_dispose(&native);
                ++frames;
            }
            quantapdf_drop_page(reference_page);
            quantapdf_close(reference_doc);
            quantapdf_ir_dispose(&detached);
            CHECK(decimal360_read_bytes(source)==original);
            CHECK(std::remove(source)==0);
            ++source_verified;
        }
    CHECK(original_sources==32u && source_verified==32u &&
          signed_regression_144_cases==2u && handoff_frames==96u &&
          handoff_changed==4u && handoff_post_rgb==total[0].rgb_abs &&
          quant_legacy_zero==96u &&
          quant_q4_total.rgb_abs()==total[0].rgb_abs &&
          quant_closest_total.rgb_abs()==0u &&
          frames==96u && native_repeats==96u &&
          pdfium_repeats==96u && original_source_pixels>0u &&
          old_size_mismatches+legacy_rgba_equal==96u &&
          old_size_mismatches==16u);
    for(unsigned method=0u;method<7u;++method)
        CHECK(improved[method]+regressed[method]+equal[method]==96u);
    size_t counted_alpha=off_q4_alpha;
    for(unsigned k=0u;k<=16u;++k)
        counted_alpha+=q4_actual_alpha_bins[k];
    CHECK(counted_alpha==original_source_pixels);
    std::fprintf(out," ],\n"
        " \"original_source_count\":%zu,"
        "\"source_bytes_verified\":%zu,"
        "\"original_pdfium_repeats\":%zu,"
        "\"native_repeat_identical\":%zu,"
        "\"frames_count\":%zu,"
        "\"pixels_examined\":%zu,"
        "\"old_size_mismatches\":%zu,"
        "\"old_size_correct_rgba_identical\":%zu,"
        "\"reference_partial_by_dpi\":[%zu,%zu,%zu],"
        "\"center_boundary_ties_by_dpi\":[%zu,%zu,%zu],"
        "\"native_alpha_off_q4_lattice\":%zu,"
        "\"signed_regression_144_frames\":%zu,"
        "\"native_alpha_q4_counts\":[",
        original_sources,source_verified,pdfium_repeats,native_repeats,
        frames,original_source_pixels,old_size_mismatches,
        legacy_rgba_equal,
        ref_nonbinary[0],ref_nonbinary[1],ref_nonbinary[2],
        center_edge_ties[0],center_edge_ties[1],center_edge_ties[2],
        off_q4_alpha,signed_regression_144_cases);
    for(unsigned k=0u;k<=16u;++k)
        std::fprintf(out,"%zu%s",q4_actual_alpha_bins[k],
            k==16u?"":",");
    std::fprintf(out,
        "],\"float_handoff_frames\":%zu,"
        "\"float_handoff_exact_post_changed\":%zu,"
        "\"float_handoff_exact_rgb_abs\":%llu,"
        "\"float_handoff_pre_rgb_abs\":%llu,"
        "\"float_handoff_post_rgb_abs\":%llu,"
        "\"total_scores\":",handoff_frames,handoff_changed,
        (unsigned long long)handoff_exact_rgb,
        (unsigned long long)handoff_pre_rgb,
        (unsigned long long)handoff_post_rgb);
    qp269_score_print_v0(out,total);
    std::fprintf(out,",\"total_signed_scores\":");
    qp269_heldout_signed_print_v0(out,signed_total);
    std::fprintf(out,",\"scores_by_dpi\":[");
    for(unsigned j=0u;j<3u;++j){
        qp269_score_print_v0(out,by_dpi[j]);
        if(j!=2u)std::fprintf(out,",");
    }
    std::fprintf(out,"],\"signed_by_dpi\":[");
    for(unsigned j=0u;j<3u;++j){
        qp269_heldout_signed_print_v0(out,signed_dpi[j]);
        if(j!=2u)std::fprintf(out,",");
    }
    std::fprintf(out,"],\"scores_by_shape\":[");
    for(unsigned shape=0u;shape<2u;++shape){
        qp269_score_print_v0(out,by_shape[shape]);
        if(shape!=1u)std::fprintf(out,",");
    }
    std::fprintf(out,"],\"frames_better_vs_q4\":[");
    for(unsigned j=0u;j<7u;++j)
        std::fprintf(out,"%zu%s",improved[j],j==6u?"":",");
    std::fprintf(out,"],\"frames_equal_vs_q4\":[");
    for(unsigned j=0u;j<7u;++j)
        std::fprintf(out,"%zu%s",equal[j],j==6u?"":",");
    std::fprintf(out,"],\"frames_worse_vs_q4\":[");
    for(unsigned j=0u;j<7u;++j)
        std::fprintf(out,"%zu%s",regressed[j],j==6u?"":",");
    std::fprintf(out,"],\"rect_closest_outer_legacy_zero_frames\":%zu,"
        "\"rect_closest_outer_legacy_q4_total\":",quant_legacy_zero);
    qp269_rect_quant_print_v0(out,quant_q4_total);
    std::fprintf(out,",\"rect_closest_outer_legacy_candidate_total\":");
    qp269_rect_quant_print_v0(out,quant_closest_total);
    std::fprintf(out,",\"rect_quant_phase_falsifiers\":");
    qp269_rect_phase_falsifiers_v0(out,source);
    std::fprintf(out,"\n}\n");
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
    CHECK(std::rename(stage.c_str(),report_path)==0);
    CHECK(corpus_fixture_executions==0u);
    return EXIT_SUCCESS;
}
} /* test-only held-out #269 */
