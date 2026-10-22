#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
using Obj=QPDFObjectHandle;
void check(bool ok,char const *expression,int line)
{ if(!ok) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,line,expression); std::exit(EXIT_FAILURE); } }
#define TCHECK(x) check((x),#x,__LINE__)
struct fixture {
    QPDF pdf;
    Obj page,resources,fonts,face;
    fixture()
    {
        pdf.emptyPDF(); page=pdf.makeIndirectObject(Obj::parse("<< /Type /Page /MediaBox [0 0 300 200] >>"));
        resources=Obj::newDictionary(); fonts=Obj::newDictionary(); resources.replaceKey("/Font",fonts);
        face=pdf.makeIndirectObject(Obj::parse("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"));
        fonts.replaceKey("/F",face); page.replaceKey("/Resources",resources); pdf.addPage(page,false);
    }
    void content(std::string const& bytes) { page.replaceKey("/Contents",pdf.newStream(bytes)); }
    void cmap(std::string const& sections,bool cid=false)
    {
        std::string ranges=cid?"<0000> <FFFF>":"<00> <FF>";
        face.replaceKey("/ToUnicode",pdf.newStream("begincmap 1 begincodespacerange "+ranges+" endcodespacerange "+sections+" endcmap"));
        if(cid) {
            face.replaceKey("/Subtype",Obj::newName("/Type0")); face.replaceKey("/Encoding",Obj::newName("/Identity-H"));
            face.replaceKey("/DescendantFonts",Obj::newArray({pdf.makeIndirectObject(Obj::parse("<< /Type /Font /Subtype /CIDFontType2 /DW 600 /W [1 [600 600] 3 6 600] >>"))}));
        }
    }
    std::string extract(quantapdf_status expected=QUANTAPDF_OK,bool encrypted=false)
    {
        { QPDFWriter writer(pdf,NATIVE_DOCUMENT_OUTPUT_PDF); if(encrypted) writer.setR6EncryptionParameters("pass","owner",true,true,true,true,true,true,qpdf_r3p_full,false); writer.write(); }
        quantapdf_document *document=nullptr; quantapdf_page *loaded=nullptr;
        TCHECK(quantapdf_open(NATIVE_DOCUMENT_OUTPUT_PDF,encrypted?"pass":nullptr,&document)==QUANTAPDF_OK);
        TCHECK(quantapdf_load_page(document,0,&loaded)==QUANTAPDF_OK);
        char *bytes=reinterpret_cast<char *>(1); size_t size=99;
        auto status=quantapdf_extract_text(loaded,&bytes,&size);
        if(status!=expected) std::fprintf(stderr,"text status=%d expected=%d\n",status,expected);
        TCHECK(status==expected);
        quantapdf_drop_page(loaded); quantapdf_close(document);
        if(expected!=QUANTAPDF_OK) { TCHECK(bytes==nullptr && size==0); return {}; }
        TCHECK(bytes!=nullptr && bytes[size]==0 && std::strlen(bytes)==size);
        std::string result(bytes,size); quantapdf_free(bytes); return result;
    }
};
void simple()
{
    fixture f; f.content("BT /F 18 Tf 12 80 Td (Hello Caf\\351 \\200) Tj ET");
    TCHECK(f.extract()=="Hello Caf\xc3\xa9 \xe2\x82\xac");
    TCHECK(f.extract(QUANTAPDF_OK,true)=="Hello Caf\xc3\xa9 \xe2\x82\xac");
    f.content(""); TCHECK(f.extract().empty());
    f.face.removeKey("/Encoding"); f.content("BT /F 12 Tf ('`) Tj ET");
    TCHECK(f.extract()=="\xe2\x80\x99\xe2\x80\x98");
    f.content("BT /F 12 Tf (\\351) Tj ET"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
}
void mappings()
{
    fixture f; f.cmap("2 beginbfchar <0001> <00660069> <0002> <00650301> endbfchar 2 beginbfrange <0003> <0004> <D83DDE00> <0005> <0006> [<0041> <0042>] endbfrange",true);
    f.content("BT /F 12 Tf <000100020003000400050006> Tj ET");
    TCHECK(f.extract()=="fie\xcc\x81\xf0\x9f\x98\x80\xf0\x9f\x98\x81" "AB");
    fixture one; one.cmap("1 beginbfrange <20> <22> <0041> endbfrange");
    one.content("BT /F 12 Tf <202122> Tj ET"); TCHECK(one.extract()=="ABC");
    one.cmap("/WMode 0 def 1 beginbfrange <20> <22> <0041> endbfrange");
    TCHECK(one.extract()=="ABC");
    // ToUnicode takes precedence over a Differences encoding.
    one.face.replaceKey("/Encoding",Obj::newName("/MacRomanEncoding"));
    TCHECK(one.extract()=="ABC");
    one.face.replaceKey("/Encoding",Obj::parse("<< /BaseEncoding /WinAnsiEncoding /Differences [32 /space] >>"));
    TCHECK(one.extract()=="ABC");
    one.face.removeKey("/ToUnicode"); one.content("BT /F 12 Tf ( ) Tj ET"); one.extract(QUANTAPDF_ERROR_UNSUPPORTED);
}
void state()
{
    fixture f;
    f.page.replaceKey("/Contents",Obj::newArray({f.pdf.newStream("BT /F 10 Tf 1 0 0 1 10 100 Tm [(A)"),f.pdf.newStream("-500 (B)] TJ 12 TL (C) ' 1 2 (D) \" ET")}));
    TCHECK(f.extract()=="A B\r\nC\r\nD");
    // Text matrices survive q/Q; text parameters are restored.
    f.content("BT /F 10 Tf q /F 20 Tf (A) Tj Q (B) Tj ET"); TCHECK(f.extract()=="AB");
    // Explicit glyph positioning must preserve adjacent glyphs and ligatures.
    f.content("BT /F 30 Tf 1 0 0 1 30 140 Tm (A) Tj 1 0 0 1 48 140 Tm (V) Tj ET"); TCHECK(f.extract()=="AV");
    f.content("BT /F 10 Tf 1 Tc 2 Tw 80 Tz 3 Ts 1 0 0 1 10 100 Tm (A B) Tj 0 -20 TD (C) Tj T* (D) Tj ET");
    TCHECK(f.extract()=="A B\r\nC\r\nD");
    Obj gs=Obj::newDictionary(),g=Obj::newDictionary(); g.replaceKey("/Font",Obj::newArray({f.face,Obj::newInteger(10)})); gs.replaceKey("/G",g); f.resources.replaceKey("/ExtGState",gs);
    f.content("/G gs BT (GS) Tj ET"); TCHECK(f.extract()=="GS");
}
void forms()
{
    fixture f; Obj objects=Obj::newDictionary(); f.resources.replaceKey("/XObject",objects);
    Obj form=f.pdf.newStream("BT /Local 10 Tf (Form) Tj ET"); auto d=form.getDict();
    d.replaceKey("/Subtype",Obj::newName("/Form")); d.replaceKey("/BBox",Obj::parse("[0 0 100 100]")); d.replaceKey("/Matrix",Obj::parse("[0 1 -1 0 100 20]"));
    Obj own=Obj::newDictionary(),fonts=Obj::newDictionary(); fonts.replaceKey("/Local",f.face); own.replaceKey("/Font",fonts); d.replaceKey("/Resources",own); objects.replaceKey("/X",form);
    f.content("BT /F 10 Tf (Page) Tj ET /X Do"); TCHECK(f.extract()=="Page\r\nForm");
    d.removeKey("/Resources"); form.replaceStreamData("BT /F 10 Tf (Inherited) Tj ET",Obj::newNull(),Obj::newNull());
    f.pdf.getRoot().getKey("/Pages").replaceKey("/Resources",f.resources); f.page.removeKey("/Resources");
    f.content("/X Do"); TCHECK(f.extract()=="Inherited");
    d.replaceKey("/Resources",f.resources); form.replaceStreamData("BT /F 10 Tf (partial) Tj ET /X Do",Obj::newNull(),Obj::newNull());
    f.extract(QUANTAPDF_ERROR_FORMAT);
}
void refusals()
{
    fixture f;
    f.content("BT /F 12 Tf (prefix) Tj ET /Span << /ActualText (replacement) >> BDC EMC"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.content("BT /F 12 Tf (prefix) Tj ET bogus"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.content("BT /F 12 Tf (prefix) Tj ET BI /W 1 /H 1 /BPC 8 /CS /G ID x EI"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.content("BT /F 12 Tf (prefix) Tj"); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.content("BT /F 12 Tf (prefix) Tj ET Q"); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.content("BT /F 12 Tf (\\201) Tj ET"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.cmap("1 beginbfchar <41> <D800> endbfchar"); f.content("BT /F 12 Tf (A) Tj ET"); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.cmap("2 beginbfchar <41> <0041> <41> <0042> endbfchar"); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.cmap("1 beginbfchar <41> <0000> endbfchar"); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.cmap("1 beginbfrange <41> <42> [<0041>] endbfrange"); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.cmap("1 beginbfchar <41> <0041> endbfchar /Other usecmap"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.cmap("1 beginbfchar <42> <0042> endbfchar"); TCHECK(f.extract()=="\xef\xbf\xbd");
    f.cmap("0 { 1 beginbfchar <41> <0041> endbfchar } repeat"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.cmap("/WMode 1 def 1 beginbfchar <41> <0041> endbfchar"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.cmap("1 beginbfchar <41> <0041> endbfchar");
    f.face.getKey("/ToUnicode").getDict().replaceKey("/UseCMap",Obj::newName("/Identity-H")); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    f.cmap("1 beginbfchar <41> <0041> endbfchar");
    f.face.getKey("/ToUnicode").replaceStreamData("begincmap 2 begincodespacerange <00> <80> <80> <FF> endcodespacerange endcmap",Obj::newNull(),Obj::newNull()); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.face.getKey("/ToUnicode").replaceStreamData("begincmap 1 begincodespacerange <00> <40> endcodespacerange 1 beginbfchar <30> <0041> endbfchar endcmap",Obj::newNull(),Obj::newNull()); f.extract(QUANTAPDF_ERROR_FORMAT);
    f.face.removeKey("/ToUnicode"); f.face.replaceKey("/Subtype",Obj::newName("/Type3")); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    fixture limits; limits.content(std::string(8u*1024u*1024u,' ')); limits.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    std::string nested; for(int i=0;i<129;++i) nested+="q "; limits.content(nested); limits.extract(QUANTAPDF_ERROR_UNSUPPORTED);
}
}
void quantapdf_test_native_text() { simple(); mappings(); state(); forms(); refusals(); }
