#ifndef QUANTAPDF_NATIVE_ORDERED_TEXT_FIXTURE_H
#define QUANTAPDF_NATIVE_ORDERED_TEXT_FIXTURE_H
#include "native_ttf_fixture.h"
#include "native_content_stream_fixture.h"
#include <array>
namespace native_ordered_text {
using O=QPDFObjectHandle;
struct paint {double x,y,w,h;unsigned color;};
struct original {std::string bytes;size_t decoded_bytes=0u,operations=0u,glyphs=0u;bool restored_text_state=false;std::vector<paint> paints;};
inline original create(size_t count,int kind,bool encrypted,size_t depth=0u,int invalid=0)
{
    auto pdf=QPDF::create();const auto base=native_ttf_fixture();
    pdf->processMemoryFile("ordered-text-authoring",base.data(),base.size());
    auto root=pdf->getRoot().getKey("/Pages"),page=root.getKey("/Kids").getArrayItem(0);
    root.replaceKey("/Resources",page.getKey("/Resources"));page.removeKey("/Resources");
    page.replaceKey("/MediaBox",O::parse("[0 0 12 12]"));
    auto blank=pdf->makeIndirectObject(O::parse("<< /Type /Page /MediaBox [0 0 12 12] >>"));
    blank.replaceKey("/Parent",root);blank.replaceKey("/Contents",pdf->newStream(""));
    root.replaceKey("/Kids",O::newArray({blank,page}));root.replaceKey("/Count",O::newInteger(2));
    const char *colors[]={"1 0 0 rg ","0 1 0 rg ","0 0 1 rg "};
    original result;std::array<std::string,3> parts;parts[0]="q ";
    const auto glyph=[&](double x,double y,double size,unsigned color) {
        // Roboto glyph 81 ('l'): four original on-curve points, unitsPerEm=2048.
        // x=156..341, y=0..1536; independent of production font/IR decoders.
        result.paints.push_back({x+156.0*size/2048.0,y,185.0*size/2048.0,1536.0*size/2048.0,color});
        ++result.glyphs;
    };
    if(kind==3) {
        parts[0]+="1 0 0 rg BT /F 2 Tf -0.4 Tc 1 3 Td ";
        parts[1]=encrypted?"[":"<";
        for(size_t i=0u;i<count;++i)parts[1]+=encrypted?"<0051> ":"0051";
        parts[1]+=encrypted?"] TJ ":"> Tj ";parts[2]="ET Q ";
        double x=1.0;for(size_t i=0u;i<count;++i) {glyph(x,3.0,2.0,0u);x+=243.0*2.0/1000.0-0.4;}
        result.operations=1u;
    } else if(kind==4) {
        result.restored_text_state=true;
        parts[0]+="1 0 0 rg BT /F 1 Tf 1 1 Td <0051> Tj ";glyph(1.0,1.0,1.0,0u);
        for(size_t i=0u;i<depth;++i)parts[1]+="q /F 2 Tf -0.4 Tc 0 1 0 rg ";
        parts[1]+="1 0 0 1 4 4 Tm ";
        for(size_t i=0u;i<depth;++i)parts[2]+="Q ";
        parts[2]+="<00510051> Tj ET Q ";glyph(4.0,4.0,1.0,0u);glyph(4.0+243.0/1000.0,4.0,1.0,0u);result.operations=2u;
    } else {
        for(size_t i=0u;i<count;++i) {
            const bool text=kind==0 || (kind==1?(i==0u || i%5u==2u):i+1u==count);
            const double x=double(i%6u)*2.0+(text?0.5:0.0),y=double((i/6u)%12u)+(text?0.5:0.0);
            const unsigned color=unsigned(i%3u);auto& part=parts[std::min(size_t(2u),i*3u/count)];
            part+=colors[color];
            if(text) {
                part+="BT /F 1 Tf "+std::to_string(x)+" "+std::to_string(y)+" Td <0051> Tj ET ";glyph(x,y,1.0,color);
            } else {
                const double left=x-(i%3u==1u && i%6u!=0u?1.0:0.0);
                part+=std::to_string(left)+" "+std::to_string(y)+" 2 1 re "+(i%2u?"f* ":"f ");
                result.paints.push_back({left,y,2.0,1.0,color});
            }
        }
        parts[2]+="Q ";result.operations=count;
    }
    if(invalid==1)parts[2]+="Q ";
    if(invalid==2)parts[2]+="BT /Missing 2 Tf <0051> Tj ET ";
    if(invalid==3)parts[2]+="1 1 2 2 re S ";
    if(invalid==4)parts[2]+="BT /F 2 Tf <0051> Tj ";
    std::vector<O> contents;
    for(size_t i=0u;i<parts.size();++i) {
        parts[i]+='\n';result.decoded_bytes+=parts[i].size();
        contents.push_back(native_content_fixture::stream(*pdf,parts[i],unsigned(i)+1u));
    }
    page.replaceKey("/Contents",O::newArray(contents));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.setStreamDataMode(qpdf_s_preserve);writer.setCompressStreams(false);
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto buffer=writer.getBufferSharedPointer();
    result.bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());return result;
}
inline std::vector<unsigned char> rgb(const original& original,unsigned dpi)
{
    const size_t side=12u*dpi/72u;const double scale=double(dpi)/72.0;
    const unsigned colors[3][3]={{255u,0u,0u},{0u,255u,0u},{0u,0u,255u}};
    std::vector<unsigned char> out(side*side*3u,255u);
    for(size_t y=0u;y<side;++y)for(size_t x=0u;x<side;++x)for(const auto& p:original.paints) {
        unsigned hits=0u;
        const double x0=p.x*scale,x1=(p.x+p.w)*scale,y0=(12.0-p.y-p.h)*scale,y1=(12.0-p.y)*scale;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const double xx=double(x)+(double(sx)+0.5)/4.0,yy=double(y)+(double(sy)+0.5)/4.0;
            hits+=unsigned(xx>=x0 && xx<x1 && yy>=y0 && yy<y1);
        }
        for(size_t c=0u;c<3u;++c) {
            auto& value=out[(y*side+x)*3u+c];
            value=static_cast<unsigned char>((colors[p.color][c]*hits+unsigned(value)*(16u-hits)+8u)/16u);
        }
    }
    return out;
}
}
#endif
