#ifndef QUANTAPDF_NATIVE_PAGE_TEXT_FIXTURE_H
#define QUANTAPDF_NATIVE_PAGE_TEXT_FIXTURE_H
#include "native_ttf_fixture.h"
#include <algorithm>

namespace native_page_text {
inline std::vector<unsigned char> hello(unsigned dpi,unsigned original_turn=0u)
{
    const auto path=std::filesystem::path(__FILE__).parent_path()/"goldens"/
        ("roboto-hello-"+std::to_string(dpi)+"-m0"+(original_turn?"-rotate"+std::to_string(original_turn):"")+
         (original_turn?".rgba.z":".rgba"));
    std::ifstream file(path,std::ios::binary);
    const std::vector<unsigned char> encoded{std::istreambuf_iterator<char>(file),{}};
    const size_t pixels=((128u*dpi+71u)/72u)*((64u*dpi+71u)/72u);
    std::vector<unsigned char> rgba;
    if(original_turn!=0u) {
        rgba.resize(4u*pixels);uLongf size=static_cast<uLongf>(rgba.size());
        if(uncompress(rgba.data(),&size,encoded.data(),static_cast<uLong>(encoded.size()))!=Z_OK || size!=rgba.size())
            throw std::runtime_error("invalid compressed independent Hello reference");
    } else rgba=encoded;
    if(rgba.size()!=4u*pixels)throw std::runtime_error("missing independent Hello page reference");
    std::vector<unsigned char> rgb(3u*pixels);
    for(size_t i=0u;i<pixels;++i) {
        if(rgba[4u*i+3u]!=255u)throw std::runtime_error("nonopaque independent Hello reference");
        std::copy_n(rgba.data()+4u*i,3u,rgb.data()+3u*i);
    }
    return rgb;
}
inline std::vector<unsigned char> turn(const std::vector<unsigned char>& rgb,size_t w,size_t h,unsigned rotation)
{
    std::vector<unsigned char> out(rgb.size());
    const size_t width=rotation==90u || rotation==270u?h:w;
    for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
        const size_t dx=rotation==90u?h-1u-y:rotation==180u?w-1u-x:rotation==270u?y:x;
        const size_t dy=rotation==90u?x:rotation==180u?h-1u-y:rotation==270u?w-1u-x:y;
        std::copy_n(rgb.data()+3u*(y*w+x),3u,out.data()+3u*(dy*width+dx));
    }
    return out;
}
struct original {
    std::string bytes;
    std::vector<unsigned char> expected;
    unsigned dpi=0u;
    size_t width=0u,height=0u;
    float width_points=0.0f,height_points=0.0f;
};
inline original create(unsigned kind,int rotation,bool crop,bool encrypted)
{
    // Every effective DPI is an existing independent quadratic reference.
    // Crop edges are whole device pixels at that effective DPI. Original
    // quarter turns have their own original-quadratic sample references.
    static const unsigned units[7]={1u,2u,3u,6u,8u,12u,16u};
    static const unsigned dpis[7]={288u,144u,96u,96u,72u,48u,36u};
    static const unsigned reference_dpis[7]={72u,72u,72u,144u,144u,144u,144u};
    using O=QPDFObjectHandle;
    auto pdf=QPDF::create();const auto base=native_ttf_fixture();
    pdf->processMemoryFile("curved page text original",base.data(),base.size());
    auto root=pdf->getRoot().getKey("/Pages"),page=root.getKey("/Kids").getArrayItem(0);
    auto geometry=crop?root:page;
    geometry.replaceKey("/MediaBox",O::parse(crop?"[-4.5 2.25 123.5 66.25]":"[3.25 -2.5 131.25 61.5]"));
    geometry.replaceKey("/Rotate",O::newInteger(rotation));
    if(crop) {
        page.removeKey("/MediaBox");geometry.replaceKey("/CropBox",O::parse("[15.5 26.25 75.5 46.25]"));
        root.replaceKey("/Resources",page.getKey("/Resources"));page.removeKey("/Resources");
    }
    page.replaceKey("/UserUnit",O::parse(std::to_string(double(units[kind])/4.0)));
    const auto contents=page.getKey("/Contents").getStreamData();
    const std::string text(reinterpret_cast<const char*>(contents->getBuffer()),contents->getSize());
    page.replaceKey("/Contents",pdf->newStream(std::string("q 1 0 0 1 ")+(crop?"-4.5 2.25":"3.25 -2.5")+" cm "+text+" Q"));
    QPDFWriter writer(*pdf);writer.setOutputMemory();
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto bytes=writer.getBufferSharedPointer();
    original out;out.bytes.assign(reinterpret_cast<const char*>(bytes->getBuffer()),bytes->getSize());out.dpi=dpis[kind];
    const unsigned ref=reference_dpis[kind],turns=unsigned((rotation%360+360)%360);
    const bool swap=turns==90u || turns==270u;
    const size_t scale=ref/72u,sw=(swap?64u:128u)*scale;
    const size_t w=(crop?60u:128u)*scale,h=(crop?20u:64u)*scale;
    out.width=swap?h:w;out.height=swap?w:h;
    const auto old=hello(ref,turns);out.expected.resize(3u*w*h);
    static const size_t left[4]={20u,24u,48u,20u},top[4]={20u,20u,24u,48u};
    for(size_t y=0u;y<out.height;++y)for(size_t x=0u;x<out.width;++x)
        std::copy_n(old.data()+3u*((y+(crop?top[turns/90u]*scale:0u))*sw+x+(crop?left[turns/90u]*scale:0u)),3u,
            out.expected.data()+3u*(y*out.width+x));
    const float unit=float(units[kind])/4.0f;
    out.width_points=float(crop?(swap?20u:60u):(swap?64u:128u))*unit;
    out.height_points=float(crop?(swap?60u:20u):(swap?128u:64u))*unit;
    return out;
}
}
#endif
