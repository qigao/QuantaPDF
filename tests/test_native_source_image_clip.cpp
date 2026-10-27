/* Original Image XObject Do under a PDF W/W* Clip Path, parsed in FULL
 * by the private strict source interpreter. The unchanged ORIGINAL PDF
 * is rendered by PDFium directly as a test-only independent oracle.
 * Native C11 raster receives uniquely-owned IR after qpdf source disposal. */
#include "backend/qpdf_native_image_ir.h"
#include "backend/native_source_raster_bridge_v1.h"
#include "backend/builtin_native_page_v1.h"
#include "backend/builtin_native_bitmap_v1.h"
#include "document/private_native_facade_bitmap_v1.h"
#include "core/context_internal.h"
#include "core/bitmap_internal.h"
#include "backend/pdfium_runtime.h"
#include "backend/document_backend.h"
#include "render/ir_raster_image_clip_v1.h"
#include "render/ir_raster_image_clip_v2.h"
#include "render/ir_raster_image_clip_v3.h"
#include "render/ir_raster_image_clip_path_v4.h"
#include "render/ir_raster_scoped_mixed_v5.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <fpdfview.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

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
    bool gray_image=false;
    std::string image_decode;
    bool second_image=false,second_image_gray=false;
    std::string second_image_decode;
    /* Test-only encoded samples: 0=RAW, otherwise Flate with
     * Predictor 1/2/10..15, independently for the two XObjects. */
    int image_predictor=0,second_image_predictor=0;
    std::string image_decode_parms,second_image_decode_parms;
};

/* Independent test-side encoder, deliberately separate from the
 * QPDF decoder selected by the product Source V10 implementation. */
std::string test_flate(std::string const& raw)
{
    uLongf cap=compressBound(static_cast<uLong>(raw.size()));
    std::string out(static_cast<size_t>(cap),'\0');
    const int status=compress2(
        reinterpret_cast<Bytef *>(out.data()),&cap,
        reinterpret_cast<Bytef const *>(raw.data()),
        static_cast<uLong>(raw.size()),Z_BEST_COMPRESSION);
    if(status!=Z_OK)throw std::runtime_error("test Flate encode");
    out.resize(static_cast<size_t>(cap));
    return out;
}
int test_paeth(int a,int b,int c)
{
    const int p=a+b-c;
    const int da=std::abs(p-a),db=std::abs(p-b),dc=std::abs(p-c);
    if(da<=db && da<=dc)return a;
    return db<=dc ? b : c;
}
std::string test_predictor_encode(std::string const& raw,
                                  size_t colors,int predictor)
{
    if((colors!=1u && colors!=3u) || raw.size()!=4u*colors)
        throw std::runtime_error("test predictor input");
    if(predictor==1)return raw;
    const size_t stride=2u*colors;
    std::string encoded;
    encoded.reserve(raw.size()+(predictor>=10?2u:0u));
    for(size_t row=0;row<2u;++row) {
        const int filter=predictor==15?(row==0u?1:4):
                         (predictor>=10 && predictor<=14?predictor-10:-1);
        if(predictor!=2 && (filter<0 || filter>4))
            throw std::runtime_error("test predictor code");
        if(predictor!=2)
            encoded.push_back(static_cast<char>(filter));
        for(size_t col=0;col<stride;++col) {
            const size_t pos=row*stride+col;
            const int pixel=static_cast<unsigned char>(raw[pos]);
            const int left=col>=colors?
                static_cast<unsigned char>(raw[pos-colors]):0;
            const int up=row!=0u?
                static_cast<unsigned char>(raw[pos-stride]):0;
            const int upper_left=(row!=0u && col>=colors)?
                static_cast<unsigned char>(raw[pos-stride-colors]):0;
            int prediction=0;
            if(predictor==2 || filter==1)prediction=left;
            else if(filter==2)prediction=up;
            else if(filter==3)prediction=(left+up)/2;
            else if(filter==4)prediction=test_paeth(left,up,upper_left);
            encoded.push_back(static_cast<char>(
                static_cast<unsigned char>(pixel-prediction)));
        }
    }
    return encoded;
}
std::string test_predictor_parms(int predictor,int colors,
                                 int columns=2,int bits=8)
{
    return "<< /Predictor "+std::to_string(predictor)+
           " /Colors "+std::to_string(colors)+
           " /Columns "+std::to_string(columns)+
           " /BitsPerComponent "+std::to_string(bits)+" >>";
}

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
    const std::string primary_samples=sc.gray_image ?
        std::string("\x00\x55\xaa\xff",4) : raw;
    const std::string primary_encoded=sc.image_predictor!=0 ?
        test_flate(test_predictor_encode(primary_samples,
            sc.gray_image?1u:3u,sc.image_predictor)) : primary_samples;
    Obj img=pdf->newStream(primary_encoded);
    if(sc.image_predictor!=0)
        img.getDict().replaceKey("/Filter",Obj::newName("/FlateDecode"));
    if(!sc.image_decode_parms.empty())
        img.getDict().replaceKey("/DecodeParms",
            Obj::parse(sc.image_decode_parms));
    img.getDict().replaceKey("/Type",Obj::newName("/XObject"));
    img.getDict().replaceKey("/Subtype",
        Obj::newName(sc.wrong_subtype?"/Form":"/Image"));
    img.getDict().replaceKey("/Width",Obj::newInteger(2));
    img.getDict().replaceKey("/Height",Obj::newInteger(2));
    img.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
    img.getDict().replaceKey("/ColorSpace",
        Obj::newName(sc.invalid_color?"/DeviceCMYK":
                     (sc.gray_image?"/DeviceGray":"/DeviceRGB")));
    if(!sc.image_decode.empty())
        img.getDict().replaceKey("/Decode",Obj::parse(sc.image_decode));
    if(sc.mask)img.getDict().replaceKey("/SMask",
        Obj::newName("/None"));
    Obj resources=Obj::newDictionary(),objects=Obj::newDictionary();
    objects.replaceKey("/I",img);
    /* Optional distinct /J resource: two Do operations are not merely
     * repeated references to one /I image. V9 must normalize each
     * image dictionary's own Decode and preserve its pixel ownership. */
    if(sc.second_image) {
        const std::string second_samples=sc.second_image_gray ?
            std::string("\x00\x55\xaa\xff",4) : raw;
        const std::string second_encoded=sc.second_image_predictor!=0 ?
            test_flate(test_predictor_encode(second_samples,
                sc.second_image_gray?1u:3u,sc.second_image_predictor)) :
            second_samples;
        Obj second=pdf->newStream(second_encoded);
        if(sc.second_image_predictor!=0)
            second.getDict().replaceKey("/Filter",
                Obj::newName("/FlateDecode"));
        if(!sc.second_image_decode_parms.empty())
            second.getDict().replaceKey("/DecodeParms",
                Obj::parse(sc.second_image_decode_parms));
        second.getDict().replaceKey("/Type",Obj::newName("/XObject"));
        second.getDict().replaceKey("/Subtype",Obj::newName("/Image"));
        second.getDict().replaceKey("/Width",Obj::newInteger(2));
        second.getDict().replaceKey("/Height",Obj::newInteger(2));
        second.getDict().replaceKey("/BitsPerComponent",Obj::newInteger(8));
        second.getDict().replaceKey("/ColorSpace",
            Obj::newName(sc.second_image_gray?"/DeviceGray":"/DeviceRGB"));
        if(!sc.second_image_decode.empty())
            second.getDict().replaceKey("/Decode",
                Obj::parse(sc.second_image_decode));
        objects.replaceKey("/J",second);
    }
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
    /* This test also renders through the unchanged production PDFium
     * backend. The test-only direct original oracle must share PDFium's
     * process-wide once-only initializer and lock: calling
     * FPDF_DestroyLibrary independently invalidated the backend's
     * once_flag and caused a second-render SegFault. */
    if(quantapdf_pdfium_enter()!=QUANTAPDF_OK)return {};
    struct runtime_guard {
        ~runtime_guard(){quantapdf_pdfium_leave();}
    } guard;
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


/* V6 is the first original-source variable length scoped admission. Its
 * original PDF is independently painted by PDFium; no Composer re-export
 * participates. The existing C11 V5 consumes the detached owned result. */
struct v6_probe {size_t x,y;unsigned r,g,b;};

int positive_scoped_mixed_v6(
    const fixture& sc,size_t clipped_count,size_t paths,size_t images,
    const std::vector<v6_probe>& goldens,bool compare_legacy_v5=false,
    bool normalized_v7=false,bool gray_v8=false,
    bool image_decode_v9=false,bool predictor_v10=false)
{
    auto original=original_pdf(sc);
    CHECK(original.size()>100u);
    const auto oracle=pdfium_original_rgb(original);
    CHECK(oracle.size()==W*H*3u);
    auto bounded=source_limits;
    bounded.max_operations=4u;
    quantapdf_ir ir{};
    const quantapdf_status accepted=predictor_v10 ?
        quantapdf_qpdf_source_scoped_mixed_v10_flate_predictor_to_ir(
            original.data(),original.size(),&bounded,&ir) :
        (image_decode_v9 ?
            quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir(
                original.data(),original.size(),&bounded,&ir) :
        (gray_v8 ?
            quantapdf_qpdf_source_scoped_mixed_v8_to_ir(
                original.data(),original.size(),&bounded,&ir) :
            (normalized_v7 ?
                quantapdf_qpdf_source_scoped_mixed_v7_to_ir(
                    original.data(),original.size(),&bounded,&ir) :
                quantapdf_qpdf_source_scoped_mixed_v6_to_ir(
                    original.data(),original.size(),&bounded,&ir))));
    OK(accepted);
    if(predictor_v10 &&
       (!sc.image_decode_parms.empty() ||
        !sc.second_image_decode_parms.empty())) {
        quantapdf_ir old;
        std::memset(&old,0xa5,sizeof(old));
        CHECK(quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir(
            original.data(),original.size(),&bounded,&old)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(old.pages==nullptr && old.operations==nullptr &&
              old.resources.images==nullptr);
        quantapdf_ir_dispose(&old);
    }
    if(image_decode_v9 &&
       (!sc.image_decode.empty() || !sc.second_image_decode.empty())) {
        /* Existing V8 MUST still refuse an explicit /Decode even when
         * the V9 Source conversion would yield exactly the same colors. */
        quantapdf_ir old_decode;
        std::memset(&old_decode,0xa5,sizeof(old_decode));
        CHECK(quantapdf_qpdf_source_scoped_mixed_v8_to_ir(
            original.data(),original.size(),&bounded,&old_decode)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(old_decode.pages==nullptr && old_decode.operations==nullptr &&
              old_decode.resources.images==nullptr);
        quantapdf_ir_dispose(&old_decode);
    }
    if(gray_v8) {
        /* V7 has no nonstroking Gray operator; a successful V8 result
         * must never silently widen an already-qualified V7 mode. */
        quantapdf_ir old_gray;
        std::memset(&old_gray,0xa5,sizeof(old_gray));
        CHECK(quantapdf_qpdf_source_scoped_mixed_v7_to_ir(
            original.data(),original.size(),&bounded,&old_gray)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(old_gray.pages==nullptr && old_gray.operations==nullptr &&
              old_gray.resources.images==nullptr);
        quantapdf_ir_dispose(&old_gray);
    }
    if(normalized_v7 || gray_v8) {
        /* Previous strict color modes must never inherit V8 Gray. */
        quantapdf_ir older_color;
        std::memset(&older_color,0xa5,sizeof(older_color));
        CHECK(quantapdf_qpdf_source_scoped_mixed_v6_to_ir(
            original.data(),original.size(),&bounded,&older_color)==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(older_color.pages==nullptr &&
              older_color.operations==nullptr &&
              older_color.resources.images==nullptr);
        quantapdf_ir_dispose(&older_color);
    }
    CHECK(ir.page_count==1u && ir.operation_count==paths+images &&
          ir.operation_count>=2u && ir.operation_count<=4u &&
          ir.resources.image_count==images &&
          ir.resources.graphics_state_count==1u &&
          ir.resources.clip_count==1u &&
          ir.resources.graphics_states[0].clip_id==1u);
    size_t path_seen=0u,image_seen=0u;
    for(size_t i=0u;i<ir.operation_count;++i) {
        const size_t expected_scope=i<clipped_count?1u:0u;
        const auto& op=ir.operations[i];
        CHECK(op.graphics_state_id==expected_scope);
        if(op.kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            ++path_seen;
            CHECK(op.value.path.options.graphics_state_id==expected_scope);
        } else {
            CHECK(op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE);
            ++image_seen;
            CHECK(op.value.image.options.graphics_state_id==expected_scope);
        }
    }
    CHECK(path_seen==paths && image_seen==images);
    /* The V5 admission stays four layers with one of each kind on both
     * sides of Q. Never silently widen that older source signature. */
    quantapdf_ir legacy{};
    const quantapdf_status older=quantapdf_qpdf_source_scoped_mixed_v5_to_ir(
        original.data(),original.size(),&bounded,&legacy);
    CHECK(older==(compare_legacy_v5 ? QUANTAPDF_OK :
                                     QUANTAPDF_ERROR_UNSUPPORTED));
    CHECK(compare_legacy_v5 || (legacy.pages==nullptr &&
          legacy.operations==nullptr && legacy.resources.images==nullptr));

    std::fill(original.begin(),original.end(),0u);
    original.clear();
    const quantapdf_ir_raster_scoped_mixed_v5_limits budgets={
        W*H,4u,512u,8000000u,16u,4096u
    };
    quantapdf_ir_raster_image native{},repeat{},prior{};
    OK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&budgets,&native));
    OK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&budgets,&repeat));
    CHECK(native.width==W && native.height==H &&
          native.stride==W*4u && native.size==W*H*4u &&
          native.size==repeat.size &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    if(compare_legacy_v5) {
        OK(quantapdf_ir_raster_scoped_mixed_v5(&legacy,0u,&budgets,&prior));
        CHECK(prior.size==native.size &&
              std::memcmp(prior.pixels,native.pixels,native.size)==0);
    }
    quantapdf_ir_dispose(&legacy);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages==nullptr && ir.operations==nullptr &&
          ir.resources.images==nullptr && ir.resources.clips==nullptr);

    double total=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=oracle.data()+(y*W+x)*3u;
        unsigned peak=0u;
        for(size_t c=0u;c<3u;++c) {
            const unsigned delta=a[c]>b[c] ? a[c]-b[c] : b[c]-a[c];
            total+=(double)delta;
            if(delta>peak)peak=delta;
        }
        if(peak>64u)++gross;
    }
    CHECK(total/(double)(W*H*3u)<=8.0 && gross<=W*H/12u);
    for(const auto& probe:goldens) {
        CHECK(probe.x<W && probe.y<H);
        const auto *a=native.pixels+probe.y*native.stride+probe.x*4u;
        const auto *b=oracle.data()+(probe.y*W+probe.x)*3u;
        CHECK(rgb_match(a,probe.r,probe.g,probe.b,3u));
        CHECK(rgb_match(b,probe.r,probe.g,probe.b,3u));
    }
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&prior);
    return EXIT_SUCCESS;
}

int negative_scoped_mixed_v6(
    const fixture& sc,quantapdf_status expected,
    const quantapdf_qpdf_source_raw_image_limits& bounds,
    bool normalized_v7=false,bool gray_v8=false,
    bool image_decode_v9=false,bool predictor_v10=false)
{
    const auto bytes=original_pdf(sc);
    for(int attempt=0;attempt<2;++attempt) {
        quantapdf_ir refused;
        std::memset(&refused,0xa5,sizeof(refused));
        const quantapdf_status actual=predictor_v10 ?
            quantapdf_qpdf_source_scoped_mixed_v10_flate_predictor_to_ir(
                bytes.data(),bytes.size(),&bounds,&refused) :
            (image_decode_v9 ?
                quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir(
                    bytes.data(),bytes.size(),&bounds,&refused) :
            (gray_v8 ?
                quantapdf_qpdf_source_scoped_mixed_v8_to_ir(
                    bytes.data(),bytes.size(),&bounds,&refused) :
                (normalized_v7 ?
                    quantapdf_qpdf_source_scoped_mixed_v7_to_ir(
                        bytes.data(),bytes.size(),&bounds,&refused) :
                    quantapdf_qpdf_source_scoped_mixed_v6_to_ir(
                        bytes.data(),bytes.size(),&bounds,&refused))));
        if(actual!=expected)
            std::fprintf(stderr,
                "Scoped Mixed V6/V7/V8/V9 unexpected status trial=%d wanted=%d got=%d\n",
                attempt,(int)expected,(int)actual);
        CHECK(actual==expected);
        CHECK(refused.pages==nullptr && refused.page_count==0u &&
              refused.operations==nullptr && refused.operation_count==0u &&
              refused.resources.images==nullptr &&
              refused.resources.image_count==0u &&
              refused.resources.graphics_states==nullptr &&
              refused.resources.clips==nullptr);
        quantapdf_ir_dispose(&refused);
    }
    return EXIT_SUCCESS;
}

int source_scoped_mixed_v6_suite()
{
    auto bounds=source_limits;
    bounds.max_operations=4u;
    const std::string outer="q 12 6 8 9 re W n ";
    const std::string img_in="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string path_in="1 0 1 rg 12 6 4 4 re f ";
    const std::string img_out="q 8 0 0 6 22 16 cm /I Do Q ";
    const std::string path_out="1 0 0 rg 24 17 5 3 re f ";
    const v6_probe white[]={ {1u,1u,255u,255u,255u},
                             {9u,12u,255u,255u,255u} };
    (void)white;

    fixture two_path_then_image{};
    two_path_then_image.a=outer+path_in+"Q "+img_out;
    if(positive_scoped_mixed_v6(
        two_path_then_image,1u,1u,1u,
        {{13u,16u,255u,0u,255u},{23u,3u,255u,0u,0u},
         {28u,6u,255u,255u,0u},{9u,12u,255u,255u,255u}})!=0)
        return EXIT_FAILURE;
    fixture two_image_then_path{};
    two_image_then_path.a=outer+img_in+"Q "+path_out;
    if(positive_scoped_mixed_v6(
        two_image_then_path,1u,1u,1u,
        {{13u,11u,255u,0u,0u},{13u,16u,0u,0u,255u},
         {26u,5u,255u,0u,0u},{23u,3u,255u,255u,255u}})!=0)
        return EXIT_FAILURE;
    fixture three_clipped_image_path{};
    three_clipped_image_path.a=outer+img_in+path_in+"Q "+path_out;
    if(positive_scoped_mixed_v6(
        three_clipped_image_path,2u,2u,1u,
        {{13u,16u,255u,0u,255u},{26u,5u,255u,0u,0u}})!=0)
        return EXIT_FAILURE;
    fixture three_outside_image_path{};
    three_outside_image_path.a=outer+path_in+"Q "+img_out+path_out;
    if(positive_scoped_mixed_v6(
        three_outside_image_path,1u,2u,1u,
        {{13u,16u,255u,0u,255u},{23u,3u,255u,0u,0u},
         {28u,6u,255u,0u,0u}})!=0)
        return EXIT_FAILURE;
    fixture three_clipped_path_image{};
    three_clipped_path_image.a=outer+path_in+img_in+"Q "+img_out;
    if(positive_scoped_mixed_v6(
        three_clipped_path_image,2u,1u,2u,
        {{13u,16u,0u,0u,255u},{23u,3u,255u,0u,0u},
         {28u,6u,255u,255u,0u}})!=0)
        return EXIT_FAILURE;
    fixture three_outside_image_path_extra{};
    three_outside_image_path_extra.a=outer+img_in+"Q "+img_out+path_out;
    if(positive_scoped_mixed_v6(
        three_outside_image_path_extra,1u,1u,2u,
        {{13u,16u,0u,0u,255u},{26u,5u,255u,0u,0u}})!=0)
        return EXIT_FAILURE;
    fixture three_clipped_path_image_last{};
    three_clipped_path_image_last.a=outer+path_in+img_in+"Q "+path_out;
    if(positive_scoped_mixed_v6(
        three_clipped_path_image_last,2u,2u,1u,
        {{13u,16u,0u,0u,255u},{26u,5u,255u,0u,0u}})!=0)
        return EXIT_FAILURE;

    fixture four{};
    four.a=outer+img_in+path_in+"Q "+img_out+path_out;
    if(positive_scoped_mixed_v6(
        four,2u,2u,2u,
        {{13u,16u,255u,0u,255u},{28u,6u,255u,0u,0u}},
        true)!=0)return EXIT_FAILURE;

    fixture split_evenodd{};
    split_evenodd.split=true;
    split_evenodd.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split_evenodd.b=" n q 16 0 0 12 6 3 cm /I Do Q Q "+path_out;
    if(positive_scoped_mixed_v6(
        split_evenodd,1u,1u,1u,
        {{13u,11u,255u,0u,0u},{13u,16u,0u,0u,255u},
         {26u,5u,255u,0u,0u}})!=0)
        return EXIT_FAILURE;

    const fixture bad[]={
        {outer+img_in+"Q "},                  /* only Image */
        {outer+path_in+"Q "},                 /* only Path */
        {outer+img_in+path_in+"Q "},          /* no unmasked layer */
        {outer+"Q "+img_out+path_out},       /* no clipped layer */
        {outer+img_in+"Q "+path_out+" BT ET"},/* late Text */
        {outer+img_in+"Q "+path_out+img_out+img_out},
        {outer+path_in+path_in+"Q "+path_out+img_out},
        {outer+"q 16 1 0 12 8 3 cm /I Do Q Q "+path_out},
        {outer+img_in+"Q "+path_out+" q 1 1 m 2 2 l S Q"}
    };
    for(const auto& sc:bad)
        if(negative_scoped_mixed_v6(
            sc,QUANTAPDF_ERROR_UNSUPPORTED,bounds)!=0)
            return EXIT_FAILURE;
    fixture unbalanced{};
    unbalanced.a=outer+img_in+path_out;
    if(negative_scoped_mixed_v6(
        unbalanced,QUANTAPDF_ERROR_FORMAT,bounds)!=0)
        return EXIT_FAILURE;
    fixture missing{};
    missing.a=two_image_then_path.a;missing.missing_resources=true;
    if(negative_scoped_mixed_v6(
        missing,QUANTAPDF_ERROR_FORMAT,bounds)!=0)
        return EXIT_FAILURE;
    fixture masked{};
    masked.a=two_image_then_path.a;masked.mask=true;
    if(negative_scoped_mixed_v6(
        masked,QUANTAPDF_ERROR_UNSUPPORTED,bounds)!=0)
        return EXIT_FAILURE;
    auto limited_bounds=bounds;
    limited_bounds.max_operations=1u;
    if(negative_scoped_mixed_v6(
        two_image_then_path,QUANTAPDF_ERROR_UNSUPPORTED,limited_bounds)!=0)
        return EXIT_FAILURE;
    /* The flexible profile consumes the requested ACTUAL paint quota.
     * Two paints under a two-operation budget succeed; three must refuse. */
    limited_bounds.max_operations=2u;
    {
        const auto original=original_pdf(two_path_then_image);
        quantapdf_ir exact{};
        OK(quantapdf_qpdf_source_scoped_mixed_v6_to_ir(
            original.data(),original.size(),&limited_bounds,&exact));
        CHECK(exact.operation_count==2u);
        quantapdf_ir_dispose(&exact);
    }
    if(negative_scoped_mixed_v6(
        three_clipped_image_path,QUANTAPDF_ERROR_UNSUPPORTED,limited_bounds)!=0)
        return EXIT_FAILURE;
    limited_bounds=bounds;
    limited_bounds.max_source_pixels=3u;
    if(negative_scoped_mixed_v6(
        two_image_then_path,QUANTAPDF_ERROR_UNSUPPORTED,limited_bounds)!=0)
        return EXIT_FAILURE;
    if(positive_scoped_mixed_v6(
        two_path_then_image,1u,1u,1u,
        {{13u,16u,255u,0u,255u},{23u,3u,255u,0u,0u}})!=0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

/* Private Source V7 qualifies finite normalized 0..1 RGB path paints.
 * No new raster code, layer count, public ABI or production API. */
int source_scoped_mixed_v7_suite()
{
    auto bounds=source_limits;
    bounds.max_operations=4u;
    const std::string outer="q 12 6 8 9 re W n ";
    const std::string inside="0.25 0.5 0.75 rg 12 6 4 4 re f ";
    const std::string outside="0.875 0.125 0.375 rg 24 17 5 3 re f ";
    const std::string image_in="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string image_out="q 8 0 0 6 22 16 cm /I Do Q ";

    fixture two_image_path{};
    two_image_path.a=outer+image_in+"Q "+outside;
    if(positive_scoped_mixed_v6(
        two_image_path,1u,1u,1u,
        {{13u,11u,255u,0u,0u},{13u,16u,0u,0u,255u},
         {26u,5u,223u,32u,96u},{23u,3u,255u,255u,255u}},
        false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture two_path_image{};
    two_path_image.a=outer+inside+"Q "+image_out;
    if(positive_scoped_mixed_v6(
        two_path_image,1u,1u,1u,
        {{13u,16u,64u,128u,191u},{23u,3u,255u,0u,0u},
         {28u,6u,255u,255u,0u},{9u,12u,255u,255u,255u}},
        false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture three{};
    three.a=outer+image_in+inside+"Q "+outside;
    if(positive_scoped_mixed_v6(
        three,2u,2u,1u,
        {{13u,16u,64u,128u,191u},{13u,11u,255u,0u,0u},
         {26u,5u,223u,32u,96u}},
        false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture four{};
    four.a=outer+inside+image_in+"Q "+image_out+outside;
    if(positive_scoped_mixed_v6(
        four,2u,2u,2u,
        {{13u,16u,0u,0u,255u},{23u,3u,255u,0u,0u},
         {26u,5u,223u,32u,96u}},
        false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture split{};
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n 0.25 0.5 0.75 rg 10 6 4 4 re f Q "+image_out;
    if(positive_scoped_mixed_v6(
        split,1u,1u,1u,
        {{13u,16u,64u,128u,191u},{23u,3u,255u,0u,0u}},
        false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture restored{};
    restored.a=outer+"q "+inside+"Q Q "+image_out+
        "24 17 5 3 re f ";
    if(positive_scoped_mixed_v6(
        restored,1u,2u,1u,
        {{13u,16u,64u,128u,191u},{23u,3u,255u,0u,0u},
         {26u,5u,0u,0u,0u}},
        false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Invalid colors fail AFTER a legitimate clipped Image was accepted,
     * and reset a poisoned output on every attempt. */
    const fixture invalid[]={
        {outer+image_in+"Q -0.01 0.5 0.5 rg 24 17 5 3 re f"},
        {outer+image_in+"Q 1.001 0.5 0.5 rg 24 17 5 3 re f"},
        {outer+image_in+"Q 0.5 0.5 0.5 RG 24 17 5 3 re f"},
        {outer+image_in+"Q "+outside+" BT ET"},
        {outer+inside+"Q "+image_out+" /I Do /I Do"}
    };
    for(const auto& bad:invalid)
        if(negative_scoped_mixed_v6(
            bad,QUANTAPDF_ERROR_UNSUPPORTED,bounds,true)!=EXIT_SUCCESS)
            return EXIT_FAILURE;

    fixture masked{};
    masked.a=two_image_path.a;
    masked.mask=true;
    if(negative_scoped_mixed_v6(
        masked,QUANTAPDF_ERROR_UNSUPPORTED,bounds,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture missing{};
    missing.a=two_image_path.a;
    missing.missing_resources=true;
    if(negative_scoped_mixed_v6(
        missing,QUANTAPDF_ERROR_FORMAT,bounds,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    auto constrained=bounds;
    constrained.max_operations=2u;
    if(negative_scoped_mixed_v6(
        three,QUANTAPDF_ERROR_UNSUPPORTED,constrained,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    constrained=bounds;
    constrained.max_source_pixels=3u;
    if(negative_scoped_mixed_v6(
        two_image_path,QUANTAPDF_ERROR_UNSUPPORTED,constrained,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    /* Existing exact-binary source must produce identical detached IR
     * Native bytes through V6 and V7; neither mutates source ownership. */
    const auto binary=original_pdf(fixture{outer+image_in+"Q "+
        "1 0 0 rg 24 17 5 3 re f "});
    quantapdf_ir older{},newer{};
    OK(quantapdf_qpdf_source_scoped_mixed_v6_to_ir(
        binary.data(),binary.size(),&bounds,&older));
    OK(quantapdf_qpdf_source_scoped_mixed_v7_to_ir(
        binary.data(),binary.size(),&bounds,&newer));
    const quantapdf_ir_raster_scoped_mixed_v5_limits raster_bounds={
        W*H,4u,512u,8000000u,16u,4096u
    };
    quantapdf_ir_raster_image old_pixels{},new_pixels{};
    OK(quantapdf_ir_raster_scoped_mixed_v5(
        &older,0u,&raster_bounds,&old_pixels));
    OK(quantapdf_ir_raster_scoped_mixed_v5(
        &newer,0u,&raster_bounds,&new_pixels));
    CHECK(old_pixels.size==new_pixels.size &&
          std::memcmp(old_pixels.pixels,new_pixels.pixels,
                      old_pixels.size)==0);
    quantapdf_ir_raster_image_dispose(&old_pixels);
    quantapdf_ir_raster_image_dispose(&new_pixels);
    quantapdf_ir_dispose(&older);
    quantapdf_ir_dispose(&newer);

    /* Recovery after all prior failures must yield the same original
     * independent reference and repeated source-owner-detached bytes. */
    if(positive_scoped_mixed_v6(
        two_path_image,1u,1u,1u,
        {{13u,16u,64u,128u,191u},{23u,3u,255u,0u,0u}},
        false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    return EXIT_SUCCESS;
}


/* Opt-in V8: a single normalized /DeviceGray nonstroking g channel
 * duplicates into opaque RGB on the existing Source V7 Path/Image and C11
 * raster budget. V7 must fail closed for the very same g source bytes. */
int source_scoped_mixed_v8_suite()
{
    auto bounds=source_limits;
    bounds.max_operations=4u;
    const std::string outer="q 12 6 8 9 re W n ";
    const std::string inside="0.25 g 12 6 4 4 re f ";
    const std::string outside="0.875 g 24 17 5 3 re f ";
    const std::string image_in="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string image_out="q 8 0 0 6 22 16 cm /I Do Q ";

    fixture two_path_image{};
    two_path_image.a=outer+inside+"Q "+image_out;
    if(positive_scoped_mixed_v6(
        two_path_image,1u,1u,1u,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u},
         {28u,6u,255u,255u,0u},{9u,12u,255u,255u,255u}},
        false,true,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture two_image_path{};
    two_image_path.a=outer+image_in+"Q "+outside;
    if(positive_scoped_mixed_v6(
        two_image_path,1u,1u,1u,
        {{13u,11u,255u,0u,0u},{13u,16u,0u,0u,255u},
         {26u,5u,223u,223u,223u},{23u,3u,255u,255u,255u}},
        false,true,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture three{};
    three.a=outer+image_in+inside+"Q "+outside;
    if(positive_scoped_mixed_v6(
        three,2u,2u,1u,
        {{13u,16u,64u,64u,64u},{13u,11u,255u,0u,0u},
         {26u,5u,223u,223u,223u}},
        false,true,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture four{};
    four.a=outer+inside+image_in+"Q "+image_out+outside;
    if(positive_scoped_mixed_v6(
        four,2u,2u,2u,
        {{13u,16u,0u,0u,255u},{23u,3u,255u,0u,0u},
         {26u,5u,223u,223u,223u}},
        false,true,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture split{};
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n 0.5 g 10 6 4 4 re f Q "+image_out;
    if(positive_scoped_mixed_v6(
        split,1u,1u,1u,
        {{13u,16u,128u,128u,128u},{23u,3u,255u,0u,0u}},
        false,true,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture restored{};
    restored.a=outer+"q "+inside+"Q Q "+image_out+
        "24 17 5 3 re f ";
    if(positive_scoped_mixed_v6(
        restored,1u,2u,1u,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u},
         {26u,5u,0u,0u,0u}},
        false,true,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Late invalid color/graphics/resource operators must destroy ALL
     * pre-staged paints and reset poisoned result slots on each repeat. */
    const fixture invalid[]={
        {outer+image_in+"Q -0.01 g 24 17 5 3 re f"},
        {outer+image_in+"Q 1.001 g 24 17 5 3 re f"},
        {outer+image_in+"Q 0.25 G 24 17 5 3 re f"},
        {outer+image_in+"Q "+outside+" BT ET"},
        {outer+inside+"Q "+image_out+" /I Do /I Do"}
    };
    for(const auto& bad:invalid)
        if(negative_scoped_mixed_v6(
            bad,QUANTAPDF_ERROR_UNSUPPORTED,bounds,true,true)!=EXIT_SUCCESS)
            return EXIT_FAILURE;
    /* A syntactically ill-formed operand count is FORMAT, not an
     * otherwise valid but unqualified Gray color. */
    if(negative_scoped_mixed_v6(
        fixture{outer+image_in+"Q 0.25 0.5 g 24 17 5 3 re f"},
        QUANTAPDF_ERROR_FORMAT,bounds,true,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture masked{};
    masked.a=two_image_path.a;masked.mask=true;
    if(negative_scoped_mixed_v6(
        masked,QUANTAPDF_ERROR_UNSUPPORTED,bounds,true,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture missing{};
    missing.a=two_image_path.a;missing.missing_resources=true;
    if(negative_scoped_mixed_v6(
        missing,QUANTAPDF_ERROR_FORMAT,bounds,true,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    auto constrained=bounds;
    constrained.max_operations=2u;
    if(negative_scoped_mixed_v6(
        three,QUANTAPDF_ERROR_UNSUPPORTED,constrained,true,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    constrained=bounds;
    constrained.max_source_pixels=3u;
    if(negative_scoped_mixed_v6(
        two_image_path,QUANTAPDF_ERROR_UNSUPPORTED,constrained,true,true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    /* The V7 RGB source grammar is a strict subset of V8; admission must
     * be bit-exact on the old profile, not just visually close. */
    const auto original=original_pdf(fixture{
        outer+"0.25 0.5 0.75 rg 12 6 4 4 re f Q "+image_out});
    quantapdf_ir old_ir{},new_ir{};
    OK(quantapdf_qpdf_source_scoped_mixed_v7_to_ir(
        original.data(),original.size(),&bounds,&old_ir));
    OK(quantapdf_qpdf_source_scoped_mixed_v8_to_ir(
        original.data(),original.size(),&bounds,&new_ir));
    const quantapdf_ir_raster_scoped_mixed_v5_limits raster_bounds={
        W*H,4u,512u,8000000u,16u,4096u
    };
    quantapdf_ir_raster_image old_out{},new_out{};
    OK(quantapdf_ir_raster_scoped_mixed_v5(
        &old_ir,0u,&raster_bounds,&old_out));
    OK(quantapdf_ir_raster_scoped_mixed_v5(
        &new_ir,0u,&raster_bounds,&new_out));
    CHECK(old_out.size==new_out.size &&
          std::memcmp(old_out.pixels,new_out.pixels,old_out.size)==0);
    quantapdf_ir_raster_image_dispose(&old_out);
    quantapdf_ir_raster_image_dispose(&new_out);
    quantapdf_ir_dispose(&old_ir);
    quantapdf_ir_dispose(&new_ir);

    if(positive_scoped_mixed_v6(
        two_path_image,1u,1u,1u,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u}},
        false,true,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

/* All twelve historical private Source signatures must remain exact.
 * Pair each admitted original PDF with the next (strictly wider or
 * mutually incompatible) profile, which must refuse the same bytes
 * atomically instead of silently widening older source admission. */
int source_profile_legacy_parity_suite()
{
    using source_fn=quantapdf_status (*)(
        const unsigned char *,size_t,
        const quantapdf_qpdf_source_raw_image_limits *,quantapdf_ir *);
    const source_fn profiles[]={
        quantapdf_qpdf_source_raw_images_to_ir,
        quantapdf_qpdf_source_mixed_path_image_to_ir,
        quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2,
        quantapdf_qpdf_source_mixed_two_paths_two_images_to_ir_v3,
        quantapdf_qpdf_source_clipped_raw_image_to_ir_v1,
        quantapdf_qpdf_source_two_clipped_raw_images_to_ir_v2,
        quantapdf_qpdf_source_restored_clipped_raw_images_to_ir_v3,
        quantapdf_qpdf_source_clipped_image_then_path_to_ir_v4,
        quantapdf_qpdf_source_scoped_mixed_v5_to_ir,
        quantapdf_qpdf_source_scoped_mixed_v6_to_ir,
        quantapdf_qpdf_source_scoped_mixed_v7_to_ir,
        quantapdf_qpdf_source_scoped_mixed_v8_to_ir
    };
    struct profile_case { std::string stream; size_t paints; };
    const std::string image_in=
        "q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string image_out=
        "q 8 0 0 6 22 16 cm /I Do Q ";
    const std::string image_clipped_second=
        "q 8 0 0 6 12 7 cm /I Do Q ";
    const std::string red_path=
        "1 0 0 rg 24 17 5 3 re f ";
    const std::string magenta_path=
        "1 0 1 rg 12 6 4 4 re f ";
    const std::string scope="q 12 6 8 9 re W n ";
    const profile_case cases[]={
        {image_in,1u},
        {red_path+image_in,2u},
        {red_path+image_in+image_out,3u},
        {magenta_path+image_in+image_out+red_path,4u},
        {scope+image_in+"Q ",1u},
        {scope+image_in+image_clipped_second+"Q ",2u},
        {scope+image_in+"Q "+image_out,2u},
        {scope+image_in+"Q "+red_path,2u},
        {scope+image_in+magenta_path+"Q "+image_out+red_path,4u},
        {scope+magenta_path+"Q "+image_out,2u},
        {scope+"0.25 0.5 0.75 rg 12 6 4 4 re f Q "+image_out,2u},
        {scope+"0.25 g 12 6 4 4 re f Q "+image_out,2u}
    };
    constexpr size_t count=sizeof(profiles)/sizeof(profiles[0]);
    static_assert(count==12u);
    CHECK(sizeof(cases)/sizeof(cases[0])==count);
    auto budgets=source_limits;
    budgets.max_operations=4u;

    for(size_t i=0u;i<count;++i) {
        const auto accepted=original_pdf(fixture{cases[i].stream});
        quantapdf_ir admitted;
        std::memset(&admitted,0xa5,sizeof(admitted));
        const quantapdf_status success=profiles[i](
            accepted.data(),accepted.size(),&budgets,&admitted);
        if(success!=QUANTAPDF_OK)
            std::fprintf(stderr,
                "Profile parity positive index=%zu status=%d\n",
                i,(int)success);
        CHECK(success==QUANTAPDF_OK);
        CHECK(admitted.page_count==1u &&
              admitted.operation_count==cases[i].paints &&
              admitted.operations!=nullptr &&
              admitted.pages!=nullptr);
        quantapdf_ir_dispose(&admitted);
        CHECK(admitted.pages==nullptr && admitted.operations==nullptr &&
              admitted.resources.images==nullptr);

        if(i+1u<count) {
            const auto unexpected=original_pdf(fixture{cases[i+1u].stream});
            for(int repeat=0;repeat<2;++repeat) {
                quantapdf_ir refused;
                std::memset(&refused,0xa5,sizeof(refused));
                const quantapdf_status status=profiles[i](
                    unexpected.data(),unexpected.size(),&budgets,&refused);
                if(status!=QUANTAPDF_ERROR_UNSUPPORTED)
                    std::fprintf(stderr,
                        "Profile parity negative index=%zu status=%d\n",
                        i,(int)status);
                CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED);
                CHECK(refused.pages==nullptr &&
                      refused.page_count==0u &&
                      refused.operations==nullptr &&
                      refused.operation_count==0u &&
                      refused.resources.images==nullptr &&
                      refused.resources.graphics_states==nullptr &&
                      refused.resources.clips==nullptr);
                quantapdf_ir_dispose(&refused);
            }
        }
    }
    return EXIT_SUCCESS;
}

/* Real private product-archive bridge: original bytes go through the
 * production-linked QPDF Source V8 adapter and canonical C11 Raster
 * objects. PDFium is invoked ONLY on the unchanged original bytes in this
 * test binary, not by the bridge and never as its fallback. */
int positive_native_source_bridge_v1(
    const fixture& sample,const std::vector<v6_probe>& probes)
{
    auto original=original_pdf(sample);
    CHECK(original.size()>100u);
    const auto direct=pdfium_original_rgb(original);
    CHECK(direct.size()==W*H*3u);
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    quantapdf_native_source_raster_bridge_limits_v1 limits={
        source_limits,{W*H,4u,512u,8000000u,16u,4096u}
    };
    limits.source.max_operations=4u;
    quantapdf_ir_raster_image first{},second{};
    OK(quantapdf_native_source_raster_bridge_v1(
        original.data(),original.size(),&limits,&device,&first));

    /* Reopen from independently owned original bytes. The source/QPDF
     * owner used by the previous call is gone before the C11 sampling. */
    auto duplicate=original;
    std::fill(original.begin(),original.end(),0u);
    original.clear();
    OK(quantapdf_native_source_raster_bridge_v1(
        duplicate.data(),duplicate.size(),&limits,&device,&second));
    std::fill(duplicate.begin(),duplicate.end(),0u);
    duplicate.clear();

    CHECK(first.width==W && first.height==H &&
          first.stride==W*4u && first.size==W*H*4u &&
          first.pixels!=nullptr && second.pixels!=nullptr &&
          second.size==first.size &&
          std::memcmp(first.pixels,second.pixels,first.size)==0);
    double total=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const auto *a=first.pixels+y*first.stride+x*4u;
        const auto *b=direct.data()+(y*W+x)*3u;
        CHECK(a[3]==255u);
        unsigned peak=0u;
        for(size_t c=0u;c<3u;++c) {
            const unsigned delta=a[c]>b[c] ? a[c]-b[c] : b[c]-a[c];
            total+=(double)delta;
            if(delta>peak)peak=delta;
        }
        if(peak>64u)++gross;
    }
    CHECK(total/(double)(W*H*3u)<=8.0 && gross<=W*H/12u);
    for(const auto& probe:probes) {
        CHECK(probe.x<W && probe.y<H);
        const auto *a=first.pixels+
            probe.y*first.stride+probe.x*4u;
        const auto *b=direct.data()+(probe.y*W+probe.x)*3u;
        CHECK(rgb_match(a,probe.r,probe.g,probe.b,3u));
        CHECK(rgb_match(b,probe.r,probe.g,probe.b,3u));
    }
    quantapdf_ir_raster_image_dispose(&first);
    quantapdf_ir_raster_image_dispose(&second);
    return EXIT_SUCCESS;
}

int negative_native_source_bridge_v1(
    const fixture& sample,quantapdf_status expected,
    const quantapdf_native_source_raster_bridge_limits_v1& limits,
    const quantapdf_native_source_raster_device_v1& device)
{
    const auto original=original_pdf(sample);
    for(int repeat=0;repeat<2;++repeat) {
        quantapdf_ir_raster_image refused;
        std::memset(&refused,0xa5,sizeof(refused));
        const auto actual=quantapdf_native_source_raster_bridge_v1(
            original.data(),original.size(),&limits,&device,&refused);
        if(actual!=expected)
            std::fprintf(stderr,
                "Native product bridge refusal trial=%d want=%d got=%d\n",
                repeat,(int)expected,(int)actual);
        CHECK(actual==expected);
        CHECK(refused.pixels==nullptr && refused.width==0u &&
              refused.height==0u && refused.stride==0u &&
              refused.size==0u);
        quantapdf_ir_raster_image_dispose(&refused);
    }
    return EXIT_SUCCESS;
}

int native_source_product_bridge_v1_suite()
{
    const std::string scope="q 12 6 8 9 re W n ";
    const std::string gray="0.25 g 12 6 4 4 re f ";
    const std::string image_in="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string image_out="q 8 0 0 6 22 16 cm /I Do Q ";
    const std::string outside="0.875 g 24 17 5 3 re f ";

    fixture two{};
    two.a=scope+gray+"Q "+image_out;
    if(positive_native_source_bridge_v1(two,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u},
         {28u,6u,255u,255u,0u}})!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture three{};
    three.a=scope+image_in+gray+"Q "+outside;
    if(positive_native_source_bridge_v1(three,
        {{13u,16u,64u,64u,64u},{13u,11u,255u,0u,0u},
         {26u,5u,223u,223u,223u}})!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture four{};
    four.a=scope+gray+image_in+"Q "+image_out+outside;
    if(positive_native_source_bridge_v1(four,
        {{13u,16u,0u,0u,255u},{23u,3u,255u,0u,0u},
         {26u,5u,223u,223u,223u}})!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture rgb{};
    rgb.a=scope+"0.25 0.5 0.75 rg 12 6 4 4 re f Q "+image_out;
    if(positive_native_source_bridge_v1(rgb,
        {{13u,16u,64u,128u,191u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture split{};
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n 0.5 g 10 6 4 4 re f Q "+image_out;
    if(positive_native_source_bridge_v1(split,
        {{13u,16u,128u,128u,128u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    quantapdf_native_source_raster_bridge_limits_v1 limits={
        source_limits,{W*H,4u,512u,8000000u,16u,4096u}
    };
    limits.source.max_operations=4u;
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    fixture late{};
    late.a=three.a+" BT ET";
    if(negative_native_source_bridge_v1(
        late,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture excess{};
    excess.a=four.a+image_out;
    if(negative_native_source_bridge_v1(
        excess,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture masked{};
    masked.a=two.a;masked.mask=true;
    if(negative_native_source_bridge_v1(
        masked,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture missing{};
    missing.a=two.a;missing.missing_resources=true;
    if(negative_native_source_bridge_v1(
        missing,QUANTAPDF_ERROR_FORMAT,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    auto constrained=limits;
    constrained.source.max_operations=1u;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,constrained,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    constrained=limits;
    constrained.raster.max_operations=1u;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,constrained,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    constrained=limits;
    constrained.raster.max_pixels=W*H-1u;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,constrained,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    auto unsupported_device=device;
    unsupported_device.page_index=1u;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,unsupported_device)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    unsupported_device=device;
    unsupported_device.dpi=144.0f;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,unsupported_device)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    unsupported_device=device;
    unsupported_device.rotation_degrees=90.0f;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,unsupported_device)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    unsupported_device=device;
    unsupported_device.clip_enabled=1;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,unsupported_device)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    unsupported_device=device;
    unsupported_device.alpha=1;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,unsupported_device)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    unsupported_device=device;
    unsupported_device.thumbnail=1;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,unsupported_device)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    unsupported_device=device;
    unsupported_device.dpi=NAN;
    if(negative_native_source_bridge_v1(
        two,QUANTAPDF_ERROR_ARGUMENT,limits,unsupported_device)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    /* A valid original must still paint after all poisoned-output
     * refusals, proving no retained QPDF/IR/raster state leaked. */
    if(positive_native_source_bridge_v1(two,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

/* Real built-in provider page ownership, NOT a synthetic copied PDF
 * source pointer and not a replacement of public PDFium dispatch. */
int positive_builtin_native_page_v1(
    const fixture& source,const std::vector<v6_probe>& goldens)
{
    auto bytes=original_pdf(source);
    CHECK(bytes.size()>100u);
    const auto direct=pdfium_original_rgb(bytes);
    CHECK(direct.size()==W*H*3u);
    const auto *backend=quantapdf_builtin_backend_ops();
    CHECK(backend!=nullptr && backend->render_page!=nullptr);
    const quantapdf_native_source_raster_bridge_limits_v1 limits={
        {source_limits.max_pdf_bytes,source_limits.max_content_bytes,
         source_limits.max_content_streams,4u,source_limits.max_page_pixels,
         source_limits.max_source_pixels,source_limits.max_drawn_pixels,
         source_limits.max_parent_steps},
        {W*H,4u,512u,8000000u,16u,4096u}
    };
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    quantapdf_ir_raster_image first{},again{};
    quantapdf_ir_raster_image *outputs[]={&first,&again};
    for(auto *out:outputs) {
        void *document=nullptr,*page=nullptr;
        OK(backend->open_memory(bytes.data(),bytes.size(),nullptr,&document));
        CHECK(document!=nullptr);
        int page_count=0;
        OK(backend->page_count(document,&page_count));
        CHECK(page_count==1);
        OK(backend->load_page(document,0,&page));
        CHECK(page!=nullptr);
        quantapdf_rect box{};
        OK(backend->page_bounds(page,&box));
        CHECK(box.x0==0.0f && box.y0==0.0f &&
              box.x1==(float)W && box.y1==(float)H);
        OK(quantapdf_builtin_native_qualify_page_v1(
            page,&limits,&device,out));
        backend->drop_page(page);
        backend->close(document);
    }
    std::fill(bytes.begin(),bytes.end(),0u);
    bytes.clear();
    CHECK(first.width==W && first.height==H &&
          first.size==W*H*4u && first.stride==W*4u &&
          first.pixels!=nullptr &&
          again.size==first.size && again.pixels!=nullptr &&
          std::memcmp(first.pixels,again.pixels,first.size)==0);

    double sum=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<H;++y)for(size_t x=0u;x<W;++x) {
        const auto *a=first.pixels+y*first.stride+x*4u;
        const auto *b=direct.data()+(y*W+x)*3u;
        CHECK(a[3]==255u);
        unsigned peak=0u;
        for(size_t c=0u;c<3u;++c){
            const unsigned d=a[c]>b[c]?a[c]-b[c]:b[c]-a[c];
            sum+=(double)d;
            if(d>peak)peak=d;
        }
        if(peak>64u)++gross;
    }
    CHECK(sum/(double)(W*H*3u)<=8.0 && gross<=W*H/12u);
    for(const auto& probe:goldens) {
        CHECK(probe.x<W && probe.y<H);
        const auto *a=first.pixels+probe.y*first.stride+probe.x*4u;
        const auto *b=direct.data()+(probe.y*W+probe.x)*3u;
        CHECK(rgb_match(a,probe.r,probe.g,probe.b,3u));
        CHECK(rgb_match(b,probe.r,probe.g,probe.b,3u));
    }
    quantapdf_ir_raster_image_dispose(&first);
    quantapdf_ir_raster_image_dispose(&again);
    return EXIT_SUCCESS;
}

int negative_builtin_native_page_v1(
    const fixture& source,quantapdf_status expected,
    const quantapdf_native_source_raster_bridge_limits_v1& limits,
    const quantapdf_native_source_raster_device_v1& device)
{
    auto bytes=original_pdf(source);
    const auto *backend=quantapdf_builtin_backend_ops();
    void *document=nullptr,*page=nullptr;
    OK(backend->open_memory(bytes.data(),bytes.size(),nullptr,&document));
    CHECK(document!=nullptr);
    OK(backend->load_page(document,0,&page));
    CHECK(page!=nullptr);
    for(int attempt=0;attempt<2;++attempt) {
        quantapdf_ir_raster_image refused;
        std::memset(&refused,0xa5,sizeof(refused));
        const auto status=quantapdf_builtin_native_qualify_page_v1(
            page,&limits,&device,&refused);
        if(status!=expected)std::fprintf(stderr,
            "Built-in Native page refusal trial=%d expected=%d actual=%d\n",
            attempt,(int)expected,(int)status);
        CHECK(status==expected);
        CHECK(refused.pixels==nullptr && refused.width==0u &&
              refused.height==0u && refused.stride==0u &&
              refused.size==0u);
        quantapdf_ir_raster_image_dispose(&refused);
        /* A rejected native request cannot corrupt the borrowed built-in
         * page or cause the real Source document to become unusable. */
        quantapdf_rect box{};
        OK(backend->page_bounds(page,&box));
        CHECK(box.x1==(float)W && box.y1==(float)H);
    }
    backend->drop_page(page);
    backend->close(document);
    return EXIT_SUCCESS;
}

int builtin_native_page_owner_v1_suite()
{
    const std::string scope="q 12 6 8 9 re W n ";
    const std::string gray="0.25 g 12 6 4 4 re f ";
    const std::string inner="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string outer="q 8 0 0 6 22 16 cm /I Do Q ";
    const std::string outpath="0.875 g 24 17 5 3 re f ";
    fixture two{},three{},four{},rgb{},split{};
    two.a=scope+gray+"Q "+outer;
    three.a=scope+inner+gray+"Q "+outpath;
    four.a=scope+gray+inner+"Q "+outer+outpath;
    rgb.a=scope+"0.25 0.5 0.75 rg 12 6 4 4 re f Q "+outer;
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n 0.5 g 10 6 4 4 re f Q "+outer;

    if(positive_builtin_native_page_v1(two,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_builtin_native_page_v1(three,
        {{13u,16u,64u,64u,64u},{13u,11u,255u,0u,0u},
         {26u,5u,223u,223u,223u}})!=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_builtin_native_page_v1(four,
        {{13u,16u,0u,0u,255u},{26u,5u,223u,223u,223u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_builtin_native_page_v1(rgb,
        {{13u,16u,64u,128u,191u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_builtin_native_page_v1(split,
        {{13u,16u,128u,128u,128u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    const quantapdf_native_source_raster_bridge_limits_v1 limits={
        {source_limits.max_pdf_bytes,source_limits.max_content_bytes,
         source_limits.max_content_streams,4u,source_limits.max_page_pixels,
         source_limits.max_source_pixels,source_limits.max_drawn_pixels,
         source_limits.max_parent_steps},
        {W*H,4u,512u,8000000u,16u,4096u}
    };
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    fixture late{},masked{},missing{},excess{};
    late.a=two.a+" BT ET";
    masked.a=two.a;masked.mask=true;
    missing.a=two.a;missing.missing_resources=true;
    excess.a=four.a+outer;

    if(negative_builtin_native_page_v1(
        late,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(negative_builtin_native_page_v1(
        masked,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(negative_builtin_native_page_v1(
        missing,QUANTAPDF_ERROR_FORMAT,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(negative_builtin_native_page_v1(
        excess,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    auto limited=limits;
    limited.source.max_operations=1u;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limited,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    limited=limits;
    limited.raster.max_pixels=W*H-1u;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limited,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    auto modified=device;
    modified.page_index=1u;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,modified)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    modified=device;modified.dpi=144.0f;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,modified)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    modified=device;modified.alpha=1;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,modified)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    modified=device;modified.rotation_degrees=90.0f;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,modified)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    modified=device;modified.thumbnail=1;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,modified)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    modified=device;modified.clip_enabled=1;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,modified)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    modified=device;modified.dpi=NAN;
    if(negative_builtin_native_page_v1(
        two,QUANTAPDF_ERROR_ARGUMENT,limits,modified)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    /* A password-bearing built-in session must not be rendered using the
     * private password-unaware Source interpreter or retried with PDFium.
     * QPDF opens the valid, unencrypted fixture even with an unused
     * caller-provided password; native qualification explicitly refuses. */
    {
        const auto original=original_pdf(two);
        const auto *backend=quantapdf_builtin_backend_ops();
        void *document=nullptr,*page=nullptr;
        OK(backend->open_memory(
            original.data(),original.size(),
            "unused-session-password",&document));
        CHECK(document!=nullptr);
        OK(backend->load_page(document,0,&page));
        CHECK(page!=nullptr);
        for(int attempt=0;attempt<2;++attempt) {
            quantapdf_ir_raster_image refused;
            std::memset(&refused,0xa5,sizeof(refused));
            CHECK(quantapdf_builtin_native_qualify_page_v1(
                page,&limits,&device,&refused)==
                QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(refused.pixels==nullptr && refused.width==0u &&
                  refused.height==0u && refused.stride==0u &&
                  refused.size==0u);
            quantapdf_ir_raster_image_dispose(&refused);
        }
        quantapdf_rect bounds{};
        OK(backend->page_bounds(page,&bounds));
        CHECK(bounds.x1==(float)W && bounds.y1==(float)H);
        backend->drop_page(page);
        backend->close(document);
    }

    /* No valid handle permits accidental publication through a bad API.
     * This must clear even an intentionally poisoned output. */
    quantapdf_ir_raster_image bad;
    std::memset(&bad,0xa5,sizeof(bad));
    CHECK(quantapdf_builtin_native_qualify_page_v1(
        nullptr,&limits,&device,&bad)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bad.pixels==nullptr && bad.size==0u &&
          bad.width==0u && bad.height==0u && bad.stride==0u);
    CHECK(quantapdf_builtin_native_qualify_page_v1(
        nullptr,&limits,&device,nullptr)==QUANTAPDF_ERROR_ARGUMENT);

    if(positive_builtin_native_page_v1(two,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

/* Production-private RGB bitmap output qualification: real provider
 * page owner + complete Source V8 + detached C11 Raster + C11 RGB layout.
 * Compare both independently rendered ORIGINAL PDFium bytes and the
 * legacy backend RGB shape; no public dispatch is modified. */
int positive_builtin_backend_rgb_v1(
    const fixture& sample,const std::vector<v6_probe>& probes)
{
    auto original=original_pdf(sample);
    CHECK(original.size()>100u);
    const auto oracle=pdfium_original_rgb(original);
    CHECK(oracle.size()==W*H*3u);
    const auto *backend=quantapdf_builtin_backend_ops();
    CHECK(backend!=nullptr);
    const quantapdf_native_source_raster_bridge_limits_v1 budgets={
        {source_limits.max_pdf_bytes,source_limits.max_content_bytes,
         source_limits.max_content_streams,4u,source_limits.max_page_pixels,
         source_limits.max_source_pixels,source_limits.max_drawn_pixels,
         source_limits.max_parent_steps},
        {W*H,4u,512u,8000000u,16u,4096u}
    };
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    quantapdf_backend_bitmap first{},again{},legacy{};
    void *document=nullptr,*page=nullptr;
    OK(backend->open_memory(
        original.data(),original.size(),nullptr,&document));
    CHECK(document!=nullptr);
    int count=0;
    OK(backend->page_count(document,&count));
    CHECK(count==1);
    OK(backend->load_page(document,0,&page));
    CHECK(page!=nullptr);

    OK(quantapdf_builtin_native_qualify_bitmap_v1(
        page,&budgets,&device,&first));
    OK(quantapdf_builtin_native_qualify_bitmap_v1(
        page,&budgets,&device,&again));
    CHECK(first.data!=nullptr && again.data!=nullptr &&
          first.data!=again.data &&
          first.width==(int)W && first.height==(int)H &&
          first.components==3 && first.stride==(int)(W*3u) &&
          first.size==W*H*3u && again.size==first.size &&
          std::memcmp(first.data,again.data,first.size)==0);

    /* Existing public backend dispatch is intentionally still PDFium.
     * Invoking it here is for independent metadata/visual comparison,
     * not a fallback within the private Native call. */
    OK(backend->render_page(
        page,72.0f,0.0f,nullptr,1.0,0,&legacy));
    CHECK(legacy.data!=nullptr && legacy.width==(int)W &&
          legacy.height==(int)H && legacy.components==3 &&
          legacy.stride==(int)(W*3u) && legacy.size==first.size);

    backend->drop_page(page);
    backend->close(document);
    std::fill(original.begin(),original.end(),0u);
    original.clear();
    CHECK(std::memcmp(first.data,again.data,first.size)==0);

    double error_oracle=0.0,error_legacy=0.0;
    size_t gross=0u;
    for(size_t i=0u;i<W*H;++i) {
        unsigned peak=0u;
        for(size_t c=0u;c<3u;++c) {
            const size_t at=i*3u+c;
            const unsigned native=first.data[at];
            const unsigned reference=oracle[at];
            const unsigned prior=legacy.data[at];
            const unsigned delta=native>reference ?
                native-reference : reference-native;
            const unsigned historical=native>prior ?
                native-prior : prior-native;
            error_oracle+=(double)delta;
            error_legacy+=(double)historical;
            if(delta>peak)peak=delta;
        }
        if(peak>64u)++gross;
    }
    CHECK(error_oracle/(double)(W*H*3u)<=8.0 &&
          error_legacy/(double)(W*H*3u)<=8.0 &&
          gross<=W*H/12u);
    for(const auto& probe:probes) {
        CHECK(probe.x<W && probe.y<H);
        const size_t at=(probe.y*W+probe.x)*3u;
        CHECK(rgb_match(first.data+at,probe.r,probe.g,probe.b,3u));
        CHECK(rgb_match(oracle.data()+at,probe.r,probe.g,probe.b,3u));
        CHECK(rgb_match(legacy.data+at,probe.r,probe.g,probe.b,3u));
    }
    std::free(first.data);
    std::free(again.data);
    std::free(legacy.data);
    return EXIT_SUCCESS;
}

int negative_builtin_backend_rgb_v1(
    const fixture& sample,quantapdf_status expected,
    const quantapdf_native_source_raster_bridge_limits_v1& limits,
    const quantapdf_native_source_raster_device_v1& device)
{
    const auto original=original_pdf(sample);
    const auto *backend=quantapdf_builtin_backend_ops();
    void *document=nullptr,*page=nullptr;
    OK(backend->open_memory(
        original.data(),original.size(),nullptr,&document));
    OK(backend->load_page(document,0,&page));
    for(int trial=0;trial<2;++trial) {
        quantapdf_backend_bitmap refused;
        std::memset(&refused,0xa5,sizeof(refused));
        const auto status=quantapdf_builtin_native_qualify_bitmap_v1(
            page,&limits,&device,&refused);
        if(status!=expected)std::fprintf(stderr,
            "Native owned RGB refusal trial=%d expected=%d actual=%d\n",
            trial,(int)expected,(int)status);
        CHECK(status==expected);
        CHECK(refused.data==nullptr && refused.size==0u &&
              refused.width==0 && refused.height==0 &&
              refused.stride==0 && refused.components==0);
        quantapdf_rect bounds{};
        OK(backend->page_bounds(page,&bounds));
        CHECK(bounds.x1==(float)W && bounds.y1==(float)H);
    }
    backend->drop_page(page);
    backend->close(document);
    return EXIT_SUCCESS;
}

int builtin_native_backend_rgb_v1_suite()
{
    const std::string scope="q 12 6 8 9 re W n ";
    const std::string gray="0.25 g 12 6 4 4 re f ";
    const std::string image_in="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string image_out="q 8 0 0 6 22 16 cm /I Do Q ";
    const std::string outside="0.875 g 24 17 5 3 re f ";
    fixture two{},three{},four{},rgb{},split{};
    two.a=scope+gray+"Q "+image_out;
    three.a=scope+image_in+gray+"Q "+outside;
    four.a=scope+gray+image_in+"Q "+image_out+outside;
    rgb.a=scope+"0.25 0.5 0.75 rg 12 6 4 4 re f Q "+image_out;
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n 0.5 g 10 6 4 4 re f Q "+image_out;

    if(positive_builtin_backend_rgb_v1(two,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_builtin_backend_rgb_v1(three,
        {{13u,16u,64u,64u,64u},{13u,11u,255u,0u,0u},
         {26u,5u,223u,223u,223u}})!=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_builtin_backend_rgb_v1(four,
        {{13u,16u,0u,0u,255u},{26u,5u,223u,223u,223u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_builtin_backend_rgb_v1(rgb,
        {{13u,16u,64u,128u,191u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_builtin_backend_rgb_v1(split,
        {{13u,16u,128u,128u,128u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    const quantapdf_native_source_raster_bridge_limits_v1 limits={
        {source_limits.max_pdf_bytes,source_limits.max_content_bytes,
         source_limits.max_content_streams,4u,source_limits.max_page_pixels,
         source_limits.max_source_pixels,source_limits.max_drawn_pixels,
         source_limits.max_parent_steps},
        {W*H,4u,512u,8000000u,16u,4096u}
    };
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    fixture late{},masked{},missing{};
    late.a=two.a+" BT ET";
    masked.a=two.a;masked.mask=true;
    missing.a=two.a;missing.missing_resources=true;
    if(negative_builtin_backend_rgb_v1(
        late,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(negative_builtin_backend_rgb_v1(
        masked,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(negative_builtin_backend_rgb_v1(
        missing,QUANTAPDF_ERROR_FORMAT,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    auto reduced=limits;
    reduced.source.max_operations=1u;
    if(negative_builtin_backend_rgb_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,reduced,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    reduced=limits;
    reduced.raster.max_pixels=W*H-1u;
    if(negative_builtin_backend_rgb_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,reduced,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    auto wrong=device;
    wrong.dpi=144.0f;
    if(negative_builtin_backend_rgb_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,wrong)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    wrong=device;wrong.alpha=1;
    if(negative_builtin_backend_rgb_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,wrong)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    wrong=device;wrong.page_index=1u;
    if(negative_builtin_backend_rgb_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,wrong)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    quantapdf_backend_bitmap poisoned;
    std::memset(&poisoned,0xa5,sizeof(poisoned));
    CHECK(quantapdf_builtin_native_qualify_bitmap_v1(
        nullptr,&limits,&device,&poisoned)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(poisoned.data==nullptr && poisoned.size==0u &&
          poisoned.width==0 && poisoned.height==0 &&
          poisoned.stride==0 && poisoned.components==0);

    if(positive_builtin_backend_rgb_v1(two,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

/* True C facade Context+Page+Bitmap lifecycle, not a synthetic
 * backend-only handle and not a public native renderer cutover. */
/* Default source-conformance CTest and the dedicated Facade CTest
 * may execute in parallel in the SAME build directory. Use a PID-scoped
 * path so separate processes never overwrite each other's original PDF. */
std::string facade_fixture_path()
{
#if defined(_WIN32)
    const unsigned long process=static_cast<unsigned long>(_getpid());
#else
    const unsigned long process=static_cast<unsigned long>(getpid());
#endif
    return "quantapdf-private-native-facade-v1-"+
        std::to_string(process)+".pdf";
}

int write_facade_original(const std::vector<unsigned char>& bytes)
{
    const std::string name=facade_fixture_path();
    std::FILE *file=std::fopen(name.c_str(),"wb");
    if(!file)return EXIT_FAILURE;
    const size_t written=std::fwrite(bytes.data(),1,bytes.size(),file);
    const int closed=std::fclose(file);
    return written==bytes.size() && closed==0 ?
        EXIT_SUCCESS : EXIT_FAILURE;
}

int positive_private_native_facade_v1(
    const fixture& sample,const std::vector<v6_probe>& probes,
    bool decode_v9=false)
{
    auto original=original_pdf(sample);
    CHECK(original.size()>100u);
    const auto direct=pdfium_original_rgb(original);
    CHECK(direct.size()==W*H*3u);
    CHECK(write_facade_original(original)==EXIT_SUCCESS);

    quantapdf_context *context=nullptr;
    quantapdf_document *document=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *native=nullptr,*pdfium=nullptr,*legacy_native=nullptr;
    OK(quantapdf_context_create(&context));
    CHECK(context!=nullptr && context->references==1u);
    OK(quantapdf_open_with_context(
        context,facade_fixture_path().c_str(),
        nullptr,&document));
    std::remove(facade_fixture_path().c_str());
    CHECK(document!=nullptr && context->references==2u);
    OK(quantapdf_load_page(document,0,&page));
    CHECK(page!=nullptr && context->references==3u);

    const quantapdf_native_source_raster_bridge_limits_v1 bounds={
        {source_limits.max_pdf_bytes,source_limits.max_content_bytes,
         source_limits.max_content_streams,4u,source_limits.max_page_pixels,
         source_limits.max_source_pixels,source_limits.max_drawn_pixels,
         source_limits.max_parent_steps},
        {W*H,4u,512u,8000000u,16u,4096u}
    };
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    /* The product-linked V8 path must still fail closed when EITHER
     * independent XObject carries an explicit /Decode. */
    if(decode_v9 &&
       (!sample.image_decode.empty() ||
        !sample.second_image_decode.empty())) {
        quantapdf_bitmap *old=reinterpret_cast<quantapdf_bitmap *>(
            static_cast<uintptr_t>(1u));
        CHECK(quantapdf_render_page_native_qualified_v1(
            page,&bounds,&device,&old)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(old==nullptr && context->references==3u);
    }
#if defined(QUANTAPDF_TESTING)
    /* Fault injection belongs to a VALID source, page and device: an
     * unqualified page is rightly rejected before allocation and
     * must not be mislabeled as NOMEM. On this valid input the one-shot
     * Context allocator failure must not retain or publish anything. */
    {
        const size_t refs=context->references;
        quantapdf_bitmap *refused=reinterpret_cast<quantapdf_bitmap *>(
            static_cast<uintptr_t>(1u));
        context->test_fail_allocation=1;
        const quantapdf_status refused_status=decode_v9 ?
            quantapdf_render_page_native_qualified_v9(
                page,&bounds,&device,&refused) :
            quantapdf_render_page_native_qualified_v1(
                page,&bounds,&device,&refused);
        CHECK(refused_status==QUANTAPDF_ERROR_NOMEM);
        CHECK(refused==nullptr && context->references==refs &&
              context->test_fail_allocation==0);
    }
#endif
    const quantapdf_status accepted=decode_v9 ?
        quantapdf_render_page_native_qualified_v9(
            page,&bounds,&device,&native) :
        quantapdf_render_page_native_qualified_v1(
            page,&bounds,&device,&native);
    OK(accepted);
    CHECK(native!=nullptr && context->references==4u);
    /* Absent /Decode is byte-identical through the ORIGINAL V8 path
     * and V9; both bitmaps outlive Page, Document, and caller Context. */
    if(decode_v9 && sample.image_decode.empty() &&
       sample.second_image_decode.empty()) {
        OK(quantapdf_render_page_native_qualified_v1(
            page,&bounds,&device,&legacy_native));
        CHECK(legacy_native!=nullptr && context->references==5u);
    }
    /* Existing public facade is STILL PDFium-backed, independent of
     * the opt-in native qualification and pixel comparison. */
    OK(quantapdf_render_page(page,&pdfium));
    CHECK(pdfium!=nullptr && context->references==
          (legacy_native==nullptr ? 5u : 6u));

    /* Teardown the complete facade-owned source tree and the caller's
     * Context reference BEFORE consuming the two detached bitmaps. */
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_context(context);
    original.assign(original.size(),0u);
    original.clear();
    CHECK(native->context==pdfium->context &&
          native->context->references==
              (legacy_native==nullptr ? 2u : 3u));
    int w=0,h=0,stride=0,components=0;
    OK(quantapdf_bitmap_dimensions(
        native,&w,&h,&stride,&components));
    CHECK(w==(int)W && h==(int)H &&
          components==3 && stride==(int)(W*3u));
    int lw=0,lh=0,ls=0,lc=0;
    OK(quantapdf_bitmap_dimensions(
        pdfium,&lw,&lh,&ls,&lc));
    CHECK(lw==w && lh==h && ls==stride && lc==components);
    const unsigned char *pixels=nullptr,*legacy=nullptr;
    size_t size=0,legacy_size=0;
    OK(quantapdf_bitmap_data(native,&pixels,&size));
    OK(quantapdf_bitmap_data(pdfium,&legacy,&legacy_size));
    CHECK(pixels!=nullptr && legacy!=nullptr &&
          size==W*H*3u && legacy_size==size);
    if(legacy_native!=nullptr) {
        int pw=0,ph=0,ps=0,pc=0;
        const unsigned char *prior=nullptr;
        size_t prior_size=0u;
        OK(quantapdf_bitmap_dimensions(
            legacy_native,&pw,&ph,&ps,&pc));
        OK(quantapdf_bitmap_data(legacy_native,&prior,&prior_size));
        CHECK(pw==w && ph==h && ps==stride && pc==components &&
              prior!=nullptr && prior_size==size &&
              std::memcmp(prior,pixels,size)==0);
    }
    double native_error=0.0,legacy_error=0.0;
    size_t gross=0u;
    for(size_t i=0u;i<W*H;++i) {
        unsigned peak=0u;
        for(size_t c=0u;c<3u;++c) {
            const size_t at=i*3u+c;
            const unsigned a=pixels[at],b=direct[at],d=legacy[at];
            const unsigned diff=a>b?a-b:b-a;
            const unsigned old=a>d?a-d:d-a;
            native_error+=(double)diff;
            legacy_error+=(double)old;
            if(diff>peak)peak=diff;
        }
        if(peak>64u)++gross;
    }
    CHECK(native_error/(double)(W*H*3u)<=8.0 &&
          legacy_error/(double)(W*H*3u)<=8.0 &&
          gross<=W*H/12u);
    for(const auto& p:probes){
        CHECK(p.x<W && p.y<H);
        const size_t at=(p.y*W+p.x)*3u;
        CHECK(rgb_match(pixels+at,p.r,p.g,p.b,3u));
        CHECK(rgb_match(legacy+at,p.r,p.g,p.b,3u));
        CHECK(rgb_match(direct.data()+at,p.r,p.g,p.b,3u));
    }
    quantapdf_drop_bitmap(pdfium);
    CHECK(native->context->references==
          (legacy_native==nullptr ? 1u : 2u));
    if(legacy_native!=nullptr) {
        quantapdf_drop_bitmap(legacy_native);
        CHECK(native->context->references==1u);
    }
    quantapdf_drop_bitmap(native);
    return EXIT_SUCCESS;
}

int negative_private_native_facade_v1(
    const fixture& source,quantapdf_status expected,
    const quantapdf_native_source_raster_bridge_limits_v1& limits,
    const quantapdf_native_source_raster_device_v1& device,
    bool decode_v9=false)
{
    const auto bytes=original_pdf(source);
    CHECK(write_facade_original(bytes)==EXIT_SUCCESS);
    quantapdf_context *context=nullptr;
    quantapdf_document *document=nullptr;
    quantapdf_page *page=nullptr;
    OK(quantapdf_context_create(&context));
    OK(quantapdf_open_with_context(context,
        facade_fixture_path().c_str(),nullptr,&document));
    std::remove(facade_fixture_path().c_str());
    OK(quantapdf_load_page(document,0,&page));
    const size_t prior=context->references;
    for(int n=0;n<2;++n) {
        quantapdf_bitmap *out=reinterpret_cast<quantapdf_bitmap *>(
            static_cast<uintptr_t>(1u));
        const auto actual=decode_v9 ?
            quantapdf_render_page_native_qualified_v9(
                page,&limits,&device,&out) :
            quantapdf_render_page_native_qualified_v1(
                page,&limits,&device,&out);
        if(actual!=expected)
            std::fprintf(stderr,
                "Native facade refusal trial=%d expected=%d got=%d\n",
                n,(int)expected,(int)actual);
        CHECK(actual==expected && out==nullptr &&
              context->references==prior);
        quantapdf_rect bounds{};
        OK(quantapdf_page_bounds(page,&bounds));
        CHECK(bounds.x1==(float)W && bounds.y1==(float)H);
    }
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_context(context);
    return EXIT_SUCCESS;
}

int private_native_facade_bitmap_v1_suite()
{
    const std::string scope="q 12 6 8 9 re W n ";
    const std::string gray="0.25 g 12 6 4 4 re f ";
    const std::string img_inside="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string img_outside="q 8 0 0 6 22 16 cm /I Do Q ";
    const std::string outside="0.875 g 24 17 5 3 re f ";
    fixture two{},three{},four{},rgb{},split{};
    two.a=scope+gray+"Q "+img_outside;
    three.a=scope+img_inside+gray+"Q "+outside;
    four.a=scope+gray+img_inside+"Q "+img_outside+outside;
    rgb.a=scope+"0.25 0.5 0.75 rg 12 6 4 4 re f Q "+img_outside;
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n 0.5 g 10 6 4 4 re f Q "+img_outside;
    if(positive_private_native_facade_v1(two,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_private_native_facade_v1(three,
        {{13u,16u,64u,64u,64u},{13u,11u,255u,0u,0u},
         {26u,5u,223u,223u,223u}})!=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_private_native_facade_v1(four,
        {{13u,16u,0u,0u,255u},{26u,5u,223u,223u,223u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_private_native_facade_v1(rgb,
        {{13u,16u,64u,128u,191u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    if(positive_private_native_facade_v1(split,
        {{13u,16u,128u,128u,128u},{23u,3u,255u,0u,0u}})
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    const quantapdf_native_source_raster_bridge_limits_v1 limits={
        {source_limits.max_pdf_bytes,source_limits.max_content_bytes,
         source_limits.max_content_streams,4u,source_limits.max_page_pixels,
         source_limits.max_source_pixels,source_limits.max_drawn_pixels,
         source_limits.max_parent_steps},
        {W*H,4u,512u,8000000u,16u,4096u}
    };
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    fixture late{},masked{},missing{};
    late.a=two.a+" BT ET";
    masked.a=two.a;masked.mask=true;
    missing.a=two.a;missing.missing_resources=true;
    if(negative_private_native_facade_v1(
        late,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(negative_private_native_facade_v1(
        masked,QUANTAPDF_ERROR_UNSUPPORTED,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(negative_private_native_facade_v1(
        missing,QUANTAPDF_ERROR_FORMAT,limits,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    auto narrow=limits;
    narrow.source.max_operations=1u;
    if(negative_private_native_facade_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,narrow,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    narrow=limits;narrow.raster.max_pixels=W*H-1u;
    if(negative_private_native_facade_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,narrow,device)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    auto altered=device;
    altered.dpi=144.0f;
    if(negative_private_native_facade_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,altered)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    altered=device;altered.alpha=1;
    if(negative_private_native_facade_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,altered)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    altered=device;altered.page_index=1u;
    if(negative_private_native_facade_v1(
        two,QUANTAPDF_ERROR_UNSUPPORTED,limits,altered)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    quantapdf_bitmap *out=reinterpret_cast<quantapdf_bitmap *>(
        static_cast<uintptr_t>(1u));
    CHECK(quantapdf_render_page_native_qualified_v1(
        nullptr,&limits,&device,&out)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(out==nullptr);
    CHECK(quantapdf_render_page_native_qualified_v1(
        nullptr,&limits,&device,nullptr)==QUANTAPDF_ERROR_ARGUMENT);
    return positive_private_native_facade_v1(two,
        {{13u,16u,64u,64u,64u},{23u,3u,255u,0u,0u}});
}


/* ISSUE #459 — the REAL product-linked private Context/Page/Bitmap path.
 * This is not the Source-only oracle: each original is opened through
 * quantapdf_open_with_context, and PDFium runs independently only as a
 * test oracle / unchanged public render dispatch. */
int private_native_facade_bitmap_v9_suite()
{
    const std::string clip="q 12 6 8 9 re W n ";
    const std::string image_in="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string image_out="q 8 0 0 6 22 16 cm /I Do Q ";
    const std::string gray_path="0.25 g 12 6 4 4 re f ";
    const std::string restored_path="0.875 g 24 17 5 3 re f ";

    fixture inverted{};
    inverted.a=clip+image_in+"Q "+restored_path;
    inverted.image_decode="[1 0 1 0 1 0]";
    if(positive_private_native_facade_v1(inverted,
        {{13u,11u,0u,255u,255u},{18u,11u,255u,0u,255u},
         {13u,16u,255u,255u,0u},{18u,16u,0u,0u,255u},
         {26u,5u,223u,223u,223u}},true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    fixture selective=inverted;
    selective.image_decode="[1 0 0 1 1 0]";
    if(positive_private_native_facade_v1(selective,
        {{13u,11u,0u,0u,255u},{18u,11u,255u,255u,255u},
         {13u,16u,255u,0u,0u},{18u,16u,0u,255u,255u}},true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture explicit_identity=inverted;
    explicit_identity.image_decode="[0 1 0 1 0 1]";
    if(positive_private_native_facade_v1(explicit_identity,
        {{13u,11u,255u,0u,0u},{18u,11u,0u,255u,0u},
         {13u,16u,0u,0u,255u},{18u,16u,255u,255u,0u}},true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    /* No explicit Decode: V8 and V9 actual Context-owned bitmaps must
     * compare byte-for-byte AFTER all original source owners are gone. */
    fixture implicit_identity=explicit_identity;
    implicit_identity.image_decode.clear();
    if(positive_private_native_facade_v1(implicit_identity,
        {{13u,11u,255u,0u,0u},{18u,11u,0u,255u,0u},
         {13u,16u,0u,0u,255u}},true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    fixture gray=inverted;
    gray.gray_image=true;
    gray.image_decode="[1 0]";
    if(positive_private_native_facade_v1(gray,
        {{13u,11u,255u,255u,255u},{18u,11u,170u,170u,170u},
         {13u,16u,85u,85u,85u},{18u,16u,0u,0u,0u}},true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    gray.image_decode="[0 1]";
    if(positive_private_native_facade_v1(gray,
        {{13u,11u,0u,0u,0u},{18u,11u,85u,85u,85u},
         {13u,16u,170u,170u,170u},{18u,16u,255u,255u,255u}},true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture three=inverted;
    three.a=clip+image_in+gray_path+"Q "+restored_path;
    if(positive_private_native_facade_v1(three,
        {{13u,16u,64u,64u,64u},{18u,11u,255u,0u,255u},
         {26u,5u,223u,223u,223u}},true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    /* Two ACTUALLY DISTINCT image dictionaries, source order and
     * independent Decode semantics across a real Q clip restore. */
    fixture heterogeneous=inverted;
    heterogeneous.second_image=true;
    heterogeneous.second_image_gray=true;
    heterogeneous.second_image_decode="[0 1]";
    heterogeneous.a=clip+gray_path+image_in+"Q "+
        "q 8 0 0 6 22 16 cm /J Do Q "+restored_path;
    if(positive_private_native_facade_v1(heterogeneous,
        {{13u,11u,0u,255u,255u},{13u,16u,255u,255u,0u},
         {23u,3u,0u,0u,0u},{27u,3u,85u,85u,85u},
         {23u,6u,170u,170u,170u},{26u,5u,223u,223u,223u}},
        true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Explicit inversion only in /J, while /I remains implicit identity.
     * V8 must fail despite earlier valid Path and /I paints. */
    fixture second_only=heterogeneous;
    second_only.image_decode.clear();
    second_only.second_image_decode="[1 0]";
    if(positive_private_native_facade_v1(second_only,
        {{13u,11u,255u,0u,0u},{13u,16u,0u,0u,255u},
         {23u,3u,255u,255u,255u},{27u,3u,170u,170u,170u},
         {23u,6u,85u,85u,85u},{26u,5u,223u,223u,223u}},
        true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* A real W* token split across Contents with translated Clip
     * definition and Q restoration (independent original oracle). */
    fixture split=inverted;
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n q 16 0 0 12 6 3 cm /I Do Q Q "+restored_path;
    if(positive_private_native_facade_v1(split,
        {{13u,11u,0u,255u,255u},{13u,16u,255u,255u,0u},
         {26u,5u,223u,223u,223u}},true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    const quantapdf_native_source_raster_bridge_limits_v1 limits={
        {source_limits.max_pdf_bytes,source_limits.max_content_bytes,
         source_limits.max_content_streams,4u,source_limits.max_page_pixels,
         source_limits.max_source_pixels,source_limits.max_drawn_pixels,
         source_limits.max_parent_steps},
        {W*H,4u,512u,8000000u,16u,4096u}
    };
    const quantapdf_native_source_raster_device_v1 device={
        0u,72.0f,0.0f,0,0,0
    };
    fixture wrong_second=heterogeneous;
    wrong_second.second_image_decode="[0 1 0 1 0 1]";
    if(negative_private_native_facade_v1(wrong_second,
        QUANTAPDF_ERROR_UNSUPPORTED,limits,device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture malformed=inverted;
    malformed.image_decode="[0 1 0 1 0 1.0001]";
    if(negative_private_native_facade_v1(malformed,
        QUANTAPDF_ERROR_UNSUPPORTED,limits,device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture masked=inverted;
    masked.mask=true;
    if(negative_private_native_facade_v1(masked,
        QUANTAPDF_ERROR_UNSUPPORTED,limits,device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture missing=inverted;
    missing.missing_resources=true;
    if(negative_private_native_facade_v1(missing,
        QUANTAPDF_ERROR_FORMAT,limits,device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    fixture late=inverted;
    late.a+=" BT ET";
    if(negative_private_native_facade_v1(late,
        QUANTAPDF_ERROR_UNSUPPORTED,limits,device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    auto reduced=limits;
    reduced.source.max_source_pixels=3u;
    if(negative_private_native_facade_v1(inverted,
        QUANTAPDF_ERROR_UNSUPPORTED,reduced,device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    reduced=limits;
    reduced.source.max_operations=1u;
    if(negative_private_native_facade_v1(inverted,
        QUANTAPDF_ERROR_UNSUPPORTED,reduced,device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    reduced=limits;
    reduced.raster.max_pixels=W*H-1u;
    if(negative_private_native_facade_v1(inverted,
        QUANTAPDF_ERROR_UNSUPPORTED,reduced,device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    auto wrong_device=device;
    wrong_device.dpi=144.0f;
    if(negative_private_native_facade_v1(inverted,
        QUANTAPDF_ERROR_UNSUPPORTED,limits,wrong_device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    wrong_device=device;wrong_device.alpha=1;
    if(negative_private_native_facade_v1(inverted,
        QUANTAPDF_ERROR_UNSUPPORTED,limits,wrong_device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    wrong_device=device;wrong_device.page_index=1u;
    if(negative_private_native_facade_v1(inverted,
        QUANTAPDF_ERROR_UNSUPPORTED,limits,wrong_device,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    quantapdf_bitmap *poison=reinterpret_cast<quantapdf_bitmap *>(
        static_cast<uintptr_t>(1u));
    CHECK(quantapdf_render_page_native_qualified_v9(
        nullptr,&limits,&device,&poison)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(poison==nullptr);
    CHECK(quantapdf_render_page_native_qualified_v9(
        nullptr,&limits,&device,nullptr)==QUANTAPDF_ERROR_ARGUMENT);

    /* Post-refusal recovery: valid V9 input still renders identically. */
    return positive_private_native_facade_v1(inverted,
        {{13u,11u,0u,255u,255u},{26u,5u,223u,223u,223u}},true);
}

/* Independent Source V10 original-PDF QPDF writer / test-only PDFium
 * oracle; all Flate PNG/TIFF predictor decoding is owned by the existing
 * QPDF stream pipeline, not a new native raster implementation. */
int source_scoped_mixed_v10_flate_predictor_suite()
{
    const std::string clip="q 12 6 8 9 re W n ";
    const std::string image_in="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string path_in="0.25 g 12 6 4 4 re f ";
    const std::string path_out="0.875 g 24 17 5 3 re f ";
    auto bounds=source_limits;
    bounds.max_operations=4u;

    /* 1=identity, 2=TIFF horizontal, PNG 10..14 fixed row algorithm,
     * 15=adaptive Sub/Paeth mixed rows (tag byte on EVERY row). */
    const int modes[]={1,2,10,11,12,13,14,15};
    for(const int pred:modes) {
        fixture sample{};
        sample.a=clip+image_in+"Q "+path_out;
        sample.image_predictor=pred;
        sample.image_decode_parms=test_predictor_parms(pred,3);
        if(positive_scoped_mixed_v6(sample,1u,1u,1u,
            {{13u,11u,255u,0u,0u},{18u,11u,0u,255u,0u},
             {13u,16u,0u,0u,255u},{18u,16u,255u,255u,0u},
             {26u,5u,223u,223u,223u}},
            false,false,false,false,true)!=EXIT_SUCCESS)
            return EXIT_FAILURE;
    }

    /* Reconstruction MUST happen before the exact V9 /Decode polarity
     * mapping. R and B invert independently, G remains unchanged. */
    fixture inverted{};
    inverted.a=clip+image_in+"Q "+path_out;
    inverted.image_predictor=15;
    inverted.image_decode_parms=test_predictor_parms(15,3);
    inverted.image_decode="[1 0 0 1 1 0]";
    if(positive_scoped_mixed_v6(inverted,1u,1u,1u,
        {{13u,11u,0u,0u,255u},{18u,11u,255u,255u,255u},
         {13u,16u,255u,0u,0u},{18u,16u,0u,255u,255u}},
        false,false,false,false,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    for(const int pred:{2,12}) {
        fixture gray=inverted;
        gray.gray_image=true;
        gray.image_predictor=pred;
        gray.image_decode_parms=test_predictor_parms(pred,1);
        gray.image_decode="[1 0]";
        if(positive_scoped_mixed_v6(gray,1u,1u,1u,
            {{13u,11u,255u,255u,255u},
             {18u,11u,170u,170u,170u},
             {13u,16u,85u,85u,85u},
             {18u,16u,0u,0u,0u}},
            false,false,false,false,true)!=EXIT_SUCCESS)
            return EXIT_FAILURE;
    }

    fixture three=inverted;
    three.a=clip+image_in+path_in+"Q "+path_out;
    if(positive_scoped_mixed_v6(three,2u,2u,1u,
        {{13u,16u,64u,64u,64u},{18u,11u,255u,255u,255u},
         {26u,5u,223u,223u,223u}},
        false,false,false,false,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    /* Two independent Image XObject dictionaries with different Flate
     * predictors and Decode polarity after actual Q clip restore. */
    fixture dual=inverted;
    dual.second_image=true;
    dual.second_image_gray=true;
    dual.second_image_predictor=2;
    dual.second_image_decode_parms=test_predictor_parms(2,1);
    dual.second_image_decode="[1 0]";
    dual.a=clip+path_in+image_in+"Q "+
        "q 8 0 0 6 22 16 cm /J Do Q "+path_out;
    if(positive_scoped_mixed_v6(dual,2u,2u,2u,
        {{13u,11u,0u,0u,255u},{13u,16u,255u,0u,0u},
         {23u,3u,255u,255u,255u},{27u,3u,170u,170u,170u},
         {23u,6u,85u,85u,85u},{26u,5u,223u,223u,223u}},
        false,false,false,false,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    /* Original W* token is split across Contents streams. Definition
     * and paint CTM are distinct; Q restores the unmasked outside path. */
    fixture split=inverted;
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n q 16 0 0 12 6 3 cm /I Do Q Q "+path_out;
    if(positive_scoped_mixed_v6(split,1u,1u,1u,
        {{13u,11u,0u,0u,255u},{13u,16u,255u,0u,0u},
         {26u,5u,223u,223u,223u}},
        false,false,false,false,true)!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    /* Dictionary shape, row count and channel metadata are independently
     * validated BEFORE QPDF may construct its predictor row buffer. */
    const std::string bad_parms[]={
        "42",
        "[]",
        "<< >>",
        "<< /Predictor 99 /Colors 3 /Columns 2 /BitsPerComponent 8 >>",
        "<< /Predictor 9 /Colors 3 /Columns 2 /BitsPerComponent 8 >>",
        "<< /Predictor 12 /Colors 1 /Columns 2 /BitsPerComponent 8 >>",
        "<< /Predictor 12 /Colors 3 /Columns 1 /BitsPerComponent 8 >>",
        "<< /Predictor 12 /Colors 3 /Columns -1 /BitsPerComponent 8 >>",
        "<< /Predictor 12 /Colors 3 /Columns 2 /BitsPerComponent 16 >>",
        "<< /Predictor /Up /Colors 3 /Columns 2 /BitsPerComponent 8 >>",
        "<< /Predictor 12 /Colors 3 /Columns 2 /BitsPerComponent 8 /EarlyChange 1 >>",
        "<< /Predictor 12 /Colors 3 /Columns 4194305 /BitsPerComponent 8 >>"
    };
    for(const auto& invalid:bad_parms) {
        fixture bad=inverted;
        bad.image_decode_parms=invalid;
        if(negative_scoped_mixed_v6(bad,
            QUANTAPDF_ERROR_UNSUPPORTED,bounds,false,false,false,true)
            !=EXIT_SUCCESS)return EXIT_FAILURE;
    }
    /* The SECOND Image/Gray resource must not evade preflight because
     * the first Image was valid. No partially published output. */
    fixture secondary_bad=dual;
    secondary_bad.second_image_decode_parms=test_predictor_parms(12,3);
    if(negative_scoped_mixed_v6(secondary_bad,
        QUANTAPDF_ERROR_UNSUPPORTED,bounds,false,false,false,true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture raw_parms=inverted;
    raw_parms.image_predictor=0;
    if(negative_scoped_mixed_v6(raw_parms,
        QUANTAPDF_ERROR_UNSUPPORTED,bounds,false,false,false,true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture masked=inverted;
    masked.mask=true;
    if(negative_scoped_mixed_v6(masked,
        QUANTAPDF_ERROR_UNSUPPORTED,bounds,false,false,false,true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture late=inverted;
    late.a+=" BT ET";
    if(negative_scoped_mixed_v6(late,
        QUANTAPDF_ERROR_UNSUPPORTED,bounds,false,false,false,true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture missing=inverted;
    missing.missing_resources=true;
    if(negative_scoped_mixed_v6(missing,
        QUANTAPDF_ERROR_FORMAT,bounds,false,false,false,true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    auto reduced=bounds;
    reduced.max_source_pixels=3u;
    if(negative_scoped_mixed_v6(inverted,
        QUANTAPDF_ERROR_UNSUPPORTED,reduced,false,false,false,true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;
    reduced=bounds;
    reduced.max_operations=1u;
    if(negative_scoped_mixed_v6(inverted,
        QUANTAPDF_ERROR_UNSUPPORTED,reduced,false,false,false,true)
        !=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Recovery after failed second-image admission uses the same strict
     * decoder and owner pipeline with the complete original PDF. */
    return positive_scoped_mixed_v6(dual,2u,2u,2u,
        {{13u,11u,0u,0u,255u},{23u,3u,255u,255u,255u},
         {26u,5u,223u,223u,223u}},
        false,false,false,false,true);
}

/* Private V9 qualifies explicit 8-bit Image /Decode per channel:
 * each pair is EXACT [0 1] or [1 0]. Real original PDF /Decode
 * semantics are compared with independent direct-PDFium pixels;
 * normalization produces only owned canonical RAW Gray/RGB bytes. */
int source_scoped_mixed_v9_image_decode_suite()
{
    auto bounds=source_limits;
    bounds.max_operations=4u;
    const std::string clip="q 12 6 8 9 re W n ";
    const std::string image_inside="q 16 0 0 12 8 3 cm /I Do Q ";
    const std::string image_outside="q 8 0 0 6 22 16 cm /I Do Q ";
    const std::string path_inside="0.25 g 12 6 4 4 re f ";
    const std::string path_outside="0.875 g 24 17 5 3 re f ";

    /* Invert all 3 RGB channels: RED -> CYAN, GREEN -> MAGENTA,
     * BLUE -> YELLOW, YELLOW -> BLUE. The original image dictionary
     * retains /Decode and PDFium receives the unchanged original. */
    fixture inverted{};
    inverted.a=clip+image_inside+"Q "+path_outside;
    inverted.image_decode="[1 0 1 0 1 0]";
    if(positive_scoped_mixed_v6(inverted,1u,1u,1u,
        {{13u,11u,0u,255u,255u},{18u,11u,255u,0u,255u},
         {13u,16u,255u,255u,0u},{18u,16u,0u,0u,255u},
         {26u,5u,223u,223u,223u},{23u,3u,255u,255u,255u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Identity array is NOT ignored by legacy source V8, even though
     * correctly normalized V9 pixels equal an absent Decode array. */
    fixture identity=inverted;
    identity.image_decode="[0 1 0 1 0 1]";
    if(positive_scoped_mixed_v6(identity,1u,1u,1u,
        {{13u,11u,255u,0u,0u},{18u,11u,0u,255u,0u},
         {13u,16u,0u,0u,255u},{18u,16u,255u,255u,0u},
         {26u,5u,223u,223u,223u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Independent RGB channel pair decisions, not a whole-RGB invert:
     * R and B inverted, G left unchanged. */
    fixture selected=inverted;
    selected.image_decode="[1 0 0 1 1 0]";
    if(positive_scoped_mixed_v6(selected,1u,1u,1u,
        {{13u,11u,0u,0u,255u},{18u,11u,255u,255u,255u},
         {13u,16u,255u,0u,0u},{18u,16u,0u,255u,255u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Exercise all remaining RGB inversion bitmasks independently.
     * Channel positions, not a whole-image invert switch, determine
     * the output. PDFium receives each unchanged original /Decode. */
    struct decode_pattern {
        const char *decode;
        std::vector<v6_probe> probes;
    };
    const decode_pattern remaining_patterns[]={
        {"[1 0 0 1 0 1]",
         {{13u,11u,0u,0u,0u},{18u,11u,255u,255u,0u},
          {13u,16u,255u,0u,255u},{18u,16u,0u,255u,0u}}},
        {"[0 1 1 0 0 1]",
         {{13u,11u,255u,255u,0u},{18u,11u,0u,0u,0u},
          {13u,16u,0u,255u,255u},{18u,16u,255u,0u,0u}}},
        {"[0 1 0 1 1 0]",
         {{13u,11u,255u,0u,255u},{18u,11u,0u,255u,255u},
          {13u,16u,0u,0u,0u},{18u,16u,255u,255u,255u}}},
        {"[1 0 1 0 0 1]",
         {{13u,11u,0u,255u,0u},{18u,11u,255u,0u,0u},
          {13u,16u,255u,255u,255u},{18u,16u,0u,0u,0u}}},
        {"[0 1 1 0 1 0]",
         {{13u,11u,255u,255u,255u},{18u,11u,0u,0u,255u},
          {13u,16u,0u,255u,0u},{18u,16u,255u,0u,255u}}}
    };
    for(const auto& pattern:remaining_patterns) {
        fixture variant=inverted;
        variant.image_decode=pattern.decode;
        if(positive_scoped_mixed_v6(variant,1u,1u,1u,pattern.probes,
            false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    }

    /* Integer and real PDF number spellings with identical numeric
     * endpoints must decode identically, without fuzzy acceptance. */
    fixture decimal_identity=identity;
    decimal_identity.image_decode="[0.0 1.0 0 1.0 0 1]";
    if(positive_scoped_mixed_v6(decimal_identity,1u,1u,1u,
        {{13u,11u,255u,0u,0u},{18u,11u,0u,255u,0u},
         {13u,16u,0u,0u,255u},{18u,16u,255u,255u,0u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* The same normalized Image Do participates in strict ordered
     * three/four layer mixes, with overpainted Path/RAW samples. */
    fixture three=inverted;
    three.a=clip+image_inside+path_inside+"Q "+path_outside;
    if(positive_scoped_mixed_v6(three,2u,2u,1u,
        {{13u,16u,64u,64u,64u},{18u,11u,255u,0u,255u},
         {26u,5u,223u,223u,223u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    fixture four=inverted;
    four.a=clip+path_inside+image_inside+"Q "+
        image_outside+path_outside;
    if(positive_scoped_mixed_v6(four,2u,2u,2u,
        {{13u,11u,0u,255u,255u},{13u,16u,255u,255u,0u},
         {23u,3u,0u,255u,255u},{26u,5u,223u,223u,223u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* A different Image XObject /J has its OWN Decode and sample layout.
     * Source order is: clipped Path, clipped /I RGB, restored /J Gray,
     * restored Path. This detects accidental sharing of the /I mapping
     * across the /J dictionary, not just repeated Do on one resource. */
    fixture heterogeneous=four;
    heterogeneous.second_image=true;
    heterogeneous.second_image_gray=true;
    heterogeneous.second_image_decode="[0 1]";
    heterogeneous.a=clip+path_inside+image_inside+"Q "+
        "q 8 0 0 6 22 16 cm /J Do Q "+path_outside;
    if(positive_scoped_mixed_v6(heterogeneous,2u,2u,2u,
        {{13u,11u,0u,255u,255u},{13u,16u,255u,255u,0u},
         {23u,3u,0u,0u,0u},{27u,3u,85u,85u,85u},
         {23u,6u,170u,170u,170u},{26u,5u,223u,223u,223u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Inversion only on /J must still reject the whole source in V8,
     * while V9 retains implicit identity on /I. */
    fixture secondary_only=heterogeneous;
    secondary_only.image_decode.clear();
    secondary_only.second_image_decode="[1 0]";
    if(positive_scoped_mixed_v6(secondary_only,2u,2u,2u,
        {{13u,11u,255u,0u,0u},{13u,16u,0u,0u,255u},
         {23u,3u,255u,255u,255u},{27u,3u,170u,170u,170u},
         {23u,6u,85u,85u,85u},{26u,5u,223u,223u,223u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* DeviceGray is one component, with a separate 2-number mapping.
     * 2x2 RAW samples [0,85,170,255] become [255,170,85,0]. */
    fixture gray=inverted;
    gray.gray_image=true;
    gray.image_decode="[1 0]";
    if(positive_scoped_mixed_v6(gray,1u,1u,1u,
        {{13u,11u,255u,255u,255u},{18u,11u,170u,170u,170u},
         {13u,16u,85u,85u,85u},{18u,16u,0u,0u,0u},
         {26u,5u,223u,223u,223u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    gray.image_decode="[0 1]";
    if(positive_scoped_mixed_v6(gray,1u,1u,1u,
        {{13u,11u,0u,0u,0u},{18u,11u,85u,85u,85u},
         {13u,16u,170u,170u,170u},{18u,16u,255u,255u,255u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Even/odd original W* with definition-time translation, Content
     * tokens split across streams and a genuine Q unclip. */
    fixture split=inverted;
    split.split=true;
    split.a="q 1 0 0 1 2 0 cm 10 6 8 9 re W*";
    split.b=" n q 16 0 0 12 6 3 cm /I Do Q Q "+path_outside;
    if(positive_scoped_mixed_v6(split,1u,1u,1u,
        {{13u,11u,0u,255u,255u},{13u,16u,255u,255u,0u},
         {26u,5u,223u,223u,223u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Explicit identity /Decode and omitted /Decode must produce byte-
     * identical native RGBA while the old V8 identity still refuses. */
    fixture absent=identity;
    absent.image_decode.clear();
    const auto src8=original_pdf(absent),src9=original_pdf(identity);
    quantapdf_ir ir8{},ir9{};
    OK(quantapdf_qpdf_source_scoped_mixed_v8_to_ir(
        src8.data(),src8.size(),&bounds,&ir8));
    OK(quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir(
        src9.data(),src9.size(),&bounds,&ir9));
    const quantapdf_ir_raster_scoped_mixed_v5_limits raster_limits={
        W*H,4u,512u,8000000u,16u,4096u
    };
    quantapdf_ir_raster_image render8{},render9{};
    OK(quantapdf_ir_raster_scoped_mixed_v5(
        &ir8,0u,&raster_limits,&render8));
    OK(quantapdf_ir_raster_scoped_mixed_v5(
        &ir9,0u,&raster_limits,&render9));
    CHECK(render8.size==render9.size && render8.size==W*H*4u &&
          std::memcmp(render8.pixels,render9.pixels,render8.size)==0);
    quantapdf_ir_raster_image_dispose(&render8);
    quantapdf_ir_raster_image_dispose(&render9);
    quantapdf_ir_dispose(&ir8);
    quantapdf_ir_dispose(&ir9);

    /* Strict refusals AFTER a source image would otherwise be accepted.
     * No implicit interpolation, clamping, nonbinary ramp, bad cardinality,
     * missing resource, alpha/mask, or post-image text is allowed. */
    const std::string bad_decode[]={
        "true",            /* a present scalar cannot substitute for array */
        "[]",              /* empty array is not implicit identity */
        "[0 1 0 1 0 1.0001]", /* near-one must not be rounded */
        "[0 1 0 1 0]",      /* wrong RGB channel count */
        "[0 1 0 1 0 1 0 1]",
        "[0 1 0 1 0 0.5]",  /* unsupported fractional ramp */
        "[1 1 0 1 0 1]",    /* constant channel */
        "[-1 1 0 1 0 1]",  /* out of range */
        "[0 1 0 1 0 /None]", /* nonnumeric */
        "42"               /* nonarray */
    };
    for(const auto& d:bad_decode) {
        fixture bad=inverted;
        bad.image_decode=d;
        if(negative_scoped_mixed_v6(
            bad,QUANTAPDF_ERROR_UNSUPPORTED,bounds,
            false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    }
    /* A malformed SECOND image must atomically discard earlier
     * accepted /I and Path operations, even after their decoded bytes
     * were staged. */
    fixture wrong_second=heterogeneous;
    wrong_second.second_image_decode="[0 1 0 1 0 1]";
    if(negative_scoped_mixed_v6(
        wrong_second,QUANTAPDF_ERROR_UNSUPPORTED,bounds,
        false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture wrong_gray=gray;
    wrong_gray.image_decode="[0 1 0 1 0 1]";
    if(negative_scoped_mixed_v6(
        wrong_gray,QUANTAPDF_ERROR_UNSUPPORTED,bounds,
        false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture late=inverted;
    late.a+=" BT ET";
    if(negative_scoped_mixed_v6(
        late,QUANTAPDF_ERROR_UNSUPPORTED,bounds,
        false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture masked=inverted;
    masked.mask=true;
    if(negative_scoped_mixed_v6(
        masked,QUANTAPDF_ERROR_UNSUPPORTED,bounds,
        false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    fixture missing=inverted;
    missing.missing_resources=true;
    if(negative_scoped_mixed_v6(
        missing,QUANTAPDF_ERROR_FORMAT,bounds,
        false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    auto reduced=bounds;
    reduced.max_source_pixels=3u;
    if(negative_scoped_mixed_v6(
        inverted,QUANTAPDF_ERROR_UNSUPPORTED,reduced,
        false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
    reduced=bounds;
    reduced.max_operations=1u;
    if(negative_scoped_mixed_v6(
        inverted,QUANTAPDF_ERROR_UNSUPPORTED,reduced,
        false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;

    /* Post-failure source reader and raster remain reusable. */
    if(positive_scoped_mixed_v6(inverted,1u,1u,1u,
        {{13u,11u,0u,255u,255u},{26u,5u,223u,223u,223u}},
        false,false,false,true)!=EXIT_SUCCESS)return EXIT_FAILURE;
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

int main(int argc,char **argv)
{
    if(argc==2 && std::strcmp(argv[1],"--scoped-v6-only")==0)
        return source_scoped_mixed_v6_suite();
    if(argc==2 && std::strcmp(argv[1],"--scoped-v7-only")==0)
        return source_scoped_mixed_v7_suite();
    if(argc==2 && std::strcmp(argv[1],"--scoped-v8-only")==0)
        return source_scoped_mixed_v8_suite();
    if(argc==2 && std::strcmp(argv[1],"--scoped-v9-decode-only")==0)
        return source_scoped_mixed_v9_image_decode_suite();
    if(argc==2 && std::strcmp(argv[1],"--scoped-v10-predictor-only")==0)
        return source_scoped_mixed_v10_flate_predictor_suite();
    if(argc==2 && std::strcmp(argv[1],"--profile-parity-only")==0)
        return source_profile_legacy_parity_suite();
    if(argc==2 && std::strcmp(argv[1],"--native-product-bridge-only")==0)
        return native_source_product_bridge_v1_suite();
    if(argc==2 && std::strcmp(argv[1],"--builtin-native-page-only")==0)
        return builtin_native_page_owner_v1_suite();
    if(argc==2 && std::strcmp(argv[1],"--builtin-native-rgb-only")==0)
        return builtin_native_backend_rgb_v1_suite();
    if(argc==2 && std::strcmp(argv[1],"--native-facade-bitmap-only")==0)
        return private_native_facade_bitmap_v1_suite();
    if(argc==2 && std::strcmp(argv[1],"--native-facade-decode-v9-only")==0)
        return private_native_facade_bitmap_v9_suite();
    if(argc!=1)return EXIT_FAILURE;
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
    if(source_scoped_mixed_v6_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(source_scoped_mixed_v7_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(source_scoped_mixed_v8_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(source_scoped_mixed_v9_image_decode_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(source_scoped_mixed_v10_flate_predictor_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(source_profile_legacy_parity_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(native_source_product_bridge_v1_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(builtin_native_page_owner_v1_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(builtin_native_backend_rgb_v1_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(private_native_facade_bitmap_v1_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    if(private_native_facade_bitmap_v9_suite()!=EXIT_SUCCESS)
        return EXIT_FAILURE;

    std::puts("Original Image Do under W/W*: independent PDFium RGB, "
              "detached C11 Clip/Image pixels and atomic negatives PASS");
    return EXIT_SUCCESS;
}
