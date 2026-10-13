#include "original_jspdf_diff_attribution.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); \
    return EXIT_FAILURE; } } while (0)

static int synthetic_attribution()
{
    constexpr size_t side=5u;
    std::array<unsigned char,side*side*4u> stroke{},fill{},candidate{};
    std::array<unsigned char,side*side*3u> oracle{};
    oracle.fill(255u);

    auto rgb=[](auto& plane,size_t x,size_t y,unsigned char r,
                unsigned char g,unsigned char b) {
        size_t off=(y*side+x)*3u;
        plane[off]=r;plane[off+1u]=g;plane[off+2u]=b;
    };
    auto rgba=[](auto& plane,size_t x,size_t y,unsigned char r,
                 unsigned char g,unsigned char b,unsigned char a) {
        size_t off=(y*side+x)*4u;
        plane[off]=r;plane[off+1u]=g;
        plane[off+2u]=b;plane[off+3u]=a;
    };

    // Stroke partial: candidate white-composited is 127 gray.
    rgba(stroke,0u,0u,0u,0u,0u,128u);
    rgba(candidate,0u,0u,0u,0u,0u,128u);
    rgb(oracle,0u,0u,200u,200u,200u);

    // Flat mismatches: one adjacent to native fractional stroke.
    rgb(oracle,1u,0u,254u,254u,254u);
    rgb(oracle,4u,4u,0u,0u,0u);
    rgb(oracle,3u,2u,254u,254u,254u);

    // Fill partial: red over white.
    rgba(fill,0u,4u,255u,0u,0u,128u);
    rgba(candidate,0u,4u,255u,0u,0u,128u);

    // Both native layers fractional; fixed source-over RGBA.
    rgba(stroke,2u,2u,0u,0u,0u,128u);
    rgba(fill,2u,2u,255u,0u,0u,128u);
    rgba(candidate,2u,2u,170u,0u,0u,192u);

    // Fully opaque native stroke vs one-step reference difference.
    rgba(stroke,4u,0u,0u,0u,0u,255u);
    rgba(candidate,4u,0u,0u,0u,0u,255u);
    rgb(oracle,4u,0u,1u,1u,1u);

    using namespace jspdf_pixel_attribution;
    const plane s{stroke.data(),side*4u,stroke.size()};
    const plane f{fill.data(),side*4u,fill.size()};
    const plane c{candidate.data(),side*4u,candidate.size()};
    const plane p{oracle.data(),side*3u,oracle.size()};
    const report a=analyze(s,f,c,p,side,side);
    CHECK(a.valid);
    CHECK(a.changed[0]==4u && a.changed[1]==1u &&
          a.changed[2]==1u && a.changed[3]==1u);
    CHECK(a.changed_total()==7u);
    CHECK(a.gross[0]==1u && a.gross[1]==1u &&
          a.gross[2]==1u && a.gross[3]==1u);
    CHECK(a.gross_total()==4u);
    CHECK(a.flat_near_partial_3x3==2u);
    CHECK(a.flat_far_from_partial_3x3==2u);
    CHECK(a.native_uncovered_reference_foreground==3u);
    CHECK(a.group_bounds[0].seen && a.group_bounds[0].min_x==1u &&
          a.group_bounds[0].min_y==0u &&
          a.group_bounds[0].max_x==4u && a.group_bounds[0].max_y==4u);
    CHECK(a.group_bounds[1].seen && a.group_bounds[1].min_x==0u &&
          a.group_bounds[1].min_y==0u);
    CHECK(a.group_bounds[2].seen && a.group_bounds[2].min_x==0u &&
          a.group_bounds[2].min_y==4u);
    CHECK(a.group_bounds[3].seen && a.group_bounds[3].min_x==2u &&
          a.group_bounds[3].min_y==2u);
    CHECK(a.flat_near_bounds.seen && a.flat_near_bounds.min_x==1u &&
          a.flat_near_bounds.min_y==0u &&
          a.flat_near_bounds.max_x==3u && a.flat_near_bounds.max_y==2u);
    CHECK(a.flat_far_bounds.seen && a.flat_far_bounds.min_x==4u &&
          a.flat_far_bounds.min_y==0u &&
          a.flat_far_bounds.max_x==4u && a.flat_far_bounds.max_y==4u);
    CHECK(a.flat_far_worst_count==2u);
    CHECK(a.flat_far_worst[0].x==4u && a.flat_far_worst[0].y==4u &&
          a.flat_far_worst[0].max_delta==255u);
    CHECK(a.flat_far_worst[1].x==4u && a.flat_far_worst[1].y==0u);
    CHECK(a.min_x==0u && a.min_y==0u);
    CHECK(a.max_x==4u && a.max_y==4u);
    CHECK(a.worst_count==7u);
    CHECK(a.worst[0].x==4u && a.worst[0].y==4u &&
          a.worst[0].max_delta==255u);
    CHECK(a.worst[1].x==2u && a.worst[1].y==2u);
    CHECK(a.worst[2].x==0u && a.worst[2].y==4u);
    CHECK(a.worst[3].x==0u && a.worst[3].y==0u);
    const auto same=analyze(s,f,c,p,side,side);
    CHECK(json(a)==json(same));
    CHECK(json(a).find("\"worst_pixels\":[{")!=std::string::npos);
    CHECK(json(a).find("\"changed_by_partial_mask\":[4,1,1,1]")!=
          std::string::npos);
    CHECK(json(a).find("\"flat_far_changed_bounds\":[4,0,4,4]")!=
          std::string::npos);
    CHECK(json(a).find("\"flat_far_worst_pixels\":[{")!=
          std::string::npos);

    // Fail-closed on incorrect source buffers, strides and bounded extents.
    CHECK(!analyze(plane{nullptr,s.stride,s.size},f,c,p,side,side).valid);
    CHECK(!analyze(plane{s.data,3u,s.size},f,c,p,side,side).valid);
    CHECK(!analyze(s,f,c,plane{p.data,p.stride,p.size-1u},
                   side,side).valid);
    CHECK(!analyze(s,f,c,p,0u,side).valid);
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(synthetic_attribution()==EXIT_SUCCESS);
    std::puts("jsPDF mismatch attribution: bounded full-grid classification PASS");
    return EXIT_SUCCESS;
}
