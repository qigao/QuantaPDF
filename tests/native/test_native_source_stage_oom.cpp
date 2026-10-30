/* Real Source V10 std::vector new failures. The EXACT production QPDF
 * Source unit is compiled independently with seven isolated test-only
 * pre-allocation arming points, not with a replacement source/provider.
 * QPDF authors the original PDF; PDFium is never linked. */
#include "backend/qpdf_native_image_ir.h"
#include "render/native_device_ir_v1.h"
#include "render/ir_raster_v1.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>

namespace {
thread_local unsigned target=0u, observed=0u, faults=0u;
thread_local bool armed=false;
/* The same test-only Source unit redirects every actual C allocation in
 * its IR materializers through this one-shot returning-NULL allocator.
 * No production allocation path or installed SDK is instrumented. */
thread_local bool track_c_allocation=false;
thread_local size_t c_fail_index=0u,c_allocations=0u,c_faults=0u;
}
extern "C" void quantapdf_source_stage_oom_arm(unsigned phase) noexcept
{
    if(phase==target) { target=0u; observed=phase; armed=true; }
}
extern "C" bool quantapdf_source_ir_oom_fail_c_allocation() noexcept
{
    if(!track_c_allocation)return false;
    ++c_allocations;
    if(c_fail_index!=0u && c_allocations==c_fail_index) {
        ++c_faults;
        return true;
    }
    return false;
}
/* The arming callback runs immediately before a genuine allocating
 * std::vector call, and the next C++ allocation fails once. */
void *operator new(std::size_t bytes)
{
    if(armed) {armed=false; ++faults; throw std::bad_alloc();}
    if(void *p=std::malloc(bytes?bytes:1u))return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t bytes) {return ::operator new(bytes);}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete[](void *p) noexcept {std::free(p);}
void operator delete(void *p,std::size_t) noexcept {std::free(p);}
void operator delete[](void *p,std::size_t) noexcept {std::free(p);}
#define CHECK(x) do {if(!(x)) { \
    std::fprintf(stderr,"Source staging OOM line %d: %s\n",__LINE__,#x); \
    return EXIT_FAILURE; \
} } while(0)
using Obj=QPDFObjectHandle;

static std::string original_pdf()
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24]"
        " /UserUnit 2 /Rotate 90 >>"));
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
    auto dict=Obj::newDictionary(),res=Obj::newDictionary();
    dict.replaceKey("/I",image);
    res.replaceKey("/XObject",dict);
    page.replaceKey("/Resources",res);
    page.replaceKey("/Contents",pdf->newStream(
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q "
        "Q 0.875 g 24 17 5 3 re f"));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(bytes->getBuffer()),
                       bytes->getSize());
}

/* Exercise all five distinct V10 Source materializers, not only the
 * original W Clip + Image + PATH case. Index 2 is a true third
 * PageTree leaf; its preceding sibling has unsupported Text, proving
 * Source does not accidentally select or render a different page. */
static std::string original_pdf_variant(
    int kind,int quarters,size_t selected_index)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto selected=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24] /Rotate 90 >>"));
    selected.replaceKey("/Parent",root);
    selected.replaceKey("/UserUnit",
        Obj::parse(std::to_string((double)quarters/4.0)));
    if(selected_index==2u) {
        auto first=pdf->makeIndirectObject(Obj::parse(
            "<< /Type /Page /MediaBox [0 0 32 24] >>"));
        auto second=pdf->makeIndirectObject(Obj::parse(
            "<< /Type /Page /MediaBox [0 0 32 24] >>"));
        first.replaceKey("/Parent",root);
        second.replaceKey("/Parent",root);
        second.replaceKey("/Contents",pdf->newStream("BT ET"));
        root.replaceKey("/Kids",Obj::newArray({first,second,selected}));
        root.replaceKey("/Count",Obj::newInteger(3));
    } else {
        root.replaceKey("/Kids",Obj::newArray({selected}));
        root.replaceKey("/Count",Obj::newInteger(1));
    }
    const char *const content[]={
        "", /* 0: Blank -> materialize_blank_page */
        "1 0 0 rg 4 4 8 6 re f", /* 1: PATH only */
        "q 16 0 0 12 8 3 cm /I Do Q", /* 2: IMAGE only */
        "q 12 6 8 9 re W* n q 16 0 0 12 8 3 cm /I Do Q Q",
        "q 12 6 8 9 re W n q 16 0 0 12 8 3 cm /I Do Q Q 0.875 g 24 17 5 3 re f" /* 4: scoped mixed */
    };
    if(kind>0)selected.replaceKey("/Contents",pdf->newStream(content[kind]));
    if(kind>=2) {
        const std::string raw("\xff\0\0\0\xff\0\0\0\xff\xff\xff\0",12);
        auto image=pdf->newStream(raw);
        auto dict=image.getDict();
        dict.replaceKey("/Type",Obj::newName("/XObject"));
        dict.replaceKey("/Subtype",Obj::newName("/Image"));
        dict.replaceKey("/Width",Obj::newInteger(2));
        dict.replaceKey("/Height",Obj::newInteger(2));
        dict.replaceKey("/BitsPerComponent",Obj::newInteger(8));
        dict.replaceKey("/ColorSpace",Obj::newName("/DeviceRGB"));
        auto xobjects=Obj::newDictionary(),resources=Obj::newDictionary();
        xobjects.replaceKey("/I",image);
        resources.replaceKey("/XObject",xobjects);
        selected.replaceKey("/Resources",resources);
    }
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(bytes->getBuffer()),
                       bytes->getSize());
}

static bool cleared(const quantapdf_ir& ir)
{
    const auto *bytes=reinterpret_cast<const unsigned char*>(&ir);
    for(size_t i=0u;i<sizeof(ir);++i)if(bytes[i]!=0u)return false;
    return true;
}

/* The ORIGINAL, unmodified checked-in 3,747-byte yWorks document is
 * qualified as an owned Source graph BEFORE public C11 dispatch changes.
 * Do not manufacture IR geometry or modify its content streams.
 * Compile the existing production parser with test-only C allocation
 * hooks; there is no second source parser or consumer project. */
static bool original_three_branch_graph(const quantapdf_ir& ir)
{
    if(ir.page_count!=1u || ir.pages==nullptr ||
       ir.pages[0].width_points!=600.0f ||
       ir.pages[0].height_points!=600.0f ||
       ir.operation_count!=3u || ir.operations==nullptr ||
       ir.resources.graphics_state_count!=3u ||
       ir.resources.graphics_state_capacity!=3u ||
       ir.resources.graphics_states==nullptr ||
       ir.resources.clip_count!=7u ||
       ir.resources.clip_capacity!=7u ||
       ir.resources.clips==nullptr ||
       ir.resources.image_count!=0u)
        return false;
    const size_t expected_commands[4]={5u,10u,6u,6u};
    for(size_t i=0u;i<4u;++i) {
        const auto& clip=ir.resources.clips[i];
        if(clip.kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
           clip.commands==nullptr ||
           clip.command_count!=expected_commands[i] ||
           clip.members!=nullptr || clip.member_count!=0u ||
           clip.fill_rule!=(i==1u || i==3u ?
              QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
              QUANTAPDF_COMPOSER_FILL_NONZERO))
            return false;
    }
    for(size_t i=0u;i<3u;++i) {
        const auto& root=ir.resources.clips[i+4u];
        const auto& gs=ir.resources.graphics_states[i];
        const auto& op=ir.operations[i];
        if(root.kind!=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
           root.members==nullptr || root.member_count!=2u ||
           root.members[0]!=1u || root.members[1]!=i+2u ||
           gs.clip_id!=i+5u || gs.fill_alpha!=1.0f ||
           gs.stroke_alpha!=1.0f ||
           gs.blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
           op.kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
           op.page_index!=0u || op.graphics_state_id!=i+1u ||
           op.value.path.options.graphics_state_id!=i+1u ||
           op.value.path.command_count!=(i==0u?2u:5u) ||
           op.value.path.commands==nullptr ||
           (i==0u ?
             (op.value.path.options.stroke!=1 ||
              op.value.path.options.fill!=0 ||
              op.value.path.options.stroke_width!=1.0f ||
              op.value.path.options.stroke_argb!=UINT32_C(0xffff0000)):
             (op.value.path.options.stroke!=0 ||
              op.value.path.options.fill!=1 ||
              op.value.path.options.fill_argb!=UINT32_C(0xff0000ff))))
            return false;
    }
    return true;
}

/* The true yWorks original is the ONLY test input. These RGB controls
 * are analytical original-geometry goldens, independent of C11 pixel
 * output: red 1px centerline y40 under W* nested rectangles; first
 * star nonzero (center winding 2), second star even-odd (center
 * winding 2 -> HOLE). No PDFium oracle, generated substitute image,
 * second raster, 72-DPI upscaling or source loss. */
static uint32_t rgba_at(
    const quantapdf_ir_raster_image& img,size_t x,size_t y)
{
    const unsigned char *p=img.pixels+y*img.stride+4u*x;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|(uint32_t)p[3];
}
static bool zero_bitmap(const quantapdf_ir_raster_image& img)
{
    return img.pixels==nullptr && img.width==0u &&
           img.height==0u && img.stride==0u && img.size==0u;
}
static bool original_three_branch_pixels(
    const quantapdf_ir& ir, quantapdf_ir_raster_image& held)
{
    const quantapdf_ir_raster_scoped_mixed_v5_limits default_limits={
        400000u,3u,96u,8000000u,1u,1u
    };
    auto limits=default_limits;
    quantapdf_ir_raster_image second{};
    const auto render=[&](unsigned dpi,
        const quantapdf_ir_raster_scoped_mixed_v5_limits& budget,
        quantapdf_ir_raster_image& img) {
        return quantapdf_native_device_ir_render_v1_three_gs_original(
            &ir,dpi,&budget,&img);
    };
    const auto fail=[&](const char *label,int status) {
        std::fprintf(stderr,"yWorks real Source RGB: %s status=%d\n",
                     label,status);
        quantapdf_ir_raster_image_dispose(&held);
        quantapdf_ir_raster_image_dispose(&second);
        return false;
    };
    auto status=render(72u,limits,held);
    if(status!=QUANTAPDF_OK || held.pixels==nullptr ||
       held.width!=600u || held.height!=600u ||
       held.stride!=2400u || held.size!=1440000u)
        return fail("72dpi authentic source paint",status);
    /* Original first red S under shared W and nested W* shell/hole. */
    if(rgba_at(held,20u,40u)!=UINT32_C(0xffffffff) ||
       rgba_at(held,40u,39u)!=UINT32_C(0xff7f7fff) ||
       rgba_at(held,40u,40u)!=UINT32_C(0xff7f7fff) ||
       rgba_at(held,100u,40u)!=UINT32_C(0xffffffff) ||
       rgba_at(held,180u,40u)!=UINT32_C(0xff7f7fff) ||
       rgba_at(held,400u,40u)!=UINT32_C(0xffffffff))
        return fail("first red half-width Stroke and W* hole",status);
    /* The first star W keeps its central winding-two region blue,
     * while the third independent star W* cuts the central hole. */
    if(rgba_at(held,80u,125u)!=UINT32_C(0x0000ffff) ||
       rgba_at(held,160u,125u)!=UINT32_C(0x0000ffff) ||
       rgba_at(held,180u,125u)!=UINT32_C(0xffffffff) ||
       rgba_at(held,250u,125u)!=UINT32_C(0xffffffff) ||
       rgba_at(held,500u,500u)!=UINT32_C(0xffffffff))
        return fail("independent W/W* blue star winding",status);
    status=render(72u,limits,second);
    if(status!=QUANTAPDF_OK || second.size!=held.size ||
       second.pixels==nullptr ||
       std::memcmp(second.pixels,held.pixels,held.size)!=0)
        return fail("repeat complete image exact bytes",status);
    quantapdf_ir_raster_image_dispose(&second);

    /* A flat rectangle fill is 4 edges; outer full canvas W=4,
     * each star=5. Both blue stars occupy x=96 by y=90 VISITED
     * pixels: 96*90*16*(4+4+5)=1,797,120 conservative checks,
     * with exactly three independent work partitions (no transfer).
     * Original max sample budget stays 8,000,000. */
    limits.max_sample_tests=5391360u; /* 3 * 1,797,120 */
    status=render(72u,limits,second);
    if(status!=QUANTAPDF_OK || second.size!=held.size ||
       second.pixels==nullptr ||
       std::memcmp(second.pixels,held.pixels,held.size)!=0)
        return fail("conservative boundary exact admission",status);
    quantapdf_ir_raster_image_dispose(&second);
    limits.max_sample_tests=5391359u;
    std::memset(&second,0xa5,sizeof(second));
    status=render(72u,limits,second);
    if(status!=QUANTAPDF_ERROR_UNSUPPORTED || !zero_bitmap(second))
        return fail("one-below aggregate work rejection",status);
    limits=default_limits;
    limits.max_edges=38u; /* floor(38/3)=12 below each 13 edges */
    std::memset(&second,0xa5,sizeof(second));
    status=render(72u,limits,second);
    if(status!=QUANTAPDF_ERROR_UNSUPPORTED || !zero_bitmap(second))
        return fail("one-below flattened edge quota",status);
    limits=default_limits;
    for(const unsigned dpi:{96u,144u}) {
        std::memset(&second,0xa5,sizeof(second));
        status=render(dpi,limits,second);
        if(status!=QUANTAPDF_ERROR_UNSUPPORTED || !zero_bitmap(second))
            return fail("unqualified non-72dpi Stroke refusal",status);
    }
    return true;
}

static bool original_three_branch_source_oom(
    const char *filename,
    const quantapdf_qpdf_source_raw_image_limits& limits)
{
    if(filename==nullptr)return false;
    std::ifstream input(filename,std::ios::binary);
    if(!input.good())return false;
    const std::string bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    if(bytes.size()!=3747u || bytes.compare(0u,5u,"%PDF-")!=0)
        return false;
    size_t allocations=0u;
    {
        quantapdf_ir ir{};
        track_c_allocation=true;
        c_fail_index=0u;c_allocations=0u;c_faults=0u;
        const auto status=
            quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(bytes.data()),
                bytes.size(),&limits,0u,&ir);
        allocations=c_allocations;
        track_c_allocation=false;
        if(status!=QUANTAPDF_OK || !original_three_branch_graph(ir) ||
           c_faults!=0u || allocations<10u || allocations>40u) {
            std::fprintf(stderr,
                "original 3GS Source status=%d count=%zu clips=%zu "
                "gs=%zu paints=%zu\n",(int)status,allocations,
                ir.resources.clip_count,ir.resources.graphics_state_count,
                ir.operation_count);
            quantapdf_ir_dispose(&ir);
            return false;
        }
        quantapdf_ir_raster_image held{};
        if(!original_three_branch_pixels(ir,held)) {
            quantapdf_ir_dispose(&ir);
            return false;
        }
        /* The independently owned C11 output must survive destruction
         * of every original QPDF, Source and Composer IR owner. */
        const uint32_t detached_red=rgba_at(held,40u,40u);
        const uint32_t detached_blue=rgba_at(held,80u,125u);
        quantapdf_ir_dispose(&ir);
        const bool detached=cleared(ir) &&
            rgba_at(held,40u,40u)==detached_red &&
            rgba_at(held,80u,125u)==detached_blue;
        quantapdf_ir_raster_image_dispose(&held);
        if(!detached)return false;
    }
    for(size_t fail=1u;fail<=allocations;++fail) {
        quantapdf_ir ir;
        std::memset(&ir,0xa5,sizeof(ir));
        track_c_allocation=true;
        c_fail_index=fail;c_allocations=0u;c_faults=0u;
        const auto status=
            quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(bytes.data()),
                bytes.size(),&limits,0u,&ir);
        const size_t seen=c_allocations,fired=c_faults;
        track_c_allocation=false;c_fail_index=0u;
        if(status!=QUANTAPDF_ERROR_NOMEM || !cleared(ir) ||
           seen!=fail || fired!=1u) {
            std::fprintf(stderr,
                "original 3GS Source OOM %zu/%zu status=%d seen=%zu "
                "fired=%zu\n",fail,allocations,(int)status,seen,fired);
            quantapdf_ir_dispose(&ir);
            return false;
        }
        quantapdf_ir recovery{};
        const auto success=
            quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(bytes.data()),
                bytes.size(),&limits,0u,&recovery);
        const bool restored=success==QUANTAPDF_OK &&
                            original_three_branch_graph(recovery);
        quantapdf_ir_dispose(&recovery);
        if(!restored || !cleared(recovery))return false;
    }
    std::printf("Native original yWorks Source: 3 paints, 4 Clip leaves, "
                "3 independent roots, 3 GS, %zu faulted C allocations\n",
                allocations);
    return true;
}


/* Exact original decimal MediaBox admission is a SOURCE-only M2
 * milestone. The same original production V10 parser, selected PageTree,
 * QPDF number tokens and C-owned IR are used here, not a second parser
 * or a public native bitmap bypass. Any original painted content still
 * refuses until fractional viewport and full cubic ring work qualify. */
static std::string original_decimal_source(
    const std::string& box,const std::string& operators,
    const std::string& page_extra={})
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox "+box+" "+page_extra+" >>"));
    page.replaceKey("/Parent",root);
    root.replaceKey("/Kids",Obj::newArray({page}));
    root.replaceKey("/Count",Obj::newInteger(1));
    if(!operators.empty())
        page.replaceKey("/Contents",pdf->newStream(operators));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    const auto bytes=writer.getBufferSharedPointer();
    return std::string(
        reinterpret_cast<const char*>(bytes->getBuffer()),
        bytes->getSize());
}
static bool exact_decimal_page_ir(const quantapdf_ir& ir)
{
    return ir.page_count==1u && ir.pages!=nullptr &&
           ir.pages[0].width_points==static_cast<float>(237.133) &&
           ir.pages[0].height_points==static_cast<float>(180.44) &&
           ir.pages[0].background_argb==UINT32_C(0xffffffff) &&
           ir.operations==nullptr && ir.operation_count==0u &&
           ir.resources.images==nullptr && ir.resources.image_count==0u &&
           ir.resources.graphics_states==nullptr &&
           ir.resources.graphics_state_count==0u &&
           ir.resources.clips==nullptr && ir.resources.clip_count==0u;
}
static bool original_texample_decimal_source_m2(
    const quantapdf_qpdf_source_raw_image_limits& limits)
{
    constexpr const char *real_box="[0 0 237.133 180.44]";
    const auto native=[&](const std::string& original,quantapdf_ir& out) {
        return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
            reinterpret_cast<const unsigned char*>(original.data()),
            original.size(),&limits,0u,&out);
    };
    const std::string positives[]={
        /* The page's original exact rational 237133/1000 and
         * 180440/1000 must survive Source-IR ownership. */
        original_decimal_source(real_box,""),
        original_decimal_source(real_box,
            "q 1 0 0 1 118.566 90.22 cm Q"),
        original_decimal_source(real_box,
            "q 1 0 0 1 118.566 90.22 cm Q",
            "/CropBox [0 0 237.133 180.44]")
    };
    for(const auto& pdf:positives) {
        quantapdf_ir ir{};
        if(native(pdf,ir)!=QUANTAPDF_OK ||
           !exact_decimal_page_ir(ir)) {
            quantapdf_ir_dispose(&ir);
            return false;
        }
        quantapdf_ir_dispose(&ir);
        if(!cleared(ir))return false;
    }
    /* A single real C-owner allocation fault must leave a null IR
     * and NOT poison a following exact original-geometry Source. */
    {
        quantapdf_ir denied;
        std::memset(&denied,0xa5,sizeof(denied));
        track_c_allocation=true;
        c_fail_index=1u;c_allocations=0u;c_faults=0u;
        const auto status=native(positives[1],denied);
        const size_t fail_count=c_faults,actual_allocations=c_allocations;
        track_c_allocation=false;c_fail_index=0u;
        if(status!=QUANTAPDF_ERROR_NOMEM || !cleared(denied) ||
           fail_count!=1u || actual_allocations!=1u) {
            quantapdf_ir_dispose(&denied);
            std::fprintf(stderr,
                "exact TeXample decimal Source OOM status=%d "
                "faults=%zu allocations=%zu\n",
                (int)status,fail_count,actual_allocations);
            return false;
        }
        quantapdf_ir recovery{};
        const auto again=native(positives[1],recovery);
        const bool accepted=again==QUANTAPDF_OK &&
                            exact_decimal_page_ir(recovery);
        quantapdf_ir_dispose(&recovery);
        if(!accepted || !cleared(recovery))return false;
    }
    /* This M2 gate admits NO painted original decimal page yet.
     * Neither near-miss decimals, cropped/rotated/nondefault-Unit
     * originals, Text/Do/Fill, nor unbalanced sources may be treated
     * as an empty white Bitmap. There is no 238x181 rescaled output
     * and no special public V10 render branch. */
    const std::string negatives[]={
        original_decimal_source("[0 0 237.134 180.44]",""),
        original_decimal_source("[0 0 237.133 180.45]",""),
        original_decimal_source(real_box,"",
            "/CropBox [0 0 237.133 180]"),
        original_decimal_source(real_box,"",
            "/UserUnit 1.25"),
        original_decimal_source(real_box,"",
            "/Rotate 90"),
        original_decimal_source(real_box,
            "q 1 0 0 1 118.566 90.22 cm 4 4 8 6 re f Q"),
        original_decimal_source(real_box,
            "1 0 0 1 118.566 90.22 cm BT ET"),
        original_decimal_source(real_box,
            "q 1 0 0 1 118.566 90.22 cm"),
        original_decimal_source(real_box,"/Missing Do")
    };
    for(size_t i=0u;i<sizeof(negatives)/sizeof(negatives[0]);++i) {
        quantapdf_ir denied;
        std::memset(&denied,0xa5,sizeof(denied));
        const auto status=native(negatives[i],denied);
        /* Structurally malformed q scopes and missing named XObject
         * resources are FORMAT, not unsupported graphic features.
         * No Source error class may leak any partially owned IR. */
        const auto expected=i>=7u ? QUANTAPDF_ERROR_FORMAT :
                                     QUANTAPDF_ERROR_UNSUPPORTED;
        if(status!=expected || !cleared(denied)) {
            std::fprintf(stderr,
                "exact TeXample decimal Source negative=%zu status=%d\n",
                i,(int)status);
            quantapdf_ir_dispose(&denied);
            return false;
        }
    }
    std::printf("Original TeXample Source M2: exact "
                "237133/1000 x 180440/1000 MediaBox in owned blank IR, "
                "3 positives / 9 atomic negatives / C OOM recovery\n");
    return true;
}


/* The UNMODIFIED 1,396-byte TeXample original. Private Source M3
 * admission proves all original 24 MOVE / 32 CUBIC / 8 CLOSE tokens
 * are represented by FOUR owned 12-command M+4C+H double-ring
 * resource/paint paths; sixteen zero-edge MOVEs are discharged only
 * under the proven original operator sequence. No C11 rendering
 * or public PDF positive is asserted here. */
static bool exact_texample_private_ir(const quantapdf_ir& ir)
{
    if(ir.page_count!=1u || ir.pages==nullptr ||
       ir.pages[0].width_points!=static_cast<float>(237.133) ||
       ir.pages[0].height_points!=static_cast<float>(180.44) ||
       ir.pages[0].background_argb!=UINT32_C(0xffffffff) ||
       ir.operation_count!=3u || ir.operations==nullptr ||
       ir.resources.image_count!=0u || ir.resources.images!=nullptr ||
       ir.resources.clip_count!=1u || ir.resources.clip_capacity!=1u ||
       ir.resources.clips==nullptr ||
       ir.resources.graphics_state_count!=1u ||
       ir.resources.graphics_state_capacity!=1u ||
       ir.resources.graphics_states==nullptr)
        return false;
    const auto& clip=ir.resources.clips[0];
    const auto& gs=ir.resources.graphics_states[0];
    if(clip.kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
       clip.fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD ||
       clip.command_count!=12u || clip.commands==nullptr ||
       clip.members!=nullptr || clip.member_count!=0u ||
       gs.clip_id!=1u || gs.fill_alpha!=1.0f ||
       gs.stroke_alpha!=1.0f ||
       gs.blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL)
        return false;
    for(size_t i=0u;i<4u;++i) {
        const quantapdf_composer_path_command *cmds=
            i==0u?clip.commands:ir.operations[i-1u].value.path.commands;
        if(cmds==nullptr ||
           cmds[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
           cmds[1].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
           cmds[2].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
           cmds[3].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
           cmds[4].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
           cmds[5].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE ||
           cmds[6].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
           cmds[7].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
           cmds[8].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
           cmds[9].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
           cmds[10].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
           cmds[11].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)
            return false;
        /* Original translated and Y-flipped endpoints, no synthetic
         * ring inferred from a path's bounding rectangle. */
        const double x0=(i==0u || i==2u)?146.9128:203.60642;
        const double x1=(i==0u || i==2u)?175.25962:231.95324;
        for(const auto &p : {std::pair<size_t,double>{0u,x0},
                             std::pair<size_t,double>{6u,x1}})
            if(std::fabs((double)cmds[p.first].point1.x-p.second)>0.0001 ||
               std::fabs((double)cmds[p.first].point1.y-90.22)>0.0001)
                return false;
        for(size_t k=1u;k<=10u;++k) {
            if(k==5u || k==6u)continue;
            if(!std::isfinite(cmds[k].point1.x) ||
               !std::isfinite(cmds[k].point2.x) ||
               !std::isfinite(cmds[k].point3.x) ||
               cmds[k].point3.x<0.0f ||
               cmds[k].point3.x>237.133f ||
               cmds[k].point3.y<0.0f ||
               cmds[k].point3.y>180.44f)
                return false;
        }
    }
    for(size_t i=0u;i<3u;++i) {
        const auto& op=ir.operations[i];
        if(op.kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
           op.page_index!=0u ||
           op.value.path.command_count!=12u ||
           op.graphics_state_id!=(i==0u?1u:0u) ||
           op.value.path.options.graphics_state_id!=(i==0u?1u:0u) ||
           op.value.path.options.struct_size<
               QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE ||
           (i==0u ?
             (op.value.path.options.fill!=1 ||
              op.value.path.options.stroke!=0 ||
              op.value.path.options.fill_rule!=
                  QUANTAPDF_COMPOSER_FILL_EVEN_ODD ||
              op.value.path.options.fill_argb!=UINT32_C(0xffff8000)) :
             (op.value.path.options.fill!=0 ||
              op.value.path.options.stroke!=1 ||
              op.value.path.options.stroke_argb!=UINT32_C(0xff000000) ||
              op.value.path.options.stroke_width!=0.3985f)))
            return false;
    }
    return true;
}
/* Existing original TeXample Source/OOM CTest, no generated PDF and
 * no second test target: borrow EACH exact closed 4-cubic-by-2 black
 * original S from the full owned Source M3 IR. The private 238x181
 * physical sample grid does NOT rescale the original 237.133x180.44
 * Source coordinates, nor does it make public decimal-page rendering
 * qualified. All lifetime and error semantics remain C-owned. */
static bool original_texample_m4a_curved_strokes(
    const quantapdf_ir& source,quantapdf_ir_raster_image& detached,
    size_t& proven_sample_cap)
{
    if(!exact_texample_private_ir(source))return false;
    const quantapdf_ir_raster_v1_limits base={
        50000u,1u,256u,8000000u
    };
    const auto is_zero=[](const quantapdf_ir_raster_image& x) {
        return x.pixels==nullptr && x.width==0u &&
               x.height==0u && x.stride==0u && x.size==0u;
    };
    for(size_t which=1u;which<=2u;++which) {
        auto page=source.pages[0];
        auto op=source.operations[which];
        quantapdf_ir slice{};
        quantapdf_ir_raster_image out{},repeat{};
        page.width_points=238.0f;
        page.height_points=181.0f;
        op.graphics_state_id=0u;
        op.value.path.options.graphics_state_id=0u;
        slice.pages=&page;slice.page_count=1u;
        slice.operations=&op;slice.operation_count=1u;
        const size_t xc=which==1u?90u:146u;
        auto render=[&](const quantapdf_ir_raster_v1_limits& limit,
                         quantapdf_ir_raster_image& image) {
            return quantapdf_ir_raster_render_v1_texample_closed_cubic_stroke_work(
                &slice,0u,&limit,&image);
        };
        auto status=render(base,out);
        if(status!=QUANTAPDF_OK || out.pixels==nullptr ||
           out.width!=238u || out.height!=181u ||
           out.stride!=238u*4u || out.size!=238u*181u*4u ||
           rgba_at(out,xc,90u)!=UINT32_C(0xffffffff) ||
           rgba_at(out,xc,5u)==UINT32_C(0xffffffff) ||
           rgba_at(out,xc,4u)!=UINT32_C(0xffffffff) ||
           rgba_at(out,xc+10u,5u)!=UINT32_C(0xffffffff)) {
            std::fprintf(stderr,
                "authentic curved Stroke M4a index=%zu status=%d "
                "at-top=%08x center=%08x\n",
                which,(int)status,
                out.pixels?rgba_at(out,xc,5u):0u,
                out.pixels?rgba_at(out,xc,90u):0u);
            quantapdf_ir_raster_image_dispose(&out);
            return false;
        }
        status=render(base,repeat);
        if(status!=QUANTAPDF_OK || repeat.pixels==nullptr ||
           repeat.size!=out.size ||
           std::memcmp(out.pixels,repeat.pixels,out.size)!=0) {
            quantapdf_ir_raster_image_dispose(&out);
            quantapdf_ir_raster_image_dispose(&repeat);
            return false;
        }
        quantapdf_ir_raster_image_dispose(&repeat);
        /* The exact admissible conservative bill is computed from
         * the source's ORIGINAL flattened candidate-edge envelopes,
         * not guessed from a synthetic circle or a full-page 192-edge
         * bill. Binary search verifies monotonicity and N−1 refusal. */
        if(which==1u) {
            size_t lo=1u,hi=base.max_sample_tests;
            while(lo+1u<hi) {
                const size_t mid=lo+(hi-lo)/2u;
                auto budget=base;budget.max_sample_tests=mid;
                quantapdf_ir_raster_image trial{};
                const auto ret=render(budget,trial);
                if(ret==QUANTAPDF_OK) {
                    if(trial.size!=out.size || trial.pixels==nullptr ||
                       std::memcmp(trial.pixels,out.pixels,out.size)!=0) {
                        quantapdf_ir_raster_image_dispose(&trial);
                        quantapdf_ir_raster_image_dispose(&out);
                        return false;
                    }
                    hi=mid;
                    quantapdf_ir_raster_image_dispose(&trial);
                } else if(ret==QUANTAPDF_ERROR_UNSUPPORTED &&
                          is_zero(trial)) {
                    lo=mid;
                } else {
                    quantapdf_ir_raster_image_dispose(&trial);
                    quantapdf_ir_raster_image_dispose(&out);
                    return false;
                }
            }
            if(hi<16u || hi>=base.max_sample_tests) {
                quantapdf_ir_raster_image_dispose(&out);
                return false;
            }
            auto cap=base;cap.max_sample_tests=hi;
            quantapdf_ir_raster_image at_cap{};
            if(render(cap,at_cap)!=QUANTAPDF_OK ||
               at_cap.size!=out.size || at_cap.pixels==nullptr ||
               std::memcmp(at_cap.pixels,out.pixels,out.size)!=0) {
                quantapdf_ir_raster_image_dispose(&at_cap);
                quantapdf_ir_raster_image_dispose(&out);
                return false;
            }
            quantapdf_ir_raster_image_dispose(&at_cap);
            cap.max_sample_tests=hi-1u;
            quantapdf_ir_raster_image denied;
            std::memset(&denied,0xa5,sizeof(denied));
            if(render(cap,denied)!=QUANTAPDF_ERROR_UNSUPPORTED ||
               !is_zero(denied)) {
                quantapdf_ir_raster_image_dispose(&denied);
                quantapdf_ir_raster_image_dispose(&out);
                return false;
            }
            proven_sample_cap=hi;
            auto under_edges=base;under_edges.max_edges=191u;
            std::memset(&denied,0xa5,sizeof(denied));
            if(render(under_edges,denied)!=QUANTAPDF_ERROR_UNSUPPORTED ||
               !is_zero(denied)) {
                quantapdf_ir_raster_image_dispose(&denied);
                quantapdf_ir_raster_image_dispose(&out);
                return false;
            }
            auto bad_op=op;
            bad_op.value.path.options.stroke_width=0.4f;
            slice.operations=&bad_op;
            std::memset(&denied,0xa5,sizeof(denied));
            if(render(base,denied)!=QUANTAPDF_ERROR_UNSUPPORTED ||
               !is_zero(denied)) {
                quantapdf_ir_raster_image_dispose(&denied);
                quantapdf_ir_raster_image_dispose(&out);
                return false;
            }
            bad_op=op;
            bad_op.value.path.options.line_join=
                QUANTAPDF_COMPOSER_LINE_JOIN_ROUND;
            std::memset(&denied,0xa5,sizeof(denied));
            if(render(base,denied)!=QUANTAPDF_ERROR_UNSUPPORTED ||
               !is_zero(denied)) {
                quantapdf_ir_raster_image_dispose(&denied);
                quantapdf_ir_raster_image_dispose(&out);
                return false;
            }
            slice.operations=&op;
        }
        if(which==1u)detached=out;
        else quantapdf_ir_raster_image_dispose(&out);
    }
    std::printf("TeXample M4a: original both closed 0.3985 cubic S "
                "sampled on private unscaled 238x181, first curved "
                "Stroke sampled-edge cap=%zu, N-1 denied\n",
                proven_sample_cap);
    return true;
}

static bool authentic_texample_source_m3(
    const char *filename,
    const quantapdf_qpdf_source_raw_image_limits& limits)
{
    if(filename==nullptr)return false;
    std::ifstream input(filename,std::ios::binary);
    if(!input.good())return false;
    const std::string original(std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{});
    if(original.size()!=1396u ||
       original.compare(0u,5u,"%PDF-")!=0)
        return false;
    const auto private_read=[&](quantapdf_ir& out) {
        return quantapdf_qpdf_source_texample_m3_stage_to_ir(
            reinterpret_cast<const unsigned char*>(original.data()),
            original.size(),&limits,&out);
    };
    /* The ordinary Source V10 is NOT widened. Even a successful
     * private original Source IR must never produce partial public
     * TeXample content through the existing bridge. */
    {
        quantapdf_ir reject;
        std::memset(&reject,0xa5,sizeof(reject));
        const auto status=
            quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(original.data()),
                original.size(),&limits,0u,&reject);
        if(status!=QUANTAPDF_ERROR_UNSUPPORTED || !cleared(reject)) {
            std::fprintf(stderr,"original TeXample public Source=%d\n",
                         (int)status);
            quantapdf_ir_dispose(&reject);
            return false;
        }
    }
    size_t allocs=0u;
    {
        quantapdf_ir ir{};
        track_c_allocation=true;
        c_fail_index=0u;c_allocations=0u;c_faults=0u;
        const auto status=private_read(ir);
        allocs=c_allocations;
        track_c_allocation=false;
        if(status!=QUANTAPDF_OK || !exact_texample_private_ir(ir) ||
           allocs<7u || allocs>32u || c_faults!=0u) {
            std::fprintf(stderr,
                "TeXample private Source M3 status=%d, allocs=%zu, "
                "paint=%zu, clip=%zu, GS=%zu\n",
                (int)status,allocs,ir.operation_count,
                ir.resources.clip_count,ir.resources.graphics_state_count);
            quantapdf_ir_dispose(&ir);
            return false;
        }
        quantapdf_ir_raster_image held{};
        size_t curved_cap=0u;
        if(!original_texample_m4a_curved_strokes(ir,held,curved_cap)) {
            quantapdf_ir_dispose(&ir);
            return false;
        }
        /* A true raster pixel owner must outlive ALL original QPDF
         * Source, Clip, PAINT and IR allocations. */
        const uint32_t original_top=rgba_at(held,90u,5u);
        quantapdf_ir_dispose(&ir);
        const bool detached_ok=cleared(ir) &&
            held.pixels!=nullptr &&
            rgba_at(held,90u,5u)==original_top &&
            rgba_at(held,90u,90u)==UINT32_C(0xffffffff);
        quantapdf_ir_raster_image_dispose(&held);
        if(!detached_ok || curved_cap==0u)return false;
    }
    for(size_t fail=1u;fail<=allocs;++fail) {
        quantapdf_ir denied;
        std::memset(&denied,0xa5,sizeof(denied));
        track_c_allocation=true;
        c_fail_index=fail;c_allocations=0u;c_faults=0u;
        const auto status=private_read(denied);
        const auto seen=c_allocations,fired=c_faults;
        track_c_allocation=false;c_fail_index=0u;
        if(status!=QUANTAPDF_ERROR_NOMEM || !cleared(denied) ||
           seen!=fail || fired!=1u) {
            std::fprintf(stderr,
                "TeXample original Source allocation %zu/%zu, "
                "status=%d, seen=%zu, fired=%zu\n",
                fail,allocs,(int)status,seen,fired);
            quantapdf_ir_dispose(&denied);
            return false;
        }
        quantapdf_ir recovery{};
        const auto again=private_read(recovery);
        const bool restored=again==QUANTAPDF_OK &&
                            exact_texample_private_ir(recovery);
        quantapdf_ir_dispose(&recovery);
        if(!restored || !cleared(recovery))return false;
    }
    std::printf("Original TeXample Source M3: 24 m/32 c/8 h, "
                "16 empty moves, 4 x 12 commands, 3 paints, "
                "1 frozen W*, %zu faulted C allocations\n",allocs);
    return true;
}

int main(int argc,char **argv)
{
    const std::string pdf=original_pdf();
    const quantapdf_qpdf_source_raw_image_limits limits={
        2u*1024u*1024u,65536u,4u,4u,1000000u,1000000u,4000000u,8u
    };
    /* Stages: 1 RAW Image, 2 IMAGE order, 3 PATH ownership,
     * 4 PATH order after Image, 5 rotated RAW bytes, 6 IMAGE Clip
     * membership, 7 PATH Clip membership (second push/growth). */
    for(unsigned phase=0u;phase<=7u;++phase) {
        quantapdf_ir ir;
        std::memset(&ir,0xA5,sizeof(ir));
        target=phase; observed=0u; faults=0u; armed=false;
        const quantapdf_status result=
            quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(pdf.data()),
                pdf.size(),&limits,0u,&ir);
        const unsigned was_observed=observed, did_fail=faults;
        const bool remained_armed=armed;
        target=0u; armed=false;
        if(phase==0u) {
            CHECK(result==QUANTAPDF_OK && ir.page_count==1u &&
                  ir.pages!=nullptr && ir.operation_count==2u &&
                  ir.resources.image_count==1u &&
                  ir.pages[0].width_points==48.0f &&
                  ir.pages[0].height_points==64.0f);
            quantapdf_ir_dispose(&ir);
            CHECK(cleared(ir));
        } else {
            if(result!=QUANTAPDF_ERROR_NOMEM || !cleared(ir) ||
               did_fail!=1u || was_observed!=phase || remained_armed) {
                std::fprintf(stderr,
                    "Source phase=%u status=%d faults=%u observed=%u armed=%d\n",
                    phase,(int)result,did_fail,was_observed,
                    remained_armed?1:0);
                return EXIT_FAILURE;
            }
            /* A failed partial stage must not poison the parser and
             * the next real invocation must own a complete IR. */
            quantapdf_ir recovery{};
            const auto status=quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(pdf.data()),
                pdf.size(),&limits,0u,&recovery);
            CHECK(status==QUANTAPDF_OK &&
                  recovery.page_count==1u && recovery.pages!=nullptr &&
                  recovery.operation_count==2u &&
                  recovery.resources.image_count==1u &&
                  recovery.pages[0].width_points==48.0f &&
                  recovery.pages[0].height_points==64.0f);
            quantapdf_ir_dispose(&recovery);
            CHECK(cleared(recovery));
        }
    }
    /* Full C-owned materialization failure matrix. Each profile takes
     * a DIFFERENT accepted Source V10 publish/rollback path, with both
     * small and large quarter UserUnit and first/third selected leaf. */
    const size_t minimum_allocations[]={1u,3u,4u,7u,9u};
    for(int kind=0;kind<=4;++kind)
    for(int quarters:{2,16})
    for(size_t selected_index:{0u,2u}) {
        const std::string original=
            original_pdf_variant(kind,quarters,selected_index);
        const size_t expected_ops=kind==0?0u:kind==4?2u:1u;
        const size_t expected_images=kind>=2?1u:0u;
        const size_t expected_clips=kind>=3?1u:0u;
        const float physical_width=(float)(6*quarters);
        const float physical_height=(float)(8*quarters);
        const auto accepted=[&](const quantapdf_ir& ir) {
            return ir.pages!=nullptr && ir.page_count==1u &&
                ir.pages[0].width_points==physical_width &&
                ir.pages[0].height_points==physical_height &&
                ir.operation_count==expected_ops &&
                ir.resources.image_count==expected_images &&
                ir.resources.clip_count==expected_clips;
        };
        size_t expected_allocations=0u;
        {
            quantapdf_ir clean{};
            track_c_allocation=true;
            c_fail_index=0u;c_allocations=0u;c_faults=0u;
            const auto status=
                quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                    reinterpret_cast<const unsigned char*>(original.data()),
                    original.size(),&limits,selected_index,&clean);
            expected_allocations=c_allocations;
            track_c_allocation=false;
            if(status!=QUANTAPDF_OK || !accepted(clean) ||
               expected_allocations<minimum_allocations[kind] ||
               expected_allocations>32u || c_faults!=0u) {
                std::fprintf(stderr,
                    "Source profile=%d unit_quarters=%d index=%zu"
                    " baseline status=%d allocs=%zu image=%zu ops=%zu clips=%zu\n",
                    kind,quarters,selected_index,(int)status,
                    expected_allocations,clean.resources.image_count,
                    clean.operation_count,clean.resources.clip_count);
                quantapdf_ir_dispose(&clean);
                return EXIT_FAILURE;
            }
            quantapdf_ir_dispose(&clean);
            CHECK(cleared(clean));
        }
        for(size_t fail=1u;fail<=expected_allocations;++fail) {
            quantapdf_ir denied;
            std::memset(&denied,0xA5,sizeof(denied));
            track_c_allocation=true;
            c_fail_index=fail;c_allocations=0u;c_faults=0u;
            const auto status=
                quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                    reinterpret_cast<const unsigned char*>(original.data()),
                    original.size(),&limits,selected_index,&denied);
            const size_t count=c_allocations,failures=c_faults;
            track_c_allocation=false;c_fail_index=0u;
            if(status!=QUANTAPDF_ERROR_NOMEM || !cleared(denied) ||
               failures!=1u || count!=fail) {
                std::fprintf(stderr,
                    "Source profile=%d unit_quarters=%d index=%zu"
                    " allocation=%zu/%zu status=%d fired=%zu seen=%zu\n",
                    kind,quarters,selected_index,fail,expected_allocations,
                    (int)status,failures,count);
                quantapdf_ir_dispose(&denied);
                return EXIT_FAILURE;
            }
            quantapdf_ir retry{};
            const auto recovered=
                quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                    reinterpret_cast<const unsigned char*>(original.data()),
                    original.size(),&limits,selected_index,&retry);
            if(recovered!=QUANTAPDF_OK || !accepted(retry)) {
                std::fprintf(stderr,
                    "Source profile=%d unit_quarters=%d index=%zu"
                    " postfault retry=%d failure_index=%zu\n",
                    kind,quarters,selected_index,(int)recovered,fail);
                quantapdf_ir_dispose(&retry);
                return EXIT_FAILURE;
            }
            quantapdf_ir_dispose(&retry);
            CHECK(cleared(retry));
        }
    }
    if(argc!=3 || !original_three_branch_source_oom(argv[1],limits))
        return EXIT_FAILURE;
    if(!original_texample_decimal_source_m2(limits))
        return EXIT_FAILURE;
    if(!authentic_texample_source_m3(argv[2],limits))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
