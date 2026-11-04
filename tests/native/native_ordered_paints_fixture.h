#ifndef QUANTAPDF_NATIVE_ORDERED_PAINTS_FIXTURE_H
#define QUANTAPDF_NATIVE_ORDERED_PAINTS_FIXTURE_H
#include "native_content_stream_fixture.h"
#include <array>
#include <cmath>
namespace native_ordered_fixture {
using Object=QPDFObjectHandle;
struct paint { double x,y;unsigned color;bool image; };
struct original {std::string bytes;size_t decoded_bytes=0u;std::vector<paint> paints;};
inline original create(size_t count,int kind,bool encrypted,unsigned quarters=4u,int rotation=0,int invalid=0)
{
    auto pdf=QPDF::create();pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    root.replaceKey("/MediaBox",Object::parse("[0 0 12 12]"));
    auto resources=Object::newDictionary(),objects=Object::newDictionary();
    const char *names[]={"/Blue","/Red","/Green"};
    const std::string colors[]={std::string("\0\0\xff",3u),std::string("\xff\0\0",3u),std::string("\0\xff\0",3u)};
    for(size_t i=0u;i<3u;++i) {
        auto image=pdf->newStream(colors[i]);auto dict=image.getDict();
        dict.replaceKey("/Subtype",Object::newName("/Image"));
        dict.replaceKey("/Width",Object::newInteger(1));dict.replaceKey("/Height",Object::newInteger(1));
        dict.replaceKey("/BitsPerComponent",Object::newInteger(8));dict.replaceKey("/ColorSpace",Object::newName("/DeviceRGB"));
        objects.replaceKey(names[i],image);
    }
    resources.replaceKey("/XObject",objects);root.replaceKey("/Resources",resources);
    std::array<std::string,3> content;original result;
    const char *fills[]={"0 0 1 rg ","1 0 0 rg ","0 1 0 rg "};
    content[0]="q ";
    for(size_t i=0u;i<count;++i) {
        // Each row leaves visible early paints; adjacent shifted tiles overlap.
        const double x=double(i%6u)*2.0-(i%3u==1u && i%6u!=0u?1.0:0.0),y=double(i/6u);
        const unsigned color=unsigned(i%3u);
        const bool image=kind==1 || (kind==2 && i%5u!=2u);
        result.paints.push_back({x,y,color,image});
        auto& part=content[std::min(size_t(2u),i*3u/count)];
        const std::string position=std::to_string(x)+" "+std::to_string(y);
        if(image)part+="q 2 0 0 1 "+position+" cm "+names[color]+" Do Q ";
        else part+=std::string(fills[color])+position+" 2 1 re "+(i%2u?"f* ":"f ");
    }
    content[2]+="Q ";
    if(invalid==1)content[2]+="q 2 0 0 1 0 0 cm /Missing Do Q ";
    if(invalid==2)content[2]+="/Missing sh ";
    if(invalid==3)content[2]+="Q ";
    auto blank=pdf->makeIndirectObject(Object::parse("<< /Type /Page >>"));
    blank.replaceKey("/Parent",root);blank.replaceKey("/Contents",pdf->newStream(""));
    auto page=pdf->makeIndirectObject(Object::parse("<< /Type /Page >>"));page.replaceKey("/Parent",root);
    page.replaceKey("/UserUnit",Object::parse(std::to_string(double(quarters)/4.0)));
    page.replaceKey("/Rotate",Object::newInteger(rotation));
    std::vector<Object> streams;
    for(size_t i=0u;i<content.size();++i) {
        content[i]+='\n';result.decoded_bytes+=content[i].size();
        streams.push_back(native_content_fixture::stream(*pdf,content[i],unsigned(i)+1u));
    }
    page.replaceKey("/Contents",Object::newArray(streams));
    root.replaceKey("/Kids",Object::newArray({blank,page}));root.replaceKey("/Count",Object::newInteger(2));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.setStreamDataMode(qpdf_s_preserve);writer.setCompressStreams(false);
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto buffer=writer.getBufferSharedPointer();
    result.bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());return result;
}
inline size_t side(unsigned points,unsigned dpi,unsigned quarters)
{return size_t(std::ceil(double(points)*dpi*quarters/288.0));}
// Analytic authored rectangles on the physical 4x4 sample grid, independent
// of Source IR, device projection, C11 geometry, masks and image producers.
inline std::vector<unsigned char> rgb(const original& sample,unsigned dpi,unsigned quarters=4u,int rotation=0)
{
    const size_t width=side(12u,dpi,quarters),height=side(12u,dpi,quarters);
    const double scale=double(dpi)*quarters/288.0;
    const unsigned colors[3][3]={{0u,0u,255u},{255u,0u,0u},{0u,255u,0u}};
    std::vector<unsigned char> out(width*height*3u,255u);
    for(size_t y=0u;y<height;++y)for(size_t x=0u;x<width;++x)for(const auto& p:sample.paints) {
        unsigned hits=0u;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const double dx=(double(x)+(double(sx)+0.5)/4.0)/scale;
            const double dy=(double(y)+(double(sy)+0.5)/4.0)/scale;
            const double px=rotation?dy:dx,py=rotation?dx:12.0-dy;
            hits+=unsigned(px>=p.x && px<p.x+2.0 && py>=p.y && py<p.y+1.0);
        }
        const unsigned alpha=(hits*255u+8u)/16u;
        for(size_t c=0u;c<3u;++c) {
            auto& dst=out[(y*width+x)*3u+c];
            dst=static_cast<unsigned char>((colors[p.color][c]*alpha+unsigned(dst)*(255u-alpha)+127u)/255u);
        }
    }
    return out;
}
}
#endif
