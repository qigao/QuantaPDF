/* #360 consumer-admission gate for the EXISTING private Q4 sampling API.
 * Included by the original-PDF source oracle after decimal360_bits().
 * Graph/Source owners MUST already have been dropped by the caller.
 * This is metadata/sampling ONLY, never Fill/Clip paint parity or fallback.
 */
struct decimal360_q4_consumer_observation {
    size_t valid_samples=0u;
    size_t legacy_exact_samples=0u;
    size_t size_discrepant_samples=0u;
    size_t sample_refusals=0u;
    size_t q4_work_acceptances=0u;
    size_t work_budget_refusals=0u;
    size_t edge_budget_refusals=0u;
    uint64_t sample_fnv64=UINT64_C(14695981039346656037);
};

static void decimal360_q4_hash(uint64_t *hash,uint64_t bits)
{
    /* FNV-1a over eight logical low-to-high bytes. Do NOT hash native
     * structure padding or host-endian raw double memory. */
    for(unsigned i=0u;i<8u;++i) {
        *hash^=(bits>>(8u*i))&UINT64_C(255);
        *hash*=UINT64_C(1099511628211);
    }
}

static decimal360_q4_consumer_observation
decimal360_probe_existing_q4_consumer(
    const quantapdf_graph_device_grid& exact,
    const quantapdf_graph_device_grid& old_grid,
    const quantapdf_graph_page_geometry& page,
    bool old_dimensions_match)
{
    decimal360_q4_consumer_observation result{};
    CHECK(exact.width>2u && exact.height>2u);
    const size_t pixels=(size_t)exact.width*(size_t)exact.height;
    CHECK(pixels<=20000u && pixels<=SIZE_MAX/128u);
    const size_t bounded_work=pixels*16u*8u;
    OK(quantapdf_graph_device_grid_admit_q4_work(
        &exact,8u,8u,bounded_work));
    ++result.q4_work_acceptances;
    CHECK(quantapdf_graph_device_grid_admit_q4_work(
        &exact,8u,8u,bounded_work-1u)==QUANTAPDF_ERROR_UNSUPPORTED);
    ++result.work_budget_refusals;
    CHECK(quantapdf_graph_device_grid_admit_q4_work(
        &exact,9u,8u,bounded_work)==QUANTAPDF_ERROR_UNSUPPORTED);
    ++result.edge_budget_refusals;

    const uint32_t anchor_x[5]={
        0u,exact.width-1u,0u,exact.width-1u,exact.width/2u
    };
    const uint32_t anchor_y[5]={
        0u,0u,exact.height-1u,exact.height-1u,exact.height/2u
    };
    for(unsigned anchor=0u;anchor<5u;++anchor)
        for(unsigned sub_y=0u;sub_y<4u;++sub_y)
            for(unsigned sub_x=0u;sub_x<4u;++sub_x) {
                const uint32_t px=anchor_x[anchor],py=anchor_y[anchor];
                double source_x=0.0,source_y=0.0;
                OK(quantapdf_graph_device_grid_sample_q4(
                    &exact,px,py,sub_x,sub_y,&source_x,&source_y));
                CHECK(std::isfinite(source_x) && std::isfinite(source_y));
                const double target_dx=(double)px+
                    ((double)sub_x+0.5)/4.0;
                const double target_dy=(double)py+
                    ((double)sub_y+0.5)/4.0;
                const double actual_dx=exact.from_source[0]*source_x+
                    exact.from_source[2]*source_y+exact.from_source[4];
                const double actual_dy=exact.from_source[1]*source_x+
                    exact.from_source[3]*source_y+exact.from_source[5];
                CHECK(std::isfinite(actual_dx) && std::isfinite(actual_dy) &&
                    std::fabs(target_dx-actual_dx)<1e-9 &&
                    std::fabs(target_dy-actual_dy)<1e-9);
                /* This is a bounded source-location probe, never an
                 * assumption about source Fill/W coverage or alpha. */
                CHECK(source_x>=page.crop_box[0]-1e-9 &&
                      source_x<=page.crop_box[2]+1e-9 &&
                      source_y>=page.crop_box[1]-1e-9 &&
                      source_y<=page.crop_box[3]+1e-9);
                decimal360_q4_hash(
                    &result.sample_fnv64,decimal360_bits(source_x));
                decimal360_q4_hash(
                    &result.sample_fnv64,decimal360_bits(source_y));
                ++result.valid_samples;
                if(old_dimensions_match) {
                    double old_x=0.0,old_y=0.0;
                    OK(quantapdf_graph_device_grid_sample_q4(
                        &old_grid,px,py,sub_x,sub_y,&old_x,&old_y));
                    CHECK(decimal360_bits(source_x)==decimal360_bits(old_x) &&
                          decimal360_bits(source_y)==decimal360_bits(old_y));
                    ++result.legacy_exact_samples;
                } else {
                    /* The original old grid would address a different
                     * bitmap extent. Do NOT equate differently phased
                     * samples or pass them into native Fill paint. */
                    ++result.size_discrepant_samples;
                }
            }

    const auto refused=[&](const quantapdf_graph_device_grid& grid,
        uint32_t x,uint32_t y,unsigned sx,unsigned sy,
        quantapdf_status expected) {
        double vx=123.0,vy=456.0;
        CHECK(quantapdf_graph_device_grid_sample_q4(
            &grid,x,y,sx,sy,&vx,&vy)==expected &&
            vx==0.0 && vy==0.0);
        ++result.sample_refusals;
    };
    refused(exact,exact.width,0u,0u,0u,QUANTAPDF_ERROR_UNSUPPORTED);
    refused(exact,0u,exact.height,0u,0u,QUANTAPDF_ERROR_UNSUPPORTED);
    refused(exact,0u,0u,4u,0u,QUANTAPDF_ERROR_UNSUPPORTED);
    refused(exact,0u,0u,0u,4u,QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_graph_device_grid corrupt=exact;
    corrupt.stride_bytes+=1u;
    refused(corrupt,0u,0u,0u,0u,QUANTAPDF_ERROR_ARGUMENT);
    corrupt=exact;
    corrupt.from_source[0]=std::nan("");
    refused(corrupt,0u,0u,0u,0u,QUANTAPDF_ERROR_ARGUMENT);
    {
        double alias=789.0;
        CHECK(quantapdf_graph_device_grid_sample_q4(
            &exact,0u,0u,0u,0u,&alias,&alias)==
                QUANTAPDF_ERROR_ARGUMENT &&
              alias==0.0);
        ++result.sample_refusals;
    }
    CHECK(result.valid_samples==80u &&
          result.sample_refusals==7u &&
          result.legacy_exact_samples+
              result.size_discrepant_samples==80u);
    return result;
}
