#ifndef QUANTAPDF_NATIVE_ORDERED_SCENE_FIXTURE_H
#define QUANTAPDF_NATIVE_ORDERED_SCENE_FIXTURE_H
#include "native_ttf_fixture.h"
#include "native_content_stream_fixture.h"
#include <array>
namespace native_ordered_scene {
using O=QPDFObjectHandle;
struct paint {double x,y,w,h;std::array<unsigned char,12> colors;bool image;};
struct original {std::string bytes;size_t decoded_bytes=0u,operations=0u,glyphs=0u,images=0u;std::vector<paint> paints;};
inline original create(size_t count,size_t letters,unsigned permutation,bool encrypted,size_t depth=0u,int invalid=0)
{
    auto pdf=QPDF::create();const auto base=native_ttf_fixture();
    pdf->processMemoryFile("ordered-scene-authoring",base.data(),base.size());
    auto root=pdf->getRoot().getKey("/Pages"),page=root.getKey("/Kids").getArrayItem(0);
    auto resources=page.getKey("/Resources"),xobjects=O::newDictionary();
    resources.replaceKey("/XObject",xobjects);root.replaceKey("/Resources",resources);page.removeKey("/Resources");
    page.replaceKey("/MediaBox",O::parse("[0 0 12 12]"));
    auto blank=pdf->makeIndirectObject(O::parse("<< /Type /Page /MediaBox [0 0 12 12] >>"));
    blank.replaceKey("/Parent",root);blank.replaceKey("/Contents",pdf->newStream(""));
    root.replaceKey("/Kids",O::newArray({blank,page}));root.replaceKey("/Count",O::newInteger(2));
    // An unused image alias and Font/ExtGState resource are legal PDF resources.
    // Their presence cannot become an arbitrary whole-page profile gate.
    resources.replaceKey("/ExtGState",O::newDictionary());
    resources.getKey("/Font").replaceKey("/Unused",resources.getKey("/Font").getKey("/F"));
    original result;std::array<std::string,3> parts;parts[0]="q ";
    const std::array<std::array<unsigned char,3>,3> colors={{{51,102,153},{204,51,102},{102,204,51}}};
    const char *rg[]={"0.2 0.4 0.6 rg ","0.8 0.2 0.4 rg ","0.4 0.8 0.2 rg "};
    for(size_t i=0u;i<count;++i) {
        auto& part=parts[std::min(size_t(2u),i*3u/count)];
        if(i==0u)for(size_t q=0u;q<depth;++q)part+="q ";
        const double x=double(i%7u)*1.75-0.5,y=double((i/7u)%7u)*1.75-0.25;
        std::array<unsigned char,12> samples{};std::string raw;
        const bool gray=i%2u!=0u;
        for(size_t pixel=0u;pixel<4u;++pixel)for(size_t c=0u;c<3u;++c)
            samples[pixel*3u+c]=gray?static_cast<unsigned char>(51u*((i+pixel)%5u)):colors[(i+pixel)%3u][c];
        for(size_t pixel=0u;pixel<4u;++pixel)for(size_t c=0u;c<(gray?1u:3u);++c)raw+=char(samples[pixel*3u+c]);
        auto image=pdf->newStream(raw);auto dict=image.getDict();
        dict.replaceKey("/Type",O::newName("/XObject"));dict.replaceKey("/Subtype",O::newName("/Image"));
        dict.replaceKey("/Width",O::newInteger(2));dict.replaceKey("/Height",O::newInteger(2));
        dict.replaceKey("/BitsPerComponent",O::newInteger(8));dict.replaceKey("/ColorSpace",O::newName(gray?"/DeviceGray":"/DeviceRGB"));
        if(gray) {image.replaceStreamData(native_content_fixture::flate(raw),O::newName("/FlateDecode"),O::newNull());image.setFilterOnWrite(false);}
        if(invalid==4 && i+1u==count)dict.replaceKey("/SMask",O::newName("/None"));
        const std::string name="/I"+std::to_string(i);xobjects.replaceKey(name,image);
        if(i==0u)xobjects.replaceKey("/Unused",image);
        const bool text=i<3u || i%11u==5u,fill=i%7u==0u;
        static const unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        for(unsigned item:orders[permutation%6u]) {
            if(item==0u) {
                part+="q 2.25 0 0 2.5 "+std::to_string(x)+" "+std::to_string(y)+" cm "+name+" Do Q ";
                result.paints.push_back({x,y,2.25,2.5,samples,true});++result.operations;++result.images;
            } else if(item==1u && text) {
                const size_t n=i==0u?letters:1u;const unsigned color=unsigned((i+1u)%3u);
                const double tx=double(i%6u)*1.75+0.25,ty=double((i/6u)%7u)*1.5+0.25;
                part+=std::string(rg[color])+"BT /F 2 Tf -0.4 Tc "+std::to_string(tx)+" "+std::to_string(ty)+" Td <";
                for(size_t g=0u;g<n;++g) {
                    part+="0051";std::array<unsigned char,12> ink{};
                    std::copy(colors[color].begin(),colors[color].end(),ink.begin());
                    result.paints.push_back({tx+double(g)*(0.486-0.4)+156.0/1024.0,ty,185.0/1024.0,1.5,ink,false});
                }
                part+="> Tj ET ";result.glyphs+=n;++result.operations;
            } else if(item==2u && fill) {
                const unsigned color=unsigned((i+2u)%3u);std::array<unsigned char,12> ink{};
                std::copy(colors[color].begin(),colors[color].end(),ink.begin());
                part+=std::string(rg[color])+std::to_string(x+0.75)+" "+std::to_string(y+0.75)+" 2.5 1.25 re f* ";
                result.paints.push_back({x+0.75,y+0.75,2.5,1.25,ink,false});++result.operations;
            }
        }
        if(i+1u==count)for(size_t q=0u;q<depth;++q)part+="Q ";
    }
    parts[2]+="Q ";
    if(invalid==1)parts[2]+="Q ";
    if(invalid==2)parts[2]+="/Missing Do ";
    if(invalid==3)parts[2]+="1 1 2 2 re S ";
    std::vector<O> contents;
    for(size_t i=0u;i<parts.size();++i) {parts[i]+='\n';result.decoded_bytes+=parts[i].size();contents.push_back(native_content_fixture::stream(*pdf,parts[i],unsigned(i)+1u));}
    page.replaceKey("/Contents",O::newArray(contents));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.setStreamDataMode(qpdf_s_preserve);writer.setCompressStreams(false);
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto buffer=writer.getBufferSharedPointer();result.bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());return result;
}
inline std::vector<unsigned char> rgb(const original& source,unsigned dpi)
{
    const size_t side=12u*dpi/72u;const double scale=double(dpi)/72.0;std::vector<unsigned char> out(side*side*3u,255u);
    for(const auto& p:source.paints)for(size_t y=0u;y<side;++y)for(size_t x=0u;x<side;++x) {
        const double x0=p.x*scale,x1=(p.x+p.w)*scale,y0=(12.0-p.y-p.h)*scale,y1=(12.0-p.y)*scale;
        unsigned hits=0u;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const double xx=double(x)+(double(sx)+0.5)/4.0,yy=double(y)+(double(sy)+0.5)/4.0;
            hits+=unsigned(xx>=x0 && xx<x1 && yy>=y0 && yy<y1);
        }
        size_t pixel=0u;
        if(p.image) {
            const double u=(double(x)+0.5-x0)/(x1-x0),v=(double(y)+0.5-y0)/(y1-y0);
            const size_t col=u<=0.0?0u:u>=1.0?1u:size_t(u*2.0),row=v<=0.0?0u:v>=1.0?1u:size_t(v*2.0);
            pixel=row*2u+col;
        }
        for(size_t c=0u;c<3u;++c) {auto& value=out[(y*side+x)*3u+c];value=static_cast<unsigned char>((unsigned(p.colors[pixel*3u+c])*hits+unsigned(value)*(16u-hits)+8u)/16u);}
    }
    return out;
}
}
#endif
