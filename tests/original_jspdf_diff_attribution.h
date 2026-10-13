#ifndef QUANTAPDF_TEST_ORIGINAL_JSPDF_DIFF_ATTRIBUTION_H
#define QUANTAPDF_TEST_ORIGINAL_JSPDF_DIFF_ATTRIBUTION_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

/* Test-only deterministic bounded attribution of FULL original-PDF RGB
 * differences. No renderer/provider call and no image output. A native
 * partial-coverage mask describes the source layers, not PDFium's cause. */
namespace jspdf_pixel_attribution {

struct plane {
    const unsigned char *data = nullptr;
    size_t stride = 0;
    size_t size = 0;
};

struct hotspot {
    size_t x = 0, y = 0;
    unsigned max_delta = 0, rgb_error = 0;
    unsigned stroke_alpha = 0, fill_alpha = 0;
    std::array<unsigned,3> candidate{}, oracle{};
};

struct report {
    bool valid = false;
    std::array<size_t,4> changed{}; // mask 0=flat, 1=stroke, 2=fill, 3=both
    std::array<size_t,4> gross{};
    std::array<std::uint64_t,4> total_rgb_error{};
    size_t flat_near_partial_3x3 = 0;
    size_t flat_far_from_partial_3x3 = 0;
    size_t native_uncovered_reference_foreground = 0;
    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    static constexpr size_t max_hotspots = 12u;
    std::array<hotspot,max_hotspots> worst{};
    size_t worst_count = 0;

    size_t changed_total() const {
        size_t sum=0; for (size_t x:changed) sum+=x; return sum;
    }
    size_t gross_total() const {
        size_t sum=0; for (size_t x:gross) sum+=x; return sum;
    }
    std::uint64_t error_total() const {
        std::uint64_t sum=0; for (auto x:total_rgb_error) sum+=x; return sum;
    }
};

/* Never use a pointer/stride before checking representable byte extents.
 * All four planes are borrowed, no mutable state or allocation proportional
 * to PDF pixel count. */
inline bool valid_plane(plane p,size_t width,size_t height,size_t channels) {
    if (!p.data || width==0u || height==0u ||
        width>std::numeric_limits<size_t>::max()/channels ||
        p.stride<width*channels ||
        height>std::numeric_limits<size_t>::max()/p.stride)
        return false;
    return p.size>=p.stride*height;
}
inline bool partial(unsigned alpha) {
    return alpha>0u && alpha<255u;
}
inline unsigned over_white(const unsigned char *rgba,size_t channel) {
    unsigned a=rgba[3];
    return ((unsigned)rgba[channel]*a+255u*(255u-a)+127u)/255u;
}
inline bool better(hotspot const& a,hotspot const& b) {
    if(a.max_delta!=b.max_delta) return a.max_delta>b.max_delta;
    if(a.rgb_error!=b.rgb_error) return a.rgb_error>b.rgb_error;
    if(a.y!=b.y) return a.y<b.y;
    return a.x<b.x;
}
inline void offer(report& out,const hotspot& item) {
    size_t i=0u;
    while(i<out.worst_count && !better(item,out.worst[i]))++i;
    if(i>=report::max_hotspots) return;
    if(out.worst_count<report::max_hotspots)++out.worst_count;
    for(size_t k=out.worst_count-1u;k>i;--k)
        out.worst[k]=out.worst[k-1u];
    out.worst[i]=item;
}

inline report analyze(
    plane stroke,plane fill,plane candidate,plane oracle,
    size_t width,size_t height)
{
    report out;
    if(!valid_plane(stroke,width,height,4u) ||
       !valid_plane(fill,width,height,4u) ||
       !valid_plane(candidate,width,height,4u) ||
       !valid_plane(oracle,width,height,3u))
        return out;
    out.valid=true;
    out.min_x=width;
    out.min_y=height;
    for(size_t y=0u;y<height;++y) for(size_t x=0u;x<width;++x) {
        const unsigned char *s=stroke.data+y*stroke.stride+x*4u;
        const unsigned char *f=fill.data+y*fill.stride+x*4u;
        const unsigned char *c=candidate.data+y*candidate.stride+x*4u;
        const unsigned char *p=oracle.data+y*oracle.stride+x*3u;
        const unsigned group=(unsigned)partial(s[3])+
                             ((unsigned)partial(f[3])<<1u);
        unsigned error=0u,largest=0u;
        hotspot sample;
        sample.x=x; sample.y=y;
        sample.stroke_alpha=s[3]; sample.fill_alpha=f[3];
        for(size_t channel=0u;channel<3u;++channel) {
            unsigned actual=over_white(c,channel), expected=p[channel];
            unsigned delta=actual>=expected ?
                actual-expected : expected-actual;
            error+=delta;
            largest=std::max(largest,delta);
            sample.candidate[channel]=actual;
            sample.oracle[channel]=expected;
        }
        if(largest==0u)continue;
        ++out.changed[group];
        out.total_rgb_error[group]+=error;
        if(largest>64u)++out.gross[group];
        sample.rgb_error=error;
        sample.max_delta=largest;
        offer(out,sample);
        out.min_x=std::min(out.min_x,x);
        out.min_y=std::min(out.min_y,y);
        out.max_x=std::max(out.max_x,x);
        out.max_y=std::max(out.max_y,y);
        if(s[3]==0u && f[3]==0u &&
           (p[0]<255u || p[1]<255u || p[2]<255u))
            ++out.native_uncovered_reference_foreground;
        if(group==0u) {
            bool adjacent=false;
            for(int dy=-1;dy<=1 && !adjacent;++dy) {
                if((dy<0 && y==0u) || (dy>0 && y+1u==height))continue;
                size_t ny=dy<0?y-1u:(dy>0?y+1u:y);
                for(int dx=-1;dx<=1;++dx) {
                    if((dx<0 && x==0u) ||
                       (dx>0 && x+1u==width) ||
                       (dx==0 && dy==0))continue;
                    size_t nx=dx<0?x-1u:(dx>0?x+1u:x);
                    const unsigned char *ns=
                        stroke.data+ny*stroke.stride+nx*4u;
                    const unsigned char *nf=
                        fill.data+ny*fill.stride+nx*4u;
                    if(partial(ns[3]) || partial(nf[3])) {
                        adjacent=true;break;
                    }
                }
            }
            if(adjacent)++out.flat_near_partial_3x3;
            else ++out.flat_far_from_partial_3x3;
        }
    }
    if(out.changed_total()==0u) {
        out.min_x=out.min_y=out.max_x=out.max_y=0u;
    }
    return out;
}

inline std::string json(const report& r) {
    std::string s="{\"schema_version\":1";
    auto append=[&](const char *name,const std::string &value) {
        s+=",\"";s+=name;s+="\":";s+=value;
    };
    auto array4=[&](auto const& v) {
        std::string a="[";
        for(size_t i=0u;i<4u;++i) {
            if(i)a+=",";
            a+=std::to_string(v[i]);
        }
        return a+"]";
    };
    append("changed_by_partial_mask",array4(r.changed));
    append("gross_by_partial_mask",array4(r.gross));
    append("abs_rgb_error_by_partial_mask",array4(r.total_rgb_error));
    append("flat_changed_adjacent_partial_3x3",
           std::to_string(r.flat_near_partial_3x3));
    append("flat_changed_far_from_partial_3x3",
           std::to_string(r.flat_far_from_partial_3x3));
    append("native_uncovered_reference_foreground",
           std::to_string(r.native_uncovered_reference_foreground));
    append("changed_bounds","["+std::to_string(r.min_x)+","+
           std::to_string(r.min_y)+","+std::to_string(r.max_x)+","+
           std::to_string(r.max_y)+"]");
    std::string top="[";
    for(size_t i=0u;i<r.worst_count;++i) {
        if(i)top+=",";
        const hotspot &h=r.worst[i];
        top+="{\"x\":"+std::to_string(h.x)+
             ",\"y\":"+std::to_string(h.y)+
             ",\"max_delta\":"+std::to_string(h.max_delta)+
             ",\"rgb_error\":"+std::to_string(h.rgb_error)+
             ",\"stroke_alpha\":"+std::to_string(h.stroke_alpha)+
             ",\"fill_alpha\":"+std::to_string(h.fill_alpha)+
             ",\"candidate_rgb\":["+std::to_string(h.candidate[0])+","+
             std::to_string(h.candidate[1])+","+
             std::to_string(h.candidate[2])+"]"+
             ",\"pdfium_rgb\":["+std::to_string(h.oracle[0])+","+
             std::to_string(h.oracle[1])+","+
             std::to_string(h.oracle[2])+"]}";
    }
    append("worst_pixels",top+"]");
    return s+"}";
}
} // namespace jspdf_pixel_attribution
#endif
