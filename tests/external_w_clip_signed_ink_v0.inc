/* #269: test-owned signed white-ink proxy for REAL original-source PDFium
 * versus actual pure C11 Native RGBA, never a model pixel predictor.
 *
 * Source colors are opaque red-on-white (green+blue low channels)
 * or opaque blue-on-white (red+green low channels). Average their two
 * low channels to classify COVERAGE-STRENGTH under/over, not to infer
 * PDFium's actual Clip/Fill/stroke-internal sample or tie rule.
 *
 * Bucket full 3-channel RGB absolute errors using that independent
 * coverage-strength sign. The color-only bucket owns residual RGB errors
 * with equal inferred ink strength. No threshold and no epsilon.
 * All sums include every original PDFium pixel and are conserved exactly.
 */
struct qp377_external_signed_ink_v0 {
    size_t compared_pixels=0u,different=0u,exact=0u;
    size_t under=0u,over=0u,color_only=0u,gross=0u;
    uint64_t under_rgb_abs=0u,over_rgb_abs=0u,color_only_rgb_abs=0u;
    uint64_t total_rgb_abs=0u;
    uint64_t original_ink_sum=0u,native_ink_sum=0u;

    void observe(const unsigned char *rgba,const unsigned char *pdfium,
                 bool blue_ink)
    {
        CHECK(rgba && pdfium && compared_pixels<500000u);
        unsigned actual[3]={};
        unsigned worst=0u;
        uint64_t diff_sum=0u;
        for(size_t c=0u;c<3u;++c){
            actual[c]=on_white(rgba,c);
            const unsigned b=pdfium[c];
            const unsigned d=actual[c]>b?actual[c]-b:b-actual[c];
            if(d>worst)worst=d;
            diff_sum+=(uint64_t)d;
        }
        const unsigned c0=blue_ink?0u:1u;
        const unsigned c1=blue_ink?1u:2u;
        const unsigned ref_ink=255u-
            ((unsigned)pdfium[c0]+(unsigned)pdfium[c1]+1u)/2u;
        const unsigned got_ink=255u-
            (actual[c0]+actual[c1]+1u)/2u;
        original_ink_sum+=(uint64_t)ref_ink;
        native_ink_sum+=(uint64_t)got_ink;
        total_rgb_abs+=diff_sum;
        ++compared_pixels;
        if(worst>64u)++gross;
        if(diff_sum==0u){
            ++exact;
            return;
        }
        ++different;
        if(got_ink<ref_ink){
            ++under;
            under_rgb_abs+=diff_sum;
        }else if(got_ink>ref_ink){
            ++over;
            over_rgb_abs+=diff_sum;
        }else{
            ++color_only;
            color_only_rgb_abs+=diff_sum;
        }
    }

    void assert_conserved(size_t expected_pixels,
                          double previous_total,size_t previous_gross) const
    {
        CHECK(compared_pixels==expected_pixels &&
              exact+different==compared_pixels &&
              under+over+color_only==different &&
              under_rgb_abs+over_rgb_abs+color_only_rgb_abs==
                  total_rgb_abs &&
              (double)total_rgb_abs==previous_total &&
              gross==previous_gross &&
              original_ink_sum<=255u*expected_pixels &&
              native_ink_sum<=255u*expected_pixels);
    }
};
static std::string qp377_signed_ink_json_v0(
    const qp377_external_signed_ink_v0& s,const char *color)
{
    CHECK(color && (std::strcmp(color,"red")==0 ||
                    std::strcmp(color,"blue")==0));
    char out[1100]={};
    const int n=std::snprintf(out,sizeof(out),
      "{\"metric\":\"opaque_source_ink_proxy_not_clip_internal_coverage\","
      "\"source_color\":\"%s\","
      "\"compared_pixels\":%zu,\"different_pixels\":%zu,"
      "\"exact_pixels\":%zu,"
      "\"under_pixels\":%zu,\"over_pixels\":%zu,"
      "\"color_only_pixels\":%zu,\"gross_pixels\":%zu,"
      "\"under_rgb_abs\":%llu,\"over_rgb_abs\":%llu,"
      "\"color_only_rgb_abs\":%llu,\"rgb_abs\":%llu,"
      "\"original_ink_sum\":%llu,\"native_ink_sum\":%llu}",
      color,s.compared_pixels,s.different,s.exact,
      s.under,s.over,s.color_only,s.gross,
      (unsigned long long)s.under_rgb_abs,
      (unsigned long long)s.over_rgb_abs,
      (unsigned long long)s.color_only_rgb_abs,
      (unsigned long long)s.total_rgb_abs,
      (unsigned long long)s.original_ink_sum,
      (unsigned long long)s.native_ink_sum);
    CHECK(n>0 && (size_t)n<sizeof(out));
    return std::string(out);
}
