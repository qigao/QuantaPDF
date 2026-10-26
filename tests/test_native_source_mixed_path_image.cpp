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


/* V2 original-source qualification: one red PATH, two real Image XObject Do
 * paints, strict content operator admission, full owned-IR rendering order.
 * QPDF authors the source bytes only. The independent PDFium oracle renders
 * those ORIGINAL bytes, never a Composer-exported approximation. */
struct three_scene {
    std::string commands;
    bool split_streams=false;
    bool missing_second=false;
    bool form_second=false;
    bool interpolate_second=false;
};

std::vector<unsigned char> make_three_original(const three_scene& sc)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    Obj pages=pdf->getRoot().getKey("/Pages");
    Obj page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24] >>"));
    pages.replaceKey("/Kids",Obj::newArray({page}));
    pages.replaceKey("/Count",Obj::newInteger(1));
    page.replaceKey("/Parent",pages);
    auto image=[&](const std::string& payload,bool form) {
        Obj im=pdf->newStream(payload);
        Obj dict=im.getDict();
        dict.replaceKey("/Type",Obj::newName("/XObject"));
        dict.replaceKey("/Subtype",Obj::newName(form?"/Form":"/Image"));
        dict.replaceKey("/Width",Obj::newInteger(1));
        dict.replaceKey("/Height",Obj::newInteger(1));
        dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
        dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
        return im;
    };
    Obj resources=Obj::newDictionary();
    Obj xobjects=Obj::newDictionary();
    xobjects.replaceKey("/I",image(std::string("\0\0\xff",3),false));
    if(!sc.missing_second) {
        Obj green=image(std::string("\0\xff\0",3),sc.form_second);
        if(sc.interpolate_second)
            green.getDict().replaceKey("/Interpolate",Obj::newBool(true));
        xobjects.replaceKey("/J",green);
    }
    resources.replaceKey("/XObject",xobjects);
    page.replaceKey("/Resources",resources);
    if(sc.split_streams) {
        const size_t index=sc.commands.find("/J Do");
        if(index==std::string::npos)return {};
        const size_t boundary=index+2u; /* '/J' | ' Do' */
        page.replaceKey("/Contents",Obj::newArray({
            pdf->newStream(sc.commands.substr(0,boundary)),
            pdf->newStream(sc.commands.substr(boundary))
        }));
    } else
        page.replaceKey("/Contents",pdf->newStream(sc.commands));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();writer.write();
    auto bytes=writer.getBufferSharedPointer();
    return {bytes->getBuffer(),bytes->getBuffer()+bytes->getSize()};
}

const char *const green_cm="q 10 0 0 10 18 7 cm /J Do Q ";

int positive_three(int ordering,bool split)
{
    three_scene sc;
    if(ordering==0)
        sc.commands=std::string(path_rect)+image_cm+green_cm;
    else if(ordering==1)
        sc.commands=std::string(image_cm)+path_rect+green_cm;
    else
        sc.commands=std::string(image_cm)+green_cm+path_rect;
    sc.split_streams=split;
    auto original=make_three_original(sc);
    CHECK(!original.empty());
    const auto kept=original;
    auto source=source_limits;
    source.max_operations=3u;
    quantapdf_ir ir{};
    OK(quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2(
        original.data(),original.size(),&source,&ir));
    CHECK(ir.page_count==1u && ir.operation_count==3u &&
          ir.resources.image_count==2u && ir.resources.images!=nullptr &&
          ir.operations!=nullptr);
    size_t paths=0u,images=0u;
    for(size_t i=0u;i<ir.operation_count;++i) {
        if(ir.operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_PATH)
            ++paths;
        else if(ir.operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            ++images;
            CHECK(ir.operations[i].value.image.image_id==images);
        } else CHECK(false);
    }
    CHECK(paths==1u && images==2u);
    CHECK(ir.operations[ordering==0?0u:ordering==1?1u:2u].kind==
          QUANTAPDF_COMPOSER_OPERATION_PATH);

    /* Earlier independently accepted APIs cannot silently broaden their
     * source profile when a third operation is present. */
    quantapdf_ir refused{};
    CHECK(quantapdf_qpdf_source_mixed_path_image_to_ir(
        kept.data(),kept.size(),&source,&refused)==
        QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(refused.pages==nullptr && refused.operations==nullptr &&
          refused.resources.images==nullptr);
    CHECK(quantapdf_qpdf_source_raw_images_to_ir(
        kept.data(),kept.size(),&source,&refused)==
        QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(refused.pages==nullptr && refused.operations==nullptr);

    std::fill(original.begin(),original.end(),0u);
    quantapdf_ir_raster_image native{},repeat{};
    OK(quantapdf_ir_raster_mixed_path_two_images_v2(
        &ir,0u,&raster_limits,&native));
    OK(quantapdf_ir_raster_mixed_path_two_images_v2(
        &ir,0u,&raster_limits,&repeat));
    CHECK(native.pixels!=nullptr && native.width==(size_t)W &&
          native.height==(size_t)H && native.size==(size_t)W*H*4u &&
          repeat.size==native.size &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_dispose(&ir); /* Independently owned native RGBA output. */
    const auto pdfium=original_pdfium(kept);
    CHECK(pdfium.size()==(size_t)(W*H*3));
    struct pixel{int x,y;unsigned r,g,b;};
    const pixel probes[]={
        {2,2,255,255,255},    /* untouched page */
        {9,10,0,0,255},       /* first Image only */
        {13,5,255,0,0},      /* Path only */
        {15,10,ordering==0?0u:255u,0u,ordering==0?255u:0u},
        {20,10,ordering==2?255u:0u,ordering==2?0u:255u,0u},
        {26,10,0,255,0},     /* second Image only */
        {30,22,255,255,255}
    };
    for(const auto& p:probes) {
        const size_t offset=(size_t)(p.y*W+p.x);
        const auto *ref=pdfium.data()+offset*3u;
        const auto *got=native.pixels+(size_t)p.y*native.stride+(size_t)p.x*4u;
        const unsigned expected[]={p.r,p.g,p.b};
        for(int c=0;c<3;++c) {
            CHECK(std::abs(int(ref[c])-int(expected[c]))<=3);
            CHECK(std::abs(int(got[c])-int(expected[c]))<=3);
        }
    }
    double error=0.0;
    size_t gross=0u;
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        const size_t xy=(size_t)(y*W+x);
        unsigned max_error=0u;
        for(int c=0;c<3;++c) {
            const unsigned delta=(unsigned)std::abs(
                int(pdfium[xy*3u+(size_t)c])-
                int(native.pixels[(size_t)y*native.stride+(size_t)x*4u+
                                  (size_t)c]));
            error+=delta;
            if(delta>max_error)max_error=delta;
        }
        if(max_error>64u)++gross;
    }
    error/=(double)(W*H*3);
    std::fprintf(stderr,"[source-mixed-v2] order=%d split=%d mae=%.4f "
        "gross=%zu\n",ordering,split,error,gross);
    CHECK(error<=10.0 && gross<=(size_t)(W*H/12));

    quantapdf_ir anew{};
    OK(quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2(
        kept.data(),kept.size(),&source,&anew));
    quantapdf_ir_raster_image later{};
    OK(quantapdf_ir_raster_mixed_path_two_images_v2(
        &anew,0u,&raster_limits,&later));
    CHECK(later.size==native.size &&
          std::memcmp(later.pixels,native.pixels,native.size)==0);
    quantapdf_ir_dispose(&anew);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&later);
    return EXIT_SUCCESS;
}

int negative_three()
{
    auto source=source_limits;
    source.max_operations=3u;
    const std::string complete=std::string(path_rect)+image_cm+green_cm;
    struct invalid {
        const char *name;
        std::string source;
        quantapdf_status expected;
        bool missing_j=false,form_j=false,interpolate=false;
    };
    const invalid cases[]={
        {"late_text",complete+"BT ET",QUANTAPDF_ERROR_UNSUPPORTED},
        {"late_clip",complete+"1 1 3 3 re W n",QUANTAPDF_ERROR_UNSUPPORTED},
        {"fourth_image",complete+green_cm,QUANTAPDF_ERROR_UNSUPPORTED},
        {"fourth_path",complete+path_rect,QUANTAPDF_ERROR_UNSUPPORTED},
        {"late_stroke",complete+"1 1 m 4 4 l S",QUANTAPDF_ERROR_UNSUPPORTED},
        {"unknown_image",complete,QUANTAPDF_ERROR_FORMAT,true},
        {"form_image",complete,QUANTAPDF_ERROR_UNSUPPORTED,false,true},
        {"interpolated",complete,QUANTAPDF_ERROR_UNSUPPORTED,false,false,true}
    };
    for(const auto& tc:cases) {
        three_scene sc;
        sc.commands=tc.source;
        sc.missing_second=tc.missing_j;
        sc.form_second=tc.form_j;
        sc.interpolate_second=tc.interpolate;
        auto original=make_three_original(sc);
        CHECK(!original.empty());
        quantapdf_ir ir;
        std::memset(&ir,0xab,sizeof(ir));
        const auto status=quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2(
            original.data(),original.size(),&source,&ir);
        if(status!=tc.expected)
            std::fprintf(stderr,"[source-mixed-v2] negative=%s expected=%d "
                "got=%d\n",tc.name,(int)tc.expected,(int)status);
        CHECK(status==tc.expected);
        CHECK(ir.pages==nullptr && ir.page_count==0u &&
              ir.operations==nullptr && ir.operation_count==0u &&
              ir.resources.images==nullptr &&
              ir.resources.image_count==0u);
        quantapdf_ir_dispose(&ir);
    }
    {
        three_scene sc;sc.commands=complete;
        const auto original=make_three_original(sc);
        quantapdf_ir ir{};
        auto too_small=source;
        too_small.max_operations=2u;
        CHECK(quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2(
            original.data(),original.size(),&too_small,&ir)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        too_small=source;
        too_small.max_source_pixels=1u;
        CHECK(quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2(
            original.data(),original.size(),&too_small,&ir)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(ir.operations==nullptr && ir.resources.images==nullptr);
    }
    return EXIT_SUCCESS;
}

int main()
{
    if(positive(false,false,true,false)!=EXIT_SUCCESS ||
       positive(true,true,false,false)!=EXIT_SUCCESS ||
       positive(false,false,false,true)!=EXIT_SUCCESS ||
       negatives()!=EXIT_SUCCESS ||
       positive_three(0,true)!=EXIT_SUCCESS ||
       positive_three(1,false)!=EXIT_SUCCESS ||
       positive_three(2,false)!=EXIT_SUCCESS ||
       negative_three()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    std::puts("[source-mixed] original Path/Image Do source-order "
              "PDFium/native pixel & refusal PASS");
    return EXIT_SUCCESS;
}
