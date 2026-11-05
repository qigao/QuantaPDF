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
    size_t clip_commands=0u;
    unsigned clip=0u;
    bool even=false,restore=false;
    std::vector<region> contours;
};
inline void encode(original& result,const std::array<std::string,3>& parts,unsigned encoding,bool encrypted,bool across_text=false)
{
    using Object=native_content_fixture::Object;
    auto pdf=QPDF::create();pdf->emptyPDF();auto root=pdf->getRoot().getKey("/Pages");
    auto blank=pdf->makeIndirectObject(Object::parse("<< /Type /Page /MediaBox [0 0 5 4] >>"));
    auto page=pdf->makeIndirectObject(Object::parse("<< /Type /Page /MediaBox [0 0 12 12] >>"));
    blank.replaceKey("/Parent",root);page.replaceKey("/Parent",root);
    root.replaceKey("/Kids",Object::newArray({blank,page}));root.replaceKey("/Count",Object::newInteger(2));
    root.replaceKey("/Resources",Object::newDictionary());
    auto decoded=parts;
    if(across_text) {
        decoded[0]+="BT ET\n";
        for(size_t i=1u;i<decoded.size();++i) {
            for(const std::string op:{"f\n","f*\n"}) {
                size_t at=0u;
                while((at=decoded[i].find(op,at))!=std::string::npos) {
                    at+=op.size();decoded[i].insert(at,"BT ET\n");at+=6u;
                }
            }
        }
    }
    std::vector<Object> streams;
    bool needs_separator=false;
    for(size_t i=0u;i<parts.size();++i) {
        // qpdf separates an empty or non-newline-terminated preceding stream.
        // Include that byte in the independent N/N-1 content work budget.
        if(needs_separator)++result.decoded_bytes;
        result.decoded_bytes+=decoded[i].size();
        needs_separator=decoded[i].empty() || decoded[i].back()!='\n';
        streams.push_back(native_content_fixture::stream(*pdf,decoded[i],unsigned((encoding+i)%8u)));
    }
    page.replaceKey("/Contents",Object::newArray(streams));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.setStreamDataMode(qpdf_s_preserve);writer.setCompressStreams(false);
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto buffer=writer.getBufferSharedPointer();
    result.bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
}
inline original create(size_t repeats,unsigned primitive,unsigned clip,bool even,
    bool restore,bool encrypted,size_t zero_commands=0u,int invalid=0,bool as_clip=false)
{
    std::array<std::string,3> parts;
    parts[0]="q\n";
    if(clip==1u || clip==4u)parts[0]+="4 4 4 4 re W n\n";
    if(clip==2u)parts[0]+="3 3 m 9 3 l 3 9 l h W* n\n";
    if(clip==3u)parts[0]+="2 2 m W* n\n";
    if(clip==5u)parts[0]+="1 1 m 5 1 l 1 5 l h W* n\n";
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
    parts[2]+=as_clip?(even?"h W* n\n":"h W n\n"):(even?"h f*\n":"h f\n");
    if(as_clip)parts[2]+="0 0 12 12 re f\n";
    parts[2]+=clip==4u?"Q Q\n":"Q\n";
    if(restore)parts[2]+="0.8 0.2 0.4 rg 0 0 2 2 re f\n";
    if(invalid==1)parts[2]+="Q\n";
    if(invalid==2)parts[2]+="/Missing Do\n";
    if(invalid==3)parts[2]+="2 /Bad m\n";
    if(invalid==4)parts[2]+="/Missing sh\n";
    original result;result.repeats=repeats;result.clip=clip;result.even=even;result.restore=restore;
    result.commands=4u*repeats+1u+zero_commands;result.operations=restore?2u:1u;
    if(as_clip) {result.clip_commands=result.commands;result.commands=5u;}
    encode(result,parts,unsigned(repeats+primitive),encrypted);return result;
}

// Multiple original subpaths share ONE fill. No per-contour compositing.
inline original create_contours(size_t count,unsigned primitive,unsigned clip,bool even,
    bool restore,bool encrypted,size_t zeros=0u,unsigned layout=0u,unsigned closing=0u,int invalid=0,bool as_clip=false,bool across_text=false)
{
    original result;result.clip=clip;result.even=even;result.restore=restore;
    result.operations=restore?2u:1u;
    std::array<std::string,3> parts;parts[0]="q\n";
    if(clip==1u || clip==4u)parts[0]+="4 4 4 4 re W n\n";
    if(clip==2u)parts[0]+="3 3 m 9 3 l 3 9 l h W* n\n";
    if(clip==3u)parts[0]+="2 2 m W* n\n";
    if(clip==5u)parts[0]+="1 1 m 5 1 l 1 5 l h W* n\n";
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
    if(as_clip) {parts[2]+=even?"W* n\n":"W n\n";parts[2]+="0 0 12 12 re f\n";result.clip_commands=result.commands;result.commands=5u;}
    else parts[2]+=even?"f*\n":"f\n";
    parts[2]+=clip==4u?"Q Q\n":"Q\n";
    if(restore)parts[2]+="0.8 0.2 0.4 rg 0 0 2 2 re f\n";
    if(invalid==1)parts[2]+="Q\n";
    if(invalid==2)parts[2]+="/Missing Do\n";
    if(invalid==3)parts[2]+="2 /Bad m\n";
    if(invalid==4)parts[2]+="/Missing sh\n";
    encode(result,parts,unsigned(count+primitive),encrypted,across_text);return result;
}
// Authored current-point cases. Empty subpaths do not contribute winding;
// following polygons use an independent rectangle membership reference.
inline original create_states(size_t repeats,unsigned primitive,unsigned clip,bool even,
    bool restore,bool encrypted,size_t zeros,unsigned kind,int invalid=0,bool across_text=false)
{
    original result;result.repeats=repeats;result.clip=clip;result.even=even;result.restore=restore;
    result.operations=kind==13u?(restore?1u:0u):(restore?2u:1u);
    std::array<std::string,3> parts;parts[0]="q\n";
    if(clip==1u)parts[0]+="4 4 4 4 re W n\n";
    if(clip==2u)parts[0]+="3 3 m 9 3 l 3 9 l h W* n\n";
    parts[0]+="0.2 0.4 0.6 rg\n";
    const auto segment=[&](const std::string& a,const std::string& b) {
        if(primitive==1u)return a+" "+b+" "+b+" c\n";
        if(primitive==2u)return b+" "+b+" v\n";
        if(primitive==3u)return a+" "+b+" y\n";
        return b+" l\n";
    };
    for(size_t i=0u;i<repeats;++i) {
        auto& out=parts[i<repeats/2u?1u:2u];
        const size_t tail=i+1u==repeats?zeros:0u;
        int shift=0;
        const auto continuation=[&]() {out+="q 1 0 0 1 3 0 cm\n";shift=3;};
        const auto polygon=[&](bool move,bool reverse) {
            const auto left=std::to_string(2-shift),right=std::to_string(10-shift);
            std::array<std::string,4> v={left+" 2",right+" 2",right+" 10",left+" 10"};
            if(reverse)std::reverse(v.begin()+1,v.end());
            if(move)out+=v[0]+" m\n";
            for(size_t j=1u;j<v.size();++j)out+=segment(v[j-1u],v[j]);
            for(size_t z=0u;z<tail;++z)out+=v.back()+" l\n";
            out+="h h q 1 0 0 1 0 0 cm Q\n";
            result.commands+=(move?5u:4u)+tail;
            result.contours.push_back({2,2,10,10,false,reverse});
        };
        if(kind==0u) {out+="2 2 m h h\n";result.commands+=2u;}
        if(kind==1u) {out+="2 2 m 10 2 l h h\n";result.commands+=3u;}
        if(kind==2u) {out+="2 2 m 2 2 2 2 2 2 c h h\n";result.commands+=3u;}
        if(kind==3u) {out+="2 2 0 8 re h h\n";result.commands+=5u;}
        if(kind<=3u)result.contours.push_back({2,2,2,2,false,false});
        if(kind==4u) {out+="2 2 m h h\n";result.commands+=2u;continuation();polygon(false,false);out+="Q\n";}
        if(kind==5u) {out+="2 2 m 10 2 l h h\n";result.commands+=3u;continuation();polygon(false,false);out+="Q\n";}
        if(kind==6u) {out+="11 11 m\n";result.commands+=2u;polygon(true,false);}
        if(kind==7u) {out+="2 2 0 8 re h h\n";result.commands+=5u;polygon(true,false);}
        if(kind==8u) {out+="2 2 m 2 2 2 2 2 2 c h h\n";result.commands+=3u;continuation();polygon(false,false);out+="Q\n";}
        if(kind==9u) {out+="f f*\n";polygon(true,false);}
        if(kind==10u) {out+="2 2 8 0 re h h\n";result.commands+=4u;polygon(true,false);}
        if(kind==11u) {polygon(true,false);polygon(false,true);}
        if(kind==13u) {out+="f f*\n";result.contours.push_back({2,2,2,2,false,false});}
        if(kind==12u) {
            // A single genuine cubic plus its implicit straight closure.
            // Each chosen curve encloses the whole [4,8] Clip, away from its
            // boundary. Only that rectangle contributes visible samples.
            out+="0 0 m\n";
            if(primitive==1u)out+="0 16 12 16 12 0 c\n";
            if(primitive==2u)out+="12 32 12 0 v\n";
            if(primitive==3u)out+="0 32 12 0 y\n";
            out+=even?"f*\n":"f\n";
            result.commands+=3u;
            result.contours.push_back({0,0,12,12,false,false});
        }
    }
    if(kind!=12u)parts[2]+=even?"f*\n":"f\n";
    parts[2]+="Q\n";
    if(restore)parts[2]+="0.8 0.2 0.4 rg 0 0 2 2 re f\n";
    if(invalid==1)parts[2]+="Q\n";
    if(invalid==2)parts[2]+="/Missing Do\n";
    if(invalid==3)parts[2]+="2 /Bad m\n";
    if(invalid==4)parts[2]+="/Missing sh\n";
    encode(result,parts,unsigned(kind+primitive),encrypted,across_text);return result;
}
inline std::vector<unsigned char> rgb(const original& sample,unsigned dpi)
{
    const size_t width=12u*dpi/72u;
    // Independent exact rational PDF sample locations: denominator 8*dpi.
    // Triangle and rectangle predicates never round a projected vertex.
    const int64_t denominator=8ll*dpi;
    std::vector<unsigned char> pixels(width*width*3u,255u);
    const auto rectangle=[&](int64_t x,int64_t y,int x0,int y0,int x1,int y1) {
        return x>=x0*denominator && x<x1*denominator &&
            y>=y0*denominator && y<y1*denominator;
    };
    for(size_t y=0u;y<width;++y)for(size_t x=0u;x<width;++x) {
        unsigned first=0u,last=0u;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const int64_t px=(8ll*int64_t(x)+2ll*sx+1ll)*72ll;
            const int64_t py=12ll*denominator-(8ll*int64_t(y)+2ll*sy+1ll)*72ll;
            bool clip=true;
            if(sample.clip==1u)clip=rectangle(px,py,4,4,8,8);
            if(sample.clip==2u)clip=px>=3ll*denominator && py>=3ll*denominator && px+py<12ll*denominator;
            if(sample.clip==3u)clip=false;
            if(sample.clip==4u)clip=rectangle(px,py,5,4,8,8);
            if(sample.clip==5u)clip=px>=denominator && py>=denominator && px+py<6ll*denominator;
            bool inside=rectangle(px,py,2,2,10,10) && (!sample.even || sample.repeats%2u!=0u);
            if(!sample.contours.empty()) {
                int winding=0;
                for(const auto& r:sample.contours) {
                    const int64_t w=r.x1-r.x0,h=r.y1-r.y0;
                    const bool hit=r.triangle ? px>=r.x0*denominator && py>=r.y0*denominator &&
                        (px-r.x0*denominator)*h+(py-r.y0*denominator)*w<w*h*denominator :
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
