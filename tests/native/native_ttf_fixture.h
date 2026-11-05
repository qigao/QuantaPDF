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
#include <zlib.h>


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

inline std::string native_ttf_fixture(int mode=0,bool translated_clip=false)
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
        if(mode==37)text+=" 1 1 2 2 re W n"; // unused late Clip
        if(mode==38)text+=" 1 1 2 2 re S";
        if(mode==39)text+=" 1 1 2 2 re"; // unfinished path
        if(mode==40)text+=" 1 1 m 2 3 l 3 1 l h f"; // nonrectangle
        if(mode==41)text=hello+"q 1 0.25 0 1 0 0 cm 1 1 2 2 re f Q";
        if(mode==42)text=hello+"BT 1 1 2 2 re f ET"; // path inside text
        if(mode==43)text=hello+"1 1 2 2 re q f Q";
        if(mode==45)text="0 0 1 rg 1 1 m 2 3 l 3 1 l h f "+hello;
        if(mode==46)text=background+blue+"0 g 1 1 2 2 re f 3 3 2 2 re f "+hello;
        if(mode==47)text=blue; // the same fractional Fill without Text
        if(mode==48)text="q 0 0 1 rg 25.1250000000000001 18.25 12.5 9.5 re f Q "+hello; // finite decimal rectangle before Text
    }
    /* #578 M2a: fully original QPDF-authored embedded CID TTF +
     * RAW/Flate RGB Image in both orders, plus late invalid operations.
     * No IR-derived or compositor-derived fixtures are used. */
    if(mode>=49 && mode<=169) {
        const std::string pixels("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
        std::string encoded=pixels;
        if(mode==64 || mode==65 || mode==81 || mode==82 || mode==114 || mode==127 || mode==146 || mode==148 || mode==169) {
            uLongf capacity=compressBound(static_cast<uLong>(pixels.size()));
            encoded.assign(static_cast<size_t>(capacity),'\0');
            if(compress2(reinterpret_cast<Bytef *>(encoded.data()),&capacity,
                         reinterpret_cast<const Bytef *>(pixels.data()),
                         static_cast<uLong>(pixels.size()),
                         Z_BEST_COMPRESSION)!=Z_OK)
                throw std::runtime_error("M2a test-only Flate encoding failed");
            encoded.resize(static_cast<size_t>(capacity));
        }
        auto image=pdf->newStream(encoded);
        auto dict=image.getDict();
        dict.replaceKey("/Type",O::newName("/XObject"));
        dict.replaceKey("/Subtype",O::newName("/Image"));
        dict.replaceKey("/Width",O::newInteger(2));
        dict.replaceKey("/Height",O::newInteger(2));
        dict.replaceKey("/BitsPerComponent",O::newInteger(8));
        dict.replaceKey("/ColorSpace",O::newName("/DeviceRGB"));
        if(mode==64 || mode==65 || mode==81 || mode==82 || mode==114 || mode==127 || mode==146 || mode==148 || mode==169) {
            dict.replaceKey("/Filter",O::newName("/FlateDecode"));
            image.setFilterOnWrite(false);
        }
        if(mode==60)
            dict.replaceKey("/SMask",O::newName("/None")); /* bad SMask */
        auto xobjects=O::newDictionary();
        xobjects.replaceKey("/I",image);
        if(mode==57)
            xobjects.replaceKey("/J",image); /* unused XObject */
        if(mode==58)
            fonts.replaceKey("/G",font); /* unused Font alias */
        resources.replaceKey("/XObject",xobjects);
        if(mode==51) {
            /* The fully parsed original has an UNUSED graphics resource.
             * M2a must reject it instead of silently dropping it from IR. */
            resources.replaceKey("/ExtGState",O::newDictionary());
        }
        const std::string show=
            "q BT /F 24 Tf 12 20 Td <002d004a005100510054> Tj ET Q";
        const std::string draw=mode==56 ?
            "q 16 1 0 12 10 15 cm /I Do Q" : /* unsupported shear */
            "q 16 0 0 12 10 15 cm /I Do Q";
        text=(mode==50 || mode==63 || mode==65) ?
            draw+" "+show : show+" "+draw;
        if(mode==52 || mode==63)text+=" /Missing Do";
        if(mode==53)text+=" 0 0 1 rg 1 1 2 2 re f"; /* late Fill */
        if(mode==54)text+=" q 1 1 2 2 re W n Q"; /* unused late Clip */
        if(mode==55)text+=" BT /F 24 Tf 1 Tr 12 20 Td <002d> Tj ET";
        if(mode==59)text+=" Q"; /* unbalanced restore */
        if(mode==61)text+=" "+draw; /* second original Image Do */
        if(mode==62)text+=" BT /F 24 Tf 12 20 Td <002d> Tj"; /* no ET */
        if(mode>=66 && mode<=169) {
            const std::string fill=(mode==93 || mode==94 || mode>=99)?
                "q 0 1 0 rg 12 20 7 10 re f Q":
                "q 0 1 0 rg 12 20 16 20 re f Q";
            const std::string first=mode==75 ?
                "q 16 1 0 12 10 15 cm /I Do Q":draw;
            const std::string tshow=mode==80 ?
                "q 0 0 1 rg BT /F 24 Tf 12 20 Td "
                "<002d004a005100510054> Tj ET Q":show;
            text=(mode==67 || mode==82 || mode==84 || mode==92 || mode==94 || mode==100 || mode==112 ||
                  (mode>=69 && mode<=80 && (mode%2)==1)) ?
                first+" "+fill+" "+tshow:
                tshow+" "+fill+" "+first;
            if(mode==68)text+=" /Missing Do";
            if(mode==69)text+=" q 1 0 0 rg 2 2 4 4 re f Q";
            if(mode==70)text+=" q 12 12 8 8 re W n Q";
            if(mode==71)text+=" q 1 1 2 2 re S Q";
            if(mode==72)resources.replaceKey("/ExtGState",O::newDictionary());
            if(mode==73)xobjects.replaceKey("/J",image);
            if(mode==74)fonts.replaceKey("/G",font);
            if(mode==76)text+=" Q";
            if(mode==77)text+=" BT /F 24 Tf 1 Tr 12 20 Td <002d> Tj ET";
            if(mode==78)text+=" "+draw;
            if(mode==79)text+=" BT /F 24 Tf 12 20 Td <002d> Tj";
            if(mode>=83 && mode<=169) {
                /* 83/84 preserve the genuine fractional Clip boundary at
                 * 96 DPI. NEW 91/92 use independent Roboto pre-M2a
                 * reference with Clip edges aligned to full physical
                 * pixels at 72/96/144; this enables exact whole-image
                 * compositing goldens without reconstructing lost
                 * glyph subpixel information from averaged 8-bit alpha. */
                const bool aligned=mode>=91 && mode<=98;
                /* 99/100 carry authored quarter-point x/y edges through
                 * genuine W/W* in both orders. Their original H stem and
                 * Clip have disjoint nonempty subpixel masks. */
                text=std::string(mode>=99?"16.25 22.25 27 15.5 re ":
                                aligned?"12 22 15 18 re ":"12 20 16 20 re ")+
                     (mode==84 || mode==92 || mode==94 || mode==100 || mode>=106?"W*":"W")+
                     " n "+text;
                if(mode==101)text+=" Q"; /* fractional excess restore */
                if(mode==102)text+=" 1 1 2 2 re W n"; /* late Clip */
                if(mode==103 || mode==104) {
                    const auto paints=text.substr(text.find(" n ")+3u);
                    if(mode==104 && translated_clip)
                        text="q 1 0 0 1 4 0 cm 12.25 22.25 m 39.25 22.25 l "
                            "39.25 27 38.25 32 39.25 37.75 c 12.25 37.75 l h W n "
                            "1 0 0 1 -4 0 cm "+paints+" Q";
                    else text=std::string(mode==103?
                        "16.25 22.25 m 43.25 22.25 l 42.25 37.75 l 16.25 37.75 l h W n ":
                        "16.25 22.25 m 43.25 22.25 l 43.25 27 42.25 32 43.25 37.75 c 16.25 37.75 l h W n ")+paints;
                }
                if(mode==85)text+=" Q"; /* unmatched graphics restoration */
                if(mode==86)text+=" 1 1 2 2 re W n"; /* late Clip after BT */
                if(mode==87)resources.replaceKey(
                    "/ExtGState",O::newDictionary()); /* orphan resource */
                if(mode==88)text+=" 1 1 2 2 re S"; /* late Stroke */
                if(mode==89)text+=" /Missing Do"; /* hidden XObject */
                if(mode==90)text="q "+text+" Q"; /* Clip inside prior q:
                                                     saved pre-Clip GS0
                                                     cannot cross BT */
                if(mode==95 || mode==96) {
                    /* One additional ORIGINAL q saves frozen W Clip 1
                     * before BT and restores that same Clip after the
                     * final paint. Extra Q in 96 is malformed. */
                    const std::string clip_prefix="12 22 15 18 re W n ";
                    const std::string paints=text.substr(clip_prefix.size());
                    text=clip_prefix+"q "+paints+" Q";
                    if(mode==96)text+=" Q";
                }
                if(mode==97 || mode==98 || (mode>=105 && mode<=112)) {
                    /* Real pre-W q saves GS0; W/n creates Clip1.
                     * Text, Fill and Image freeze GS1, then original
                     * Q restores GS0 for a FOURTH RED Fill. The extra
                     * Q in mode98 is a complete-original negative. */
                    std::string suffix=mode>=106?
                        " Q 1 0 0 rg 18.25 20.25 18.5 12.5 re f":
                        " Q 1 0 0 rg 34 9 6 5 re f";
                    if(mode==109)suffix=
                        " Q 1 0 0 rg 18.25 20.25 m 36.75 20.25 l 35.75 32.75 l 18.25 32.75 l h f";
                    if(mode==110)suffix=translated_clip?
                        " Q 1 0 0 rg q 1 0 0 1 4 0 cm 14.25 20.25 m 32.75 20.25 l 32.75 24 31.75 28 32.75 32.75 c 14.25 32.75 l h f Q":
                        " Q 1 0 0 rg 18.25 20.25 m 36.75 20.25 l 36.75 24 35.75 28 36.75 32.75 c 18.25 32.75 l h f";
                    text="q "+text+suffix;
                    if(mode==98 || mode==107)text+=" Q";
                    if(mode==108)text+=" 1 1 2 2 re W n";
                    if(mode==111)text+=" 0 0 1 rg 1 1 2 2 re f";
                }
                if(mode>=113 && mode<=125) {
                    const std::string first_rule=mode==114?"W*":"W";
                    const std::string second_rule=mode==114?"W":"W*";
                    const std::string first_scope="q 16.25 22.25 27 15.5 re "+
                        first_rule+" n "+show+" "+fill;
                    const std::string second_scope=" q "+
                        std::string(mode==121?"1 0 0 1 1 0 cm ":"")+
                        "20.5 15.75 18.25 18.5 re "+second_rule;
                    text=first_scope+(mode==117?"":" Q")+second_scope;
                    if(mode!=123)text+=" n";
                    if(mode!=122 && mode!=123)text+=" "+draw;
                    if(mode==119)text+=" "+show;
                    if(mode==120)text+=" "+fill;
                    if(mode!=115 && mode!=123)text+=" Q";
                    if(mode==116 || mode==117)text+=" Q";
                    if(mode==118)text+=" q 1 1 2 2 re W n Q";
                    if(mode==124)text+=" /Missing Do";
                    if(mode==125)fonts.replaceKey("/G",font);
                }
                if(mode>=126 && mode<=146) {
                    /* Both q frames precede BT: the inner frame retains
                     * leaf1, so the second W/n must preserve two authored
                     * leaves and root Intersection(1,2), not reduce a rect. */
                    const std::string first_rule=(mode==127 || mode==146)?"W*":"W";
                    const std::string second_rule=(mode==127 || mode==146)?"W":"W*";
                    const std::string outer=std::string(mode==142?"":"q ")+
                        "16.25 22.25 27 15.5 re "+first_rule+" n ";
                    std::string inner="q "+std::string(mode==131 && !translated_clip?"1 0 0 1 1 0 cm ":"")+
                        (mode==131 && translated_clip?"21.5 15.75 18.25 18.5 re ":"20.5 15.75 18.25 18.5 re ");
                    if(mode==139)inner="q 20.5 15.75 m 38.75 15.75 l 37.75 34.25 l 20.5 34.25 l h ";
                    if(mode==140)inner="q 20.5 15.75 m 38.75 15.75 l 38.75 20 37.75 30 38.75 34.25 c 20.5 34.25 l h ";
                    if(mode==140 && translated_clip)inner="q 1 0 0 1 4 0 cm 16.5 15.75 m 34.75 15.75 l 34.75 20 33.75 30 34.75 34.25 c 16.5 34.25 l h 1 0 0 1 -4 0 cm ";
                    if(mode==143)inner="q 20.5 15.75 18.2500000001 18.5 re ";
                    if(mode==145 || mode==146)inner="q 15.5 15.75 23.25 18.5 re ";
                    inner+=second_rule+(mode==135?std::string(" "):std::string(" n "));
                    if(mode==130)inner+="q 1 1 2 2 re W n ";
                    text=outer+inner+(mode==136?draw+" "+fill+" "+show:show+" "+fill+" "+draw);
                    if(mode==134)text=outer+show+" "+fill+" "+inner+draw;
                    if(mode==144)text=outer+inner+fill+" "+draw; /* no original Text */
                    text+=" Q";
                    if(mode==132)text+=" "+fill; /* after root2->leaf1 */
                    if(mode!=128 && mode!=142)text+=" Q";
                    if(mode==129)text+=" Q";
                    if(mode==133)text+=" 1 0 0 rg 34 9 6 5 re f"; /* restored GS0 */
                    if(mode==137)fonts.replaceKey("/G",font);
                    if(mode==138)text+=" /Missing Do";
                    if(mode==141)text+=" q 1 1 2 2 re W n Q";
                }
                if(mode>=147) {
                    /* Reverse siblings: Image/Fill freeze leaf1 before
                     * BT; real Q restores GS0, then a new q/W/n owns
                     * leaf2 exclusively for the original Text show. */
                    const std::string first_rule=(mode==148 || mode==169)?"W*":"W";
                    const std::string second_rule=(mode==148 || mode==169)?"W":"W*";
                    std::string first_scope=std::string(mode>=168?
                        "q 15.5 15.75 23.25 18.5 re ":"q 16.25 22.25 27 15.5 re ")+first_rule+" n "+
                        (mode==166?fill+" "+draw:draw+(mode==163?"":" "+fill));
                    std::string second_scope=std::string(mode==164?"":"q ")+
                        (mode==155 && !translated_clip?"1 0 0 1 1 0 cm ":"")+
                        (mode==155 && translated_clip?"21.5 15.75 18.25 18.5 re ":"20.5 15.75 18.25 18.5 re ");
                    if(mode==160)second_scope="q 20.5 15.75 18.2500000001 18.5 re ";
                    if(mode==161)second_scope="q 20.5 15.75 m 38.75 15.75 l 37.75 34.25 l 20.5 34.25 l h ";
                    if(mode==162)second_scope="q 20.5 15.75 m 38.75 15.75 l 38.75 20 37.75 30 38.75 34.25 c 20.5 34.25 l h ";
                    if(mode==162 && translated_clip)second_scope="q 1 0 0 1 4 0 cm 16.5 15.75 m 34.75 15.75 l 34.75 20 33.75 30 34.75 34.25 c 16.5 34.25 l h 1 0 0 1 -4 0 cm ";
                    if(mode>=168)second_scope="q 16.25 22.25 27 15.5 re ";
                    text=first_scope+(mode==151?" ":" Q ")+second_scope+second_rule+
                        (mode==157?" ":" n ");
                    if(mode==167)text+="Q ";
                    if(mode!=156)text+=show;
                    if(mode==153)text+=" "+draw;
                    if(mode==154)text+=" "+fill;
                    if(mode==165)text+=" "+show;
                    if(mode!=149 && mode!=164 && mode!=167)text+=" Q";
                    if(mode==150 || mode==151)text+=" Q";
                    if(mode==152)text+=" q 1 1 2 2 re W n Q";
                    if(mode==158)text+=" /Missing Do";
                    if(mode==159)fonts.replaceKey("/G",font);
                }
            }
        }
    }
    if(translated_clip && (mode==131 || mode==155)) {
        // Identity-CTM original with independently translated authored operands.
        const auto shift=[&](const std::string& from,const std::string& to) {
            const size_t at=text.find(from);
            if(at==std::string::npos)throw std::runtime_error("missing translated fixture operand");
            text.replace(at,from.size(),to);
        };
        shift("12 20 Td","13 20 Td");
        if(mode==131) {
            shift("12 20 7 10 re","13 20 7 10 re");
            shift("16 0 0 12 10 15 cm","16 0 0 12 11 15 cm");
        }
    }
    page.replaceKey("/Contents",pdf->newStream(text));
    QPDFWriter writer(*pdf);writer.setOutputMemory();writer.write();
    auto blob=writer.getBufferSharedPointer();
    return {reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize()};
}
#endif
