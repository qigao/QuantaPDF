#ifndef QUANTAPDF_TEST_NATIVE_TTF_FRACTIONAL_REFERENCE_H
#define QUANTAPDF_TEST_NATIVE_TTF_FRACTIONAL_REFERENCE_H

/* TEST ONLY: original-quadratic glyph samples plus analytic authored
 * rectangles. No Source IR, production sampler or captured Bitmap input. */
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

struct native_ttf_fractional_reference {
    size_t width=0u,height=0u,disjoint=0u,overlap=0u,spurious=0u;
    size_t restored_inside=0u,restored_outside=0u;
    size_t restored_over_text=0u,restored_over_fill=0u,restored_over_image=0u;
    size_t sibling_old_only=0u,sibling_new_only=0u;
    size_t sibling_wrong_root=0u,sibling_wrong_intersection=0u;
    std::vector<unsigned char> rgba,rgb;
};

inline native_ttf_fractional_reference native_ttf_fractional_expected(int mode,unsigned dpi)
{
    native_ttf_fractional_reference out;
    if((mode!=83 && mode!=84 && mode!=99 && mode!=100 && mode!=105 && mode!=106 &&
        mode!=113 && mode!=114) ||
       (dpi!=72u && dpi!=96u && dpi!=144u))return out;
    const bool restored=mode==105 || mode==106;
    const bool sibling=mode==113 || mode==114;
    const bool quarter=mode>=99,text_first=mode==83 || mode==99 || restored || sibling;
    out.width=(128u*dpi+71u)/72u;out.height=(64u*dpi+71u)/72u;
    const size_t pixels=out.width*out.height;
    const auto goldens=std::filesystem::path(__FILE__).parent_path()/"goldens";
    std::ifstream masks_file(goldens/("roboto-hello-"+
        std::to_string(dpi)+"-m2b2i.mask16"),std::ios::binary);
    const std::vector<unsigned char> masks{
        std::istreambuf_iterator<char>(masks_file),{}};
    std::ifstream old_file(goldens/("roboto-hello-"+
        std::to_string(dpi)+"-m0.rgba"),std::ios::binary);
    const std::vector<unsigned char> old{
        std::istreambuf_iterator<char>(old_file),{}};
    if(masks.size()!=pixels*10u || old.size()!=pixels*4u)return {};
    const auto count=[](unsigned mask) {
        unsigned n=0u;
        for(unsigned k=0u;k<16u;++k)n+=(mask>>k)&1u;
        return n;
    };
    static const unsigned char samples[4][3]={
        {255u,0u,0u},{0u,255u,0u},{0u,0u,255u},{255u,255u,0u}
    };
    const double scale=(double)dpi/72.0;
    out.rgba.assign(pixels*4u,255u);out.rgb.assign(pixels*3u,255u);
    for(size_t y=0u;y<out.height;++y)
    for(size_t x=0u;x<out.width;++x) {
        const size_t i=y*out.width+x;
        unsigned clip_mask=0u,fill_hits=0u,image_hits=0u,suffix_hits=0u;
        unsigned old_image_hits=0u,intersection_hits=0u;
        for(unsigned sy=0u;sy<4u;++sy)
        for(unsigned sx=0u;sx<4u;++sx) {
            const double px=(double)x+((double)sx+0.5)/4.0;
            const double py=(double)y+((double)sy+0.5)/4.0;
            const bool inside=px>=(quarter?16.25:12.0)*scale &&
                px<(quarter?43.25:28.0)*scale &&
                py>=(quarter?26.25:24.0)*scale &&
                py<(quarter?41.75:44.0)*scale;
            if(inside)clip_mask|=1u<<(4u*sy+sx);
            fill_hits+=(unsigned)(inside && px>=12.0*scale &&
                px<(quarter?19.0:28.0)*scale &&
                py>=(quarter?34.0:24.0)*scale && py<44.0*scale);
            const bool image_rectangle=px>=10.0*scale && px<26.0*scale &&
                py>=37.0*scale && py<49.0*scale;
            const bool image_clip=sibling?
                (px>=20.5*scale && px<38.75*scale && py>=29.75*scale && py<48.25*scale):inside;
            image_hits+=(unsigned)(image_clip && image_rectangle);
            old_image_hits+=(unsigned)(inside && image_rectangle);
            intersection_hits+=(unsigned)(inside && image_clip && image_rectangle);
            if(sibling && image_rectangle) {
                out.sibling_old_only+=(unsigned)(inside && !image_clip);
                out.sibling_new_only+=(unsigned)(image_clip && !inside);
            }
            /* Original Q restores GS0: this rectangle is independent
             * of Clip membership, including samples on either side. */
            if(restored && px>=(mode==105?34.0:18.25)*scale &&
               px<(mode==105?40.0:36.75)*scale &&
               py>=(mode==105?50.0:31.25)*scale &&
               py<(mode==105?55.0:43.75)*scale) {
                ++suffix_hits;
                out.restored_inside+=(unsigned)inside;
                out.restored_outside+=(unsigned)!inside;
            }
        }
        unsigned gray=255u,unclipped=255u;
        for(size_t g=0u;g<5u;++g) {
            const size_t at=2u*(g*pixels+i);
            const unsigned glyph=(unsigned)masks[at]|((unsigned)masks[at+1u]<<8u);
            const unsigned intersection=glyph&clip_mask;
            gray=(gray*(16u-count(intersection))+8u)/16u;
            unclipped=(unclipped*(16u-count(glyph))+8u)/16u;
            if(glyph!=0u && glyph!=65535u && clip_mask!=0u && clip_mask!=65535u) {
                out.disjoint+=(unsigned)(intersection==0u);
                out.overlap+=(unsigned)(intersection!=0u);
            }
        }
        if(old[4u*i]!=unclipped || old[4u*i+1u]!=unclipped ||
           old[4u*i+2u]!=unclipped || old[4u*i+3u]!=255u)return {};
        const unsigned fill_alpha=(fill_hits*255u+8u)/16u;
        const unsigned image_alpha=(image_hits*255u+8u)/16u;
        const unsigned old_image_alpha=(old_image_hits*255u+8u)/16u;
        const unsigned intersection_alpha=(intersection_hits*255u+8u)/16u;
        const unsigned suffix_alpha=(suffix_hits*255u+8u)/16u;
        if(suffix_hits>0u) {
            out.restored_over_text+=(unsigned)(gray<255u && fill_hits==0u && image_hits==0u);
            out.restored_over_fill+=(unsigned)(fill_hits>0u && image_hits==0u);
            out.restored_over_image+=(unsigned)(image_hits>0u);
        }
        const double nx=((double)x+0.5-10.0*scale)/(16.0*scale)*2.0;
        const double ny=((double)y+0.5-37.0*scale)/(12.0*scale)*2.0;
        const size_t ix=nx<=0.0?0u:nx>=2.0?1u:(size_t)nx;
        const size_t iy=ny<=0.0?0u:ny>=2.0?1u:(size_t)ny;
        const auto composite=[&](unsigned text_alpha,unsigned channel,unsigned image_mode=0u) {
            unsigned value=255u;
            for(unsigned layer=0u;layer<3u;++layer) {
                const bool image=text_first?layer==2u:layer==0u;
                const unsigned alpha=layer==1u?fill_alpha:image?
                    (image_mode==1u?old_image_alpha:image_mode==2u?intersection_alpha:image_alpha):text_alpha;
                const unsigned color=layer==1u?(channel==1u?255u:0u):
                    image?(unsigned)samples[2u*iy+ix][channel]:0u;
                value=(color*alpha+value*(255u-alpha)+127u)/255u;
            }
            if(restored)value=((channel==0u?255u:0u)*suffix_alpha+
                value*(255u-suffix_alpha)+127u)/255u;
            return value;
        };
        const unsigned product=((255u-unclipped)*
            ((count(clip_mask)*255u+8u)/16u)+127u)/255u;
        bool different=false,wrong_root=false,wrong_intersection=false;
        for(unsigned channel=0u;channel<3u;++channel) {
            const auto value=(unsigned char)composite(255u-gray,channel);
            out.rgba[4u*i+channel]=value;out.rgb[3u*i+channel]=value;
            different=different || value!=composite(product,channel);
            if(sibling) {
                wrong_root=wrong_root || value!=composite(255u-gray,channel,1u);
                wrong_intersection=wrong_intersection || value!=composite(255u-gray,channel,2u);
            }
        }
        out.spurious+=(unsigned)(gray==255u && product>0u && different);
        out.sibling_wrong_root+=(unsigned)wrong_root;
        out.sibling_wrong_intersection+=(unsigned)wrong_intersection;
    }
    return out;
}
#endif
