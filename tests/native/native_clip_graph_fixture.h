#ifndef QUANTAPDF_NATIVE_CLIP_GRAPH_FIXTURE_H
#define QUANTAPDF_NATIVE_CLIP_GRAPH_FIXTURE_H
#include "native_ttf_fixture.h"
#include "native_content_stream_fixture.h"
#include <array>
#include <algorithm>

namespace native_clip_graph {
using O=QPDFObjectHandle;
// Oracle rates are authored exact fractions, independently of the Native
// planner's binary32 decomposition and multiword arithmetic.
struct dpi_rate {
    int64_t numerator,denominator;
    dpi_rate(unsigned value):numerator(value),denominator(1) {}
    dpi_rate(int64_t n,int64_t d):numerator(n),denominator(d) {}
    explicit operator float() const {return float(numerator)/float(denominator);}
    bool operator==(const dpi_rate&) const = default;
};
// Integer sixteenth-point regions are an independent sample oracle.
struct region {int64_t left,bottom,right,top;};
struct paint {
    region bounds;
    std::array<unsigned char,12> colors;
    std::vector<region> clips;
    bool image=false,glyph=false;
    bool affine=false;
    std::array<int64_t,6> matrix{}; // original PDF image CTM, in sixteenths
};
struct options {
    unsigned scene=0u,order=0u,primitive=0u,leaves=4u;
    unsigned image_affine=0u,paint_mask=7u;
    bool empty=false,prefix=true,sparse=false,fringe=false,glyph_fringe=false,gray_image=false;
    int64_t width_ticks=192,height_ticks=192;
    int64_t media_left=0,media_bottom=0,ticks_per_point=16;
    int rotation=0;
    unsigned user_quarters=4u;
    bool crop=false,inherited_geometry=false;
    region crop_box{};
    size_t unused=0u,contours=1u,zeros=0u,depth=0u;
};
struct original {
    std::string bytes;
    size_t decoded_bytes=0u,operations=0u,images=0u,glyphs=0u;
    size_t leaves=0u,nodes=0u,states=0u;
    int64_t width_ticks=192,height_ticks=192,ticks_per_point=16;
    int64_t viewport_left=0,viewport_bottom=0,absolute_left=0,absolute_bottom=0;
    unsigned rotation=0u,user_quarters=4u;
    std::vector<paint> paints;
};
inline options viewport_case(options opt,unsigned kind)
{
    opt.fringe=true;opt.glyph_fringe=true;
    switch(kind) {
    case 0u:opt.media_left=36;opt.media_bottom=-20;opt.width_ticks=188;opt.height_ticks=180;break;
    case 1u:opt.media_left=-116;opt.media_bottom=12;opt.width_ticks=196;opt.height_ticks=204;opt.inherited_geometry=true;break;
    case 2u:opt.crop=true;opt.crop_box={20,28,180,188};break;
    case 3u:opt.media_left=-88;opt.media_bottom=-44;opt.width_ticks=196;opt.height_ticks=204;
        opt.crop=true;opt.crop_box={28,20,184,196};opt.inherited_geometry=true;break;
    case 4u:opt.media_left=36;opt.media_bottom=20;opt.width_ticks=188;opt.height_ticks=180;
        opt.crop=true;opt.crop_box={-16,-24,176,196};break;
    case 5u:opt.media_left=-52;opt.media_bottom=28;opt.width_ticks=196;opt.height_ticks=192;
        opt.crop=true;opt.crop_box={-20,-12,212,216};opt.inherited_geometry=true;break;
    case 6u:opt.width_ticks=196;opt.height_ticks=204;opt.crop=true;opt.crop_box={0,0,184,188};break;
    default:throw std::runtime_error("invalid viewport fixture");
    }
    return opt;
}
inline std::string decimal(int64_t value,int64_t den)
{
    const bool negative=value<0;
    uint64_t n=uint64_t(negative?-value:value),d=uint64_t(den);
    std::string out=(negative?"-":"")+std::to_string(n/d);n%=d;
    if(n!=0u) {
        out+='.';
        do {n*=10u;out+=char('0'+n/d);n%=d;}while(n!=0u);
    }
    return out;
}
inline options decimal_case(options opt,unsigned kind)
{
    opt.fringe=true;opt.glyph_fringe=false;opt.ticks_per_point=1000000000ll;
    opt.width_ticks=12137000000ll;opt.height_ticks=11293000000ll;
    switch(kind) {
    case 0u:opt.media_left=2103000000ll;opt.media_bottom=-1207000000ll;break;
    case 1u:opt.media_left=-7301000000ll;opt.media_bottom=609000000ll;
        opt.crop=true;opt.crop_box={1103000000ll,917000000ll,9331250000ll,10719000000ll};
        opt.inherited_geometry=true;break;
    case 2u:opt.width_ticks=12125000001ll;opt.height_ticks=11125000001ll;break;
    case 3u:opt.width_ticks=12124999999ll;opt.height_ticks=11124999999ll;break;
    case 4u:opt.width_ticks=12000000001ll;opt.height_ticks=11000000001ll;break;
    case 5u:opt.media_left=2103000000ll;opt.media_bottom=-1207000000ll;
        opt.crop=true;opt.crop_box={-331000000ll,729000000ll,11917000000ll,11893000000ll};break;
    default:throw std::runtime_error("invalid decimal viewport fixture");
    }
    return opt;
}
inline std::array<int64_t,6> image_matrix(unsigned kind,int64_t left)
{
    switch(kind) {
    case 1u:return {0,48,-32,0,left+48,48};
    case 2u:return {-32,0,0,48,left+48,48};
    case 3u:return {32,0,0,-48,left+16,96};
    case 4u:return {32,12,20,48,left,32};
    case 5u:return {40,24,-24,40,left+32,32};
    case 6u:return {20,32,40,-12,left,64};
    case 7u:return {32,16,16,9,left,48};
    case 8u:return {320,32,-48,224,left-112,-64};
    default:throw std::runtime_error("invalid affine image fixture");
    }
}
inline original create(options opt,bool encrypted,int invalid=0)
{
    auto pdf=QPDF::create();const auto base=native_ttf_fixture();
    pdf->processMemoryFile("Clip graph original",base.data(),base.size());
    auto root=pdf->getRoot().getKey("/Pages"),page=root.getKey("/Kids").getArrayItem(0);
    auto resources=page.getKey("/Resources");root.replaceKey("/Resources",resources);page.removeKey("/Resources");
    const auto box=[&](region r) {return O::parse("["+decimal(r.left,opt.ticks_per_point)+" "+
        decimal(r.bottom,opt.ticks_per_point)+" "+decimal(r.right,opt.ticks_per_point)+" "+
        decimal(r.top,opt.ticks_per_point)+"]");};
    auto geometry=opt.inherited_geometry?root:page;
    geometry.replaceKey("/MediaBox",box({opt.media_left,opt.media_bottom,opt.media_left+opt.width_ticks,opt.media_bottom+opt.height_ticks}));
    if(opt.inherited_geometry)page.removeKey("/MediaBox");
    if(opt.crop)geometry.replaceKey("/CropBox",box({opt.media_left+opt.crop_box.left,opt.media_bottom+opt.crop_box.bottom,
        opt.media_left+opt.crop_box.right,opt.media_bottom+opt.crop_box.top}));
    geometry.replaceKey("/Rotate",O::newInteger(opt.rotation));
    page.replaceKey("/UserUnit",O::parse(std::to_string(double(opt.user_quarters)/4.0)));
    resources.replaceKey("/ExtGState",O::newDictionary());
    resources.getKey("/Font").replaceKey("/Unused",resources.getKey("/Font").getKey("/F"));
    std::array<unsigned char,12> samples={255,0,0,0,255,0,0,0,255,255,255,0};
    std::string raw;
    if(opt.gray_image)for(size_t i=0u;i<4u;++i) {
        const auto gray=static_cast<unsigned char>(17u+68u*i);raw+=char(gray);
        samples[3u*i]=samples[3u*i+1u]=samples[3u*i+2u]=gray;
    } else raw.assign(reinterpret_cast<const char*>(samples.data()),samples.size());
    auto image=pdf->newStream(raw);auto dict=image.getDict();
    dict.replaceKey("/Type",O::newName("/XObject"));dict.replaceKey("/Subtype",O::newName("/Image"));
    dict.replaceKey("/Width",O::newInteger(2));dict.replaceKey("/Height",O::newInteger(2));
    dict.replaceKey("/BitsPerComponent",O::newInteger(8));dict.replaceKey("/ColorSpace",O::newName(opt.gray_image?"/DeviceGray":"/DeviceRGB"));
    auto xobjects=O::newDictionary();xobjects.replaceKey("/I",image);xobjects.replaceKey("/Unused",image);
    resources.replaceKey("/XObject",xobjects);
    auto blank=pdf->makeIndirectObject(O::parse("<< /Type /Page /MediaBox [0 0 5 4] >>"));
    blank.replaceKey("/Parent",root);blank.replaceKey("/Contents",pdf->newStream("\n"));
    blank.replaceKey("/CropBox",O::parse("[0 0 5 4]"));
    blank.replaceKey("/Rotate",O::newInteger(0));
    root.replaceKey("/Kids",O::newArray({blank,page}));root.replaceKey("/Count",O::newInteger(2));

    const std::array<region,5> boxes={{{18,22,162,166},{68,44,132,148},
        {38,66,158,138},{40,30,120,118},{52,50,136,136}}};
    const char *inks[]={"0.2 0.4 0.6 rg ","0.8 0.2 0.4 rg ","0.4 0.8 0.2 rg "};
    const std::array<std::array<unsigned char,3>,3> colors={{{51,102,153},{204,51,102},{102,204,51}}};
    static const unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    original result;result.width_ticks=opt.width_ticks;result.height_ticks=opt.height_ticks;result.ticks_per_point=opt.ticks_per_point;
    result.rotation=unsigned((opt.rotation%360+360)%360);result.user_quarters=opt.user_quarters;
    if(opt.crop) {
        result.viewport_left=std::max(int64_t(0),opt.crop_box.left);result.viewport_bottom=std::max(int64_t(0),opt.crop_box.bottom);
        result.width_ticks=std::min(opt.width_ticks,opt.crop_box.right)-result.viewport_left;
        result.height_ticks=std::min(opt.height_ticks,opt.crop_box.top)-result.viewport_bottom;
    }
    result.absolute_left=opt.media_left+result.viewport_left;result.absolute_bottom=opt.media_bottom+result.viewport_bottom;
    std::string content;std::vector<region> active;
    const bool translated=opt.media_left!=0 || opt.media_bottom!=0 || opt.crop;
    if(translated)content="q 1 0 0 1 "+decimal(opt.media_left,opt.ticks_per_point)+" "+
        decimal(opt.media_bottom,opt.ticks_per_point)+" cm ";
    std::vector<std::vector<region>> saved;
    size_t root_id=0u,next_id=0u;std::vector<size_t> saved_ids,observed_roots;
    const auto save=[&]() {content+="q ";saved.push_back(active);saved_ids.push_back(root_id);};
    const auto restore=[&]() {content+="Q ";active=saved.back();saved.pop_back();root_id=saved_ids.back();saved_ids.pop_back();};
    size_t bodies=0u;
    const auto body=[&]() {
        if(root_id!=0u && std::find(observed_roots.begin(),observed_roots.end(),root_id)==observed_roots.end()) {
            observed_roots.push_back(root_id);++result.states;
        }
        static const int sparse_left[5]={16,64,80,96,144};
        const int left=opt.sparse?sparse_left[bodies]:16*int(bodies+1u);
        for(unsigned kind:orders[opt.order%6u]) {
            if((opt.paint_mask&(1u<<kind))==0u)continue;
            paint p{};p.clips=active;
            if(kind==0u) {
                if(opt.image_affine!=0u) {
                    p.affine=true;p.matrix=image_matrix(opt.image_affine,left);
                    content+="q ";for(auto value:p.matrix)content+=decimal(value,16)+" ";content+="cm /I Do Q ";
                } else content+="q 0.75 0 0 3 "+std::to_string(left/16)+" 3 cm /I Do Q ";
                p.bounds={left,48,left+12,96};p.colors=samples;p.image=true;++result.images;
                if(p.affine) {
                    const auto& m=p.matrix;
                    const std::array<int64_t,4> x={m[4],m[4]+m[0],m[4]+m[0]+m[2],m[4]+m[2]};
                    const std::array<int64_t,4> y={m[5],m[5]+m[1],m[5]+m[1]+m[3],m[5]+m[3]};
                    p.bounds={*std::min_element(x.begin(),x.end()),*std::min_element(y.begin(),y.end()),
                        *std::max_element(x.begin(),x.end()),*std::max_element(y.begin(),y.end())};
                }
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
        // Intermediate wide-graph leaves cover the page. The last leaf is
        // restrictive, so truncating a second or later membership word
        // changes the independent RGB result.
        region r=index<boxes.size()?boxes[index]:region{0,0,192,192};
        if(index>=boxes.size() && index+1u==opt.leaves)r={84,76,100,102};
        if(opt.empty && index==2u)r.right=r.left;
        const bool parent=!active.empty();
        // Build with a translated CTM, then restore it before applying W/n.
        content+="q 1 0 0 1 0.125 0.375 cm ";
        const auto point=[&](int64_t x,int64_t y) {return std::to_string(double(x)/16.0-0.125)+" "+std::to_string(double(y)/16.0-0.375);};
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
        active.push_back(r);root_id=++next_id;
        if(observed) {++result.leaves;result.nodes+=parent?2u:1u;}
    };
    if(opt.fringe) {
        content+="q 0.2 0.4 0.6 rg 11.75 -0.25 0.5 12.5 re f Q ";
        paint edge{};edge.bounds={188,-4,196,196};
        std::copy(colors[0].begin(),colors[0].end(),edge.colors.begin());
        result.paints.push_back(edge);++result.operations;
        content+="q 12.25 0 0 0.5 0 -0.25 cm /I Do Q ";
        edge={};edge.bounds={0,-4,196,4};edge.colors=samples;edge.image=true;
        result.paints.push_back(edge);++result.operations;++result.images;
    }
    if(opt.prefix)body();
    if(opt.scene==1u) {
        for(unsigned i=0u;i<opt.leaves;++i) {save();define(i);body();restore();body();}
    } else {
        save();define(0u);
        for(size_t q=0u;q<opt.depth;++q)save();
        body();
        for(size_t i=0u;i<opt.unused;++i) {save();define(4u,false);restore();}
        if(opt.scene==0u) {
            for(unsigned i=1u;i<opt.leaves;++i) {save();define(i);if(!opt.sparse || i+1u==opt.leaves)body();}
            for(unsigned i=1u;i<opt.leaves;++i) {restore();if(!opt.sparse || i+1u==opt.leaves)body();}
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
    if(opt.glyph_fringe) {
        for(auto at:std::array<std::array<int64_t,2>,2>{{{result.viewport_left+result.width_ticks-4,result.viewport_bottom+96},
                                                  {result.viewport_left+20,result.viewport_bottom-12}}}) {
            content+="q 0 g BT /F 2 Tf "+std::to_string(double(at[0])/16.0)+" "+
                std::to_string(double(at[1])/16.0)+" Td <0051> Tj ET Q ";
            paint glyph{};glyph.glyph=true;glyph.bounds={at[0],at[1],at[0],at[1]+24};
            result.paints.push_back(glyph);++result.operations;++result.glyphs;
        }
    }
    if(translated)content+="Q ";
    if(invalid==1)content+="Q ";
    if(invalid==2)content+="/Missing Do ";
    if(invalid==3)content+="2 2 m BT ET ";
    if(invalid==4)content+="BT 1 Tr ET ";
    if(invalid==5)content+="0 0 1 1 re W f ";
    if(invalid==6)content+="q 1 2 2 4 1 1 cm /I Do Q ";
    if(invalid==7)content+="q 0 0 0 0 1 1 cm /I Do Q ";
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
inline double width_points(const original& source)
{return double((source.rotation==90u || source.rotation==270u?source.height_ticks:source.width_ticks)*source.user_quarters)/double(4*source.ticks_per_point);}
inline double height_points(const original& source)
{return double((source.rotation==90u || source.rotation==270u?source.width_ticks:source.height_ticks)*source.user_quarters)/double(4*source.ticks_per_point);}
inline size_t width(const original& source,dpi_rate dpi)
{return size_t((uint64_t(source.rotation==90u || source.rotation==270u?source.height_ticks:source.width_ticks)*source.user_quarters*uint64_t(dpi.numerator)+288u*uint64_t(source.ticks_per_point)*uint64_t(dpi.denominator)-1u)/(288u*uint64_t(source.ticks_per_point)*uint64_t(dpi.denominator)));}
inline size_t height(const original& source,dpi_rate dpi)
{return size_t((uint64_t(source.rotation==90u || source.rotation==270u?source.width_ticks:source.height_ticks)*source.user_quarters*uint64_t(dpi.numerator)+288u*uint64_t(source.ticks_per_point)*uint64_t(dpi.denominator)-1u)/(288u*uint64_t(source.ticks_per_point)*uint64_t(dpi.denominator)));}
inline std::vector<unsigned char> rgb(const original& source,dpi_rate dpi,bool truncate_glyph_viewport=false,bool omit_glyph_origin=false)
{
    const size_t w=width(source,dpi),h=height(source,dpi);
    // Invert the authored page transform at each physical sample using
    // rational sixteenth-point coordinates. No production geometry input.
    const int64_t frame=dpi.numerator*source.user_quarters;
    const int64_t den=frame*source.ticks_per_point;
    const auto authored=[&](int64_t x,int64_t y) {
        int64_t px=0,py=0;
        switch(source.rotation) {
        case 0u:px=x;py=source.height_ticks*frame-y;break;
        case 90u:px=y;py=x;break;
        case 180u:px=source.width_ticks*frame-x;py=y;break;
        case 270u:px=source.width_ticks*frame-y;py=source.height_ticks*frame-x;break;
        default:throw std::runtime_error("invalid reference page turn");
        }
        return std::array<int64_t,2>{px+source.viewport_left*frame,py+source.viewport_bottom*frame};
    };
    const auto horizontal=[&](int64_t lo,int64_t hi,int64_t x) {
        return source.rotation<180u?x>=lo && x<hi:x>lo && x<=hi;
    };
    const auto vertical=[&](int64_t lo,int64_t hi,int64_t y) {
        return source.rotation==90u || source.rotation==180u?y>=lo && y<hi:y>lo && y<=hi;
    };
    std::vector<unsigned char> out(w*h*3u,255u);
    const auto contains=[&](region r,int64_t x,int64_t y) {
        return horizontal(r.left*den,r.right*den,16*x) && vertical(r.bottom*den,r.top*den,16*y);
    };
    for(const auto& p:source.paints) {
        // Independently project the authored parallelogram's integer corners.
        // A rational horizontal ray supplies the same half-open screen edges
        // for reflected and rotated images, without a floating inverse.
        const auto display=[&](int64_t x,int64_t y) {
            if(source.rotation==0u)return std::array<int64_t,2>{x,-y};
            if(source.rotation==90u)return std::array<int64_t,2>{y,x};
            if(source.rotation==180u)return std::array<int64_t,2>{-x,y};
            return std::array<int64_t,2>{-y,-x};
        };
        const auto& m=p.matrix;
        const std::array<std::array<int64_t,2>,4> quad={display(m[4],m[5]),display(m[4]+m[0],m[5]+m[1]),
            display(m[4]+m[0]+m[2],m[5]+m[1]+m[3]),display(m[4]+m[2],m[5]+m[3])};
        const auto affine_contains=[&](int64_t x,int64_t y) {
            const auto point=display(x,y);const int64_t px=16*point[0],py=16*point[1];bool inside=false;
            for(size_t i=0u;i<4u;++i) {
                const auto a=quad[i],b=quad[(i+1u)%4u];
                const int64_t cross=(b[0]-a[0])*(py-a[1]*den)-(b[1]-a[1])*(px-a[0]*den);
                if((a[1]*den<=py && py<b[1]*den && cross>0) ||
                   (b[1]*den<=py && py<a[1]*den && cross<0))inside=!inside;
            }
            return inside;
        };
        for(size_t y=0u;y<h;++y)for(size_t x=0u;x<w;++x) {
        unsigned hits=0u;
        for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
            const auto at=authored((8ll*int64_t(x)+2ll*sx+1ll)*36ll*source.ticks_per_point*dpi.denominator,
                (8ll*int64_t(y)+2ll*sy+1ll)*36ll*source.ticks_per_point*dpi.denominator);
            const int64_t px=at[0],py=at[1];
            const int64_t gx=px-(omit_glyph_origin?source.absolute_left*frame:0ll);
            const int64_t gy=py-(omit_glyph_origin?source.absolute_bottom*frame:0ll);
            bool visible=p.glyph?
                horizontal((p.bounds.left*64+156)*den,(p.bounds.left*64+341)*den,1024*gx) &&
                    vertical(p.bounds.bottom*den,p.bounds.top*den,16*gy):
                p.affine?affine_contains(px,py):contains(p.bounds,px,py);
            visible=visible && horizontal(source.viewport_left*frame,(source.viewport_left+source.width_ticks)*frame,px) &&
                vertical(source.viewport_bottom*frame,(source.viewport_bottom+source.height_ticks)*frame,py);
            if(p.glyph && truncate_glyph_viewport)
                visible=visible && horizontal(source.viewport_left*frame,
                    (source.viewport_left+(source.width_ticks/source.ticks_per_point)*source.ticks_per_point)*frame,px) &&
                    vertical((source.viewport_bottom+source.height_ticks%source.ticks_per_point)*frame,
                        (source.viewport_bottom+source.height_ticks)*frame,py);
            for(const auto& clip:p.clips)visible=visible && contains(clip,px,py);
            hits+=unsigned(visible);
        }
        size_t sample=0u;
        if(p.image) {
            const auto at=authored((2ll*int64_t(x)+1ll)*144ll*source.ticks_per_point*dpi.denominator,
                (2ll*int64_t(y)+1ll)*144ll*source.ticks_per_point*dpi.denominator);
            if(p.affine) {
                const int64_t dx=16*at[0]-m[4]*den,dy=16*at[1]-m[5]*den;
                int64_t u=m[3]*dx-m[2]*dy,v=m[0]*dy-m[1]*dx,d=(m[0]*m[3]-m[1]*m[2])*den;
                if(d<0) {u=-u;v=-v;d=-d;}
                const size_t col=(source.rotation<180u?2*u<d:2*u<=d)?0u:1u;
                const size_t row=(source.rotation==90u || source.rotation==180u?2*v>=d:2*v>d)?0u:1u;
                sample=2u*row+col;
            } else {
            const int64_t mx=int64_t(p.bounds.left+p.bounds.right)*den;
            const int64_t my=int64_t(p.bounds.bottom+p.bounds.top)*den;
            // Rotate reorients source texels before nearest-neighbor lookup;
            // ties therefore follow the corresponding physical axis.
            const size_t col=(source.rotation<180u?32*at[0]<mx:32*at[0]<=mx)?0u:1u;
            const size_t row=(source.rotation==90u || source.rotation==180u?32*at[1]>=my:32*at[1]>my)?0u:1u;
            sample=2u*row+col;
            }
        }
        for(size_t c=0u;c<3u;++c) {
            auto& value=out[(y*w+x)*3u+c];
            value=static_cast<unsigned char>((unsigned(p.colors[sample*3u+c])*hits+unsigned(value)*(16u-hits)+8u)/16u);
        }
    }
    }
    return out;
}
}
#endif
