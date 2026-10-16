/* #360 isolated SOURCE-backed pixel diagnostic on the explicit exact Grid.
 * The production SDK remains PDFium. This test does not admit third-party
 * original pixel parity and never uses a PDFium fallback for native paint. */
static int test_decimal360_exact_fillclip_pixel_ab(const char *report_path)
{
    CHECK(report_path!=nullptr && *report_path);
    const char *source=GENERATED_UNIT7_EXACT_FILLCLIP_PDF;
    const std::string stage=std::string(report_path)+".tmp";
    CHECK(source && *source && std::strcmp(source,report_path)!=0 &&
          stage!=source);
    (void)std::remove(report_path); (void)std::remove(stage.c_str());
    const unsigned dpis[3]={72u,96u,144u};
    const quantapdf_ir_raster_v1_limits limits={20000u,1u,64u,3000000u};
    size_t source_count=0u,source_unchanged=0u,frame_count=0u;
    size_t original_pdfium_repeat=0u,exact_grid_pdfium_match=0u;
    size_t legacy_grid_size_mismatch=0u,legacy_same_status=0u;
    size_t legacy_native_exact_rgba_matches=0u;
    size_t native_admitted=0u,native_rejected=0u;
    size_t candidate_repeat_identical=0u,negative_zero_count=0u;
    size_t candidate_difference_pixels=0u,candidate_gross_pixels=0u;
    uint64_t total_rgb_abs_error=0u;
    FILE *out=std::fopen(stage.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,"{\n"
      " \"schema_version\":1,\n"
      " \"source_kind\":\"test_generated_original_pdf\",\n"
      " \"source_producer\":\"QPDF_original_not_composer_reexport\",\n"
      " \"exact_grid_supplied_before_owner_drop\":true,\n"
      " \"new_renderer_opt_in_test_only\":true,\n"
      " \"legacy_renderer_unchanged\":true,\n"
      " \"pdfium_original_test_oracle_only\":true,\n"
      " \"native_positive_provenance_increment\":0,\n"
      " \"public_sdk_backend_switched\":false,\n"
      " \"frames\":[\n");
    for(unsigned crop=0u;crop<4u;++crop)
        for(int rotation=0;rotation<360;rotation+=90) {
            write_single_w_visible_page(rotation,7,source,crop,0u);
            const auto original=decimal360_read_bytes(source);
            quantapdf_document *doc=nullptr;
            quantapdf_graph_snapshot *snapshot=nullptr;
            quantapdf_ir ir{};
            quantapdf_graph_page_geometry geometry{};
            quantapdf_graph_device_grid exact[3]={};
            OK(quantapdf_open(source,nullptr,&doc));
            OK(quantapdf_document_capture_graph(
                doc,&graph_limits,&snapshot));
            quantapdf_close(doc);
            CHECK(snapshot && snapshot->graph);
            OK(quantapdf_graph_query_page_geometry(
                snapshot->graph,0u,4u,&geometry));
            CHECK(geometry.rotation==rotation &&
                  geometry.user_unit==7.0);
            OK(quantapdf_graph_snapshot_page_composer_ir(
                snapshot,0u,4u,&decode_limits,&parse_limits,1u,
                &lower_limits,&ir));
            CHECK(ir.page_count==1u && ir.operation_count==1u &&
                  ir.resources.clip_count==1u &&
                  ir.resources.graphics_state_count==1u);
            for(unsigned j=0u;j<3u;++j) {
                OK(quantapdf_graph_page_decimal_handoff_v0(
                    snapshot->graph,0u,4u,dpis[j],20000u,70000u,
                    &exact[j]));
                CHECK(exact[j].width>0u && exact[j].height>0u &&
                      exact[j].output_bytes==(size_t)exact[j].width*
                         (size_t)exact[j].height*3u);
            }
            /* Both PDF document and Graph Snapshot are closed/dropped before
             * any opt-in C11 native raster invocation. Detached Owned IR
             * and exact copied metadata are the sole downstream inputs. */
            quantapdf_graph_snapshot_drop(snapshot);
            snapshot=nullptr;
            ++source_count;
            quantapdf_document *pdfdoc=nullptr;
            quantapdf_page *page=nullptr;
            OK(quantapdf_open(source,nullptr,&pdfdoc));
            OK(quantapdf_load_page(pdfdoc,0u,&page));
            for(unsigned j=0u;j<3u;++j) {
                const unsigned dpi=dpis[j];
                const auto& grid=exact[j];
                const size_t frame=(size_t)crop*12u+
                    (size_t)(rotation/90)*3u+j;
                CHECK(frame==frame_count);
                quantapdf_graph_device_grid legacy_grid{};
                OK(quantapdf_graph_page_device_grid(
                    &geometry,dpi,20000u,70000u,&legacy_grid));
                const bool legacy_size_matches=
                    legacy_grid.width==grid.width &&
                    legacy_grid.height==grid.height;
                if(!legacy_size_matches)++legacy_grid_size_mismatch;
                quantapdf_ir_raster_image candidate{},again{},legacy{};
                const quantapdf_status candidate_st=
                    quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
                        &ir,0u,&geometry,&grid,dpi,&limits,70000u,
                        &candidate);
                const quantapdf_status repeat_st=
                    quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
                        &ir,0u,&geometry,&grid,dpi,&limits,70000u,
                        &again);
                CHECK(candidate_st==repeat_st);
                CHECK(candidate_st==QUANTAPDF_OK ||
                      candidate_st==QUANTAPDF_ERROR_UNSUPPORTED);
                const quantapdf_status legacy_st=
                    quantapdf_ir_raster_visible_page_fillclip_preview_v0(
                        &ir,0u,&geometry,dpi,&limits,70000u,&legacy);
                CHECK(legacy_st==QUANTAPDF_OK ||
                      legacy_st==QUANTAPDF_ERROR_UNSUPPORTED);
                if(legacy_size_matches){
                    CHECK(candidate_st==legacy_st);
                    ++legacy_same_status;
                }
                if(candidate_st==QUANTAPDF_OK) {
                    const size_t width=(size_t)grid.width;
                    const size_t height=(size_t)grid.height;
                    CHECK(candidate.pixels && again.pixels &&
                          candidate.width==width &&
                          candidate.height==height &&
                          candidate.stride==width*4u &&
                          candidate.size==width*height*4u &&
                          again.width==candidate.width &&
                          again.height==candidate.height &&
                          again.stride==candidate.stride &&
                          again.size==candidate.size &&
                          std::memcmp(candidate.pixels,again.pixels,
                                      candidate.size)==0);
                    ++native_admitted;
                    ++candidate_repeat_identical;
                    if(legacy_size_matches){
                        CHECK(legacy_st==QUANTAPDF_OK &&
                              legacy.pixels &&
                              legacy.size==candidate.size &&
                              std::memcmp(legacy.pixels,candidate.pixels,
                                          candidate.size)==0);
                        ++legacy_native_exact_rgba_matches;
                    }
                }else{
                    const quantapdf_ir_raster_image zero{};
                    CHECK(std::memcmp(&candidate,&zero,sizeof(candidate))==0 &&
                          std::memcmp(&again,&zero,sizeof(again))==0);
                    ++native_rejected;
                }
                /* Negative inputs must never publish a partial bitmap,
                 * including after a prior successful call on the same IR. */
                const auto refuses=[&](
                    const quantapdf_graph_device_grid *supplied,unsigned d,
                    const quantapdf_ir_raster_v1_limits *caps,
                    size_t cap,quantapdf_status want) {
                    quantapdf_ir_raster_image refused{},zero{};
                    std::memset(&refused,0xa5,sizeof(refused));
                    CHECK(quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
                        &ir,0u,&geometry,supplied,d,caps,cap,&refused)==want);
                    CHECK(std::memcmp(&refused,&zero,sizeof(refused))==0);
                    ++negative_zero_count;
                };
                refuses(nullptr,dpi,&limits,70000u,QUANTAPDF_ERROR_ARGUMENT);
                refuses(&grid,120u,&limits,70000u,QUANTAPDF_ERROR_UNSUPPORTED);
                refuses(&grid,dpi,&limits,1u,QUANTAPDF_ERROR_UNSUPPORTED);
                quantapdf_graph_device_grid corrupted=grid;
                corrupted.stride_bytes+=1u;
                refuses(&corrupted,dpi,&limits,70000u,
                    QUANTAPDF_ERROR_UNSUPPORTED);
                corrupted=grid;
                corrupted.from_source[0]=std::nan("");
                refuses(&corrupted,dpi,&limits,70000u,
                    QUANTAPDF_ERROR_UNSUPPORTED);

                const quantapdf_render_options opts={
                    sizeof(quantapdf_render_options),(float)dpi,
                    0.0f,0,{0},0
                };
                quantapdf_bitmap *ref=nullptr,*ref_repeat=nullptr;
                OK(quantapdf_render_page_with_options(page,&opts,&ref));
                OK(quantapdf_render_page_with_options(
                    page,&opts,&ref_repeat));
                int pw=0,ph=0,ps=0,pc=0,rw=0,rh=0,rs=0,rc=0;
                OK(quantapdf_bitmap_dimensions(ref,&pw,&ph,&ps,&pc));
                OK(quantapdf_bitmap_dimensions(
                    ref_repeat,&rw,&rh,&rs,&rc));
                CHECK(pw==(int)grid.width && ph==(int)grid.height &&
                      pc==3 && ps>=pw*3 && ps<=pw*3+4 &&
                      rw==pw && rh==ph && rs==ps && rc==pc);
                const unsigned char *rgb=nullptr,*rgb_repeat=nullptr;
                size_t rgb_bytes=0u,rgb_repeat_bytes=0u;
                OK(quantapdf_bitmap_data(ref,&rgb,&rgb_bytes));
                OK(quantapdf_bitmap_data(
                    ref_repeat,&rgb_repeat,&rgb_repeat_bytes));
                CHECK(rgb && rgb_repeat &&
                      rgb_bytes==(size_t)ps*(size_t)ph &&
                      rgb_repeat_bytes==rgb_bytes &&
                      std::memcmp(rgb,rgb_repeat,rgb_bytes)==0);
                ++original_pdfium_repeat;
                ++exact_grid_pdfium_match;

                uint64_t abs_rgb=0u;
                size_t different=0u,gross=0u;
                unsigned largest=0u;
                if(candidate_st==QUANTAPDF_OK)
                    for(size_t y=0u;y<(size_t)ph;++y)
                        for(size_t x=0u;x<(size_t)pw;++x) {
                            const unsigned char *native_px=
                                candidate.pixels+
                                y*candidate.stride+4u*x;
                            const unsigned char *oracle_px=
                                rgb+y*(size_t)ps+3u*x;
                            unsigned maximum=0u;
                            for(unsigned color=0u;color<3u;++color){
                                const unsigned value=on_white(
                                    native_px,color);
                                const unsigned target=oracle_px[color];
                                const unsigned diff=value>target?
                                    value-target:target-value;
                                abs_rgb+=(uint64_t)diff;
                                if(diff>maximum)maximum=diff;
                            }
                            if(maximum!=0u)++different;
                            if(maximum>64u)++gross;
                            if(maximum>largest)largest=maximum;
                        }
                total_rgb_abs_error+=abs_rgb;
                candidate_difference_pixels+=different;
                candidate_gross_pixels+=gross;
                std::fprintf(out,
                    "  {\"crop\":%u,\"rotation\":%d,\"dpi\":%u,"
                    "\"frame\":%zu,\"pdfium_grid\":[%d,%d],"
                    "\"exact_grid\":[%u,%u],\"legacy_grid\":[%u,%u],"
                    "\"legacy_size_matches\":%s,"
                    "\"exact_native_status\":%d,"
                    "\"legacy_native_status\":%d,"
                    "\"native_rgb_abs_error\":%llu,"
                    "\"native_different_pixels\":%zu,"
                    "\"native_gross_pixels\":%zu,"
                    "\"native_max_rgb_delta\":%u}%s\n",
                    crop,rotation,dpi,frame,pw,ph,grid.width,grid.height,
                    legacy_grid.width,legacy_grid.height,
                    legacy_size_matches?"true":"false",
                    (int)candidate_st,(int)legacy_st,
                    (unsigned long long)abs_rgb,different,gross,largest,
                    frame==47u?"":",");
                quantapdf_drop_bitmap(ref_repeat);
                quantapdf_drop_bitmap(ref);
                quantapdf_ir_raster_image_dispose(&legacy);
                quantapdf_ir_raster_image_dispose(&again);
                quantapdf_ir_raster_image_dispose(&candidate);
                ++frame_count;
            }
            quantapdf_drop_page(page);
            quantapdf_close(pdfdoc);
            quantapdf_ir_dispose(&ir);
            CHECK(decimal360_read_bytes(source)==original);
            CHECK(std::remove(source)==0);
            ++source_unchanged;
        }
    CHECK(source_count==16u && source_unchanged==16u &&
          frame_count==48u && original_pdfium_repeat==48u &&
          exact_grid_pdfium_match==48u &&
          native_admitted+native_rejected==48u &&
          legacy_same_status+legacy_grid_size_mismatch==48u &&
          negative_zero_count==240u);
    std::fprintf(out," ],\n"
       " \"original_source_count\":%zu,"
       "\"source_bytes_unchanged_count\":%zu,"
       "\"frame_count\":%zu,"
       "\"pdfium_repeat_count\":%zu,"
       "\"exact_grid_pdfium_size_matches\":%zu,"
       "\"legacy_grid_dimension_discrepancies\":%zu,"
       "\"legacy_size_same_status_cases\":%zu,"
       "\"legacy_native_rgba_exact_matches\":%zu,"
       "\"new_native_admitted_frames\":%zu,"
       "\"new_native_rejected_frames\":%zu,"
       "\"new_native_repeat_identical_frames\":%zu,"
       "\"negative_zero_output_cases\":%zu,"
       "\"total_native_rgb_abs_error\":%llu,"
       "\"total_native_different_pixels\":%zu,"
       "\"total_native_gross_pixels\":%zu"
       "\n}\n",
       source_count,source_unchanged,frame_count,original_pdfium_repeat,
       exact_grid_pdfium_match,legacy_grid_size_mismatch,
       legacy_same_status,legacy_native_exact_rgba_matches,
       native_admitted,native_rejected,candidate_repeat_identical,
       negative_zero_count,
       (unsigned long long)total_rgb_abs_error,
       candidate_difference_pixels,candidate_gross_pixels);
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
    CHECK(std::rename(stage.c_str(),report_path)==0);
    CHECK(corpus_fixture_executions==0u);
    return EXIT_SUCCESS;
}
