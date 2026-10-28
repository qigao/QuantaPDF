/* Native-only public SDK acceptance; never imports PDFium and never
 * creates an IR re-export oracle. Source PDF is authored with QPDF, while
 * the expected RGB samples are fixed independent render semantics. */
#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <zlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr,"native public render failed at %d: %s\n",__LINE__,#expr); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(expr) CHECK((expr)==QUANTAPDF_OK)

using Obj = QPDFObjectHandle;

static std::string create_source(bool late_text)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);

    const std::string raw("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    auto image=pdf->newStream(raw);
    image.getDict().replaceKey("/Type",Obj::newName("/XObject"));
    image.getDict().replaceKey("/Subtype",Obj::newName("/Image"));
    image.getDict().replaceKey("/Width",Obj::newInteger(2));
    image.getDict().replaceKey("/Height",Obj::newInteger(2));
    image.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    image.getDict().replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
    auto objects=Obj::newDictionary(),resources=Obj::newDictionary();
    objects.replaceKey("/I",image);
    resources.replaceKey("/XObject",objects);
    page.replaceKey("/Resources",resources);
    const std::string source=
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q "
        "Q 0.875 g 24 17 5 3 re f"
        +(late_text ? std::string(" BT ET") : std::string());
    page.replaceKey("/Contents",pdf->newStream(source));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char *>(blob->getBuffer()),
                       blob->getSize());
}

/* Legitimate native zero-paint pages: Contents absent, an exact
 * empty stream, an empty Contents array, or balanced no-ink q/Q.
 * Unsupported text and even an empty /Annots key still refuse. */
static std::string create_blank_source(int mode)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);
    if(mode==1)page.replaceKey("/Contents",pdf->newStream(""));
    if(mode==2)page.replaceKey("/Contents",Obj::parse("[]"));
    if(mode==3)page.replaceKey("/Contents",
        pdf->newStream("q 1 0 0 1 10 0 cm Q"));
    if(mode==4)page.replaceKey("/Contents",
        pdf->newStream("BT ET"));
    if(mode==5)page.replaceKey("/Annots",Obj::parse("[]"));
    if(mode==6)page.replaceKey("/MediaBox",
        Obj::parse("[0 0 1000 1000]"));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(blob->getBuffer()),blob->getSize());
}


/* Native-only opaque Path Fill originals with no /Resources or Image:
 * 1..4 source-ordered PATHs, grayscale, exact overlap and atomic
 * fifth PATH / late Text / Stroke / Clip refusals. */
static std::string create_path_only_source(int mode)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);
    std::string source;
    if(mode==0)
        source="1 0 0 rg 4 4 8 6 re f";
    if(mode==1)
        source="q 1 0 0 rg 4 4 8 6 re f Q "
               "0 0 1 rg 12 10 7 5 re f";
    if(mode==2)
        source="0.5 g 4 4 8 6 re f";
    if(mode==3)
        source="1 0 0 rg 4 4 8 6 re f BT ET";
    if(mode==4)
        source="1 0 0 rg 4 4 8 6 re f 4 4 8 6 re S";
    if(mode==5)
        source="q 4 4 8 6 re W n 1 0 0 rg 4 4 8 6 re f Q";
    if(mode>=6 && mode<=9) {
        /* Three source-ordered opaque fills; the fourth covers part of
         * green without changing the blue or red-only sample regions. */
        source="1 0 0 rg 4 4 8 6 re f "
               "0 0 1 rg 12 10 7 5 re f "
               "0 1 0 rg 8 4 8 8 re f";
        if(mode>=7)source+=" 1 1 0 rg 10 7 6 5 re f";
        if(mode==8)source+=" 0 0 0 rg 1 1 2 2 re f"; /* fifth */
        if(mode==9)source+=" BT ET"; /* unsupported late Text */
    }
    page.replaceKey("/Contents",pdf->newStream(source));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
}


/* One/two original Image-only Do draws, no Clip or PATH. QPDF authors
 * the original and only native C11 goldens are checked. For mode 3,
 * the TIFF-2 Gray stream bytes are compressed independently with zlib
 * and preserved exactly by the QPDF writer. */
static std::string create_image_only_source(int mode)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);
    const bool gray=mode==2 || mode==3 || mode==20;
    const std::string rgb("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    const std::string samples=gray ?
        std::string("\0\x55\xaa\xff",4) : rgb;
    std::string encoded=samples;
    if(mode==3 || mode==20) {
        const std::string tiff("\0\x55\xaa\x55",4);
        uLongf cap=compressBound(static_cast<uLong>(tiff.size()));
        encoded.assign(static_cast<size_t>(cap),'\0');
        const int result=compress2(
            reinterpret_cast<Bytef *>(encoded.data()),&cap,
            reinterpret_cast<const Bytef *>(tiff.data()),
            static_cast<uLong>(tiff.size()),Z_BEST_COMPRESSION);
        if(result!=Z_OK)
            throw std::runtime_error("test-only TIFF Flate encode");
        encoded.resize(static_cast<size_t>(cap));
    }
    auto image=pdf->newStream(encoded);
    image.getDict().replaceKey("/Type",Obj::newName("/XObject"));
    image.getDict().replaceKey("/Subtype",Obj::newName("/Image"));
    image.getDict().replaceKey("/Width",Obj::newInteger(2));
    image.getDict().replaceKey("/Height",Obj::newInteger(2));
    image.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    image.getDict().replaceKey("/ColorSpace",
        Obj::newName(mode==7?"/DeviceCMYK":
                     (gray?"/DeviceGray":"/DeviceRGB")));
    if(gray)
        image.getDict().replaceKey("/Decode",Obj::parse("[1 0]"));
    if(mode==3 || mode==20) {
        image.getDict().replaceKey("/Filter",Obj::newName("/FlateDecode"));
        image.getDict().replaceKey("/DecodeParms",Obj::parse(
            "<< /Predictor 2 /Colors 1 /Columns 2 /BitsPerComponent 8 >>"));
        image.setFilterOnWrite(false);
    }
    if(mode==8)
        image.getDict().replaceKey("/ImageMask",Obj::newBool(true));
    auto objects=Obj::newDictionary(),resources=Obj::newDictionary();
    objects.replaceKey("/I",image);
    resources.replaceKey("/XObject",objects);
    page.replaceKey("/Resources",resources);
    std::string content="q 16 0 0 12 8 3 cm /I Do Q";
    if(mode==1)
        content+=" q 8 0 0 6 24 0 cm /I Do Q";
    if(mode==4)content+=" BT ET";
    if(mode==5)content=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q";
    if(mode==9)content=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
        "q 8 0 0 6 12 9 cm /I Do Q Q";
    if(mode==10)content=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q BT ET";
    if(mode==11)content=
        "q 12 6 8 9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q";
    if(mode==12)content=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
        "q 8 0 0 6 12 9 cm /I Do Q "
        "q 4 0 0 4 12 9 cm /I Do Q Q"; /* third Image */
    if(mode==13)content=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q "
        "q 8 0 0 6 24 0 cm /I Do Q"; /* second outside Clip */
    if(mode==14)content=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
        "q 8 0 0 6 12 9 cm /J Do Q Q"; /* missing resource */
    if(mode==15)content=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q Q";
    if(mode==16)content=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
        "q 12 6 8 9 re W* n /I Do Q Q"; /* second Clip */
    if(mode==6 || (mode>=17 && mode<=21))
        content+=" q 8 0 0 6 24 0 cm /I Do Q"
                 " q 8 0 0 6 0 0 cm /I Do Q";
    if(mode>=17 && mode<=20)
        content+=" q 8 0 0 6 24 12 cm /I Do Q"; /* fourth */
    if(mode==18)
        content+=" q 4 0 0 4 12 19 cm /I Do Q"; /* fifth: reject */
    if(mode==19)
        content+=" BT ET"; /* late unsupported: discard all four */
    if(mode==21)
        content+=" q 8 0 0 6 16 9 cm /I Do Q"; /* fourth overlaps */
    if(mode==22)
        content="q 12 0 0 9 3 3 cm /I Do Q"; /* 96-DPI exact bounds */
    if(mode==23 || mode==24 || mode==25) {
        content="q 9 0 0 9 5 2 cm /I Do Q";
        if(mode==24)
            content="q 5 2 9 9 re W n "
                    "q 9 0 0 9 5 2 cm /I Do Q Q";
        if(mode==25)content+=" BT ET"; /* late unsupported */
    }
    page.replaceKey("/Contents",pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(blob->getBuffer()),blob->getSize());
}


/* Native-only public original with one IMAGE and one opaque PATH fill in
 * either authorial order. No reference renderer or alternate raster is
 * involved. Two pixels distinguish IMAGE-over-PATH and PATH-over-IMAGE.
 * A Gray+TIFF case reuses exactly the same qualified V10 Image decoder. */
static std::string create_unclipped_mixed_source(int mode)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);
    const bool gray=mode==8;
    const std::string rgb("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    const std::string samples=gray ?
        std::string("\0\x55\xaa\xff",4) : rgb;
    std::string encoded=samples;
    if(gray) {
        const std::string tiff("\0\x55\xaa\x55",4);
        uLongf cap=compressBound(static_cast<uLong>(tiff.size()));
        encoded.assign(static_cast<size_t>(cap),'\0');
        const auto result=compress2(
            reinterpret_cast<Bytef *>(encoded.data()),&cap,
            reinterpret_cast<const Bytef *>(tiff.data()),
            static_cast<uLong>(tiff.size()),Z_BEST_COMPRESSION);
        if(result!=Z_OK)
            throw std::runtime_error("native mixed Gray TIFF Flate encode");
        encoded.resize(static_cast<size_t>(cap));
    }
    auto image=pdf->newStream(encoded);
    /* QPDFObjectHandle is a shared object handle returned by value;
     * do not bind a non-const reference to a temporary getDict(). */
    auto dict=image.getDict();
    dict.replaceKey("/Type",Obj::newName("/XObject"));
    dict.replaceKey("/Subtype",Obj::newName("/Image"));
    dict.replaceKey("/Width",Obj::newInteger(2));
    dict.replaceKey("/Height",Obj::newInteger(2));
    dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
    dict.replaceKey("/ColorSpace",
        Obj::newName(mode==7?"/DeviceCMYK":
                     (gray?"/DeviceGray":"/DeviceRGB")));
    if(gray) {
        dict.replaceKey("/Decode",Obj::parse("[1 0]"));
        dict.replaceKey("/Filter",Obj::newName("/FlateDecode"));
        dict.replaceKey("/DecodeParms",Obj::parse(
            "<< /Predictor 2 /Colors 1 /Columns 2 /BitsPerComponent 8 >>"));
        image.setFilterOnWrite(false);
    }
    auto xobjects=Obj::newDictionary(),resources=Obj::newDictionary();
    xobjects.replaceKey(mode==6?"/J":"/I",image);
    resources.replaceKey("/XObject",xobjects);
    page.replaceKey("/Resources",resources);
    const std::string draw="q 16 0 0 12 8 3 cm /I Do Q";
    const std::string fill="0 1 0 rg 4 4 8 6 re f";
    const bool path_first=mode==1 || mode==6;
    std::string source=path_first ?
        fill+" "+draw : draw+" "+fill;
    if(mode==2) source+=" BT ET";
    if(mode==3) source="q 4 4 8 6 re W n "+source+" Q";
    if(mode==4) source+=" 1 0 0 rg 26 2 3 4 re f";
    if(mode==5) source+=" "+draw;
    page.replaceKey("/Contents",pdf->newStream(source));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto buffer=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(buffer->getBuffer()),buffer->getSize());
}

/* One original 8-bit RGB Do and one opaque blue PATH Fill under the
 * SAME authorial W/W* Clip, in both PDF source orders. No PDFium oracle.
 * Negative originals poison the full Native Bitmap/thumbnail. */
static std::string create_clipped_mixed_source(int mode)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",root);
    const std::string raw("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    auto image=pdf->newStream(raw);
    auto dict=image.getDict();
    dict.replaceKey("/Type",Obj::newName("/XObject"));
    dict.replaceKey("/Subtype",Obj::newName("/Image"));
    dict.replaceKey("/Width",Obj::newInteger(2));
    dict.replaceKey("/Height",Obj::newInteger(2));
    dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
    dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
    auto images=Obj::newDictionary(),resources=Obj::newDictionary();
    images.replaceKey("/I",image);
    resources.replaceKey("/XObject",images);
    page.replaceKey("/Resources",resources);
    const std::string outer=std::string("q 12 6 8 9 re ")+
        (mode==1?"W* n ":"W n ");
    const std::string paint_image="q 8 0 0 12 8 3 cm /I Do Q ";
    const std::string paint_path="0 0 1 rg 12 6 8 9 re f ";
    std::string content=outer+
        (mode==1?paint_path+paint_image:paint_image+paint_path)+"Q";
    if(mode==2)
        content=outer+paint_image+paint_path+
            "1 0 0 rg 12 6 8 9 re f Q"; /* third all-clipped */
    if(mode==3)
        content=outer+paint_image+
            "q 12 6 8 9 re W* n "+paint_path+"Q Q"; /* second W* */
    if(mode==4)
        content+=" Q"; /* graphics stack underflow = FORMAT */
    if(mode==5)
        content=outer+"q 8 0 0 12 8 3 cm /J Do Q "+
            paint_path+"Q"; /* missing Image XObject = FORMAT */
    if(mode==6)
        content+=" BT ET"; /* late unsupported Text */
    page.replaceKey("/Contents",pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(blob->getBuffer()),blob->getSize());
}

static bool write_pdf(const std::string& filename,const std::string& content)
{
    FILE *f=nullptr;
#ifdef _WIN32
    if(fopen_s(&f,filename.c_str(),"wb")!=0)return false;
#else
    f=std::fopen(filename.c_str(),"wb");
#endif
    if(!f)return false;
    const bool good=std::fwrite(content.data(),1u,content.size(),f)==content.size();
    return std::fclose(f)==0 && good;
}

static int probe_rgb(const unsigned char *data,int stride,int x,int y,
                     unsigned red,unsigned green,unsigned blue)
{
    const auto *px=data+(size_t)y*(size_t)stride+(size_t)x*3u;
    return px[0]==red && px[1]==green && px[2]==blue;
}

/* Native original-PDF goldens use the true 96-DPI 4/3 device
 * sampling grid, including fractional image/Clip intersections and
 * already-qualified integer-image and 144-DPI profiles.
 * No PDFium oracle or 72-DPI bitmap upscaling. */
static int high_dpi_cases(const std::string& filename)
{
    struct entry {int kind,mode,dpi;bool success;};
    const entry cases[]={
        {0,0,96,true},{0,0,144,true},
        {1,0,96,true},{1,0,144,true},
        {2,22,96,true},{2,0,96,true},
        {2,23,96,true},{2,24,96,true},
        {2,5,96,true},{2,9,96,true},
        {3,0,96,true},{3,1,96,true},
        {2,17,96,true},
        {2,0,144,true},{2,5,144,true},
        {3,0,144,true},{2,17,144,true},
        {2,25,96,false},{2,7,96,false},
        {2,4,144,false},{0,6,96,false},{0,6,144,false}
    };
    std::vector<unsigned char> full_fractional_image;
    for(const auto& c:cases) {
        quantapdf_bitmap *retained_rotation=nullptr;
        const std::string original=c.kind==0?
            create_blank_source(c.mode):
            c.kind==1?create_path_only_source(c.mode):
            c.kind==2?create_image_only_source(c.mode):
                      create_unclipped_mixed_source(c.mode);
        CHECK(write_pdf(filename,original));
        quantapdf_context *ctx=nullptr;
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *im=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_context_create(&ctx));
        OK(quantapdf_open_with_context(ctx,filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);
        opts.dpi=(float)c.dpi;
        const auto result=quantapdf_render_page_with_options(page,&opts,&im);
        if(!c.success) {
            CHECK(result==QUANTAPDF_ERROR_UNSUPPORTED && im==nullptr);
            if(c.kind==2 && c.mode==25) {
                quantapdf_bitmap *small=reinterpret_cast<quantapdf_bitmap*>(
                    uintptr_t(1));
                CHECK(quantapdf_render_thumbnail(page,16,12,&small)==
                      QUANTAPDF_ERROR_UNSUPPORTED && small==nullptr);
            }
        } else {
            CHECK(result==QUANTAPDF_OK && im!=nullptr);
            int w=0,h=0,s=0,nc=0;
            const unsigned char *px=nullptr,*repeated=nullptr;
            size_t count=0u,repeated_size=0u;
            OK(quantapdf_bitmap_dimensions(im,&w,&h,&s,&nc));
            OK(quantapdf_bitmap_data(im,&px,&count));
            CHECK(w==(c.dpi==96?43:64) && h==(c.dpi==96?32:48) &&
                  s==w*3 && nc==3 && px!=nullptr &&
                  count==(size_t)w*(size_t)h*3u);
            if(c.kind==0)
                for(size_t i=0u;i<count;++i)CHECK(px[i]==255u);
            if(c.kind==1 && c.dpi==96) {
                CHECK(probe_rgb(px,s,4,20,255,255,255));
                CHECK(probe_rgb(px,s,5,20,255,64,64));
                CHECK(probe_rgb(px,s,6,20,255,0,0));
                CHECK(probe_rgb(px,s,16,20,255,255,255));
            }
            if(c.kind==1 && c.dpi==144) {
                CHECK(probe_rgb(px,s,8,30,255,0,0));
                CHECK(probe_rgb(px,s,24,30,255,255,255));
            }
            if(c.kind==2 && c.mode==22) {
                CHECK(probe_rgb(px,s,6,17,255,0,0));
                CHECK(probe_rgb(px,s,17,17,0,255,0));
                CHECK(probe_rgb(px,s,6,25,0,0,255));
                CHECK(probe_rgb(px,s,17,25,255,255,0));
            }
            if(c.kind==2 && c.mode==0 && c.dpi==96) {
                CHECK(probe_rgb(px,s,10,15,255,191,191));
                CHECK(probe_rgb(px,s,11,15,255,0,0));
                CHECK(probe_rgb(px,s,24,15,0,255,0));
                CHECK(probe_rgb(px,s,10,22,191,191,255));
            }
            if(c.kind==2 && (c.mode==23 || c.mode==24)) {
                /* Both fractional image edges AND the W Clip use
                 * one 16-sample intersection (3/16 at x=6,y=17). */
                CHECK(probe_rgb(px,s,6,17,255,207,207));
                CHECK(probe_rgb(px,s,7,18,255,0,0));
                CHECK(probe_rgb(px,s,6,16,255,255,255));
                if(c.mode==23)full_fractional_image.assign(px,px+count);
                else CHECK(full_fractional_image.size()==count &&
                           std::memcmp(px,full_fractional_image.data(),
                                       count)==0);
            }
            if(c.kind==2 && c.mode==0 && c.dpi==144) {
                CHECK(probe_rgb(px,s,18,20,255,0,0));
                CHECK(probe_rgb(px,s,44,20,0,255,0));
                CHECK(probe_rgb(px,s,18,40,0,0,255));
                CHECK(probe_rgb(px,s,44,40,255,255,0));
            }
            if(c.kind==2 && c.mode==5) {
                if(c.dpi==144) {
                    CHECK(probe_rgb(px,s,26,22,255,0,0));
                    CHECK(probe_rgb(px,s,36,22,0,255,0));
                    CHECK(probe_rgb(px,s,26,32,0,0,255));
                    CHECK(probe_rgb(px,s,36,32,255,255,0));
                    CHECK(probe_rgb(px,s,20,24,255,255,255));
                } else {
                    CHECK(probe_rgb(px,s,18,16,255,0,0));
                    CHECK(probe_rgb(px,s,24,16,0,255,0));
                    CHECK(probe_rgb(px,s,18,22,0,0,255));
                    CHECK(probe_rgb(px,s,24,22,255,255,0));
                    CHECK(probe_rgb(px,s,15,16,255,255,255));
                }
            }
            if(c.kind==2 && c.mode==9 && c.dpi==96) {
                CHECK(probe_rgb(px,s,18,17,0,0,255));
                CHECK(probe_rgb(px,s,24,17,255,255,0));
            }
            if(c.kind==3) {
                if(c.dpi==144) {
                    CHECK(probe_rgb(px,s,20,32,0,255,0));
                    CHECK(probe_rgb(px,s,36,22,0,255,0));
                } else {
                    CHECK(probe_rgb(px,s,12,19,
                        c.mode==0?0u:255u,c.mode==0?255u:0u,0u));
                }
            }
            if(c.kind==2 && c.mode==17) {
                if(c.dpi==144) {
                    CHECK(probe_rgb(px,s,50,16,255,0,0));
                    CHECK(probe_rgb(px,s,60,16,0,255,0));
                    CHECK(probe_rgb(px,s,50,40,255,0,0));
                } else {
                    CHECK(probe_rgb(px,s,35,10,255,0,0));
                    CHECK(probe_rgb(px,s,40,10,0,255,0));
                }
            }
            quantapdf_bitmap *again=nullptr;
            OK(quantapdf_render_page_with_options(page,&opts,&again));
            OK(quantapdf_bitmap_data(again,&repeated,&repeated_size));
            CHECK(repeated!=nullptr && repeated_size==count &&
                  std::memcmp(repeated,px,count)==0);
            quantapdf_drop_bitmap(again);
            /* Full raster is produced at requested 96/144 DPI FIRST;
             * 90/180/270 are then exact lossless permutations of that
             * target grid. Neither source coordinates nor 4x4 Image/Clip
             * sampling may be reinterpreted as 72-DPI pixels. */
            const int rotations[3]={90,180,270};
            for(int angle:rotations) {
                opts.rotation_degrees=(float)angle;
                quantapdf_bitmap *rotated=nullptr;
                OK(quantapdf_render_page_with_options(page,&opts,&rotated));
                CHECK(rotated!=nullptr);
                int rw=0,rh=0,rs=0,rc=0;
                const unsigned char *rotated_bytes=nullptr;
                size_t rn=0u;
                OK(quantapdf_bitmap_dimensions(
                    rotated,&rw,&rh,&rs,&rc));
                OK(quantapdf_bitmap_data(rotated,&rotated_bytes,&rn));
                CHECK(rw==(angle==180?w:h) &&
                      rh==(angle==180?h:w) &&
                      rs==rw*3 && rc==3 && rn==count &&
                      rotated_bytes!=nullptr);
                for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                    const int dx=angle==90?h-1-y:
                                 angle==180?w-1-x:y;
                    const int dy=angle==90?x:
                                 angle==180?h-1-y:w-1-x;
                    const unsigned char *src=px+
                        (size_t)y*(size_t)s+(size_t)x*3u;
                    const unsigned char *dst=rotated_bytes+
                        (size_t)dy*(size_t)rs+(size_t)dx*3u;
                    CHECK(std::memcmp(src,dst,3u)==0);
                }
                quantapdf_bitmap *repeat_rotated=nullptr;
                OK(quantapdf_render_page_with_options(
                    page,&opts,&repeat_rotated));
                const unsigned char *repeat_rotated_bytes=nullptr;
                size_t repeat_rotated_size=0u;
                OK(quantapdf_bitmap_data(
                    repeat_rotated,&repeat_rotated_bytes,
                    &repeat_rotated_size));
                CHECK(repeat_rotated_bytes!=nullptr &&
                      repeat_rotated_size==rn &&
                      std::memcmp(rotated_bytes,repeat_rotated_bytes,rn)==0);
                quantapdf_drop_bitmap(repeat_rotated);
                if(angle==270)retained_rotation=rotated;
                else quantapdf_drop_bitmap(rotated);
            }
            opts.rotation_degrees=45.0f;
            quantapdf_bitmap *invalid=reinterpret_cast<quantapdf_bitmap*>(
                uintptr_t(1));
            CHECK(quantapdf_render_page_with_options(page,&opts,&invalid)==
                  QUANTAPDF_ERROR_UNSUPPORTED && invalid==nullptr);
            opts.rotation_degrees=360.0f;
            invalid=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_page_with_options(page,&opts,&invalid)==
                  QUANTAPDF_ERROR_UNSUPPORTED && invalid==nullptr);
            opts.rotation_degrees=0.0f;
            opts.alpha=1;
            invalid=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_page_with_options(page,&opts,&invalid)==
                  QUANTAPDF_ERROR_UNSUPPORTED && invalid==nullptr);
            opts.alpha=0;
            opts.clip_enabled=1;
            opts.clip={0.0f,0.0f,4.0f,4.0f};
            invalid=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_page_with_options(page,&opts,&invalid)==
                  QUANTAPDF_ERROR_UNSUPPORTED && invalid==nullptr);
            opts.clip_enabled=0;
        }
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        quantapdf_drop_context(ctx);
        if(im!=nullptr) {
            const unsigned char *owner=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_data(im,&owner,&bytes));
            CHECK(owner!=nullptr && bytes!=0u &&
                  probe_rgb(owner,(c.dpi==96?43:64)*3,0,0,255,255,255));
            int rw=0,rh=0,rs=0,rc=0;
            const unsigned char *rotated_owner=nullptr;
            size_t rotated_bytes=0u;
            OK(quantapdf_bitmap_dimensions(
                retained_rotation,&rw,&rh,&rs,&rc));
            OK(quantapdf_bitmap_data(
                retained_rotation,&rotated_owner,&rotated_bytes));
            CHECK(rw==(c.dpi==96?32:48) &&
                  rh==(c.dpi==96?43:64) &&
                  rs==rw*3 && rc==3 && rotated_bytes==bytes &&
                  rotated_owner!=nullptr);
            CHECK(probe_rgb(rotated_owner,rs,0,rh-1,
                            owner[0],owner[1],owner[2]));
        }
        quantapdf_drop_bitmap(retained_rotation);
        quantapdf_drop_bitmap(im);
    }
    return EXIT_SUCCESS;
}

/* A second original-PDF source fixture writer ONLY changes page tree
 * /Rotate. QPDF authors the actual page and resource graph, not a mock IR.
 * Modes: 0 leaf; 1 inherited; 2 leaf overrides inherited 90;
 * 3 malformed name; 4 real instead of integer; 5 non-quarter;
 * 6 malformed inherited name; 7 over-depth Pages ancestry;
 * 8 explicit unsupported CropBox; 9 unsupported UserUnit.
 * Every Source refusal must leave an empty Bitmap. */
static std::string original_pdf_with_rotate(
    const std::string& original,int degrees,int mode)
{
    auto pdf=QPDF::create();
    pdf->setSuppressWarnings(true);
    pdf->setAttemptRecovery(false);
    pdf->processMemoryFile("native original Rotate",
        original.data(),original.size(),"");
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pages.getKey("/Kids").getArrayItem(0);
    if(mode==1)
        pages.replaceKey("/Rotate",Obj::newInteger(degrees));
    else if(mode==2) {
        pages.replaceKey("/Rotate",Obj::newInteger(90));
        page.replaceKey("/Rotate",Obj::newInteger(degrees));
    } else if(mode==3)
        page.replaceKey("/Rotate",Obj::newName("/Ninety"));
    else if(mode==4)
        page.replaceKey("/Rotate",Obj::parse("90.5"));
    else if(mode==5)
        page.replaceKey("/Rotate",Obj::newInteger(45));
    else if(mode==6)
        pages.replaceKey("/Rotate",Obj::newName("/Ninety"));
    else if(mode==7) {
        /* QPDF's geometry reader can navigate this valid page tree,
         * but Native Source V10 independently caps its /Parent lease
         * at max_parent_steps=8, including the page and root. */
        Obj parent=pages;
        for(int depth=0;depth<9;++depth) {
            auto node=pdf->makeIndirectObject(
                Obj::parse("<< /Type /Pages /Count 1 /Kids [] >>"));
            node.replaceKey("/Parent",parent);
            parent.replaceKey("/Kids",Obj::newArray({node}));
            parent=node;
        }
        parent.replaceKey("/Kids",Obj::newArray({page}));
        page.replaceKey("/Parent",parent);
        page.replaceKey("/Rotate",Obj::newInteger(degrees));
    } else if(mode==8) {
        page.replaceKey("/Rotate",Obj::newInteger(degrees));
        page.replaceKey("/CropBox",Obj::parse("[0 0 31 24]"));
    } else if(mode==9) {
        page.replaceKey("/Rotate",Obj::newInteger(degrees));
        page.replaceKey("/UserUnit",Obj::newInteger(2));
    } else
        page.replaceKey("/Rotate",Obj::newInteger(degrees));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto buffer=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char *>(buffer->getBuffer()),buffer->getSize());
}

static int rotated_xy(int x,int y,int w,int h,int angle,int *dx,int *dy)
{
    switch(angle) {
    case 0:*dx=x;*dy=y;return 1;
    case 90:*dx=h-1-y;*dy=x;return 1;
    case 180:*dx=w-1-x;*dy=h-1-y;return 1;
    case 270:*dx=y;*dy=w-1-x;return 1;
    default:return 0;
    }
}

static int original_source_rotation_cases(const std::string& filename)
{
    const std::string originals[]={
        create_source(false),create_blank_source(0),
        create_path_only_source(0),create_image_only_source(0),
        create_image_only_source(17),create_clipped_mixed_source(0)
    };
    const int dpis[]={72,96,144};
    const int source_angles[]={90,180,270};
    const int display_angles[]={0,90,180,270};

    for(size_t kind=0u;kind<sizeof(originals)/sizeof(originals[0]);++kind)
    for(int dpi:dpis) {
        CHECK(write_pdf(filename,originals[kind]));
        quantapdf_context *base_ctx=nullptr;
        quantapdf_document *base_doc=nullptr;
        quantapdf_page *base_page=nullptr;
        quantapdf_bitmap *base_bitmap=nullptr;
        OK(quantapdf_context_create(&base_ctx));
        OK(quantapdf_open_with_context(
            base_ctx,filename.c_str(),nullptr,&base_doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(base_doc,0,&base_page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);opts.dpi=(float)dpi;
        OK(quantapdf_render_page_with_options(
            base_page,&opts,&base_bitmap));
        int bw=0,bh=0,bs=0,bc=0;
        const unsigned char *base_pixels=nullptr;
        size_t base_size=0u;
        OK(quantapdf_bitmap_dimensions(
            base_bitmap,&bw,&bh,&bs,&bc));
        OK(quantapdf_bitmap_data(base_bitmap,&base_pixels,&base_size));
        CHECK(bw==(dpi==72?32:dpi==96?43:64) &&
              bh==(dpi==72?24:dpi==96?32:48) &&
              bs==bw*3 && bc==3 && base_size==(size_t)bs*(size_t)bh);
        const std::vector<unsigned char> baseline(
            base_pixels,base_pixels+base_size);
        quantapdf_drop_bitmap(base_bitmap);
        quantapdf_drop_page(base_page);
        quantapdf_close(base_doc);
        quantapdf_drop_context(base_ctx);

        for(int angle:source_angles) {
            std::vector<unsigned char> direct_pixels;
            for(int source_mode=0;source_mode<3;++source_mode) {
                if(source_mode==2 && angle!=180)continue;
                const std::string rotated_pdf=original_pdf_with_rotate(
                    originals[kind],angle,source_mode);
                CHECK(write_pdf(filename,rotated_pdf));
                quantapdf_context *ctx=nullptr;
                quantapdf_document *doc=nullptr;
                quantapdf_page *page=nullptr;
                quantapdf_bitmap *bitmap=nullptr;
                OK(quantapdf_context_create(&ctx));
                OK(quantapdf_open_with_context(
                    ctx,filename.c_str(),nullptr,&doc));
                CHECK(std::remove(filename.c_str())==0);
                OK(quantapdf_load_page(doc,0,&page));
                quantapdf_rect bounds{};
                OK(quantapdf_page_bounds(page,&bounds));
                const int source_width=(angle==180?32:24);
                const int source_height=(angle==180?24:32);
                CHECK(bounds.x0==0.0f && bounds.y0==0.0f &&
                      bounds.x1==(float)source_width &&
                      bounds.y1==(float)source_height);
                OK(quantapdf_render_page_with_options(
                    page,&opts,&bitmap));
                int sw=0,sh=0,ss=0,sc=0;
                const unsigned char *pixels=nullptr;
                size_t size=0u;
                OK(quantapdf_bitmap_dimensions(
                    bitmap,&sw,&sh,&ss,&sc));
                OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
                const int expected_w=angle==180?bw:bh;
                const int expected_h=angle==180?bh:bw;
                CHECK(sw==expected_w && sh==expected_h &&
                      ss==sw*3 && sc==3 &&
                      size==(size_t)ss*(size_t)sh && pixels!=nullptr);
                /* Exact per-pixel equivalence is warranted for the 72
                 * and 144 DPI device lattices, where the page corners
                 * and every authored source point map to integral
                 * destination pixels. 96 DPI's 4/3 page edge has a
                 * distinct ceil-lattice; below we check independent
                 * deep-inside source RGB goldens and exact repeat. */
                if(dpi!=96)
                    for(int y=0;y<bh;++y)
                        for(int x=0;x<bw;++x) {
                            int rx=0,ry=0;
                            CHECK(rotated_xy(
                                x,y,bw,bh,angle,&rx,&ry));
                            const unsigned char *from=baseline.data()+
                                (size_t)y*(size_t)bs+(size_t)x*3u;
                            const unsigned char *to=pixels+
                                (size_t)ry*(size_t)ss+(size_t)rx*3u;
                            CHECK(std::memcmp(from,to,3u)==0);
                        }

                if(kind==0u) {
                    const int red_x=dpi==72?13:dpi==96?18:26;
                    const int red_y=dpi==72?11:dpi==96?16:22;
                    const int gray_x=dpi==72?26:dpi==96?35:52;
                    const int gray_y=dpi==72?5:dpi==96?7:10;
                    int rx=0,ry=0,gx=0,gy=0;
                    CHECK(rotated_xy(
                        red_x,red_y,bw,bh,angle,&rx,&ry));
                    CHECK(rotated_xy(
                        gray_x,gray_y,bw,bh,angle,&gx,&gy));
                    CHECK(probe_rgb(pixels,ss,rx,ry,255u,0u,0u));
                    CHECK(probe_rgb(pixels,ss,gx,gy,223u,223u,223u));
                }
                const std::vector<unsigned char> stable(pixels,pixels+size);
                if(source_mode==0)direct_pixels=stable;
                else CHECK(direct_pixels==stable);
                if(kind==0u) {
                    /* Public display turn is a separate post-raster
                     * permutation of the fully source-rotated page;
                     * validate every pixel at every DPI, including 4/3. */
                    for(int display:display_angles) {
                        opts.rotation_degrees=(float)display;
                        quantapdf_bitmap *turned=nullptr;
                        OK(quantapdf_render_page_with_options(
                            page,&opts,&turned));
                        int tw=0,th=0,ts=0,tc=0;
                        const unsigned char *tp=nullptr;
                        size_t tn=0u;
                        OK(quantapdf_bitmap_dimensions(
                            turned,&tw,&th,&ts,&tc));
                        OK(quantapdf_bitmap_data(turned,&tp,&tn));
                        CHECK(tw==(display==90||display==270?sh:sw) &&
                              th==(display==90||display==270?sw:sh) &&
                              ts==tw*3 && tc==3 && tn==size && tp!=nullptr);
                        for(int y=0;y<sh;++y)
                            for(int x=0;x<sw;++x) {
                                int dx=0,dy=0;
                                CHECK(rotated_xy(x,y,sw,sh,display,&dx,&dy));
                                const unsigned char *from=stable.data()+
                                    (size_t)y*(size_t)ss+(size_t)x*3u;
                                const unsigned char *to=tp+
                                    (size_t)dy*(size_t)ts+(size_t)dx*3u;
                                CHECK(std::memcmp(from,to,3u)==0);
                            }
                        quantapdf_drop_bitmap(turned);
                    }
                    opts.rotation_degrees=0.0f;
                }
                quantapdf_drop_page(page);
                quantapdf_close(doc);
                quantapdf_drop_context(ctx);
                const unsigned char *retained=nullptr;
                size_t retained_size=0u;
                OK(quantapdf_bitmap_data(
                    bitmap,&retained,&retained_size));
                CHECK(retained!=nullptr && retained_size==size &&
                      std::memcmp(retained,stable.data(),size)==0);
                quantapdf_drop_bitmap(bitmap);
            }
        }
    }

    /* Strict inherited /Rotate acceptance; no unqualified output from
     * bad numeric types, non-quarter values or unsupported late Text. */
    for(int mode=3;mode<=9;++mode) {
        const auto bad=original_pdf_with_rotate(
            create_source(false),90,mode);
        CHECK(write_pdf(filename,bad));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_bitmap *failed=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1));
        const auto status=quantapdf_render_page(page,&failed);
        if(mode==8) {
            /* An originally cropped [0 0 31 24] Source with
             * /Rotate90 is now a QUALIFIED native 24x31 viewport,
             * not a discarded or scaled 32x24 PDFium image. */
            CHECK(status==QUANTAPDF_OK && failed!=nullptr);
            int w=0,h=0,stride=0,channels=0;
            const unsigned char *rgb=nullptr;
            size_t size=0u;
            OK(quantapdf_bitmap_dimensions(
                failed,&w,&h,&stride,&channels));
            OK(quantapdf_bitmap_data(failed,&rgb,&size));
            CHECK(w==24 && h==31 && stride==72 && channels==3 &&
                  size==24u*31u*3u && rgb!=nullptr);
            CHECK(probe_rgb(rgb,stride,12,13,255u,0u,0u));
            CHECK(probe_rgb(rgb,stride,18,26,223u,223u,223u));
            quantapdf_drop_bitmap(failed);
        } else {
            const auto expected=mode<=6 ?
                QUANTAPDF_ERROR_FORMAT:QUANTAPDF_ERROR_UNSUPPORTED;
            if(status!=expected || failed!=nullptr)
                std::fprintf(stderr,
                    "original Rotate rejected mode=%d actual=%d expected=%d output=%p\n",
                    mode,(int)status,(int)expected,(void*)failed);
            CHECK(status==expected && failed==nullptr);
        }
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    CHECK(write_pdf(filename,original_pdf_with_rotate(
        create_source(true),90,1)));
    {
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *out=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1));
        OK(quantapdf_open(filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        CHECK(quantapdf_render_page(page,&out)==
              QUANTAPDF_ERROR_UNSUPPORTED && out==nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    return EXIT_SUCCESS;
}


/* Seven actual PDF page-tree leaves; no IR re-export or PDFium oracle.
 * 0 blank, 1 red PATH, 2 distinct RAW image, 3 image/W Clip/gray PATH,
 * 4 late unsupported Text, 5 direct original /Rotate 90 PATH,
 * 6 nested inherited /MediaBox + /Rotate 270 + /Resources Image.
 * A duplicate/unreachable page must never be substituted for the
 * loaded Page lease's true original index. */
static std::string selected_page_original_pdf(int bad_mode=0)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    const std::string raw("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    auto image=pdf->newStream(raw);
    auto dict=image.getDict();
    dict.replaceKey("/Type",Obj::newName("/XObject"));
    dict.replaceKey("/Subtype",Obj::newName("/Image"));
    dict.replaceKey("/Width",Obj::newInteger(2));
    dict.replaceKey("/Height",Obj::newInteger(2));
    dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
    dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
    auto xobjects=Obj::newDictionary();
    xobjects.replaceKey("/I",image);
    auto resources=Obj::newDictionary();
    resources.replaceKey("/XObject",xobjects);

    auto make_page=[&](const std::string& contents) {
        auto p=pdf->makeIndirectObject(
            Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
        p.replaceKey("/Parent",root);
        if(!contents.empty())
            p.replaceKey("/Contents",pdf->newStream(contents));
        return p;
    };
    const std::string fill="1 0 0 rg 4 4 8 6 re f";
    const std::string paint="q 16 0 0 12 8 3 cm /I Do Q";
    const std::string scoped="q 12 6 8 9 re W n "+paint+
                             " Q 0.875 g 24 17 5 3 re f";
    auto p0=make_page("");
    auto p1=make_page(fill);
    auto p2=make_page(paint);
    auto p3=make_page(scoped);
    auto p4=make_page("BT ET");
    auto p5=make_page(fill);
    p2.replaceKey("/Resources",resources);
    p3.replaceKey("/Resources",resources);
    p5.replaceKey("/Rotate",Obj::newInteger(90));

    auto branch=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Pages /Count 1 /MediaBox [0 0 32 24]"
                   " /Rotate 270 >>"));
    branch.replaceKey("/Parent",root);
    branch.replaceKey("/Resources",resources);
    auto p6=pdf->makeIndirectObject(Obj::parse("<< /Type /Page >>"));
    p6.replaceKey("/Parent",branch);
    p6.replaceKey("/Contents",pdf->newStream(paint));
    branch.replaceKey("/Kids",Obj::newArray({p6}));
    root.replaceKey("/Kids",
        Obj::newArray({p0,p1,p2,p3,p4,p5,branch}));
    root.replaceKey("/Count",Obj::newInteger(7));

    /* Invalid trees should not become renderable even if the selected
     * leaf appears before the defect. Deliberately author malformed
     * structure without altering the selected contents. */
    if(bad_mode==1)
        root.replaceKey("/Count",Obj::newInteger(6));
    if(bad_mode==2)
        branch.replaceKey("/Kids",Obj::newArray({p2}));
    if(bad_mode==3)
        p1.replaceKey("/Parent",branch);
    if(bad_mode==4)
        branch.replaceKey("/Count",Obj::newInteger(2));
    if(bad_mode==5)
        p2.replaceKey("/Resources",Obj::newInteger(3));

    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
}

static int selected_page_v10_cases(const std::string& filename)
{
    const std::string original=selected_page_original_pdf();
    const int dpis[]={72,96,144};
    for(int dpi:dpis)for(int selected=0;selected<7;++selected) {
        CHECK(write_pdf(filename,original));
        quantapdf_context *ctx=nullptr;
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr,*invalid=nullptr;
        quantapdf_bitmap *bitmap=nullptr,*repeat=nullptr,*thumb=nullptr;
        OK(quantapdf_context_create(&ctx));
        OK(quantapdf_open_with_context(ctx,filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        int count=0;
        OK(quantapdf_page_count(doc,&count));
        CHECK(count==7);
        CHECK(quantapdf_load_page(doc,-1,&invalid)==
              QUANTAPDF_ERROR_ARGUMENT && invalid==nullptr);
        if(selected==0) {
            invalid=reinterpret_cast<quantapdf_page*>(uintptr_t(1));
            CHECK(quantapdf_load_page(doc,7,&invalid)!=QUANTAPDF_OK &&
                  invalid==nullptr);
        }
        OK(quantapdf_load_page(doc,selected,&page));
        const bool source_rotated=(selected==5 || selected==6);
        const int pdf_width=source_rotated?24:32;
        const int pdf_height=source_rotated?32:24;
        quantapdf_rect page_bounds{};
        OK(quantapdf_page_bounds(page,&page_bounds));
        CHECK(page_bounds.x0==0.0f && page_bounds.y0==0.0f &&
              page_bounds.x1==(float)pdf_width &&
              page_bounds.y1==(float)pdf_height);
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);opts.dpi=(float)dpi;
        const auto status=quantapdf_render_page_with_options(
            page,&opts,&bitmap);
        if(selected==4) {
            CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED &&
                  bitmap==nullptr);
            thumb=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_thumbnail(
                page,16,16,&thumb)==QUANTAPDF_ERROR_UNSUPPORTED &&
                  thumb==nullptr);
        } else {
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,channels=0;
            const unsigned char *pixels=nullptr,*again=nullptr;
            size_t size=0u,again_size=0u;
            OK(quantapdf_bitmap_dimensions(
                bitmap,&w,&h,&stride,&channels));
            OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
            const int expected_w=source_rotated?
                (dpi==72?24:dpi==96?32:48):
                (dpi==72?32:dpi==96?43:64);
            const int expected_h=source_rotated?
                (dpi==72?32:dpi==96?43:64):
                (dpi==72?24:dpi==96?32:48);
            CHECK(w==expected_w && h==expected_h &&
                  stride==w*3 && channels==3 &&
                  size==(size_t)stride*(size_t)h &&
                  pixels!=nullptr);
            CHECK(probe_rgb(pixels,stride,0,0,255u,255u,255u));
            if(selected==0)
                for(size_t i=0u;i<size;++i)CHECK(pixels[i]==255u);
            if(selected==1) {
                const int x=dpi==72?6:dpi==96?8:12;
                const int y=dpi==72?16:dpi==96?21:32;
                CHECK(probe_rgb(pixels,stride,x,y,255u,0u,0u));
                CHECK(probe_rgb(pixels,stride,w-2,2,255u,255u,255u));
            }
            if(selected==2 || selected==3) {
                const int x=dpi==72?13:dpi==96?18:26;
                const int y=dpi==72?11:dpi==96?16:22;
                CHECK(probe_rgb(pixels,stride,x,y,255u,0u,0u));
                if(selected==2) {
                    CHECK(probe_rgb(pixels,stride,
                        dpi==72?20:dpi==96?27:40,y,0u,255u,0u));
                } else {
                    CHECK(probe_rgb(pixels,stride,
                        dpi==72?26:dpi==96?35:52,
                        dpi==72?5:dpi==96?7:10,
                        223u,223u,223u));
                }
            }
            if(selected==5) {
                const int x=dpi==72?7:dpi==96?10:15;
                const int y=dpi==72?6:dpi==96?8:12;
                CHECK(probe_rgb(pixels,stride,x,y,255u,0u,0u));
            }
            if(selected==6) {
                const int x=dpi==72?11:dpi==96?16:22;
                const int y=dpi==72?18:dpi==96?24:37;
                CHECK(probe_rgb(pixels,stride,x,y,255u,0u,0u));
            }
            OK(quantapdf_render_page_with_options(
                page,&opts,&repeat));
            OK(quantapdf_bitmap_data(repeat,&again,&again_size));
            CHECK(again_size==size && again!=nullptr &&
                  std::memcmp(again,pixels,size)==0);
            quantapdf_drop_bitmap(repeat);repeat=nullptr;

            /* Public 90-degree display option is applied AFTER the
             * specific original selected page's Source rotation and
             * true-DPI native raster. Check every byte, not geometry
             * alone, even on fractional 96-DPI image edges. */
            opts.rotation_degrees=90.0f;
            OK(quantapdf_render_page_with_options(
                page,&opts,&repeat));
            int rw=0,rh=0,rs=0,rc=0;
            const unsigned char *rotated=nullptr;
            size_t rn=0u;
            OK(quantapdf_bitmap_dimensions(
                repeat,&rw,&rh,&rs,&rc));
            OK(quantapdf_bitmap_data(repeat,&rotated,&rn));
            CHECK(rw==h && rh==w && rs==rw*3 && rc==3 &&
                  rn==size && rotated!=nullptr);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const unsigned char *from=pixels+
                    (size_t)y*(size_t)stride+(size_t)x*3u;
                const unsigned char *to=rotated+
                    (size_t)x*(size_t)rs+(size_t)(h-1-y)*3u;
                CHECK(std::memcmp(from,to,3u)==0);
            }
            quantapdf_drop_bitmap(repeat);repeat=nullptr;

            if(dpi==72) {
                OK(quantapdf_render_thumbnail(
                    page,16,16,&thumb));
                int tw=0,th=0,ts=0,tc=0;
                OK(quantapdf_bitmap_dimensions(
                    thumb,&tw,&th,&ts,&tc));
                CHECK(tw==(source_rotated?12:16) &&
                      th==(source_rotated?16:12) &&
                      ts==tw*3 && tc==3);
                if(selected==1) {
                    const unsigned char *small=nullptr;
                    size_t small_size=0u;
                    OK(quantapdf_bitmap_data(
                        thumb,&small,&small_size));
                    CHECK(probe_rgb(small,ts,3,8,255u,0u,0u));
                }
            }
            quantapdf_drop_bitmap(thumb);thumb=nullptr;
        }
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        quantapdf_drop_context(ctx);
        if(bitmap!=nullptr) {
            const unsigned char *retained=nullptr;
            size_t n=0u;
            OK(quantapdf_bitmap_data(bitmap,&retained,&n));
            CHECK(retained!=nullptr && n!=0u &&
                  retained[0]==255u && retained[1]==255u);
        }
        quantapdf_drop_bitmap(bitmap);
    }
    /* Invalid structure or selected Resource must be refused on the
     * actual multi-page original. A bad later sibling invalidates even
     * a request for blank page zero; source V10 walks the FULL bounded
     * PageTree before accepting a selected page. Never return another
     * (usually blank) page after any structural failure. */
    for(int mode=1;mode<=5;++mode) {
        const std::string bad=selected_page_original_pdf(mode);
        CHECK(write_pdf(filename,bad));
        quantapdf_context *ctx=nullptr;
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1));
        OK(quantapdf_context_create(&ctx));
        const auto opened=quantapdf_open_with_context(
            ctx,filename.c_str(),nullptr,&doc);
        CHECK(std::remove(filename.c_str())==0);
        if(opened!=QUANTAPDF_OK) {
            CHECK(doc==nullptr);
            quantapdf_drop_context(ctx);
            continue; /* malformed input refused at document open */
        }
        const int selected=mode==5?2:0;
        const auto loaded=quantapdf_load_page(doc,selected,&page);
        if(loaded!=QUANTAPDF_OK) {
            CHECK(page==nullptr);
            quantapdf_close(doc);
            quantapdf_drop_context(ctx);
            continue; /* malformed input refused at Page lease */
        }
        const auto rendered=quantapdf_render_page(page,&bitmap);
        if(rendered!=QUANTAPDF_ERROR_FORMAT || bitmap!=nullptr)
            std::fprintf(stderr,
                "bad multi page-tree mode=%d selected=%d status=%d bitmap=%p\n",
                mode,selected,(int)rendered,(void*)bitmap);
        CHECK(rendered==QUANTAPDF_ERROR_FORMAT && bitmap==nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        quantapdf_drop_context(ctx);
    }
    return EXIT_SUCCESS;
}

/* An ORIGINAL QPDF-authored PDF with moved MediaBox and absolute
 * authorial CTM. The same content is not re-exported as an IR or passed
 * through a second pixel engine; the expected result is based on the
 * independent zero-origin original plus fixed RGB source goldens.
 * 0 exact page Media, 1 inherited Media; negatives 2 CropBox mismatch,
 * 3 fractional origin, 4 bound overflow, 5 content NOT translated. */
static std::string shifted_media_original(
    const std::string& original,const std::string& paint,
    int shift_x,int shift_y,int source_rotation,int mode)
{
    auto pdf=QPDF::create();
    pdf->setSuppressWarnings(true);
    pdf->setAttemptRecovery(false);
    pdf->processMemoryFile("native shifted MediaBox",
        original.data(),original.size(),"");
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pages.getKey("/Kids").getArrayItem(0);
    std::string box;
    if(mode==3) {
        /* Numerical +0.5: concatenating "-7" and ".5" makes -7.5
         * instead of the intended -6.5 and changes the page height!
         * QPDF serializes these finite binary-exact quarter endpoints. */
        box="["+std::to_string((double)shift_x+0.5)+" "+
             std::to_string((double)shift_y+0.5)+" "+
             std::to_string((double)(shift_x+32)+0.5)+" "+
             std::to_string((double)(shift_y+24)+0.5)+"]";
    } else {
        box="["+std::to_string(shift_x)+" "+
            std::to_string(shift_y)+" "+
            std::to_string(shift_x+32)+" "+
            std::to_string(shift_y+24)+"]";
    }
    if(mode==1) {
        page.removeKey("/MediaBox");
        pages.replaceKey("/MediaBox",Obj::parse(box));
        pages.replaceKey("/Rotate",Obj::newInteger(source_rotation));
    } else {
        page.replaceKey("/MediaBox",Obj::parse(box));
        page.replaceKey("/Rotate",Obj::newInteger(source_rotation));
    }
    if(mode==2) {
        page.replaceKey("/CropBox",Obj::parse(
            "["+std::to_string(shift_x+1)+" "+
                 std::to_string(shift_y)+" "+
                 std::to_string(shift_x+32)+" "+
                 std::to_string(shift_y+24)+"]"));
    }
    /* Deliberately use a REAL source CTM translation, not a bitmap
     * coordinate offset. The late-Text negative must fail because
     * of unsupported Text, not merely an unrelated out-of-box CTM. */
    const std::string translate=
        "q 1 0 0 1 "+std::to_string(shift_x)+" "+
         std::to_string(shift_y)+" cm "+paint+
         (mode==5?" BT ET":"")+" Q";
    page.replaceKey("/Contents",pdf->newStream(translate));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
}

static int native_media_origin_cases(const std::string& filename)
{
    const std::string originals[]={
        create_blank_source(0),create_path_only_source(0),
        create_image_only_source(0),create_source(false),
        create_image_only_source(5)
    };
    const std::string contents[]={
        "",
        "1 0 0 rg 4 4 8 6 re f",
        "q 16 0 0 12 8 3 cm /I Do Q",
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q "
        "Q 0.875 g 24 17 5 3 re f",
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q Q"
    };
    const struct {int x,y;} origins[]={
        {-13,-7},{15,11},{0,-9},{8,0}
    };
    const int rotations[]={0,90,180,270};
    const int dpis[]={72,96,144};
    for(size_t k=0u;k<5u;++k)
    for(int angle:rotations)
    for(int dpi:dpis) {
        const auto base=original_pdf_with_rotate(
            originals[k],angle,0);
        CHECK(write_pdf(filename,base));
        quantapdf_context *ctx=nullptr;
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=nullptr;
        OK(quantapdf_context_create(&ctx));
        OK(quantapdf_open_with_context(
            ctx,filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);opts.dpi=(float)dpi;
        OK(quantapdf_render_page_with_options(page,&opts,&bitmap));
        int w=0,h=0,stride=0,channels=0;
        const unsigned char *pixels=nullptr;
        size_t n=0u;
        OK(quantapdf_bitmap_dimensions(
            bitmap,&w,&h,&stride,&channels));
        OK(quantapdf_bitmap_data(bitmap,&pixels,&n));
        const std::vector<unsigned char> golden(pixels,pixels+n);
        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        quantapdf_drop_context(ctx);
        for(const auto& origin:origins)
        for(int inherited=0;inherited<=1;++inherited) {
            const auto shifted=shifted_media_original(
                originals[k],contents[k],
                origin.x,origin.y,angle,inherited);
            CHECK(write_pdf(filename,shifted));
            ctx=nullptr;doc=nullptr;page=nullptr;bitmap=nullptr;
            OK(quantapdf_context_create(&ctx));
            OK(quantapdf_open_with_context(
                ctx,filename.c_str(),nullptr,&doc));
            CHECK(std::remove(filename.c_str())==0);
            OK(quantapdf_load_page(doc,0,&page));
            quantapdf_rect rect={};
            OK(quantapdf_page_bounds(page,&rect));
            CHECK(rect.x0==0.0f && rect.y0==0.0f &&
                  rect.x1==(float)(angle==90||angle==270?24:32) &&
                  rect.y1==(float)(angle==90||angle==270?32:24));
            OK(quantapdf_render_page_with_options(
                page,&opts,&bitmap));
            int rw=0,rh=0,rs=0,rc=0;
            const unsigned char *sample=nullptr;
            size_t sz=0u;
            OK(quantapdf_bitmap_dimensions(
                bitmap,&rw,&rh,&rs,&rc));
            OK(quantapdf_bitmap_data(bitmap,&sample,&sz));
            if(rw!=w || rh!=h || rs!=stride || rc!=channels ||
               sz!=golden.size() || sample==nullptr ||
               std::memcmp(sample,golden.data(),sz)!=0)
                std::fprintf(stderr,
                    "Media M0 mismatch kind=%zu angle=%d dpi=%d origin=(%d,%d) inherited=%d\n",
                    k,angle,dpi,origin.x,origin.y,inherited);
            CHECK(rw==w && rh==h && rs==stride && rc==channels &&
                  sz==golden.size() && sample!=nullptr &&
                  std::memcmp(sample,golden.data(),sz)==0);
            if(k==1u) {
                /* Real SOURCE red PATH survives negative/positive
                 * PDF absolute origins without moving in display. */
                int sx=angle==90||angle==270?7:6;
                int sy=angle==90||angle==270?6:16;
                if(dpi!=72) {
                    sx=dpi==96?(angle==90||angle==270?10:8):
                       (angle==90||angle==270?15:12);
                    sy=dpi==96?(angle==90||angle==270?8:21):
                       (angle==90||angle==270?12:32);
                }
                /* Fixed red goldens for unrotated and 90-degree PDF
                 * originals. Other angles use exact full RGB map. */
                if(angle==0 || angle==90)
                    CHECK(probe_rgb(sample,rs,sx,sy,255u,0u,0u));
            }
            quantapdf_bitmap *again=nullptr,*display=nullptr;
            OK(quantapdf_render_page_with_options(
                page,&opts,&again));
            const unsigned char *same=nullptr;
            size_t same_size=0u;
            OK(quantapdf_bitmap_data(again,&same,&same_size));
            CHECK(same_size==sz && same!=nullptr &&
                  std::memcmp(sample,same,sz)==0);
            quantapdf_drop_bitmap(again);
            opts.rotation_degrees=90.0f;
            OK(quantapdf_render_page_with_options(
                page,&opts,&display));
            int dw=0,dh=0,ds=0,dc=0;
            const unsigned char *turned=nullptr;
            size_t turned_size=0u;
            OK(quantapdf_bitmap_dimensions(
                display,&dw,&dh,&ds,&dc));
            OK(quantapdf_bitmap_data(
                display,&turned,&turned_size));
            CHECK(dw==rh && dh==rw && ds==dw*3 && dc==3 &&
                  turned_size==sz && turned!=nullptr);
            for(int y=0;y<rh;++y)for(int x=0;x<rw;++x) {
                const unsigned char *from=sample+
                    (size_t)y*(size_t)rs+(size_t)x*3u;
                const unsigned char *to=turned+
                    (size_t)x*(size_t)ds+(size_t)(rh-1-y)*3u;
                CHECK(std::memcmp(from,to,3u)==0);
            }
            quantapdf_drop_bitmap(display);
            opts.rotation_degrees=0.0f;
            quantapdf_drop_page(page);
            quantapdf_close(doc);
            quantapdf_drop_context(ctx);
            const unsigned char *retained=nullptr;
            size_t retained_size=0u;
            OK(quantapdf_bitmap_data(
                bitmap,&retained,&retained_size));
            CHECK(retained_size==sz && retained!=nullptr &&
                  std::memcmp(retained,golden.data(),sz)==0);
            quantapdf_drop_bitmap(bitmap);
        }
    }
    /* An explicit non-full integer CropBox is now a real native
     * viewport. Fractional/huge origins and late Text STILL refuse. */
    for(int mode=2;mode<=5;++mode) {
        const auto original=mode==5 ?
            create_source(true):create_source(false);
        const auto malformed=shifted_media_original(
            original,contents[3],
            mode==4?1000001:15,mode==4?1000001:-7,0,mode);
        CHECK(write_pdf(filename,malformed));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        OK(quantapdf_open(filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_bitmap *result=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1));
        const auto status=quantapdf_render_page(page,&result);
        if(mode==2 || mode==3) {
            /* Mode 2 is an actual 31x24 crop viewport. Mode 3 uses a
             * separately admitted original quarter-point full Media
             * origin: [15.5 -6.5 47.5 17.5], with a real authorial
             * integer CTM. Neither can be treated as a 72-DPI crop
             * or silently refused after full source admission. */
            if(status!=QUANTAPDF_OK || result==nullptr)
                std::fprintf(stderr,
                    "origin legacy positive mode=%d status=%d output=%p\n",
                    mode,(int)status,(void*)result);
            CHECK(status==QUANTAPDF_OK && result!=nullptr);
            int w=0,h=0,stride=0,channels=0;
            const unsigned char *rgb=nullptr;
            size_t n=0u;
            OK(quantapdf_bitmap_dimensions(
                result,&w,&h,&stride,&channels));
            OK(quantapdf_bitmap_data(result,&rgb,&n));
            const int expected_w=mode==2?31:32;
            CHECK(w==expected_w && h==24 &&
                  stride==expected_w*3 && channels==3 &&
                  n==(size_t)expected_w*24u*3u && rgb!=nullptr);
            CHECK(probe_rgb(rgb,stride,
                mode==2?12:13,11,255u,0u,0u));
            CHECK(probe_rgb(rgb,stride,
                mode==2?25:26,5,223u,223u,223u));
            quantapdf_drop_bitmap(result);result=nullptr;
        } else
            CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && result==nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    return EXIT_SUCCESS;
}

/* Original PDF CropBox, not a post-raster bitmap crop. A real
 * authorial content stream crosses the effective Media∩Crop viewport;
 * RAW Image texture still retains its FULL original 2x2 XObject.
 * Mode 0 explicit crop, 1 inherited Crop and Media, 2 Crop extends
 * outside Media and uses the genuine intersection, 3 wrong type,
 * 4 fractional crop edge (currently unsupported), 5 empty intersection. */
static std::string crop_viewport_original(
    const std::string& original,const std::string& paint,
    int dx,int dy,int rotation,int mode)
{
    auto pdf=QPDF::create();
    pdf->setSuppressWarnings(true);
    pdf->setAttemptRecovery(false);
    pdf->processMemoryFile("native source CropBox",
        original.data(),original.size(),"");
    auto pages=pdf->getRoot().getKey("/Pages");
    auto page=pages.getKey("/Kids").getArrayItem(0);
    const std::string media="["+std::to_string(dx)+" "+
        std::to_string(dy)+" "+
        std::to_string(dx+32)+" "+
        std::to_string(dy+24)+"]";
    std::string crop="["+std::to_string(dx+8)+" "+
        std::to_string(dy+4)+" "+
        std::to_string(dx+24)+" "+
        std::to_string(dy+20)+"]";
    if(mode==2)
        crop="["+std::to_string(dx-3)+" "+
            std::to_string(dy-2)+" "+
            std::to_string(dx+24)+" "+
            std::to_string(dy+20)+"]";
    if(mode==3)crop="/NotABox";
    if(mode==4)
        /* Negative coordinates need NUMERICAL +0.5. Concatenating
         * -3 and ".5" would incorrectly serialize -3.5, changing
         * the original CropBox span from 15.5 to 16.5. */
        crop="["+std::to_string((double)(dx+8)+0.5)+" "+
            std::to_string(dy+4)+" "+
            std::to_string(dx+24)+" "+
            std::to_string(dy+20)+"]";
    if(mode==5)
        crop="["+std::to_string(dx+40)+" "+
            std::to_string(dy+40)+" "+
            std::to_string(dx+50)+" "+
            std::to_string(dy+50)+"]";
    if(mode==1) {
        page.removeKey("/MediaBox");
        pages.replaceKey("/MediaBox",Obj::parse(media));
        pages.replaceKey("/CropBox",Obj::parse(crop));
        pages.replaceKey("/Rotate",Obj::newInteger(rotation));
    } else {
        page.replaceKey("/MediaBox",Obj::parse(media));
        page.replaceKey("/CropBox",Obj::parse(crop));
        page.replaceKey("/Rotate",Obj::newInteger(rotation));
    }
    page.replaceKey("/Contents",pdf->newStream(
        "q 1 0 0 1 "+std::to_string(dx)+" "+
        std::to_string(dy)+" cm "+paint+" Q"));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
}

static int cropped_source_v10_cases(const std::string& filename)
{
    const std::string originals[]={
        create_blank_source(0),
        create_path_only_source(0),
        create_image_only_source(0),
        create_source(false),
        create_image_only_source(5)
    };
    const std::string contents[]={
        "",
        "1 0 0 rg 4 4 8 6 re f",
        "q 16 0 0 12 8 3 cm /I Do Q",
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q "
        "Q 0.875 g 24 17 5 3 re f",
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q Q"
    };
    const struct {int x,y;} origins[]={{-11,7},{13,-9}};
    const int dpis[]={72,96,144};
    const int turns[]={0,90,180,270};
    for(size_t k=0u;k<5u;++k)
    for(int dpi:dpis)
    for(int source_turn:turns) {
        const auto canonical=crop_viewport_original(
            originals[k],contents[k],0,0,source_turn,0);
        CHECK(write_pdf(filename,canonical));
        quantapdf_context *ctx=nullptr;
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bm=nullptr;
        OK(quantapdf_context_create(&ctx));
        OK(quantapdf_open_with_context(
            ctx,filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);
        opts.dpi=(float)dpi;
        OK(quantapdf_render_page_with_options(page,&opts,&bm));
        int rw=0,rh=0,rs=0,rc=0;
        const unsigned char *pixels=nullptr;
        size_t bytes=0u;
        OK(quantapdf_bitmap_dimensions(bm,&rw,&rh,&rs,&rc));
        OK(quantapdf_bitmap_data(bm,&pixels,&bytes));
        const int edge=dpi==72?16:dpi==96?22:32;
        CHECK(rw==edge && rh==edge && rs==edge*3 && rc==3 &&
              bytes==(size_t)edge*(size_t)rs && pixels!=nullptr);
        if(k==0u)
            for(size_t i=0u;i<bytes;++i)CHECK(pixels[i]==255u);
        /* Independently fixed sampled RGB pixels on the true SOURCE
         * intersection viewport at each native DPI; the rotated
         * original is checked against these unrotated visible points
         * on the same original page (not another renderer). */
        if(source_turn==0) {
            if(k==1u) {
                CHECK(probe_rgb(pixels,rs,
                    dpi==72?1:dpi==96?1:2,
                    dpi==72?12:dpi==96?17:24,
                    255u,0u,0u));
                CHECK(probe_rgb(pixels,rs,
                    dpi==72?5:dpi==96?7:10,
                    dpi==72?12:dpi==96?17:24,
                    255u,255u,255u));
            }
            if(k==2u) {
                CHECK(probe_rgb(pixels,rs,
                    dpi==72?3:dpi==96?5:6,
                    dpi==72?7:dpi==96?10:14,
                    255u,0u,0u));
                CHECK(probe_rgb(pixels,rs,
                    dpi==72?12:dpi==96?15:24,
                    dpi==72?7:dpi==96?10:14,
                    0u,255u,0u));
                CHECK(probe_rgb(pixels,rs,
                    dpi==72?3:dpi==96?5:6,
                    dpi==72?13:dpi==96?18:26,
                    0u,0u,255u));
                CHECK(probe_rgb(pixels,rs,
                    dpi==72?12:dpi==96?15:24,
                    dpi==72?13:dpi==96?18:26,
                    255u,255u,0u));
                CHECK(probe_rgb(pixels,rs,2,2,255u,255u,255u));
            }
            if(k==3u || k==4u) {
                CHECK(probe_rgb(pixels,rs,
                    dpi==72?5:dpi==96?7:10,
                    dpi==72?7:dpi==96?10:14,
                    255u,0u,0u));
                CHECK(probe_rgb(pixels,rs,
                    dpi==72?10:dpi==96?14:22,
                    dpi==72?7:dpi==96?10:14,
                    0u,255u,0u));
                CHECK(probe_rgb(pixels,rs,2,2,255u,255u,255u));
            }
        }
        const std::vector<unsigned char> golden(pixels,pixels+bytes);
        quantapdf_drop_bitmap(bm);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        quantapdf_drop_context(ctx);

        for(const auto& origin:origins)
        for(int inherited=0;inherited<=1;++inherited) {
            const auto original=crop_viewport_original(
                originals[k],contents[k],origin.x,origin.y,
                source_turn,inherited);
            CHECK(write_pdf(filename,original));
            ctx=nullptr;doc=nullptr;page=nullptr;bm=nullptr;
            OK(quantapdf_context_create(&ctx));
            OK(quantapdf_open_with_context(
                ctx,filename.c_str(),nullptr,&doc));
            CHECK(std::remove(filename.c_str())==0);
            OK(quantapdf_load_page(doc,0,&page));
            quantapdf_rect bounds{};
            OK(quantapdf_page_bounds(page,&bounds));
            CHECK(bounds.x0==0.0f && bounds.y0==0.0f &&
                  bounds.x1==16.0f && bounds.y1==16.0f);
            OK(quantapdf_render_page_with_options(page,&opts,&bm));
            const unsigned char *actual=nullptr;
            size_t actual_size=0u;
            int w=0,h=0,stride=0,channels=0;
            OK(quantapdf_bitmap_dimensions(
                bm,&w,&h,&stride,&channels));
            OK(quantapdf_bitmap_data(bm,&actual,&actual_size));
            if(actual==nullptr || actual_size!=golden.size() ||
               std::memcmp(actual,golden.data(),golden.size())!=0)
                std::fprintf(stderr,
                    "Crop M1 original mismatch kind=%zu dpi=%d pageRotate=%d origin=(%d,%d) inherited=%d\n",
                    k,dpi,source_turn,origin.x,origin.y,inherited);
            CHECK(w==rw && h==rh && stride==rs && channels==rc &&
                  actual!=nullptr && actual_size==golden.size() &&
                  std::memcmp(actual,golden.data(),golden.size())==0);
            quantapdf_bitmap *repeated=nullptr,*turned=nullptr;
            OK(quantapdf_render_page_with_options(
                page,&opts,&repeated));
            const unsigned char *same=nullptr;
            size_t same_size=0u;
            OK(quantapdf_bitmap_data(
                repeated,&same,&same_size));
            CHECK(same_size==actual_size && same!=nullptr &&
                  std::memcmp(same,actual,actual_size)==0);
            quantapdf_drop_bitmap(repeated);
            opts.rotation_degrees=90.0f;
            OK(quantapdf_render_page_with_options(
                page,&opts,&turned));
            const unsigned char *turn_data=nullptr;
            size_t turn_bytes=0u;
            OK(quantapdf_bitmap_data(
                turned,&turn_data,&turn_bytes));
            CHECK(turn_data!=nullptr && turn_bytes==actual_size);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const unsigned char *from=actual+
                    (size_t)y*(size_t)stride+(size_t)x*3u;
                const unsigned char *to=turn_data+
                    (size_t)x*(size_t)stride+
                    (size_t)(h-1-y)*3u;
                CHECK(std::memcmp(from,to,3u)==0);
            }
            quantapdf_drop_bitmap(turned);
            opts.rotation_degrees=0.0f;
            quantapdf_drop_page(page);
            quantapdf_close(doc);
            quantapdf_drop_context(ctx);
            const unsigned char *retained=nullptr;
            size_t retained_size=0u;
            OK(quantapdf_bitmap_data(
                bm,&retained,&retained_size));
            CHECK(retained!=nullptr && retained_size==actual_size &&
                  std::memcmp(retained,golden.data(),actual_size)==0);
            quantapdf_drop_bitmap(bm);
        }
    }
    /* A CropBox partly outside Media must use the exact visible
     * intersection, not its non-clamped original width. */
    for(int dpi:dpis) {
        const auto original=crop_viewport_original(
            originals[2],contents[2],-11,7,0,2);
        CHECK(write_pdf(filename,original));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bm=nullptr;
        OK(quantapdf_open(filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);opts.dpi=(float)dpi;
        OK(quantapdf_render_page_with_options(
            page,&opts,&bm));
        const int w=dpi==72?24:dpi==96?32:48;
        const int h=dpi==72?20:dpi==96?27:40;
        int rw=0,rh=0,rs=0,rc=0;
        const unsigned char *pixels=nullptr;
        size_t n=0u;
        OK(quantapdf_bitmap_dimensions(
            bm,&rw,&rh,&rs,&rc));
        OK(quantapdf_bitmap_data(bm,&pixels,&n));
        CHECK(rw==w && rh==h && rs==3*w && rc==3 &&
              n==(size_t)h*(size_t)rs && pixels!=nullptr);
        CHECK(probe_rgb(pixels,rs,
            dpi==72?11:dpi==96?15:22,
            dpi==72?8:dpi==96?11:16,
            255u,0u,0u));
        quantapdf_drop_bitmap(bm);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    /* The genuine selected SEVENTH leaf inherits Media/Resources,
     * original /Rotate270 and now CropBox from a nested /Pages node.
     * Other six leaves must retain their different own geometry.
     * This is not an IR oracle: QPDF authors one original 7-page PDF. */
    {
        const std::string seven=selected_page_original_pdf();
        auto pdf=QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile("native selected CropBox",
            seven.data(),seven.size(),"");
        auto root=pdf->getRoot().getKey("/Pages");
        auto branch=root.getKey("/Kids").getArrayItem(6);
        branch.replaceKey("/CropBox",Obj::parse("[8 4 24 20]"));
        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.write();
        const auto blob=writer.getBufferSharedPointer();
        const std::string original(
            reinterpret_cast<const char*>(blob->getBuffer()),
            blob->getSize());
        for(int dpi:dpis) {
            CHECK(write_pdf(filename,original));
            quantapdf_document *doc=nullptr;
            quantapdf_page *page=nullptr;
            quantapdf_bitmap *bm=nullptr;
            OK(quantapdf_open(filename.c_str(),nullptr,&doc));
            CHECK(std::remove(filename.c_str())==0);
            int pages=0;
            OK(quantapdf_page_count(doc,&pages));
            CHECK(pages==7);
            OK(quantapdf_load_page(doc,6,&page));
            quantapdf_rect bounds{};
            OK(quantapdf_page_bounds(page,&bounds));
            CHECK(bounds.x0==0.0f && bounds.y0==0.0f &&
                  bounds.x1==16.0f && bounds.y1==16.0f);
            quantapdf_render_options opts{};
            opts.struct_size=sizeof(opts);opts.dpi=(float)dpi;
            OK(quantapdf_render_page_with_options(page,&opts,&bm));
            int w=0,h=0,stride=0,channels=0;
            const unsigned char *pixels=nullptr;
            size_t n=0u;
            OK(quantapdf_bitmap_dimensions(
                bm,&w,&h,&stride,&channels));
            OK(quantapdf_bitmap_data(bm,&pixels,&n));
            const int edge=dpi==72?16:dpi==96?22:32;
            CHECK(w==edge && h==edge && stride==3*edge &&
                  channels==3 && n==(size_t)edge*(size_t)stride &&
                  pixels!=nullptr);
            /* Source /Rotate270 maps visible full-image quadrants:
             * unrotated red (3,7),green (12,7),blue (3,13),
             * yellow (12,13) => x'=y, y'=w-1-x. */
            const int red_x=dpi==72?7:dpi==96?10:14;
            const int red_y=dpi==72?12:dpi==96?16:25;
            const int green_y=dpi==72?3:dpi==96?6:7;
            const int bottom_x=dpi==72?13:dpi==96?18:26;
            CHECK(probe_rgb(pixels,stride,
                red_x,red_y,255u,0u,0u));
            CHECK(probe_rgb(pixels,stride,
                red_x,green_y,0u,255u,0u));
            CHECK(probe_rgb(pixels,stride,
                bottom_x,red_y,0u,0u,255u));
            CHECK(probe_rgb(pixels,stride,
                bottom_x,green_y,255u,255u,0u));
            quantapdf_bitmap *again=nullptr;
            opts.rotation_degrees=90.0f;
            OK(quantapdf_render_page_with_options(
                page,&opts,&again));
            const unsigned char *rotated=nullptr;
            size_t rotated_bytes=0u;
            OK(quantapdf_bitmap_data(
                again,&rotated,&rotated_bytes));
            CHECK(rotated_bytes==n && rotated!=nullptr);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const unsigned char *from=pixels+
                    (size_t)y*(size_t)stride+(size_t)x*3u;
                const unsigned char *to=rotated+
                    (size_t)x*(size_t)stride+(size_t)(h-1-y)*3u;
                CHECK(std::memcmp(from,to,3u)==0);
            }
            quantapdf_drop_bitmap(again);
            quantapdf_drop_page(page);
            quantapdf_close(doc);
            const unsigned char *owner=nullptr;
            size_t owner_bytes=0u;
            OK(quantapdf_bitmap_data(bm,&owner,&owner_bytes));
            CHECK(owner!=nullptr && owner_bytes==n &&
                  std::memcmp(owner,pixels,n)==0);
            quantapdf_drop_bitmap(bm);
        }
    }
    /* Independent structure refusal is not made into silent blank ink. */
    for(int mode=3;mode<=5;++mode) {
        const auto invalid=crop_viewport_original(
            originals[2],contents[2],-11,7,0,mode);
        CHECK(write_pdf(filename,invalid));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bm=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1));
        const auto opened=quantapdf_open(filename.c_str(),nullptr,&doc);
        CHECK(std::remove(filename.c_str())==0);
        if(opened!=QUANTAPDF_OK) {
            CHECK(doc==nullptr);
            continue;
        }
        const auto loaded=quantapdf_load_page(doc,0,&page);
        if(loaded!=QUANTAPDF_OK) {
            CHECK(page==nullptr);
            quantapdf_close(doc);
            continue;
        }
        const auto status=quantapdf_render_page(page,&bm);
        if(mode==4) {
            /* A fractional source CropBox span is now qualified:
             * [8.5,24] yields 15.5 device points and a 16x16 RGB
             * canvas. The RAW green edge in column 15 is exactly
             * 8/16 covered with its full original XObject scale. */
            CHECK(status==QUANTAPDF_OK && bm!=nullptr);
            int w=0,h=0,s=0,ch=0;
            const unsigned char *rgb=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_dimensions(bm,&w,&h,&s,&ch));
            OK(quantapdf_bitmap_data(bm,&rgb,&bytes));
            CHECK(w==16 && h==16 && s==48 && ch==3 &&
                  bytes==768u && rgb!=nullptr);
            CHECK(probe_rgb(rgb,s,4,6,255u,0u,0u));
            CHECK(probe_rgb(rgb,s,15,6,127u,255u,127u));
            CHECK(probe_rgb(rgb,s,15,1,255u,255u,255u));
            quantapdf_drop_bitmap(bm);
        } else
            CHECK(status==QUANTAPDF_ERROR_FORMAT && bm==nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    return EXIT_SUCCESS;
}

/* QPDF-authored original fractional Media/Crop endpoint fixtures.
 * One valid M0 viewport has an EXACT integer span but quarter-point
 * signed origin. No QPDF rendering or second raster oracle.
 * Case 0: Media[.25 .5 32.25 24.5] / no Crop.
 * Case 1: Media[-7.25 -.5 24.75 23.5] / no Crop.
 * Case 2: Media[.25 .5 32.25 24.5], Crop[8.25 4.5 24.25 20.5].
 * Case 3: same as case 2 but inherited Media/Crop on root /Pages.
 * Negative 4/5: cropped spans 16.5/Media span 31.5,
 * 6: decimal .1 (not quarter), 7: decimal .125 (not quarter). */
static std::string original_quarter_box_pdf(
    const std::string& original,int fixture,int source_rotation)
{
    auto pdf=QPDF::create();
    pdf->setSuppressWarnings(true);
    pdf->setAttemptRecovery(false);
    pdf->processMemoryFile("original fractional quarter PDF",
        original.data(),original.size(),"");
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=root.getKey("/Kids").getArrayItem(0);
    std::string media="[0.25 0.5 32.25 24.5]";
    std::string crop;
    if(fixture==1)media="[-7.25 -0.5 24.75 23.5]";
    if(fixture==2 || fixture==3)
        crop="[8.25 4.5 24.25 20.5]";
    if(fixture==4)
        crop="[8.25 4.5 24.75 20.5]";
    if(fixture==5)
        media="[0.25 0.5 31.75 24.5]";
    if(fixture==6)
        media="[0.1 0.5 32.1 24.5]";
    if(fixture==7)
        media="[-7.125 0.5 24.875 24.5]";
    if(fixture==3) {
        page.removeKey("/MediaBox");
        root.replaceKey("/MediaBox",Obj::parse(media));
        root.replaceKey("/CropBox",Obj::parse(crop));
        root.replaceKey("/Rotate",Obj::newInteger(source_rotation));
    } else {
        page.replaceKey("/MediaBox",Obj::parse(media));
        if(!crop.empty())page.replaceKey("/CropBox",Obj::parse(crop));
        page.replaceKey("/Rotate",Obj::newInteger(source_rotation));
    }
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
}

static int native_fractional_quarter_cases(const std::string& filename)
{
    const std::string originals[]={
        create_blank_source(0),create_path_only_source(0),
        create_image_only_source(0),create_image_only_source(5)
    };
    const int dpis[]={72,96,144};
    for(size_t kind=0u;kind<4u;++kind)
    for(int dpi:dpis)
    for(int fixture=0;fixture<=3;++fixture) {
        CHECK(write_pdf(filename,original_quarter_box_pdf(
            originals[kind],fixture,0)));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bm=nullptr;
        OK(quantapdf_open(filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);
        opts.dpi=(float)dpi;
        const auto render_status=
            quantapdf_render_page_with_options(page,&opts,&bm);
        if(render_status!=QUANTAPDF_OK)
            std::fprintf(stderr,
                "fractional quarter Source rejected kind=%zu dpi=%d fixture=%d status=%d bitmap=%p\n",
                kind,dpi,fixture,(int)render_status,(void*)bm);
        CHECK(render_status==QUANTAPDF_OK && bm!=nullptr);
        int w=0,h=0,s=0,ch=0;
        const unsigned char *px=nullptr;
        size_t bytes=0u;
        OK(quantapdf_bitmap_dimensions(bm,&w,&h,&s,&ch));
        OK(quantapdf_bitmap_data(bm,&px,&bytes));
        const int expected_w=fixture>=2 ?
            (dpi==72?16:dpi==96?22:32):
            (dpi==72?32:dpi==96?43:64);
        const int expected_h=fixture>=2 ?
            (dpi==72?16:dpi==96?22:32):
            (dpi==72?24:dpi==96?32:48);
        CHECK(w==expected_w && h==expected_h &&
              s==3*w && ch==3 &&
              bytes==(size_t)w*(size_t)h*3u && px!=nullptr);
        if(kind==0u)
            for(size_t i=0u;i<bytes;++i)CHECK(px[i]==255u);
        if(fixture==0 && kind==1u) {
            /* PATH from x=4, Media left=.25 -> x=3.75 at 72,
             * x=5.0 at 96 and x=7.5 at 144. Single native grid,
             * independent 16-sample geometric coverage RGB. */
            if(dpi==72) {
                CHECK(probe_rgb(px,s,3,16,255u,191u,191u));
                CHECK(probe_rgb(px,s,4,16,255u,0u,0u));
            } else if(dpi==96) {
                CHECK(probe_rgb(px,s,4,22,255u,255u,255u));
                CHECK(probe_rgb(px,s,5,22,255u,0u,0u));
            } else {
                CHECK(probe_rgb(px,s,7,32,255u,127u,127u));
                CHECK(probe_rgb(px,s,8,32,255u,0u,0u));
            }
        }
        if(fixture==0 && kind==2u) {
            /* IMAGE right edge: 23.75 source device units, so 3/4
             * coverage at 72 and 96, half at 144. Green quadrant
             * source channel remains nearest-neighbor from FULL RAW. */
            if(dpi==72) {
                CHECK(probe_rgb(px,s,23,12,64u,255u,64u));
                CHECK(probe_rgb(px,s,8,12,255u,0u,0u));
            } else if(dpi==96) {
                CHECK(probe_rgb(px,s,31,16,64u,255u,64u));
                CHECK(probe_rgb(px,s,11,16,255u,0u,0u));
            } else {
                CHECK(probe_rgb(px,s,47,24,127u,255u,127u));
                CHECK(probe_rgb(px,s,17,24,255u,0u,0u));
            }
        }
        if(fixture>=2 && kind==2u) {
            if(dpi==72) {
                CHECK(probe_rgb(px,s,14,7,0u,255u,0u));
                CHECK(probe_rgb(px,s,15,7,64u,255u,64u));
            } else if(dpi==96) {
                CHECK(probe_rgb(px,s,20,10,0u,255u,0u));
                CHECK(probe_rgb(px,s,21,10,255u,255u,255u));
            } else {
                CHECK(probe_rgb(px,s,30,14,0u,255u,0u));
                CHECK(probe_rgb(px,s,31,14,127u,255u,127u));
            }
        }
        if(fixture>=2 && kind==3u)
            CHECK(probe_rgb(px,s,1,1,255u,255u,255u));
        quantapdf_bitmap *again=nullptr,*rotated=nullptr;
        OK(quantapdf_render_page_with_options(page,&opts,&again));
        const unsigned char *same=nullptr;
        size_t same_size=0u;
        OK(quantapdf_bitmap_data(again,&same,&same_size));
        CHECK(same_size==bytes && same!=nullptr &&
              std::memcmp(same,px,bytes)==0);
        quantapdf_drop_bitmap(again);
        opts.rotation_degrees=90.0f;
        OK(quantapdf_render_page_with_options(page,&opts,&rotated));
        const unsigned char *turned=nullptr;
        size_t turned_bytes=0u;
        int rw=0,rh=0,rs=0,rc=0;
        OK(quantapdf_bitmap_dimensions(
            rotated,&rw,&rh,&rs,&rc));
        OK(quantapdf_bitmap_data(
            rotated,&turned,&turned_bytes));
        CHECK(rw==h && rh==w && rs==rw*3 && rc==3 &&
              turned_bytes==bytes && turned!=nullptr);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const unsigned char *before=px+
                (size_t)y*(size_t)s+(size_t)x*3u;
            const unsigned char *after=turned+
                (size_t)x*(size_t)rs+(size_t)(h-1-y)*3u;
            CHECK(std::memcmp(before,after,3u)==0);
        }
        quantapdf_drop_bitmap(rotated);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        const unsigned char *owned=nullptr;
        size_t owned_size=0u;
        OK(quantapdf_bitmap_data(bm,&owned,&owned_size));
        CHECK(owned!=nullptr && owned_size==bytes &&
              owned[0]==px[0]);
        quantapdf_drop_bitmap(bm);
    }
    /* Old M0 rejects fractional SPANS; M1 must admit their real
     * physical ceil dimensions and SOURCE Image coverage. A decimal
     * outside the quarter lattice still fails atomically. */
    for(int fixture=4;fixture<=7;++fixture) {
        CHECK(write_pdf(filename,original_quarter_box_pdf(
            originals[2],fixture,0)));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1));
        OK(quantapdf_open(filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        const auto status=quantapdf_render_page(page,&bitmap);
        if(fixture<=5) {
            CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
            int w=0,h=0,stride=0,channels=0;
            const unsigned char *rgb=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_dimensions(
                bitmap,&w,&h,&stride,&channels));
            OK(quantapdf_bitmap_data(bitmap,&rgb,&bytes));
            const int width=fixture==4?17:32;
            const int height=fixture==4?16:24;
            CHECK(w==width && h==height && stride==3*w &&
                  channels==3 && rgb!=nullptr &&
                  bytes==(size_t)stride*(size_t)height);
            if(fixture==4) {
                CHECK(probe_rgb(rgb,stride,2,7,255u,0u,0u));
                CHECK(probe_rgb(rgb,stride,11,7,0u,255u,0u));
                CHECK(probe_rgb(rgb,stride,15,7,64u,255u,64u));
                CHECK(probe_rgb(rgb,stride,16,7,255u,255u,255u));
            } else {
                CHECK(probe_rgb(rgb,stride,10,11,255u,0u,0u));
                CHECK(probe_rgb(rgb,stride,20,11,0u,255u,0u));
                CHECK(probe_rgb(rgb,stride,31,11,255u,255u,255u));
            }
            quantapdf_drop_bitmap(bitmap);
        } else
            CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED &&
                  bitmap==nullptr);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
    }
    return EXIT_SUCCESS;
}

/* Exact original PDF /Rotate in a quarter-point MediaBox, plus
 * the REAL seventh selected PageTree leaf inheriting a quarter-point
 * Crop and /Rotate270 from /Pages. These authored sources are NOT
 * synthetic Composer IR and do not call an external pixel renderer. */
static int native_fractional_quarter_rotation_cases(const std::string& filename)
{
    const int dpis[]={72,96,144};
    for(int dpi:dpis)
    for(int angle: {90,180,270}) {
        const auto original=original_quarter_box_pdf(
            create_image_only_source(0),0,angle);
        CHECK(write_pdf(filename,original));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bm=nullptr;
        OK(quantapdf_open(filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);opts.dpi=(float)dpi;
        OK(quantapdf_render_page_with_options(page,&opts,&bm));
        int w=0,h=0,s=0,nc=0;
        const unsigned char *px=nullptr;
        size_t n=0u;
        OK(quantapdf_bitmap_dimensions(bm,&w,&h,&s,&nc));
        OK(quantapdf_bitmap_data(bm,&px,&n));
        const int canonical_w=dpi==72?32:dpi==96?43:64;
        const int canonical_h=dpi==72?24:dpi==96?32:48;
        const bool quarter=angle==90||angle==270;
        CHECK(w==(quarter?canonical_h:canonical_w) &&
              h==(quarter?canonical_w:canonical_h) &&
              s==w*3 && nc==3 && n==(size_t)s*(size_t)h &&
              px!=nullptr);
        int rx=0,ry=0,gx=0,gy=0,bx=0,by=0,yx=0,yy=0;
        if(angle==90) {
            rx=12;ry=12;gx=12;gy=20;
            bx=6;by=12;yx=6;yy=20;
        } else if(angle==180) {
            rx=20;ry=12;gx=12;gy=12;
            bx=20;by=6;yx=12;yy=6;
        } else {
            rx=12;ry=20;gx=12;gy=12;
            bx=18;by=20;yx=18;yy=12;
        }
        const auto device=[&](int value) {
            return dpi==72?value:
                   dpi==96?(value*4+1)/3:value*2;
        };
        CHECK(probe_rgb(px,s,device(rx),device(ry),255u,0u,0u));
        CHECK(probe_rgb(px,s,device(gx),device(gy),0u,255u,0u));
        CHECK(probe_rgb(px,s,device(bx),device(by),0u,0u,255u));
        CHECK(probe_rgb(px,s,device(yx),device(yy),255u,255u,0u));
        const std::vector<unsigned char> stable(px,px+n);
        quantapdf_bitmap *turned=nullptr;
        opts.rotation_degrees=90.0f;
        OK(quantapdf_render_page_with_options(page,&opts,&turned));
        int tw=0,th=0,ts=0,tc=0;
        const unsigned char *tp=nullptr;
        size_t tn=0u;
        OK(quantapdf_bitmap_dimensions(
            turned,&tw,&th,&ts,&tc));
        OK(quantapdf_bitmap_data(turned,&tp,&tn));
        CHECK(tw==h && th==w && ts==tw*3 && tc==3 &&
              tn==n && tp!=nullptr);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const unsigned char *from=stable.data()+
                (size_t)y*(size_t)s+(size_t)x*3u;
            const unsigned char *to=tp+
                (size_t)x*(size_t)ts+(size_t)(h-1-y)*3u;
            CHECK(std::memcmp(from,to,3u)==0);
        }
        quantapdf_drop_bitmap(turned);
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        const unsigned char *owner=nullptr;
        size_t owner_size=0u;
        OK(quantapdf_bitmap_data(bm,&owner,&owner_size));
        CHECK(owner_size==n && owner!=nullptr &&
              std::memcmp(owner,stable.data(),n)==0);
        quantapdf_drop_bitmap(bm);
    }

    const auto seven=selected_page_original_pdf();
    auto pdf=QPDF::create();
    pdf->setSuppressWarnings(true);
    pdf->setAttemptRecovery(false);
    pdf->processMemoryFile("selected original quarter Crop",
        seven.data(),seven.size(),"");
    auto branch=pdf->getRoot().getKey("/Pages")
        .getKey("/Kids").getArrayItem(6);
    branch.replaceKey("/MediaBox",
        Obj::parse("[0.25 0.5 32.25 24.5]"));
    branch.replaceKey("/CropBox",
        Obj::parse("[8.25 4.5 24.25 20.5]"));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto blob=writer.getBufferSharedPointer();
    const std::string selected_original(
        reinterpret_cast<const char*>(blob->getBuffer()),blob->getSize());
    for(int dpi:dpis) {
        CHECK(write_pdf(filename,selected_original));
        quantapdf_document *doc=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bm=nullptr;
        OK(quantapdf_open(filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        int count=0;
        OK(quantapdf_page_count(doc,&count));
        CHECK(count==7);
        OK(quantapdf_load_page(doc,6,&page));
        quantapdf_rect bounds{};
        OK(quantapdf_page_bounds(page,&bounds));
        CHECK(bounds.x0==0.0f && bounds.y0==0.0f &&
              bounds.x1==16.0f && bounds.y1==16.0f);
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);opts.dpi=(float)dpi;
        OK(quantapdf_render_page_with_options(page,&opts,&bm));
        int w=0,h=0,s=0,ch=0;
        const unsigned char *px=nullptr;
        size_t n=0u;
        OK(quantapdf_bitmap_dimensions(bm,&w,&h,&s,&ch));
        OK(quantapdf_bitmap_data(bm,&px,&n));
        const int edge=dpi==72?16:dpi==96?22:32;
        CHECK(w==edge && h==edge && s==w*3 && ch==3 &&
              n==(size_t)s*(size_t)h && px!=nullptr);
        const auto pixel=[&](int v) {
            return dpi==72?v:dpi==96?(v*4+1)/3:v*2;
        };
        CHECK(probe_rgb(px,s,pixel(8),pixel(13),255u,0u,0u));
        CHECK(probe_rgb(px,s,pixel(8),pixel(4),0u,255u,0u));
        CHECK(probe_rgb(px,s,pixel(13),pixel(13),0u,0u,255u));
        CHECK(probe_rgb(px,s,pixel(13),pixel(4),255u,255u,0u));
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        const unsigned char *retained=nullptr;
        size_t sz=0u;
        OK(quantapdf_bitmap_data(bm,&retained,&sz));
        CHECK(retained!=nullptr && sz==n &&
              retained[0]==px[0]);
        quantapdf_drop_bitmap(bm);
    }
    return EXIT_SUCCESS;
}

/* True fractional original page SPANS. Every expected dimension comes
 * from exact quarter-point endpoints x dpi / 72, rounded UP only for
 * device pixel storage. Ink is sampled against the unrounded boundary.
 * QPDF authors the original; no IR re-export or second pixel provider. */
static std::string native_partial_span_pdf(
    const std::string& original,int mode,int rotation)
{
    auto pdf=QPDF::create();
    pdf->setSuppressWarnings(true);
    pdf->setAttemptRecovery(false);
    pdf->processMemoryFile("true source fractional spans",
        original.data(),original.size(),"");
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=root.getKey("/Kids").getArrayItem(0);
    const bool crop=mode>=2;
    const std::string media=mode==0 || mode==1 ?
        "[0 0 16.5 12.25]":"[0 0 32 24]";
    const std::string crop_box=mode==2 || mode==3 ?
        "[8 3 24.5 15.25]":"[8 3 24.75 15.5]";
    page.replaceKey("/MediaBox",Obj::parse(media));
    if(crop)page.replaceKey("/CropBox",Obj::parse(crop_box));
    page.replaceKey("/Rotate",Obj::newInteger(rotation));
    if(mode==1)
        page.replaceKey("/Contents",pdf->newStream(
            "1 0 0 rg 0 0 32 24 re f"));
    if(mode==0)
        page.removeKey("/Contents");
    if(mode==2 || mode==3 || mode==4) {
        const std::string content=mode==3 ?
            "q 8 3 16.5 12.25 re W n "
            "q 32 0 0 24 0 0 cm /I Do Q Q":
            "q 32 0 0 24 0 0 cm /I Do Q";
        page.replaceKey("/Contents",pdf->newStream(content));
    }
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    auto out=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(out->getBuffer()),out->getSize());
}

static int native_fractional_span_cases(const std::string& filename)
{
    const std::string originals[]={
        create_blank_source(0),
        create_path_only_source(0),
        create_image_only_source(0),
        create_image_only_source(0),
        create_image_only_source(0)
    };
    const int dpis[]={72,96,144};
    for(int mode=0;mode<=4;++mode)
    for(int dpi:dpis) {
        CHECK(write_pdf(filename,native_partial_span_pdf(
            originals[mode],mode,0)));
        quantapdf_context *context=nullptr;
        quantapdf_document *document=nullptr;
        quantapdf_page *page=nullptr;
        quantapdf_bitmap *bitmap=nullptr;
        OK(quantapdf_context_create(&context));
        OK(quantapdf_open_with_context(context,
            filename.c_str(),nullptr,&document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(document,0,&page));
        quantapdf_render_options opts{};
        opts.struct_size=sizeof(opts);opts.dpi=(float)dpi;
        const auto status=quantapdf_render_page_with_options(
            page,&opts,&bitmap);
        if(status!=QUANTAPDF_OK)
            std::fprintf(stderr,
                "true partial Source failed mode=%d dpi=%d status=%d\n",
                mode,dpi,(int)status);
        CHECK(status==QUANTAPDF_OK && bitmap!=nullptr);
        int w=0,h=0,stride=0,ch=0;
        const unsigned char *pixels=nullptr;
        size_t size=0u;
        OK(quantapdf_bitmap_dimensions(
            bitmap,&w,&h,&stride,&ch));
        OK(quantapdf_bitmap_data(bitmap,&pixels,&size));
        const int expected_w=mode==4 ?
            (dpi==72?17:dpi==96?23:34):
            (dpi==72?17:dpi==96?22:33);
        const int expected_h=mode<=3 ?
            (dpi==72?13:dpi==96?17:25):
            (dpi==72?13:dpi==96?17:25);
        CHECK(w==expected_w && h==expected_h &&
              stride==w*3 && ch==3 &&
              size==(size_t)stride*(size_t)h &&
              pixels!=nullptr);
        if(mode==0)
            for(size_t i=0u;i<size;++i)
                CHECK(pixels[i]==255u);
        if(mode==1) {
            /* Full-cover authored PATH crosses the true viewport. Its
             * last pixel rows and columns are not force-stretched. */
            CHECK(probe_rgb(pixels,stride,5,5,255u,0u,0u));
            if(dpi==72) {
                CHECK(probe_rgb(pixels,stride,16,4,255u,127u,127u));
                CHECK(probe_rgb(pixels,stride,4,12,255u,191u,191u));
                CHECK(probe_rgb(pixels,stride,16,12,255u,223u,223u));
            } else if(dpi==96) {
                CHECK(probe_rgb(pixels,stride,21,4,255u,0u,0u));
                CHECK(probe_rgb(pixels,stride,5,16,255u,191u,191u));
            } else {
                CHECK(probe_rgb(pixels,stride,32,4,255u,0u,0u));
                CHECK(probe_rgb(pixels,stride,5,24,255u,127u,127u));
            }
        }
        if(mode==2) {
            if(dpi==72) {
                CHECK(probe_rgb(pixels,stride,16,2,127u,255u,127u));
                CHECK(probe_rgb(pixels,stride,4,12,191u,191u,255u));
                CHECK(probe_rgb(pixels,stride,16,12,255u,255u,223u));
            } else if(dpi==96) {
                CHECK(probe_rgb(pixels,stride,20,16,255u,255u,191u));
            } else {
                CHECK(probe_rgb(pixels,stride,32,24,255u,255u,127u));
            }
        }
        const std::vector<unsigned char> stable(pixels,pixels+size);
        if(mode==3) {
            /* W/W* matching full source rectangle is one sample mask:
             * at the right page edge it must equal the *unclipped*
             * mode2 source, not multiply 8/16 coverage twice. */
            CHECK(write_pdf(filename,native_partial_span_pdf(
                originals[2],2,0)));
            quantapdf_document *other=nullptr;
            quantapdf_page *other_page=nullptr;
            quantapdf_bitmap *other_bitmap=nullptr;
            OK(quantapdf_open(filename.c_str(),nullptr,&other));
            CHECK(std::remove(filename.c_str())==0);
            OK(quantapdf_load_page(other,0,&other_page));
            OK(quantapdf_render_page_with_options(
                other_page,&opts,&other_bitmap));
            const unsigned char *other_pixels=nullptr;
            size_t other_size=0u;
            OK(quantapdf_bitmap_data(
                other_bitmap,&other_pixels,&other_size));
            CHECK(other_size==size && other_pixels!=nullptr &&
                  std::memcmp(other_pixels,stable.data(),size)==0);
            quantapdf_drop_bitmap(other_bitmap);
            quantapdf_drop_page(other_page);
            quantapdf_close(other);
        }
        quantapdf_bitmap *repeat=nullptr,*turned=nullptr;
        OK(quantapdf_render_page_with_options(
            page,&opts,&repeat));
        const unsigned char *rp=nullptr;size_t rn=0u;
        OK(quantapdf_bitmap_data(repeat,&rp,&rn));
        CHECK(rp!=nullptr && rn==size &&
              std::memcmp(rp,stable.data(),rn)==0);
        quantapdf_drop_bitmap(repeat);
        opts.rotation_degrees=90.0f;
        OK(quantapdf_render_page_with_options(
            page,&opts,&turned));
        int rw=0,rh=0,rs=0,rc=0;
        const unsigned char *rotated=nullptr;size_t rr=0u;
        OK(quantapdf_bitmap_dimensions(
            turned,&rw,&rh,&rs,&rc));
        OK(quantapdf_bitmap_data(turned,&rotated,&rr));
        CHECK(rw==h && rh==w && rs==rw*3 && rc==3 &&
              rr==size && rotated!=nullptr);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const unsigned char *from=stable.data()+
                (size_t)y*(size_t)stride+(size_t)x*3u;
            const unsigned char *to=rotated+
                (size_t)x*(size_t)rs+(size_t)(h-1-y)*3u;
            CHECK(std::memcmp(from,to,3u)==0);
        }
        quantapdf_drop_bitmap(turned);
        quantapdf_drop_page(page);
        quantapdf_close(document);
        quantapdf_drop_context(context);
        const unsigned char *retained=nullptr;size_t retained_size=0u;
        OK(quantapdf_bitmap_data(
            bitmap,&retained,&retained_size));
        CHECK(retained_size==size && retained!=nullptr &&
              std::memcmp(retained,stable.data(),size)==0);
        quantapdf_drop_bitmap(bitmap);
    }
    return EXIT_SUCCESS;
}

int main()
{
    const std::string filename="native-standalone-smoke.pdf";
    CHECK(high_dpi_cases(filename)==EXIT_SUCCESS);
    CHECK(original_source_rotation_cases(filename)==EXIT_SUCCESS);
    CHECK(selected_page_v10_cases(filename)==EXIT_SUCCESS);
    CHECK(native_media_origin_cases(filename)==EXIT_SUCCESS);
    CHECK(cropped_source_v10_cases(filename)==EXIT_SUCCESS);
    CHECK(native_fractional_quarter_cases(filename)==EXIT_SUCCESS);
    CHECK(native_fractional_quarter_rotation_cases(filename)==EXIT_SUCCESS);
    CHECK(native_fractional_span_cases(filename)==EXIT_SUCCESS);
    CHECK(write_pdf(filename,create_source(false)));

    quantapdf_context *context=nullptr;
    quantapdf_document *document=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr,*second=nullptr,*thumbnail=nullptr;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(context,filename.c_str(),nullptr,&document));
    CHECK(std::remove(filename.c_str())==0);
    int count=0;
    OK(quantapdf_page_count(document,&count));
    CHECK(count==1);
    OK(quantapdf_load_page(document,0,&page));

    OK(quantapdf_render_page(page,&bitmap));
    CHECK(bitmap!=nullptr);
    int width=0,height=0,stride=0,components=0;
    const unsigned char *data=nullptr;
    size_t size=0u;
    OK(quantapdf_bitmap_dimensions(bitmap,&width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(bitmap,&data,&size));
    CHECK(width==32 && height==24 && components==3 && stride==96 &&
          size==32u*24u*3u && data!=nullptr);
    CHECK(probe_rgb(data,stride,13,11,255u,0u,0u));
    CHECK(probe_rgb(data,stride,26,5,223u,223u,223u));
    CHECK(probe_rgb(data,stride,2,2,255u,255u,255u));

    quantapdf_render_options opts{};
    opts.struct_size=sizeof(opts);
    opts.dpi=72.0f;
    OK(quantapdf_render_page_with_options(page,&opts,&second));
    CHECK(second!=nullptr);
    const unsigned char *bytes2=nullptr;
    size_t size2=0;
    OK(quantapdf_bitmap_data(second,&bytes2,&size2));
    CHECK(size2==size && std::memcmp(data,bytes2,size)==0);
    quantapdf_drop_bitmap(second);second=nullptr;

    /* The original W-Clipped RGB Image plus Q-restored Gray PATH is
     * now qualified in TRUE 96-DPI device space. This is not an
     * interpolated 72-DPI bitmap: sample each geometry on 43x32. */
    opts.dpi=96.0f;
    OK(quantapdf_render_page_with_options(page,&opts,&second));
    CHECK(second!=nullptr);
    {
        int dw=0,dh=0,ds=0,dc=0;
        const unsigned char *dp=nullptr;
        size_t dn=0u;
        OK(quantapdf_bitmap_dimensions(second,&dw,&dh,&ds,&dc));
        OK(quantapdf_bitmap_data(second,&dp,&dn));
        CHECK(dw==43 && dh==32 && ds==129 && dc==3 &&
              dn==43u*32u*3u && dp!=nullptr);
        CHECK(probe_rgb(dp,ds,18,16,255u,0u,0u));
        CHECK(probe_rgb(dp,ds,24,16,0u,255u,0u));
        CHECK(probe_rgb(dp,ds,15,16,255u,255u,255u));
        CHECK(probe_rgb(dp,ds,35,7,223u,223u,223u));
    }
    quantapdf_drop_bitmap(second);second=nullptr;
    opts.dpi=72.0f;
    /* Exact 72-DPI quarter turns are pure row/column permutations of
     * the qualified original native bitmap, including clipped pixels and
     * PATH+IMAGE source order. The planner swaps dimensions at 90/270,
     * while the 180 transform preserves them. No cross-engine oracle. */
    quantapdf_bitmap *rotated_retained=nullptr;
    const int angles[3]={90,180,270};
    for(int angle:angles) {
        opts.rotation_degrees=(float)angle;
        OK(quantapdf_render_page_with_options(page,&opts,&second));
        CHECK(second!=nullptr);
        int rw=0,rh=0,rs=0,rc=0;
        const unsigned char *rotated=nullptr;
        size_t rotated_size=0u;
        OK(quantapdf_bitmap_dimensions(second,&rw,&rh,&rs,&rc));
        OK(quantapdf_bitmap_data(second,&rotated,&rotated_size));
        const int expected_w=angle==180?width:height;
        const int expected_h=angle==180?height:width;
        CHECK(rw==expected_w && rh==expected_h && rc==3 &&
              rs==rw*3 && rotated_size==size && rotated!=nullptr);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const int dx=angle==90?height-1-y:
                         angle==180?width-1-x:y;
            const int dy=angle==90?x:
                         angle==180?height-1-y:width-1-x;
            const unsigned char *from=data+(size_t)y*(size_t)stride+
                                      (size_t)x*3u;
            const unsigned char *to=rotated+(size_t)dy*(size_t)rs+
                                      (size_t)dx*3u;
            CHECK(std::memcmp(from,to,3u)==0);
        }
        const int red_x=angle==90?height-1-11:
                        angle==180?width-1-13:11;
        const int red_y=angle==90?13:
                        angle==180?height-1-11:width-1-13;
        CHECK(probe_rgb(rotated,rs,red_x,red_y,255u,0u,0u));
        quantapdf_bitmap *repeat=nullptr;
        OK(quantapdf_render_page_with_options(page,&opts,&repeat));
        const unsigned char *bytes_again=nullptr;
        size_t repeated_size=0u;
        OK(quantapdf_bitmap_data(repeat,&bytes_again,&repeated_size));
        CHECK(bytes_again!=nullptr && repeated_size==rotated_size &&
              std::memcmp(bytes_again,rotated,rotated_size)==0);
        quantapdf_drop_bitmap(repeat);
        if(angle==270)rotated_retained=second;
        else quantapdf_drop_bitmap(second);
        second=nullptr;
    }
    opts.rotation_degrees=45.0f;
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_page_with_options(page,&opts,&second)==
          QUANTAPDF_ERROR_UNSUPPORTED && second==nullptr);
    opts.rotation_degrees=360.0f;
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_page_with_options(page,&opts,&second)==
          QUANTAPDF_ERROR_UNSUPPORTED && second==nullptr);
    opts.rotation_degrees=-90.0f;
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_page_with_options(page,&opts,&second)==
          QUANTAPDF_ERROR_UNSUPPORTED && second==nullptr);
    /* A W-Clipped Image followed by a Q-restored Gray PATH uses the
     * genuine 96-DPI source/device grid BEFORE its lossless 90-degree
     * RGB orientation. This was an obsolete unsupported test. */
    opts.rotation_degrees=90.0f;opts.dpi=96.0f;
    OK(quantapdf_render_page_with_options(page,&opts,&second));
    CHECK(second!=nullptr);
    {
        int rw=0,rh=0,rs=0,rc=0;
        const unsigned char *rgb=nullptr;
        size_t n=0u;
        OK(quantapdf_bitmap_dimensions(second,&rw,&rh,&rs,&rc));
        OK(quantapdf_bitmap_data(second,&rgb,&n));
        CHECK(rw==32 && rh==43 && rs==96 && rc==3 &&
              rgb!=nullptr && n==32u*43u*3u);
        /* Unrotated device samples (18,16) RED and (35,7)
         * 223-Gray become (15,18) and (24,35) after 90 degrees. */
        CHECK(probe_rgb(rgb,rs,15,18,255u,0u,0u));
        CHECK(probe_rgb(rgb,rs,24,35,223u,223u,223u));
    }
    quantapdf_drop_bitmap(second);second=nullptr;
    opts.dpi=72.0f;opts.rotation_degrees=0.0f;
    /* A native 72-DPI image is the sole source for bounded thumbnail
     * area resampling; every 2x2 RGB box has an exact arithmetic mean,
     * with no PDFium or rescaling-specific source traversal. */
    OK(quantapdf_render_thumbnail(page,16,12,&thumbnail));
    CHECK(thumbnail!=nullptr);
    int tw=0,th=0,ts=0,tc=0;
    const unsigned char *tp=nullptr;
    size_t tn=0u;
    OK(quantapdf_bitmap_dimensions(thumbnail,&tw,&th,&ts,&tc));
    OK(quantapdf_bitmap_data(thumbnail,&tp,&tn));
    CHECK(tw==16 && th==12 && ts==48 && tc==3 &&
          tn==16u*12u*3u && tp!=nullptr);
    for(int y=0;y<12;++y)for(int x=0;x<16;++x)
        for(int channel=0;channel<3;++channel) {
            unsigned sum=0u;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                sum+=data[(size_t)(2*y+dy)*(size_t)stride+
                          (size_t)(2*x+dx)*3u+(size_t)channel];
            CHECK(tp[(size_t)y*(size_t)ts+(size_t)x*3u+
                     (size_t)channel]==(sum+2u)/4u);
        }
    /* Scale to the limiting height using an uneven fractional ratio. */
    OK(quantapdf_render_thumbnail(page,7,5,&second));
    OK(quantapdf_bitmap_dimensions(second,&tw,&th,&ts,&tc));
    CHECK(tw==6 && th==5 && ts==18 && tc==3);
    OK(quantapdf_bitmap_data(second,&tp,&tn));
    CHECK(tp!=nullptr && tn==6u*5u*3u &&
          probe_rgb(tp,ts,0,0,255u,255u,255u));
    quantapdf_drop_bitmap(second);second=nullptr;

    /* Exact 1x1 area mean must account for ALL original image pixels. */
    OK(quantapdf_render_thumbnail(page,1,1,&second));
    OK(quantapdf_bitmap_dimensions(second,&tw,&th,&ts,&tc));
    CHECK(tw==1 && th==1 && ts==3 && tc==3);
    OK(quantapdf_bitmap_data(second,&tp,&tn));
    CHECK(tp!=nullptr && tn==3u);
    for(int channel=0;channel<3;++channel) {
        unsigned sum=0u;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
            sum+=data[(size_t)y*(size_t)stride+
                      (size_t)x*3u+(size_t)channel];
        CHECK(tp[channel]==(sum+(unsigned)(width*height)/2u)/
                           (unsigned)(width*height));
    }
    quantapdf_drop_bitmap(second);second=nullptr;

    /* A larger bound never upscales, returns byte-identical Native RGB. */
    OK(quantapdf_render_thumbnail(page,64,64,&second));
    OK(quantapdf_bitmap_dimensions(second,&tw,&th,&ts,&tc));
    CHECK(tw==32 && th==24 && ts==96 && tc==3);
    OK(quantapdf_bitmap_data(second,&tp,&tn));
    CHECK(tn==size && std::memcmp(tp,data,size)==0);
    quantapdf_drop_bitmap(second);second=nullptr;

    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_thumbnail(page,0,12,&second)==
          QUANTAPDF_ERROR_ARGUMENT && second==nullptr);

    /* The output owns its detached native bytes and retains Context.
     * Only the returned Bitmap remains after the source owners go away. */
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_context(context);
    const unsigned char *retained_rotated=nullptr;
    size_t retained_rotated_size=0u;
    int retained_w=0,retained_h=0,retained_stride=0,retained_components=0;
    OK(quantapdf_bitmap_dimensions(rotated_retained,
        &retained_w,&retained_h,&retained_stride,&retained_components));
    OK(quantapdf_bitmap_data(rotated_retained,
        &retained_rotated,&retained_rotated_size));
    CHECK(retained_w==24 && retained_h==32 && retained_stride==72 &&
          retained_components==3 && retained_rotated_size==size &&
          probe_rgb(retained_rotated,retained_stride,11,18,255u,0u,0u));
    quantapdf_drop_bitmap(rotated_retained);
    data=nullptr;size=0;
    OK(quantapdf_bitmap_data(bitmap,&data,&size));
    CHECK(probe_rgb(data,stride,13,11,255u,0u,0u));
    OK(quantapdf_bitmap_data(thumbnail,&tp,&tn));
    CHECK(tp!=nullptr && tn==16u*12u*3u &&
          probe_rgb(tp,48,1,1,255u,255u,255u));
    quantapdf_drop_bitmap(thumbnail);
    quantapdf_drop_bitmap(bitmap);

    CHECK(write_pdf(filename,create_source(true)));
    document=nullptr;page=nullptr;second=nullptr;
    OK(quantapdf_open(filename.c_str(),nullptr,&document));
    CHECK(std::remove(filename.c_str())==0);
    OK(quantapdf_load_page(document,0,&page));
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_page(page,&second)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(second==nullptr); /* no raster fallback / partial publication */
    second=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
    CHECK(quantapdf_render_thumbnail(page,16,12,&second)==
          QUANTAPDF_ERROR_UNSUPPORTED && second==nullptr);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    /* The PDF structure may legitimately have no painting. Native V10
     * must return an opaque white page, not a failed Source profile or
     * a guessed incomplete white page after unsupported operators. */
    for(int mode=0;mode<6;++mode) {
        CHECK(write_pdf(filename,create_blank_source(mode)));
        quantapdf_context *blank_context=nullptr;
        quantapdf_document *blank_document=nullptr;
        quantapdf_page *blank_page=nullptr;
        quantapdf_bitmap *blank=nullptr,*small=nullptr;
        OK(quantapdf_context_create(&blank_context));
        OK(quantapdf_open_with_context(
            blank_context,filename.c_str(),nullptr,&blank_document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(blank_document,0,&blank_page));
        const quantapdf_status st=quantapdf_render_page(
            blank_page,&blank);
        if(mode>=4) {
            CHECK(st==QUANTAPDF_ERROR_UNSUPPORTED && blank==nullptr);
            small=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_thumbnail(blank_page,16,12,&small)==
                  QUANTAPDF_ERROR_UNSUPPORTED && small==nullptr);
        } else {
            CHECK(st==QUANTAPDF_OK && blank!=nullptr);
            int w=0,h=0,s=0,c=0;
            const unsigned char *buffer=nullptr;
            size_t n=0u;
            OK(quantapdf_bitmap_dimensions(blank,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(blank,&buffer,&n));
            CHECK(w==32 && h==24 && s==96 && c==3 &&
                  n==32u*24u*3u && buffer!=nullptr);
            for(size_t i=0u;i<n;++i)CHECK(buffer[i]==255u);
            OK(quantapdf_render_thumbnail(blank_page,16,12,&small));
            OK(quantapdf_bitmap_dimensions(small,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(small,&buffer,&n));
            CHECK(w==16 && h==12 && s==48 && c==3 &&
                  n==16u*12u*3u);
            for(size_t i=0u;i<n;++i)CHECK(buffer[i]==255u);
        }
        quantapdf_drop_page(blank_page);
        quantapdf_close(blank_document);
        quantapdf_drop_context(blank_context);
        if(blank!=nullptr) {
            const unsigned char *bytes=nullptr;
            size_t length=0u;
            OK(quantapdf_bitmap_data(blank,&bytes,&length));
            CHECK(bytes!=nullptr && length==32u*24u*3u &&
                  bytes[0]==255u && bytes[length-1u]==255u);
        }
        quantapdf_drop_bitmap(small);
        quantapdf_drop_bitmap(blank);
    }


    /* One or two opaque PATHs, no Image/Clip resources. Compare fixed
     * independently declared RGB goldens, NOT any PDFium oracle. Later
     * unsupported Text/Stroke/Clip/third PATH must refuse completely. */
    for(int mode=0;mode<10;++mode) {
        CHECK(write_pdf(filename,create_path_only_source(mode)));
        quantapdf_context *path_context=nullptr;
        quantapdf_document *path_document=nullptr;
        quantapdf_page *path_page=nullptr;
        quantapdf_bitmap *filled=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1)),*repeated=nullptr,*small=nullptr;
        OK(quantapdf_context_create(&path_context));
        OK(quantapdf_open_with_context(
            path_context,filename.c_str(),nullptr,&path_document));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(path_document,0,&path_page));
        const auto st=quantapdf_render_page(path_page,&filled);
        const bool valid=mode<=2 || mode==6 || mode==7;
        if(!valid) {
            CHECK(st==QUANTAPDF_ERROR_UNSUPPORTED && filled==nullptr);
            small=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_thumbnail(path_page,16,12,&small)==
                  QUANTAPDF_ERROR_UNSUPPORTED && small==nullptr);
        } else {
            CHECK(st==QUANTAPDF_OK && filled!=nullptr);
            int w=0,h=0,s=0,c=0;
            const unsigned char *px=nullptr,*rp=nullptr;
            size_t n=0u,rn=0u;
            OK(quantapdf_bitmap_dimensions(filled,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(filled,&px,&n));
            CHECK(w==32 && h==24 && s==96 && c==3 &&
                  n==32u*24u*3u && px!=nullptr);
            const unsigned red=mode==2 ? 128u : 255u;
            const unsigned green=mode==2 ? 128u : 0u;
            const unsigned blue=mode==2 ? 128u : 0u;
            CHECK(probe_rgb(px,s,6,17,red,green,blue));
            CHECK(probe_rgb(px,s,1,1,255u,255u,255u));
            if(mode>=6) {
                CHECK(probe_rgb(px,s,10,18,0u,255u,0u));
                CHECK(probe_rgb(px,s,14,11,0u,0u,255u));
                CHECK(probe_rgb(px,s,14,13,
                    mode==7?255u:0u,255u,0u));
                CHECK(probe_rgb(px,s,11,15,
                    mode==7?255u:0u,255u,0u));
                CHECK(probe_rgb(px,s,18,12,0u,0u,255u));
                CHECK(probe_rgb(px,s,22,12,255u,255u,255u));
            } else {
                CHECK(probe_rgb(px,s,14,11,
                    mode==1?0u:255u,
                    mode==1?0u:255u,
                    255u));
            }
            OK(quantapdf_render_page(path_page,&repeated));
            OK(quantapdf_bitmap_data(repeated,&rp,&rn));
            CHECK(rn==n && rp!=nullptr && std::memcmp(px,rp,n)==0);
            quantapdf_drop_bitmap(repeated);repeated=nullptr;
            OK(quantapdf_render_thumbnail(path_page,16,12,&small));
            OK(quantapdf_bitmap_dimensions(small,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(small,&rp,&rn));
            CHECK(w==16 && h==12 && s==48 && c==3 &&
                  rn==16u*12u*3u && rp!=nullptr);
            CHECK(probe_rgb(rp,s,3,8,red,green,blue));
            if(mode>=6) {
                CHECK(probe_rgb(rp,s,5,9,0u,255u,0u));
                CHECK(probe_rgb(rp,s,7,5,0u,0u,255u));
                CHECK(probe_rgb(rp,s,6,7,
                    mode==7?255u:0u,255u,0u));
            } else {
                CHECK(probe_rgb(rp,s,7,5,
                    mode==1?0u:255u,
                    mode==1?0u:255u,
                    255u));
            }
        }
        quantapdf_drop_page(path_page);
        quantapdf_close(path_document);
        quantapdf_drop_context(path_context);
        if(filled!=nullptr) {
            const unsigned char *owned=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_data(filled,&owned,&bytes));
            CHECK(owned!=nullptr && bytes==32u*24u*3u &&
                  probe_rgb(owned,96,6,17,
                            mode==2?128u:255u,
                            mode==2?128u:0u,
                            mode==2?128u:0u));
        }
        quantapdf_drop_bitmap(small);
        quantapdf_drop_bitmap(filled);
    }


    /* One-to-four native IMAGE layers with deterministic independent
     * RGB goldens. Fifth IMAGE / late Text rejects atomically; original
     * RAW/TIFF Predictor image bytes, thumbnail, no fallback and actual
     * Context Bitmap ownership stay under the existing C11 budget. */
    for(int mode=0;mode<22;++mode) {
        CHECK(write_pdf(filename,create_image_only_source(mode)));
        quantapdf_context *image_context=nullptr;
        quantapdf_document *image_doc=nullptr;
        quantapdf_page *image_page=nullptr;
        quantapdf_bitmap *rendered=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1)),*again=nullptr,*small=nullptr;
        OK(quantapdf_context_create(&image_context));
        OK(quantapdf_open_with_context(
            image_context,filename.c_str(),nullptr,&image_doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(image_doc,0,&image_page));
        const auto result=quantapdf_render_page(image_page,&rendered);
        const bool clipped=mode==5 || mode==9 || mode==11;
        const bool image_only_four=mode==6 || mode==17 ||
                                   mode==20 || mode==21;
        if(mode>=4 && !clipped && !image_only_four) {
            /* Unknown /J is an invalid resource; an unmatched Q is
             * malformed original-PDF graphics-stack syntax. Both are
             * FORMAT failures, not ordinary unsupported features. */
            const auto rejection=(mode==14 || mode==15) ?
                QUANTAPDF_ERROR_FORMAT : QUANTAPDF_ERROR_UNSUPPORTED;
            if(result!=rejection || rendered!=nullptr)
                std::fprintf(stderr,
                    "[native-two-image] refused mode=%d got=%d expected=%d bitmap=%p\n",
                    mode,(int)result,(int)rejection,(void*)rendered);
            CHECK(result==rejection && rendered==nullptr);
            small=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            const auto thumb_status=
                quantapdf_render_thumbnail(image_page,16,12,&small);
            if(thumb_status!=rejection || small!=nullptr)
                std::fprintf(stderr,
                    "[native-two-image] thumbnail mode=%d got=%d expected=%d bitmap=%p\n",
                    mode,(int)thumb_status,(int)rejection,(void*)small);
            CHECK(thumb_status==rejection && small==nullptr);
        } else {
            CHECK(result==QUANTAPDF_OK && rendered!=nullptr);
            int w=0,h=0,s=0,c=0;
            const unsigned char *pixels=nullptr,*repeat=nullptr;
            size_t bytes=0u,repeat_size=0u;
            OK(quantapdf_bitmap_dimensions(rendered,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(rendered,&pixels,&bytes));
            CHECK(w==32 && h==24 && s==96 && c==3 &&
                  bytes==32u*24u*3u && pixels!=nullptr);
            const bool gray=mode==2 || mode==3 || mode==20;
            if(clipped) {
                /* The Clip is x=[12,20), page y=[6,15):
                 * original four RAW quadrants are partially visible.
                 * Full image outside this real Clip must remain WHITE. */
                CHECK(probe_rgb(pixels,s,13,11,255u,0u,0u));
                CHECK(probe_rgb(pixels,s,18,11,0u,255u,0u));
                CHECK(probe_rgb(pixels,s,13,16,0u,0u,255u));
                CHECK(probe_rgb(pixels,s,18,16,255u,255u,0u));
                CHECK(probe_rgb(pixels,s,20,11,255u,255u,255u));
                CHECK(probe_rgb(pixels,s,10,11,255u,255u,255u));
                CHECK(probe_rgb(pixels,s,13,19,255u,255u,255u));
                if(mode==9) {
                    /* Second source Do paints top half of the Clip:
                     * its bottom image row overwrites first top row. */
                    CHECK(probe_rgb(pixels,s,13,13,0u,0u,255u));
                    CHECK(probe_rgb(pixels,s,18,13,255u,255u,0u));
                }
            } else {
                CHECK(probe_rgb(pixels,s,13,11,
                    255u,gray?255u:0u,gray?255u:0u));
                CHECK(probe_rgb(pixels,s,20,11,
                    gray?170u:0u,gray?170u:255u,gray?170u:0u));
                CHECK(probe_rgb(pixels,s,13,16,
                    gray?85u:0u,gray?85u:0u,255u*(unsigned)!gray+
                        (gray?85u:0u)));
                CHECK(probe_rgb(pixels,s,20,16,
                    gray?0u:255u,gray?0u:255u,0u));
            }
            CHECK(probe_rgb(pixels,s,2,2,255u,255u,255u));
            if(mode==1 || image_only_four) {
                CHECK(probe_rgb(pixels,s,26,19,
                    255u,gray?255u:0u,gray?255u:0u));
                CHECK(probe_rgb(pixels,s,30,19,
                    gray?170u:0u,gray?170u:255u,gray?170u:0u));
            }
            if(image_only_four) {
                CHECK(probe_rgb(pixels,s,2,19,
                    255u,gray?255u:0u,gray?255u:0u));
                CHECK(probe_rgb(pixels,s,6,19,
                    gray?170u:0u,gray?170u:255u,gray?170u:0u));
            }
            if(mode==17 || mode==20) {
                CHECK(probe_rgb(pixels,s,26,8,
                    255u,gray?255u:0u,gray?255u:0u));
                CHECK(probe_rgb(pixels,s,30,8,
                    gray?170u:0u,gray?170u:255u,gray?170u:0u));
                CHECK(probe_rgb(pixels,s,26,13,255u,255u,255u));
            }
            if(mode==21) {
                /* Four paints, final /I overlays prior GREEN with RED
                 * but leaves the preceding bottom-row YELLOW untouched. */
                CHECK(probe_rgb(pixels,s,18,11,255u,0u,0u));
                CHECK(probe_rgb(pixels,s,22,11,0u,255u,0u));
                CHECK(probe_rgb(pixels,s,18,16,255u,255u,0u));
            }
            OK(quantapdf_render_page(image_page,&again));
            OK(quantapdf_bitmap_data(again,&repeat,&repeat_size));
            CHECK(repeat_size==bytes && repeat!=nullptr &&
                  std::memcmp(pixels,repeat,bytes)==0);
            quantapdf_drop_bitmap(again);again=nullptr;
            OK(quantapdf_render_thumbnail(image_page,16,12,&small));
            OK(quantapdf_bitmap_dimensions(small,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(small,&repeat,&repeat_size));
            CHECK(w==16 && h==12 && s==48 && c==3 &&
                  repeat_size==16u*12u*3u && repeat!=nullptr);
            CHECK(probe_rgb(repeat,s,6,5,
                255u,gray?255u:0u,gray?255u:0u));
            if(clipped) {
                CHECK(probe_rgb(repeat,s,5,5,255u,255u,255u));
                CHECK(probe_rgb(repeat,s,9,5,0u,255u,0u));
                if(mode==9)
                    CHECK(probe_rgb(repeat,s,6,6,0u,0u,255u));
            }
            if(mode==17 || mode==20) {
                /* 16x12 thumbnail at x=15 spans full x=[30,32).
                 * Row y=3 samples only the fourth IMAGE top row;
                 * y=4 straddles its green/yellow (or 170/0 Gray)
                 * boundary, requiring exact native area averaging. */
                CHECK(probe_rgb(repeat,s,15,3,
                    gray?170u:0u,gray?170u:255u,gray?170u:0u));
                CHECK(probe_rgb(repeat,s,15,4,
                    gray?85u:128u,gray?85u:255u,gray?85u:0u));
            }
            if(mode==21)
                CHECK(probe_rgb(repeat,s,9,5,255u,0u,0u));
        }
        quantapdf_drop_page(image_page);
        quantapdf_close(image_doc);
        quantapdf_drop_context(image_context);
        if(rendered!=nullptr) {
            const unsigned char *owner=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_data(rendered,&owner,&bytes));
            CHECK(owner!=nullptr && bytes==32u*24u*3u &&
                  probe_rgb(owner,96,13,11,255u,
                    (mode==2 || mode==3 || mode==20)?255u:0u,
                    (mode==2 || mode==3 || mode==20)?255u:0u));
        }
        quantapdf_drop_bitmap(small);
        quantapdf_drop_bitmap(rendered);
    }


    /* Public Source V10 admits two-paint IMAGE+PATH in either order.
     * Mode 3 also qualifies both paints under ONE frozen W Clip after
     * the same-Clip feature was introduced. Remaining negative modes
     * still must poison both public full/thumbnail handles. */
    for(int mode=0;mode<9;++mode) {
        CHECK(write_pdf(filename,create_unclipped_mixed_source(mode)));
        quantapdf_context *mixed_context=nullptr;
        quantapdf_document *mixed_doc=nullptr;
        quantapdf_page *mixed_page=nullptr;
        quantapdf_bitmap *rendered=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1)),*again=nullptr,*small=nullptr;
        OK(quantapdf_context_create(&mixed_context));
        OK(quantapdf_open_with_context(
            mixed_context,filename.c_str(),nullptr,&mixed_doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(mixed_doc,0,&mixed_page));
        const auto status=quantapdf_render_page(mixed_page,&rendered);
        const bool valid=mode==0 || mode==1 || mode==3 || mode==8;
        if(!valid) {
            const auto expected_status=mode==6 ?
                QUANTAPDF_ERROR_FORMAT:QUANTAPDF_ERROR_UNSUPPORTED;
            if(status!=expected_status || rendered!=nullptr)
                std::fprintf(stderr,
                    "[native-mixed] mode=%d expected=%d got=%d bitmap=%p\n",
                    mode,(int)expected_status,(int)status,(void*)rendered);
            CHECK(status==expected_status && rendered==nullptr);
            small=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_thumbnail(mixed_page,16,12,&small)==
                  (mode==6?QUANTAPDF_ERROR_FORMAT:
                            QUANTAPDF_ERROR_UNSUPPORTED) &&
                  small==nullptr);
        } else {
            CHECK(status==QUANTAPDF_OK && rendered!=nullptr);
            const bool gray=mode==8, image_last=mode==1;
            int w=0,h=0,s=0,c=0;
            const unsigned char *pixels=nullptr,*repeat=nullptr;
            size_t bytes=0u,repeat_size=0u;
            OK(quantapdf_bitmap_dimensions(rendered,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(rendered,&pixels,&bytes));
            CHECK(w==32 && h==24 && s==96 && c==3 &&
                  bytes==32u*24u*3u && pixels!=nullptr);
            if(mode==3) {
                /* New qualified single W clip x=[4,12), bitmap y=[14,20).
                 * IMAGE paints first, then green PATH covers the clipped
                 * overlap. Any original image pixels OUTSIDE the Clip
                 * must remain white; this is never a fallback. */
                CHECK(probe_rgb(pixels,s,10,16,0u,255u,0u));
                CHECK(probe_rgb(pixels,s,6,16,0u,255u,0u));
                CHECK(probe_rgb(pixels,s,10,13,255u,255u,255u));
                CHECK(probe_rgb(pixels,s,13,11,255u,255u,255u));
                CHECK(probe_rgb(pixels,s,13,16,255u,255u,255u));
                CHECK(probe_rgb(pixels,s,20,16,255u,255u,255u));
                CHECK(probe_rgb(pixels,s,2,2,255u,255u,255u));
            } else {
                /* At (10,16), source Image and green PATH overlap.
                 * At (6,16), only PATH paints. The other probes are
                 * independent Image sample quadrants. */
                CHECK(probe_rgb(pixels,s,10,16,
                    image_last?(gray?85u:0u):0u,
                    image_last?(gray?85u:0u):255u,
                    image_last?(gray?85u:255u):0u));
                CHECK(probe_rgb(pixels,s,6,16,0u,255u,0u));
                CHECK(probe_rgb(pixels,s,13,11,255u,
                    gray?255u:0u,gray?255u:0u));
                CHECK(probe_rgb(pixels,s,20,11,
                    gray?170u:0u,gray?170u:255u,gray?170u:0u));
                CHECK(probe_rgb(pixels,s,13,16,
                    gray?85u:0u,gray?85u:0u,gray?85u:255u));
                CHECK(probe_rgb(pixels,s,20,16,
                    gray?0u:255u,gray?0u:255u,0u));
                CHECK(probe_rgb(pixels,s,2,2,255u,255u,255u));
            }
            OK(quantapdf_render_page(mixed_page,&again));
            OK(quantapdf_bitmap_data(again,&repeat,&repeat_size));
            CHECK(repeat!=nullptr && repeat_size==bytes &&
                  std::memcmp(pixels,repeat,bytes)==0);
            quantapdf_drop_bitmap(again);again=nullptr;
            OK(quantapdf_render_thumbnail(mixed_page,16,12,&small));
            OK(quantapdf_bitmap_dimensions(small,&w,&h,&s,&c));
            OK(quantapdf_bitmap_data(small,&repeat,&repeat_size));
            CHECK(w==16 && h==12 && s==48 && c==3 &&
                  repeat!=nullptr && repeat_size==16u*12u*3u);
            if(mode==3) {
                CHECK(probe_rgb(repeat,s,5,8,0u,255u,0u));
                CHECK(probe_rgb(repeat,s,3,8,0u,255u,0u));
                CHECK(probe_rgb(repeat,s,5,6,255u,255u,255u));
            } else {
                CHECK(probe_rgb(repeat,s,5,8,
                    image_last?(gray?85u:0u):0u,
                    image_last?(gray?85u:0u):255u,
                    image_last?(gray?85u:255u):0u));
                CHECK(probe_rgb(repeat,s,3,8,0u,255u,0u));
            }
        }
        quantapdf_drop_page(mixed_page);
        quantapdf_close(mixed_doc);
        quantapdf_drop_context(mixed_context);
        if(rendered!=nullptr) {
            const unsigned char *owned=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_data(rendered,&owned,&bytes));
            CHECK(owned!=nullptr && bytes==32u*24u*3u &&
                  probe_rgb(owned,96,6,16,0u,255u,0u));
        }
        if(small!=nullptr) {
            const unsigned char *owned=nullptr;
            size_t bytes=0u;
            OK(quantapdf_bitmap_data(small,&owned,&bytes));
            CHECK(owned!=nullptr && bytes==16u*12u*3u &&
                  probe_rgb(owned,48,3,8,0u,255u,0u));
        }
        quantapdf_drop_bitmap(small);
        quantapdf_drop_bitmap(rendered);
    }

    /* Public native Source V10: exactly one opaque PATH Fill and one
     * Image Do in either source order under one SAME W/W* Clip. Native
     * fixed RGB/thumbnail probes are independent of any second renderer. */
    for(int mode=0;mode<7;++mode) {
        CHECK(write_pdf(filename,create_clipped_mixed_source(mode)));
        quantapdf_context *ctx=nullptr;
        quantapdf_document *doc=nullptr;
        quantapdf_page *clipped_page=nullptr;
        quantapdf_bitmap *filled=reinterpret_cast<quantapdf_bitmap*>(
            uintptr_t(1)),*again=nullptr,*small=nullptr;
        OK(quantapdf_context_create(&ctx));
        OK(quantapdf_open_with_context(
            ctx,filename.c_str(),nullptr,&doc));
        CHECK(std::remove(filename.c_str())==0);
        OK(quantapdf_load_page(doc,0,&clipped_page));
        const auto st=quantapdf_render_page(clipped_page,&filled);
        if(mode>=2) {
            const auto rejection=(mode==4 || mode==5)?
                QUANTAPDF_ERROR_FORMAT:QUANTAPDF_ERROR_UNSUPPORTED;
            CHECK(st==rejection && filled==nullptr);
            small=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
            CHECK(quantapdf_render_thumbnail(clipped_page,16,12,&small)==
                  rejection && small==nullptr);
        } else {
            CHECK(st==QUANTAPDF_OK && filled!=nullptr);
            int w=0,h=0,clipped_stride=0,channels=0;
            size_t clipped_count=0u,other_count=0u;
            const unsigned char *bytes=nullptr,*other=nullptr;
            OK(quantapdf_bitmap_dimensions(
                filled,&w,&h,&clipped_stride,&channels));
            OK(quantapdf_bitmap_data(filled,&bytes,&clipped_count));
            CHECK(w==32 && h==24 && clipped_stride==96 && channels==3 &&
                  clipped_count==32u*24u*3u && bytes!=nullptr);
            /* Clip x=[12,20), y=[9,18). The Image paints on its
             * left half, while the blue PATH covers the whole Clip. */
            CHECK(probe_rgb(bytes,clipped_stride,13,11,
                0u,mode==1?255u:0u,mode==1?0u:255u));
            CHECK(probe_rgb(bytes,clipped_stride,13,16,
                mode==1?255u:0u,mode==1?255u:0u,mode==1?0u:255u));
            CHECK(probe_rgb(bytes,clipped_stride,18,11,0u,0u,255u));
            CHECK(probe_rgb(bytes,clipped_stride,18,16,0u,0u,255u));
            CHECK(probe_rgb(bytes,clipped_stride,11,11,255u,255u,255u));
            CHECK(probe_rgb(bytes,clipped_stride,20,11,255u,255u,255u));
            CHECK(probe_rgb(bytes,clipped_stride,13,19,255u,255u,255u));
            OK(quantapdf_render_page(clipped_page,&again));
            OK(quantapdf_bitmap_data(again,&other,&other_count));
            CHECK(other!=nullptr && other_count==clipped_count &&
                  std::memcmp(bytes,other,clipped_count)==0);
            quantapdf_drop_bitmap(again);again=nullptr;
            OK(quantapdf_render_thumbnail(clipped_page,16,12,&small));
            OK(quantapdf_bitmap_dimensions(
                small,&w,&h,&clipped_stride,&channels));
            OK(quantapdf_bitmap_data(small,&other,&other_count));
            CHECK(w==16 && h==12 && clipped_stride==48 && channels==3 &&
                  other_count==16u*12u*3u && other!=nullptr);
            CHECK(probe_rgb(other,clipped_stride,6,5,
                0u,mode==1?255u:0u,mode==1?0u:255u));
            CHECK(probe_rgb(other,clipped_stride,6,8,
                mode==1?255u:0u,mode==1?255u:0u,mode==1?0u:255u));
            CHECK(probe_rgb(other,clipped_stride,9,5,0u,0u,255u));
            CHECK(probe_rgb(other,clipped_stride,5,5,255u,255u,255u));
        }
        quantapdf_drop_page(clipped_page);
        quantapdf_close(doc);
        quantapdf_drop_context(ctx);
        if(filled!=nullptr) {
            const unsigned char *detached=nullptr;
            size_t n=0u;
            OK(quantapdf_bitmap_data(filled,&detached,&n));
            CHECK(detached!=nullptr && n==32u*24u*3u &&
                  probe_rgb(detached,96,18,11,0u,0u,255u));
        }
        quantapdf_drop_bitmap(small);
        quantapdf_drop_bitmap(filled);
    }

    std::puts("[native-public-render] same-Clip PATH+IMAGE / clipped IMAGE / mixed PATH+IMAGE / IMAGE-only / PATH-only / blank, native refusal passed");
    return EXIT_SUCCESS;
}
