#ifndef QUANTAPDF_NATIVE_ORDERED_SCENE_FIXTURE_H
#define QUANTAPDF_NATIVE_ORDERED_SCENE_FIXTURE_H
#include "native_ttf_fixture.h"
#include "native_content_stream_fixture.h"
#include <array>
namespace native_ordered_scene {
using O=QPDFObjectHandle;
enum class text_presence { glyphs,empty,none };
struct clip_definition {
    size_t at=0u;
    bool after_empty_text=false;
    unsigned primitive=0u;
    size_t contours=1u,zeros=0u;
    bool shifted=false;
};
struct paint {double x,y,w,h;std::array<unsigned char,12> colors;bool image;bool clipped=false;};
struct original {std::string bytes;size_t decoded_bytes=0u,operations=0u,glyphs=0u,images=0u;std::vector<paint> paints;unsigned clip_kind=0u;};
inline original create(size_t count,size_t letters,unsigned permutation,bool encrypted,size_t depth=0u,int invalid=0,unsigned clip_kind=0u,size_t restore_at=SIZE_MAX,bool omit_images=false,text_presence text_mode=text_presence::glyphs,unsigned path_kind=0u,size_t path_repeats=1u,size_t zeros=0u,bool text_before_path=false,unsigned divisor=1u,clip_definition definition={})
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
    original result;result.clip_kind=clip_kind;std::array<std::string,3> parts;parts[0]="q ";
    std::string clip;
    if(clip_kind==1u || clip_kind==2u)clip=std::string("2.25 2.25 7.5 7.5 re ")+(clip_kind==1u?"W n ":"W* n ");
    if(clip_kind==3u)clip="2 2 m 10 2 l 8 10 l 2 10 l h W n ";
    if(clip_kind==4u)clip="2 2 m W* n ";
    if(definition.primitive!=0u && clip_kind>=1u && clip_kind<=3u) {
        const std::array<std::array<double,2>,4> vertices=clip_kind==3u?
            std::array<std::array<double,2>,4>{{{2,2},{10,2},{8,10},{2,10}}}:
            std::array<std::array<double,2>,4>{{{2.25,2.25},{9.75,2.25},{9.75,9.75},{2.25,9.75}}};
        const auto point=[&](size_t index) {return std::to_string(vertices[index][0]-(definition.shifted?0.125:0.0))+" "+std::to_string(vertices[index][1]-(definition.shifted?0.375:0.0));};
        clip=definition.shifted?"q 1 0 0 1 0.125 0.375 cm ":"";
        for(size_t contour=0u;contour<definition.contours;++contour) {
            clip+=point(0u)+" m ";
            for(size_t j=1u;j<vertices.size();++j) {
                const auto a=point(j-1u),b=point(j);
                if(definition.primitive==1u)clip+=b+" l ";
                if(definition.primitive==2u)clip+=a+" "+b+" "+b+" c ";
                if(definition.primitive==3u)clip+=b+" "+b+" v ";
                if(definition.primitive==4u)clip+=a+" "+b+" y ";
            }
            if(contour+1u==definition.contours)for(size_t z=0u;z<definition.zeros;++z)clip+=point(3u)+" l ";
            clip+="h ";
        }
        // Restoring the construction CTM preserves the current path.
        if(definition.shifted)clip+="Q ";
        clip+=clip_kind==2u?"W* n ":"W n ";
    }
    if(definition.after_empty_text)parts[0]+="BT ET ";
    if(definition.at==0u)parts[0]+=clip;
    bool clipped=clip_kind!=0u && definition.at==0u;
    const std::array<std::array<unsigned char,3>,3> colors={{{51,102,153},{204,51,102},{102,204,51}}};
    const char *rg[]={"0.2 0.4 0.6 rg ","0.8 0.2 0.4 rg ","0.4 0.8 0.2 rg "};
    for(size_t i=0u;i<count;++i) {
        auto& part=parts[std::min(size_t(2u),i*3u/count)];
        if(i==0u)for(size_t q=0u;q<depth;++q)part+="q ";
        if(i==restore_at) {
            for(size_t q=0u;q<=depth;++q)part+="Q ";
            for(size_t q=0u;q<=depth;++q)part+="q ";
            clipped=false;
        }
        if(i!=0u && i==definition.at) {part+=clip;clipped=clip_kind!=0u;}
        const size_t first_paint=result.paints.size();
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
                if(omit_images)continue;
                part+="q 2.25 0 0 2.5 "+std::to_string(x)+" "+std::to_string(y)+" cm "+name+" Do Q ";
                result.paints.push_back({x,y,2.25,2.5,samples,true});++result.operations;++result.images;
            } else if(item==1u && text && text_mode!=text_presence::none) {
                const size_t n=text_mode==text_presence::empty?0u:i==0u?letters:1u;const unsigned color=unsigned((i+1u)%3u);
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
                part+=rg[color];
                const double left=x+0.75+(divisor==1u?0.0:1.0/divisor);
                const double bottom=y+0.75+(divisor==1u?0.0:3.0/divisor);
                if(path_kind==0u)part+=std::to_string(left)+" "+std::to_string(bottom)+" 2.5 1.25 re f* ";
                else {
                    const std::array<std::string,4> v={
                        std::to_string(left)+" "+std::to_string(bottom),
                        std::to_string(left+2.5)+" "+std::to_string(bottom),
                        std::to_string(left+2.5)+" "+std::to_string(bottom+1.25),
                        std::to_string(left)+" "+std::to_string(bottom+1.25)};
                    for(size_t repeat=0u;repeat<path_repeats;++repeat) {
                        if(text_before_path && repeat==0u) {
                            // Real Text immediately before an arbitrary Fill;
                            // q/Q restores the Fill color at page-description level.
                            part+=std::string("q ")+rg[(color+1u)%3u]+"BT /F 2 Tf 1 1 Td <0051> Tj ET Q ";
                            std::array<unsigned char,12> glyph_ink{};
                            std::copy(colors[(color+1u)%3u].begin(),colors[(color+1u)%3u].end(),glyph_ink.begin());
                            result.paints.push_back({1.0+156.0/1024.0,1.0,185.0/1024.0,1.5,glyph_ink,false});
                            ++result.glyphs;++result.operations;
                        }
                        part+=v[0]+" m ";
                        if(path_kind==5u)part+="h h ";
                        for(size_t j=1u;j<v.size();++j) {
                            const auto& a=v[j-1u];const auto& b=v[j];
                            if(path_kind==1u || path_kind==5u)part+=b+" l ";
                            if(path_kind==2u)part+=a+" "+b+" "+b+" c ";
                            if(path_kind==3u)part+=b+" "+b+" v ";
                            if(path_kind==4u)part+=a+" "+b+" y ";
                        }
                        if(repeat+1u==path_repeats)for(size_t z=0u;z<zeros;++z)part+=v.back()+" l ";
                        part+="h ";
                    }
                    part+="f* ";
                }
                result.paints.push_back({left,bottom,2.5,1.25,ink,false});++result.operations;
            }
        }
        for(size_t at=first_paint;at<result.paints.size();++at)result.paints[at].clipped=clipped;
        if(i+1u==count)for(size_t q=0u;q<depth;++q)part+="Q ";
    }
    if(definition.at==count)parts[2]+=clip;
    parts[2]+="Q ";
    if(invalid==1)parts[2]+="Q ";
    if(invalid==2)parts[2]+="/Missing Do ";
    if(invalid==3)parts[2]+="1 1 2 2 re S ";
    if(invalid==5)parts[2]+="2 /Bad m ";
    if(invalid==6)parts[2]+="2 2 l ";
    if(invalid==7)parts[2]+="2 2 m BT ET ";
    if(invalid==8)parts[2]+="BT BT ET ET ";
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
            const double px=xx/scale,py=12.0-yy/scale;
            const bool inside=source.clip_kind==1u || source.clip_kind==2u?
                px>=2.25 && px<9.75 && py>=2.25 && py<9.75:
                source.clip_kind==3u?py>=2.0 && py<10.0 && px>=2.0 && px<10.0-(py-2.0)/4.0:false;
            hits+=unsigned(xx>=x0 && xx<x1 && yy>=y0 && yy<y1 && (!p.clipped || inside));
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
