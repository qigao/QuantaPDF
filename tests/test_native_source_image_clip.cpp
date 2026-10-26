/* Original Image XObject Do under a PDF W/W* Clip Path, parsed in FULL
 * by the private strict source interpreter. The unchanged ORIGINAL PDF
 * is rendered by PDFium directly as a test-only independent oracle.
 * Native C11 raster receives uniquely-owned IR after qpdf source disposal. */
#include "backend/qpdf_native_image_ir.h"
#include "render/ir_raster_image_clip_v1.h"
#include "render/ir_raster_image_clip_v2.h"
#include "render/ir_raster_image_clip_v3.h"
#include "render/ir_raster_image_clip_path_v4.h"
#include "render/ir_raster_scoped_mixed_v5.h"

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

/* Original PDF V2: TWO Image Do draws, both under the SAME frozen W/W*
 * source Clip. The second Image may overlap the first, but must not leave
 * the Clip scope; the single-Do V1 API must reject this source. */
int positive_two(const fixture& source,quantapdf_composer_fill_rule rule)
{
    auto original=original_pdf(source);
    CHECK(original.size()>100u);
    const auto direct=pdfium_original_rgb(original);
    CHECK(direct.size()==W*H*3u);
    quantapdf_ir old_api{};
    CHECK(quantapdf_qpdf_source_clipped_raw_image_to_ir_v1(
        original.data(),original.size(),&source_limits,&old_api)
        ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(old_api.pages==nullptr && old_api.operations==nullptr &&
          old_api.resources.images==nullptr);

    quantapdf_ir ir{};
    OK(quantapdf_qpdf_source_two_clipped_raw_images_to_ir_v2(
        original.data(),original.size(),&source_limits,&ir));
    CHECK(ir.page_count==1u && ir.operation_count==2u &&
          ir.resources.image_count==2u && ir.resources.clip_count==1u &&
          ir.resources.graphics_state_count==1u);
    for(size_t i=0u;i<2u;++i) {
        CHECK(ir.operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
              ir.operations[i].graphics_state_id==1u &&
              ir.operations[i].value.image.options.graphics_state_id==1u &&
              ir.operations[i].value.image.image_id==i+1u &&
              ir.resources.images[i].format==QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW);
    }
    CHECK(ir.resources.graphics_states[0].clip_id==1u &&
          ir.resources.clips[0].kind==
              QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
          ir.resources.clips[0].fill_rule==rule &&
          ir.resources.clips[0].command_count==5u);
    original.clear();
    original.shrink_to_fit();
    quantapdf_ir_raster_image native{},again{};
    quantapdf_ir_raster_image_v1_limits img=image_limits;
    quantapdf_ir_raster_v1_limits mask=clip_limits;
    img.max_operations=2u;
    mask.max_operations=2u;
    OK(quantapdf_ir_raster_two_clipped_raw_images_v2(
        &ir,0u,&img,&mask,&native));
    OK(quantapdf_ir_raster_two_clipped_raw_images_v2(
        &ir,0u,&img,&mask,&again));
    CHECK(native.width==W && native.height==H &&
          native.size==W*H*4u && native.stride==W*4u &&
          native.size==again.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages==nullptr && ir.operations==nullptr &&
          ir.resources.images==nullptr && ir.resources.clips==nullptr);

    double diff=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const unsigned char *p=native.pixels+y*native.stride+x*4u;
        const unsigned char *q=direct.data()+(y*W+x)*3u;
        unsigned max_diff=0u;
        for(size_t c=0u;c<3u;++c) {
            const unsigned actual=(unsigned)p[c],expected=(unsigned)q[c];
            const unsigned delta=actual>expected?
                actual-expected:expected-actual;
            if(delta>max_diff)max_diff=delta;
            diff+=(double)delta;
        }
        if(max_diff>64u)++gross;
    }
    CHECK(diff/(double)(W*H*3u)<=8.0 && gross<=W*H/12u);
    struct probe {size_t x,y;unsigned r,g,b;};
    const probe expected[]={
        {1u,1u,255u,255u,255u}, {9u,11u,255u,255u,255u},
        {24u,12u,255u,255u,255u},
        {13u,10u,255u,0u,0u}, {19u,10u,0u,255u,0u},
        {13u,12u,255u,0u,0u}, {19u,12u,0u,255u,0u},
        {13u,16u,0u,0u,255u}, {19u,16u,255u,255u,0u},
        {13u,18u,255u,255u,255u}
    };
    for(const auto& v:expected) {
        const unsigned char *p=native.pixels+
            v.y*native.stride+v.x*4u;
        const unsigned char *q=direct.data()+(v.y*W+v.x)*3u;
        CHECK(rgb_match(p,v.r,v.g,v.b,3u));
        CHECK(rgb_match(q,v.r,v.g,v.b,3u));
    }
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&again);
    return EXIT_SUCCESS;
}

/* Original-source opt-in V4: an Image Do under a frozen W/W* leaf,
 * a genuine Q restore, then an independent unclipped red PATH paint.
 * Oracle loads the unchanged original PDF; Native only borrows copied IR. */
int positive_image_then_path(const fixture& source,
                             quantapdf_composer_fill_rule rule)
{
    auto original=original_pdf(source);
    CHECK(original.size()>100u);
    const auto direct=pdfium_original_rgb(original);
    CHECK(direct.size()==W*H*3u);
    quantapdf_ir old{};
    CHECK(quantapdf_qpdf_source_restored_clipped_raw_images_to_ir_v3(
        original.data(),original.size(),&source_limits,&old)
        ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(old.pages==nullptr && old.operations==nullptr);
    quantapdf_ir ir{};
    OK(quantapdf_qpdf_source_clipped_image_then_path_to_ir_v4(
        original.data(),original.size(),&source_limits,&ir));
    CHECK(ir.page_count==1u && ir.operation_count==2u &&
          ir.resources.image_count==1u &&
          ir.resources.graphics_state_count==1u &&
          ir.resources.clip_count==1u);
    CHECK(ir.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
          ir.operations[1].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
          ir.operations[0].graphics_state_id==1u &&
          ir.operations[0].value.image.options.graphics_state_id==1u &&
          ir.operations[0].value.image.image_id==1u &&
          ir.operations[1].graphics_state_id==0u &&
          ir.operations[1].value.path.options.fill==1 &&
          ir.operations[1].value.path.options.stroke==0 &&
          ir.operations[1].value.path.options.fill_argb==UINT32_C(0xffff0000) &&
          ir.resources.graphics_states[0].clip_id==1u &&
          ir.resources.clips[0].fill_rule==rule &&
          ir.resources.clips[0].command_count==5u);
    original.clear();original.shrink_to_fit();
    auto img=image_limits;
    auto edge=clip_limits;
    img.max_operations=2u;
    edge.max_operations=2u;
    quantapdf_ir_raster_image native{},again{},general{};
    OK(quantapdf_ir_raster_clipped_image_then_path_v4(
        &ir,0u,&img,&edge,&native));
    OK(quantapdf_ir_raster_clipped_image_then_path_v4(
        &ir,0u,&img,&edge,&again));
    /* Both paths consume the same original-PDF-sourced, owned IR: a new
     * generic V5 executor must reproduce the established strict V4 output. */
    const quantapdf_ir_raster_scoped_mixed_v5_limits generic={
        img.max_pixels,4u,edge.max_edges,edge.max_sample_tests,
        img.max_source_pixels,img.max_drawn_pixels
    };
    OK(quantapdf_ir_raster_scoped_mixed_v5(
        &ir,0u,&generic,&general));
    CHECK(native.width==W && native.height==H &&
          native.stride==W*4u && native.size==W*H*4u &&
          native.size==again.size && native.size==general.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0 &&
          std::memcmp(native.pixels,general.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&general);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages==nullptr && ir.operations==nullptr &&
          ir.resources.images==nullptr && ir.resources.clips==nullptr);
    double diff=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const auto *p=native.pixels+y*native.stride+x*4u;
        const auto *q=direct.data()+(y*W+x)*3u;
        unsigned worst=0u;
        for(size_t c=0u;c<3u;++c) {
            const unsigned lhs=(unsigned)p[c],rhs=(unsigned)q[c];
            const unsigned delta=lhs>rhs?lhs-rhs:rhs-lhs;
            if(delta>worst)worst=delta;
            diff+=(double)delta;
        }
        if(worst>64u)++gross;
    }
    CHECK(diff/(double)(W*H*3u)<=8.0 && gross<=W*H/12u);
    struct probe {size_t x,y;unsigned r,g,b;};
    const probe pixels[]={
        {1u,1u,255u,255u,255u},
        {13u,10u,255u,0u,0u},
        {19u,10u,0u,255u,0u},
        {13u,16u,0u,0u,255u},
        {19u,16u,255u,255u,0u},
        /* Red PATH is OUTSIDE the old Clip and must paint after Q. */
        {23u,12u,255u,0u,0u},
        {27u,15u,255u,0u,0u},
        {22u,10u,255u,255u,255u},
        {27u,19u,255u,255u,255u}
    };
    for(const auto& v:pixels) {
        const auto *p=native.pixels+v.y*native.stride+v.x*4u;
        const auto *q=direct.data()+(v.y*W+v.x)*3u;
        CHECK(rgb_match(p,v.r,v.g,v.b,3u));
        CHECK(rgb_match(q,v.r,v.g,v.b,3u));
    }
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&again);
    return EXIT_SUCCESS;
}
int negative_image_then_path(const fixture& source,
                             quantapdf_status expected,
                             const quantapdf_qpdf_source_raw_image_limits& budget)
{
    const auto original=original_pdf(source);
    for(int attempt=0;attempt!=2;++attempt) {
        quantapdf_ir ir;
        std::memset(&ir,0xa5,sizeof(ir));
        const auto actual=quantapdf_qpdf_source_clipped_image_then_path_to_ir_v4(
            original.data(),original.size(),&budget,&ir);
        if(actual!=expected)
            std::fprintf(stderr,
                "Image/Q/Path V4 expected=%d actual=%d attempt=%d\n",
                (int)expected,(int)actual,attempt);
        CHECK(actual==expected);
        CHECK(ir.pages==nullptr && ir.page_count==0u &&
              ir.operations==nullptr && ir.operation_count==0u &&
              ir.resources.images==nullptr &&
              ir.resources.graphics_states==nullptr &&
              ir.resources.clips==nullptr);
        quantapdf_ir_dispose(&ir);
    }
    return EXIT_SUCCESS;
}

/* V3: first Image under the frozen Clip, Q restores its previous scope,
 * second original Do must be visible OUTSIDE that Clip. V1/V2 source
 * signatures must still reject the expanded q/Q semantics. */
int positive_restored(const fixture& source,
                      quantapdf_composer_fill_rule rule)
{
    auto original=original_pdf(source);
    CHECK(original.size()>100u);
    const auto direct=pdfium_original_rgb(original);
    CHECK(direct.size()==W*H*3u);
    quantapdf_ir old_api{};
    CHECK(quantapdf_qpdf_source_two_clipped_raw_images_to_ir_v2(
        original.data(),original.size(),&source_limits,&old_api)
        ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(old_api.pages==nullptr && old_api.operations==nullptr);
    quantapdf_ir ir{};
    OK(quantapdf_qpdf_source_restored_clipped_raw_images_to_ir_v3(
        original.data(),original.size(),&source_limits,&ir));
    CHECK(ir.page_count==1u && ir.operation_count==2u &&
          ir.resources.image_count==2u && ir.resources.clip_count==1u &&
          ir.resources.graphics_state_count==1u);
    CHECK(ir.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
          ir.operations[1].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
          ir.operations[0].graphics_state_id==1u &&
          ir.operations[1].graphics_state_id==0u &&
          ir.operations[0].value.image.options.graphics_state_id==1u &&
          ir.operations[1].value.image.options.graphics_state_id==0u &&
          ir.operations[0].value.image.image_id==1u &&
          ir.operations[1].value.image.image_id==2u);
    CHECK(ir.resources.graphics_states[0].clip_id==1u &&
          ir.resources.clips[0].kind==
              QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
          ir.resources.clips[0].fill_rule==rule &&
          ir.resources.clips[0].command_count==5u);
    original.clear();original.shrink_to_fit();

    quantapdf_ir_raster_image native{},again{};
    quantapdf_ir_raster_image_v1_limits img=image_limits;
    quantapdf_ir_raster_v1_limits mask=clip_limits;
    img.max_operations=2u;
    mask.max_operations=2u;
    OK(quantapdf_ir_raster_restored_raw_images_v3(
        &ir,0u,&img,&mask,&native));
    OK(quantapdf_ir_raster_restored_raw_images_v3(
        &ir,0u,&img,&mask,&again));
    CHECK(native.width==W && native.height==H &&
          native.stride==W*4u && native.size==W*H*4u &&
          native.size==again.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages==nullptr && ir.operations==nullptr &&
          ir.resources.images==nullptr && ir.resources.clips==nullptr);

    double diff=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const auto *p=native.pixels+y*native.stride+x*4u;
        const auto *q=direct.data()+(y*W+x)*3u;
        unsigned worst=0u;
        for(size_t c=0u;c<3u;++c) {
            const unsigned n=(unsigned)p[c],d=(unsigned)q[c];
            const unsigned delta=n>d?n-d:d-n;
            if(delta>worst)worst=delta;
            diff+=(double)delta;
        }
        if(worst>64u)++gross;
    }
    CHECK(diff/(double)(W*H*3u)<=8.0 && gross<=W*H/12u);
    struct probe {size_t x,y;unsigned r,g,b;};
    const probe pixels[]={
        {1u,1u,255u,255u,255u},
        {9u,11u,255u,255u,255u},
        {21u,12u,255u,255u,255u},
        {13u,10u,255u,0u,0u}, {19u,10u,0u,255u,0u},
        {13u,16u,0u,0u,255u}, {19u,16u,255u,255u,0u},
        /* These pixels are outside the Clip, and MUST appear after Q. */
        {23u,12u,255u,0u,0u}, {27u,12u,0u,255u,0u},
        {23u,15u,0u,0u,255u}, {27u,15u,255u,255u,0u}
    };
    for(const auto& v:pixels) {
        const auto *p=native.pixels+v.y*native.stride+v.x*4u;
        const auto *q=direct.data()+(v.y*W+v.x)*3u;
        CHECK(rgb_match(p,v.r,v.g,v.b,3u));
        CHECK(rgb_match(q,v.r,v.g,v.b,3u));
    }
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&again);
    return EXIT_SUCCESS;
}

int negative_two(const fixture& source,quantapdf_status expected,
                 const quantapdf_qpdf_source_raw_image_limits& bounds)
{
    auto original=original_pdf(source);
    for(int trial=0;trial<2;++trial) {
        quantapdf_ir refused;
        std::memset(&refused,0xa5,sizeof(refused));
        const auto status=quantapdf_qpdf_source_two_clipped_raw_images_to_ir_v2(
            original.data(),original.size(),&bounds,&refused);
        if(status!=expected)
            std::fprintf(stderr,
                "two Do source expected=%d actual=%d trial=%d\n",
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

int negative_restored(const fixture& source,quantapdf_status expected,
                      const quantapdf_qpdf_source_raw_image_limits& bounds)
{
    auto original=original_pdf(source);
    for(int trial=0;trial<2;++trial) {
        quantapdf_ir refused;
        std::memset(&refused,0xa5,sizeof(refused));
        const auto status=
            quantapdf_qpdf_source_restored_clipped_raw_images_to_ir_v3(
                original.data(),original.size(),&bounds,&refused);
        if(status!=expected)
            std::fprintf(stderr,
                "Q-restored source expected=%d actual=%d trial=%d\n",
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


/* Source Scoped V5 exercises the REAL original q/Q grammar rather than a
 * manually populated mixed IR. One Path and one Image Do are painted while
 * W/W* is active, followed by an actual Q pop and one of each unclipped. */
int positive_scoped_mixed_v5(const fixture& source,
                             quantapdf_composer_fill_rule rule,
                             bool path_first_inside, bool path_first_outside)
{
    auto bytes=original_pdf(source);
    CHECK(bytes.size()>100u);
    const auto oracle=pdfium_original_rgb(bytes);
    CHECK(oracle.size()==W*H*3u);
    auto admission_limits=source_limits;
    admission_limits.max_operations=4u;
    quantapdf_ir old{};
    CHECK(quantapdf_qpdf_source_clipped_image_then_path_to_ir_v4(
        bytes.data(),bytes.size(),&admission_limits,&old)
        ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(old.pages==nullptr && old.operations==nullptr &&
          old.resources.images==nullptr);
    quantapdf_ir ir{};
    OK(quantapdf_qpdf_source_scoped_mixed_v5_to_ir(
        bytes.data(),bytes.size(),&admission_limits,&ir));
    CHECK(ir.page_count==1u && ir.operation_count==4u &&
          ir.resources.image_count==2u &&
          ir.resources.graphics_state_count==1u &&
          ir.resources.clip_count==1u &&
          ir.resources.graphics_states[0].clip_id==1u &&
          ir.resources.clips[0].fill_rule==rule);
    for(size_t i=0u;i<4u;++i) {
        const size_t expected=i<2u?1u:0u;
        CHECK(ir.operations[i].graphics_state_id==expected);
        const int image=(i<2u) ? (path_first_inside ? i==1u : i==0u) :
            (path_first_outside ? i==3u : i==2u);
        CHECK(ir.operations[i].kind ==
            (image ? QUANTAPDF_COMPOSER_OPERATION_IMAGE :
                     QUANTAPDF_COMPOSER_OPERATION_PATH));
        if(image)
            CHECK(ir.operations[i].value.image.options.graphics_state_id ==
                  expected);
        else
            CHECK(ir.operations[i].value.path.options.graphics_state_id ==
                  expected);
    }
    bytes.clear();bytes.shrink_to_fit(); /* qpdf owner no longer exists. */
    const quantapdf_ir_raster_scoped_mixed_v5_limits budgets={
        W*H,4u,512u,8000000u,16u,4096u
    };
    quantapdf_ir_raster_image first{},repeat{};
    OK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&budgets,&first));
    OK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&budgets,&repeat));
    CHECK(first.pixels!=nullptr && first.width==W && first.height==H &&
          first.size==W*H*4u && first.stride==W*4u &&
          repeat.size==first.size &&
          std::memcmp(first.pixels,repeat.pixels,first.size)==0);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages==nullptr && ir.resources.images==nullptr &&
          ir.resources.clips==nullptr && ir.operations==nullptr);

    double total=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const unsigned char *a=first.pixels+y*first.stride+x*4u;
        const unsigned char *b=oracle.data()+(y*W+x)*3u;
        unsigned max_error=0u;
        for(size_t c=0u;c<3u;++c) {
            const unsigned lhs=a[c],rhs=b[c];
            const unsigned delta=lhs>rhs?lhs-rhs:rhs-lhs;
            if(delta>max_error)max_error=delta;
            total+=(double)delta;
        }
        if(max_error>64u)++gross;
    }
    CHECK(total/(double)(W*H*3u)<=8.0 && gross<=W*H/12u);
    struct probe {size_t x,y;unsigned r,g,b;};
    const probe expected[]={
        {1u,1u,255u,255u,255u},
        {13u,11u,255u,0u,0u},       /* clipped original top-left red */
        {18u,11u,0u,255u,0u},       /* clipped original top-right green */
        {13u,16u,path_first_inside ? 0u : 255u,0u,255u},
                                        /* blue Image or magenta PATH */
        {18u,16u,255u,255u,0u},     /* clipped original bottom-right */
        {23u,3u,255u,0u,0u},        /* post-Q unclipped image red */
        {28u,3u,0u,255u,0u},        /* post-Q unclipped green */
        {23u,6u,0u,0u,255u},
        {28u,6u,255u,path_first_outside ? 255u : 0u,0u},
                                        /* yellow Image or red PATH */
        {26u,5u,255u,path_first_outside ? 255u : 0u,0u},
        {2u,20u,255u,255u,255u},  /* independent unpainted page */
        {9u,12u,255u,255u,255u},   /* outside frozen W Clip */
        {23u,12u,255u,255u,255u}
    };
    for(const auto& v:expected) {
        const auto *a=first.pixels+v.y*first.stride+v.x*4u;
        const auto *b=oracle.data()+(v.y*W+v.x)*3u;
        CHECK(rgb_match(a,v.r,v.g,v.b,3u));
        CHECK(rgb_match(b,v.r,v.g,v.b,3u));
    }
    quantapdf_ir_raster_image_dispose(&first);
    quantapdf_ir_raster_image_dispose(&repeat);
    return EXIT_SUCCESS;
}

int negative_scoped_mixed_v5(
    const fixture& source, quantapdf_status expected,
    const quantapdf_qpdf_source_raw_image_limits& limits)
{
    const auto bytes=original_pdf(source);
    for(int repeat=0;repeat<2;++repeat) {
        quantapdf_ir rejected;
        std::memset(&rejected,0xa5,sizeof(rejected));
        const quantapdf_status actual=
            quantapdf_qpdf_source_scoped_mixed_v5_to_ir(
                bytes.data(),bytes.size(),&limits,&rejected);
        if(actual!=expected)
            std::fprintf(stderr,
                "Source Scoped Mixed V5 expected=%d actual=%d trial=%d\n",
                (int)expected,(int)actual,repeat);
        CHECK(actual==expected);
        CHECK(rejected.pages==nullptr && rejected.page_count==0u &&
              rejected.operations==nullptr && rejected.operation_count==0u &&
              rejected.resources.images==nullptr &&
              rejected.resources.image_count==0u &&
              rejected.resources.graphics_states==nullptr &&
              rejected.resources.clips==nullptr);
        quantapdf_ir_dispose(&rejected);
    }
    return EXIT_SUCCESS;
}

int source_scoped_mixed_v5_suite()
{
    auto budgets=source_limits;
    budgets.max_operations=4u;
    const std::string outer="q 12 6 8 9 re W n ";
    const std::string inner_image="q 16 0 0 12 8 3 cm /I Do Q ";
    /* Magenta PATH over blue source image tests source z-order. */
    const std::string inner_path="1 0 1 rg 12 6 4 4 re f ";
    const std::string outer_image="q 8 0 0 6 22 16 cm /I Do Q ";
    /* Red PATH overlaps the Q-restored second Image; order must matter. */
    const std::string outer_path="1 0 0 rg 24 17 5 3 re f ";
    for(int which=0;which<4;++which) {
        const bool path_first_inside=(which&1)!=0;
        const bool path_first_outside=(which&2)!=0;
        fixture source{};
        source.a=outer +
            (path_first_inside ? inner_path+inner_image :
                                 inner_image+inner_path) +
            "Q " +
            (path_first_outside ? outer_path+outer_image :
                                  outer_image+outer_path);
        if(positive_scoped_mixed_v5(
            source,QUANTAPDF_COMPOSER_FILL_NONZERO,
            path_first_inside,path_first_outside)!=EXIT_SUCCESS)
            return EXIT_FAILURE;
    }
    fixture evenodd{};
    evenodd.split=true;
    evenodd.a="q 12 6 8 9 re W*";
    evenodd.b=" n "+inner_image+inner_path+
              "Q "+outer_image+outer_path;
    if(positive_scoped_mixed_v5(evenodd,
        QUANTAPDF_COMPOSER_FILL_EVEN_ODD,false,false)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture definition{};
    /* Rebase BOTH Image and Path authored coordinates by -2 so Clip,
     * paint and independently observed PDFium pixels stay identical,
     * while validating definition-time CTM ownership. */
    definition.a=
        "q 1 0 0 1 2 0 cm 10 6 8 9 re W n "
        "q 16 0 0 12 6 3 cm /I Do Q "
        "1 0 1 rg 10 6 4 4 re f "
        "Q "+outer_image+outer_path;
    if(positive_scoped_mixed_v5(
        definition,QUANTAPDF_COMPOSER_FILL_NONZERO,false,false)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    const std::string valid=outer+inner_image+inner_path+
                            "Q "+outer_image+outer_path;
    const fixture unsupported[]={
        {valid+" BT ET"},                   /* late source text */
        {valid+" /I Do"},                   /* fifth paint */
        {outer+inner_image+inner_path+outer_image+outer_path+"Q"},
        {outer+"Q "+inner_image+inner_path+outer_image+outer_path},
        {outer+inner_image+"Q "+inner_path+outer_image+outer_path},
        {outer+inner_image+inner_path+"Q "+
            outer_image+"q 2 2 2 2 re S Q"}, /* unsupported Stroke */
        {outer+"q 16 1 0 12 8 3 cm /I Do Q "+
            inner_path+"Q "+outer_image+outer_path},
        {outer+inner_image+inner_path+"Q "+
            "q 8 0 0 6 22.5 16 cm /I Do Q "+outer_path}
    };
    for(const auto& bad:unsupported)
        if(negative_scoped_mixed_v5(
            bad,QUANTAPDF_ERROR_UNSUPPORTED,budgets)!=EXIT_SUCCESS)
            return EXIT_FAILURE;
    fixture missing{};
    missing.a=valid;missing.missing_resources=true;
    if(negative_scoped_mixed_v5(missing,QUANTAPDF_ERROR_FORMAT,budgets)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture wrong{};
    wrong.a=valid;wrong.wrong_subtype=true;
    if(negative_scoped_mixed_v5(wrong,QUANTAPDF_ERROR_UNSUPPORTED,budgets)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture masked{};
    masked.a=valid;masked.mask=true;
    if(negative_scoped_mixed_v5(masked,QUANTAPDF_ERROR_UNSUPPORTED,budgets)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    auto limited=budgets;
    limited.max_operations=3u;
    if(negative_scoped_mixed_v5(
        {valid},QUANTAPDF_ERROR_UNSUPPORTED,limited)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    limited=budgets;
    limited.max_source_pixels=4u;
    if(negative_scoped_mixed_v5(
        {valid},QUANTAPDF_ERROR_UNSUPPORTED,limited)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    limited=budgets;
    limited.max_drawn_pixels=239u;
    if(negative_scoped_mixed_v5(
        {valid},QUANTAPDF_ERROR_UNSUPPORTED,limited)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture recovery{};
    recovery.a=valid;
    if(positive_scoped_mixed_v5(
        recovery,QUANTAPDF_COMPOSER_FILL_NONZERO,false,false)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
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

    /* Two ordered Image Do operations under one immutable W/W* source Clip. */
    const std::string two =
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q "
        "q 8 0 0 6 12 7 cm /I Do Q Q";
    fixture twin{};
    twin.a=two;
    if(positive_two(twin,QUANTAPDF_COMPOSER_FILL_NONZERO)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture twin_evenodd{};
    twin_evenodd.split=true;
    twin_evenodd.a="q 12 6 8 9 re W*";
    twin_evenodd.b=
        " n q 16 0 0 12 8 3 cm /I Do Q "
        "q 8 0 0 6 12 7 cm /I Do Q Q";
    if(positive_two(twin_evenodd,QUANTAPDF_COMPOSER_FILL_EVEN_ODD)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture twin_definition{};
    twin_definition.a=
        "q 1 0 0 1 2 0 cm 10 6 8 9 re W n "
        "q 16 0 0 12 6 3 cm /I Do Q "
        "q 8 0 0 6 10 7 cm /I Do Q Q";
    if(positive_two(twin_definition,QUANTAPDF_COMPOSER_FILL_NONZERO)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    const std::string first=
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q ";
    const fixture invalid[]={
        {first+"Q q 8 0 0 6 12 7 cm /I Do Q"},
        {first+"q 8 0 0 6 12 7 cm /I Do /I Do Q Q"},
        {two+" BT ET"},
        {two+" 1 0 0 rg"},
        {two+" 5 5 3 3 re f"},
        {first+"q 8 1 0 6 12 7 cm /I Do Q Q"},
        {first+"q 8 0 0 6 12.5 7 cm /I Do Q Q"},
        {first+"q 8 0 0 6 12 7 cm /Unknown Do Q Q"},
        {first+"Q"}
    };
    for(size_t i=0u;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        const auto status=i==7u ? QUANTAPDF_ERROR_FORMAT :
            (i==8u ? QUANTAPDF_ERROR_UNSUPPORTED : QUANTAPDF_ERROR_UNSUPPORTED);
        if(negative_two(invalid[i],status,source_limits)!=EXIT_SUCCESS)
            return EXIT_FAILURE;
    }
    fixture lone{};
    lone.a=first+"Q";
    if(negative_two(lone,QUANTAPDF_ERROR_UNSUPPORTED,source_limits)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture missing_two{};
    missing_two.a=two;missing_two.missing_resources=true;
    if(negative_two(missing_two,QUANTAPDF_ERROR_FORMAT,source_limits)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture masked_two{};
    masked_two.a=two;masked_two.mask=true;
    if(negative_two(masked_two,QUANTAPDF_ERROR_UNSUPPORTED,source_limits)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    quantapdf_qpdf_source_raw_image_limits too_small=source_limits;
    too_small.max_source_pixels=4u; /* First 2x2 accepted, second fails. */
    if(negative_two(twin,QUANTAPDF_ERROR_UNSUPPORTED,too_small)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    too_small=source_limits;
    too_small.max_drawn_pixels=192u; /* First draw admitted, second over cap. */
    if(negative_two(twin,QUANTAPDF_ERROR_UNSUPPORTED,too_small)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    too_small=source_limits;
    too_small.max_operations=1u;
    if(negative_two(twin,QUANTAPDF_ERROR_UNSUPPORTED,too_small)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_two(twin,QUANTAPDF_COMPOSER_FILL_NONZERO)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    /* True q/Q restoration: first Image under W/W*, second Image wholly
     * outside the frozen Clip but visible after pop. Old V2 rejects it. */
    fixture restored{};
    restored.a=
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q Q "
        "q 8 0 0 6 22 7 cm /I Do Q";
    if(positive_restored(restored,QUANTAPDF_COMPOSER_FILL_NONZERO)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture restored_evenodd{};
    restored_evenodd.split=true;
    restored_evenodd.a="q 12 6 8 9 re W*";
    restored_evenodd.b=
        " n q 16 0 0 12 8 3 cm /I Do Q Q "
        "q 8 0 0 6 22 7 cm /I Do Q";
    if(positive_restored(restored_evenodd,QUANTAPDF_COMPOSER_FILL_EVEN_ODD)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture restored_definition{};
    restored_definition.a=
        "q 1 0 0 1 2 0 cm 10 6 8 9 re W n "
        "q 16 0 0 12 6 3 cm /I Do Q Q "
        "q 8 0 0 6 22 7 cm /I Do Q";
    if(positive_restored(restored_definition,QUANTAPDF_COMPOSER_FILL_NONZERO)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    /* V3 must reject the old both-clipped V2 source, Q pops before
     * the first Do, a missing Q before the second Do and any later
     * unqualified page operation or malformed source operand. */
    fixture invalid_restored[]={
        {two},
        {"q 12 6 8 9 re W n Q q 16 0 0 12 8 3 cm /I Do Q "
         "q 8 0 0 6 22 7 cm /I Do Q"},
        {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
         "q 8 0 0 6 22 7 cm /I Do Q Q"},
        {restored.a+" BT ET"},
        {restored.a+" 1 0 0 rg"},
        {restored.a+" q 3 3 2 2 re f Q"},
        {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q "
         "q 8 1 0 6 22 7 cm /I Do Q"},
        {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q "
         "q 8 0 0 6 22.5 7 cm /I Do Q"},
        {restored.a+" q 1 0 0 1 0 0 cm /I Do Q"}
    };
    for(const auto& sample:invalid_restored)
        if(negative_restored(sample,QUANTAPDF_ERROR_UNSUPPORTED,source_limits)
           !=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture missing_restored{};
    missing_restored.a=restored.a;
    missing_restored.missing_resources=true;
    if(negative_restored(missing_restored,QUANTAPDF_ERROR_FORMAT,
                         source_limits)!=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture masked_restored{};
    masked_restored.a=restored.a;masked_restored.mask=true;
    if(negative_restored(masked_restored,QUANTAPDF_ERROR_UNSUPPORTED,
                         source_limits)!=EXIT_SUCCESS)return EXIT_FAILURE;
    quantapdf_qpdf_source_raw_image_limits restricted=source_limits;
    restricted.max_source_pixels=4u;
    if(negative_restored(restored,QUANTAPDF_ERROR_UNSUPPORTED,restricted)
       !=EXIT_SUCCESS)return EXIT_FAILURE;
    restricted=source_limits;
    restricted.max_drawn_pixels=192u;
    if(negative_restored(restored,QUANTAPDF_ERROR_UNSUPPORTED,restricted)
       !=EXIT_SUCCESS)return EXIT_FAILURE;
    restricted=source_limits;
    restricted.max_operations=1u;
    if(negative_restored(restored,QUANTAPDF_ERROR_UNSUPPORTED,restricted)
       !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_restored(restored,QUANTAPDF_COMPOSER_FILL_NONZERO)
       !=EXIT_SUCCESS)return EXIT_FAILURE;

    /* V4: Image Do under W/W*, Q restores, then one red PATH fill paints
     * outside the old Clip. Complete-content source rules remain strict. */
    fixture restored_path{};
    restored_path.a=
        "q 12 6 8 9 re W n "
        "q 16 0 0 12 8 3 cm /I Do Q Q "
        "1 0 0 rg 22 7 8 6 re f";
    if(positive_image_then_path(restored_path,QUANTAPDF_COMPOSER_FILL_NONZERO)
       !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture evenodd_path{};
    evenodd_path.split=true;
    evenodd_path.a="q 12 6 8 9 re W*";
    evenodd_path.b=
        " n q 16 0 0 12 8 3 cm /I Do Q Q "
        "1 0 0 rg 22 7 8 6 re f";
    if(positive_image_then_path(evenodd_path,QUANTAPDF_COMPOSER_FILL_EVEN_ODD)
       !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture defined_path{};
    defined_path.a=
        "q 1 0 0 1 2 0 cm 10 6 8 9 re W n "
        "q 16 0 0 12 6 3 cm /I Do Q Q "
        "1 0 0 rg 22 7 8 6 re f";
    if(positive_image_then_path(defined_path,QUANTAPDF_COMPOSER_FILL_NONZERO)
       !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture invalid_path[]={
        {restored_path.a+" BT ET"},
        {restored_path.a+" q 2 2 m 3 3 l S Q"},
        {restored_path.a+" q 1 0 0 1 0 0 cm /I Do Q"},
        {"q 12 6 8 9 re W n Q "
         "q 16 0 0 12 8 3 cm /I Do Q "
         "1 0 0 rg 22 7 8 6 re f"},
        {"q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
         "1 0 0 rg 22 7 8 6 re f Q"},
        {"q 12 6 8 9 re W n "
         "1 0 0 rg 22 7 8 6 re f "
         "q 16 0 0 12 8 3 cm /I Do Q Q"},
        {"q 12 6 8 9 re W n "
         "q 16 0 0 12 8 3 cm /I Do Q Q "
         "1 0 0 rg 22 7 8 6 re f "
         "q 16 0 0 12 8 3 cm /I Do Q"},
        {"q 12 6 8 9 re W n "
         "q 16 1 0 12 8 3 cm /I Do Q Q "
         "1 0 0 rg 22 7 8 6 re f"}
    };
    for(const auto& bad:invalid_path)
        if(negative_image_then_path(bad,QUANTAPDF_ERROR_UNSUPPORTED,
                                    source_limits)!=EXIT_SUCCESS)
            return EXIT_FAILURE;
    fixture missing_path{};
    missing_path.a=restored_path.a;missing_path.missing_resources=true;
    if(negative_image_then_path(missing_path,QUANTAPDF_ERROR_FORMAT,
                                source_limits)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture mask_path{};
    mask_path.a=restored_path.a;mask_path.mask=true;
    if(negative_image_then_path(mask_path,QUANTAPDF_ERROR_UNSUPPORTED,
                                source_limits)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    auto limited=source_limits;
    limited.max_source_pixels=3u;
    if(negative_image_then_path(restored_path,QUANTAPDF_ERROR_UNSUPPORTED,
                                limited)!=EXIT_SUCCESS)return EXIT_FAILURE;
    limited=source_limits;
    limited.max_drawn_pixels=100u;
    if(negative_image_then_path(restored_path,QUANTAPDF_ERROR_UNSUPPORTED,
                                limited)!=EXIT_SUCCESS)return EXIT_FAILURE;
    limited=source_limits;
    limited.max_operations=1u;
    if(negative_image_then_path(restored_path,QUANTAPDF_ERROR_UNSUPPORTED,
                                limited)!=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_image_then_path(restored_path,QUANTAPDF_COMPOSER_FILL_NONZERO)
       !=EXIT_SUCCESS)return EXIT_FAILURE;

    if(source_scoped_mixed_v5_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    std::puts("Original Image Do under W/W*: independent PDFium RGB, "
              "detached C11 Clip/Image pixels and atomic negatives PASS");
    return EXIT_SUCCESS;
}
