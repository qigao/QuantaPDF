#ifndef QUANTAPDF_NATIVE_TTF_GRAPH_REFERENCE_H
#define QUANTAPDF_NATIVE_TTF_GRAPH_REFERENCE_H
#include "native_ttf_fractional_reference.h"

// Former profile refusals, now ordinary authored Clip graphs. Curved leaves
// and translated Text use separately authored geometric equivalents; all
// other originals use the original five glyph sample planes and rational
// polygon/image geometry. Neither reference reads Source IR or Bitmap bytes.
inline bool native_ttf_graph_equivalent(int mode)
{
    return mode==131 || mode==140 || mode==155 || mode==162;
}
inline int native_ttf_graph_alias(int mode)
{
    switch(mode) {
    case 86:return 83;
    case 102:return 99;
    case 108:return 106;
    case 118:case 125:return 113;
    case 137:case 141:case 142:return 126;
    case 152:case 159:case 164:return 147;
    default:return 0;
    }
}
inline bool native_ttf_graph_supported(int mode)
{
    if(native_ttf_graph_alias(mode)!=0 || native_ttf_graph_equivalent(mode))return true;
    switch(mode) {
    case 117:case 119:case 120:case 121:case 122:
    case 132:case 133:case 134:case 136:case 139:
    case 151:case 153:case 154:case 156:case 161:
    case 163:case 165:case 166:case 167:return true;
    default:return false;
    }
}
inline native_ttf_fractional_reference native_ttf_graph_compute(int mode,unsigned dpi)
{
    if(const int alias=native_ttf_graph_alias(mode))return native_ttf_fractional_expected(alias,dpi);
    if(!native_ttf_graph_supported(mode) || native_ttf_graph_equivalent(mode) ||
       (dpi!=72u && dpi!=96u && dpi!=144u))return {};
    enum kind {text,fill,image,red};
    struct paint {kind type;unsigned clip;int dx=0;};
    std::vector<paint> paints;
    switch(mode) {
    case 117:case 134:paints={{text,1},{fill,1},{image,3}};break;
    case 119:paints={{text,1},{fill,1},{image,2},{text,2}};break;
    case 120:paints={{text,1},{fill,1},{image,2},{fill,2}};break;
    case 121:paints={{text,1},{fill,1},{image,2,1}};break;
    case 122:paints={{text,1},{fill,1}};break;
    case 132:paints={{text,3},{fill,3},{image,3},{fill,1}};break;
    case 133:paints={{text,3},{fill,3},{image,3},{red,0}};break;
    case 136:paints={{image,3},{fill,3},{text,3}};break;
    case 139:paints={{text,3},{fill,3},{image,3}};break;
    case 151:paints={{image,1},{fill,1},{text,3}};break;
    case 153:paints={{image,1},{fill,1},{text,2},{image,2}};break;
    case 154:paints={{image,1},{fill,1},{text,2},{fill,2}};break;
    case 156:paints={{image,1},{fill,1}};break;
    case 161:paints={{image,1},{fill,1},{text,2}};break;
    case 163:paints={{image,1},{text,2}};break;
    case 165:paints={{image,1},{fill,1},{text,2},{text,2}};break;
    case 166:paints={{fill,1},{image,1},{text,2}};break;
    case 167:paints={{image,1},{fill,1},{text,0}};break;
    default:return {};
    }
    native_ttf_fractional_reference out;
    out.width=(128u*dpi+71u)/72u;out.height=(64u*dpi+71u)/72u;
    const size_t pixels=out.width*out.height;
    const auto goldens=std::filesystem::path(__FILE__).parent_path()/"goldens";
    std::ifstream mf(goldens/("roboto-hello-"+std::to_string(dpi)+"-m2b2i.mask16"),std::ios::binary);
    const std::vector<unsigned char> masks{std::istreambuf_iterator<char>(mf),{}};
    std::ifstream gf(goldens/("roboto-hello-"+std::to_string(dpi)+"-m0.rgba"),std::ios::binary);
    const std::vector<unsigned char> golden{std::istreambuf_iterator<char>(gf),{}};
    if(masks.size()!=pixels*10u || golden.size()!=pixels*4u)return {};
    const auto count=[](unsigned mask) {unsigned n=0;for(unsigned k=0;k<16u;++k)n+=(mask>>k)&1u;return n;};
    const int64_t den=8ll*dpi;
    const auto rect=[&](int64_t x,int64_t y,int x0,int y0,int x1,int y1) {
        return 4ll*x>=x0*den && 4ll*x<x1*den && 4ll*y>y0*den && 4ll*y<=y1*den;
    };
    const unsigned char colors[4][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,0}};
    out.rgb.assign(pixels*3u,255u);out.rgba.assign(pixels*4u,255u);
    for(size_t y=0;y<out.height;++y)for(size_t x=0;x<out.width;++x) {
        const size_t at=y*out.width+x;
        unsigned clip[4]={65535u,0u,0u,0u};
        unsigned coverage[4][3]{};
        for(unsigned sy=0;sy<4u;++sy)for(unsigned sx=0;sx<4u;++sx) {
            const int64_t rx=(8ll*int64_t(x)+2ll*sx+1ll)*72ll;
            const int64_t ry=64ll*den-(8ll*int64_t(y)+2ll*sy+1ll)*72ll;
            const bool a=rect(rx,ry,65,89,173,151);
            bool b=rect(rx,ry,mode==121?86:82,63,mode==121?159:155,137);
            if(mode==139 || mode==161)b=b && 37ll*(4ll*rx-155ll*den)+2ll*(4ll*ry-63ll*den)<0;
            const bool in[4]={true,a,b,a&&b};
            const bool shape[3]={rect(rx,ry,48,80,76,120),
                rect(rx,ry,mode==121?44:40,60,mode==121?108:104,108),
                rect(rx,ry,136,36,160,56)};
            const unsigned bit=1u<<(4u*sy+sx);
            for(unsigned c=1;c<4u;++c)if(in[c])clip[c]|=bit;
            for(unsigned s=0;s<3u;++s) {
                for(unsigned c=0;c<4u;++c)coverage[c][s]+=unsigned(in[c] && shape[s]);
            }
        }
        unsigned glyphs[5]{},unclipped=255u;
        for(size_t g=0;g<5u;++g) {
            const size_t i=2u*(g*pixels+at);glyphs[g]=unsigned(masks[i])|(unsigned(masks[i+1u])<<8u);
            unclipped=(unclipped*(16u-count(glyphs[g]))+8u)/16u;
        }
        for(unsigned c=0;c<3u;++c)if(golden[4u*at+c]!=unclipped)return {};
        if(golden[4u*at+3u]!=255u)return {};
        for(unsigned channel=0;channel<3u;++channel) {
            unsigned value=255u;
            for(const auto& p:paints) {
                if(p.type==text) {
                    for(unsigned glyph:glyphs)value=(value*(16u-count(glyph&clip[p.clip]))+8u)/16u;
                } else {
                    const unsigned slot=p.type==fill?0u:p.type==image?1u:2u;
                    const unsigned n=coverage[p.clip][slot];
                    unsigned color=p.type==fill?(channel==1u?255u:0u):(channel==0u?255u:0u);
                    if(p.type==image) {
                        // Pixel-center nearest-neighbour in the authored RAW 2x2.
                        const int64_t cx=(2ll*int64_t(x)+1ll)*72ll;
                        const int64_t cy=(2ll*int64_t(y)+1ll)*72ll;
                        const size_t ix=cx>=(18ll+p.dx)*2ll*dpi?1u:0u;
                        const size_t iy=cy>=43ll*2ll*dpi?1u:0u;
                        color=colors[2u*iy+ix][channel];
                    }
                    value=(color*n+value*(16u-n)+8u)/16u;
                }
            }
            out.rgb[3u*at+channel]=out.rgba[4u*at+channel]=static_cast<unsigned char>(value);
        }
    }
    return out;
}
inline native_ttf_fractional_reference native_ttf_graph_expected(int mode,unsigned dpi)
{
    static thread_local std::array<std::array<native_ttf_fractional_reference,3>,170> values{};
    if(mode<0 || mode>=170 || (dpi!=72u && dpi!=96u && dpi!=144u))return {};
    auto& value=values[size_t(mode)][dpi==72u?0u:dpi==96u?1u:2u];
    if(value.rgb.empty())value=native_ttf_graph_compute(mode,dpi);
    return value;
}
#endif
