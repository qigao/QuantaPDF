/* #269: 96-DPI ORIGINAL-PDFIUM primitive mask ownership diagnostic.
 * This is deliberately a test-only C++ oracle (QPDF writer + PDFium only in
 * the TEST binary). The native comparison uses the #367 real private C11
 * exact-Grid Fill/W opt-in after Graph Snapshot and Source teardown.
 * Do not infer external pixel-positive promotion from these generated PDFs.
 */
#include "269_unit7_mask_edge_distance_v0.inc"

static int test_269_unit7_96dpi_mask_ownership(const char *report_path)
{
    CHECK(report_path!=nullptr && *report_path);
    const char *const source_paths[3]={
        GENERATED_269_MASK_COMBINED_PDF,
        GENERATED_269_MASK_FILL_PDF,
        GENERATED_269_MASK_CLIP_PDF
    };
    const std::string stage=std::string(report_path)+".tmp";
    for(unsigned kind=0u;kind<3u;++kind){
        CHECK(source_paths[kind]!=nullptr && *source_paths[kind]);
        CHECK(std::strcmp(source_paths[kind],report_path)!=0 &&
              stage!=source_paths[kind]);
        for(unsigned other=0u;other<kind;++other)
            CHECK(std::strcmp(source_paths[kind],
                              source_paths[other])!=0);
    }
    (void)std::remove(report_path);
    (void)std::remove(stage.c_str());
    const char *const mask_names[3]={"combined","fill_only","clip_only"};
    const char *const region_names[5]={
        "outside_primitives","opaque_both","fill_edge_only",
        "clip_edge_only","both_partial"
    };
    struct region_stat {
        size_t pixels=0u,different=0u,gross=0u;
        uint64_t abs_rgb=0u;
    };
    const quantapdf_ir_raster_v1_limits limits={
        20000u,1u,64u,3000000u
    };
    size_t source_files=0u,source_unchanged=0u;
    size_t reference_repeats=0u,native_repeat_ok=0u;
    size_t legacy_exact_rgba=0u,exact_device_sizes=0u;
    size_t total_pixels=0u,total_different=0u,total_gross=0u;
    size_t total_mask_partial[3]={0u},total_mask_full[3]={0u};
    size_t product_different=0u;
    uint64_t total_rgb_abs=0u,product_abs=0u;
    region_stat total_regions[5]={};
    region_stat total_boundary[4]={};
    size_t native_near2_total=0u,native_far2_total=0u;
    const char *const boundary_names[4]={
        "far_radius1","fill_edge_radius1","clip_edge_radius1",
        "both_edge_radius1"
    };
    FILE *out=std::fopen(stage.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        " \"schema_version\":1,\n"
        " \"source_kind\":\"test_generated_original_pdf\",\n"
        " \"source_writer\":\"test_owned_qpdf_writer\",\n"
        " \"origin\":\"three_unmodified_original_pdf_bytes_per_case\",\n"
        " \"dpi\":96,\n"
        " \"mask_kinds\":[\"combined\",\"fill_only\",\"clip_only\"],\n"
        " \"region_order\":[\"outside_primitives\",\"opaque_both\","
           "\"fill_edge_only\",\"clip_edge_only\",\"both_partial\"],\n"
        " \"native_consumer\":\"explicit_exact_grid_fill_w_clip_test_only\",\n"
        " \"primitive_reference\":\"original_pdfium_only\",\n"
        " \"pdfium_fallback_used\":false,\n"
        " \"native_renderer_modified\":false,\n"
        " \"external_native_pixel_positive_increment\":0,\n"
        " \"public_sdk_backend_switched\":false,\n"
        " \"frames\":[\n");

    for(unsigned crop=0u;crop<4u;++crop)
        for(int rotation=0;rotation<360;rotation+=90){
            const size_t frame=(size_t)crop*4u+
                (size_t)rotation/90u;
            const uint32_t width=rotation%180==0?94u:75u;
            const uint32_t height=rotation%180==0?75u:94u;
            const size_t pixels=(size_t)width*(size_t)height;
            CHECK(pixels==7050u);
            std::vector<unsigned char> masks[3];
            std::vector<unsigned char> originals[3];
            uint64_t original_hashes[3]={};
            size_t mask_partial[3]={0u},mask_full[3]={0u};
            size_t mask_nonzero[3]={0u};
            for(unsigned kind=0u;kind<3u;++kind){
                /* Three distinct originals per crop/rotation:
                 * combined Fill+W, Fill-only and W-only mask represented
                 * by the original W plus a page-wide opaque source Fill. */
                write_single_w_visible_page(
                    rotation,7,source_paths[kind],crop,kind);
                originals[kind]=decimal360_read_bytes(
                    source_paths[kind]);
                original_hashes[kind]=decimal360_fnv64(
                    originals[kind]);
                quantapdf_document *doc=nullptr;
                quantapdf_page *page=nullptr;
                quantapdf_bitmap *first=nullptr,*again=nullptr;
                OK(quantapdf_open(source_paths[kind],nullptr,&doc));
                OK(quantapdf_load_page(doc,0u,&page));
                const quantapdf_render_options opt={
                    sizeof(quantapdf_render_options),96.0f,
                    0.0f,0,{0},0
                };
                OK(quantapdf_render_page_with_options(
                    page,&opt,&first));
                OK(quantapdf_render_page_with_options(
                    page,&opt,&again));
                int w=0,h=0,stride=0,components=0;
                int rw=0,rh=0,rstride=0,rcomponents=0;
                OK(quantapdf_bitmap_dimensions(
                    first,&w,&h,&stride,&components));
                OK(quantapdf_bitmap_dimensions(
                    again,&rw,&rh,&rstride,&rcomponents));
                CHECK(w==(int)width && h==(int)height &&
                      components==3 && stride>=w*3 &&
                      stride<=w*3+4 && rw==w && rh==h &&
                      rstride==stride && rcomponents==components);
                const unsigned char *rgb=nullptr,*repeat_rgb=nullptr;
                size_t rgb_bytes=0u,repeat_bytes=0u;
                OK(quantapdf_bitmap_data(first,&rgb,&rgb_bytes));
                OK(quantapdf_bitmap_data(
                    again,&repeat_rgb,&repeat_bytes));
                CHECK(rgb && repeat_rgb &&
                      rgb_bytes==(size_t)stride*(size_t)h &&
                      rgb_bytes==repeat_bytes &&
                      std::memcmp(rgb,repeat_rgb,rgb_bytes)==0);
                ++reference_repeats;
                masks[kind].resize(pixels);
                for(size_t y=0u;y<(size_t)height;++y)
                    for(size_t x=0u;x<(size_t)width;++x) {
                        const unsigned char *p=
                            rgb+y*(size_t)stride+3u*x;
                        /* All originals paint opaque solid red with
                         * the same PDFium white background. The green
                         * channel is an independent coverage observation,
                         * NOT the native raster alpha. */
                        CHECK(p[0]==255u && p[1]==p[2]);
                        const unsigned char cov=
                            (unsigned char)(255u-p[1]);
                        masks[kind][y*(size_t)width+x]=cov;
                        if(cov==255u)++mask_full[kind];
                        if(cov>0u && cov<255u)++mask_partial[kind];
                        if(cov>0u)++mask_nonzero[kind];
                    }
                total_mask_partial[kind]+=mask_partial[kind];
                total_mask_full[kind]+=mask_full[kind];
                quantapdf_drop_bitmap(again);
                quantapdf_drop_bitmap(first);
                quantapdf_drop_page(page);
                quantapdf_close(doc);
                CHECK(decimal360_read_bytes(source_paths[kind])==
                      originals[kind]);
                ++source_files;
            }

            quantapdf_document *doc=nullptr;
            quantapdf_graph_snapshot *snapshot=nullptr;
            quantapdf_ir ir{};
            quantapdf_graph_page_geometry geometry{};
            quantapdf_graph_device_grid exact{},legacy{};
            OK(quantapdf_open(source_paths[0],nullptr,&doc));
            OK(quantapdf_document_capture_graph(
                doc,&graph_limits,&snapshot));
            quantapdf_close(doc);
            CHECK(snapshot && snapshot->graph);
            OK(quantapdf_graph_query_page_geometry(
                snapshot->graph,0u,4u,&geometry));
            CHECK(geometry.rotation==rotation &&
                  geometry.user_unit==7.0);
            OK(quantapdf_graph_page_decimal_handoff_v0(
                snapshot->graph,0u,4u,96u,20000u,70000u,
                &exact));
            CHECK(exact.width==width && exact.height==height &&
                  exact.output_bytes==pixels*3u);
            ++exact_device_sizes;
            OK(quantapdf_graph_snapshot_page_composer_ir(
                snapshot,0u,4u,&decode_limits,&parse_limits,1u,
                &lower_limits,&ir));
            CHECK(ir.page_count==1u && ir.operation_count==1u &&
                  ir.resources.clip_count==1u &&
                  ir.resources.graphics_state_count==1u);
            quantapdf_graph_snapshot_drop(snapshot);
            snapshot=nullptr;
            /* After this line only copied exact Grid + geometry and
             * detached owned Composer IR reach the native renderer. */
            OK(quantapdf_graph_page_device_grid(
                &geometry,96u,20000u,70000u,&legacy));
            CHECK(legacy.width==width && legacy.height==height);
            quantapdf_ir_raster_image native{},native_repeat{},old{};
            OK(quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
                &ir,0u,&geometry,&exact,96u,&limits,70000u,
                &native));
            OK(quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
                &ir,0u,&geometry,&exact,96u,&limits,70000u,
                &native_repeat));
            OK(quantapdf_ir_raster_visible_page_fillclip_preview_v0(
                &ir,0u,&geometry,96u,&limits,70000u,&old));
            CHECK(native.pixels && native_repeat.pixels && old.pixels &&
                  native.width==(size_t)width &&
                  native.height==(size_t)height &&
                  native.stride==(size_t)width*4u &&
                  native.size==pixels*4u &&
                  native_repeat.size==native.size &&
                  old.size==native.size &&
                  std::memcmp(native.pixels,native_repeat.pixels,
                              native.size)==0 &&
                  std::memcmp(native.pixels,old.pixels,
                              native.size)==0);
            ++native_repeat_ok;
            ++legacy_exact_rgba;
            region_stat regions[5]={};
            region_stat boundary[4]={};
            size_t near2=0u,far2=0u;
            uint64_t rgb_abs=0u,primitive_product_abs=0u;
            size_t differing=0u,gross=0u,product_diff=0u;
            size_t nonzero_product=0u,nonzero_native=0u;
            for(size_t k=0u;k<pixels;++k){
                const unsigned f=(unsigned)masks[1][k];
                const unsigned w=(unsigned)masks[2][k];
                const unsigned combined=(unsigned)masks[0][k];
                const unsigned predicted=(f*w+127u)/255u;
                if(predicted>0u)++nonzero_product;
                if(combined!=predicted)++product_diff;
                primitive_product_abs+=(uint64_t)(
                    combined>predicted?combined-predicted:
                    predicted-combined);
                const unsigned char *native_rgba=
                    native.pixels+4u*k;
                const unsigned red=on_white(native_rgba,0u);
                const unsigned green=on_white(native_rgba,1u);
                const unsigned blue=on_white(native_rgba,2u);
                const unsigned reference_green=255u-combined;
                CHECK(red<=255u && green<=255u && blue<=255u);
                const unsigned native_cov=255u-green;
                if(native_cov>0u)++nonzero_native;
                const unsigned channel_error[3]={
                    red>255u?red-255u:255u-red,
                    green>reference_green?green-reference_green:
                        reference_green-green,
                    blue>reference_green?blue-reference_green:
                        reference_green-blue
                };
                const uint64_t rgb_difference=(uint64_t)channel_error[0]+
                    (uint64_t)channel_error[1]+
                    (uint64_t)channel_error[2];
                const unsigned largest=std::max(
                    channel_error[0],std::max(
                        channel_error[1],channel_error[2]));
                const unsigned bucket=
                    (f==0u || w==0u)?0u:
                    (f==255u && w==255u)?1u:
                    (f<255u && w==255u)?2u:
                    (f==255u && w<255u)?3u:4u;
                CHECK(bucket<5u);
                auto& reg=regions[bucket];
                ++reg.pixels;
                reg.abs_rgb+=rgb_difference;
                if(largest>0u){++reg.different;++differing;}
                if(largest>64u){++reg.gross;++gross;}
                rgb_abs+=rgb_difference;
                if(largest>0u) {
                    /* Ownership is inferred from ORIGINAL PDFium binary
                     * primitive-mask spatial transitions, NOT native
                     * pixel coverage. This does not adjust any pixels. */
                    const size_t x=k%(size_t)width;
                    const size_t y=k/(size_t)width;
                    const bool near_fill1=
                        qp269_near_primitive_mask_transition(
                            masks[1],width,height,x,y,1);
                    const bool near_clip1=
                        qp269_near_primitive_mask_transition(
                            masks[2],width,height,x,y,1);
                    const unsigned owner=(near_fill1?1u:0u)|
                        (near_clip1?2u:0u);
                    CHECK(owner<4u);
                    ++boundary[owner].different;
                    boundary[owner].abs_rgb+=rgb_difference;
                    if(largest>64u) ++boundary[owner].gross;
                    const bool near_fill2=
                        qp269_near_primitive_mask_transition(
                            masks[1],width,height,x,y,2);
                    const bool near_clip2=
                        qp269_near_primitive_mask_transition(
                            masks[2],width,height,x,y,2);
                    if(near_fill2||near_clip2) ++near2;
                    else ++far2;
                }
            }
            CHECK(differing<=pixels && gross<=differing &&
                  near2+far2==differing);
            size_t owner_count=0u,owner_gross=0u;
            uint64_t owner_abs=0u;
            for(unsigned owner=0u;owner<4u;++owner) {
                const auto& part=boundary[owner];
                auto& all=total_boundary[owner];
                CHECK(part.gross<=part.different);
                owner_count+=part.different;
                owner_gross+=part.gross;
                owner_abs+=part.abs_rgb;
                all.different+=part.different;
                all.gross+=part.gross;
                all.abs_rgb+=part.abs_rgb;
            }
            CHECK(owner_count==differing && owner_gross==gross &&
                  owner_abs==rgb_abs);
            for(unsigned region=0u;region<5u;++region){
                auto& dest=total_regions[region];
                const auto& entry=regions[region];
                dest.pixels+=entry.pixels;
                dest.different+=entry.different;
                dest.gross+=entry.gross;
                dest.abs_rgb+=entry.abs_rgb;
            }
            total_pixels+=pixels;
            total_rgb_abs+=rgb_abs;
            total_different+=differing;
            total_gross+=gross;
            product_abs+=primitive_product_abs;
            product_different+=product_diff;
            native_near2_total+=near2;
            native_far2_total+=far2;
            std::fprintf(out,
                "  {\"crop\":%u,\"rotation\":%d,\"dpi\":96,"
                "\"original_pdf_bytes_fnv64\":"
                  "[\"%016llx\",\"%016llx\",\"%016llx\"],"
                "\"size\":[%u,%u],"
                "\"mask_nonzero\":[%zu,%zu,%zu],"
                "\"mask_partial\":[%zu,%zu,%zu],"
                "\"mask_full\":[%zu,%zu,%zu],"
                "\"product_nonzero\":%zu,"
                "\"native_nonzero\":%zu,"
                "\"original_combined_vs_product_abs\":%llu,"
                "\"original_combined_vs_product_diff\":%zu,"
                "\"native_vs_combined_rgb_abs\":%llu,"
                "\"native_vs_combined_different\":%zu,"
                "\"native_vs_combined_gross\":%zu,"
                "\"regions\":[\n",
                crop,rotation,
                (unsigned long long)original_hashes[0],
                (unsigned long long)original_hashes[1],
                (unsigned long long)original_hashes[2],
                width,height,
                mask_nonzero[0],mask_nonzero[1],mask_nonzero[2],
                mask_partial[0],mask_partial[1],mask_partial[2],
                mask_full[0],mask_full[1],mask_full[2],
                nonzero_product,nonzero_native,
                (unsigned long long)primitive_product_abs,product_diff,
                (unsigned long long)rgb_abs,differing,gross);
            for(unsigned region=0u;region<5u;++region){
                const auto& stat=regions[region];
                std::fprintf(out,
                    "    {\"name\":\"%s\",\"pixels\":%zu,"
                    "\"different\":%zu,\"gross\":%zu,"
                    "\"rgb_abs\":%llu}%s\n",
                    region_names[region],stat.pixels,stat.different,
                    stat.gross,(unsigned long long)stat.abs_rgb,
                    region==4u?"":",");
            }
            std::fprintf(out,"  ],\"boundary_near1\":[\n");
            for(unsigned owner=0u;owner<4u;++owner) {
                const auto& part=boundary[owner];
                std::fprintf(out,
                    "    {\"name\":\"%s\",\"different\":%zu,"
                    "\"gross\":%zu,\"rgb_abs\":%llu}%s\n",
                    boundary_names[owner],part.different,part.gross,
                    (unsigned long long)part.abs_rgb,
                    owner==3u?"":",");
            }
            std::fprintf(out,
                "  ],\"native_diff_near_radius2\":%zu,"
                "\"native_diff_far_radius2\":%zu}%s\n",
                near2,far2,frame==15u?"":",");
            quantapdf_ir_raster_image_dispose(&old);
            quantapdf_ir_raster_image_dispose(&native_repeat);
            quantapdf_ir_raster_image_dispose(&native);
            quantapdf_ir_dispose(&ir);
            for(unsigned kind=0u;kind<3u;++kind){
                CHECK(decimal360_read_bytes(source_paths[kind])==
                      originals[kind]);
                CHECK(std::remove(source_paths[kind])==0);
                ++source_unchanged;
            }
        }

    size_t region_total=0u,diff_total=0u,gross_total=0u;
    uint64_t error_total=0u;
    for(unsigned i=0u;i<5u;++i){
        region_total+=total_regions[i].pixels;
        diff_total+=total_regions[i].different;
        gross_total+=total_regions[i].gross;
        error_total+=total_regions[i].abs_rgb;
    }
    CHECK(source_files==48u && source_unchanged==48u &&
          reference_repeats==48u && native_repeat_ok==16u &&
          legacy_exact_rgba==16u && exact_device_sizes==16u &&
          total_pixels==16u*7050u && region_total==total_pixels &&
          diff_total==total_different &&
          gross_total==total_gross && error_total==total_rgb_abs &&
          product_different<=total_pixels &&
          native_near2_total+native_far2_total==total_different);
    std::fprintf(out,
        " ],\n"
        " \"source_files\":%zu,"
        "\"source_byte_repeat_count\":%zu,"
        "\"original_pdfium_repeat_frames\":%zu,"
        "\"native_exact_grid_frames\":%zu,"
        "\"native_repeat_identical_frames\":%zu,"
        "\"legacy_native_rgba_identical_frames\":%zu,"
        "\"pixels_examined\":%zu,"
        "\"native_vs_original_rgb_abs\":%llu,"
        "\"native_vs_original_different\":%zu,"
        "\"native_vs_original_gross\":%zu,"
        "\"original_combined_vs_product_abs\":%llu,"
        "\"original_combined_vs_product_different\":%zu,"
        "\"mask_partial_total\":[%zu,%zu,%zu],"
        "\"mask_full_total\":[%zu,%zu,%zu],"
        "\"native_diff_near_radius2\":%zu,"
        "\"native_diff_far_radius2\":%zu,"
        "\"regions\":[\n",
        source_files,source_unchanged,reference_repeats,
        exact_device_sizes,native_repeat_ok,legacy_exact_rgba,
        total_pixels,(unsigned long long)total_rgb_abs,
        total_different,total_gross,
        (unsigned long long)product_abs,product_different,
        total_mask_partial[0],total_mask_partial[1],
        total_mask_partial[2],
        total_mask_full[0],total_mask_full[1],total_mask_full[2],
        native_near2_total,native_far2_total);
    for(unsigned region=0u;region<5u;++region){
        const auto& stat=total_regions[region];
        std::fprintf(out,
            "  {\"name\":\"%s\",\"pixels\":%zu,"
            "\"different\":%zu,\"gross\":%zu,"
            "\"rgb_abs\":%llu}%s\n",
            region_names[region],stat.pixels,stat.different,
            stat.gross,(unsigned long long)stat.abs_rgb,
            region==4u?"":",");
    }
    std::fprintf(out," ],\"boundary_near1\":[\n");
    for(unsigned owner=0u;owner<4u;++owner) {
        const auto& part=total_boundary[owner];
        std::fprintf(out,
            "  {\"name\":\"%s\",\"different\":%zu,"
            "\"gross\":%zu,\"rgb_abs\":%llu}%s\n",
            boundary_names[owner],part.different,part.gross,
            (unsigned long long)part.abs_rgb,
            owner==3u?"":",");
    }
    std::fprintf(out," ]\n}\n");
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
    CHECK(std::rename(stage.c_str(),report_path)==0);
    CHECK(corpus_fixture_executions==0u);
    return EXIT_SUCCESS;
}
