#ifndef QUANTAPDF_TEST_NATIVE_TTF_FRACTIONAL_REFERENCE_H
#define QUANTAPDF_TEST_NATIVE_TTF_FRACTIONAL_REFERENCE_H

/* TEST ONLY: original-quadratic glyph samples plus analytic authored
 * rectangles. No Source IR, production sampler or captured Bitmap input. */
#include <array>
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
    size_t reverse_old_only=0u,reverse_new_only=0u;
    size_t reverse_wrong_text_root=0u,reverse_wrong_image_fill_root=0u;
    size_t reverse_wrong_intersection=0u;
    size_t reverse_text_samples=0u,reverse_fill_samples=0u,reverse_image_samples=0u;
    size_t compound_drop_first=0u,compound_drop_second=0u;
    size_t compound_product_wrong=0u,compound_product_spurious=0u;
    size_t compound_text_samples=0u,compound_fill_samples=0u,compound_image_samples=0u;
    std::vector<unsigned char> rgba,rgb;
};

inline native_ttf_fractional_reference native_ttf_fractional_compute(int mode,unsigned dpi)
{
    native_ttf_fractional_reference out;
    if((mode!=83 && mode!=84 && mode!=99 && mode!=100 && mode!=105 && mode!=106 &&
        mode!=111 && mode!=112 && mode!=113 && mode!=114 && mode!=126 && mode!=127 && mode!=144 && mode!=145 && mode!=146 &&
        mode!=147 && mode!=148 && mode!=168 && mode!=169) ||
       (dpi!=72u && dpi!=96u && dpi!=144u))return out;
    const bool restored=mode==105 || mode==106 || mode==111 || mode==112;
    const bool reverse=mode==147 || mode==148 || mode==168 || mode==169;
    const bool reverse_crossing=mode==168 || mode==169;
    const bool sibling=mode==113 || mode==114;
    const bool compound=mode==126 || mode==127 || mode==144 || mode==145 || mode==146;
    const bool quarter=mode>=99,text_first=mode==83 || mode==99 || (restored && mode!=112) || sibling || compound;
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
        unsigned clip_mask=0u,fill_hits=0u,image_hits=0u,suffix_hits=0u,extra_hits=0u;
        unsigned old_image_hits=0u,intersection_hits=0u,intersection_fill_hits=0u;
        unsigned leaf_masks[2]={0u,0u},leaf_fill[2]={0u,0u},leaf_image[2]={0u,0u};
        for(unsigned sy=0u;sy<4u;++sy)
        for(unsigned sx=0u;sx<4u;++sx) {
            const double px=(double)x+((double)sx+0.5)/4.0;
            const double py=(double)y+((double)sy+0.5)/4.0;
            if(mode==111 && px>=scale && px<3.0*scale && py>=61.0*scale && py<63.0*scale)++extra_hits;
            const bool first_inside=px>=(reverse_crossing?15.5:quarter?16.25:12.0)*scale &&
                px<(reverse_crossing?38.75:quarter?43.25:28.0)*scale &&
                py>=(reverse_crossing?29.75:quarter?26.25:24.0)*scale &&
                py<(reverse_crossing?48.25:quarter?41.75:44.0)*scale;
            const bool second_inside=px>=(reverse_crossing?16.25:
                (mode==145 || mode==146)?15.5:20.5)*scale && px<(reverse_crossing?43.25:38.75)*scale &&
                py>=(reverse_crossing?26.25:29.75)*scale && py<(reverse_crossing?41.75:48.25)*scale;
            const bool inside=reverse?second_inside:first_inside && (!compound || second_inside);
            if(inside)clip_mask|=1u<<(4u*sy+sx);
            fill_hits+=(unsigned)((reverse?first_inside:inside) && px>=12.0*scale &&
                px<(quarter?19.0:28.0)*scale &&
                py>=(quarter?34.0:24.0)*scale && py<44.0*scale);
            const bool image_rectangle=px>=10.0*scale && px<26.0*scale &&
                py>=37.0*scale && py<49.0*scale;
            if(compound || reverse)for(unsigned leaf=0u;leaf<2u;++leaf) {
                const bool in_leaf=leaf==0u?first_inside:second_inside;
                if(in_leaf)leaf_masks[leaf]|=1u<<(4u*sy+sx);
                leaf_fill[leaf]+=(unsigned)(in_leaf && px>=12.0*scale && px<19.0*scale &&
                    py>=34.0*scale && py<44.0*scale);
                leaf_image[leaf]+=(unsigned)(in_leaf && image_rectangle);
            }
            const bool image_clip=reverse?first_inside:sibling?
                (px>=20.5*scale && px<38.75*scale && py>=29.75*scale && py<48.25*scale):inside;
            image_hits+=(unsigned)(image_clip && image_rectangle);
            old_image_hits+=(unsigned)(inside && image_rectangle);
            intersection_hits+=(unsigned)(inside && image_clip && image_rectangle);
            if(reverse)intersection_fill_hits+=(unsigned)(first_inside && second_inside &&
                px>=12.0*scale && px<19.0*scale && py>=34.0*scale && py<44.0*scale);
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
            if(compound || reverse) {
                if(compound)out.compound_text_samples+=count(intersection);
            }
            if(reverse) {
                out.reverse_text_samples+=count(intersection);
                out.reverse_old_only+=count(glyph&leaf_masks[0]&~leaf_masks[1]);
                out.reverse_new_only+=count(glyph&leaf_masks[1]&~leaf_masks[0]);
            }
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
        if(compound) {out.compound_fill_samples+=fill_hits;out.compound_image_samples+=image_hits;}
        if(reverse) {out.reverse_fill_samples+=fill_hits;out.reverse_image_samples+=image_hits;}
        if(suffix_hits>0u) {
            out.restored_over_text+=(unsigned)(gray<255u && fill_hits==0u && image_hits==0u);
            out.restored_over_fill+=(unsigned)(fill_hits>0u && image_hits==0u);
            out.restored_over_image+=(unsigned)(image_hits>0u);
        }
        const double nx=((double)x+0.5-10.0*scale)/(16.0*scale)*2.0;
        const double ny=((double)y+0.5-37.0*scale)/(12.0*scale)*2.0;
        const size_t ix=nx<=0.0?0u:nx>=2.0?1u:(size_t)nx;
        const size_t iy=ny<=0.0?0u:ny>=2.0?1u:(size_t)ny;
        const auto composite=[&](unsigned text_alpha,unsigned channel,unsigned image_mode=0u,unsigned fill_mode=0u) {
            unsigned value=255u;
            for(unsigned layer=0u;layer<3u;++layer) {
                const bool image=text_first?layer==2u:layer==0u;
                const unsigned alpha=layer==1u?(fill_mode==1u?(leaf_fill[1]*255u+8u)/16u:
                    fill_mode==2u?(intersection_fill_hits*255u+8u)/16u:fill_alpha):image?
                    (image_mode==1u?old_image_alpha:image_mode==2u?intersection_alpha:image_alpha):text_alpha;
                const unsigned color=layer==1u?(channel==1u?255u:0u):
                    image?(unsigned)samples[2u*iy+ix][channel]:0u;
                value=(color*alpha+value*(255u-alpha)+127u)/255u;
            }
            if(restored)value=((channel==0u?255u:0u)*suffix_alpha+
                value*(255u-suffix_alpha)+127u)/255u;
            return value;
        };
        const auto ordered_composite=[&](unsigned channel,unsigned text_mode=0u,unsigned image_mode=0u,unsigned fill_mode=0u) {
            const unsigned text_clip=text_mode==1u?leaf_masks[0]:text_mode==2u?(leaf_masks[0]&leaf_masks[1]):text_mode>=3u?leaf_masks[text_mode-3u]:clip_mask;
            unsigned value=255u;
            for(unsigned layer=0u;layer<3u;++layer) {
                const bool image=text_first?layer==2u:layer==0u;
                if(layer!=1u && !image) {
                    if(mode==144)continue;
                    for(size_t g=0u;g<5u;++g) {
                        const size_t at=2u*(g*pixels+i);
                        const unsigned glyph=(unsigned)masks[at]|((unsigned)masks[at+1u]<<8u);
                        value=(value*(16u-count(glyph&text_clip))+8u)/16u;
                    }
                } else {
                    const unsigned hits=image?(image_mode==1u?old_image_hits:image_mode==2u?intersection_hits:image_mode>=3u?leaf_image[image_mode-3u]:image_hits):
                        (fill_mode==1u?leaf_fill[1]:fill_mode==2u?intersection_fill_hits:fill_mode>=3u?leaf_fill[fill_mode-3u]:fill_hits);
                    const unsigned color=image?(unsigned)samples[2u*iy+ix][channel]:(channel==1u?255u:0u);
                    value=(color*hits+value*(16u-hits)+8u)/16u;
                }
            }
            if(restored)value=((channel==0u?255u:0u)*suffix_hits+value*(16u-suffix_hits)+8u)/16u;
            if(mode==111)value=((channel==2u?255u:0u)*extra_hits+value*(16u-extra_hits)+8u)/16u;
            return value;
        };
        const unsigned product=((255u-unclipped)*
            ((count(clip_mask)*255u+8u)/16u)+127u)/255u;
        unsigned triple_product=255u-unclipped;
        if(compound)for(unsigned leaf=0u;leaf<2u;++leaf)
            triple_product=(triple_product*((count(leaf_masks[leaf])*255u+8u)/16u)+127u)/255u;
        bool different=false,wrong_root=false,wrong_intersection=false;
        bool reverse_text_wrong=false,reverse_early_wrong=false,reverse_intersection_wrong=false;
        bool dropped[2]={false,false};
        bool triple_wrong=false;
        for(unsigned channel=0u;channel<3u;++channel) {
            const auto value=(unsigned char)ordered_composite(channel);
            out.rgba[4u*i+channel]=value;out.rgb[3u*i+channel]=value;
            different=different || value!=composite(product,channel);
            if(compound)triple_wrong=triple_wrong || value!=composite(triple_product,channel);
            if(sibling) {
                wrong_root=wrong_root || value!=ordered_composite(channel,0u,1u);
                wrong_intersection=wrong_intersection || value!=ordered_composite(channel,0u,2u);
            }
            if(reverse) {
                reverse_text_wrong=reverse_text_wrong || value!=ordered_composite(channel,1u);
                reverse_early_wrong=reverse_early_wrong || value!=ordered_composite(channel,0u,1u,1u);
                reverse_intersection_wrong=reverse_intersection_wrong ||
                    value!=ordered_composite(channel,2u,2u,2u);
            }
            if(compound)for(unsigned leaf=0u;leaf<2u;++leaf) {
                const unsigned wrong=ordered_composite(channel,leaf+3u,leaf+3u,leaf+3u);
                dropped[leaf]=dropped[leaf] || value!=wrong;
            }
        }
        out.spurious+=(unsigned)(gray==255u && product>0u && different);
        out.sibling_wrong_root+=(unsigned)wrong_root;
        out.sibling_wrong_intersection+=(unsigned)wrong_intersection;
        out.reverse_wrong_text_root+=(unsigned)reverse_text_wrong;
        out.reverse_wrong_image_fill_root+=(unsigned)reverse_early_wrong;
        out.reverse_wrong_intersection+=(unsigned)reverse_intersection_wrong;
        out.compound_drop_first+=(unsigned)dropped[1];
        out.compound_drop_second+=(unsigned)dropped[0];
        out.compound_product_wrong+=(unsigned)triple_wrong;
        out.compound_product_spurious+=(unsigned)(triple_wrong && gray==255u && triple_product>0u);
    }
    return out;
}
/* Immutable TEST reference values only: independent geometry and original
 * glyph assets do not depend on a Source/Context/IR. Repeated fault/quota
 * cases reuse their computation and receive detached value copies. RAW and
 * Flate originals that only swap rectangle W/W* denote the same sample set.
 * No production owner, allocation-fault result or rendered byte is cached. */
inline native_ttf_fractional_reference native_ttf_fractional_expected(int mode,unsigned dpi)
{
    if(dpi!=72u && dpi!=96u && dpi!=144u)return {};
    if(mode==114)mode=113;
    if(mode==127)mode=126;
    if(mode==146)mode=145;
    if(mode==148)mode=147;
    if(mode==169)mode=168;
    static constexpr int profiles[]={83,84,99,100,105,106,111,112,113,126,144,145,147,168};
    size_t profile=0u;
    while(profile<std::size(profiles) && profiles[profile]!=mode)++profile;
    if(profile==std::size(profiles))return {};
    static thread_local std::array<std::array<native_ttf_fractional_reference,3>,std::size(profiles)> values{};
    auto& value=values[profile][dpi==72u?0u:dpi==96u?1u:2u];
    if(value.rgba.empty())value=native_ttf_fractional_compute(mode,dpi);
    return value;
}
#endif
