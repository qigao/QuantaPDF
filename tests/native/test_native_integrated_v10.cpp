/* Public Native V10 joint Source admission for six grammar additions without
 * implementing Stroke. QPDF independently authors ORIGINAL source PDFs;
 * a single owned C11 raster is the only actual pixel producer. */
#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"Native V10 integration failed line %d: %s\n", \
                 __LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

using Obj=QPDFObjectHandle;
static const char *const filename="native-original-v10-integration.pdf";

static bool write_pdf(const std::string& bytes)
{
    FILE *f=nullptr;
#ifdef _WIN32
    if(fopen_s(&f,filename,"wb")!=0)return false;
#else
    f=std::fopen(filename,"wb");
#endif
    if(f==nullptr)return false;
    const bool wrote=
        std::fwrite(bytes.data(),1u,bytes.size(),f)==bytes.size();
    return std::fclose(f)==0 && wrote;
}

static std::string original(const std::string& operators,
                            bool with_image,int quarters,int rotation)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    page.replaceKey("/Parent",pages);
    page.replaceKey("/UserUnit",Obj::parse(
        std::to_string((double)quarters/4.0)));
    page.replaceKey("/Rotate",Obj::newInteger(rotation));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    if(!operators.empty())
        page.replaceKey("/Contents",pdf->newStream(operators));
    if(with_image) {
        const std::string pixels(
            "\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
        auto image=pdf->newStream(pixels);
        auto dict=image.getDict();
        dict.replaceKey("/Type",Obj::newName("/XObject"));
        dict.replaceKey("/Subtype",Obj::newName("/Image"));
        dict.replaceKey("/Width",Obj::newInteger(2));
        dict.replaceKey("/Height",Obj::newInteger(2));
        dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
        dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
        auto objects=Obj::newDictionary();
        objects.replaceKey("/I",image);
        auto resources=Obj::newDictionary();
        resources.replaceKey("/XObject",objects);
        page.replaceKey("/Resources",resources);
    }
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
}


/* QPDFWriter canonicalizes numeric spellings. To exercise #506's
 * ORIGINAL zero-padded PDF tokens together with signed re, n, W*,
 * q/Q and stroking-only metadata, author the original objects and
 * complete xref bytes directly, using no second PDF interpreter or
 * renderer. All stream lengths and xref offsets are exact. */
static std::string literal_original(
    const std::string& unit,const std::string& box,int crop_mode,
    int rotation,const std::string& operators,bool with_image,
    const std::string& visible_crop={})
{
    std::string bytes="%PDF-1.7\n";
    size_t offsets[6]={};
    const auto object=[&](size_t id,const std::string& contents) {
        offsets[id]=bytes.size();
        bytes+=std::to_string(id)+" 0 obj\n"+contents+"\nendobj\n";
    };
    object(1u,"<< /Type /Catalog /Pages 2 0 R >>");
    const std::string& crop=visible_crop.empty()?box:visible_crop;
    const std::string inherited=crop_mode==2?
        " /CropBox "+crop:std::string();
    const std::string local=crop_mode==1?
        " /CropBox "+crop:std::string();
    object(2u,"<< /Type /Pages /Count 1 /Kids [3 0 R]"+
        inherited+" >>");
    object(3u,"<< /Type /Page /Parent 2 0 R /MediaBox "+box+
        local+" /UserUnit "+unit+
        " /Rotate "+std::to_string(rotation)+
        " /Contents 4 0 R"+
        (with_image?
            " /Resources << /XObject << /I 5 0 R >> >>":
            std::string())+" >>");
    const std::string paint=operators+"\n";
    object(4u,"<< /Length "+std::to_string(paint.size())+
        " >>\nstream\n"+paint+"endstream");
    if(with_image) {
        const std::string samples(
            "\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
        object(5u,"<< /Type /XObject /Subtype /Image /Width 2"
            " /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB"
            " /Length 12 >>\nstream\n"+samples+"\nendstream");
    }
    const size_t count=with_image?5u:4u;
    const size_t xref=bytes.size();
    bytes+="xref\n0 "+std::to_string(count+1u)+
        "\n0000000000 65535 f \n";
    for(size_t id=1u;id<=count;++id) {
        char record[32]={};
        const int n=std::snprintf(
            record,sizeof(record),"%010zu 00000 n \n",offsets[id]);
        if(n!=20)
            throw std::runtime_error("invalid original lexical xref");
        bytes.append(record,static_cast<size_t>(n));
    }
    bytes+="trailer\n<< /Root 1 0 R /Size "+
        std::to_string(count+1u)+" >>\nstartxref\n"+
        std::to_string(xref)+"\n%%EOF\n";
    return bytes;
}

static int render(const std::string& bytes,int quarters,int rotation,
                  int dpi,std::vector<unsigned char>& rgb,
    int visible_w=32,int visible_h=24)
{
    CHECK(write_pdf(bytes));
    quantapdf_context *context=nullptr;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(
        context,filename,nullptr,&doc));
    CHECK(std::remove(filename)==0);
    OK(quantapdf_load_page(doc,0,&page));
    quantapdf_render_options opts{};
    opts.struct_size=sizeof(opts);
    opts.dpi=(float)dpi;
    CHECK(quantapdf_render_page_with_options(
        page,&opts,&bitmap)==QUANTAPDF_OK && bitmap!=nullptr);
    int width=0,height=0,stride=0,channels=0;
    const unsigned char *pixels=nullptr;
    size_t bytes_count=0u;
    OK(quantapdf_bitmap_dimensions(
        bitmap,&width,&height,&stride,&channels));
    OK(quantapdf_bitmap_data(bitmap,&pixels,&bytes_count));
    const int sw=(rotation==90)?visible_h:visible_w;
    const int sh=(rotation==90)?visible_w:visible_h;
    const int expected_w=(sw*quarters*dpi+287)/288;
    const int expected_h=(sh*quarters*dpi+287)/288;
    CHECK(width==expected_w && height==expected_h &&
          stride==width*3 && channels==3 && pixels!=nullptr &&
          bytes_count==(size_t)stride*(size_t)height);
    rgb.assign(pixels,pixels+bytes_count);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    quantapdf_drop_context(context);
    /* Public Bitmap owns RGB independently of retired input handles. */
    const unsigned char *retained=nullptr;
    size_t retained_size=0u;
    OK(quantapdf_bitmap_data(bitmap,&retained,&retained_size));
    CHECK(retained!=nullptr && retained_size==rgb.size() &&
          std::memcmp(retained,rgb.data(),retained_size)==0);
    quantapdf_drop_bitmap(bitmap);
    return EXIT_SUCCESS;
}

struct case_data {
    const char *plain;
    const char *decorated;
    bool image;
    bool blank;
};
static const case_data cases[]={
    {"",
     "0 w 0 J 0 j 1 M [] 0 d 0 G 0 0 0 RG 0 0 0 1 K 2 2 3 3 re n",false,true},
    {"1 0 0 rg 4 4 8 6 re f",
     "0 w 1 J 2 j 5 M [1 2] 1 d 0.3 G 0 1 0 RG 0 0 0 1 K 2 2 3 3 re n 1 0 0 rg 12 10 -8 -6 re f",false,false},
    {"0.5 g 4 4 8 6 re f",
     "2.25 w 2 J 1 j 10 M [2 0 3] 0 d 1 G 1 0 1 RG 0 1 0 1 K 0.5 g 4 10 8 -6 re f",false,false},
    {"q 1 0 0 rg 4 4 8 6 re f Q 0 0 1 rg 12 10 7 5 re f",
     "q 0 w 2 J 1 j 1 M [1] 0 d 0 G 1 1 0 RG 1 0 0 rg 12 4 -8 6 re f Q 0 0 0 1 K 0 0 1 rg 19 15 -7 -5 re f",false,false},
    {"q 16 0 0 12 8 3 cm /I Do Q",
     "1 w 0 J 0 j 2 M [1 2] 0 d 0.2 G 0.1 0.2 0.3 RG 0 0 0 0 K 2 2 3 3 re n q 16 0 0 12 8 3 cm /I Do Q",true,false},
    {"q 16 0 0 12 8 3 cm /I Do Q 1 0 0 rg 4 4 8 6 re f",
     "0 w 1 J 1 j 1 M [] 0 d 0 G 0 0 0 RG q 16 0 0 12 8 3 cm /I Do Q 0 0 0 1 K 1 0 0 rg 12 10 -8 -6 re f",true,false},
    {"q 12 6 8 9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q",
     "1 w 2 J 1 j 4 M [1 3] 0 d 1 G 0 1 0 RG 2 2 3 3 re n q 20 15 -8 -9 re W* n 0 0 0 1 K q 16 0 0 12 8 3 cm /I Do Q Q",true,false},
    {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q 0.875 g 24 17 5 3 re f",
     "q 0 w 0 J 0 j 5 M [1 0 2] 1 d 0.5 G 20 15 -8 -9 re W n 1 0 1 RG q 16 0 0 12 8 3 cm /I Do Q Q 0 0 0 1 K 0.875 g 29 20 -5 -3 re f",true,false},
    {"1 0 0 rg 4 4 8 6 re f q 16 0 0 12 8 3 cm /I Do Q",
     "0 w 2 J 1 j 1 M [] 0 d 0 G 0 0 0 RG 0 0 0 1 K 2 2 3 3 re n 1 0 0 rg 12 10 -8 -6 re f q 16 0 0 12 8 3 cm /I Do Q",true,false}
};

static int positive()
{
    for(const auto& data:cases)
    for(int quarters:{4,5})
    for(int rotation:{0,90})
    for(int dpi:{72,96,144}) {
        std::vector<unsigned char> plain,got;
        CHECK(render(original(data.plain,data.image,quarters,rotation),
                     quarters,rotation,dpi,plain)==EXIT_SUCCESS);
        CHECK(render(original(data.decorated,data.image,quarters,rotation),
                     quarters,rotation,dpi,got)==EXIT_SUCCESS);
        CHECK(plain.size()==got.size() &&
              std::memcmp(plain.data(),got.data(),got.size())==0);
        bool all_white=true,has_ink=false;
        for(size_t k=0;k<got.size();k+=3u) {
            const bool white=got[k]==255u &&
                got[k+1u]==255u && got[k+2u]==255u;
            all_white&=white;has_ink|=!white;
        }
        CHECK(data.blank?all_white:has_ink);
    }
    return EXIT_SUCCESS;
}

static int negative()
{
    const char *const bad[]={
        "1 0 0 rg 12 10 -8 -6 re f 1 w 2 G 1 M [1] 0 d S",
        "1 0 0 rg 4 4 8 6 re f 12 10 -8 -6 re 0.3 w f",
        "0 w 0 G 0 0 0 RG 0 0 0 1 K 1 M [] 0 d 12 10 -8 -6 re f BT ET",
        "0 w 1 0 0 rg 12 10 -8 -6 re f 1000001 M",
        "0 w 12 10 -8 -6 re f [0 0] 0 d",
        "0 G 2 2 3 3 re n 12 10 -8 -6 re f 2 G",
        "0 w 0 G 1 0 0 rg 12 10 -8 -6 re f 0 -0.1 0 RG",
        "0 w 12 10 -8 -6 re f Q",
        "12 10 -8 -6 re W 1 M n",
        "q 20 15 -8 -9 re W n q 16 0 0 12 8 3 cm /Missing Do Q Q",
        "12 10 0 -6 re f",
        "0 G 2 2 3 3 re n 12 10 -8 -6 re f 1 n",
        "1 0 0 rg 12 10 -8 -6 re f q 16 0 0 12 8 3 cm /I Do Q S",
        "q 20 15 -8 -9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q 0 0 2 RG",
        "1 0 0 rg 12 10 -8 -6 re f 0 0 0 1 K 0 1 1 0 k",
        "1 0 0 rg 12 10 -8 -6 re f q 16 0 0 12 8 3 cm /I Do Q [0 0] 0 d"
    };
    for(const char *ops:bad)
    for(int dpi:{72,96,144}) {
        const bool with_image=std::strstr(ops,"/I Do")!=nullptr;
        CHECK(write_pdf(original(ops,with_image,4,0)));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=
            reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_open(filename,nullptr,&doc));
        CHECK(std::remove(filename)==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);
        opts.dpi=(float)dpi;
        const auto status=quantapdf_render_page_with_options(
            page,&opts,&bitmap);
        CHECK(status!=QUANTAPDF_OK && bitmap==nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    return EXIT_SUCCESS;
}


/* Combined admission must preserve the actual original decimal
 * lexemes, not merely QPDF's normalized numerical observations. */
static int lexical_original_cases()
{
    const char *const canonical_units[]={
        "0.25","1","1.25","4"
    };
    const char *const padded_units[]={
        "0.250000000000000000","1.000000000000000000",
        "1.250000000000000000","4.000000000000000000"
    };
    const int quarters[]={1,4,5,16};
    const std::string box="[0 0 32 24]";
    const std::string padded_box=
        std::string("[0.000000000000000000 0.000000000000000000 ")+
        "32.00000000000000000 24.00000000000000000]";
    const case_data lexical[]={
        {"",
         "0 w 0 J 0 j 1 M [] 0 d 0 G 0 0 0 RG 0 0 0 1 K "
         "2 2 3 3 re n",false,true},
        {"1 0 0 rg 4 4 8 6 re f",
         "0 w 1 J 2 j 3 M [1 2] 0 d 0.3 G 1 0.5 0 RG "
         "0 0 0 1 K 2 2 3 3 re n "
         "1 0 0 rg 12 10 -8 -6 re f",false,false},
        {"q 12 6 8 9 re W* n "
         "q 16 0 0 12 8 3 cm /I Do Q Q",
         "0 w 1 J 2 j 3 M [1 2] 0 d 0.3 G 1 0.5 0 RG "
         "0 0 0 1 K 2 2 3 3 re n "
         "q 20 15 -8 -9 re W* n "
         "q 16 0 0 12 8 3 cm /I Do Q Q",true,false}
    };
    for(const auto& item:lexical)
    for(size_t factor=0u;factor<4u;++factor)
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop_mode=0;crop_mode<=2;++crop_mode) {
        std::vector<unsigned char> reference;
        const auto baseline=literal_original(
            canonical_units[factor],box,crop_mode,rotation,
            item.plain,item.image);
        CHECK(render(baseline,quarters[factor],rotation,dpi,
                     reference)==EXIT_SUCCESS);
        bool has_ink=false,all_white=true;
        for(size_t i=0u;i<reference.size();i+=3u) {
            const bool white=reference[i]==255u &&
                reference[i+1u]==255u && reference[i+2u]==255u;
            has_ink|=!white;all_white&=white;
        }
        CHECK(item.blank?all_white:has_ink);
        for(bool pad_unit:{false,true})
        for(bool pad_box:{false,true}) {
            const auto authored=literal_original(
                pad_unit?padded_units[factor]:canonical_units[factor],
                pad_box?padded_box:box,crop_mode,rotation,
                item.decorated,item.image);
            if(pad_unit)
                CHECK(authored.find(padded_units[factor])!=
                      std::string::npos);
            if(pad_box)
                CHECK(authored.find(padded_box)!=std::string::npos);
            std::vector<unsigned char> got;
            CHECK(render(authored,quarters[factor],rotation,dpi,
                         got)==EXIT_SUCCESS);
            CHECK(got.size()==reference.size() &&
                  std::memcmp(got.data(),reference.data(),got.size())==0);
        }
    }
    return EXIT_SUCCESS;
}

static int lexical_rejection_cases()
{
    const std::string box="[0 0 32 24]";
    const std::string bad_box=
        "[0 0 32.00000000000000001 24]";
    const std::string bad_crop=
        "[0 0 32.00000000000000001 24]";
    const std::string painted=
        "0 w 0 J 0 j 1 M [] 0 d 0 G 0 0 0 RG 0 0 0 1 K "
        "1 0 0 rg 12 10 -8 -6 re f";
    const std::vector<std::string> invalid={
        literal_original("1.250000000000000001",box,0,0,painted,false),
        literal_original("1.2500000000000000000",box,0,0,painted,false),
        literal_original("1.25",bad_box,0,0,painted,false),
        literal_original("1.25",bad_crop,1,0,painted,false),
        literal_original("1.25",bad_crop,2,0,painted,false),
        literal_original("1.250000000000000000",box,0,0,
                         painted+" S",false)
    };
    for(const auto& source:invalid)
    for(int dpi:{72,96,144}) {
        CHECK(write_pdf(source));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=
            reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_open(filename,nullptr,&doc));
        CHECK(std::remove(filename)==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options options{};
        options.struct_size=sizeof(options);
        options.dpi=static_cast<float>(dpi);
        const auto status=quantapdf_render_page_with_options(
            page,&options,&bitmap);
        CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && bitmap==nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    return EXIT_SUCCESS;
}


/* An equal-to-Media CropBox cannot qualify clipping semantics.
 * These ORIGINAL PDFs deliberately shrink the visible page to 16x16:
 * clipped RAW images, authored negative-width re and Q-restored
 * nonstroking Fill can cross the viewport boundary. Verify that the
 * same single C11 renderer preserves all paint and owner semantics
 * with local/inherited, canonically spelled or padded CropBox values. */
static int active_crop_original_cases()
{
    const char *const canonical_units[]={"0.25","1","1.25","4"};
    const char *const padded_units[]={
        "0.250000000000000000","1.000000000000000000",
        "1.250000000000000000","4.000000000000000000"
    };
    const int quarters[]={1,4,5,16};
    const std::string box="[0 0 32 24]";
    const std::string padded_box="[0.000000000000000000 0.000000000000000000 32.00000000000000000 24.00000000000000000]";
    const std::string crop="[8 4 24 20]";
    const std::string padded_crop="[8.000000000000000000 4.000000000000000000 24.00000000000000000 20.00000000000000000]";
    const case_data active[]={
        {"1 0 0 rg 4 4 8 6 re f",
         "0 w 2 J 1 j 3 M [1 2] 0 d 0 G 1 0 1 RG 0 0 0 1 K 2 2 3 3 re n 1 0 0 rg 12 10 -8 -6 re f",false,false},
        {"q 12 6 8 9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q",
         "0 w 2 J 1 j 3 M [1 2] 0 d 0 G 1 0 1 RG 0 0 0 1 K 2 2 3 3 re n q 20 15 -8 -9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q",true,false},
        {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q 0.875 g 18 17 5 3 re f",
         "q 0 w 2 J 1 j 3 M [1 2] 0 d 0 G 1 0 1 RG 0 0 0 1 K 20 15 -8 -9 re W n q 16 0 0 12 8 3 cm /I Do Q Q 0.875 g 23 20 -5 -3 re f",true,false},
        {"q 16 0 0 12 8 3 cm /I Do Q 1 0 0 rg 4 4 8 6 re f",
         "0 w 2 J 1 j 3 M [1 2] 0 d 0 G 1 0 1 RG 0 0 0 1 K 2 2 3 3 re n q 16 0 0 12 8 3 cm /I Do Q 1 0 0 rg 12 10 -8 -6 re f",true,false}
    };
    for(const auto& item:active)
    for(size_t factor=0u;factor<4u;++factor)
    for(int rotation:{0,90})
    for(int dpi:{72,96,144})
    for(int crop_mode:{1,2}) {
        std::vector<unsigned char> reference;
        const auto baseline=literal_original(
            canonical_units[factor],box,crop_mode,rotation,
            item.plain,item.image,crop);
        CHECK(render(baseline,quarters[factor],rotation,dpi,
                     reference,16,16)==EXIT_SUCCESS);
        bool has_ink=false,has_white=false;
        for(size_t i=0u;i<reference.size();i+=3u) {
            const bool white=reference[i]==255u &&
                reference[i+1u]==255u && reference[i+2u]==255u;
            has_ink|=!white;
            has_white|=white;
        }
        CHECK(has_ink && has_white);
        for(bool pad_unit:{false,true})
        for(bool pad_media:{false,true})
        for(bool pad_crop:{false,true}) {
            const auto original_bytes=literal_original(
                pad_unit?padded_units[factor]:canonical_units[factor],
                pad_media?padded_box:box,crop_mode,rotation,
                item.decorated,item.image,
                pad_crop?padded_crop:crop);
            if(pad_unit)
                CHECK(original_bytes.find(padded_units[factor])!=
                      std::string::npos);
            if(pad_media)
                CHECK(original_bytes.find("/MediaBox "+padded_box)!=
                      std::string::npos);
            if(pad_crop)
                CHECK(original_bytes.find("/CropBox "+padded_crop)!=
                      std::string::npos);
            std::vector<unsigned char> got;
            CHECK(render(original_bytes,quarters[factor],rotation,dpi,
                         got,16,16)==EXIT_SUCCESS);
            CHECK(got.size()==reference.size() &&
                  std::memcmp(reference.data(),got.data(),got.size())==0);
        }
    }
    return EXIT_SUCCESS;
}

/* A reduced original CropBox must not turn invalid precision, late
 * Stroke/GS, invalid path or unclosed Clip into partially painted
 * pixels. Both local and inherited CropBoxes are tested at real DPI. */
static int active_crop_rejection_cases()
{
    const std::string box="[0 0 32 24]";
    const std::string crop="[8 4 24 20]";
    const std::string bad_crop="[8 4 24.00000000000000001 20]";
    const std::string painted=
        "0 w 0 J 0 j 2 M [1 2] 0 d 0 G 0 0 0 RG 0 0 0 1 K "
        "1 0 0 rg 12 10 -8 -6 re f";
    const std::string clipped=
        "q 20 15 -8 -9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q";
    for(int crop_mode:{1,2}) {
        const std::vector<std::string> invalid={
            literal_original("1.25",box,crop_mode,0,painted,false,bad_crop),
            literal_original("1.250000000000000001",box,crop_mode,0,painted,false,crop),
            literal_original("1.2500000000000000000",box,crop_mode,0,painted,false,crop),
            literal_original("1.25",box,crop_mode,0,painted+" S",false,crop),
            literal_original("1.25",box,crop_mode,0,painted+" 2 G",false,crop),
            literal_original("1.25",box,crop_mode,0,painted+" 1 n",false,crop),
            literal_original("1.25",box,crop_mode,0,painted+" 12 10 -0 -6 re f",false,crop),
            literal_original("1.25",box,crop_mode,0,painted+" 0 1 1 0 k",false,crop),
            literal_original("1.25",box,crop_mode,0,painted+" [0 0] 0 d",false,crop),
            literal_original("1.25",box,crop_mode,0,clipped+" 1 M S",true,crop)
        };
        for(const auto& source:invalid)
        for(int dpi:{72,96,144}) {
            CHECK(write_pdf(source));
            quantapdf_document *doc=nullptr;
            quantapdf_page *page=nullptr;
            quantapdf_bitmap *bitmap=
                reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            OK(quantapdf_open(filename,nullptr,&doc));
            CHECK(std::remove(filename)==0);
            OK(quantapdf_load_page(doc,0,&page));
            quantapdf_render_options opts{};
            opts.struct_size=sizeof(opts);
            opts.dpi=static_cast<float>(dpi);
            const auto status=quantapdf_render_page_with_options(
                page,&opts,&bitmap);
            CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED &&
                  bitmap==nullptr);
            quantapdf_drop_page(page);
            quantapdf_close(doc);
        }
    }
    return EXIT_SUCCESS;
}

int main()
{
    CHECK(positive()==EXIT_SUCCESS);
    CHECK(negative()==EXIT_SUCCESS);
    CHECK(lexical_original_cases()==EXIT_SUCCESS);
    CHECK(lexical_rejection_cases()==EXIT_SUCCESS);
    CHECK(active_crop_original_cases()==EXIT_SUCCESS);
    CHECK(active_crop_rejection_cases()==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
