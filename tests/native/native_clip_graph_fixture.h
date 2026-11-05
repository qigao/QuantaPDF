#ifndef QUANTAPDF_NATIVE_CLIP_GRAPH_FIXTURE_H
#define QUANTAPDF_NATIVE_CLIP_GRAPH_FIXTURE_H
#include "native_ttf_fixture.h"
#include "native_content_stream_fixture.h"
#include <array>
#include <algorithm>

namespace native_clip_graph {
using O=QPDFObjectHandle;
// Integer sixteenth-point regions are an independent sample oracle.
struct region {int left,bottom,right,top;};
struct paint {
    region bounds;
    std::array<unsigned char,12> colors;
    std::vector<region> clips;
    bool image=false,glyph=false;
};
struct options {
    unsigned scene=0u,order=0u,primitive=0u,leaves=4u;
    bool empty=false,prefix=true;
    size_t unused=0u,contours=1u,zeros=0u,depth=0u;
};
struct original {
    std::string bytes;
    size_t decoded_bytes=0u,operations=0u,images=0u,glyphs=0u;
    size_t leaves=0u,nodes=0u,states=0u;
    std::vector<paint> paints;
};
inline original create(options opt,bool encrypted,int invalid=0)
{
    auto pdf=QPDF::create();const auto base=native_ttf_fixture();
    pdf->processMemoryFile("Clip graph original",base.data(),base.size());
    auto root=pdf->getRoot().getKey("/Pages"),page=root.getKey("/Kids").getArrayItem(0);
    auto resources=page.getKey("/Resources");root.replaceKey("/Resources",resources);page.removeKey("/Resources");
    page.replaceKey("/MediaBox",O::parse("[0 0 12 12]"));
    resources.replaceKey("/ExtGState",O::newDictionary());
    resources.getKey("/Font").replaceKey("/Unused",resources.getKey("/Font").getKey("/F"));
    const std::array<unsigned char,12> samples={255,0,0,0,255,0,0,0,255,255,255,0};
    const std::string raw(reinterpret_cast<const char*>(samples.data()),samples.size());
    auto image=pdf->newStream(raw);auto dict=image.getDict();
    dict.replaceKey("/Type",O::newName("/XObject"));dict.replaceKey("/Subtype",O::newName("/Image"));
    dict.replaceKey("/Width",O::newInteger(2));dict.replaceKey("/Height",O::newInteger(2));
    dict.replaceKey("/BitsPerComponent",O::newInteger(8));dict.replaceKey("/ColorSpace",O::newName("/DeviceRGB"));
    auto xobjects=O::newDictionary();xobjects.replaceKey("/I",image);xobjects.replaceKey("/Unused",image);
    resources.replaceKey("/XObject",xobjects);
    auto blank=pdf->makeIndirectObject(O::parse("<< /Type /Page /MediaBox [0 0 5 4] >>"));
    blank.replaceKey("/Parent",root);blank.replaceKey("/Contents",pdf->newStream("\n"));
    root.replaceKey("/Kids",O::newArray({blank,page}));root.replaceKey("/Count",O::newInteger(2));

    const std::array<region,5> boxes={{{18,22,162,166},{68,44,132,148},
        {38,66,158,138},{40,30,120,118},{52,50,136,136}}};
    const char *inks[]={"0.2 0.4 0.6 rg ","0.8 0.2 0.4 rg ","0.4 0.8 0.2 rg "};
    const std::array<std::array<unsigned char,3>,3> colors={{{51,102,153},{204,51,102},{102,204,51}}};
    static const unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    original result;std::string content;std::vector<region> active;
    std::vector<std::vector<region>> saved;
    const auto save=[&]() {content+="q ";saved.push_back(active);};
    const auto restore=[&]() {content+="Q ";active=saved.back();saved.pop_back();};
    size_t bodies=0u;
    const auto body=[&]() {
        const int left=16*int(bodies+1u);
        for(unsigned kind:orders[opt.order%6u]) {
            paint p{};p.clips=active;
            if(kind==0u) {
                content+="q 0.75 0 0 3 "+std::to_string(left/16)+" 3 cm /I Do Q ";
                p.bounds={left,48,left+12,96};p.colors=samples;p.image=true;++result.images;
            } else if(kind==1u) {
                content+="q 0 g BT /F 2 Tf "+std::to_string(double(left+2)/16.0)+" 3 Td <0051> Tj ET Q ";
                p.bounds={left+2,48,left+2,72};p.glyph=true;++result.glyphs;
            } else {
                const size_t ink=bodies%3u;content+=std::string("q ")+inks[ink]+std::to_string(left/16)+" 2 0.75 7 re f* Q ";
                p.bounds={left,32,left+12,144};
                std::copy(colors[ink].begin(),colors[ink].end(),p.colors.begin());
            }
            result.paints.push_back(p);++result.operations;
        }
        ++bodies;
    };
    const auto define=[&](unsigned index,bool observed=true) {
        region r=boxes[index];if(opt.empty && index==2u)r.right=r.left;
        const bool parent=!active.empty();
        // Build with a translated CTM, then restore it before applying W/n.
        content+="q 1 0 0 1 0.125 0.375 cm ";
        const auto point=[&](int x,int y) {return std::to_string(double(x)/16.0-0.125)+" "+std::to_string(double(y)/16.0-0.375);};
        if(r.right==r.left)content+=point(r.left,r.bottom)+" m ";
        else for(size_t repeat=0u;repeat<(index==1u?opt.contours:1u);++repeat) {
            if(opt.primitive==0u) {
                content+=point(r.left,r.bottom)+" "+std::to_string(double(r.right-r.left)/16.0)+" "+std::to_string(double(r.top-r.bottom)/16.0)+" re ";
            } else {
                const std::array<std::string,4> v={point(r.left,r.bottom),point(r.right,r.bottom),point(r.right,r.top),point(r.left,r.top)};
                content+=v[0]+" m ";
                for(size_t j=1u;j<v.size();++j) {
                    if(opt.primitive==1u)content+=v[j]+" l ";
                    if(opt.primitive==2u)content+=v[j-1u]+" "+v[j]+" "+v[j]+" c ";
                    if(opt.primitive==3u)content+=v[j]+" "+v[j]+" v ";
                    if(opt.primitive==4u)content+=v[j-1u]+" "+v[j]+" y ";
                }
                if(index==1u && repeat+1u==opt.contours)
                    for(size_t z=0u;z<opt.zeros;++z)content+=v[3]+" l ";
                content+="h ";
            }
        }
        content+=index%2u==0u?"Q W n ":"Q W* n ";
        active.push_back(r);
        if(observed) {++result.leaves;++result.states;result.nodes+=parent?2u:1u;}
    };
    if(opt.prefix)body();
    if(opt.scene==1u) {
        for(unsigned i=0u;i<opt.leaves;++i) {save();define(i);body();restore();body();}
    } else {
        save();define(0u);
        for(size_t q=0u;q<opt.depth;++q)save();
        body();
        for(size_t i=0u;i<opt.unused;++i) {save();define(4u,false);restore();}
        if(opt.scene==0u) {
            for(unsigned i=1u;i<opt.leaves;++i) {save();define(i);body();}
            for(unsigned i=1u;i<opt.leaves;++i) {restore();body();}
        } else if(opt.scene==2u) {
            for(unsigned i=1u;i<opt.leaves;++i) {save();define(i);body();restore();body();}
        } else {
            save();define(1u);body();restore();
            save();define(2u);body();restore();
            for(size_t q=0u;q<opt.depth;++q)restore();
            restore();body();save();define(3u);body();restore();body();
        }
        if(opt.scene!=3u) {
            for(size_t q=0u;q<opt.depth;++q)restore();
            restore();body();
        }
    }
    if(invalid==1)content+="Q ";
    if(invalid==2)content+="/Missing Do ";
    if(invalid==3)content+="2 2 m BT ET ";
    if(invalid==4)content+="BT 1 Tr ET ";
    if(invalid==5)content+="0 0 1 1 re W f ";
    std::array<std::string,3> parts;
    size_t begin=0u;
    for(size_t i=0u;i<2u;++i) {
        const size_t cut=content.find(' ',content.size()*(i+1u)/3u)+1u;
        parts[i]=content.substr(begin,cut-begin)+"\n";begin=cut;
    }
    parts[2]=content.substr(begin)+"\n";
    std::vector<O> streams;
    for(size_t i=0u;i<parts.size();++i) {
        result.decoded_bytes+=parts[i].size();
        streams.push_back(native_content_fixture::stream(*pdf,parts[i],unsigned((opt.scene+opt.primitive+i)%8u)));
    }
    page.replaceKey("/Contents",O::newArray(streams));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.setStreamDataMode(qpdf_s_preserve);writer.setCompressStreams(false);
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto bytes=writer.getBufferSharedPointer();
    result.bytes.assign(reinterpret_cast<const char*>(bytes->getBuffer()),bytes->getSize());return result;
}
inline std::vector<unsigned char> rgb(const original& source,unsigned dpi)
{
    const size_t side=12u*dpi/72u;
    const int64_t unit=8ll*dpi;
    std::vector<unsigned char> out(side*side*3u,255u);
    const auto contains=[&](region r,int64_t x,int64_t y) {
        return 16*x>=r.left*unit && 16*x<r.right*unit && 16*y>r.bottom*unit && 16*y<=r.top*unit;
    };
    for(const auto& p:source.paints)for(size_t y=0u;y<side;++y)for(size_t x=0u;x<side;++x) {
        unsigned hits=0u;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const int64_t px=(8ll*int64_t(x)+2ll*sx+1ll)*72ll;
            const int64_t py=12*unit-(8ll*int64_t(y)+2ll*sy+1ll)*72ll;
            bool visible=p.glyph?
                1024*px>=(p.bounds.left*64+156)*unit && 1024*px<(p.bounds.left*64+341)*unit &&
                    16*py>p.bounds.bottom*unit && 16*py<=p.bounds.top*unit:
                contains(p.bounds,px,py);
            for(const auto& clip:p.clips)visible=visible && contains(clip,px,py);
            hits+=unsigned(visible);
        }
        size_t sample=0u;
        if(p.image) {
            const int64_t px=(2ll*int64_t(x)+1ll)*36ll,py=12ll*dpi-(2ll*int64_t(y)+1ll)*36ll;
            const size_t col=32*px<int64_t(p.bounds.left+p.bounds.right)*dpi?0u:1u;
            const size_t row=32*py>int64_t(p.bounds.bottom+p.bounds.top)*dpi?0u:1u;
            sample=2u*row+col;
        }
        for(size_t c=0u;c<3u;++c) {
            auto& value=out[(y*side+x)*3u+c];
            value=static_cast<unsigned char>((unsigned(p.colors[sample*3u+c])*hits+unsigned(value)*(16u-hits)+8u)/16u);
        }
    }
    return out;
}
}
#endif
