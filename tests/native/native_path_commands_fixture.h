#ifndef QUANTAPDF_NATIVE_PATH_COMMANDS_FIXTURE_H
#define QUANTAPDF_NATIVE_PATH_COMMANDS_FIXTURE_H
#include "native_content_stream_fixture.h"
#include <array>

namespace native_path_commands {
// Independently authored PDF loops. Collinear cubics and both shorthand
// forms trace the same square; repeated winding distinguishes f from f*.
struct region {int x0,y0,x1,y1;bool triangle,reverse;};
struct original {
    std::string bytes;
    size_t decoded_bytes=0u,commands=0u,operations=0u,repeats=0u;
    unsigned clip=0u;
    bool even=false,restore=false;
    std::vector<region> contours;
};
inline void encode(original& result,const std::array<std::string,3>& parts,unsigned encoding,bool encrypted)
{
    using Object=native_content_fixture::Object;
    auto pdf=QPDF::create();pdf->emptyPDF();auto root=pdf->getRoot().getKey("/Pages");
    auto blank=pdf->makeIndirectObject(Object::parse("<< /Type /Page /MediaBox [0 0 5 4] >>"));
    auto page=pdf->makeIndirectObject(Object::parse("<< /Type /Page /MediaBox [0 0 12 12] >>"));
    blank.replaceKey("/Parent",root);page.replaceKey("/Parent",root);
    root.replaceKey("/Kids",Object::newArray({blank,page}));root.replaceKey("/Count",Object::newInteger(2));
    root.replaceKey("/Resources",Object::newDictionary());
    std::vector<Object> streams;
    for(size_t i=0u;i<parts.size();++i) {
        result.decoded_bytes+=parts[i].size();
        streams.push_back(native_content_fixture::stream(*pdf,parts[i],unsigned((encoding+i)%8u)));
    }
    page.replaceKey("/Contents",Object::newArray(streams));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.setStreamDataMode(qpdf_s_preserve);writer.setCompressStreams(false);
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto buffer=writer.getBufferSharedPointer();
    result.bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
}
inline original create(size_t repeats,unsigned primitive,unsigned clip,bool even,
    bool restore,bool encrypted,size_t zero_commands=0u,int invalid=0)
{
    std::array<std::string,3> parts;
    parts[0]="q\n";
    if(clip==1u || clip==4u)parts[0]+="4 4 4 4 re W n\n";
    if(clip==2u)parts[0]+="3 3 m 9 3 l 3 9 l h W* n\n";
    if(clip==3u)parts[0]+="2 2 m W* n\n";
    if(clip==4u)parts[0]+="q 5 3 4 6 re W* n\n";
    parts[0]+="0.2 0.4 0.6 rg 2 2 m\n";
    const std::array<std::string,4> corner={"2 2","10 2","10 10","2 10"};
    for(size_t i=1u;i<4u*repeats;++i) {
        const auto& a=corner[(i-1u)%4u];const auto& b=corner[i%4u];
        auto& part=parts[i<2u*repeats?1u:2u];
        if(primitive==0u)part+=b+" l\n";
        if(primitive==1u)part+=a+" "+b+" "+b+" c\n";
        if(primitive==2u)part+=b+" "+b+" v\n";
        if(primitive==3u)part+=a+" "+b+" y\n";
    }
    for(size_t i=0u;i<zero_commands;++i)parts[2]+="2 10 l\n";
    parts[2]+=even?"h f*\n":"h f\n";
    parts[2]+=clip==4u?"Q Q\n":"Q\n";
    if(restore)parts[2]+="0.8 0.2 0.4 rg 0 0 2 2 re f\n";
    if(invalid==1)parts[2]+="Q\n";
    if(invalid==2)parts[2]+="/Missing Do\n";
    if(invalid==3)parts[2]+="2 /Bad m\n";
    if(invalid==4)parts[2]+="/Missing sh\n";
    original result;result.repeats=repeats;result.clip=clip;result.even=even;result.restore=restore;
    result.commands=4u*repeats+1u+zero_commands;result.operations=restore?2u:1u;
    encode(result,parts,unsigned(repeats+primitive),encrypted);return result;
}

// Multiple original subpaths share ONE fill. No per-contour compositing.
inline original create_contours(size_t count,unsigned primitive,unsigned clip,bool even,
    bool restore,bool encrypted,size_t zeros=0u,unsigned layout=0u,unsigned closing=0u,int invalid=0)
{
    original result;result.clip=clip;result.even=even;result.restore=restore;
    result.operations=restore?2u:1u;
    std::array<std::string,3> parts;parts[0]="q\n";
    if(clip==1u || clip==4u)parts[0]+="4 4 4 4 re W n\n";
    if(clip==2u)parts[0]+="3 3 m 9 3 l 3 9 l h W* n\n";
    if(clip==3u)parts[0]+="2 2 m W* n\n";
    if(clip==4u)parts[0]+="q 5 3 4 6 re W* n\n";
    parts[0]+="0.2 0.4 0.6 rg\n";
    const auto point=[](int x,int y) {return std::to_string(x)+" "+std::to_string(y);};
    for(size_t i=0u;i<count;++i) {
        region r{};
        if(layout==0u) {
            if(i%3u==0u)r={1,1,11,11,false,false};
            if(i%3u==1u)r={4,4,8,8,false,true};
            if(i%3u==2u)r={5,5,7,7,false,false};
        } else if(layout==1u) {
            const int x=1+3*int(i%3u),y=1+3*int((i/3u)%3u);
            r={x,y,x+4,y+4,false,false};
        } else {
            // Multiples of three project exactly at all three current DPIs.
            // Layout 3 retains the separate 96-DPI boundary reproducer.
            const int offset=layout==2u?0:1,extent=layout==2u?3:4;
            const int x=offset+6*int(i%2u),y=offset+6*int((i/2u)%2u);
            r={x,y,x+extent,y+extent,true,i%2u!=0u};
        }
        result.contours.push_back(r);auto& out=parts[i<count/2u?1u:2u];
        std::vector<std::string> vertices={point(r.x0,r.y0),point(r.x1,r.y0),point(r.x1,r.y1),point(r.x0,r.y1)};
        if(r.triangle)vertices.erase(vertices.begin()+2);
        if(r.reverse)std::reverse(vertices.begin()+1,vertices.end());
        if(closing==3u && !r.triangle) {
            out+=point(r.x0,r.reverse?r.y1:r.y0)+" "+std::to_string(r.x1-r.x0)+" "+std::to_string((r.reverse?-1:1)*(r.y1-r.y0))+" re\n";
        } else {
            out+=vertices[0]+" m\n";
            for(size_t j=1u;j<vertices.size();++j) {
                const auto& a=vertices[j-1u];const auto& b=vertices[j];
                if(primitive==0u)out+=b+" l\n";
                if(primitive==1u)out+=a+" "+b+" "+b+" c\n";
                if(primitive==2u)out+=b+" "+b+" v\n";
                if(primitive==3u)out+=a+" "+b+" y\n";
            }
            if(i==count-1u)for(size_t z=0u;z<zeros;++z)out+=vertices.back()+" l\n";
            if(closing==2u)out+=vertices[0]+" l\n";
            if(closing!=1u)out+="h\n";
        }
        result.commands+=vertices.size()+1u;
        // These state saves do not save or clear the current PDF path.
        out+="q 1 0 0 1 0 0 cm Q\n";
    }
    result.commands+=zeros;
    parts[2]+=even?"f*\n":"f\n";parts[2]+=clip==4u?"Q Q\n":"Q\n";
    if(restore)parts[2]+="0.8 0.2 0.4 rg 0 0 2 2 re f\n";
    if(invalid==1)parts[2]+="Q\n";
    if(invalid==2)parts[2]+="/Missing Do\n";
    if(invalid==3)parts[2]+="2 /Bad m\n";
    if(invalid==4)parts[2]+="/Missing sh\n";
    encode(result,parts,unsigned(count+primitive),encrypted);return result;
}
inline std::vector<unsigned char> rgb(const original& sample,unsigned dpi)
{
    const size_t width=12u*dpi/72u;const double scale=double(dpi)/72.0;
    std::vector<unsigned char> pixels(width*width*3u,255u);
    const auto rectangle=[](double x,double y,double x0,double y0,double x1,double y1) {
        return x>=x0 && x<x1 && y>=y0 && y<y1;
    };
    for(size_t y=0u;y<width;++y)for(size_t x=0u;x<width;++x) {
        unsigned first=0u,last=0u;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const double px=(double(x)+(double(sx)+0.5)/4.0)/scale;
            const double py=12.0-(double(y)+(double(sy)+0.5)/4.0)/scale;
            bool clip=true;
            if(sample.clip==1u)clip=rectangle(px,py,4,4,8,8);
            if(sample.clip==2u)clip=px>=3.0 && py>=3.0 && px+py<12.0;
            if(sample.clip==3u)clip=false;
            if(sample.clip==4u)clip=rectangle(px,py,5,4,8,8);
            bool inside=rectangle(px,py,2,2,10,10) && (!sample.even || sample.repeats%2u!=0u);
            if(!sample.contours.empty()) {
                int winding=0;
                for(const auto& r:sample.contours) {
                    const bool hit=r.triangle ? px>=r.x0 && py>=r.y0 &&
                        (px-r.x0)/double(r.x1-r.x0)+(py-r.y0)/double(r.y1-r.y0)<1.0 :
                        rectangle(px,py,r.x0,r.y0,r.x1,r.y1);
                    if(hit)winding+=r.reverse?-1:1;
                }
                inside=sample.even?winding%2!=0:winding!=0;
            }
            first+=clip && inside;
            last+=sample.restore && rectangle(px,py,0,0,2,2);
        }
        const unsigned a[3]={51u,102u,153u},b[3]={204u,51u,102u};
        for(size_t c=0u;c<3u;++c) {
            unsigned value=(a[c]*first+255u*(16u-first)+8u)/16u;
            pixels[3u*(y*width+x)+c]=static_cast<unsigned char>((b[c]*last+value*(16u-last)+8u)/16u);
        }
    }
    return pixels;
}
}
#endif
