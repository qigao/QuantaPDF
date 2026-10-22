#include <quantapdf/quantapdf.h>
#include "snapshot/text_snapshot.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
using Obj=QPDFObjectHandle;
void check(bool ok,char const *expression,int line)
{ if(!ok) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,line,expression); std::exit(EXIT_FAILURE); } }
#define SCHECK(x) check((x),#x,__LINE__)
void near(float a,float b) { SCHECK(std::isfinite(a) && std::abs(a-b)<0.002f); }
void point(quantapdf_point p,float x,float y) { near(p.x,x); near(p.y,y); }
struct fixture {
    QPDF pdf;
    Obj page,resources,fonts,face;
    fixture()
    {
        pdf.emptyPDF(); page=pdf.makeIndirectObject(Obj::parse("<< /Type /Page /MediaBox [0 0 240 160] /CropBox [10 20 210 120] /UserUnit 2 >>"));
        resources=Obj::newDictionary(); fonts=Obj::newDictionary(); resources.replaceKey("/Font",fonts);
        face=pdf.makeIndirectObject(Obj::parse("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"));
        fonts.replaceKey("/F",face); page.replaceKey("/Resources",resources); pdf.addPage(page,false);
    }
    void content(std::string const& bytes) { page.replaceKey("/Contents",pdf.newStream(bytes)); }
    quantapdf_text_page *extract(quantapdf_status expected=QUANTAPDF_OK,bool encrypted=false)
    {
        { QPDFWriter w(pdf,NATIVE_DOCUMENT_OUTPUT_PDF); if(encrypted) w.setR6EncryptionParameters("pass","owner",true,true,true,true,true,true,qpdf_r3p_full,false); w.write(); }
        quantapdf_document *d=nullptr; quantapdf_page *p=nullptr;
        SCHECK(quantapdf_open(NATIVE_DOCUMENT_OUTPUT_PDF,encrypted?"pass":nullptr,&d)==QUANTAPDF_OK);
        SCHECK(quantapdf_load_page(d,0,&p)==QUANTAPDF_OK);
        auto t=reinterpret_cast<quantapdf_text_page *>(1);
        auto status=quantapdf_extract_structured_text(p,&t);
        if(status!=expected) std::fprintf(stderr,"structured status=%d expected=%d\n",status,expected);
        SCHECK(status==expected);
        quantapdf_drop_page(p); quantapdf_close(d);
        if(expected!=QUANTAPDF_OK) SCHECK(t==nullptr);
        return t;
    }
};
quantapdf_quad search(quantapdf_text_page *t,char const *needle)
{
    size_t count=0; quantapdf_search_result r={}; r.struct_size=sizeof(r);
    SCHECK(quantapdf_text_search(t,needle,&r,1,&count)==QUANTAPDF_OK && count==1);
    return r.quad;
}
void geometry()
{
    static const float expected[4][8]={
        {40,65.64f,53.34f,65.64f,40,84.14f,53.34f,84.14f},
        {134.36f,40,134.36f,53.34f,115.86f,40,115.86f,53.34f},
        {360,134.36f,346.66f,134.36f,360,115.86f,346.66f,115.86f},
        {65.64f,360,65.64f,346.66f,84.14f,360,84.14f,346.66f}};
    static const float direction[4][2]={{1,0},{0,1},{-1,0},{0,-1}};
    for(int rotation=0;rotation<4;++rotation) {
        fixture f; f.page.replaceKey("/Rotate",Obj::newInteger(rotation*90));
        f.content("BT /F 10 Tf 30 80 Td (A) Tj ET");
        auto t=f.extract(QUANTAPDF_OK,rotation==1); auto q=search(t,"A"); auto e=expected[rotation];
        point(q.ul,e[0],e[1]); point(q.ur,e[2],e[3]); point(q.ll,e[4],e[5]); point(q.lr,e[6],e[7]);
        SCHECK(t->block_count==1 && t->line_count==1 && t->span_count==1 && t->char_count==1);
        near(t->lines[0].direction_x,direction[rotation][0]); near(t->lines[0].direction_y,direction[rotation][1]);
        near(t->spans[0].font_size,20); quantapdf_drop_text_page(t);
    }
    for(auto family:{"/Times-Roman","/Courier"}) {
        fixture f; f.face.replaceKey("/BaseFont",Obj::newName(family));
        f.content("BT /F 10 Tf 30 80 Td (A) Tj ET"); auto t=f.extract(); auto q=search(t,"A");
        if(std::strcmp(family,"/Times-Roman")==0) { point(q.ul,40,66.34f); point(q.lr,54.44f,84.34f); }
        else { point(q.ul,40,67.42f); point(q.lr,52,83.14f); }
        quantapdf_drop_text_page(t);
    }
    fixture empty; auto t=empty.extract(); SCHECK(t->block_count==0 && t->char_count==0); quantapdf_drop_text_page(t);
}
void styles()
{
    fixture f; Obj states=Obj::newDictionary(); states.replaceKey("/Half",Obj::parse("<< /ca 0.5 >>")); f.resources.replaceKey("/ExtGState",states);
    f.content("BT /F 10 Tf 30 80 Td (A) Tj q /Half gs 1 0 0 rg (B) Tj Q (C) Tj 12 TL T* (D) Tj ET");
    auto t=f.extract(); SCHECK(t->line_count==2 && t->span_count==4);
    SCHECK(t->spans[0].argb==0xff000000u && t->spans[1].argb==0x80ff0000u && t->spans[2].argb==0xff000000u);
    SCHECK(std::strcmp(t->strings+t->spans[0].text_offset,"A")==0);
    (void)search(t,"ABC"); size_t count=99; SCHECK(quantapdf_text_search(t,"CD",nullptr,0,&count)==QUANTAPDF_OK && count==0);
    quantapdf_drop_text_page(t);
    f.content("BT /F 10 Tf 30 80 Td [(A) -500 (B)] TJ ET"); t=f.extract();
    SCHECK(t->char_count==3 && t->chars[1].codepoint==' '); (void)search(t,"A B"); quantapdf_drop_text_page(t);
    Obj colors=Obj::newDictionary(); colors.replaceKey("/RGB",Obj::newName("/DeviceRGB")); f.resources.replaceKey("/ColorSpace",colors);
    f.content("/RGB cs 0.2 0.4 0.6 sc BT /F 10 Tf (RGB) Tj ET"); t=f.extract(); SCHECK(t->spans[0].argb==0xff336699u); quantapdf_drop_text_page(t);
    f.content("0.4 g BT /F 10 Tf (Gray) Tj ET"); t=f.extract(); SCHECK(t->spans[0].argb==0xff666666u); quantapdf_drop_text_page(t);
}
void clusters()
{
    for(auto subtype:{"/CIDFontType2","/CIDFontType0"}) {
        fixture f; f.face.replaceKey("/Subtype",Obj::newName("/Type0")); f.face.replaceKey("/Encoding",Obj::newName("/Identity-H"));
        auto descendant=Obj::parse("<< /DW 600 /FontDescriptor << /Ascent 800 /Descent -200 >> >>"); descendant.replaceKey("/Subtype",Obj::newName(subtype));
        f.face.replaceKey("/DescendantFonts",Obj::newArray({descendant}));
        f.face.replaceKey("/ToUnicode",f.pdf.newStream("begincmap 1 begincodespacerange <0000> <FFFF> endcodespacerange 2 beginbfchar <0001> <00660069> <0002> <00650301> endbfchar endcmap"));
        f.content("BT /F 10 Tf 30 80 Td <00010002> Tj ET"); auto t=f.extract();
        SCHECK(t->char_count==4 && t->string_size==6); auto q=search(t,"fi"),single=search(t,"i");
        point(q.ul,40,64); point(q.lr,52,84); point(single.ul,40,64); point(single.lr,52,84);
        q=search(t,"e\xcc\x81"); point(q.ul,52,64); point(q.lr,64,84); quantapdf_drop_text_page(t);
    }
}
void forms()
{
    fixture f; auto form=f.pdf.newStream("BT /Local 10 Tf (A) Tj ET"),dict=form.getDict();
    dict.replaceKey("/Subtype",Obj::newName("/Form")); dict.replaceKey("/BBox",Obj::parse("[0 0 100 100]")); dict.replaceKey("/Matrix",Obj::parse("[0 1 -1 0 100 20]"));
    auto own=Obj::newDictionary(),fonts=Obj::newDictionary(); fonts.replaceKey("/Local",f.face); own.replaceKey("/Font",fonts); dict.replaceKey("/Resources",own);
    auto objects=Obj::newDictionary(); objects.replaceKey("/X",form); f.resources.replaceKey("/XObject",objects); f.content("/X Do");
    auto t=f.extract(); auto q=search(t,"A"); point(q.ul,165.64f,200); point(q.lr,184.14f,186.66f);
    near(t->lines[0].direction_x,0); near(t->lines[0].direction_y,-1); quantapdf_drop_text_page(t);
    // Inherited resource lookup and a nonuniform/sheared glyph cell.
    f.page.removeKey("/Resources"); f.pdf.getRoot().getKey("/Pages").replaceKey("/Resources",f.resources);
    f.content("2 0.5 1 3 4 5 cm BT /F 10 Tf 30 20 Td (A) Tj ET"); t=f.extract();
    // Raw top-left=(91.18,101.54), bottom-right=(95.27,77.125).
    point(t->chars[0].quad.ul,162.36f,36.92f); point(t->chars[0].quad.lr,170.54f,85.75f);
    near(t->spans[0].font_size,63.245552f); quantapdf_drop_text_page(t);
}
void refusals()
{
    fixture f; f.content("BT /F 10 Tf (prefix) Tj ET 0 1 1 0 k BT (bad) Tj ET"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.content("0 1 1 0 k 0 g BT /F 10 Tf (reset) Tj ET"); auto t=f.extract(); quantapdf_drop_text_page(t);
    f.content("BT /F 10 Tf (prefix) Tj ET /Unknown cs BT (bad) Tj ET"); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.content("BT /F 10 Tf (prefix) Tj ET /Span << /ActualText (replacement) >> BDC EMC"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.content("BT /F 10 Tf (prefix) Tj ET bogus"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.content("BT /F 10 Tf 1 1 2 2 0 0 Tm (A) Tj ET"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.content("BT /F 0 Tf (A) Tj ET"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.content("BT /F 10 Tf (A) Tj ET"); f.face.replaceKey("/FontDescriptor",Obj::parse("<< /Ascent 800 >>")); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    // Missing geometry metrics must not break the separate plain-text path.
    quantapdf_document *d=nullptr; quantapdf_page *p=nullptr; char *bytes=nullptr; size_t size=0;
    SCHECK(quantapdf_open(NATIVE_DOCUMENT_OUTPUT_PDF,nullptr,&d)==QUANTAPDF_OK);
    SCHECK(quantapdf_load_page(d,0,&p)==QUANTAPDF_OK);
    SCHECK(quantapdf_extract_text(p,&bytes,&size)==QUANTAPDF_OK && size==1 && bytes[0]=='A');
    quantapdf_free(bytes); quantapdf_drop_page(p); quantapdf_close(d);
    f.face.replaceKey("/FontDescriptor",Obj::parse("<< /Ascent -300 /Descent 200 >>")); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.face.removeKey("/FontDescriptor"); f.content("BT /F 1 Tf ("+std::string(262145u,'A')+") Tj ET"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
}
}
void quantapdf_test_native_structured() { geometry(); styles(); clusters(); forms(); refusals(); }
