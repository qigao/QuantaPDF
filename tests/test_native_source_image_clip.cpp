/* Original Image XObject Do under a PDF W/W* Clip Path, parsed in FULL
 * by the private strict source interpreter. The unchanged ORIGINAL PDF
 * is rendered by PDFium directly as a test-only independent oracle.
 * Native C11 raster receives uniquely-owned IR after qpdf source disposal. */
#include "backend/qpdf_native_image_ir.h"
#include "render/ir_raster_image_clip_v1.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <fpdfview.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"%s:%d: original Image Clip: %s\n", \
        __FILE__,__LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

namespace {
using Obj=QPDFObjectHandle;
constexpr size_t W=32u,H=24u;
const quantapdf_qpdf_source_raw_image_limits source_limits={
    1048576u,16384u,3u,2u,4096u,4096u,4096u,3u
};
const quantapdf_ir_raster_image_v1_limits image_limits={
    4096u,2u,4096u,4096u
};
const quantapdf_ir_raster_v1_limits clip_limits={
    4096u,1u,96u,300000u
};

struct fixture {
    std::string a,b;
    bool split=false,wrong_subtype=false,missing_resources=false;
    bool mask=false,invalid_color=false;
};

std::vector<unsigned char> original_pdf(const fixture& sc)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    Obj pages=pdf->getRoot().getKey("/Pages");
    Obj page=pdf->makeIndirectObject(
        Obj::parse("<< /Type /Page /MediaBox [0 0 32 24] >>"));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",pages);

    const std::string raw("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
    Obj img=pdf->newStream(raw);
    img.getDict().replaceKey("/Type",Obj::newName("/XObject"));
    img.getDict().replaceKey("/Subtype",
        Obj::newName(sc.wrong_subtype?"/Form":"/Image"));
    img.getDict().replaceKey("/Width",Obj::newInteger(2));
    img.getDict().replaceKey("/Height",Obj::newInteger(2));
    img.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    img.getDict().replaceKey("/ColorSpace",
        Obj::newName(sc.invalid_color?"/DeviceCMYK":"/DeviceRGB"));
    if(sc.mask)img.getDict().replaceKey("/SMask",
        Obj::newName("/None"));
    Obj resources=Obj::newDictionary(),objects=Obj::newDictionary();
    objects.replaceKey("/I",img);
    resources.replaceKey("/XObject",objects);
    if(!sc.missing_resources)page.replaceKey("/Resources",resources);
    if(sc.split)
        page.replaceKey("/Contents",Obj::newArray({
            pdf->newStream(sc.a),pdf->newStream(sc.b)}));
    else
        page.replaceKey("/Contents",pdf->newStream(sc.a+sc.b));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    auto bytes=writer.getBufferSharedPointer();
    return {bytes->getBuffer(),bytes->getBuffer()+bytes->getSize()};
}

std::vector<unsigned char> pdfium_original_rgb(
    const std::vector<unsigned char>& original)
{
    FPDF_InitLibrary();
    FPDF_DOCUMENT doc=FPDF_LoadMemDocument(
        original.data(),static_cast<int>(original.size()),nullptr);
    if(!doc)return {};
    FPDF_PAGE page=FPDF_LoadPage(doc,0);
    if(!page) {FPDF_CloseDocument(doc);return {};}
    FPDF_BITMAP bitmap=FPDFBitmap_CreateEx(
        static_cast<int>(W),static_cast<int>(H),FPDFBitmap_BGR,nullptr,0);
    if(!bitmap) {FPDF_ClosePage(page);FPDF_CloseDocument(doc);return {};}
    FPDFBitmap_FillRect(bitmap,0,0,(int)W,(int)H,0xffffffffu);
    FPDF_RenderPageBitmap(bitmap,page,0,0,(int)W,(int)H,0,0);
    const auto *buffer=static_cast<const unsigned char *>(
        FPDFBitmap_GetBuffer(bitmap));
    const int stride=FPDFBitmap_GetStride(bitmap);
    std::vector<unsigned char> rgb(W*H*3u);
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const unsigned char *px=buffer+y*(size_t)stride+x*3u;
        unsigned char *dst=rgb.data()+(y*W+x)*3u;
        dst[0]=px[2];dst[1]=px[1];dst[2]=px[0];
    }
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();
    return rgb;
}

bool rgb_match(const unsigned char *rgb,unsigned r,unsigned g,unsigned b,
               unsigned tolerance)
{
    return std::abs((int)rgb[0]-(int)r)<=(int)tolerance &&
           std::abs((int)rgb[1]-(int)g)<=(int)tolerance &&
           std::abs((int)rgb[2]-(int)b)<=(int)tolerance;
}

int positive(const fixture& source,quantapdf_composer_fill_rule rule,
             bool complete_colors)
{
    auto original=original_pdf(source);
    CHECK(original.size()>100u);
    const auto direct=pdfium_original_rgb(original);
    CHECK(direct.size()==W*H*3u);
    quantapdf_ir ir{};
    OK(quantapdf_qpdf_source_clipped_raw_image_to_ir_v1(
        original.data(),original.size(),&source_limits,&ir));
    CHECK(ir.page_count==1u && ir.operation_count==1u &&
          ir.resources.image_count==1u && ir.resources.clip_count==1u &&
          ir.resources.graphics_state_count==1u);
    CHECK(ir.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
          ir.operations[0].graphics_state_id==1u &&
          ir.operations[0].value.image.options.graphics_state_id==1u);
    CHECK(ir.resources.graphics_states[0].clip_id==1u &&
          ir.resources.clips[0].kind==
              QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
          ir.resources.clips[0].fill_rule==rule &&
          ir.resources.clips[0].command_count==5u);
    /* Source original bytes and qpdf temporary owners cannot be consumed
     * or retained by this private pure-C11 image raster. */
    original.clear();
    original.shrink_to_fit();
    quantapdf_ir_raster_image native{},again{};
    OK(quantapdf_ir_raster_clipped_raw_image_v1(
        &ir,0u,&image_limits,&clip_limits,&native));
    OK(quantapdf_ir_raster_clipped_raw_image_v1(
        &ir,0u,&image_limits,&clip_limits,&again));
    CHECK(native.width==W && native.height==H &&
          native.stride==W*4u && native.size==W*H*4u &&
          again.size==native.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages==nullptr && ir.resources.images==nullptr &&
          ir.resources.clips==nullptr);

    double diff=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const unsigned char *p=native.pixels+y*native.stride+x*4u;
        const unsigned char *q=direct.data()+(y*W+x)*3u;
        unsigned md=0u;
        for(size_t c=0u;c<3u;++c) {
            const unsigned actual=(unsigned)p[c];
            const unsigned ref=(unsigned)q[c];
            const unsigned delta=actual>ref?actual-ref:ref-actual;
            if(delta>md)md=delta;
            diff+=(double)delta;
        }
        if(md>64u)++gross;
    }
    CHECK(diff/(double)(W*H*3u)<=8.0 && gross<=W*H/12u);
    struct probe {size_t x,y;unsigned r,g,b;};
    const probe white[]={{1u,1u,255u,255u,255u},
                         {9u,11u,255u,255u,255u},
                         {24u,12u,255u,255u,255u}};
    for(const auto& v:white) {
        const unsigned char *p=native.pixels+v.y*native.stride+v.x*4u;
        const unsigned char *q=direct.data()+(v.y*W+v.x)*3u;
        CHECK(rgb_match(p,v.r,v.g,v.b,3u));
        CHECK(rgb_match(q,v.r,v.g,v.b,3u));
    }
    if(complete_colors) {
        const probe colored[]={{13u,10u,255u,0u,0u},
                               {19u,10u,0u,255u,0u},
                               {13u,16u,0u,0u,255u},
                               {19u,16u,255u,255u,0u}};
        for(const auto& v:colored) {
            const unsigned char *p=native.pixels+
                v.y*native.stride+v.x*4u;
            const unsigned char *q=direct.data()+(v.y*W+v.x)*3u;
            CHECK(rgb_match(p,v.r,v.g,v.b,3u));
            CHECK(rgb_match(q,v.r,v.g,v.b,3u));
        }
    }
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&again);
    return EXIT_SUCCESS;
}

int negative(const fixture& src,quantapdf_status expected)
{
    auto original=original_pdf(src);
    for(int trial=0;trial<2;++trial) {
        quantapdf_ir refused;
        std::memset(&refused,0xa5,sizeof(refused));
        auto status=quantapdf_qpdf_source_clipped_raw_image_to_ir_v1(
            original.data(),original.size(),&source_limits,&refused);
        if(status!=expected)
            std::fprintf(stderr,"clip-source expected=%d actual=%d trial=%d\n",
                (int)expected,(int)status,trial);
        CHECK(status==expected);
        CHECK(refused.pages==nullptr && refused.page_count==0u &&
              refused.operations==nullptr && refused.operation_count==0u &&
              refused.resources.images==nullptr &&
              refused.resources.graphics_states==nullptr &&
              refused.resources.clips==nullptr);
        quantapdf_ir_dispose(&refused);
    }
    return EXIT_SUCCESS;
}

} // namespace

int main()
{
    fixture basic{};
    basic.a="q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q";
    if(positive(basic,QUANTAPDF_COMPOSER_FILL_NONZERO,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    fixture evenodd{};
    evenodd.split=true;
    evenodd.a="q 12 6 8 9 re W*";
    evenodd.b=" n q 16 0 0 12 8 3 cm /I Do Q Q";
    if(positive(evenodd,QUANTAPDF_COMPOSER_FILL_EVEN_ODD,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    fixture definition{};
    definition.a=
        "q 1 0 0 1 2 0 cm 10 6 8 9 re W n "
        "q 16 0 0 12 6 3 cm /I Do Q Q";
    if(positive(definition,QUANTAPDF_COMPOSER_FILL_NONZERO,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    const std::string valid="q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q";
    const fixture unsupported[]={
        {valid+" BT ET"},
        {valid+" 1 0 0 rg"},
        {valid+" 10 10 2 2 re f"},
        {"q 12 6 8 9 re W n Q q 16 0 0 12 8 3 cm /I Do Q"},
        {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do /I Do Q Q"},
        {"q W n q 16 0 0 12 8 3 cm /I Do Q Q"},
        {"q 12 6 8 9 re W n q 1 1 0 1 8 3 cm /I Do Q Q"},
        {"q 12 6 8 9 re W n q 16 0 0 12 8.5 3 cm /I Do Q Q"}
    };
    for(size_t i=0u;i<sizeof(unsupported)/sizeof(unsupported[0]);++i)
        if(negative(unsupported[i],QUANTAPDF_ERROR_UNSUPPORTED)!=EXIT_SUCCESS)
            return EXIT_FAILURE;
    fixture unknown{};
    unknown.a="q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /Unknown Do Q Q";
    if(negative(unknown,QUANTAPDF_ERROR_FORMAT)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture wrong{};
    wrong.a=valid;
    wrong.wrong_subtype=true;
    if(negative(wrong,QUANTAPDF_ERROR_UNSUPPORTED)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture soft{};
    soft.a=valid;
    soft.mask=true;
    if(negative(soft,QUANTAPDF_ERROR_UNSUPPORTED)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture cmyk{};
    cmyk.a=valid;
    cmyk.invalid_color=true;
    if(negative(cmyk,QUANTAPDF_ERROR_UNSUPPORTED)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture missing{};
    missing.a=valid;
    missing.missing_resources=true;
    if(negative(missing,QUANTAPDF_ERROR_FORMAT)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    /* Nonterminated W / partial q scope remain malformed rather than
     * permitting a partially committed Image from an earlier operand. */
    fixture unterminated{};
    unterminated.a="q 12 6 8 9 re W";
    if(negative(unterminated,QUANTAPDF_ERROR_FORMAT)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    if(positive(basic,QUANTAPDF_COMPOSER_FILL_NONZERO,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    std::puts("Original Image Do under W/W*: independent PDFium RGB, "
              "detached C11 Clip/Image pixels and atomic negatives PASS");
    return EXIT_SUCCESS;
}
