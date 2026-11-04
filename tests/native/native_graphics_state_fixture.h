#ifndef QUANTAPDF_NATIVE_GRAPHICS_STATE_FIXTURE_H
#define QUANTAPDF_NATIVE_GRAPHICS_STATE_FIXTURE_H
#include "native_content_stream_fixture.h"
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
