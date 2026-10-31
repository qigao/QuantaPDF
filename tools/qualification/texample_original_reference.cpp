/* Offline mathematical reference for the unchanged TeXample original.
 * SHA256 f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284
 *
 * This is NOT a PDF parser, production renderer, SDK consumer or CI target.
 * The frozen table below is the independently transcribed original 32 cubic
 * controls after its literal translation/Y flip, retaining authored decimals.
 * No QuantaPDF code or output is imported. Source binary32 storage is
 * independently checked against this authored-decimal reference. Fill uses long-double polynomial
 * horizontal-ray roots. Stroke isolates ALL quintic stationary-distance roots
 * via Bernstein sign variation, not V1 interval distance, curve flattening,
 * segment distances or a renderer oracle. A second-derivative interpolation
 * bound builds only a conservative query index, never the reference coverage.
 *
 * Build with C++20: output.rgba [72|96|144 [authored|source-binary32
 * [output.masks]]]. Historical output.rgba alone remains authored 72 DPI. The whole-page
 * 4x4 masks are resolved with independent opaque integer source-over in the
 * original orange Fill, black Stroke, black Stroke order on white. Fail on near-boundary
 * numerical ambiguity. No image is obtained from a running QuantaPDF build.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
using Real=long double;
struct Point { Real x,y; };
struct Curve { std::array<Point,4> p; };
static const std::array<std::array<Curve,8>,4> original_authored={{
  {{
    Curve{{{{146.91280L,90.22000L},{146.91280L,58.75548L},{121.68372L,33.52638L},{90.21920L,33.52638L}}}},
    Curve{{{{90.21920L,33.52638L},{58.75466L,33.52638L},{33.52558L,58.75548L},{33.52558L,90.22000L}}}},
    Curve{{{{33.52558L,90.22000L},{33.52558L,121.68452L},{58.75466L,146.91362L},{90.21920L,146.91362L}}}},
    Curve{{{{90.21920L,146.91362L},{121.68372L,146.91362L},{146.91280L,121.68452L},{146.91280L,90.22000L}}}},
    Curve{{{{175.25962L,90.22000L},{175.25962L,43.02320L},{137.41599L,5.17958L},{90.21920L,5.17958L}}}},
    Curve{{{{90.21920L,5.17958L},{43.02239L,5.17958L},{5.17876L,43.02320L},{5.17876L,90.22000L}}}},
    Curve{{{{5.17876L,90.22000L},{5.17876L,137.41680L},{43.02239L,175.26042L},{90.21920L,175.26042L}}}},
    Curve{{{{90.21920L,175.26042L},{137.41599L,175.26042L},{175.25962L,137.41680L},{175.25962L,90.22000L}}}}
  }},
  {{
    Curve{{{{203.60642L,90.22000L},{203.60642L,58.75548L},{178.37734L,33.52638L},{146.91280L,33.52638L}}}},
    Curve{{{{146.91280L,33.52638L},{115.44828L,33.52638L},{90.21920L,58.75548L},{90.21920L,90.22000L}}}},
    Curve{{{{90.21920L,90.22000L},{90.21920L,121.68452L},{115.44828L,146.91362L},{146.91280L,146.91362L}}}},
    Curve{{{{146.91280L,146.91362L},{178.37734L,146.91362L},{203.60642L,121.68452L},{203.60642L,90.22000L}}}},
    Curve{{{{231.95324L,90.22000L},{231.95324L,43.02320L},{194.10961L,5.17958L},{146.91280L,5.17958L}}}},
    Curve{{{{146.91280L,5.17958L},{99.71601L,5.17958L},{61.87238L,43.02320L},{61.87238L,90.22000L}}}},
    Curve{{{{61.87238L,90.22000L},{61.87238L,137.41680L},{99.71601L,175.26042L},{146.91280L,175.26042L}}}},
    Curve{{{{146.91280L,175.26042L},{194.10961L,175.26042L},{231.95324L,137.41680L},{231.95324L,90.22000L}}}}
  }},
  {{
    Curve{{{{146.91280L,90.22000L},{146.91280L,58.75548L},{121.68372L,33.52638L},{90.21920L,33.52638L}}}},
    Curve{{{{90.21920L,33.52638L},{58.75466L,33.52638L},{33.52558L,58.75548L},{33.52558L,90.22000L}}}},
    Curve{{{{33.52558L,90.22000L},{33.52558L,121.68452L},{58.75466L,146.91362L},{90.21920L,146.91362L}}}},
    Curve{{{{90.21920L,146.91362L},{121.68372L,146.91362L},{146.91280L,121.68452L},{146.91280L,90.22000L}}}},
    Curve{{{{175.25962L,90.22000L},{175.25962L,43.02320L},{137.41599L,5.17958L},{90.21920L,5.17958L}}}},
    Curve{{{{90.21920L,5.17958L},{43.02239L,5.17958L},{5.17876L,43.02320L},{5.17876L,90.22000L}}}},
    Curve{{{{5.17876L,90.22000L},{5.17876L,137.41680L},{43.02239L,175.26042L},{90.21920L,175.26042L}}}},
    Curve{{{{90.21920L,175.26042L},{137.41599L,175.26042L},{175.25962L,137.41680L},{175.25962L,90.22000L}}}}
  }},
  {{
    Curve{{{{203.60642L,90.22000L},{203.60642L,58.75548L},{178.37734L,33.52638L},{146.91280L,33.52638L}}}},
    Curve{{{{146.91280L,33.52638L},{115.44828L,33.52638L},{90.21920L,58.75548L},{90.21920L,90.22000L}}}},
    Curve{{{{90.21920L,90.22000L},{90.21920L,121.68452L},{115.44828L,146.91362L},{146.91280L,146.91362L}}}},
    Curve{{{{146.91280L,146.91362L},{178.37734L,146.91362L},{203.60642L,121.68452L},{203.60642L,90.22000L}}}},
    Curve{{{{231.95324L,90.22000L},{231.95324L,43.02320L},{194.10961L,5.17958L},{146.91280L,5.17958L}}}},
    Curve{{{{146.91280L,5.17958L},{99.71601L,5.17958L},{61.87238L,43.02320L},{61.87238L,90.22000L}}}},
    Curve{{{{61.87238L,90.22000L},{61.87238L,137.41680L},{99.71601L,175.26042L},{146.91280L,175.26042L}}}},
    Curve{{{{146.91280L,175.26042L},{194.10961L,175.26042L},{231.95324L,137.41680L},{231.95324L,90.22000L}}}}
  }}
}};
static auto authored=original_authored;
static std::size_t W=238,H=181,A=W*H;
static Real scale=1,stroke_width=0.3985L;
static Real distance_margin=std::numeric_limits<Real>::infinity();
static Real crossing_margin=std::numeric_limits<Real>::infinity();
static Point power_eval(const Curve& c,Real t)
{
    auto axis=[t](Real p0,Real p1,Real p2,Real p3) {
        const Real a=3*(p1-p0),b=3*(p2-2*p1+p0);
        const Real d=p3-3*p2+3*p1-p0;
        return ((d*t+b)*t+a)*t+p0;
    };
    return {axis(c.p[0].x,c.p[1].x,c.p[2].x,c.p[3].x),
            axis(c.p[0].y,c.p[1].y,c.p[2].y,c.p[3].y)};
}
static Real norm2(Point a,Point b)
{
    const Real x=a.x-b.x,y=a.y-b.y;
    return x*x+y*y;
}
static void roots(const Curve& curve,Point query,const std::array<Real,6>& b,
    Real lo,Real hi,unsigned depth,Real& best)
{
    int previous=0,changes=0;
    for(Real x:b) {
        const int sign=x<0?-1:x>0?1:0;
        if(sign) {
            if(previous && sign!=previous)++changes;
            previous=sign;
        }
    }
    if(changes==0)return;
    const Real mid=(lo+hi)/2;
    best=std::min(best,norm2(power_eval(curve,mid),query));
    if(depth==64u || hi-lo<1e-17L)return;
    std::array<Real,6> a=b,left{},right{};
    left[0]=a[0];right[5]=a[5];
    for(unsigned n=5u;n>0u;--n) {
        for(unsigned i=0u;i<n;++i)a[i]=(a[i]+a[i+1u])/2;
        left[6u-n]=a[0];right[n-1u]=a[n-1u];
    }
    roots(curve,query,left,lo,mid,depth+1u,best);
    roots(curve,query,right,mid,hi,depth+1u,best);
}
static Real stationary_distance2(const Curve& c,Point q)
{
    std::array<Real,6> g{},bernstein{};
    const Real px[4]={c.p[0].x-q.x,3*(c.p[1].x-c.p[0].x),
        3*(c.p[2].x-2*c.p[1].x+c.p[0].x),
        c.p[3].x-3*c.p[2].x+3*c.p[1].x-c.p[0].x};
    const Real py[4]={c.p[0].y-q.y,3*(c.p[1].y-c.p[0].y),
        3*(c.p[2].y-2*c.p[1].y+c.p[0].y),
        c.p[3].y-3*c.p[2].y+3*c.p[1].y-c.p[0].y};
    for(unsigned i=0u;i<4u;++i)for(unsigned j=0u;j<3u;++j)
        g[i+j]+=px[i]*(j+1u)*px[j+1u]+py[i]*(j+1u)*py[j+1u];
    const unsigned choose[6][6]={
        {1,0,0,0,0,0},{1,1,0,0,0,0},{1,2,1,0,0,0},
        {1,3,3,1,0,0},{1,4,6,4,1,0},{1,5,10,10,5,1}};
    for(unsigned k=0u;k<6u;++k)for(unsigned i=0u;i<=k;++i)
        bernstein[k]+=g[i]*(Real)choose[k][i]/choose[5][i];
    Real best=std::min(norm2(c.p[0],q),norm2(c.p[3],q));
    roots(c,q,bernstein,0,1,0u,best);
    return best;
}
static std::vector<uint16_t> stroke(const std::array<Curve,8>& path)
{
    std::vector<uint8_t> candidates(A*16u,0u);
    std::vector<uint16_t> mask(A,0u);
    constexpr unsigned N=1024u;
    const Real radius=stroke_width*scale/2;
    for(unsigned k=0u;k<8u;++k) {
        const auto& c=path[k];
        const Real ex=6*std::max(std::fabs(c.p[2].x-2*c.p[1].x+c.p[0].x),
            std::fabs(c.p[3].x-2*c.p[2].x+c.p[1].x))/(8*N*N)+1e-10L;
        const Real ey=6*std::max(std::fabs(c.p[2].y-2*c.p[1].y+c.p[0].y),
            std::fabs(c.p[3].y-2*c.p[2].y+c.p[1].y))/(8*N*N)+1e-10L;
        Point a=c.p[0];
        for(unsigned j=1u;j<=N;++j) {
            const Point b=power_eval(c,(Real)j/N);
            const Real xl=std::min(a.x,b.x)-radius-ex,xh=std::max(a.x,b.x)+radius+ex;
            const Real yl=std::min(a.y,b.y)-radius-ey,yh=std::max(a.y,b.y)+radius+ey;
            const int x0=std::max(0,(int)std::floor(xl)),x1=std::min((int)W,(int)std::ceil(xh));
            const int y0=std::max(0,(int)std::floor(yl)),y1=std::min((int)H,(int)std::ceil(yh));
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)
            for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx) {
                const Real px=x+((Real)sx+0.5L)/4,py=y+((Real)sy+0.5L)/4;
                if(px>=xl && px<=xh && py>=yl && py<=yh)
                    candidates[((std::size_t)y*W+(std::size_t)x)*16u+4u*sy+sx]|=(uint8_t)(1u<<k);
            }
            a=b;
        }
    }
    for(std::size_t y=0;y<H;++y)for(std::size_t x=0;x<W;++x)
    for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx) {
        const Point p={(Real)x+((Real)sx+0.5L)/4,(Real)y+((Real)sy+0.5L)/4};
        if(p.x>=237133.0L/1000*scale || p.y>=180440.0L/1000*scale)continue;
        const auto bits=candidates[(y*W+x)*16u+4u*sy+sx];
        bool hit=false;
        for(unsigned k=0u;k<8u;++k)if(bits&(1u<<k)) {
            const Real d=stationary_distance2(path[k],p)-radius*radius;
            distance_margin=std::min(distance_margin,std::fabs(d));
            if(std::fabs(d)<1e-10L)std::exit(5);
            hit=hit || d<0;
        }
        if(hit)mask[y*W+x]|=(uint16_t)(1u<<(4u*sy+sx));
    }
    return mask;
}
static std::vector<uint16_t> clipped_fill()
{
    std::vector<uint16_t> mask(A,0u);
    for(std::size_t y=0u;y<H;++y)for(unsigned sy=0;sy<4;++sy) {
        const Real py=(Real)y+((Real)sy+0.5L)/4;
        std::vector<Real> crossings[2];
        if(py>=180440.0L/1000*scale)continue;
        for(unsigned g=0;g<2;++g)for(const auto& curve:authored[g]) {
            const Real a=curve.p[0].y,b=curve.p[3].y;
            if(!((a<=py && b>py) || (b<=py && a>py)))continue;
            Real lo=0,hi=1;
            for(unsigned k=0;k<90;++k) {
                const Real mid=(lo+hi)/2;
                const Real cy=power_eval(curve,mid).y;
                if((cy<py)==(b>a))lo=mid;else hi=mid;
            }
            const Real cx=power_eval(curve,(lo+hi)/2).x;
            const Real sample=std::round(4*cx-0.5L)/4+0.125L;
            const Real margin=std::fabs(cx-sample);
            crossing_margin=std::min(crossing_margin,margin);
            if(margin<1e-10L)std::exit(6);
            crossings[g].push_back(cx);
        }
        for(std::size_t x=0u;x<W;++x)for(unsigned sx=0;sx<4;++sx) {
            const Real px=(Real)x+((Real)sx+0.5L)/4;
            if(px>=237133.0L/1000*scale)continue;
            unsigned parity[2]={0u,0u};
            for(unsigned g=0;g<2;++g)for(Real cx:crossings[g])parity[g]^=(unsigned)(cx>px);
            if(parity[0] && parity[1])mask[y*W+x]|=(uint16_t)(1u<<(4u*sy+sx));
        }
    }
    return mask;
}
int main(int argc,char **argv)
{
    if(argc<2 || argc>5)return 2;
    unsigned dpi=72u;
    if(argc>=3) {
        if(std::strcmp(argv[2],"72")==0)dpi=72u;
        else if(std::strcmp(argv[2],"96")==0)dpi=96u;
        else if(std::strcmp(argv[2],"144")==0)dpi=144u;
        else return 2;
    }
    bool binary=false;
    if(argc>=4) {
        binary=std::strcmp(argv[3],"source-binary32")==0;
        if(!binary && std::strcmp(argv[3],"authored")!=0)return 2;
    }
    scale=(Real)dpi/72;
    /* Exact rational ceil independently determines storage. Geometry
     * uses DPI/72 throughout and never fits to that integer buffer. */
    W=(237133u*dpi+71999u)/72000u;
    H=(180440u*dpi+71999u)/72000u;A=W*H;
    if(binary)stroke_width=(Real)(float)stroke_width;
    for(auto& path:authored)for(auto& c:path)for(auto& p:c.p) {
        if(binary) {p.x=(Real)(float)p.x;p.y=(Real)(float)p.y;}
        p.x*=scale;p.y*=scale;
    }
    const std::array<std::vector<uint16_t>,3> masks={
        clipped_fill(),stroke(authored[2]),stroke(authored[3])};
    if(argc==5) {
        auto *mask_file=std::fopen(argv[4],"wb");
        if(!mask_file)return 7;
        bool ok=true;
        for(const auto& mask:masks)for(uint16_t sample:mask) {
            const unsigned char bytes[2]={(unsigned char)(sample&255u),
                                         (unsigned char)(sample>>8u)};
            if(std::fwrite(bytes,1u,2u,mask_file)!=2u)ok=false;
        }
        if(std::fclose(mask_file)!=0 || !ok)return 8;
    }
    std::vector<unsigned char> rgba(A*4u,255u);
    const unsigned rgb[3][3]={{255u,128u,0u},{0u,0u,0u},{0u,0u,0u}};
    for(std::size_t layer=0u;layer<3u;++layer)for(std::size_t i=0u;i<A;++i) {
        unsigned n=0u;
        for(unsigned k=0u;k<16u;++k)n+=(masks[layer][i]>>k)&1u;
        for(unsigned c=0u;c<3u;++c)
            rgba[4u*i+c]=(unsigned char)((rgb[layer][c]*n+rgba[4u*i+c]*(16u-n)+8u)/16u);
    }
    auto *f=std::fopen(argv[1],"wb");
    if(!f)return 3;
    const bool saved=std::fwrite(rgba.data(),1u,rgba.size(),f)==rgba.size();
    if(std::fclose(f)!=0 || !saved)return 4;
    std::printf("Independent stationary-root reference: %zu bytes, minimum squared-distance margin %.12Lg, minimum ray/sample margin %.12Lg\n",
                rgba.size(),distance_margin,crossing_margin);
    return 0;
}
