#include "qpdf_document_internal.h"
#include "base14_metrics.h"
#include "qpdf_text.h"
#include "ttf_font.h"
#include "snapshot/text_snapshot.h"
#include <qpdf/Pipeline.hh>
#include <qpdf/QPDFExc.hh>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <limits>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>

namespace {
using Obj = QPDFObjectHandle;
constexpr size_t byte_limit = 8u*1024u*1024u;
struct failure { quantapdf_status status; };
void need(bool ok, quantapdf_status status = QUANTAPDF_ERROR_FORMAT)
{ if (!ok) throw failure{status}; }
void supported(bool ok) { need(ok, QUANTAPDF_ERROR_UNSUPPORTED); }
double number(Obj v)
{ need(v.isNumber()); double n = v.getNumericValue(); need(std::isfinite(n)); return n; }
class sink final : public Pipeline {
public:
    explicit sink(size_t limit) : Pipeline("native text", nullptr), limit(limit) {}
    void write(unsigned char const *data, size_t count) override
    { supported(count <= limit-bytes.size()); bytes.append(reinterpret_cast<char const *>(data),count); }
    void finish() override { finished = true; }
    std::string bytes;
    size_t limit;
    bool finished = false;
};
std::string decode(Obj stream, size_t limit)
{
    need(stream.isStream()); auto d = stream.getDict();
    supported(d.getKey("/F").isNull() && d.getKey("/FFilter").isNull() && d.getKey("/FDecodeParms").isNull());
    Obj f = d.getKey("/Filter");
    need(f.isNull() || f.isName() || f.isArray());
    auto check = [](Obj v) { need(v.isName()); supported(v.getName() != "/Crypt"); };
    if (f.isName()) check(f);
    if (f.isArray()) { supported(f.getArrayNItems() <= 16); for (int i=0;i<f.getArrayNItems();++i) check(f.getArrayItem(i)); }
    supported(stream.pipeStreamData(nullptr,nullptr,0,qpdf_dl_specialized,true,true));
    sink output(limit); bool filtered = false;
    need(stream.pipeStreamData(&output,&filtered,0,qpdf_dl_specialized,true,true) && filtered && output.finished);
    return std::move(output.bytes);
}
void utf8(std::string& out, unsigned cp)
{
    need(cp > 0 && cp <= 0x10ffffu && !(cp >= 0xd800u && cp <= 0xdfffu));
    if (cp < 0x80u) out += static_cast<char>(cp);
    else if (cp < 0x800u) { out += static_cast<char>(0xc0u|(cp>>6)); out += static_cast<char>(0x80u|(cp&63)); }
    else if (cp < 0x10000u) { out += static_cast<char>(0xe0u|(cp>>12)); out += static_cast<char>(0x80u|((cp>>6)&63)); out += static_cast<char>(0x80u|(cp&63)); }
    else { out += static_cast<char>(0xf0u|(cp>>18)); out += static_cast<char>(0x80u|((cp>>12)&63)); out += static_cast<char>(0x80u|((cp>>6)&63)); out += static_cast<char>(0x80u|(cp&63)); }
}
std::string unicode(std::string const& bytes)
{
    need(!bytes.empty() && bytes.size()%2u == 0); supported(bytes.size() <= 64u);
    std::string result;
    auto unit = [&](size_t i) { return static_cast<unsigned char>(bytes[i])*256u+static_cast<unsigned char>(bytes[i+1u]); };
    for (size_t i=0;i<bytes.size();i+=2u) {
        unsigned cp=unit(i);
        if (cp>=0xd800u && cp<=0xdbffu) { need(i+3u<bytes.size()); unsigned low=unit(i+2u); need(low>=0xdc00u && low<=0xdfffu); cp=0x10000u+((cp-0xd800u)<<10)+(low-0xdc00u); i+=2u; }
        utf8(result,cp);
    }
    return result;
}

/* A CMap is data, never executable PostScript. The lexer consumes prologue
 * tokens but only the three mapping sections below can affect decoding. */
struct token { std::string value; bool hex=false; };
class cmap_lexer {
public:
    explicit cmap_lexer(std::string const& bytes) : bytes(bytes) {}
    token next()
    {
        while (at<bytes.size()) {
            unsigned char c=static_cast<unsigned char>(bytes[at]);
            if (c==0 || c==9 || c==10 || c==12 || c==13 || c==32) { ++at; continue; }
            if (c=='%') { while (at<bytes.size() && bytes[at]!='\n' && bytes[at]!='\r') ++at; continue; }
            break;
        }
        if (at==bytes.size()) return {};
        char c=bytes[at++];
        if (c=='<' && at<bytes.size() && bytes[at]!='<') {
            std::string result; int high=-1; bool ended=false;
            while (at<bytes.size()) {
                unsigned char v=static_cast<unsigned char>(bytes[at++]);
                if (v=='>') { ended=true; break; }
                if (v==0 || v==9 || v==10 || v==12 || v==13 || v==32) continue;
                int n=v>='0' && v<='9' ? v-'0' : v>='a' && v<='f' ? v-'a'+10 : v>='A' && v<='F' ? v-'A'+10 : -1;
                need(n>=0); if (high<0) high=n; else { result+=static_cast<char>(high*16+n); high=-1; }
                supported(result.size()<=64u);
            }
            need(ended && high<0 && !result.empty()); return {result,true};
        }
        if (c=='(') {
            size_t depth=1;
            while (at<bytes.size() && depth) {
                char v=bytes[at++];
                if (v=='\\') { need(at<bytes.size()); ++at; }
                else if (v=='(') ++depth; else if (v==')') --depth;
            }
            need(depth==0); return {"(string)",false};
        }
        if ((c=='<' || c=='>') && at<bytes.size() && bytes[at]==c) { ++at; return {std::string(2,c),false}; }
        if (c=='[' || c==']' || c=='{' || c=='}') return {std::string(1,c),false};
        std::string result(1,c);
        while (at<bytes.size()) {
            char v=bytes[at];
            if (v==0 || v==' ' || v=='\t' || v=='\r' || v=='\n' || v=='\f' || v=='%' || v=='<' || v=='>' || v=='[' || v==']' || v=='(' || v=='/' || v=='{' || v=='}') break;
            result+=v; ++at;
        }
        return {result,false};
    }
private:
    std::string const& bytes; size_t at=0;
};
unsigned code(token const& t, size_t width)
{ need(t.hex && t.value.size()==width); unsigned n=0; for (unsigned char c:t.value) n=n*256u+c; return n; }
struct cmap {
    size_t width=0;
    std::vector<std::pair<unsigned,unsigned>> spaces;
    std::map<unsigned,std::string> values;
    explicit cmap(std::string const& bytes)
    {
        cmap_lexer lexer(bytes); token previous; size_t work=0; bool began=false,ended=false;
        for (;;) {
            supported(++work<=200000u); token t=lexer.next(); if (t.value.empty()) break;
            if (!t.hex && t.value=="usecmap") supported(false);
            if (!t.hex && t.value=="begincmap") { need(!began && !ended); began=true; }
            if (!t.hex && t.value=="endcmap") { need(began && !ended); ended=true; }
            if (!t.hex && (t.value=="begincodespacerange" || t.value=="beginbfchar" || t.value=="beginbfrange")) {
                need(began && !ended && !previous.hex && !previous.value.empty());
                unsigned count=0; for (char c:previous.value) { need(c>='0' && c<='9'); supported(count<=6553u); count=count*10u+static_cast<unsigned>(c-'0'); }
                supported(count<=65536u);
                for (unsigned i=0;i<count;++i) {
                    token first=lexer.next(); need(first.hex); supported(first.value.size()==1u || first.value.size()==2u);
                    if (!width) width=first.value.size();
                    supported(width==first.value.size());
                    unsigned low=code(first,width);
                    if (t.value=="beginbfchar") { put(low,unicode_hex(lexer.next())); continue; }
                    unsigned high=code(lexer.next(),width); need(low<=high);
                    if (t.value=="begincodespacerange") { supported(spaces.size()<256u); for (auto const& s:spaces) need(high<s.first || low>s.second); spaces.emplace_back(low,high); continue; }
                    supported(high-low < 65536u); token dest=lexer.next();
                    if (!dest.hex && dest.value=="[") {
                        for (unsigned j=low;j<=high;++j) put(j,unicode_hex(lexer.next()));
                        token end=lexer.next(); need(!end.hex && end.value=="]");
                    } else {
                        need(dest.hex); std::string value=dest.value;
                        for (unsigned j=low;j<=high;++j) {
                            put(j,unicode(value));
                            if (j!=high) { size_t n=value.size(); while (n && static_cast<unsigned char>(value[n-1u])==255u) value[--n]=0; need(n>0); value[n-1u]=static_cast<char>(static_cast<unsigned char>(value[n-1u])+1u); }
                        }
                    }
                }
                std::string expected=t.value=="beginbfchar" ? "endbfchar" : t.value=="beginbfrange" ? "endbfrange" : "endcodespacerange";
                token end=lexer.next(); need(!end.hex && end.value==expected);
            } else if (!t.hex && (t.value=="beginnotdefchar" || t.value=="beginnotdefrange" || t.value=="begincidchar" || t.value=="begincidrange")) supported(false);
            if (!t.hex && (t.value=="/WMode" || t.value=="/CMapType")) {
                token mode=lexer.next();
                supported(!mode.hex && mode.value==(t.value=="/WMode"?"0":"2"));
            }
            supported(!t.hex && t.value!="/UseCMap");
            bool literal=!t.value.empty() && t.value.front()=='/';
            if (!literal) {
                size_t start=(t.value.front()=='+' || t.value.front()=='-')?1u:0u;
                bool digit=false,dot=false,valid=true;
                for(size_t i=start;i<t.value.size();++i) {
                    char c=t.value[i];
                    if(c>='0' && c<='9') digit=true;
                    else if(c=='.' && !dot) dot=true;
                    else valid=false;
                }
                literal=valid && digit;
            }
            static const char *boilerplate[]={"(string)","<<",">>","findresource","begin","dict","def","end","currentdict","defineresource","pop","CMapName","begincmap","endcmap","begincodespacerange","beginbfchar","beginbfrange"};
            for(auto word:boilerplate) if(t.value==word) literal=true;
            supported(literal);
            previous=std::move(t);
        }
        need(began && ended && width && !spaces.empty());
        for (auto const& value:values) need(in_space(value.first));
    }
    bool in_space(unsigned c) const { for (auto const& s:spaces) if(c>=s.first && c<=s.second) return true; return false; }
    static std::string unicode_hex(token const& t) { need(t.hex); return unicode(t.value); }
    void put(unsigned c,std::string value) { supported(values.size()<65536u); need(values.emplace(c,std::move(value)).second); }
};
unsigned winansi_metric_code(unsigned c)
{
    // PDF WinAnsi uses /bullet in the positions unassigned by Windows-1252.
    if(c==127u || c==129u || c==141u || c==143u || c==144u || c==157u) return 149u;
    if(c==160u) return 32u;
    if(c==173u) return 45u;
    return c;
}
unsigned winansi(unsigned c)
{
    c=winansi_metric_code(c);
    static constexpr unsigned special[32]={0x20ac,0,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0,0x17d,0,0,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0,0x17e,0x178};
    if(c>=128u && c<160u) { supported(special[c-128u]!=0); return special[c-128u]; }
    supported(c>=32u && c!=127u); return c;
}
struct font {
    size_t width=1;
    std::map<unsigned,std::string> mapping;
    std::map<unsigned,double> widths;
    std::set<unsigned> differences;
    std::vector<std::pair<unsigned,unsigned>> spaces;
    bool has_cmap=false, ansi=false, standard=true, base14=false, cid=false;
    double default_width=0, ascent=0, descent=0;
    quantapdf_composer_font base=QUANTAPDF_COMPOSER_FONT_HELVETICA;
    std::string outline_bytes;
    quantapdf::detail::ttf_font_face outline_face;
    std::vector<uint16_t> cid_to_gid;
    explicit font(Obj object, bool geometry,bool outlines=false)
    {
        need(object.isDictionary()); Obj subtype=object.getKey("/Subtype"); need(subtype.isName());
        std::string type=subtype.getName(); supported(type=="/Type1" || type=="/TrueType" || type=="/Type0");
        cid=type=="/Type0"; Obj metrics=object;
        Obj unicode_map=object.getKey("/ToUnicode");
        if (!unicode_map.isNull()) {
            need(unicode_map.isStream()); supported(unicode_map.getDict().getKey("/UseCMap").isNull());
            cmap parsed(decode(unicode_map,1024u*1024u)); width=parsed.width; mapping=std::move(parsed.values); spaces=std::move(parsed.spaces); has_cmap=true;
        }
        if (cid) {
            Obj encoding=object.getKey("/Encoding"); supported(encoding.isName() && encoding.getName()=="/Identity-H" && has_cmap && width==2u);
            Obj descendants=object.getKey("/DescendantFonts"); need(descendants.isArray() && descendants.getArrayNItems()==1);
            metrics=descendants.getArrayItem(0); need(metrics.isDictionary());
            Obj sub=metrics.getKey("/Subtype"); supported(sub.isName() && (sub.getName()=="/CIDFontType0" || sub.getName()=="/CIDFontType2"));
            default_width=1000; Obj dw=metrics.getKey("/DW"); if (!dw.isNull()) default_width=number(dw);
            Obj w=metrics.getKey("/W");
            if (!w.isNull()) {
                need(w.isArray()); supported(w.getArrayNItems()<=65536);
                for (int i=0;i<w.getArrayNItems();) {
                    unsigned first=cid_code(w.getArrayItem(i++)); need(i<w.getArrayNItems()); Obj next=w.getArrayItem(i++);
                    if (next.isArray()) {
                        supported(next.getArrayNItems()<=65536);
                        for (int j=0;j<next.getArrayNItems();++j) { need(first+static_cast<unsigned>(j)<=65535u); put_width(first+static_cast<unsigned>(j),number(next.getArrayItem(j))); }
                    } else {
                        unsigned last=cid_code(next); need(first<=last && i<w.getArrayNItems()); double n=number(w.getArrayItem(i++));
                        for (unsigned j=first;j<=last;++j) put_width(j,n);
                    }
                }
            }
        } else {
            supported(width==1u);
            Obj encoding=object.getKey("/Encoding"), diff=Obj::newNull();
            if (encoding.isDictionary()) { diff=encoding.getKey("/Differences"); encoding=encoding.getKey("/BaseEncoding"); }
            if (!encoding.isNull()) { need(encoding.isName()); supported(has_cmap || encoding.getName()=="/WinAnsiEncoding" || encoding.getName()=="/StandardEncoding"); ansi=encoding.getName()=="/WinAnsiEncoding"; standard=encoding.getName()=="/StandardEncoding"; }
            if (!diff.isNull()) {
                need(diff.isArray()); supported(diff.getArrayNItems()<=512); int n=-1;
                for (int i=0;i<diff.getArrayNItems();++i) { Obj v=diff.getArrayItem(i); if(v.isInteger()) { n=static_cast<int>(cid_code(v)); need(n<=255); } else { need(v.isName() && n>=0 && n<=255); differences.insert(static_cast<unsigned>(n++)); } }
            }
            Obj name=object.getKey("/BaseFont");
            if (name.isName()) {
                static const char *names[]={"/Helvetica","/Helvetica-Bold","/Helvetica-Oblique","/Helvetica-BoldOblique","/Times-Roman","/Times-Bold","/Times-Italic","/Times-BoldItalic","/Courier","/Courier-Bold","/Courier-Oblique","/Courier-BoldOblique"};
                static constexpr quantapdf_composer_font fonts[]={QUANTAPDF_COMPOSER_FONT_HELVETICA,QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD,QUANTAPDF_COMPOSER_FONT_HELVETICA_OBLIQUE,QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD_OBLIQUE,QUANTAPDF_COMPOSER_FONT_TIMES_ROMAN,QUANTAPDF_COMPOSER_FONT_TIMES_BOLD,QUANTAPDF_COMPOSER_FONT_TIMES_ITALIC,QUANTAPDF_COMPOSER_FONT_TIMES_BOLD_ITALIC,QUANTAPDF_COMPOSER_FONT_COURIER,QUANTAPDF_COMPOSER_FONT_COURIER_BOLD,QUANTAPDF_COMPOSER_FONT_COURIER_OBLIQUE,QUANTAPDF_COMPOSER_FONT_COURIER_BOLD_OBLIQUE};
                for(size_t i=0;i<12u;++i) if(name.getName()==names[i]) { base14=true; base=fonts[i]; }
                if (!has_cmap) supported(name.getName()!="/Symbol" && name.getName()!="/ZapfDingbats");
            }
            supported(has_cmap || !encoding.isNull() || base14);
            Obj ws=object.getKey("/Widths");
            if (!ws.isNull()) {
                need(ws.isArray()); supported(ws.getArrayNItems()<=256); unsigned first=cid_code(object.getKey("/FirstChar"));
                need(first+static_cast<unsigned>(ws.getArrayNItems())<=256u);
                for(int i=0;i<ws.getArrayNItems();++i) put_width(first+static_cast<unsigned>(i),number(ws.getArrayItem(i)));
            }
            Obj descriptor=object.getKey("/FontDescriptor");
            if (!descriptor.isNull()) { need(descriptor.isDictionary()); Obj missing=descriptor.getKey("/MissingWidth"); if(!missing.isNull()) default_width=number(missing); }
        }
        need(default_width>=0);
        if(outlines) {
            supported(cid && metrics.getKey("/Subtype").isNameAndEquals("/CIDFontType2"));
            Obj descriptor=metrics.getKey("/FontDescriptor");need(descriptor.isDictionary());
            Obj file=descriptor.getKey("/FontFile2");supported(file.isStream() &&
                descriptor.getKey("/FontFile").isNull() && descriptor.getKey("/FontFile3").isNull());
            outline_bytes=decode(file,2u*1024u*1024u);
            const auto status=quantapdf::detail::ttf_font_face::parse(
                reinterpret_cast<const unsigned char*>(outline_bytes.data()),outline_bytes.size(),&outline_face);
            need(status==QUANTAPDF_OK,status);
            supported(outline_face.outline_kind==quantapdf::detail::sfnt_outline_kind::true_type);
            Obj map=metrics.getKey("/CIDToGIDMap");
            if(!map.isNull() && !map.isNameAndEquals("/Identity")) {
                supported(map.isStream());const auto bytes=decode(map,131072u);need(bytes.size()%2u==0u);
                for(size_t i=0u;i<bytes.size();i+=2u)
                    cid_to_gid.push_back(static_cast<uint16_t>(
                        static_cast<unsigned char>(bytes[i])*256u+static_cast<unsigned char>(bytes[i+1u])));
                supported(!cid_to_gid.empty());
            }
        }
        if (geometry) {
            Obj descriptor=metrics.getKey("/FontDescriptor");
            if (descriptor.isNull()) {
                supported(base14);
                quantapdf::detail::base14_vertical_metrics(base,&ascent,&descent);
            } else {
                need(descriptor.isDictionary());
                Obj a=descriptor.getKey("/Ascent"), d=descriptor.getKey("/Descent");
                supported(!a.isNull() && !d.isNull());
                ascent=number(a); descent=number(d);
            }
            need(ascent>descent);
        }
    }
    static unsigned cid_code(Obj v) { need(v.isInteger() && v.getIntValue()>=0 && v.getIntValue()<=65535); return static_cast<unsigned>(v.getIntValue()); }
    void put_width(unsigned c,double n) { need(n>=0); need(widths.emplace(c,n).second); }
    std::string text(unsigned c) const
    {
        auto it=mapping.find(c); if(it!=mapping.end()) return it->second;
        if (has_cmap) {
            bool valid=false;
            for (auto const& space:spaces) if(c>=space.first && c<=space.second) valid=true;
            need(valid);
            // Missing mapping is unknown Unicode, including an unmapped
            // continuation glyph in a Composer cluster. Never infer from CID.
            return "\xef\xbf\xbd";
        }
        supported(!cid && differences.count(c)==0);
        std::string result;
        if (!ansi) { supported(c>=32u && c<=126u); utf8(result,c==39u?0x2019u:c==96u?0x2018u:c); }
        else utf8(result,winansi(c));
        return result;
    }
    double advance(unsigned c) const
    {
        auto it=widths.find(c); if(it!=widths.end()) return it->second;
        if(base14) {
            supported(ansi || standard);
            unsigned metric=ansi?winansi_metric_code(c):c==39u?146u:c==96u?145u:c;
            return quantapdf::detail::base14_glyph_width(static_cast<unsigned char>(metric),base);
        }
        return default_width;
    }
};
using matrix=std::array<double,6>;
constexpr matrix identity={1,0,0,1,0,0};
matrix concat(matrix const& p,matrix const& c)
{
    matrix r={p[0]*c[0]+p[2]*c[1],p[1]*c[0]+p[3]*c[1],p[0]*c[2]+p[2]*c[3],p[1]*c[2]+p[3]*c[3],p[0]*c[4]+p[2]*c[5]+p[4],p[1]*c[4]+p[3]*c[5]+p[5]};
    for(double v:r) need(std::isfinite(v));
    return r;
}
matrix read_matrix(Obj v)
{ if(v.isNull()) return identity; need(v.isArray() && v.getArrayNItems()==6); matrix r; for(int i=0;i<6;++i) r[static_cast<size_t>(i)]=number(v.getArrayItem(i)); return r; }
struct graphics {
    matrix ctm=identity;
    font *face=nullptr;
    double size=0, character=0, word=0, scale=1, leading=0, rise=0;
    std::array<double,3> fill={0,0,0};
    double opacity=1;
    int fill_space=1;
    bool color_supported=true;
};
struct character_value {
    quantapdf_quad quad;
    float font_size,dx,dy;
    uint32_t codepoint,argb;
    bool break_before;
};
struct projection {
    QPDF parser;
    quantapdf_qpdf_page_geometry const *geometry;
    std::vector<character_value> characters;
    bool pending_break=false;
    bool outline_mode=false;
    double outline_height=0.0;
    size_t outline_glyphs=0u,outline_commands=0u;
    std::vector<quantapdf::detail::source_outline_paint> outline_paints;
    // Direct dictionaries have no ObjGen identity. Cache the handle identity
    // instead; heap ownership keeps font pointers stable across vector growth.
    std::vector<std::pair<Obj,std::unique_ptr<font>>> fonts;
    std::set<QPDFObjGen> active;
    size_t work=0,content_bytes=0,font_bytes=0,font_entries=0;
    std::string output;
    bool previous=false;
    double end_x=0,end_y=0,dir_x=1,dir_y=0,last_height=0;
    explicit projection(quantapdf_qpdf_page_geometry const *geometry=nullptr) : geometry(geometry)
    { parser.emptyPDF(); parser.setSuppressWarnings(true); }
    void step() { supported(++work<=1000000u); }
    void append(std::string const& value) { supported(value.size()<=byte_limit-output.size()); output+=value; }
    font *get_font(Obj object)
    {
        need(object.isDictionary());
        for (auto const& cached:fonts)
            if (cached.first.isSameObjectAs(object)) return cached.second.get();
        supported(fonts.size()<(outline_mode?1u:256u));
        Obj cmap=object.getKey("/ToUnicode");
        if(cmap.isStream()) { auto bytes=decode(cmap,1024u*1024u); supported(bytes.size()<=byte_limit-font_bytes); font_bytes+=bytes.size(); }
        auto decoded=std::make_unique<font>(object,geometry!=nullptr,outline_mode);
        size_t entries=decoded->mapping.size()+decoded->widths.size();
        supported(entries<=131072u-font_entries); font_entries+=entries;
        font *result=decoded.get(); fonts.emplace_back(object,std::move(decoded));
        return result;
    }
    void content(Obj streams,Obj resources,graphics const& initial,size_t depth);
    quantapdf_point point(matrix const& m,double x,double y) const
    {
        double px=0,py=0;
        quantapdf_qpdf_pdf_point_to_public(*geometry,m[0]*x+m[2]*y+m[4],
            m[1]*x+m[3]*y+m[5],&px,&py);
        constexpr double bound=static_cast<double>(std::numeric_limits<float>::max())/8.0;
        supported(std::isfinite(px) && std::isfinite(py) && std::abs(px)<bound && std::abs(py)<bound);
        return {static_cast<float>(px),static_cast<float>(py)};
    }
    character_value cell(matrix const& m,graphics const& g,unsigned c) const
    {
        supported(g.color_supported && g.size!=0 && g.scale!=0);
        double determinant=m[0]*m[3]-m[1]*m[2];
        supported(std::isfinite(determinant) && determinant!=0);
        double width=g.face->advance(c)*g.size*g.scale/1000;
        double top=g.rise+g.face->ascent*g.size/1000;
        double bottom=g.rise+g.face->descent*g.size/1000;
        character_value result={};
        result.quad={point(m,0,top),point(m,width,top),point(m,0,bottom),point(m,width,bottom)};
        // Transform vectors independently of large translated float positions.
        auto linear=*geometry;
        linear.left=linear.bottom=linear.right=linear.top=0;
        double dx=0,dy=0;
        quantapdf_qpdf_pdf_point_to_public(linear,m[0]*g.size*g.scale,
            m[1]*g.size*g.scale,&dx,&dy);
        double length=std::hypot(dx,dy); supported(std::isfinite(length) && length>0);
        result.dx=static_cast<float>(dx/length); result.dy=static_cast<float>(dy/length);
        double size=std::abs(g.size)*std::hypot(m[2],m[3])*geometry->user_unit;
        supported(std::isfinite(size) && size>0 && size<std::numeric_limits<float>::max()/8);
        result.font_size=static_cast<float>(size);
        auto channel=[](double n) { return static_cast<uint32_t>(std::clamp(n,0.0,1.0)*255+0.5); };
        result.argb=(channel(g.opacity)<<24)|(channel(g.fill[0])<<16)|(channel(g.fill[1])<<8)|channel(g.fill[2]);
        return result;
    }
    void emit(std::string const& value,character_value prototype)
    {
        // Validated Unicode only. All scalars in a source glyph's mapping
        // share its complete loose cell, with one source-glyph advance.
        for(size_t i=0;i<value.size();) {
            unsigned first=static_cast<unsigned char>(value[i++]);
            unsigned cp=first; size_t extra=0;
            if(first>=0xf0u) { cp=first&7u; extra=3; }
            else if(first>=0xe0u) { cp=first&15u; extra=2; }
            else if(first>=0xc0u) { cp=first&31u; extra=1; }
            need(extra<=value.size()-i);
            while(extra!=0u) { cp=(cp<<6)|(static_cast<unsigned char>(value[i++])&63u); --extra; }
            if(cp=='\r' || cp=='\n') { pending_break=true; continue; }
            supported(characters.size()<262144u);
            prototype.codepoint=cp; prototype.break_before=pending_break;
            pending_break=false; characters.push_back(prototype);
        }
    }
    void begin_outline_paint(graphics const& g)
    {
        supported(outline_paints.size()<4u && g.opacity==1.0 && g.color_supported);
        auto channel=[](double v) {supported(v>=0.0&&v<=1.0);return static_cast<uint32_t>(v*255.0+0.5);};
        outline_paints.push_back({{},UINT32_C(0xff000000)|(channel(g.fill[0])<<16u)|
            (channel(g.fill[1])<<8u)|channel(g.fill[2])});
    }
    void outline(font const& face,unsigned code,graphics const& g,matrix const& m)
    {
        supported(++outline_glyphs<=64u && !outline_paints.empty());
        supported(g.size>0.0 && g.scale>0.0 && m[0]>0.0 && m[3]>0.0 &&
                  m[1]==0.0 && m[2]==0.0);
        unsigned gid=code;
        if(!face.cid_to_gid.empty()) {supported(code<face.cid_to_gid.size());gid=face.cid_to_gid[code];}
        supported(gid<face.outline_face.num_glyphs && (gid!=0u || code==0u));
        std::vector<quantapdf::detail::ttf_outline_contour> contours;
        const auto status=face.outline_face.outline_for(static_cast<uint16_t>(gid),&contours);
        need(status==QUANTAPDF_OK,status);
        using point=quantapdf::detail::ttf_outline_point;
        quantapdf::detail::source_outline_glyph glyph;
        auto transform=[&](point p) {
            const double x=p.x*g.size*g.scale/face.outline_face.units_per_em;
            const double y=p.y*g.size/face.outline_face.units_per_em+g.rise;
            const double px=m[0]*x+m[4],py=outline_height-(m[3]*y+m[5]);
            supported(std::isfinite(px)&&std::isfinite(py)&&std::abs(px)<=1000000.0&&std::abs(py)<=1000000.0);
            return quantapdf_point{static_cast<float>(px),static_cast<float>(py)};
        };
        auto append=[&](quantapdf_composer_path_command c) {
            supported(++outline_commands<=4096u);glyph.commands.push_back(c);
        };
        auto midpoint=[](point a,point b) {return point{(a.x+b.x)/2,(a.y+b.y)/2,true};};
        auto cubic=[&](point a,point q,point b) {
            quantapdf_composer_path_command c{};c.kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
            c.point1=transform({a.x+(q.x-a.x)*2/3,a.y+(q.y-a.y)*2/3,true});
            c.point2=transform({b.x+(q.x-b.x)*2/3,b.y+(q.y-b.y)*2/3,true});
            c.point3=transform(b);append(c);
        };
        auto quadratic=[&](point a,point q,point b) {
            const double denominator=a.y-2*q.y+b.y;
            const double t=denominator!=0.0?(a.y-q.y)/denominator:0.0;
            if(t>0.0 && t<1.0) {
                point u{a.x+(q.x-a.x)*t,a.y+(q.y-a.y)*t,true};
                point v{q.x+(b.x-q.x)*t,q.y+(b.y-q.y)*t,true};
                point p{u.x+(v.x-u.x)*t,u.y+(v.y-u.y)*t,true};
                /* At the exact quadratic Y extremum both incident controls
                 * share that height; retain the common analytic value. */
                u.y=v.y=p.y;
                cubic(a,u,p);cubic(p,v,b);
            } else cubic(a,q,b);
        };
        for(auto contour:contours) {
            if(contour.empty())continue;
            if(!contour.front().on_curve) {
                if(contour.back().on_curve) {const auto last=contour.back();contour.pop_back();contour.insert(contour.begin(),last);}
                else contour.insert(contour.begin(),midpoint(contour.back(),contour.front()));
            }
            const point first=contour.front();contour.push_back(first);
            quantapdf_composer_path_command move{};move.kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
            move.point1=transform(first);append(move);point current=first;
            for(size_t i=1u;i<contour.size();) {
                if(contour[i].on_curve) {
                    if(current.x!=contour[i].x || current.y!=contour[i].y) {
                        quantapdf_composer_path_command line{};line.kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
                        line.point1=transform(contour[i]);append(line);
                    }
                    current=contour[i++];
                } else {
                    need(i+1u<contour.size());const point control=contour[i];
                    const bool on=contour[i+1u].on_curve;
                    const point end=on?contour[i+1u]:midpoint(control,contour[i+1u]);
                    quadratic(current,control,end);current=end;i+=on?2u:1u;
                }
            }
            quantapdf_composer_path_command close{};close.kind=QUANTAPDF_COMPOSER_PATH_CLOSE;append(close);
        }
        if(!glyph.commands.empty())outline_paints.back().glyphs.push_back(std::move(glyph));
    }
    void show(std::string const& bytes,graphics const& g,matrix& text)
    {
        need(g.face!=nullptr); need(bytes.size()%g.face->width==0);
        for(size_t i=0;i<bytes.size();i+=g.face->width) {
            step(); unsigned c=static_cast<unsigned char>(bytes[i]); if(g.face->width==2u) c=c*256u+static_cast<unsigned char>(bytes[i+1u]);
            auto value=g.face->text(c); matrix m=concat(g.ctm,text);
            if(outline_mode)outline(*g.face,c,g,m);
            double x=m[4]+m[2]*g.rise,y=m[5]+m[3]*g.rise;
            need(std::isfinite(x) && std::isfinite(y));
            double len=std::hypot(m[0],m[1]); need(std::isfinite(len) && len>0);
            double dx=m[0]/len,dy=m[1]/len,height=std::abs(g.size)*std::hypot(m[2],m[3]);
            need(std::isfinite(height));
            character_value current={};
            if(geometry) current=cell(m,g,c);
            if(previous && !output.empty()) {
                double cross=std::abs((x-end_x)*dir_y-(y-end_y)*dir_x);
                double along=(x-end_x)*dir_x+(y-end_y)*dir_y;
                double unit=std::max(height,last_height);
                if(dx*dir_x+dy*dir_y<0.95 || cross>std::max(0.1,unit*0.5)) { if(output.back()!='\n' && output.back()!='\r') { append("\r\n"); pending_break=true; } }
                else if(along>std::max(0.1,unit*0.2) && output.back()!=' ' && output.back()!='\n' && !value.empty() && value.front()!=' ') {
                    append(" ");
                    if(geometry && !characters.empty()) {
                        auto gap=characters.back();
                        gap.quad={gap.quad.ur,current.quad.ul,gap.quad.lr,current.quad.ll};
                        emit(" ",gap);
                    }
                }
            }
            append(value);
            if(geometry) emit(value,current);
            double distance=(g.face->advance(c)*g.size/1000.0+g.character+((g.face->width==1u && c==32u)?g.word:0))*g.scale;
            need(std::isfinite(distance));
            text=concat(text,matrix{1,0,0,1,distance,0});
            end_x=x+m[0]*distance; end_y=y+m[1]*distance; need(std::isfinite(end_x) && std::isfinite(end_y));
            previous=true; dir_x=dx; dir_y=dy; last_height=height;
        }
    }
    void draw(Obj object,Obj resources,graphics g,size_t depth)
    {
        step(); need(object.isStream()); auto d=object.getDict(); Obj type=d.getKey("/Subtype"); need(type.isName());
        if(type.getName()=="/Image") return;
        supported(type.getName()=="/Form" && depth<32u); need(active.insert(object.getObjGen()).second);
        Obj own=d.getKey("/Resources"); need(own.isNull() || own.isDictionary());
        g.ctm=concat(g.ctm,read_matrix(d.getKey("/Matrix")));
        content(object,own.isNull()?resources:own,g,depth+1u); active.erase(object.getObjGen());
    }
};
class reader final : public Obj::ParserCallbacks {
public:
    reader(projection& p,Obj resources,graphics initial,size_t depth,
           std::vector<graphics> saved={}) : p(p),resources(resources),g(initial),depth(depth),stack(std::move(saved)) {}
    void count(size_t n) { need(args.size()==n); }
    void inside() { need(in_text); }
    void move(double x,double y) { inside(); line=concat(line,matrix{1,0,0,1,x,y}); text=line; }
    void show(Obj v,bool begin_paint=true) { inside(); need(v.isString());
        if(p.outline_mode && begin_paint)p.begin_outline_paint(g);
        p.show(v.getStringValue(),g,text); }
    Obj resource(char const *category,Obj name) { need(name.isName() && resources.isDictionary()); Obj dict=resources.getKey(category); need(dict.isDictionary()); return dict.getKey(name.getName()); }
    void color_space(Obj value)
    {
        need(value.isName());
        std::set<std::string> seen;
        while(value.isName()) {
            auto name=value.getName();
            if(name=="/DeviceGray" || name=="/DeviceRGB") {
                g.fill_space=name=="/DeviceGray"?1:3;
                g.fill={0,0,0}; g.color_supported=true; return;
            }
            if(name=="/DeviceCMYK" || name=="/Pattern") break;
            supported(seen.size()<16u); need(seen.insert(name).second);
            value=resource("/ColorSpace",value); need(!value.isNull());
        }
        g.fill_space=0; g.color_supported=false;
    }
    void fill(size_t channels)
    {
        count(channels); g.color_supported=true;
        double red=number(args[0]);
        g.fill=channels==1u?std::array<double,3>{red,red,red}:
            std::array<double,3>{red,number(args[1]),number(args[2])};
    }
    void handleObject(Obj object) override
    {
        p.step(); supported(!object.isInlineImage());
        if(!object.isOperator()) { supported(args.size()<64u); args.push_back(object); return; }
        auto op=object.getOperatorValue();
        if(p.outline_mode) {
            static const char *allowed[]={"q","Q","cm","BT","ET","Tf","Tc","Tw","Tz","TL","Ts","Tr","Tm","Td","TD","T*","Tj","TJ","'","\"","g","rg"};
            supported(std::any_of(std::begin(allowed),std::end(allowed),[&](const char *s){return op==s;}));
        }
        if(op=="q") { count(0); supported(stack.size()<128u); stack.push_back(g); }
        else if(op=="Q") { count(0); need(!stack.empty()); g=stack.back(); stack.pop_back(); }
        else if(op=="cm") { count(6); matrix m; for(size_t i=0;i<6u;++i) m[i]=number(args[i]); g.ctm=concat(g.ctm,m); }
        else if(op=="BT") { count(0); need(!in_text); in_text=true; text=line=identity; }
        else if(op=="ET") { count(0); inside(); in_text=false; }
        else if(op=="Tf") { count(2); g.face=p.get_font(resource("/Font",args[0])); g.size=number(args[1]); }
        else if(op=="Tc" || op=="Tw" || op=="Tz" || op=="TL" || op=="Ts") {
            count(1); double n=number(args[0]);
            if(op=="Tc") g.character=n; else if(op=="Tw") g.word=n; else if(op=="Tz") g.scale=n/100; else if(op=="TL") g.leading=n; else g.rise=n;
        } else if(op=="Tr") { count(1); need(args[0].isInteger() && args[0].getIntValue()>=0 && args[0].getIntValue()<=7);
            if(p.outline_mode)supported(args[0].getIntValue()==0);
        } else if(op=="Tm") { count(6); inside(); for(size_t i=0;i<6u;++i) text[i]=number(args[i]); line=text; }
        else if(op=="Td" || op=="TD") { count(2); double x=number(args[0]),y=number(args[1]); if(op=="TD") g.leading=-y; move(x,y); }
        else if(op=="T*") { count(0); move(0,-g.leading); }
        else if(op=="Tj") { count(1); show(args[0]); }
        else if(op=="TJ") {
            count(1); inside(); need(args[0].isArray()); supported(args[0].getArrayNItems()<=100000);
            if(p.outline_mode)p.begin_outline_paint(g);
            for(int i=0;i<args[0].getArrayNItems();++i) { p.step(); Obj v=args[0].getArrayItem(i); if(v.isString()) show(v,false); else text=concat(text,matrix{1,0,0,1,-number(v)*g.size*g.scale/1000.0,0}); }
        } else if(op=="'" || op=="\"") {
            count(op=="'"?1u:3u); if(op=="\"") { g.word=number(args[0]); g.character=number(args[1]); } move(0,-g.leading); show(args.back());
        } else if(op=="Do") { count(1); p.draw(resource("/XObject",args[0]),resources,g,depth); }
        else if(op=="gs") {
            count(1); Obj state=resource("/ExtGState",args[0]); need(state.isDictionary()); Obj f=state.getKey("/Font");
            if(!f.isNull()) { need(f.isArray() && f.getArrayNItems()==2); g.face=p.get_font(f.getArrayItem(0)); g.size=number(f.getArrayItem(1)); }
            if(p.geometry) { Obj alpha=state.getKey("/ca"); if(!alpha.isNull()) g.opacity=std::clamp(number(alpha),0.0,1.0); }
        } else if((p.geometry || p.outline_mode) && (op=="g" || op=="rg" || op=="k" || op=="cs" || op=="sc" || op=="scn")) {
            if(op=="g") { fill(1); g.fill_space=1; }
            else if(op=="rg") { fill(3); g.fill_space=3; }
            else if(op=="k") { count(4); for(auto v:args) (void)number(v); g.color_supported=false; g.fill_space=0; }
            else if(op=="cs") { count(1); color_space(args[0]); }
            else if(g.fill_space==1 || g.fill_space==3) fill(static_cast<size_t>(g.fill_space));
            else { need(!args.empty() && args.size()<=32u); g.color_supported=false; }
        } else if(op=="BMC" || op=="BDC") {
            count(op=="BMC"?1u:2u); need(args[0].isName()); supported(marked<128u); ++marked;
            if(op=="BDC") { Obj properties=args[1]; if(properties.isName()) properties=resource("/Properties",properties); need(properties.isDictionary()); supported(properties.getKey("/ActualText").isNull()); }
        } else if(op=="EMC") { count(0); need(marked>0u); --marked; }
        else {
            struct grammar { char const *name; size_t count; };
            static const grammar ignored[]={
                {"w",1},{"J",1},{"j",1},{"M",1},{"d",2},{"ri",1},{"i",1},
                {"m",2},{"l",2},{"c",6},{"v",4},{"y",4},{"h",0},{"re",4},
                {"S",0},{"s",0},{"f",0},{"F",0},{"f*",0},{"B",0},{"B*",0},{"b",0},{"b*",0},{"n",0},{"W",0},{"W*",0},
                {"CS",1},{"cs",1},{"G",1},{"g",1},{"RG",3},{"rg",3},{"K",4},{"k",4},{"sh",1},{"MP",1},{"DP",2},{"BX",0},{"EX",0}};
            bool found=false; for(auto const& spec:ignored) if(op==spec.name) { count(spec.count); found=true; break; }
            if(op=="SC" || op=="SCN" || op=="sc" || op=="scn") { need(!args.empty() && args.size()<=32u); found=true; }
            supported(found);
        }
        args.clear();
    }
    void handleEOF() override { need(args.empty() && stack.empty() && !in_text && marked==0u); }
private:
    projection& p; Obj resources; graphics g; size_t depth,marked=0; bool in_text=false;
    // Text/line matrices are not part of the saved graphics-state stack.
    matrix text=identity,line=identity;
    std::vector<graphics> stack; std::vector<Obj> args;
};
void projection::content(Obj streams,Obj resources,graphics const& initial,size_t depth)
{
    std::string bytes;
    auto append_stream=[&](Obj stream) {
        step(); supported(content_bytes<byte_limit); auto part=decode(stream,byte_limit-content_bytes-1u);
        content_bytes+=part.size()+1u; bytes+=part; bytes+='\n';
    };
    if(streams.isArray()) { supported(streams.getArrayNItems()<=1024); for(int i=0;i<streams.getArrayNItems();++i) append_stream(streams.getArrayItem(i)); }
    else if(!streams.isNull()) append_stream(streams);
    Obj stream=parser.newStream(bytes); reader interpret(*this,resources,initial,depth); stream.parseAsContents(&interpret); need(!parser.anyWarnings());
}
quantapdf_rect bounds(quantapdf_quad const& q)
{
    quantapdf_rect r={q.ul.x,q.ul.y,q.ul.x,q.ul.y};
    for(auto p:{q.ur,q.ll,q.lr}) {
        r.x0=std::min(r.x0,p.x); r.y0=std::min(r.y0,p.y);
        r.x1=std::max(r.x1,p.x); r.y1=std::max(r.y1,p.y);
    }
    return r;
}
void include(quantapdf_rect& r,quantapdf_rect const& b)
{
    r.x0=std::min(r.x0,b.x0); r.y0=std::min(r.y0,b.y0);
    r.x1=std::max(r.x1,b.x1); r.y1=std::max(r.y1,b.y1);
}
void dispose_text(quantapdf_text_page *p)
{
    if(!p) return;
    std::free(p->blocks); std::free(p->lines); std::free(p->spans);
    std::free(p->chars); std::free(p->strings); std::free(p);
}
quantapdf_text_page *publish(projection const& p)
{
    std::vector<quantapdf_text_line_internal> lines;
    std::vector<quantapdf_text_span_internal> spans;
    std::vector<quantapdf_text_char_internal> chars;
    std::string strings;
    for(auto const& source:p.characters) {
        auto box=bounds(source.quad);
        bool new_line=lines.empty() || source.break_before;
        if(!new_line) new_line=source.dx*lines.back().direction_x+source.dy*lines.back().direction_y<0.95f;
        if(new_line) {
            if(!lines.empty()) lines.back().span_count=spans.size()-lines.back().first_span;
            quantapdf_text_line_internal line={}; line.bounds=box;
            line.direction_x=source.dx; line.direction_y=source.dy;
            line.first_span=spans.size(); lines.push_back(line);
        } else include(lines.back().bounds,box);
        bool new_span=new_line || std::abs(spans.back().font_size-source.font_size)>0.001f || spans.back().argb!=source.argb;
        if(new_span) {
            if(!spans.empty()) strings+='\0';
            quantapdf_text_span_internal span={}; span.bounds=box;
            span.font_size=source.font_size; span.argb=source.argb;
            span.first_char=chars.size(); span.text_offset=strings.size(); spans.push_back(span);
        } else include(spans.back().bounds,box);
        std::string value; utf8(value,source.codepoint);
        strings+=value; supported(strings.size()<=byte_limit+p.characters.size());
        auto& span=spans.back(); span.text_size+=value.size(); ++span.char_count;
        quantapdf_text_char_internal target={}; target.codepoint=source.codepoint;
        target.quad=source.quad; target.span_index=spans.size()-1u; chars.push_back(target);
    }
    strings+='\0';
    if(!lines.empty()) lines.back().span_count=spans.size()-lines.back().first_span;
    size_t retained=sizeof(quantapdf_text_page)+sizeof(quantapdf_text_block_internal)+strings.size()+
        lines.size()*sizeof(lines[0])+spans.size()*sizeof(spans[0])+chars.size()*sizeof(chars[0]);
    supported(retained<=64u*1024u*1024u);
    std::unique_ptr<quantapdf_text_page,decltype(&dispose_text)> result(
        static_cast<quantapdf_text_page *>(std::calloc(1,sizeof(quantapdf_text_page))),dispose_text);
    need(result!=nullptr,QUANTAPDF_ERROR_NOMEM);
    auto copy=[](auto const& values,auto **target) {
        if(values.empty()) return;
        *target=static_cast<std::remove_reference_t<decltype(**target)> *>(std::malloc(values.size()*sizeof(values[0])));
        need(*target!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(*target,values.data(),values.size()*sizeof(values[0]));
    };
    copy(lines,&result->lines); copy(spans,&result->spans); copy(chars,&result->chars);
    result->strings=static_cast<char *>(std::malloc(strings.size())); need(result->strings!=nullptr,QUANTAPDF_ERROR_NOMEM);
    std::memcpy(result->strings,strings.data(),strings.size()); result->string_size=strings.size();
    result->line_count=lines.size(); result->span_count=spans.size(); result->char_count=chars.size();
    if(!lines.empty()) {
        result->blocks=static_cast<quantapdf_text_block_internal *>(std::calloc(1,sizeof(*result->blocks)));
        need(result->blocks!=nullptr,QUANTAPDF_ERROR_NOMEM);
        result->block_count=1; result->blocks[0].bounds=lines[0].bounds; result->blocks[0].line_count=lines.size();
        for(auto const& line:lines) include(result->blocks[0].bounds,line.bounds);
    }
    return result.release();
}

}
namespace quantapdf::detail {
namespace {
graphics outline_graphics(source_text_seed const& seed)
{
    graphics g;g.ctm=seed.ctm;
    g.fill={static_cast<double>((seed.argb>>16u)&255u)/255,
            static_cast<double>((seed.argb>>8u)&255u)/255,
            static_cast<double>(seed.argb&255u)/255};
    return g;
}
class source_outline_reader final : public source_text_outlines {
    projection p;
    reader tokens;
    template<class F> quantapdf_status invoke(F f) {
        try {f();return QUANTAPDF_OK;}
        catch(failure const& e){return e.status;}
        catch(std::bad_alloc const&){return QUANTAPDF_ERROR_NOMEM;}
        catch(std::length_error const&){return QUANTAPDF_ERROR_UNSUPPORTED;}
        catch(...){return QUANTAPDF_ERROR_FORMAT;}
    }
    static std::vector<graphics> saved_graphics(const std::vector<source_text_seed>& saved)
    {std::vector<graphics> result;for(auto const& g:saved)result.push_back(outline_graphics(g));return result;}
public:
    source_outline_reader(Obj resources,double height,source_text_seed seed,
                          const std::vector<source_text_seed>& saved) :
        p(),tokens(p,resources,outline_graphics(seed),0u,saved_graphics(saved))
    {p.outline_mode=true;p.outline_height=height;}
    quantapdf_status consume(Obj object) override {return invoke([&]{tokens.handleObject(object);});}
    quantapdf_status finish() override {return invoke([&]{tokens.handleEOF();
        supported(!p.fonts.empty() && !p.outline_paints.empty());});}
    const std::vector<source_outline_paint>& paints() const override {return p.outline_paints;}
};
}
std::unique_ptr<source_text_outlines> make_source_text_outlines(
    QPDFObjectHandle resources,double height,source_text_seed seed,
    const std::vector<source_text_seed>& saved)
{return std::make_unique<source_outline_reader>(resources,height,seed,saved);}
}

extern "C" quantapdf_status quantapdf_qpdf_extract_text(quantapdf_qpdf_document *document,int page_index,char **out_utf8,size_t *out_size)
{
    if(out_utf8) *out_utf8=nullptr;
    if(out_size) *out_size=0;
    if(!document || page_index<0 || !out_utf8 || !out_size) return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_qpdf_page_geometry geometry={}; auto status=quantapdf_qpdf_load_page_geometry(*document->pdf,page_index,&geometry); if(status!=QUANTAPDF_OK) return status;
        Obj resources=quantapdf_qpdf_inherited_value(geometry.page,"/Resources"); need(resources.isNull() || resources.isDictionary());
        projection p; p.content(geometry.page.getKey("/Contents"),resources,graphics{},0u);
        char *copy=static_cast<char *>(std::malloc(p.output.size()+1u)); need(copy!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(copy,p.output.c_str(),p.output.size()+1u); *out_utf8=copy; *out_size=p.output.size(); return QUANTAPDF_OK;
    } catch(failure const& e) { return e.status; }
      catch(std::bad_alloc const&) { return QUANTAPDF_ERROR_NOMEM; }
      catch(std::length_error const&) { return QUANTAPDF_ERROR_UNSUPPORTED; }
      catch(QPDFExc const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch(std::runtime_error const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch(...) { return QUANTAPDF_ERROR_BACKEND; }
}

extern "C" quantapdf_status quantapdf_qpdf_extract_structured_text(
    quantapdf_qpdf_document *document,int page_index,quantapdf_text_page **out_text)
{
    if(!out_text) return QUANTAPDF_ERROR_ARGUMENT;
    *out_text=nullptr;
    if(!document || page_index<0) return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_qpdf_page_geometry geometry={};
        auto status=quantapdf_qpdf_load_page_geometry(*document->pdf,page_index,&geometry);
        if(status!=QUANTAPDF_OK) return status;
        Obj resources=quantapdf_qpdf_inherited_value(geometry.page,"/Resources");
        need(resources.isNull() || resources.isDictionary());
        projection p(&geometry); p.content(geometry.page.getKey("/Contents"),resources,graphics{},0u);
        *out_text=publish(p); return QUANTAPDF_OK;
    } catch(failure const& e) { return e.status; }
      catch(std::bad_alloc const&) { return QUANTAPDF_ERROR_NOMEM; }
      catch(std::length_error const&) { return QUANTAPDF_ERROR_UNSUPPORTED; }
      catch(QPDFExc const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch(std::runtime_error const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch(...) { return QUANTAPDF_ERROR_BACKEND; }
}
