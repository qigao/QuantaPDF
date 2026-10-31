#ifndef QUANTAPDF_NATIVE_TTF_FIXTURE_H
#define QUANTAPDF_NATIVE_TTF_FIXTURE_H
/* Test-only ORIGINAL PDF author. Glyph IDs/1000-em PDF widths are fixed
 * from the reviewed Roboto fixture, independently of the production decoder. */
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <vector>


/* Test-author sfnt packaging only: copy every original table byte except
 * cmap/DSIG, rebuild directory offsets/search fields and whole-font checksum.
 * No outlines or metrics are decoded or changed. ISO 32000-1 9.9 requires
 * cmap omission for embedded CID fonts; original PDF CIDToGIDMap selects GIDs. */
inline std::string native_cid_font_program(const std::string& original)
{
    const auto u16=[&](size_t at) {return (unsigned char)original.at(at)*256u+(unsigned char)original.at(at+1u);};
    const auto u32=[&](size_t at) {return (uint32_t)u16(at)*65536u+u16(at+2u);};
    struct table {size_t entry;uint32_t offset,length;};std::vector<table> tables;
    for(unsigned i=0u;i<u16(4u);++i) {
        const size_t at=12u+16u*i;const auto tag=original.substr(at,4u);
        if(tag!="cmap" && tag!="DSIG")tables.push_back({at,u32(at+8u),u32(at+12u)});
    }
    std::string out=original.substr(0u,12u);out.resize(12u+16u*tables.size(),'\0');
    const auto put16=[&](size_t at,unsigned v) {out.at(at)=char(v>>8u);out.at(at+1u)=char(v);};
    const auto put32=[&](size_t at,uint32_t v) {
        for(unsigned j=0u;j<4u;++j)out.at(at+j)=char(v>>(24u-8u*j));
    };
    unsigned power=1u,selector=0u;
    while(power*2u<=tables.size()) {power*=2u;++selector;}
    put16(4u,(unsigned)tables.size());put16(6u,power*16u);
    put16(8u,selector);put16(10u,(unsigned)tables.size()*16u-power*16u);
    size_t head=0u;
    for(size_t i=0u;i<tables.size();++i) {
        const auto& t=tables[i];const size_t at=12u+16u*i,offset=out.size();
        out.replace(at,8u,original,t.entry,8u);put32(at+8u,(uint32_t)offset);put32(at+12u,t.length);
        out.append(original,t.offset,t.length);
        while(out.size()%4u!=0u)out+='\0';
        if(original.compare(t.entry,4u,"head")==0)head=offset;
    }
    if(head==0u)throw std::runtime_error("missing original head");
    put32(head+8u,0u);uint32_t sum=0u;
    for(size_t at=0u;at<out.size();at+=4u) {
        uint32_t word=0u;for(unsigned j=0u;j<4u;++j)word=(word<<8u)|(unsigned char)out[at+j];
        sum+=word;
    }
    put32(head+8u,UINT32_C(0xb1b0afba)-sum);return out;
}

inline std::string native_ttf_fixture(int mode=0)
{
    using O=QPDFObjectHandle;
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/
                    "fixtures"/"fonts"/"Roboto-Regular.ttf";
    std::ifstream file(path,std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(file),{}};
    if(bytes.size()!=515100u)throw std::runtime_error("missing original Roboto fixture");
    if(mode==27 || mode==28) {
        const auto u16=[&](size_t at) {return (unsigned char)bytes.at(at)*256u+(unsigned char)bytes.at(at+1u);};
        const auto u32=[&](size_t at) {return (size_t)u16(at)*65536u+u16(at+2u);};
        size_t glyf=0u,loca=0u,head=0u;
        for(unsigned i=0u;i<u16(4u);++i) {
            const size_t at=12u+16u*i;
            if(bytes.compare(at,4u,"glyf")==0)glyf=u32(at+8u);
            if(bytes.compare(at,4u,"loca")==0)loca=u32(at+8u);
            if(bytes.compare(at,4u,"head")==0)head=u32(at+8u);
        }
        const size_t glyph=glyf+(u16(head+50u)==0u?2u*u16(loca+2317u*2u):u32(loca+2317u*4u));
        if(mode==27) {bytes.at(glyph+12u)=char(0x09);bytes.at(glyph+13u)=char(0x0d);} // cycle
        if(mode==28)bytes.at(glyph+11u)=char((unsigned char)bytes.at(glyph+11u)&~2u); // point attachment
    }
    bytes=native_cid_font_program(bytes);
    if(mode==17)bytes.resize(24u);
    auto pdf=QPDF::create();pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(O::parse("<< /Type /Page /MediaBox [0 0 128 64] >>"));
    page.replaceKey("/Parent",root);root.replaceKey("/Kids",O::newArray({page}));
    root.replaceKey("/Count",O::newInteger(1));
    auto desc=pdf->makeIndirectObject(O::parse(
        "<< /Type /FontDescriptor /FontName /Roboto-Regular /Flags 32 "
        "/FontBBox [-1509 -555 2352 2163] /ItalicAngle 0 /Ascent 928 "
        "/Descent -244 /CapHeight 711 /StemV 80 >>"));
    if(mode!=15) {
        auto program=pdf->newStream(bytes);
        if(mode!=30)program.getDict().replaceKey("/Length1",O::newInteger(static_cast<long long>(bytes.size())+(mode==31?1:0)));
        desc.replaceKey("/FontFile2",program);
    }
    if(mode==21)desc.replaceKey("/FontFile3",pdf->newStream("CFF"));
    auto cid=pdf->makeIndirectObject(O::parse(
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Roboto-Regular "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        "/DW 0 /W [5 [248] 45 [713] 74 [530] 81 [243] 84 [570] 2317 [530]] >>"));
    cid.replaceKey("/FontDescriptor",desc);
    cid.replaceKey("/CIDToGIDMap",O::newName("/Identity"));
    if(mode==14)cid.replaceKey("/Subtype",O::newName("/CIDFontType0"));
    if(mode==4 || mode==18) {
        std::string map("\0\0\0\x2d\0\x4a\0\x51\0\x54",10);
        if(mode==18)map.pop_back();
        cid.replaceKey("/CIDToGIDMap",pdf->newStream(map));
        cid.replaceKey("/W",O::parse("[1 [713 530 243 570]]"));
    }
    auto font=pdf->makeIndirectObject(O::parse(
        "<< /Type /Font /Subtype /Type0 /BaseFont /Roboto-Regular /Encoding /Identity-H >>"));
    font.replaceKey("/DescendantFonts",O::newArray({cid}));
    const std::string cmap=
        "/CIDInit /ProcSet findresource begin 12 dict begin begincmap "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def "
        "/CMapName /Adobe-Identity-UCS def /CMapType 2 def "
        "1 begincodespacerange <0000> <ffff> endcodespacerange "
        "10 beginbfchar <0005> <0020> <002d> <0048> <004a> <0065> "
        "<0051> <006c> <0054> <006f> <090d> <00e9> "
        "<0001> <0048> <0002> <0065> <0003> <006c> <0004> <006f> "
        "endbfchar endcmap CMapName currentdict /CMap defineresource pop end end";
    font.replaceKey("/ToUnicode",pdf->newStream(cmap));
    auto fonts=O::newDictionary(),resources=O::newDictionary();fonts.replaceKey("/F",font);
    resources.replaceKey("/Font",fonts);page.replaceKey("/Resources",resources);
    std::string text="q BT /F 24 Tf 12 20 Td <002d004a005100510054> Tj ET Q";
    if(mode==1)text="q BT /F 24 Tf -8 Tc 1 0 0 rg 1 0 0 1 18 20 Tm <004a0054> Tj "
        "0 Tc 0 0 1 rg 1 0 0 1 24 20 Tm <090d> Tj "
        "0 g 1 0 0 1 28 20 Tm <002d> Tj ET Q";
    if(mode==2)text="q BT /F 24 Tf 12 20 Td [<002d004a> 120 <0051> -120 <00510054>] TJ ET Q";
    if(mode==3)text="q BT /F 24 Tf 12 20 Td <00050005> Tj ET Q";
    if(mode==4)text="q BT /F 24 Tf 12 20 Td <00010002000300030004> Tj ET Q";
    if(mode==5)text="q BT /F 24 Tf 122 -1 Td <002d> Tj ET Q";
    if(mode==6)text="q 1 0 0 1 12 20 cm BT /F 24 Tf <002d004a005100510054> Tj ET Q";
    if(mode==7)text="q BT /F 24 Tf 0 Tc 90 Tz 2 Ts 1 0 0 1 12 18 Tm "
        "100 Tz <002d004a005100510054> Tj ET Q"; /* rise cancels baseline */
    if(mode==8)text="q BT /F 24 Tf 12 40 Td 20 TL <002d004a005100510054> ' ET Q";
    if(mode==9)text="q BT /F 24 Tf 12 40 Td 20 TL 0 0 <002d004a005100510054> \" ET Q";
    if(mode==10)text="q BT /F 24 Tf 1 Tr 12 20 Td <002d> Tj ET Q";
    if(mode==11)text+=" 1 1 2 2 re f";
    if(mode==12)text+=" /Missing Do";
    if(mode==13)text+=" Q";
    if(mode==16)text="q BT /F 24 Tf 12 20 Td <ffff> Tj ET Q";
    if(mode==19)text="q BT /F 24 Tf 1 .1 0 1 12 20 Tm <002d> Tj ET Q";
    if(mode==20)text="q BT /F -24 Tf 12 20 Td <002d> Tj ET Q";
    if(mode==22)text="BT /F 24 Tf 12 20 Td <002d> Tj <002d> Tj <002d> Tj <002d> Tj <002d> Tj ET";
    if(mode==23) {text="BT /F 24 Tf <";for(int i=0;i<65;++i)text+="0005";text+="> Tj ET";}
    if(mode==24)text="q 0 0 1 1 re W n BT /F 24 Tf <002d> Tj ET Q";
    if(mode==25)page.replaceKey("/Rotate",O::newInteger(90));
    if(mode==27 || mode==28)text="BT /F 24 Tf 12 20 Td <090d> Tj ET";
    if(mode==29)font.removeKey("/ToUnicode");
    if(mode==26)page.replaceKey("/MediaBox",O::parse("[0 0 128.5 64]"));
    if(mode>=32 && mode<=48) {
        const std::string background="q 0.875 g 0 0 128 64 re f Q ";
        const std::string hello="q BT /F 24 Tf 12 20 Td <002d004a005100510054> Tj ET Q ";
        const std::string blue="q 0 0 1 rg 25.125 18.25 12.5 9.5 re f* Q ";
        const std::string accent="q 1 0 0 rg BT /F 24 Tf 24 20 Td <090d> Tj ET Q";
        text=background+(mode==33?blue+hello:hello+blue)+accent;
        if(mode==34)text=background+hello;
        if(mode==35)text+=" 0 0 1 rg 1 1 2 2 re f"; // fifth logical paint
        if(mode==36)text+=" /Missing Do";
        if(mode==37)text+=" 1 1 2 2 re W n";
        if(mode==38)text+=" 1 1 2 2 re S";
        if(mode==39)text+=" 1 1 2 2 re"; // unfinished path
        if(mode==40)text+=" 1 1 m 2 3 l 3 1 l h f"; // nonrectangle
        if(mode==41)text=hello+"q 1 0.25 0 1 0 0 cm 1 1 2 2 re f Q";
        if(mode==42)text=hello+"BT 1 1 2 2 re f ET"; // path inside text
        if(mode==43)text=hello+"1 1 2 2 re q f Q";
        if(mode==45)text="0 0 1 rg 1 1 m 2 3 l 3 1 l h f "+hello;
        if(mode==46)text=background+blue+"0 g 1 1 2 2 re f 3 3 2 2 re f "+hello;
        if(mode==47)text=blue; // no text: retain the old integer PATH-only gate
        if(mode==48)text="q 0 0 1 rg 25.1250000000000001 18.25 12.5 9.5 re f Q "+hello; // interrupted rect
    }
    page.replaceKey("/Contents",pdf->newStream(text));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.write();
    auto blob=writer.getBufferSharedPointer();
    return {reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize()};
}
#endif
