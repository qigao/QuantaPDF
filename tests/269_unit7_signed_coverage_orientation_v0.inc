/* #269: OBSERVATION-ONLY signed native-vs-original RGB/coverage residual.
 * No change to rasterizer, masks, PDFium reference or device Grid.
 * "under"/"over" describe composed red-on-white coverage, not which
 * primitive caused the difference. A signed count is NOT causal proof.
 */
struct qp269_residual_stat_v0 {
    size_t different=0u,gross=0u;
    uint64_t rgb_abs=0u,coverage_abs=0u;
};
static const char *const qp269_signed_names_v0[3]={
    "native_under_original_opaque",
    "native_over_original_clear",
    "other_rgb_channels_only"
};
static const char *const qp269_orientation_names_v0[4]={
    "diagonal_only",
    "vertical_edge",
    "horizontal_edge",
    "both_axes"
};
/* Probe ONLY original-PDFium binary primitive masks. Neighbor differences
 * along the pixel X axis mean a vertical edge; Y axis a horizontal edge.
 * Explicit bounds: no padding, no fabricated off-page transitions. */
static unsigned qp269_original_mask_edge_orientation_v0(
    const std::vector<unsigned char>& fill,
    const std::vector<unsigned char>& clip,
    size_t w,size_t h,size_t x,size_t y)
{
    CHECK(w>0u && h>0u && x<w && y<h &&
          fill.size()==w*h && clip.size()==w*h);
    bool x_transition=false,y_transition=false;
    for(unsigned mask_index=0u;mask_index<2u;++mask_index){
        const auto& mask=mask_index==0u?fill:clip;
        const unsigned char c=mask[y*w+x];
        if(x>0u && mask[y*w+x-1u]!=c)x_transition=true;
        if(x+1u<w && mask[y*w+x+1u]!=c)x_transition=true;
        if(y>0u && mask[(y-1u)*w+x]!=c)y_transition=true;
        if(y+1u<h && mask[(y+1u)*w+x]!=c)y_transition=true;
    }
    return (x_transition?1u:0u)|(y_transition?2u:0u);
}
/* Q4 4x4 sixteen-sample normalized-alpha lattice is a diagnostic
 * hypothesis only. Do not alter alpha values or reject off-lattice results;
 * those are preserved in the full literal 256-bin histogram. */
static bool qp269_alpha_q4_lattice_v0(unsigned char alpha)
{
    for(unsigned k=0u;k<=16u;++k){
        if((k*255u+8u)/16u==(unsigned)alpha)return true;
    }
    return false;
}
