#ifndef QUANTAPDF_NATIVE_GRAPHICS_STATE_FIXTURE_H
#define QUANTAPDF_NATIVE_GRAPHICS_STATE_FIXTURE_H
#include "native_content_stream_fixture.h"
#include "native_ttf_fixture.h"
#include <array>

namespace native_graphics_fixture {
using Object=QPDFObjectHandle;
struct original {std::string bytes;size_t decoded_bytes=0u;};
inline original create(size_t depth,bool clipped,bool images,bool encrypted,
                       bool late_growth=false,int invalid=0,bool mid_path=false)
{
    auto pdf=QPDF::create();pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    root.replaceKey("/MediaBox",Object::parse("[0 0 12 9]"));
    auto resources=Object::newDictionary();
    if(images) {
        auto objects=Object::newDictionary();
        for(const auto& name:{"/Blue","/Red"}) {
            const bool blue=std::string(name)=="/Blue";
            const std::string rgb=blue?std::string("\0\0\xff",3u):std::string("\xff\0\0",3u);
            auto image=pdf->newStream(rgb);auto dict=image.getDict();
            dict.replaceKey("/Subtype",Object::newName("/Image"));
            dict.replaceKey("/Width",Object::newInteger(1));
            dict.replaceKey("/Height",Object::newInteger(1));
            dict.replaceKey("/BitsPerComponent",Object::newInteger(8));
            dict.replaceKey("/ColorSpace",Object::newName("/DeviceRGB"));
            objects.replaceKey(name,image);
        }
        resources.replaceKey("/XObject",objects);
    }
    root.replaceKey("/Resources",resources);
    std::array<std::string,3> content;
    content[0]=clipped?"q 1 1 10 7 re W n ":"";
    content[0]+="1 0 0 rg ";
    if(late_growth)content[0]+="0 0 1 rg 0 0 4 9 re f 1 0 0 rg ";
    for(size_t i=0u;i<depth;++i) {
        content[0]+="q 1 0 0 1 "+std::string(i%2u?"-1":"1")+" 0 cm 0 1 0 rg ";
        // Stroking metadata has no pixel effect on these Fill/Image originals.
        content[0]+="0.5 w 2 J 1 j 0.5 G 4 M [1 2] 0 d ";
    }
    if(images) {
        content[1]="q 6 0 0 9 "+std::to_string(-int(depth%2u))+" 0 cm /Blue Do Q ";
    } else if(late_growth) {
        content[1]=std::to_string(4-int(depth%2u))+" 0 4 9 re f ";
    } else {
        const auto left=std::to_string(-int(depth%2u));
        const auto right=std::to_string(4-int(depth%2u));
        content[1]=mid_path?
            "0 0 1 rg "+left+" 0 m "+right+" 0 l q 1 0 0 1 0 1 cm 1 0 0 rg "+
                right+" 8 l Q "+left+" 9 l h f Q ":
            "0 0 1 rg "+left+" 0 4 9 re f Q ";
        content[1]+=std::to_string(4-int((depth-1u)%2u))+" 0 4 9 re f ";
    }
    const size_t remaining=depth-(!images && !late_growth?1u:0u);
    for(size_t i=0u;i<remaining;++i)content[2]+="Q ";
    content[2]+=images?"q 6 0 0 9 6 0 cm /Red Do Q ":"8 0 4 9 re f ";
    // Long, balanced nonpainting state traffic tests token count independently
    // from maximum nesting. No actual paint is skipped or replayed.
    for(size_t i=0u;i<257u;++i)content[2]+="q 1 0 0 1 0 0 cm 0.5 g Q ";
    if(clipped)content[2]+="Q ";
    if(invalid==1)content[2]+="Q ";
    if(invalid==2)content[2]+="q ";
    if(invalid==3)content[2]+="(bad) 0 0 1 0 0 cm ";
    if(invalid==4)content[2]+="/Missing sh ";
    auto blank=pdf->makeIndirectObject(Object::parse("<< /Type /Page >>"));
    blank.replaceKey("/Parent",root);blank.replaceKey("/Contents",pdf->newStream(""));
    auto page=pdf->makeIndirectObject(Object::parse("<< /Type /Page >>"));
    page.replaceKey("/Parent",root);
    std::vector<Object> streams;original result;
    for(size_t i=0u;i<content.size();++i) {
        content[i]+='\n';
        result.decoded_bytes+=content[i].size();
        streams.push_back(native_content_fixture::stream(*pdf,content[i],unsigned(i)+1u));
    }
    page.replaceKey("/Contents",Object::newArray(streams));
    root.replaceKey("/Kids",Object::newArray({blank,page}));
    root.replaceKey("/Count",Object::newInteger(2));
    QPDFWriter writer(*pdf);writer.setOutputMemory();
    writer.setStreamDataMode(qpdf_s_preserve);writer.setCompressStreams(false);
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto buffer=writer.getBufferSharedPointer();
    result.bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
    return result;
}
inline constexpr const char *alpha_form =
    "<< /Type /XObject /Subtype /Form /BBox [0 0 4 2.5] /Matrix [1 0 0 1 0.5 0]"
    " /Group << /S /Transparency /I true >> >>";

inline constexpr const char *alpha_knockout_form =
    "<< /Type /XObject /Subtype /Form /BBox [0 0 4 2.5] /Matrix [1 0 0 1 0.5 0]"
    " /Group << /S /Transparency /I true /K true >> >>";

inline std::string luminosity_form(const char *space,bool knockout=false)
{
    return std::string("<< /Subtype /Form /BBox [0 0 4 2.5] /Matrix [1 0 0 1 .5 0] "
        "/Group << /S /Transparency /CS ")+space+(knockout?" /K true":"")+" >> >>";
}

inline constexpr const char* stitched_luminosity_mask =
    "<< /S /Luminosity /BC [.625] /TR << /FunctionType 3 /Domain [0 1] /Range [0 .875] "
    "/Bounds [.5 .75] /Encode [0 1 1 0 .75 1.25] /Functions ["
    "<< /FunctionType 2 /Domain [0 1] /Range [.125 .375] /C0 [-.5] /C1 [1.5] /N 1 >> "
    "<< /FunctionType 3 /Domain [.25 .75] /Bounds [] /Encode [.25 .75] /Functions ["
    "<< /FunctionType 2 /Domain [0 1] /N 2 >>] >> "
    "<< /FunctionType 2 /Domain [0 2] /N 1 >>] >> >>";
inline constexpr const char* stitched_luminosity_fills =
    ".25 g .5 0 1 2.5 re f .5 g 1.5 0 1 2.5 re f "
    "1 g 2.5 0 1 2.5 re f .75 g 3.5 0 1 2.5 re f";
inline constexpr const char* stitched_alpha_mask =
    "<< /S /Alpha /TR << /FunctionType 3 /Domain [0 1] /Bounds [1] "
    "/Encode [0 1 .875 .125] /Functions ["
    "<< /FunctionType 2 /Domain [0 1] /C0 [.25] /C1 [.25] /N 1 >> "
    "<< /FunctionType 2 /Domain [0 1] /N 1 >>] >> >>";

struct stream_transfer_fixture {
    std::string dictionary;
    std::string bytes;
    int child=-1;
};

inline std::string pack_samples(unsigned bits,std::initializer_list<uint32_t> values)
{
    std::string bytes((values.size()*bits+7u)/8u,'\0');
    size_t offset=0u;
    for(uint32_t value:values)for(unsigned b=bits;b>0u;--b,++offset) {
        const unsigned bit=(value>>(b-1u))&1u;
        bytes[offset/8u]=static_cast<char>(static_cast<unsigned char>(bytes[offset/8u])|
            (bit<<(7u-offset%8u)));
    }
    return bytes;
}

inline constexpr const char* sampled_stitch_mask =
    "<< /S /Luminosity /BC [.625] /TR << /FunctionType 3 /Domain [0 1] "
    "/Bounds [.5] /Encode [0 1 0 1] /Functions [null null] >> >>";
inline constexpr const char* sampled_stitch_fills =
    "0 g 0 0 .5 2.5 re f .25 g .5 0 .5 2.5 re f .28125 g 1 0 .5 2.5 re f .5 g 1.5 0 .5 2.5 re f "
    ".5625 g 2 0 .5 2.5 re f .6875 g 2.5 0 .5 2.5 re f .8125 g 3 0 .5 2.5 re f .9375 g 3.5 0 .5 2.5 re f";

inline std::vector<stream_transfer_fixture> sampled_stitch_tables()
{
    return {{"<< /FunctionType 0 /Domain [.25 .75] /Range [.125 .75] /Size [3] "
             "/BitsPerSample 12 /Encode [3 -1] /Decode [1 0] >>",pack_samples(12u,{4095u,0u,4095u}),0},
            {"<< /FunctionType 0 /Domain [0 1] /Range [0 1] /Size [4] "
             "/BitsPerSample 1 /Order 3 /Encode [0 4] >>",pack_samples(1u,{0u,1u,0u,1u}),1}};
}

inline stream_transfer_fixture calculator_function(std::string program,int child=-1)
{
    return {"<< /FunctionType 4 /Domain [0 1] /Range [0 1] >>",std::move(program),child};
}

inline constexpr const char* calculator_piecewise =
    "{ % square the lower interval\n dup .5 lt { dup mul } { neg 1 add } ifelse }\n";

inline std::vector<stream_transfer_fixture> calculator_stitch_streams()
{
    return {calculator_function(calculator_piecewise,0),
        {"<< /FunctionType 0 /Domain [0 1] /Range [0 1] /Size [2] /BitsPerSample 1 >>",
         pack_samples(1u,{0u,1u}),1}};
}

inline constexpr const char* calculator_stitch_mask =
    "<< /S /Luminosity /BC [.25] /TR << /FunctionType 3 /Domain [0 1] "
    "/Bounds [.5] /Encode [0 1 1 0] /Functions [null null] >> >>";

inline original create_alpha_group(const std::string& page_content, const std::string& mask_content,
    const char *form_dictionary=alpha_form, const char *soft_mask="<< /S /Alpha /TR /Identity >>",
    bool cyclic_transfer=false,const std::vector<stream_transfer_fixture>& streams={},
    unsigned char image_opacity=255u,bool blend_images=false)
{
    auto pdf=QPDF::create();
    const auto font_fixture=native_ttf_fixture();
    pdf->processMemoryFile("alpha-group fixture",font_fixture.data(),font_fixture.size());
    auto page=pdf->getAllPages().front();
    page.replaceKey("/MediaBox",Object::parse("[0 0 12 4]"));
    page.replaceKey("/Contents",pdf->newStream(page_content));
    auto resources=page.getKey("/Resources");
    auto form=pdf->newStream(mask_content);
    auto form_dict=form.getDict(), entries=Object::parse(form_dictionary);
    for(const auto& key:entries.getKeys())form_dict.replaceKey(key,entries.getKey(key));
    auto mask=Object::parse(soft_mask);
    for(const auto& fixture:streams) {
        auto stream=pdf->newStream(fixture.bytes);
        auto dict=stream.getDict(),keys=Object::parse(fixture.dictionary);
        for(const auto& key:keys.getKeys())dict.replaceKey(key,keys.getKey(key));
        if(fixture.child<0)mask.replaceKey("/TR",stream);
        else mask.getKey("/TR").getKey("/Functions").setArrayItem(fixture.child,stream);
    }
    if(cyclic_transfer) {
        auto transfer=pdf->makeIndirectObject(mask.getKey("/TR"));
        transfer.getKey("/Functions").setArrayItem(0,transfer);
        mask.replaceKey("/TR",transfer);
    }
    mask.replaceKey("/G",form);
    auto states=Object::parse("<< /Alpha <<>> /Inert << /ca 1 >> /Clear << /SMask /None >> "
        "/Half << /ca .5 >> /Quarter << /ca .25 >> /Zero << /ca 0 >> "
        "/StrokeOnly << /CA 1 >> /Precise << /ca .123456789 >> /Bad << /ca 1.1 >> "
        "/Multiply << /BM /Multiply >> /Screen << /BM /Screen >> /Overlay << /BM /Overlay >> "
        "/Darken << /BM /Darken >> /Lighten << /BM /Lighten >> /Reset << /BM /Normal >> "
        "/ColorDodge << /BM /ColorDodge >> /ColorBurn << /BM /ColorBurn >> "
        "/HardLight << /BM /HardLight >> /SoftLight << /BM /SoftLight >> "
        "/Difference << /BM /Difference >> /Exclusion << /BM /Exclusion >> "
        "/Hue << /BM /Hue >> /Saturation << /BM /Saturation >> "
        "/Color << /BM /Color >> /Luminosity << /BM /Luminosity >> "
        "/FallbackDodge << /BM [/CustomBlend /ColorDodge] >> "
        "/FallbackBurn << /BM [/CustomBlend /ColorBurn] >> "
        "/FallbackHue << /BM [/CustomBlend /Hue] >> "
        "/CustomOnly << /BM /CustomBlend >> /UnsupportedArr << /BM [/CustomA /CustomB] >> "
        "/BadArrEmpty << /BM [] >> /BadArrType << /BM [/Multiply 1] >> "
        "/BadBMNum << /BM 0 >> "
        "/Separate << /TK false >> /Knockout << /TK true >> /BadTK << /TK 0 >> >>");
    states.getKey("/Alpha").replaceKey("/SMask",mask);
    resources.replaceKey("/ExtGState",states);
    auto color=Object::parse(
        "<< /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB >>");
    auto image=pdf->newStream(std::string("\xff\xff\0",3u));
    auto masked_image=pdf->newStream(std::string("\xff\xff\0",3u));
    for(const auto& key:color.getKeys()) {
        image.getDict().replaceKey(key,color.getKey(key));
        masked_image.getDict().replaceKey(key,color.getKey(key));
    }
    auto opacity=pdf->newStream(std::string(1u,static_cast<char>(image_opacity)));
    auto gray=Object::parse(
        "<< /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray >>");
    for(const auto& key:gray.getKeys())opacity.getDict().replaceKey(key,gray.getKey(key));
    masked_image.getDict().replaceKey("/SMask",opacity);
    auto objects=Object::newDictionary();
    objects.replaceKey("/Plain",image);
    objects.replaceKey("/Own",masked_image);
    if(blend_images) {
        color.replaceKey("/Width",Object::newInteger(2));
        color.replaceKey("/Interpolate",Object::newBool(true));
        const std::string rgb("\xcc\x66\x33\x33\xcc\x66",6u);
        auto interpolated=pdf->newStream(rgb);
        auto keyed=pdf->newStream(rgb);
        auto wide=pdf->newStream(std::string(
            "\xcc\xcd\x66\x66\x33\x33\x33\x33\xcc\xcc\x66\x66",12u));
        for(const auto& key:color.getKeys()) {
            interpolated.getDict().replaceKey(key,color.getKey(key));
            keyed.getDict().replaceKey(key,color.getKey(key));
            wide.getDict().replaceKey(key,color.getKey(key));
        }
        keyed.getDict().replaceKey("/Mask",Object::parse("[204 204 102 102 51 51]"));
        auto ramp=pdf->newStream(std::string("\0\xff",2u));
        gray.replaceKey("/Width",Object::newInteger(2));
        gray.replaceKey("/Interpolate",Object::newBool(true));
        for(const auto& key:gray.getKeys())ramp.getDict().replaceKey(key,gray.getKey(key));
        wide.getDict().replaceKey("/BitsPerComponent",Object::newInteger(16));
        wide.getDict().replaceKey("/SMask",ramp);
        objects.replaceKey("/Interpolated",interpolated);
        objects.replaceKey("/Keyed",keyed);
        objects.replaceKey("/WideMask",wide);
    }
    resources.replaceKey("/XObject",objects);
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion(blend_images?"1.5":"1.4");
    writer.setOutputMemory();
    writer.write();
    const auto buffer=writer.getBufferSharedPointer();
    return {std::string(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize()),
            page_content.size()+mask_content.size()};
}

// Independent source rectangle/Clip sample intersections and integer opaque
// source-over. All coordinates are exact integers/rational 4x4 sample centers.
inline std::vector<unsigned char> rgb(unsigned dpi,bool clipped,bool images)
{
    const size_t width=12u*dpi/72u,height=9u*dpi/72u;
    std::vector<unsigned char> output(width*height*3u,255u);
    const unsigned colors[3][3]={{0u,0u,255u},{0u,255u,0u},{255u,0u,0u}};
    for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x)
        for(size_t paint=0u;paint<(images?2u:3u);++paint) {
            unsigned hits=0u;
            const size_t left=paint*(images?6u:4u),right=left+(images?6u:4u);
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const uint64_t xx=(uint64_t(x)*8u+2u*sx+1u)*72u;
                const uint64_t yy=(uint64_t(y)*8u+2u*sy+1u)*72u;
                const uint64_t unit=uint64_t(dpi)*8u;
                hits+=unsigned(xx>=left*unit && xx<right*unit &&
                    (!clipped || (xx>=unit && xx<11u*unit && yy>=unit && yy<8u*unit)));
            }
            const auto& color=colors[images && paint==1u?2u:paint];
            for(size_t c=0u;c<3u;++c) {
                auto& dst=output[(y*width+x)*3u+c];
                dst=static_cast<unsigned char>((color[c]*hits+unsigned(dst)*(16u-hits)+8u)/16u);
            }
        }
    return output;
}
}
#endif
