/* Original PDF source: complete Path + Image Do operator admission.
 * QPDF authors and parses ORIGINAL bytes in tests. PDFium independently
 * renders those unchanged source bytes as a test-only oracle, never a
 * re-exported Composer IR. The real mixed painter remains pure C11. */
#include "backend/qpdf_native_image_ir.h"
#include "render/ir_raster_mixed_v1.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(e) do { if(!(e)) { \
    std::fprintf(stderr,"%s:%d: source mixed Path/Image: %s\n", \
        __FILE__,__LINE__,#e); return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

namespace {
using Obj=QPDFObjectHandle;
constexpr int W=32,H=24;
const quantapdf_qpdf_source_raw_image_limits source_limits={
    1048576u,16384u,3u,2u,4096u,4096u,4096u,3u
};
const quantapdf_ir_raster_mixed_v1_limits raster_limits={
    4096u,128u,400000u,4096u,4096u
};

struct scene {
    std::string first,second;
    bool split=false;
    bool missing_image=false;
    bool image_is_form=false;
    bool interpolate=false;
    bool default_rgb=false;
};

std::vector<unsigned char> make_original(const scene& sc)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    Obj pages=pdf->getRoot().getKey("/Pages");
    Obj page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24] >>"));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",pages);

    Obj resources=Obj::newDictionary(), xobjects=Obj::newDictionary();
    Obj blue=pdf->newStream(std::string("\0\0\xff",3));
    blue.getDict().replaceKey("/Type",Obj::newName("/XObject"));
    blue.getDict().replaceKey("/Subtype",
        Obj::newName(sc.image_is_form?"/Form":"/Image"));
    blue.getDict().replaceKey("/Width",Obj::newInteger(1));
    blue.getDict().replaceKey("/Height",Obj::newInteger(1));
    blue.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    blue.getDict().replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
    if(sc.interpolate)
        blue.getDict().replaceKey("/Interpolate",Obj::newBool(true));
    xobjects.replaceKey("/I",blue);
    resources.replaceKey("/XObject",xobjects);
    if(sc.default_rgb)
        resources.replaceKey("/ColorSpace",
            Obj::parse("<< /DefaultRGB /DeviceGray >>"));
    if(!sc.missing_image)
        page.replaceKey("/Resources",resources);
    if(sc.split)
        page.replaceKey("/Contents",Obj::newArray({
            pdf->newStream(sc.first),pdf->newStream(sc.second)}));
    else
        page.replaceKey("/Contents",pdf->newStream(sc.first+sc.second));

    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    auto bytes=writer.getBufferSharedPointer();
    return {bytes->getBuffer(),bytes->getBuffer()+bytes->getSize()};
}

std::vector<unsigned char> original_pdfium(
    const std::vector<unsigned char>& bytes)
{
    FPDF_InitLibrary();
    FPDF_DOCUMENT document=FPDF_LoadMemDocument(
        bytes.data(),static_cast<int>(bytes.size()),nullptr);
    if(!document)return {};
    FPDF_PAGE page=FPDF_LoadPage(document,0);
    if(!page){FPDF_CloseDocument(document);return {};}
    FPDF_BITMAP bitmap=FPDFBitmap_CreateEx(W,H,FPDFBitmap_BGR,nullptr,0);
    if(!bitmap){FPDF_ClosePage(page);FPDF_CloseDocument(document);return {};}
    FPDFBitmap_FillRect(bitmap,0,0,W,H,0xffffffffu);
    FPDF_RenderPageBitmap(bitmap,page,0,0,W,H,0,0);
    const auto *bgra=static_cast<const unsigned char *>(
        FPDFBitmap_GetBuffer(bitmap));
    const int stride=FPDFBitmap_GetStride(bitmap);
    std::vector<unsigned char> rgb(static_cast<size_t>(W*H*3));
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){
        const auto *bgr=bgra+static_cast<size_t>(y)*stride+
            static_cast<size_t>(x)*3u;
        auto *dest=rgb.data()+static_cast<size_t>(y*W+x)*3u;
        dest[0]=bgr[2];dest[1]=bgr[1];dest[2]=bgr[0];
    }
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(document);
    FPDF_DestroyLibrary();
    return rgb;
}

const char *const path_rect="q 1 0 0 rg 12 5 14 14 re f Q ";
const char *const path_poly=
    "q 1 0 0 rg 12 5 m 26 5 l 26 19 l 12 19 l h f Q ";
const char *const image_cm="q 16 0 0 12 8 6 cm /I Do Q ";

int positive(bool image_first,bool polygon,bool split,bool path_ctm)
{
    scene sc;
    const std::string path=path_ctm ?
        "q 1 0 0 rg 1 0 0 1 2 1 cm 10 4 14 14 re f Q " :
        (polygon ? path_poly:path_rect);
    const std::string image=image_cm;
    if(image_first)sc.first=image,sc.second=path;
    else sc.first=path,sc.second=image;
    sc.split=split;
    if(split && !image_first && !path_ctm) {
        /* Image Do operator and operand cross /Contents boundary. */
        sc.first=path+"q 16 0 0 12 8 6 cm /I";
        sc.second=" Do Q ";
    }
    auto original=make_original(sc);
    auto kept=original;
    CHECK(!original.empty());

    quantapdf_ir detached{};
    OK(quantapdf_qpdf_source_mixed_path_image_to_ir(
        original.data(),original.size(),&source_limits,&detached));
    CHECK(detached.page_count==1u && detached.operation_count==2u &&
          detached.resources.image_count==1u &&
          detached.resources.images!=nullptr &&
          detached.resources.images[0].format==QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW);
    CHECK(detached.operations[0].kind==(image_first ?
        QUANTAPDF_COMPOSER_OPERATION_IMAGE :
        QUANTAPDF_COMPOSER_OPERATION_PATH));
    CHECK(detached.operations[1].kind==(image_first ?
        QUANTAPDF_COMPOSER_OPERATION_PATH :
        QUANTAPDF_COMPOSER_OPERATION_IMAGE));

    /* The prior Image-only interpreter must STILL refuse a page with a
     * path, including when the Image Do is otherwise fully supported. */
    quantapdf_ir image_only{};
    CHECK(quantapdf_qpdf_source_raw_images_to_ir(
        original.data(),original.size(),&source_limits,&image_only)==
        QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image_only.pages==nullptr && image_only.operations==nullptr &&
          image_only.resources.images==nullptr);

    std::fill(original.begin(),original.end(),0u);
    quantapdf_ir_raster_image native{},repeated{};
    OK(quantapdf_ir_raster_mixed_path_image_v1(
        &detached,0u,&raster_limits,&native));
    OK(quantapdf_ir_raster_mixed_path_image_v1(
        &detached,0u,&raster_limits,&repeated));
    CHECK(native.pixels!=nullptr && native.width==(size_t)W &&
          native.height==(size_t)H && native.size==(size_t)W*H*4u &&
          native.stride==(size_t)W*4u &&
          native.size==repeated.size &&
          std::memcmp(native.pixels,repeated.pixels,native.size)==0);
    quantapdf_ir_dispose(&detached);

    auto reference=original_pdfium(kept);
    CHECK(reference.size()==static_cast<size_t>(W*H*3));
    struct probe{int x,y;unsigned r,g,b;};
    const probe expected[]={
        {2,2,255,255,255}, /* outside both */
        {9,10,0,0,255},    /* image only */
        {25,10,255,0,0},  /* path only */
        {15,10,image_first?255u:0u,0u,image_first?0u:255u},
        {30,22,255,255,255}
    };
    for(const auto& v:expected){
        const unsigned char *pdf=reference.data()+
            static_cast<size_t>(v.y*W+v.x)*3u;
        const unsigned char *raster=native.pixels+
            static_cast<size_t>(v.y)*native.stride+
            static_cast<size_t>(v.x)*4u;
        for(int c=0;c<3;++c){
            const unsigned want=c==0?v.r:(c==1?v.g:v.b);
            CHECK(std::abs(int(pdf[c])-int(want))<=3);
            CHECK(std::abs(int(raster[c])-int(want))<=3);
        }
    }
    double mae=0.0;
    size_t gross=0u;
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){
        const auto *pdf=reference.data()+static_cast<size_t>(y*W+x)*3u;
        const auto *raster=native.pixels+static_cast<size_t>(y)*native.stride+
            static_cast<size_t>(x)*4u;
        unsigned max_delta=0;
        for(int c=0;c<3;++c){
            unsigned delta=static_cast<unsigned>(
                std::abs(int(pdf[c])-int(raster[c])));
            mae+=delta;
            if(delta>max_delta)max_delta=delta;
        }
        if(max_delta>64u)++gross;
    }
    mae/=double(W*H*3);
    std::fprintf(stderr,
        "[source-mixed] image_first=%d polygon=%d split=%d path_ctm=%d "
        "mae=%.4f gross=%zu\n",
        image_first,polygon,split,path_ctm,mae,gross);
    CHECK(mae<=10.0 && gross<=W*H/12);

    quantapdf_ir recovered{};
    OK(quantapdf_qpdf_source_mixed_path_image_to_ir(
        kept.data(),kept.size(),&source_limits,&recovered));
    quantapdf_ir_raster_image last{};
    OK(quantapdf_ir_raster_mixed_path_image_v1(
        &recovered,0u,&raster_limits,&last));
    CHECK(last.size==native.size &&
          std::memcmp(last.pixels,native.pixels,native.size)==0);
    quantapdf_ir_dispose(&recovered);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&repeated);
    quantapdf_ir_raster_image_dispose(&last);
    return EXIT_SUCCESS;
}

int negatives()
{
    struct bad{const char *id,*tail;quantapdf_status expected;};
    const bad cases[]={
        {"late_text","BT ET",QUANTAPDF_ERROR_UNSUPPORTED},
        {"late_stroke","3 3 m 7 3 l S",QUANTAPDF_ERROR_UNSUPPORTED},
        {"late_clip","1 1 3 3 re W n",QUANTAPDF_ERROR_UNSUPPORTED},
        {"late_image","q 2 0 0 2 4 4 cm /I Do Q",QUANTAPDF_ERROR_UNSUPPORTED},
        {"late_font","/F1 8 Tf",QUANTAPDF_ERROR_UNSUPPORTED},
        {"late_group","/Ext gs",QUANTAPDF_ERROR_UNSUPPORTED},
        {"unknown_name","/Missing Do",QUANTAPDF_ERROR_FORMAT},
        {"incomplete_path","1 1 m",QUANTAPDF_ERROR_FORMAT},
        {"fractional_color","0.5 0 0 rg",QUANTAPDF_ERROR_UNSUPPORTED},
        {"unsupported_gray","0 g",QUANTAPDF_ERROR_UNSUPPORTED},
        {"late_form","/Fm Do",QUANTAPDF_ERROR_FORMAT}
    };
    for(const auto& tc:cases){
        scene sc;
        sc.first=std::string(path_rect)+image_cm+tc.tail;
        auto original=make_original(sc);
        quantapdf_ir ir;
        std::memset(&ir,0xab,sizeof(ir));
        const quantapdf_status status=
            quantapdf_qpdf_source_mixed_path_image_to_ir(
                original.data(),original.size(),&source_limits,&ir);
        if(status!=tc.expected)
            std::fprintf(stderr,
                "[source-mixed] negative %s expected=%d actual=%d\n",
                tc.id,int(tc.expected),int(status));
        CHECK(status==tc.expected);
        CHECK(ir.pages==nullptr && ir.page_count==0u &&
              ir.operations==nullptr && ir.operation_count==0u &&
              ir.resources.images==nullptr && ir.resources.image_count==0u);
        quantapdf_ir_dispose(&ir);
    }
    {
        scene sc;
        sc.first=std::string(path_rect)+image_cm;
        sc.interpolate=true;
        auto pdf=make_original(sc);
        quantapdf_ir ir{};
        CHECK(quantapdf_qpdf_source_mixed_path_image_to_ir(
            pdf.data(),pdf.size(),&source_limits,&ir)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(ir.operations==nullptr && ir.resources.images==nullptr);
        sc.interpolate=false;
        sc.default_rgb=true;
        pdf=make_original(sc);
        CHECK(quantapdf_qpdf_source_mixed_path_image_to_ir(
            pdf.data(),pdf.size(),&source_limits,&ir)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        sc.default_rgb=false;
        sc.image_is_form=true;
        pdf=make_original(sc);
        CHECK(quantapdf_qpdf_source_mixed_path_image_to_ir(
            pdf.data(),pdf.size(),&source_limits,&ir)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        sc.image_is_form=false;
        sc.missing_image=true;
        pdf=make_original(sc);
        CHECK(quantapdf_qpdf_source_mixed_path_image_to_ir(
            pdf.data(),pdf.size(),&source_limits,&ir)==
            QUANTAPDF_ERROR_FORMAT);
        sc.missing_image=false;
        pdf=make_original(sc);
        auto limits=source_limits;
        limits.max_operations=1u;
        CHECK(quantapdf_qpdf_source_mixed_path_image_to_ir(
            pdf.data(),pdf.size(),&limits,&ir)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        limits=source_limits;
        limits.max_pdf_bytes=32u;
        CHECK(quantapdf_qpdf_source_mixed_path_image_to_ir(
            pdf.data(),pdf.size(),&limits,&ir)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(ir.operations==nullptr && ir.resources.images==nullptr);
    }
    return EXIT_SUCCESS;
}
} // namespace

int main()
{
    if(positive(false,false,true,false)!=EXIT_SUCCESS ||
       positive(true,true,false,false)!=EXIT_SUCCESS ||
       positive(false,false,false,true)!=EXIT_SUCCESS ||
       negatives()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    std::puts("[source-mixed] original Path/Image Do source-order "
              "PDFium/native pixel & refusal PASS");
    return EXIT_SUCCESS;
}
