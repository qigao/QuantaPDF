#ifndef QUANTAPDF_NATIVE_IMAGE_AFFINE_REFERENCE_H
#define QUANTAPDF_NATIVE_IMAGE_AFFINE_REFERENCE_H
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Original modes 56/75 use [16 1 0 12 10 15]. Keep their original Hello
// masks; derive the sheared image analytically in authored rational points.
inline std::vector<unsigned char> native_image_shear_reference(int mode,unsigned dpi)
{
    const size_t w=(128u*dpi+71u)/72u,h=(64u*dpi+71u)/72u;
    std::ifstream input(std::filesystem::path(__FILE__).parent_path()/"goldens"/
        ("roboto-hello-"+std::to_string(dpi)+"-m2b2i.mask16"),std::ios::binary);
    const std::vector<unsigned char> masks{std::istreambuf_iterator<char>(input),{}};
    if(masks.size()!=w*h*10u)return {};
    static constexpr unsigned char colors[4][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,0}};
    std::vector<unsigned char> out(w*h*4u,255u);
    for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
        unsigned image=0u,fill=0u;const int64_t den=8ll*dpi;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const int64_t px=(8ll*int64_t(x)+2ll*sx+1ll)*72ll;
            const int64_t py=(8ll*int64_t(y)+2ll*sy+1ll)*72ll;
            const int64_t u=px-10ll*den,v=(49ll*den-py)*16ll-u;
            image+=unsigned(u>=0 && u<16ll*den && v>0 && v<=192ll*den);
            fill+=unsigned(px>=12ll*den && px<28ll*den && py>=24ll*den && py<44ll*den);
        }
        const int64_t u=(2ll*int64_t(x)+1ll)*36ll-10ll*dpi;
        const int64_t v=(49ll*dpi-(2ll*int64_t(y)+1ll)*36ll)*16ll-u;
        const size_t col=2ll*u<16ll*dpi?0u:1u,row=2ll*v>192ll*dpi?0u:1u;
        auto *pixel=out.data()+4u*(y*w+x);
        const auto paint=[&](unsigned n,const unsigned char *color) {
            for(size_t c=0u;c<3u;++c)pixel[c]=static_cast<unsigned char>(
                (unsigned(color[c])*n+unsigned(pixel[c])*(16u-n)+8u)/16u);
        };
        const auto text=[&]() {
            static constexpr unsigned char black[3]={0,0,0};
            for(size_t g=0u;g<5u;++g) {
                const size_t at=2u*(g*w*h+y*w+x);
                unsigned mask=unsigned(masks[at])|(unsigned(masks[at+1u])<<8u),hits=0u;
                for(unsigned bit=0u;bit<16u;++bit)hits+=(mask>>bit)&1u;
                paint(hits,black);
            }
        };
        if(mode==56)text();
        paint(image,colors[2u*row+col]);
        if(mode==75) {static constexpr unsigned char green[3]={0,255,0};paint(fill,green);text();}
    }
    return out;
}
#endif
