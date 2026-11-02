/* Real Source V10 std::vector new failures. The EXACT production QPDF
 * Source unit is compiled independently with eight isolated test-only
 * pre-allocation arming points, not with a replacement source/provider.
 * QPDF authors the original PDF; PDFium is never linked. */
#include <quantapdf/quantapdf.h>
#include "backend/qpdf_native_image_ir.h"
#include "backend/native_prepared_page_v1.h"
#include "composer/native_prepared_composer_v1.h"
#include "core/context_internal.h"
#include "backend/ttf_font.h"
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
#include <filesystem>
#include <iterator>
#include <new>
#include <string>
#include "native_ttf_fixture.h"
#include <vector>

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
 * Source step (vector growth or text-reader creation), and the next
 * C++ allocation fails once. */
/* Keep actual fault-injection calls observable and prevent optimizers
 * from treating these replacement new/delete pairs as the default heap.
 * The matching allocator/deallocator both use malloc/free. */
#if defined(_MSC_VER)
#define QP_FAULT_NOINLINE __declspec(noinline)
#else
#define QP_FAULT_NOINLINE __attribute__((noinline))
#endif
QP_FAULT_NOINLINE void *operator new(std::size_t bytes)
{
    if(armed) {armed=false; ++faults; throw std::bad_alloc();}
    if(void *p=std::malloc(bytes?bytes:1u))return p;
    throw std::bad_alloc();
}
QP_FAULT_NOINLINE void *operator new[](std::size_t bytes) {return ::operator new(bytes);}
QP_FAULT_NOINLINE void operator delete(void *p) noexcept {std::free(p);}
QP_FAULT_NOINLINE void operator delete[](void *p) noexcept {std::free(p);}
QP_FAULT_NOINLINE void operator delete(void *p,std::size_t) noexcept {std::free(p);}
QP_FAULT_NOINLINE void operator delete[](void *p,std::size_t) noexcept {std::free(p);}
#undef QP_FAULT_NOINLINE
#define CHECK(x) do {if(!(x)) { \
    std::fprintf(stderr,"Source staging OOM line %d: %s\n",__LINE__,#x); \
    return EXIT_FAILURE; \
} } while(0)
using Obj=QPDFObjectHandle;

static std::string original_pdf(bool legacy=false)
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(Obj::parse(
        "<< /Type /Page /MediaBox [0 0 32 24]"
        " /UserUnit 2 /Rotate 90 >>"));
    if(legacy) {page.removeKey("/UserUnit");page.removeKey("/Rotate");}
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
    if(legacy)writer.setCompressStreams(false);
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
 * or a public native bitmap bypass. These blank controls also prove
 * that selecting the complete painted profile does not change M2. */
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
            "/CropBox [0 0 237.133 180.44]"),
        original_decimal_source(real_box,"1 0 0 1 118.566 90.22 cm q Q"),
        original_decimal_source(real_box,"1 0 0 1 118.566 90.22 cm q 0 G Q"),
        original_decimal_source(real_box,
            "1 0 0 1 118.566 90.22 cm q 0 G 0 g 0.3985 w q q Q Q Q")
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
                "6 positives / 9 atomic negatives / C OOM recovery\n");
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
    size_t& proven_sample_cap,bool physical=false)
{
    if(!exact_texample_private_ir(source))return false;
    quantapdf_native_device_plan plan{};
    if(physical && quantapdf_native_device_plan_create_physical(
        "0 0 237.133 180.44","0 0 237.133 180.44","1",0,72u,50000u,
        &plan)!=QUANTAPDF_OK)return false;
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
        if(!physical) {
            page.width_points=238.0f;
            page.height_points=181.0f;
        }
        op.graphics_state_id=0u;
        op.value.path.options.graphics_state_id=0u;
        slice.pages=&page;slice.page_count=1u;
        slice.operations=&op;slice.operation_count=1u;
        const size_t xc=which==1u?90u:146u;
        auto render=[&](const quantapdf_ir_raster_v1_limits& limit,
                         quantapdf_ir_raster_image& image) {
            return physical ?
                quantapdf_ir_raster_render_v1_texample_physical_cubic_stroke_work(
                    &slice,0u,&limit,&plan,&image) :
                quantapdf_ir_raster_render_v1_texample_closed_cubic_stroke_work(
                    &slice,0u,&limit,&image);
        };
        auto status=render(base,out);
        if(status!=QUANTAPDF_OK || out.pixels==nullptr ||
           out.width!=238u || out.height!=181u ||
           out.stride!=238u*4u || out.size!=238u*181u*4u ||
           rgba_at(out,xc,90u)!=UINT32_C(0xffffffff) ||
           rgba_at(out,xc,5u)==UINT32_C(0xffffffff) ||
           rgba_at(out,xc,4u)!=UINT32_C(0xffffffff) ||
           rgba_at(out,xc+30u,5u)!=UINT32_C(0xffffffff)) {
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
        if(physical) {
            /* No original artwork reaches the fractional page fringe. */
            for(size_t y=0u;y<out.height;++y)
                if(rgba_at(out,237u,y)!=UINT32_C(0xffffffff)) {
                    quantapdf_ir_raster_image_dispose(&out);return false;
                }
            for(size_t x=0u;x<out.width;++x)
                if(rgba_at(out,x,180u)!=UINT32_C(0xffffffff)) {
                    quantapdf_ir_raster_image_dispose(&out);return false;
                }
            const auto valid=plan;
            quantapdf_ir_raster_image denied;
            if(quantapdf_native_device_plan_create(
                "0 0 237.133 180.44","0 0 237.133 180.44","1",0,72u,
                50000u,&plan)!=QUANTAPDF_OK)return false;
            std::memset(&denied,0xa5,sizeof(denied));
            if(render(base,denied)!=QUANTAPDF_ERROR_UNSUPPORTED || !is_zero(denied)) {
                quantapdf_ir_raster_image_dispose(&out);return false;
            }
            plan=valid;
            plan.from_composer[0]=238.0/237.133;
            std::memset(&denied,0xa5,sizeof(denied));
            if(render(base,denied)!=QUANTAPDF_ERROR_UNSUPPORTED || !is_zero(denied)) {
                quantapdf_ir_raster_image_dispose(&out);return false;
            }
            plan=valid;
        }
        if(which==1u)detached=out;
        else quantapdf_ir_raster_image_dispose(&out);
    }
    std::printf("TeXample %s: original both closed 0.3985 cubic S "
                "sampled without fit stretch, first curved "
                "Stroke sampled-edge cap=%zu, N-1 denied\n",
                physical?"M4b physical 237.133x180.44":"M4a private 238x181",
                proven_sample_cap);
    return true;
}

/* Independent rectangular controls qualify only physical page coverage,
 * never substitute for the original PDF. Geometry crosses all page edges,
 * so clipping must happen at each 4x4 sample rather than by whole pixels. */
static bool physical_texample_page_fringe(unsigned dpi=72u,bool buffer_rect=false)
{
    quantapdf_composer_page_state page{237.133f,180.44f,UINT32_C(0xffffffff),0};
    quantapdf_composer_path_command commands[5]{};
    commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].kind=commands[2].kind=commands[3].kind=
        QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    commands[0].point1={-1.0f,-1.0f};
    commands[1].point1={239.0f,-1.0f};
    commands[2].point1={239.0f,182.0f};
    commands[3].point1={-1.0f,182.0f};
    quantapdf_composer_operation op{};
    op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    op.value.path.commands=commands;op.value.path.command_count=5u;
    auto& opt=op.value.path.options;
    opt.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    opt.fill=1;opt.fill_argb=UINT32_C(0xffff0000);
    opt.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    quantapdf_ir ir{};
    ir.pages=&page;ir.page_count=1u;ir.operations=&op;ir.operation_count=1u;
    quantapdf_composer_clip_state clip{};
    clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clip.commands=commands;clip.command_count=5u;
    clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    const quantapdf_composer_clip_state *clips[1]={&clip};
    quantapdf_native_device_plan plan{};
    const quantapdf_ir_raster_v1_limits limit={dpi==72u?50000u:1000000u,1u,16u,8000000u};
    if(quantapdf_native_device_plan_create_physical(
        "0 0 237.133 180.44","0 0 237.133 180.44","1",0,dpi,
        limit.max_pixels,&plan)!=QUANTAPDF_OK)return false;
    if(dpi!=72u || buffer_rect) {
        /* Exact synthetic quarter/half-point rectangle extends past the
         * true decimal right/bottom edges and maps to full buffer bounds.
         * It is a fringe control only, never replacement original paths. */
        const float right=dpi==72u?238.0f:dpi==96u?237.75f:237.5f;
        const float bottom=dpi==72u?181.0f:dpi==96u?180.75f:180.5f;
        commands[0].point1={0.0f,0.0f};
        commands[1].point1={right,0.0f};
        commands[2].point1={right,bottom};
        commands[3].point1={0.0f,bottom};
    }
    for(int transparent:{0,1})for(size_t count:{0u,1u}) {
        /* The legacy clipped full-page rectangle's exhaustive quota is
         * independently strict; higher-DPI fringe controls use its exact
         * full-canvas Fill fast path within the same 8M cap. */
        if(dpi!=72u && count!=0u)continue;
        page.suppress_background=transparent;
        quantapdf_ir_raster_image out{};
        if(quantapdf_ir_raster_render_v1_physical_planned(
            &ir,0u,&limit,&plan,count?clips:nullptr,count,&out)!=QUANTAPDF_OK ||
           out.pixels==nullptr || out.width!=plan.width || out.height!=plan.height)
            return false;
        for(size_t y=0u;y<plan.height;++y)for(size_t x=0u;x<plan.width;++x) {
            /* Exact decimal arithmetic reference: sample (8*x+2*s+1)/8
             * lies inside iff numerator*1000 < page_millipoints*8. */
            unsigned nx=0u,ny=0u;
            for(unsigned k=0u;k<4u;++k) {
                nx+=((8u*x+2u*k+1u)*UINT64_C(72000)<UINT64_C(237133)*8u*dpi);
                ny+=((8u*y+2u*k+1u)*UINT64_C(72000)<UINT64_C(180440)*8u*dpi);
            }
            const unsigned n=nx*ny;
            const unsigned alpha=transparent?(255u*n+8u)/16u:255u;
            const unsigned gb=transparent?0u:(255u*(16u-n)+8u)/16u;
            const uint32_t expected=(UINT32_C(0xff)<<24u)|(gb<<16u)|
                                    (gb<<8u)|alpha;
            if(rgba_at(out,x,y)!=expected) {
                std::fprintf(stderr,"physical fringe (%zu,%zu) alpha=%d clip=%zu "
                    "actual=%08x expected=%08x\n",x,y,transparent,count,
                    rgba_at(out,x,y),expected);
                quantapdf_ir_raster_image_dispose(&out);return false;
            }
        }
        quantapdf_ir_raster_image_dispose(&out);
    }
    /* A FIT plan or a missing physical anchor cannot opt into physical
     * rendering by changing its mode tag. Legacy planned entry stays FIT. */
    quantapdf_ir_raster_image denied;
    const auto valid=plan;
    auto zero=[](const quantapdf_ir_raster_image& image) {
        return image.pixels==nullptr && image.width==0u && image.height==0u &&
               image.stride==0u && image.size==0u;
    };
    std::memset(&denied,0xa5,sizeof(denied));
    if(quantapdf_ir_raster_render_v1_planned(&ir,0u,&limit,&plan,nullptr,0u,
        &denied)!=QUANTAPDF_ERROR_UNSUPPORTED || !zero(denied))return false;
    if(quantapdf_native_device_plan_create("0 0 237.133 180.44",
        "0 0 237.133 180.44","1",0,dpi,limit.max_pixels,&plan)!=QUANTAPDF_OK)
        return false;
    for(int retag:{0,1}) {
        if(retag)plan.mode=QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1;
        std::memset(&denied,0xa5,sizeof(denied));
        if(quantapdf_ir_raster_render_v1_physical_planned(&ir,0u,&limit,&plan,
            nullptr,0u,&denied)!=QUANTAPDF_ERROR_UNSUPPORTED || !zero(denied))
            return false;
    }
    plan=valid;plan.from_composer[5]=0.25;
    std::memset(&denied,0xa5,sizeof(denied));
    if(quantapdf_ir_raster_render_v1_physical_planned(&ir,0u,&limit,&plan,
        nullptr,0u,&denied)!=QUANTAPDF_ERROR_UNSUPPORTED || !zero(denied))
        return false;
    std::printf("Physical %u DPI fringe: every RGBA pixel matches exact rational "
                "sample counts, opaque/transparent%s\n",dpi,
                dpi==72u?", with/without Clip":"");
    return true;
}

/* The SAME untouched PDF, fully owned Source M3 graph and three source
 * operations. The independent authored-decimal reference uses polynomial
 * ray roots + all quintic stationary-distance roots, never native pixels. */
/* Use one existing positive render to prove that actual Source geometry
 * survives destruction of every temporary IR/command/device-plan view. No
 * extra expensive original-PDF execution or test allocation hook is added. */
/* Exercise the production adoption API on an existing positive render.
 * Restore the test fixture by moving its IR back only after compiled disposal;
 * production instead disposes the entire owner. No Source parse/copy/render is added. */
static quantapdf_status render_adopted_page(quantapdf_ir& source,
    const quantapdf_ir_raster_scoped_mixed_v5_limits& limits,unsigned dpi,
    quantapdf_native_execution_receipt_v1 *receipt,quantapdf_ir_raster_image *out)
{
    if(receipt!=nullptr)std::memset(receipt,0,sizeof(*receipt));
    if(out==nullptr)return QUANTAPDF_ERROR_ARGUMENT;
    std::memset(out,0,sizeof(*out));
    const quantapdf_ir before=source;
    quantapdf_native_source_page_v1 page{};
    quantapdf_native_source_raster_device_v1 device{};device.dpi=(float)dpi;
    auto mutable_limits=limits;
    quantapdf_context *context=nullptr;
    auto status=quantapdf_context_create(&context);
    if(status!=QUANTAPDF_OK)return status;
    quantapdf_context_lease_internal domain{context,quantapdf_drop_context};
    status=quantapdf_native_source_page_prepare_context_v1(&source,&mutable_limits,&device,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&domain,&page);
    if(status!=QUANTAPDF_OK) {
        const bool unchanged=std::memcmp(&source,&before,sizeof(source))==0 && domain.context==context;
        quantapdf_context_lease_drop_internal(&domain);
        return unchanged?status:QUANTAPDF_ERROR_BACKEND;
    }
    const bool adopted=cleared(source) && domain.context==nullptr && domain.drop==nullptr &&
        page.domain.context==context && context->references==1u && page.ir.pages==before.pages &&
        page.ir.operations==before.operations && page.compiled.metadata.source==nullptr &&
        page.compiled.ledger.completed_leases==0u && page.compiled.ledger.used.sample_work==0u &&
        page.compiled.ledger.resources.live_bytes==page.compiled.metadata.usage.retained_bytes+
            qp_compiled_retained_bytes(&page.compiled);
    std::memset(&mutable_limits,0xa5,sizeof(mutable_limits));std::memset(&device,0xa5,sizeof(device));
    status=adopted?quantapdf_native_source_page_execute_v1(&page,receipt,out):QUANTAPDF_ERROR_BACKEND;
    quantapdf_native_compiled_page_dispose_v1(&page.compiled);
    source=page.ir;std::memset(&page.ir,0,sizeof(page.ir));page.owns_ir=0;
    quantapdf_native_source_page_dispose_v1(&page);
    if(std::memcmp(&source,&before,sizeof(source))!=0)return QUANTAPDF_ERROR_BACKEND;
    return status;
}

static quantapdf_status render_detached_geometry(const quantapdf_ir& source,
    const quantapdf_native_prepared_page_v1& prepared,size_t *work,quantapdf_ir_raster_image *out)
{
    quantapdf_composer_path_command commands[4096]{};
    quantapdf_outline_glyph glyphs[64]{};
    quantapdf_composer_operation ops[4]{};
    quantapdf_composer_clip_state clips[1]{};
    quantapdf_composer_graphics_state states[1]{};
    quantapdf_composer_page_state page=source.pages[0];
    quantapdf_ir view=source;size_t used=0u,nglyphs=0u;
    const auto copy=[&](const quantapdf_composer_path_command *input,size_t n) {
        if(n>4096u-used)return static_cast<quantapdf_composer_path_command *>(nullptr);
        auto *result=commands+used;std::memcpy(result,input,n*sizeof(*input));used+=n;return result;
    };
    if(work)*work=0u;
    std::memset(out,0,sizeof(*out));
    if(source.operation_count>4u || source.resources.clip_count>1u || source.resources.graphics_state_count>1u)
        return QUANTAPDF_ERROR_BACKEND;
    for(size_t i=0u;i<source.operation_count;++i) {
        ops[i]=source.operations[i];
        if(ops[i].kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            auto& path=ops[i].value.path;path.commands=copy(path.commands,path.command_count);
            if(path.commands==nullptr)return QUANTAPDF_ERROR_BACKEND;
        } else {
            auto& text=ops[i].value.text_outlines;
            if(text.glyph_count>64u-nglyphs)return QUANTAPDF_ERROR_BACKEND;
            text.glyphs=text.glyph_count!=0u?glyphs+nglyphs:nullptr;
            for(size_t j=0u;j<text.glyph_count;++j) {
                auto& glyph=glyphs[nglyphs++];glyph=source.operations[i].value.text_outlines.glyphs[j];
                glyph.commands=copy(glyph.commands,glyph.command_count);
                if(glyph.commands==nullptr)return QUANTAPDF_ERROR_BACKEND;
            }
        }
    }
    view.pages=&page;view.operations=ops;
    if(source.resources.clip_count) {
        clips[0]=source.resources.clips[0];clips[0].commands=copy(clips[0].commands,clips[0].command_count);
        if(clips[0].commands==nullptr)return QUANTAPDF_ERROR_BACKEND;
        view.resources.clips=clips;
    }
    if(source.resources.graphics_state_count) {
        states[0]=source.resources.graphics_states[0];view.resources.graphics_states=states;
    }
    auto device=prepared.physical;quantapdf_native_work_ledger_v1 ledger{};
    quantapdf_native_work_init_v1(&ledger,prepared.limits.max_sample_tests,prepared.limits.max_edges);
    if(!quantapdf_native_resources_init_v1(&ledger,&prepared.resource_limits,prepared.usage.retained_bytes))
        return QUANTAPDF_ERROR_BACKEND;
    quantapdf_native_geometry_v1 geometry{};
    auto status=prepared.route==QP_NATIVE_PREPARED_TEXT?
        quantapdf_native_geometry_prepare_text_v1(&view,&prepared.physical_limits,&device,&ledger,&geometry):
        quantapdf_native_geometry_prepare_texample_v1(&view,&prepared.physical_limits,&device,&ledger,&geometry);
    if(status!=QUANTAPDF_OK)return status;
    quantapdf_native_geometry_info_v1 info{};
    if(quantapdf_native_geometry_info_get_v1(&geometry,&info)!=QUANTAPDF_OK || info.commands!=used) {
        quantapdf_native_geometry_dispose_v1(&geometry);return QUANTAPDF_ERROR_BACKEND;
    }
    std::memset(commands,0xa5,used*sizeof(*commands));std::memset(glyphs,0xa5,sizeof(glyphs));
    std::memset(ops,0xa5,sizeof(ops));std::memset(clips,0xa5,sizeof(clips));
    std::memset(states,0xa5,sizeof(states));std::memset(&view,0xa5,sizeof(view));
    std::memset(&page,0xa5,sizeof(page));std::memset(&device,0xa5,sizeof(device));
    status=quantapdf_native_geometry_execute_v1(&geometry,work,out);
    quantapdf_native_geometry_dispose_v1(&geometry);
    if(ledger.resources.live_bytes!=prepared.usage.retained_bytes+(status==QUANTAPDF_OK?out->size:0u) ||
       (status==QUANTAPDF_OK && (!ledger.samples_exact || ledger.used.edges!=info.edges))) {
        quantapdf_ir_raster_image_dispose(out);if(work)*work=0u;return QUANTAPDF_ERROR_BACKEND;
    }
    return status;
}

static bool original_texample_three_layer(quantapdf_ir& source,
    const char *original_filename,quantapdf_ir_raster_image& detached,bool shared=false,unsigned dpi=72u)
{
    if(!exact_texample_private_ir(source) ||
       (dpi!=72u && dpi!=96u && dpi!=144u) || (!shared && dpi!=72u))return false;
    const bool physical_dpi=dpi!=72u;
    const size_t width=dpi==72u?238u:dpi==96u?317u:475u;
    const size_t height=dpi==72u?181u:dpi==96u?241u:361u;
    const size_t minimum_edges=physical_dpi?(dpi==96u?416u:512u):shared?384u:768u;
    const auto filename=std::filesystem::path(original_filename).parent_path()
        .parent_path().parent_path()/"native"/"goldens"/("texample"+std::to_string(dpi)+
            (physical_dpi?"-m4c.rgba":"-m4b.rgba"));
    std::ifstream input(filename,std::ios::binary);
    const std::string expected{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    if(expected.size()!=width*height*4u)return false;
    quantapdf_native_device_plan plan{};
    const quantapdf_ir_raster_v1_limits limits={physical_dpi?1000000u:50000u,3u,shared?512u:1024u,8000000u};
    if(quantapdf_native_device_plan_create_physical("0 0 237.133 180.44",
        "0 0 237.133 180.44","1",0,dpi,limits.max_pixels,&plan)!=QUANTAPDF_OK)
        return false;
    const auto kernel=physical_dpi?
        quantapdf_ir_raster_render_v1_texample_physical_dpi_three_layer:shared?
        quantapdf_ir_raster_render_v1_texample_shared_three_layer:
        quantapdf_ir_raster_render_v1_texample_physical_three_layer;
    size_t memory_cap=0u;bool detach_once=true;
    const auto render=[&](const quantapdf_ir *ir,const quantapdf_ir_raster_v1_limits *budget,
        const quantapdf_native_device_plan *device,size_t *observed_work,quantapdf_ir_raster_image *out) {
        /* Keep legacy and explicitly malformed private-plan tests on their
         * original entry. Public-shaped shared execution uses preparation,
         * including its exact N/N-1 and every real malloc fault/recovery. */
        if(!shared || ir!=&source || std::memcmp(device,&plan,sizeof(plan))!=0)
            return kernel(ir,budget,device,observed_work,out);
        if(observed_work!=nullptr)*observed_work=0u;
        std::memset(out,0,sizeof(*out));
        const quantapdf_ir_raster_scoped_mixed_v5_limits caps={budget->max_pixels,
            budget->max_operations,budget->max_edges,budget->max_sample_tests,1000000u,4000000u};
        quantapdf_native_source_raster_device_v1 request{};request.dpi=(float)dpi;
        quantapdf_native_prepared_page_v1 prepared{};
        auto result=quantapdf_native_prepare_page_v1(ir,&caps,&request,
            QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&prepared);
        if(result!=QUANTAPDF_OK)return result;
        if(prepared.route!=QP_NATIVE_PREPARED_EXACT_CUBICS)return QUANTAPDF_ERROR_BACKEND;
        if(memory_cap!=0u)prepared.resource_limits.max_live_bytes=memory_cap;
        if(detach_once && memory_cap!=0u && budget->max_sample_tests<limits.max_sample_tests) {
            detach_once=false;return render_detached_geometry(*ir,prepared,observed_work,out);
        }
        quantapdf_native_execution_receipt_v1 receipt{};
        result=(memory_cap==0u || (track_c_allocation && c_fail_index!=0u))?
            render_adopted_page(source,caps,dpi,&receipt,out):
            quantapdf_native_execute_page_v1(&prepared,&receipt,out);
        if(result==QUANTAPDF_OK && (!receipt.sample_tests_exact ||
           receipt.charged_sample_work!=receipt.sample_tests ||
           receipt.compiled_edges==0u || receipt.compiled_edges>budget->max_edges ||
           receipt.completed_leases!=1u ||
           receipt.peak_reserved_sample_work!=budget->max_sample_tests ||
           receipt.peak_reserved_edges!=budget->max_edges ||
           receipt.source_pixels!=0u || receipt.drawn_pixels!=0u ||
           receipt.retained_bytes!=prepared.usage.retained_bytes || receipt.output_bytes!=out->size ||
           receipt.peak_live_bytes<receipt.retained_bytes+receipt.output_bytes ||
           receipt.peak_live_bytes>prepared.resource_limits.max_live_bytes))return QUANTAPDF_ERROR_BACKEND;
        if(result==QUANTAPDF_OK) {
            /* Reuse existing N/N-1/edge/OOM runs at the observed exact byte
             * peak; no additional expensive original-PDF render is added. */
            if(memory_cap==0u)memory_cap=receipt.peak_live_bytes;
            else if(budget->max_edges==limits.max_edges && receipt.peak_live_bytes!=memory_cap)
                return QUANTAPDF_ERROR_BACKEND;
        }
        if(result!=QUANTAPDF_OK && (receipt.charged_sample_work!=0u ||
           receipt.compiled_edges!=0u || receipt.completed_leases!=0u ||
           receipt.peak_reserved_sample_work!=0u || receipt.peak_reserved_edges!=0u ||
           receipt.source_pixels!=0u || receipt.drawn_pixels!=0u || receipt.retained_bytes!=0u ||
           receipt.output_bytes!=0u || receipt.peak_live_bytes!=0u))
            return QUANTAPDF_ERROR_BACKEND;
        if(observed_work!=nullptr)*observed_work=receipt.sample_tests;
        return result;
    };
    auto zero=[](const quantapdf_ir_raster_image& x) {
        return x.pixels==nullptr && x.width==0u && x.height==0u &&
               x.stride==0u && x.size==0u;
    };
    auto equal=[&](const quantapdf_ir_raster_image& x) {
        return x.pixels!=nullptr && x.width==width && x.height==height &&
            x.stride==width*4u && x.size==expected.size() &&
            std::memcmp(x.pixels,expected.data(),expected.size())==0;
    };
    auto snapshot=[&]() {
        std::string bytes;
        auto append=[&](const void *p,size_t size) {
            bytes.append(static_cast<const char *>(p),size);
        };
        append(source.pages,sizeof(*source.pages));
        append(source.operations,3u*sizeof(*source.operations));
        append(source.resources.clips,sizeof(*source.resources.clips));
        append(source.resources.graphics_states,sizeof(*source.resources.graphics_states));
        append(source.resources.clips[0].commands,12u*sizeof(quantapdf_composer_path_command));
        for(size_t i=0u;i<3u;++i)
            append(source.operations[i].value.path.commands,
                   12u*sizeof(quantapdf_composer_path_command));
        return bytes;
    };
    const std::string frozen=snapshot();
    quantapdf_ir_raster_image full{};
    size_t work=0u,allocations=0u;
    track_c_allocation=true;c_fail_index=0u;c_allocations=0u;c_faults=0u;
    const auto status=render(
        &source,&limits,&plan,&work,&full);
    allocations=c_allocations;track_c_allocation=false;
    if(status!=QUANTAPDF_OK || !equal(full) || allocations!=4u ||
       c_faults!=0u || work==0u || work>=limits.max_sample_tests ||
       (!physical_dpi && (rgba_at(full,146u,87u)!=UINT32_C(0xbfbfbfff) ||
        rgba_at(full,118u,20u)!=UINT32_C(0xff8000ff) ||
        rgba_at(full,118u,90u)!=UINT32_C(0xffffffff)))) {
        std::fprintf(stderr,"original three-layer status=%d work=%zu allocations=%zu "
            "known pixel=%08x reference match=%d\n",(int)status,work,allocations,
            full.pixels?rgba_at(full,146u,87u):0u,equal(full)?1:0);
        quantapdf_ir_raster_image_dispose(&full);return false;
    }
    auto exact=limits;exact.max_sample_tests=work;
    quantapdf_ir_raster_image repeat{};
    size_t repeat_work=0u;
    if(render(&source,
        &exact,&plan,&repeat_work,&repeat)!=QUANTAPDF_OK ||
       repeat_work!=work || !equal(repeat)) {
        quantapdf_ir_raster_image_dispose(&repeat);
        quantapdf_ir_raster_image_dispose(&full);return false;
    }
    quantapdf_ir_raster_image_dispose(&repeat);
    if(shared) {
        auto edges=limits;edges.max_edges=minimum_edges;
        if(render(&source,&edges,&plan,&repeat_work,&repeat)!=QUANTAPDF_OK ||
           repeat_work!=work || !equal(repeat)) {
            quantapdf_ir_raster_image_dispose(&repeat);
            quantapdf_ir_raster_image_dispose(&full);return false;
        }
        quantapdf_ir_raster_image_dispose(&repeat);
    }
    exact.max_sample_tests=work-1u;
    std::memset(&repeat,0xa5,sizeof(repeat));repeat_work=999u;
    if(render(&source,
        &exact,&plan,&repeat_work,&repeat)!=QUANTAPDF_ERROR_UNSUPPORTED ||
       repeat_work!=0u || !zero(repeat)) {
        quantapdf_ir_raster_image_dispose(&full);return false;
    }
    /* Every real temporary/output malloc fails once; complete reset and
     * fresh original golden equality prove cleanup and recovery. */
    for(size_t fail=1u;fail<=allocations;++fail) {
        std::memset(&repeat,0xa5,sizeof(repeat));repeat_work=999u;
        track_c_allocation=true;c_fail_index=fail;c_allocations=0u;c_faults=0u;
        const auto ret=render(
            &source,&limits,&plan,&repeat_work,&repeat);
        track_c_allocation=false;c_fail_index=0u;
        if(ret!=QUANTAPDF_ERROR_NOMEM || !zero(repeat) || repeat_work!=0u ||
           c_faults!=1u || c_allocations!=fail) {
            std::fprintf(stderr,"three-layer OOM fail=%zu status=%d seen=%zu\n",
                fail,(int)ret,c_allocations);
            quantapdf_ir_raster_image_dispose(&full);return false;
        }
        if(render(&source,
            &limits,&plan,&repeat_work,&repeat)!=QUANTAPDF_OK ||
           repeat_work!=work || !equal(repeat)) {
            quantapdf_ir_raster_image_dispose(&repeat);
            quantapdf_ir_raster_image_dispose(&full);return false;
        }
        quantapdf_ir_raster_image_dispose(&repeat);
    }
    for(unsigned which=0u;which<(physical_dpi?27u:25u);++which) {
        auto bad=source;
        auto page=source.pages[0];
        auto gs=source.resources.graphics_states[0];
        auto clip=source.resources.clips[0];
        quantapdf_composer_font_state hidden_font{};
        quantapdf_composer_operation ops[4]{};
        for(size_t i=0u;i<3u;++i)ops[i]=source.operations[i];
        quantapdf_composer_path_command cmds[12];
        std::memcpy(cmds,source.operations[1].value.path.commands,sizeof(cmds));
        auto budget=limits;auto device=plan;
        bad.pages=&page;bad.operations=ops;
        bad.resources.graphics_states=&gs;bad.resources.clips=&clip;
        switch(which) {
        case 0:budget.max_sample_tests=8000001u;break;
        case 1:budget.max_edges=minimum_edges-1u;break;
        case 2:budget.max_pixels=width*height-1u;break;
        case 3:budget.max_operations=4u;break;
        case 4:bad.operation_count=4u;ops[3]=ops[0];break;
        case 5:ops[0].graphics_state_id=0u;
               ops[0].value.path.options.graphics_state_id=0u;break;
        case 6:ops[1].graphics_state_id=1u;
               ops[1].value.path.options.graphics_state_id=1u;break;
        case 7:clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;break;
        case 8:gs.fill_alpha=0.5f;break;
        case 9:gs.clip_id=0u;break;
        case 10:gs.soft_mask_id=1u;break;
        case 11:bad.resources.fonts=&hidden_font;bad.resources.font_capacity=1u;break;
        case 12:clip.commands=nullptr;break;
        case 13:ops[0].value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;break;
        case 14:ops[2].value.path.options.stroke_width=0.4f;break;
        case 15:ops[2].value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND;break;
        case 16:ops[1].value.path.options.transform.e=1.0f;break;
        case 17:cmds[1].point2.y+=0.001f;
                ops[1].value.path.commands=cmds;break;
        case 18:cmds[4].point3.y+=0.00001f;
                ops[1].value.path.commands=cmds;break;
        case 19:ops[1]=source.operations[2];ops[2]=source.operations[1];break;
        case 20:device.from_composer[0]=238.0/237.133;break;
        case 21:device.mode=QUANTAPDF_NATIVE_DEVICE_FIT_V1;break;
        case 22:page.height_points=181.0f;break;
        case 23:ops[2].value.path.options.stroke_paint_id=1u;break;
        case 24:ops[2].value.path.options.fill_paint_id=1u;break;
        case 25:device.dpi=72u;break;
        case 26:device.dpi=120u;break;
        }
        std::memset(&repeat,0xa5,sizeof(repeat));repeat_work=999u;
        const auto ret=render(
            &bad,&budget,&device,&repeat_work,&repeat);
        if(ret!=QUANTAPDF_ERROR_UNSUPPORTED || !zero(repeat) || repeat_work!=0u) {
            std::fprintf(stderr,"three-layer negative=%u status=%d\n",which,(int)ret);
            quantapdf_ir_raster_image_dispose(&full);return false;
        }
    }
    if(snapshot()!=frozen) {
        quantapdf_ir_raster_image_dispose(&full);return false;
    }
    std::printf("Original TeXample %u DPI %s three-layer: %zu authored-decimal reference "
        "bytes identical; work N=%zu, N-1 atomic refusal, four malloc faults "
        "and recovery, %u malformed/scope/quota refusals, %zu/%zu edge boundary\n",
        dpi,shared?"shared-geometry":"legacy",expected.size(),work,
        physical_dpi?27u:25u,minimum_edges,minimum_edges-1u);
    detached=full;
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
    const auto public_read=[&](quantapdf_ir& out) {
        return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
            reinterpret_cast<const unsigned char*>(original.data()),
            original.size(),&limits,0u,&out);
    };
    /* Public V10 must reach the same complete original owned graph in
     * one callback pass, not the retained private fixture selector. */
    {
        quantapdf_ir ir{};
        track_c_allocation=true;c_fail_index=0u;c_allocations=0u;c_faults=0u;
        const auto status=public_read(ir);
        const size_t public_allocations=c_allocations;
        track_c_allocation=false;
        if(status!=QUANTAPDF_OK || !exact_texample_private_ir(ir) ||
           public_allocations<7u || public_allocations>32u || c_faults!=0u) {
            std::fprintf(stderr,"original TeXample public Source=%d allocations=%zu\n",
                         (int)status,public_allocations);
            quantapdf_ir_dispose(&ir);return false;
        }
        quantapdf_ir_raster_image shared{};
        if(!original_texample_three_layer(ir,filename,shared,true)) {
            quantapdf_ir_dispose(&ir);return false;
        }
        quantapdf_ir_raster_image high_dpi[2]{};
        std::string high_bytes[2];
        for(size_t i=0u;i<2u;++i) {
            if(!original_texample_three_layer(ir,filename,high_dpi[i],true,
                i==0u?96u:144u)) {
                for(auto& image:high_dpi)quantapdf_ir_raster_image_dispose(&image);
                quantapdf_ir_raster_image_dispose(&shared);
                quantapdf_ir_dispose(&ir);return false;
            }
            high_bytes[i].assign(reinterpret_cast<const char*>(high_dpi[i].pixels),
                                 high_dpi[i].size);
        }
        quantapdf_ir_dispose(&ir);
        bool high_detached=true;
        for(size_t i=0u;i<2u;++i) {
            high_detached=high_detached && high_dpi[i].pixels!=nullptr &&
                std::memcmp(high_dpi[i].pixels,high_bytes[i].data(),high_bytes[i].size())==0;
            quantapdf_ir_raster_image_dispose(&high_dpi[i]);
        }
        const bool detached=high_detached && cleared(ir) && shared.pixels!=nullptr &&
            rgba_at(shared,146u,87u)==UINT32_C(0xbfbfbfff);
        const std::string reference(reinterpret_cast<const char*>(shared.pixels),
                                    shared.size);
        quantapdf_ir_raster_image_dispose(&shared);
        if(!detached)return false;
        quantapdf_native_device_plan recovery_plan{};
        const quantapdf_ir_raster_v1_limits recovery_limits={50000u,3u,512u,8000000u};
        if(quantapdf_native_device_plan_create_physical("0 0 237.133 180.44",
            "0 0 237.133 180.44","1",0,72u,50000u,&recovery_plan)!=QUANTAPDF_OK)
            return false;
        for(size_t fail=1u;fail<=public_allocations;++fail) {
            std::memset(&ir,0xa5,sizeof(ir));
            track_c_allocation=true;c_fail_index=fail;c_allocations=0u;c_faults=0u;
            const auto denied=public_read(ir);
            const size_t seen=c_allocations,fired=c_faults;
            track_c_allocation=false;c_fail_index=0u;
            if(denied!=QUANTAPDF_ERROR_NOMEM || !cleared(ir) ||
               seen!=fail || fired!=1u) {
                std::fprintf(stderr,"public Source OOM fail=%zu status=%d\n",fail,(int)denied);
                quantapdf_ir_dispose(&ir);return false;
            }
            if(public_read(ir)!=QUANTAPDF_OK || !exact_texample_private_ir(ir)) {
                quantapdf_ir_dispose(&ir);return false;
            }
            quantapdf_ir_raster_image recovered{};
            const auto rendered=quantapdf_ir_raster_render_v1_texample_shared_three_layer(
                &ir,&recovery_limits,&recovery_plan,nullptr,&recovered);
            quantapdf_ir_dispose(&ir);
            const bool recovered_pixels=rendered==QUANTAPDF_OK && cleared(ir) &&
                recovered.pixels!=nullptr && recovered.size==reference.size() &&
                std::memcmp(recovered.pixels,reference.data(),reference.size())==0;
            quantapdf_ir_raster_image_dispose(&recovered);
            if(!recovered_pixels)return false;
        }
        std::printf("Original TeXample public Source: %zu C allocation faults and full-image recovery\n",
                    public_allocations);
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
        quantapdf_ir_raster_image physical_held{};
        size_t physical_cap=0u;
        if(!original_texample_m4a_curved_strokes(ir,physical_held,
                physical_cap,true) || !exact_texample_private_ir(ir)) {
            quantapdf_ir_raster_image_dispose(&held);
            quantapdf_ir_raster_image_dispose(&physical_held);
            quantapdf_ir_dispose(&ir);return false;
        }
        /* The Source M3 producer already used the EXACT decimal Y
         * anchor. No path is near the page fringe, so the real physical
         * mapping must preserve every original M4a sample byte here. */
        if(physical_held.size!=held.size ||
           std::memcmp(physical_held.pixels,held.pixels,held.size)!=0) {
            quantapdf_ir_raster_image_dispose(&held);
            quantapdf_ir_raster_image_dispose(&physical_held);
            quantapdf_ir_dispose(&ir);return false;
        }
        quantapdf_ir_raster_image three_layer_held{};
        if(!original_texample_three_layer(ir,filename,three_layer_held) ||
           !exact_texample_private_ir(ir)) {
            quantapdf_ir_raster_image_dispose(&held);
            quantapdf_ir_raster_image_dispose(&physical_held);
            quantapdf_ir_raster_image_dispose(&three_layer_held);
            quantapdf_ir_dispose(&ir);return false;
        }
        const uint32_t physical_top=rgba_at(physical_held,90u,5u);
        /* A true raster pixel owner must outlive ALL original QPDF
         * Source, Clip, PAINT and IR allocations. */
        const uint32_t original_top=rgba_at(held,90u,5u);
        quantapdf_ir_dispose(&ir);
        const bool detached_ok=cleared(ir) &&
            held.pixels!=nullptr &&
            rgba_at(held,90u,5u)==original_top &&
            rgba_at(held,90u,90u)==UINT32_C(0xffffffff);
        const bool physical_detached_ok=physical_held.pixels!=nullptr &&
            rgba_at(physical_held,90u,5u)==physical_top &&
            rgba_at(physical_held,90u,90u)==UINT32_C(0xffffffff);
        quantapdf_ir_raster_image_dispose(&held);
        const bool three_detached_ok=three_layer_held.pixels!=nullptr &&
            rgba_at(three_layer_held,146u,87u)==UINT32_C(0xbfbfbfff) &&
            rgba_at(three_layer_held,118u,20u)==UINT32_C(0xff8000ff);
        quantapdf_ir_raster_image_dispose(&physical_held);
        quantapdf_ir_raster_image_dispose(&three_layer_held);
        if(!three_detached_ok || !detached_ok || !physical_detached_ok || curved_cap==0u ||
           physical_cap==0u)return false;
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

/* Negative variants alone are reserialized by QPDF; every positive recovery
 * reopens the untouched 1396-byte external original through the PUBLIC API. */
static bool public_texample_complete_page(const char *filename)
{
    const auto golden=std::filesystem::path(filename).parent_path().parent_path()
        .parent_path()/"native"/"goldens"/"texample72-m4b.rgba";
    std::ifstream input(golden,std::ios::binary);
    const std::string rgba{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    if(rgba.size()!=172312u)return false;
    quantapdf_context *ctx=nullptr;
    if(quantapdf_context_create(&ctx)!=QUANTAPDF_OK)return false;
    auto render=[&](const char *file,quantapdf_bitmap **out) {
        quantapdf_document *doc=nullptr;quantapdf_page *page=nullptr;
        auto status=quantapdf_open_with_context(ctx,file,nullptr,&doc);
        if(status==QUANTAPDF_OK)status=quantapdf_load_page(doc,0,&page);
        if(status==QUANTAPDF_OK) {
            quantapdf_render_options options{};
            options.struct_size=sizeof(options);options.dpi=72.0f;
            status=quantapdf_render_page_with_options(page,&options,out);
        }
        if(page!=nullptr)quantapdf_drop_page(page);
        if(doc!=nullptr)quantapdf_close(doc);
        return status;
    };
    auto equal=[&](quantapdf_bitmap *bmp) {
        int w=0,h=0,stride=0,channels=0;
        const unsigned char *rgb=nullptr;size_t bytes=0u;
        if(bmp==nullptr || quantapdf_bitmap_dimensions(bmp,&w,&h,&stride,&channels)!=
           QUANTAPDF_OK || quantapdf_bitmap_data(bmp,&rgb,&bytes)!=QUANTAPDF_OK ||
           w!=238 || h!=181 || stride!=714 || channels!=3 || rgb==nullptr ||
           bytes!=129234u)return false;
        for(size_t i=0u;i<238u*181u;++i) {
            for(size_t c=0u;c<3u;++c)
                if(rgb[i*3u+c]!=static_cast<unsigned char>(rgba[i*4u+c]))return false;
            if(static_cast<unsigned char>(rgba[i*4u+3u])!=255u)return false;
        }
        return true;
    };
    for(unsigned which=0u;which<15u;++which) {
        auto pdf=QPDF::create();pdf->processFile(filename);
        auto page=pdf->getRoot().getKey("/Pages").getKey("/Kids").getArrayItem(0);
        const auto raw=page.getKey("/Contents").getStreamData();
        std::string stream(reinterpret_cast<const char*>(raw->getBuffer()),raw->getSize());
        auto replace=[&](const char *from,const char *to) {
            const auto pos=stream.find(from);
            if(pos==std::string::npos)return false;
            stream.replace(pos,std::strlen(from),to);return true;
        };
        bool edited=true;
        switch(which) {
        case 0:stream+="\nBT ET\n";break;
        case 1:stream+="\n0 g\n";break;
        case 2:stream+="\n0 0 1 1 re f\n";break;
        case 3:{const auto pos=stream.rfind("S");
                edited=pos!=std::string::npos;if(edited)stream.erase(pos,1u);break;}
        case 4:edited=replace("W*","W");break;
        case 5:edited=replace("f*","f");break;
        case 6:edited=replace("0.3985 w","0.4 w");break;
        case 7:edited=replace("1 0.5 0 rg","1 0.49999 0 rg");break;
        case 8:edited=replace("28.3468 31.46452","28.3478 31.46452");break;
        case 9:page.replaceKey("/MediaBox",Obj::parse("[0 0 237.133 181]"));break;
        case 10:page.replaceKey("/UserUnit",Obj::newInteger(2));break;
        case 11:page.replaceKey("/Rotate",Obj::newInteger(90));break;
        case 12:page.replaceKey("/CropBox",Obj::parse("[0 0 237 180]"));break;
        case 13:stream+="\n/Missing Do\n";break;
        case 14:stream+="\nQ\n";break;
        }
        if(!edited) {quantapdf_drop_context(ctx);return false;}
        page.replaceKey("/Contents",pdf->newStream(stream));
        QPDFWriter writer(*pdf);writer.setOutputMemory();writer.write();
        const auto bytes=writer.getBufferSharedPointer();
        const std::filesystem::path badfile="quantapdf-texample-public-negative.pdf";
        {
            std::ofstream out(badfile,std::ios::binary|std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes->getBuffer()),
                      static_cast<std::streamsize>(bytes->getSize()));
            if(!out.good()) {quantapdf_drop_context(ctx);return false;}
        }
        quantapdf_bitmap *denied=reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        const auto status=render(badfile.string().c_str(),&denied);
        std::filesystem::remove(badfile);
        /* Removing S leaves a pending original path at Q; an extra Q
         * underflows the stack. Both are structurally malformed FORMAT. */
        const auto expected=(which==3u || which==14u)?
            QUANTAPDF_ERROR_FORMAT:QUANTAPDF_ERROR_UNSUPPORTED;
        if(status!=expected || denied!=nullptr) {
            std::fprintf(stderr,"public TeXample negative=%u status=%d bitmap=%p\n",
                         which,(int)status,static_cast<void*>(denied));
            quantapdf_drop_context(ctx);return false;
        }
        quantapdf_bitmap *recovery=nullptr;
        if(render(filename,&recovery)!=QUANTAPDF_OK || !equal(recovery)) {
            if(recovery!=nullptr)quantapdf_drop_bitmap(recovery);
            quantapdf_drop_context(ctx);return false;
        }
        quantapdf_drop_bitmap(recovery);
    }
    quantapdf_drop_context(ctx);
    std::printf("Public TeXample: 15 complete-page/scope/metadata atomic refusals, "
                "untouched original full RGB recovery after each\n");
    return true;
}


static std::vector<unsigned char> text_ir_snapshot(const quantapdf_ir& ir)
{
    std::vector<unsigned char> result;
    const auto append=[&](const void *p,size_t n) {
        const auto b=static_cast<const unsigned char*>(p);result.insert(result.end(),b,b+n);
    };
    append(&ir,sizeof(ir));append(ir.pages,ir.page_count*sizeof(*ir.pages));
    append(ir.operations,ir.operation_count*sizeof(*ir.operations));
    for(size_t i=0u;i<ir.operation_count;++i) {
        if(ir.operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const auto& path=ir.operations[i].value.path;
            append(path.commands,path.command_count*sizeof(*path.commands));continue;
        }
        const auto& text=ir.operations[i].value.text_outlines;
        append(text.glyphs,text.glyph_count*sizeof(*text.glyphs));
        for(size_t g=0u;g<text.glyph_count;++g)
            append(text.glyphs[g].commands,text.glyphs[g].command_count*sizeof(*text.glyphs[g].commands));
    }
    return result;
}
/* The M2a-only page-owned allocation callback observes the real, charged
 * C11 malloc sequence for Text geometry, RAW Image and final RGBA output.
 * It is explicitly not a global malloc override or a separate renderer. */
struct m2a_malloc_fault_state {
    size_t attempts=0u,fail_on=0u,failures=0u;
};
static int m2a_malloc_fault_callback(void *owner,size_t bytes)
{
    auto *state=static_cast<m2a_malloc_fault_state *>(owner);
    if(state==nullptr || bytes==0u)return 0;
    ++state->attempts;
    if(state->fail_on!=0u && state->attempts==state->fail_on) {
        ++state->failures;
        return 1;
    }
    return 0;
}

/* M2a: the SAME two original QPDF PDFs now carry one owned embedded
 * CID-TTF show plus a RAW Image, in both original orders. Test the real
 * Common Source -> IR -> C11 compiled page and error/lifetime boundaries.
 * Keep this compact: the existing 40s Source/OOM gate is unchanged. */
static int m2a_ordered_text_image_source_contract(
    const quantapdf_qpdf_source_raw_image_limits& limits)
{
    target=0u;armed=false;track_c_allocation=false;c_fail_index=0u;
    for(int mode:{49,50,64,65}) {
        /* A public Composer is a separate real retained Context guard.
         * Do NOT link a test to the unexported retain_internal in Windows. */
        quantapdf_context *context=nullptr;
        quantapdf_composer *composer_guard=nullptr;
        CHECK(quantapdf_context_create(&context)==QUANTAPDF_OK);
        CHECK(quantapdf_composer_create_with_context(context,nullptr,
              &composer_guard)==QUANTAPDF_OK &&
              composer_guard!=nullptr && context->references==2u);
        const std::string original=native_ttf_fixture(mode);
        const auto read=[&](quantapdf_ir& ir) {
            return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(original.data()),
                original.size(),&limits,0u,&ir);
        };
        quantapdf_ir ir{};
        if(mode==49) {
            track_c_allocation=true;c_fail_index=0u;
            c_allocations=0u;c_faults=0u;
        }
        const auto read_status=read(ir);
        const size_t source_allocations=c_allocations;
        track_c_allocation=false;
        CHECK(read_status==QUANTAPDF_OK &&
              ir.page_count==1u && ir.operation_count==2u &&
              ir.resources.image_count==1u && ir.resources.images!=nullptr &&
              ir.resources.images[0].size==12u && ir.pages!=nullptr &&
              ir.pages[0].width_points==128.0f &&
              ir.pages[0].height_points==64.0f);
        const bool text_first=mode==49 || mode==64;
        const auto& img=ir.operations[text_first?1u:0u];
        const auto& txt=ir.operations[text_first?0u:1u];
        CHECK(img.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
              img.value.image.image_id==1u &&
              txt.kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES &&
              txt.value.text_outlines.glyph_count==5u &&
              txt.value.text_outlines.glyphs!=nullptr);
        for(size_t g=0u;g<txt.value.text_outlines.glyph_count;++g)
            CHECK(txt.value.text_outlines.glyphs[g].commands!=nullptr &&
                  txt.value.text_outlines.glyphs[g].command_count>0u);
        const quantapdf_ir saved=ir;
        quantapdf_native_source_raster_device_v1 device{};
        device.dpi=72.0f;
        const quantapdf_ir_raster_scoped_mixed_v5_limits caps{
            1000000u,4u,512u,8000000u,
            limits.max_source_pixels,limits.max_drawn_pixels
        };
        quantapdf_native_prepared_page_v1 metadata{};
        CHECK(quantapdf_native_prepare_page_v1(&ir,&caps,&device,
              QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&metadata)==QUANTAPDF_OK &&
              metadata.route==QP_NATIVE_PREPARED_M2A_TEXT_IMAGE &&
              metadata.usage.operations==2u &&
              metadata.usage.glyphs==5u &&
              metadata.usage.expanded_paints==6u &&
              metadata.usage.image_pixels==4u);

        /* One real render fixes sample, edge, memory and placement charges.
         * Its published RGBA must survive independent page/IR teardown. */
        quantapdf_native_execution_receipt_v1 normal{};
        quantapdf_ir_raster_image baseline{};
        CHECK(quantapdf_native_execute_page_v1(&metadata,&normal,&baseline)==QUANTAPDF_OK &&
              normal.sample_tests_exact &&
              normal.sample_tests==normal.charged_sample_work &&
              normal.charged_sample_work>1u &&
              normal.compiled_edges>1u &&
              normal.compiled_edges<=caps.max_edges &&
              normal.source_pixels==4u && normal.drawn_pixels==192u &&
              normal.retained_bytes==metadata.usage.retained_bytes &&
              normal.peak_live_bytes<=metadata.resource_limits.max_live_bytes &&
              normal.output_bytes==128u*64u*4u &&
              normal.completed_leases>=1u &&
              baseline.width==128u && baseline.height==64u &&
              baseline.size==normal.output_bytes &&
              rgba_at(baseline,0u,0u)==UINT32_C(0xffffffff));
        const auto empty=[&](const quantapdf_ir_raster_image& image) {
            return image.pixels==nullptr && image.width==0u &&
                image.height==0u && image.stride==0u && image.size==0u;
        };
        const auto cleared_receipt=[&](const quantapdf_native_execution_receipt_v1& r) {
            return r.sample_tests==0u && r.charged_sample_work==0u &&
                   r.compiled_edges==0u && r.completed_leases==0u &&
                   r.source_pixels==0u && r.drawn_pixels==0u &&
                   r.output_bytes==0u && r.peak_live_bytes==0u;
        };
        const auto retry=[&](
            quantapdf_ir_raster_scoped_mixed_v5_limits bound,
            size_t memory_limit,quantapdf_status wanted,
            size_t fault_index=0u,size_t layer_fault_index=0u) {
            quantapdf_native_prepared_page_v1 prepared{};
            m2a_malloc_fault_state layer_fault{};
            quantapdf_native_execution_receipt_v1 receipt{};
            quantapdf_ir_raster_image result{};
            quantapdf_status status=quantapdf_native_prepare_page_v1(
                &ir,&bound,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
                &prepared);
            if(status==QUANTAPDF_OK) {
                if(memory_limit!=0u)
                    prepared.resource_limits.max_live_bytes=memory_limit;
                if(layer_fault_index!=0u) {
                    layer_fault.fail_on=layer_fault_index;
                    prepared.m2a_malloc_fault.owner=&layer_fault;
                    prepared.m2a_malloc_fault.reject=m2a_malloc_fault_callback;
                }
                track_c_allocation=fault_index!=0u;
                c_fail_index=fault_index;c_allocations=0u;c_faults=0u;
                status=quantapdf_native_execute_page_v1(
                    &prepared,&receipt,&result);
                track_c_allocation=false;c_fail_index=0u;
            }
            bool ok=status==wanted &&
                std::memcmp(&ir,&saved,sizeof(ir))==0 &&
                context->references==2u;
            if(wanted==QUANTAPDF_OK)
                ok=ok && result.size==baseline.size &&
                    std::memcmp(result.pixels,baseline.pixels,result.size)==0 &&
                    receipt.source_pixels==4u && receipt.drawn_pixels==192u;
            else ok=ok && empty(result) && cleared_receipt(receipt);
            if(fault_index!=0u)
                ok=ok && c_allocations==fault_index && c_faults==1u;
            if(layer_fault_index!=0u)
                ok=ok && layer_fault.attempts==layer_fault_index &&
                   layer_fault.failures==1u;
            quantapdf_ir_raster_image_dispose(&result);
            return ok;
        };
        if(mode==49) {
            /* Tightest exact quotas and one-below refusals; all operate
             * on this identical, immutable original owned IR. */
            auto bound=caps;
            bound.max_sample_tests=normal.charged_sample_work;
            CHECK(retry(bound,0u,QUANTAPDF_OK));
            bound.max_sample_tests--;
            CHECK(retry(bound,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            bound=caps;bound.max_edges=normal.compiled_edges;
            CHECK(retry(bound,0u,QUANTAPDF_OK));
            bound.max_edges--;
            CHECK(retry(bound,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            bound=caps;bound.max_pixels=8191u;
            CHECK(retry(bound,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            bound=caps;bound.max_source_pixels=3u;
            CHECK(retry(bound,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            bound=caps;bound.max_drawn_pixels=191u;
            CHECK(retry(bound,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            bound=caps;bound.max_operations=1u;
            CHECK(retry(bound,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            CHECK(retry(caps,normal.peak_live_bytes,QUANTAPDF_OK));
            CHECK(normal.peak_live_bytes>metadata.usage.retained_bytes);
            CHECK(retry(caps,normal.peak_live_bytes-1u,QUANTAPDF_ERROR_UNSUPPORTED));
            /* All 4 actual original Text geometry/mask/raster malloc
             * faults remain real C11 faults; each permits clean retry. */
            for(size_t fail=1u;fail<=4u;++fail) {
                CHECK(retry(caps,0u,QUANTAPDF_ERROR_NOMEM,fail));
                CHECK(retry(caps,0u,QUANTAPDF_OK));
            }
            /* Original exact TTF allocation sites 1..4 above are
             * unchanged. Fault-inject the two additional real sites:
             * #5 is the owned RAW Image RGBA layer and #6 is the
             * final V5 composite RGBA output, both through the same
             * private page ledger; neither failure may publish output
             * or reset the resource balance. Recover on this same IR. */
            CHECK(retry(caps,0u,QUANTAPDF_ERROR_NOMEM,0u,5u));
            CHECK(retry(caps,0u,QUANTAPDF_OK));
            CHECK(retry(caps,0u,QUANTAPDF_ERROR_NOMEM,0u,6u));
            CHECK(retry(caps,0u,QUANTAPDF_OK));
            /* Exhaustively fault-inject EVERY actual C-owned Source
             * materialization allocation (including realloc and all
             * detached TTF glyph command arrays) in this original
             * page. Each failure must clean the entire owned IR; replay
             * first/middle/final failures on the unchanged original.
             * This avoids redundant full font parses under the fixed
             * 40-second ASan Source/OOM CTest deadline. */
            CHECK(source_allocations>=5u && source_allocations<=32u);
            for(size_t fail=1u;fail<=source_allocations;++fail) {
                quantapdf_ir denied{};
                track_c_allocation=true;c_fail_index=fail;
                c_allocations=0u;c_faults=0u;
                const auto denied_status=read(denied);
                track_c_allocation=false;c_fail_index=0u;
                CHECK(denied_status==QUANTAPDF_ERROR_NOMEM &&
                      c_allocations==fail && c_faults==1u &&
                      cleared(denied) &&
                      std::memcmp(&ir,&saved,sizeof(ir))==0);
                if(fail==1u || fail==source_allocations/2u ||
                   fail==source_allocations) {
                    quantapdf_ir recovery{};
                    CHECK(read(recovery)==QUANTAPDF_OK &&
                          recovery.operation_count==2u &&
                          recovery.resources.image_count==1u);
                    quantapdf_ir_dispose(&recovery);
                    CHECK(cleared(recovery));
                }
            }
            /* Unreachable/hidden resource storage is never hidden by
             * selecting only the one Image or first Text show. */
            ++ir.resources.font_count;
            const quantapdf_ir malformed=ir;
            quantapdf_native_prepared_page_v1 rejected{};
            CHECK(quantapdf_native_prepare_page_v1(
                  &ir,&caps,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
                  &rejected)==QUANTAPDF_ERROR_UNSUPPORTED &&
                  std::memcmp(&ir,&malformed,sizeof(ir))==0 &&
                  rejected.source==nullptr);
            --ir.resources.font_count;
            CHECK(std::memcmp(&ir,&saved,sizeof(ir))==0);
        }
        /* Explicit same-Context failure leaves caller-owned IR/lease
         * untouched; successful adoption moves the same retained guard.
         * The detached output survives page and Context-reference drops. */
        /* Move the caller's existing reference only AFTER complete
         * admission. Failed preparation must preserve it untouched. */
        quantapdf_context_lease_internal lease{context,quantapdf_drop_context};
        CHECK(context->references==2u);
        auto too_small=caps;too_small.max_source_pixels=3u;
        quantapdf_native_source_page_v1 refused{};
        CHECK(quantapdf_native_source_page_prepare_context_v1(
                  &ir,&too_small,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
                  &lease,&refused)==QUANTAPDF_ERROR_UNSUPPORTED &&
              std::memcmp(&ir,&saved,sizeof(ir))==0 &&
              lease.context==context && context->references==2u &&
              refused.owns_ir==0 && refused.compiled.ready==0);
        CHECK(lease.context==context && context->references==2u);
        quantapdf_native_source_page_v1 owned{};
        CHECK(quantapdf_native_source_page_prepare_context_v1(
                  &ir,&caps,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
                  &lease,&owned)==QUANTAPDF_OK &&
              cleared(ir) && lease.context==nullptr && owned.owns_ir &&
              owned.domain.context==context && context->references==2u);
        quantapdf_native_execution_receipt_v1 receipt{};
        quantapdf_ir_raster_image rendered{};
        CHECK(quantapdf_native_source_page_execute_v1(
                  &owned,&receipt,&rendered)==QUANTAPDF_OK &&
              rendered.size==baseline.size &&
              receipt.charged_sample_work==normal.charged_sample_work &&
              receipt.source_pixels==4u && receipt.drawn_pixels==192u &&
              std::memcmp(rendered.pixels,baseline.pixels,baseline.size)==0);
        quantapdf_native_source_page_dispose_v1(&owned);
        CHECK(context->references==1u && cleared(ir) &&
              rendered.size==baseline.size &&
              std::memcmp(rendered.pixels,baseline.pixels,baseline.size)==0);
        quantapdf_ir_raster_image_dispose(&rendered);
        quantapdf_ir_raster_image_dispose(&baseline);
        quantapdf_drop_composer(composer_guard);
    }
    /* Even after a completely valid first paint, the single original
     * PDF reader must reject the ENTIRE page on a late unsupported
     * Do/Fill/W/Tr, broken matrix/qQ/ET, duplicate Do, extra resource
     * or SMask. No partial owned IR may escape, then a good original
     * must still prepare normally. */
    for(int mode=51;mode<=63;++mode) {
        const std::string original=native_ttf_fixture(mode);
        quantapdf_ir denied;
        std::memset(&denied,0xa5,sizeof(denied));
        const auto status=quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
            reinterpret_cast<const unsigned char*>(original.data()),
            original.size(),&limits,0u,&denied);
        if(status!=QUANTAPDF_ERROR_UNSUPPORTED &&
           status!=QUANTAPDF_ERROR_FORMAT)
            std::fprintf(stderr,"M2a negative original mode=%d status=%d\n",mode,(int)status);
        CHECK((status==QUANTAPDF_ERROR_UNSUPPORTED ||
               status==QUANTAPDF_ERROR_FORMAT) && cleared(denied));
    }
    {
        const std::string original=native_ttf_fixture(49);
        quantapdf_ir recovered{};
        CHECK(quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
            reinterpret_cast<const unsigned char*>(original.data()),
            original.size(),&limits,0u,&recovered)==QUANTAPDF_OK &&
            recovered.operation_count==2u &&
            recovered.resources.image_count==1u);
        quantapdf_ir_dispose(&recovered);
        CHECK(cleared(recovered));
    }
    std::puts("M2a: RAW/Flate both orders, 13 late-invalid refusals, N/N-1 quotas, all Source C-malloc faults, six C11 malloc faults and Context recovery");
    return EXIT_SUCCESS;
}

/* M2b.1: original black CID TrueType + green solid rectangle Fill +
 * decoded RAW RGB Image. Two original authorial orders share ONE owned
 * Source/Context and one C11 V5 page; never three independent renders.
 * All metadata/output are detached only after complete Source EOF.
 *
 * This is the existing second Source/OOM CTest invocation, under its
 * own unchanged 40-second sanitizer timeout; keep its injections
 * bounded to the two new three-paint originals. */
static int m2b1_ordered_source_ir_contract(
    const quantapdf_qpdf_source_raw_image_limits& limits)
{
    target=0u;armed=false;track_c_allocation=false;c_fail_index=0u;
    for(int mode:{66,67,81,82}) {
        const std::string original=native_ttf_fixture(mode);
        const auto read=[&](quantapdf_ir& out) {
            return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(original.data()),
                original.size(),&limits,0u,&out);
        };
        quantapdf_context *context=nullptr;
        quantapdf_composer *guard=nullptr;
        CHECK(quantapdf_context_create(&context)==QUANTAPDF_OK);
        CHECK(quantapdf_composer_create_with_context(context,nullptr,
              &guard)==QUANTAPDF_OK && guard!=nullptr &&
              context->references==2u);
        quantapdf_ir ir{};
        if(mode==66) {
            track_c_allocation=true;c_fail_index=0u;
            c_allocations=0u;c_faults=0u;
        }
        const auto status=read(ir);
        const size_t source_allocations=c_allocations;
        track_c_allocation=false;
        CHECK(status==QUANTAPDF_OK && ir.page_count==1u &&
              ir.operation_count==3u && ir.resources.image_count==1u &&
              ir.resources.images!=nullptr &&
              ir.resources.images[0].size==12u);
        const quantapdf_composer_operation_kind expected[3]={
            (mode==66 || mode==81)?
                QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES:
                     QUANTAPDF_COMPOSER_OPERATION_IMAGE,
            QUANTAPDF_COMPOSER_OPERATION_PATH,
            (mode==66 || mode==81)?
                QUANTAPDF_COMPOSER_OPERATION_IMAGE:
                     QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES
        };
        for(size_t i=0u;i<3u;++i)
            CHECK(ir.operations[i].kind==expected[i] &&
                  ir.operations[i].page_index==0u &&
                  ir.operations[i].graphics_state_id==0u);
        CHECK(ir.operations[1].value.path.command_count==5u &&
              ir.operations[1].value.path.commands!=nullptr &&
              ir.operations[1].value.path.options.fill==1 &&
              ir.operations[1].value.path.options.fill_argb==
                  UINT32_C(0xff00ff00));
        const auto& text=ir.operations[(mode==66 || mode==81)?0u:2u].value.text_outlines;
        CHECK(text.argb==UINT32_C(0xff000000) &&
              text.glyph_count==5u && text.glyphs!=nullptr);
        const quantapdf_ir snapshot=ir;
        quantapdf_native_source_raster_device_v1 device{};
        device.dpi=72.0f;
        const quantapdf_ir_raster_scoped_mixed_v5_limits caps={
            1000000u,4u,512u,8000000u,
            limits.max_source_pixels,limits.max_drawn_pixels
        };
        quantapdf_native_prepared_page_v1 metadata{};
        CHECK(quantapdf_native_prepare_page_v1(
              &ir,&caps,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
              &metadata)==QUANTAPDF_OK &&
              metadata.route==QP_NATIVE_PREPARED_M2B1_TEXT_FILL_IMAGE &&
              metadata.usage.operations==3u &&
              metadata.usage.expanded_paints==7u &&
              metadata.usage.glyphs==5u &&
              metadata.usage.image_pixels==4u &&
              metadata.usage.commands>=5u);
        m2a_malloc_fault_state counter{};
        metadata.m2a_malloc_fault={&counter,m2a_malloc_fault_callback};
        quantapdf_native_execution_receipt_v1 full_receipt{};
        quantapdf_ir_raster_image full{};
        CHECK(quantapdf_native_execute_page_v1(&metadata,&full_receipt,&full)==
                  QUANTAPDF_OK &&
              /* Generic C11 Fill reports its charged work but has
               * not been promoted to exact glyph-only classifier. */
              full_receipt.charged_sample_work>0u &&
              full_receipt.compiled_edges>0u &&
              full_receipt.compiled_edges<=caps.max_edges &&
              full_receipt.source_pixels==4u &&
              full_receipt.drawn_pixels==192u &&
              full_receipt.retained_bytes==metadata.usage.retained_bytes &&
              full_receipt.peak_live_bytes<=metadata.resource_limits.max_live_bytes &&
              full_receipt.output_bytes==128u*64u*4u &&
              full_receipt.completed_leases>=2u &&
              full.width==128u && full.height==64u &&
              full.size==full_receipt.output_bytes &&
              rgba_at(full,0u,0u)==UINT32_C(0xffffffff));
        CHECK(counter.attempts>=7u && counter.attempts<=16u);
        metadata.m2a_malloc_fault={};
        const auto zero_receipt=[](const quantapdf_native_execution_receipt_v1& r) {
            return r.sample_tests==0u && r.charged_sample_work==0u &&
                   r.compiled_edges==0u && r.completed_leases==0u &&
                   r.source_pixels==0u && r.drawn_pixels==0u &&
                   r.retained_bytes==0u && r.output_bytes==0u &&
                   r.peak_live_bytes==0u;
        };
        const auto retry=[&](
            quantapdf_ir_raster_scoped_mixed_v5_limits budget,
            size_t live_max,quantapdf_status desired,
            size_t failure_index=0u) {
            quantapdf_native_prepared_page_v1 prepared{};
            quantapdf_native_execution_receipt_v1 receipt{};
            quantapdf_ir_raster_image result{};
            m2a_malloc_fault_state fault{};
            quantapdf_status got=quantapdf_native_prepare_page_v1(
                &ir,&budget,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
                &prepared);
            if(got==QUANTAPDF_OK) {
                if(live_max!=0u)
                    prepared.resource_limits.max_live_bytes=live_max;
                if(failure_index!=0u) {
                    fault.fail_on=failure_index;
                    prepared.m2a_malloc_fault={&fault,m2a_malloc_fault_callback};
                }
                got=quantapdf_native_execute_page_v1(&prepared,&receipt,&result);
            }
            bool ok=got==desired &&
                std::memcmp(&snapshot,&ir,sizeof(ir))==0 &&
                context->references==2u;
            if(desired==QUANTAPDF_OK)
                ok=ok && result.size==full.size && result.pixels!=nullptr &&
                    std::memcmp(result.pixels,full.pixels,full.size)==0 &&
                    receipt.charged_sample_work==full_receipt.charged_sample_work &&
                    receipt.source_pixels==4u && receipt.drawn_pixels==192u;
            else
                ok=ok && zero_bitmap(result) && zero_receipt(receipt);
            if(failure_index!=0u)
                ok=ok && fault.attempts==failure_index &&
                    fault.failures==1u;
            if(!ok)std::fprintf(stderr,
                "M2b1 mode=%d budget s=%zu e=%zu p=%zu o=%zu"
                " mem=%zu fault=%zu desired=%d got=%d attempts=%zu\n",
                mode,budget.max_sample_tests,budget.max_edges,
                budget.max_pixels,budget.max_operations,
                live_max,failure_index,(int)desired,(int)got,fault.attempts);
            quantapdf_ir_raster_image_dispose(&result);
            return ok;
        };
        if(mode==66) {
            auto budget=caps;
            budget.max_pixels=full.width*full.height;
            CHECK(retry(budget,0u,QUANTAPDF_OK));
            budget.max_pixels--;CHECK(retry(budget,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            budget=caps;budget.max_source_pixels=4u;
            CHECK(retry(budget,0u,QUANTAPDF_OK));
            budget.max_source_pixels--;CHECK(retry(budget,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            budget=caps;budget.max_drawn_pixels=192u;
            CHECK(retry(budget,0u,QUANTAPDF_OK));
            budget.max_drawn_pixels--;CHECK(retry(budget,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            budget=caps;budget.max_operations=3u;
            CHECK(retry(budget,0u,QUANTAPDF_OK));
            budget.max_operations--;CHECK(retry(budget,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            CHECK(retry(caps,full_receipt.peak_live_bytes,QUANTAPDF_OK));
            CHECK(full_receipt.peak_live_bytes>metadata.usage.retained_bytes);
            CHECK(retry(caps,full_receipt.peak_live_bytes-1u,
                        QUANTAPDF_ERROR_UNSUPPORTED));
            /* Discover the exact smallest admissible global resource
             * budget, not a per-Text or per-Fill local reservation. */
            for(unsigned axis=0u;axis<2u;++axis) {
                size_t low=1u,high=axis?caps.max_edges:caps.max_sample_tests;
                while(low<high) {
                    const size_t mid=low+(high-low)/2u;
                    auto limit=caps;
                    if(axis)limit.max_edges=mid;
                    else limit.max_sample_tests=mid;
                    quantapdf_native_prepared_page_v1 prepared{};
                    quantapdf_ir_raster_image result{};
                    quantapdf_native_execution_receipt_v1 receipt{};
                    auto accepted=quantapdf_native_prepare_page_v1(
                        &ir,&limit,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
                        &prepared);
                    if(accepted==QUANTAPDF_OK)
                        accepted=quantapdf_native_execute_page_v1(
                            &prepared,&receipt,&result);
                    quantapdf_ir_raster_image_dispose(&result);
                    CHECK(accepted==QUANTAPDF_OK ||
                          accepted==QUANTAPDF_ERROR_UNSUPPORTED);
                    if(accepted==QUANTAPDF_OK)high=mid;
                    else low=mid+1u;
                }
                auto limit=caps;
                if(axis)limit.max_edges=low;
                else limit.max_sample_tests=low;
                CHECK(retry(limit,0u,QUANTAPDF_OK));
                CHECK(low>1u);
                if(axis)limit.max_edges=low-1u;
                else limit.max_sample_tests=low-1u;
                CHECK(retry(limit,0u,QUANTAPDF_ERROR_UNSUPPORTED));
            }
            /* Observe and fault-inject EVERY native malloc actually
             * reached by the same Source/compiled three-layer page. */
            for(size_t fail=1u;fail<=counter.attempts;++fail) {
                CHECK(retry(caps,0u,QUANTAPDF_ERROR_NOMEM,fail));
                CHECK(retry(caps,0u,QUANTAPDF_OK));
            }
            /* Exhaustive owned Source C allocation failure matrix,
             * full atomic rollback; recover original at boundaries. */
            CHECK(source_allocations>=5u && source_allocations<=40u);
            for(size_t fail=1u;fail<=source_allocations;++fail) {
                quantapdf_ir denied{};
                track_c_allocation=true;c_fail_index=fail;
                c_allocations=0u;c_faults=0u;
                const auto bad=read(denied);
                track_c_allocation=false;c_fail_index=0u;
                CHECK(bad==QUANTAPDF_ERROR_NOMEM &&
                      c_allocations==fail && c_faults==1u &&
                      cleared(denied) &&
                      std::memcmp(&snapshot,&ir,sizeof(ir))==0);
                if(fail==1u || fail==source_allocations/2u ||
                   fail==source_allocations) {
                    quantapdf_ir recovery{};
                    CHECK(read(recovery)==QUANTAPDF_OK &&
                          recovery.operation_count==3u &&
                          recovery.resources.image_count==1u);
                    quantapdf_ir_dispose(&recovery);
                    CHECK(cleared(recovery));
                }
            }
        }
        /* Same-context refusal cannot transfer the caller's owned IR
         * or Context reference. Success moves the unique lease and
         * detaches its bitmap from Source/Page/Context disposal. */
        quantapdf_context_lease_internal lease{context,quantapdf_drop_context};
        auto too_small=caps;too_small.max_source_pixels=3u;
        quantapdf_native_source_page_v1 refused{};
        CHECK(quantapdf_native_source_page_prepare_context_v1(
                &ir,&too_small,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
                &lease,&refused)==QUANTAPDF_ERROR_UNSUPPORTED &&
              std::memcmp(&snapshot,&ir,sizeof(ir))==0 &&
              lease.context==context && context->references==2u &&
              refused.owns_ir==0);
        quantapdf_native_source_page_v1 prepared{};
        CHECK(quantapdf_native_source_page_prepare_context_v1(
                &ir,&caps,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,
                &lease,&prepared)==QUANTAPDF_OK &&
              prepared.owns_ir && cleared(ir) &&
              lease.context==nullptr && context->references==2u);
        quantapdf_native_execution_receipt_v1 receipt{};
        quantapdf_ir_raster_image rendered{};
        CHECK(quantapdf_native_source_page_execute_v1(
                &prepared,&receipt,&rendered)==QUANTAPDF_OK &&
              receipt.source_pixels==4u && receipt.drawn_pixels==192u &&
              rendered.size==full.size &&
              std::memcmp(rendered.pixels,full.pixels,full.size)==0);
        quantapdf_native_source_page_dispose_v1(&prepared);
        CHECK(context->references==1u && cleared(ir) &&
              rendered.size==full.size &&
              std::memcmp(rendered.pixels,full.pixels,full.size)==0);
        quantapdf_drop_composer(guard);
        quantapdf_ir_raster_image_dispose(&rendered);
        quantapdf_ir_raster_image_dispose(&full);
    }
    /* Every original follows a valid three-paint prefix, then violates
     * exactly one Source grammar, resource or state contract. No
     * execution may publish a partial page from the good prefix. */
    for(int mode=68;mode<=80;++mode) {
        const std::string original=native_ttf_fixture(mode);
        quantapdf_ir denied;
        std::memset(&denied,0xa5,sizeof(denied));
        const auto st=quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
            reinterpret_cast<const unsigned char*>(original.data()),
            original.size(),&limits,0u,&denied);
        if(st!=QUANTAPDF_ERROR_UNSUPPORTED &&
           st!=QUANTAPDF_ERROR_FORMAT)
            std::fprintf(stderr,"M2b1 original late refusal mode=%d got=%d\n",
                         mode,(int)st);
        CHECK((st==QUANTAPDF_ERROR_UNSUPPORTED ||
               st==QUANTAPDF_ERROR_FORMAT) && cleared(denied));
    }
    std::puts("M2b.1: ordered three-paint originals, exact N/N-1,"
              " Source+C11 actual malloc faults and 13 atomic negatives,"
              " same-Context recovery");
    return EXIT_SUCCESS;
}

static int native_ttf_source_and_raster_contracts(
    const quantapdf_qpdf_source_raw_image_limits& source_limits,int mode=1)
{
    const auto original=native_ttf_fixture(mode);
    const auto read=[&](quantapdf_ir& ir) {
        return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
            reinterpret_cast<const unsigned char*>(original.data()),original.size(),&source_limits,0u,&ir);
    };
    target=8u;armed=false;observed=0u;faults=0u;
    quantapdf_ir denied;std::memset(&denied,0xa5,sizeof(denied));
    const auto staged=read(denied);
    const bool fired=observed==8u && faults==1u && !armed;
    target=0u;armed=false;CHECK(staged==QUANTAPDF_ERROR_NOMEM && fired && cleared(denied));
    quantapdf_ir ir{};track_c_allocation=true;c_fail_index=0u;c_allocations=0u;c_faults=0u;
    const auto accepted=read(ir);const size_t source_allocations=c_allocations;track_c_allocation=false;
    CHECK(accepted==QUANTAPDF_OK && source_allocations==(mode==1?9u:12u) && ir.operation_count==(mode==1?3u:4u));
    if(mode==1) {
    for(size_t i=0u;i<3u;++i)CHECK(ir.operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES);
    CHECK(ir.operations[0].value.text_outlines.glyph_count==2u &&
          ir.operations[1].value.text_outlines.glyph_count==1u &&
          ir.operations[2].value.text_outlines.glyph_count==1u);
    } else {
        CHECK(ir.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_PATH && ir.operations[2].kind==QUANTAPDF_COMPOSER_OPERATION_PATH);
        CHECK(ir.operations[1].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES && ir.operations[1].value.text_outlines.glyph_count==5u);
        CHECK(ir.operations[3].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES && ir.operations[3].value.text_outlines.glyph_count==1u);
    }
    const auto snapshot=text_ir_snapshot(ir);
    const auto goldens=std::filesystem::path(__FILE__).parent_path()/"goldens";
    quantapdf_ir_raster_image held{};std::vector<unsigned char> held_bytes;
    for(unsigned dpi:{72u,96u,144u}) {
        quantapdf_native_device_plan plan{};
        CHECK(quantapdf_native_device_plan_create_physical("0 0 128 64","0 0 128 64","1",0,dpi,1000000u,&plan)==QUANTAPDF_OK);
        quantapdf_ir_raster_v1_limits limits={1000000u,4u,512u,8000000u};
        size_t memory_cap=0u;
        const auto prepare=[&](const quantapdf_ir_raster_v1_limits& budget,
                               quantapdf_native_prepared_page_v1& out) {
            const quantapdf_ir_raster_scoped_mixed_v5_limits caps={budget.max_pixels,
                budget.max_operations,budget.max_edges,budget.max_sample_tests,
                source_limits.max_source_pixels,source_limits.max_drawn_pixels};
            quantapdf_native_source_raster_device_v1 device{};device.dpi=(float)dpi;
            const auto status=quantapdf_native_prepare_page_v1(&ir,&caps,&device,
                QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&out);
            if(status==QUANTAPDF_OK && memory_cap!=0u)out.resource_limits.max_live_bytes=memory_cap;
            return status;
        };
        quantapdf_native_prepared_page_v1 prepared{};
        track_c_allocation=true;c_fail_index=0u;c_allocations=0u;armed=true;
        const auto preparation=prepare(limits,prepared);
        const bool no_new=armed;armed=false;track_c_allocation=false;
        CHECK(preparation==QUANTAPDF_OK && no_new && c_allocations==0u &&
              prepared.source==&ir && prepared.route==QP_NATIVE_PREPARED_TEXT &&
              prepared.usage.operations==ir.operation_count &&
              prepared.usage.glyphs==(mode==1?4u:6u) &&
              prepared.usage.expanded_paints==(mode==1?4u:8u));
        size_t work=0u;quantapdf_ir_raster_image full{};
        quantapdf_native_execution_receipt_v1 receipt{};
        track_c_allocation=true;c_fail_index=0u;c_allocations=0u;c_faults=0u;
        const auto status=render_adopted_page(ir,prepared.limits,dpi,&receipt,&full);
        work=receipt.sample_tests;
        track_c_allocation=false;
        CHECK(status==QUANTAPDF_OK && receipt.sample_tests_exact && c_allocations==4u && work>0u && work<8000000u);
        CHECK(receipt.charged_sample_work==work && receipt.compiled_edges>0u &&
              receipt.compiled_edges<=limits.max_edges && receipt.completed_leases==1u &&
              receipt.peak_reserved_sample_work==limits.max_sample_tests &&
              receipt.peak_reserved_edges==limits.max_edges && receipt.source_pixels==0u &&
              receipt.drawn_pixels==0u && receipt.retained_bytes==prepared.usage.retained_bytes &&
              receipt.output_bytes==full.size && receipt.peak_live_bytes>=receipt.retained_bytes+full.size &&
              receipt.peak_live_bytes<=prepared.resource_limits.max_live_bytes);
        memory_cap=receipt.peak_live_bytes;
        std::ifstream file(goldens/(std::string(mode==1?"roboto-layers-":"roboto-mixed-")+std::to_string(dpi)+(mode==1?"-m0.rgba":"-m1.rgba")),std::ios::binary);
        const std::vector<unsigned char> expected{std::istreambuf_iterator<char>(file),{}};
        CHECK(expected.size()==full.size && std::memcmp(expected.data(),full.pixels,full.size)==0);
        bool detach_once=true;
        const auto retry=[&](const quantapdf_ir_raster_v1_limits& budget,quantapdf_status want,size_t fail_index=0u) {
            quantapdf_ir_raster_image result;std::memset(&result,0xa5,sizeof(result));size_t seen=999u;
            track_c_allocation=fail_index!=0u;c_fail_index=fail_index;c_allocations=0u;c_faults=0u;
            quantapdf_status got;
            if(std::memcmp(&plan,&prepared.physical,sizeof(plan))!=0)
                got=quantapdf_ir_raster_render_v1_text_outlines(&ir,&budget,&plan,&seen,&result);
            else {
                quantapdf_native_prepared_page_v1 retry_plan{};
                quantapdf_native_execution_receipt_v1 retry_receipt{};
                got=prepare(budget,retry_plan);
                const bool detached=got==QUANTAPDF_OK && detach_once && want==QUANTAPDF_OK &&
                    fail_index==0u && budget.max_sample_tests==work;
                if(detached) {
                    detach_once=false;got=render_detached_geometry(ir,retry_plan,&seen,&result);
                } else if(got==QUANTAPDF_OK)got=fail_index!=0u?
                    render_adopted_page(ir,retry_plan.limits,dpi,&retry_receipt,&result):
                    quantapdf_native_execute_page_v1(&retry_plan,&retry_receipt,&result);
                else std::memset(&result,0,sizeof(result));
                if(!detached)seen=retry_receipt.sample_tests;
                if(!detached && got==QUANTAPDF_OK && (!retry_receipt.sample_tests_exact ||
                   retry_receipt.peak_live_bytes>memory_cap || retry_receipt.output_bytes!=result.size ||
                   (budget.max_edges==limits.max_edges && retry_receipt.peak_live_bytes!=memory_cap)))
                    got=QUANTAPDF_ERROR_BACKEND;
                if(got!=QUANTAPDF_OK && (retry_receipt.charged_sample_work!=0u ||
                   retry_receipt.compiled_edges!=0u || retry_receipt.completed_leases!=0u ||
                   retry_receipt.peak_reserved_sample_work!=0u ||
                   retry_receipt.peak_reserved_edges!=0u || retry_receipt.source_pixels!=0u ||
                   retry_receipt.drawn_pixels!=0u || retry_receipt.retained_bytes!=0u ||
                   retry_receipt.output_bytes!=0u || retry_receipt.peak_live_bytes!=0u))got=QUANTAPDF_ERROR_BACKEND;
            }
            track_c_allocation=false;c_fail_index=0u;
            bool ok=got==want && text_ir_snapshot(ir)==snapshot;
            if(want==QUANTAPDF_OK)ok=ok && seen==work && result.size==full.size &&
                std::memcmp(result.pixels,full.pixels,full.size)==0;
            else ok=ok && zero_bitmap(result) && seen==0u;
            if(fail_index)ok=ok && c_allocations==fail_index && c_faults==1u;
            quantapdf_ir_raster_image_dispose(&result);return ok;
        };
        const auto original_plan=plan;
        plan.width_points+=0.000001;CHECK(retry(limits,QUANTAPDF_ERROR_UNSUPPORTED));plan=original_plan;
        plan.height_points+=0.000001;
        plan.source_to_device[5]=plan.height_points*((double)dpi/72.0);
        CHECK(retry(limits,QUANTAPDF_ERROR_UNSUPPORTED));plan=original_plan;
        auto exact=limits;exact.max_sample_tests=work;CHECK(retry(exact,QUANTAPDF_OK));
        exact.max_sample_tests=work-1u;CHECK(retry(exact,QUANTAPDF_ERROR_UNSUPPORTED));
        size_t lo=1u,hi=512u;
        while(lo<hi) {
            const size_t mid=lo+(hi-lo)/2u;auto budget=limits;budget.max_edges=mid;
            quantapdf_ir_raster_image probe{};
            const auto st=quantapdf_ir_raster_render_v1_text_outlines(&ir,&budget,&plan,nullptr,&probe);
            quantapdf_ir_raster_image_dispose(&probe);
            CHECK(st==QUANTAPDF_OK || st==QUANTAPDF_ERROR_UNSUPPORTED);
            if(st==QUANTAPDF_OK)hi=mid;else lo=mid+1u;
        }
        exact=limits;exact.max_edges=lo;CHECK(retry(exact,QUANTAPDF_OK));
        CHECK(lo>1u);exact.max_edges=lo-1u;CHECK(retry(exact,QUANTAPDF_ERROR_UNSUPPORTED));
        exact=limits;exact.max_pixels=full.width*full.height-1u;CHECK(retry(exact,QUANTAPDF_ERROR_UNSUPPORTED));
        exact=limits;exact.max_operations=2u;CHECK(retry(exact,QUANTAPDF_ERROR_UNSUPPORTED));
        for(size_t fail=1u;fail<=4u;++fail) {
            CHECK(retry(limits,QUANTAPDF_ERROR_NOMEM,fail));CHECK(retry(limits,QUANTAPDF_OK));
        }
        std::printf("TTF layers physical %u: edges=%zu work=%zu all RGBA, N/N-1, four OOMs/recovery\n",dpi,lo,work);
        if(dpi==96u) {held=full;held_bytes.assign(full.pixels,full.pixels+full.size);}
        else quantapdf_ir_raster_image_dispose(&full);
    }
    CHECK(text_ir_snapshot(ir)==snapshot);quantapdf_ir_dispose(&ir);CHECK(cleared(ir));
    CHECK(held.pixels!=nullptr && held.size==58824u &&
          held.size==held_bytes.size() && std::memcmp(held.pixels,held_bytes.data(),held.size)==0);quantapdf_ir_raster_image_dispose(&held);
    for(size_t fail=1u;fail<=source_allocations;++fail) {
        std::memset(&denied,0xa5,sizeof(denied));
        track_c_allocation=true;c_fail_index=fail;c_allocations=0u;c_faults=0u;
        const auto status=read(denied);track_c_allocation=false;c_fail_index=0u;
        CHECK(status==QUANTAPDF_ERROR_NOMEM && cleared(denied) && c_allocations==fail && c_faults==1u);
        CHECK(read(ir)==QUANTAPDF_OK && text_ir_snapshot(ir).size()==snapshot.size());
        const quantapdf_ir_raster_v1_limits recovery_limits={1000000u,4u,512u,8000000u};
        quantapdf_ir_raster_image recovery{};quantapdf_native_device_plan plan{};
        CHECK(quantapdf_native_device_plan_create_physical("0 0 128 64","0 0 128 64","1",0,96u,1000000u,&plan)==QUANTAPDF_OK);
        CHECK(quantapdf_ir_raster_render_v1_text_outlines(&ir,&recovery_limits,&plan,nullptr,&recovery)==QUANTAPDF_OK);
        CHECK(recovery.size==held_bytes.size() && std::memcmp(recovery.pixels,held_bytes.data(),recovery.size)==0);
        quantapdf_ir_raster_image_dispose(&recovery);quantapdf_ir_dispose(&ir);
    }
    return EXIT_SUCCESS;
}

/* M0: exercise the actual shared SFNT subset writer, independently of PDF
 * serialization. These are format/mapping/outline and real allocation gates. */
static int native_ttf_subset_contracts()
{
    using namespace quantapdf::detail;
    const auto filename=std::filesystem::path(__FILE__).parent_path().parent_path()/
        "fixtures"/"fonts"/"Roboto-Regular.ttf";
    std::ifstream file(filename,std::ios::binary);
    const std::vector<unsigned char> original{std::istreambuf_iterator<char>(file),{}};
    ttf_font_face face;
    CHECK(ttf_font_face::parse(original.data(),original.size(),&face)==QUANTAPDF_OK);
    const auto inspect=[](const std::vector<unsigned char>& bytes,bool has_cmap) {
        if(bytes.size()<12u || bytes.size()%4u!=0u)return false;
        const auto u16=[&](size_t at) {return (unsigned)bytes.at(at)*256u+bytes.at(at+1u);};
        const auto u32=[&](size_t at) {return (uint32_t)u16(at)*65536u+u16(at+2u);};
        const size_t count=u16(4u),directory=12u+16u*count;
        if(count==0u || directory>bytes.size())return false;
        size_t power=1u;unsigned selector=0u;
        while(power*2u<=count) {power*=2u;++selector;}
        if(u16(6u)!=power*16u || u16(8u)!=selector ||
           u16(10u)!=count*16u-power*16u)return false;
        std::map<std::string,std::pair<size_t,size_t>> tables;
        size_t end=directory;
        for(size_t i=0u;i<count;++i) {
            const size_t at=12u+16u*i;
            const std::string tag(reinterpret_cast<const char*>(bytes.data()+at),4u);
            const size_t offset=u32(at+8u),length=u32(at+12u);
            if(offset%4u!=0u || offset<end || offset>bytes.size() ||
               length>bytes.size()-offset || !tables.emplace(tag,std::make_pair(offset,length)).second)
                return false;
            uint32_t sum=0u;
            for(size_t p=0u;p<length;p+=4u) {
                uint32_t word=0u;
                for(size_t j=0u;j<4u;++j) {
                    const size_t k=p+j;
                    word=(word<<8u)|(k<length && !(tag=="head" && k>=8u && k<12u)?
                        bytes[offset+k]:0u);
                }
                sum+=word;
            }
            if(sum!=u32(at+4u))return false;
            end=offset+length;
        }
        for(const char *tag:{"head","hhea","hmtx","loca","maxp","glyf"})
            if(!tables.count(tag))return false;
        if((tables.count("cmap")!=0u)!=has_cmap || tables.count("DSIG"))return false;
        uint32_t sum=0u;
        for(size_t at=0u;at<bytes.size();at+=4u)sum+=u32(at);
        return sum==UINT32_C(0xb1b0afba);
    };
    std::map<uint16_t,uint32_t> used;
    for(uint32_t code:{72u,101u,233u}) {
        const auto gid=face.glyph_for(code);CHECK(gid!=0u);used.emplace(gid,code);
    }
    std::vector<unsigned char> unicode,explicit_unicode,cid;
    CHECK(subset_true_type_font(face,used,&unicode)==QUANTAPDF_OK);
    CHECK(subset_true_type_font(face,used,&explicit_unicode,ttf_subset_purpose::unicode)==QUANTAPDF_OK);
    CHECK(unicode==explicit_unicode && inspect(unicode,true));
    CHECK(subset_true_type_font(face,used,&cid,ttf_subset_purpose::cid_font_file2)==QUANTAPDF_OK);
    CHECK(inspect(cid,false));
    ttf_font_face unicode_face,cid_face,rejected;
    CHECK(ttf_font_face::parse(unicode.data(),unicode.size(),&unicode_face)==QUANTAPDF_OK);
    CHECK(ttf_font_face::parse_cid(cid.data(),cid.size(),&cid_face)==QUANTAPDF_OK);
    CHECK(ttf_font_face::parse(cid.data(),cid.size(),&rejected)!=QUANTAPDF_OK);
    used.emplace(0u,0u); /* .notdef and an accented composite retain closure. */
    for(const auto& entry:used) {
        const auto gid=entry.first;
        if(gid!=0u)CHECK(unicode_face.glyph_for(entry.second)==gid);
        CHECK(cid_face.width_for(gid)==face.width_for(gid));
        std::vector<ttf_outline_contour> a,b;
        CHECK(face.outline_for(gid,&a)==QUANTAPDF_OK);
        CHECK(cid_face.outline_for(gid,&b)==QUANTAPDF_OK);
        CHECK(a.size()==b.size());
        for(size_t i=0u;i<a.size();++i) {
            CHECK(a[i].size()==b[i].size());
            for(size_t j=0u;j<a[i].size();++j)
                CHECK(a[i][j].x==b[i][j].x && a[i][j].y==b[i][j].y &&
                      a[i][j].on_curve==b[i][j].on_curve);
        }
    }
    cid.assign(1u,42u);
    CHECK(subset_true_type_font(face,used,&cid,static_cast<ttf_subset_purpose>(255))==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(cid.empty());
    const std::map<uint16_t,uint32_t> bad{{face.num_glyphs,72u}};
    cid.assign(1u,42u);
    CHECK(subset_true_type_font(face,bad,&cid,ttf_subset_purpose::cid_font_file2)==QUANTAPDF_ERROR_FORMAT);
    CHECK(cid.empty());
    cid.assign(1u,42u);faults=0u;armed=true;
    const auto status=subset_true_type_font(face,used,&cid,ttf_subset_purpose::cid_font_file2);
    CHECK(status==QUANTAPDF_ERROR_NOMEM && cid.empty() && faults==1u && !armed);
    CHECK(subset_true_type_font(face,used,&cid,ttf_subset_purpose::cid_font_file2)==QUANTAPDF_OK);
    CHECK(inspect(cid,false));
    std::puts("CID FontFile2: cmap-free checksums, Unicode mapping, composite closure and OOM recovery passed");
    return EXIT_SUCCESS;
}
/* End M0 SFNT contracts. */

struct prepared_context_probe {
    quantapdf_native_source_page_v1 *page{};
    quantapdf_context *context{};
    size_t drops{};
    bool ordered{};
};
static prepared_context_probe prepared_domains[2];
static void drop_prepared_context(quantapdf_context *context)
{
    for(auto& probe:prepared_domains)if(probe.context==context) {
        const quantapdf_native_source_page_v1 zero{};
        probe.ordered=context->references==1u && probe.page!=nullptr &&
            std::memcmp(probe.page,&zero,sizeof(zero))==0;
        ++probe.drops;
    }
    quantapdf_drop_context(context);
}

/* Real heap IR adoption, including RAW data lifetime and publication. */
static int owned_page_contracts()
{
    quantapdf_composer_page_state fixture_page{};
    quantapdf_composer_operation fixture_op{};
    quantapdf_composer_path_command fixture_commands[5]{};
    quantapdf_ir fixture_ir{};const auto fixture=&fixture_ir;
    fixture_page.width_points=fixture_page.height_points=8.0f;
    fixture_page.background_argb=UINT32_C(0xffffffff);
    fixture_ir.pages=&fixture_page;fixture_ir.page_count=1u;
    fixture_ir.operations=&fixture_op;fixture_ir.operation_count=1u;
    fixture_op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    fixture_op.value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    fixture_op.value.path.options.fill=1;fixture_op.value.path.options.fill_argb=UINT32_C(0xff0000ff);
    fixture_op.value.path.commands=fixture_commands;fixture_op.value.path.command_count=5u;
    fixture_commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    fixture_commands[1].kind=fixture_commands[2].kind=fixture_commands[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    fixture_commands[1].point1.x=fixture_commands[2].point1.x=4.0f;
    fixture_commands[2].point1.y=fixture_commands[3].point1.y=4.0f;
    fixture_commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    for(unsigned mode=0u;mode<4u;++mode) {
        quantapdf_ir ir{},before;
        quantapdf_native_source_page_v1 page{},zero{},other_page{};
        quantapdf_context *context=nullptr;
        CHECK(quantapdf_context_create(&context)==QUANTAPDF_OK);
        quantapdf_context_lease_internal domain{context,drop_prepared_context};
        prepared_domains[0]={&page,context,0u,false};prepared_domains[1]={};
        quantapdf_native_source_raster_device_v1 device{};
        quantapdf_ir_raster_scoped_mixed_v5_limits caps={512u,4u,512u,8000000u,1u,128u};
        quantapdf_native_execution_receipt_v1 receipt;
        quantapdf_ir_raster_image first{},second{},refused{};
        const unsigned dpi=mode==3u?96u:72u;
        ir.pages=(quantapdf_composer_page_state *)calloc(1u,sizeof(*ir.pages));
        CHECK(ir.pages!=NULL);*ir.pages=fixture->pages[0];ir.page_count=1u;
        ir.operations=(quantapdf_composer_operation *)calloc(1u,sizeof(*ir.operations));
        CHECK(ir.operations!=NULL);ir.operation_count=1u;
        if(mode==0u) {
            *ir.operations=fixture->operations[0];
            ir.operations[0].value.path.commands=(quantapdf_composer_path_command *)malloc(
                5u*sizeof(quantapdf_composer_path_command));
            CHECK(ir.operations[0].value.path.commands!=NULL);
            memcpy(ir.operations[0].value.path.commands,fixture->operations[0].value.path.commands,
                   5u*sizeof(quantapdf_composer_path_command));
        } else {
            quantapdf_composer_operation *op=ir.operations;
            ir.resources.images=(quantapdf_composer_image_state *)calloc(1u,sizeof(*ir.resources.images));
            CHECK(ir.resources.images!=NULL);
            ir.resources.image_count=ir.resources.image_capacity=1u;
            quantapdf_composer_image_state *image=ir.resources.images;
            image->format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;image->width=image->height=1;
            image->components=3;image->size=3u;image->data=(unsigned char *)malloc(3u);
            CHECK(image->data!=NULL);image->data[0]=255u;image->data[1]=image->data[2]=0u;
            op->kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;op->bounds.x1=op->bounds.y1=8.0f;
            op->value.image.image_id=1u;
            op->value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
            op->value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
            if(mode>=2u) {
                ir.resources.clips=(quantapdf_composer_clip_state *)calloc(1u,sizeof(*ir.resources.clips));
                ir.resources.graphics_states=(quantapdf_composer_graphics_state *)calloc(1u,sizeof(*ir.resources.graphics_states));
                CHECK(ir.resources.clips!=NULL && ir.resources.graphics_states!=NULL);
                ir.resources.clip_count=ir.resources.clip_capacity=1u;
                ir.resources.graphics_state_count=ir.resources.graphics_state_capacity=1u;
                quantapdf_composer_clip_state *clip=ir.resources.clips;
                clip->kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;clip->command_count=5u;
                clip->commands=(quantapdf_composer_path_command *)malloc(5u*sizeof(*clip->commands));
                CHECK(clip->commands!=NULL);
                memcpy(clip->commands,fixture->operations[0].value.path.commands,5u*sizeof(*clip->commands));
                clip->fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
                clip->transform.a=clip->transform.d=1.0f;
                ir.resources.graphics_states[0].clip_id=1u;
                ir.resources.graphics_states[0].fill_alpha=ir.resources.graphics_states[0].stroke_alpha=1.0f;
                ir.resources.graphics_states[0].blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
                op->graphics_state_id=op->value.image.options.graphics_state_id=1u;
            }
        }
        before=ir;device.dpi=73.0f;
        CHECK(quantapdf_native_source_page_prepare_context_v1(&ir,&caps,&device,
            QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&domain,&page)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(memcmp(&ir,&before,sizeof(ir))==0 && memcmp(&page,&zero,sizeof(page))==0);
        CHECK(domain.context==context && context->references==1u && prepared_domains[0].drops==0u);
        device.dpi=(float)dpi;
        if(mode!=1u) {
            const size_t cap=caps.max_sample_tests;caps.max_sample_tests=1u;
            CHECK(quantapdf_native_source_page_prepare_context_v1(&ir,&caps,&device,
                QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&domain,&page)==QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(memcmp(&ir,&before,sizeof(ir))==0 && memcmp(&page,&zero,sizeof(page))==0);
            caps.max_sample_tests=cap;
        }
        CHECK((quantapdf_native_source_page_prepare_context_v1(&ir,&caps,&device,
            QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&domain,&page))==QUANTAPDF_OK);
        CHECK(ir.pages==NULL && ir.operations==NULL && ir.resources.images==NULL && ir.operation_count==0u);
        CHECK(domain.context==nullptr && domain.drop==nullptr && page.domain.context==context && context->references==1u);
        if(mode==1u) {
            quantapdf_context *other=nullptr;
            CHECK(quantapdf_context_create(&other)==QUANTAPDF_OK);
            prepared_domains[1]={&other_page,other,0u,false};
            quantapdf_context_lease_internal other_domain{other,drop_prepared_context};
            quantapdf_ir blank{};blank.pages=(quantapdf_composer_page_state *)calloc(1u,sizeof(*blank.pages));
            CHECK(blank.pages!=nullptr);*blank.pages=fixture_page;blank.page_count=1u;
            CHECK(quantapdf_native_source_page_prepare_context_v1(&blank,&caps,&device,
                QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&other_domain,&other_page)==QUANTAPDF_OK);
            CHECK(other_domain.context==nullptr && blank.pages==nullptr);
        }
        CHECK(page.owns_ir && page.ir.pages==before.pages && page.compiled.metadata.source==NULL);
        CHECK(page.compiled.ledger.completed_leases==0u && page.compiled.ledger.used.sample_work==0u);
        const size_t retained=page.compiled.metadata.usage.retained_bytes+qp_compiled_retained_bytes(&page.compiled);
        CHECK(page.compiled.ledger.resources.live_bytes==retained);
        quantapdf_ir_dispose(&ir);
        memset(&caps,0xa5,sizeof(caps));memset(&device,0xa5,sizeof(device));
        CHECK((quantapdf_native_source_page_execute_v1(&page,&receipt,&first))==QUANTAPDF_OK);
        CHECK(first.pixels!=NULL && rgba_at(first,1u,1u)==(mode==0u?UINT32_C(0x0000ffff):UINT32_C(0xff0000ff)));
        CHECK(page.compiled.ledger.resources.live_bytes==retained);
        const size_t edges=receipt.compiled_edges,work=receipt.charged_sample_work;
        if(mode==1u) {
            CHECK((quantapdf_native_source_page_execute_v1(&page,&receipt,&second))==QUANTAPDF_OK);
            CHECK(receipt.source_pixels==1u && receipt.drawn_pixels==128u &&
                  receipt.compiled_edges==edges && receipt.charged_sample_work==work &&
                  first.pixels!=second.pixels && memcmp(first.pixels,second.pixels,first.size)==0);
        }
        memset(&receipt,0xa5,sizeof(receipt));memset(&refused,0xa5,sizeof(refused));
        CHECK(quantapdf_native_source_page_execute_v1(&page,&receipt,&refused)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(refused.pixels==NULL && refused.size==0u && receipt.output_bytes==0u &&
              receipt.charged_sample_work==0u && page.compiled.ledger.failed);
        quantapdf_native_source_page_dispose_v1(&page);
        CHECK(memcmp(&page,&zero,sizeof(page))==0 && first.pixels!=NULL);
        CHECK(prepared_domains[0].drops==1u && prepared_domains[0].ordered);
        if(mode==1u) {
            quantapdf_ir_raster_image other{};
            CHECK(prepared_domains[1].drops==0u && other_page.domain.context->references==1u);
            CHECK(quantapdf_native_source_page_execute_v1(&other_page,nullptr,&other)==QUANTAPDF_OK);
            CHECK(rgba_at(other,1u,1u)==UINT32_C(0xffffffff));
            quantapdf_native_source_page_dispose_v1(&other_page);
            CHECK(prepared_domains[1].drops==1u && prepared_domains[1].ordered);
            quantapdf_ir_raster_image_dispose(&other);
        }
        CHECK(rgba_at(first,1u,1u)==(mode==0u?UINT32_C(0x0000ffff):UINT32_C(0xff0000ff)));
        if(second.pixels!=NULL) {
            first.pixels[0]^=255u;CHECK(rgba_at(second,1u,1u)==UINT32_C(0xff0000ff));
        }
        quantapdf_ir_raster_image_dispose(&first);quantapdf_ir_raster_image_dispose(&second);
        quantapdf_native_source_page_dispose_v1(&page);
    }
    puts("Owned Source page: atomic IR adoption, frozen projection/masks, cumulative placement and detached publication passed");
    return 0;
}

static int composer_preparation_contracts()
{
    target=0u;armed=false;track_c_allocation=false;c_fail_index=0u;
    quantapdf_context *context=nullptr,*wrong_context=nullptr;
    CHECK(quantapdf_context_create(&context)==QUANTAPDF_OK);
    CHECK(quantapdf_context_create(&wrong_context)==QUANTAPDF_OK);
    quantapdf_context_lease_internal domain{context,quantapdf_drop_context};
    quantapdf_context_lease_internal wrong{wrong_context,quantapdf_drop_context};
    quantapdf_composer *composer=nullptr;
    CHECK(quantapdf_composer_create_with_context(context,nullptr,&composer)==QUANTAPDF_OK);
    quantapdf_composer_page_options page_options{};page_options.struct_size=sizeof(page_options);
    page_options.width_points=page_options.height_points=6.0f;page_options.background_argb=UINT32_C(0xffffffff);
    size_t page_index=99u;
    CHECK(quantapdf_composer_add_page(composer,&page_options,&page_index)==QUANTAPDF_OK && page_index==0u);
    quantapdf_composer_path_command rectangle[5]{};
    rectangle[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    rectangle[1].kind=rectangle[2].kind=rectangle[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    rectangle[1].point1.x=rectangle[2].point1.x=6.0f;
    rectangle[2].point1.y=rectangle[3].point1.y=6.0f;
    rectangle[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    quantapdf_composer_clip_options clip_options{};clip_options.struct_size=sizeof(clip_options);
    clip_options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    quantapdf_composer_clip_id clip_id=0u;
    CHECK(quantapdf_composer_add_clip_path(composer,rectangle,5u,&clip_options,&clip_id)==QUANTAPDF_OK);
    quantapdf_composer_graphics_state_options gs{};gs.struct_size=sizeof(gs);
    gs.fill_alpha=gs.stroke_alpha=1.0f;gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;gs.clip_id=clip_id;
    quantapdf_composer_graphics_state_id gs_id=0u;
    CHECK(quantapdf_composer_add_graphics_state(composer,&gs,&gs_id)==QUANTAPDF_OK);
    rectangle[1].point1.x=rectangle[2].point1.x=3.0f;
    rectangle[2].point1.y=rectangle[3].point1.y=3.0f;
    quantapdf_composer_path_options path_options{};path_options.struct_size=sizeof(path_options);
    path_options.fill=1;path_options.fill_argb=UINT32_C(0xff0000ff);
    path_options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;path_options.graphics_state_id=gs_id;
    CHECK(quantapdf_composer_draw_path(composer,0u,rectangle,5u,&path_options)==QUANTAPDF_OK);
    unsigned char red[3]={255u,0u,0u};
    quantapdf_composer_raster raster{};raster.struct_size=sizeof(raster);
    raster.format=QUANTAPDF_COMPOSER_RASTER_RGB24;raster.width=raster.height=1u;
    raster.stride=raster.size=3u;raster.pixels=red;
    quantapdf_composer_image_id image_id=0u;
    CHECK(quantapdf_composer_add_raster(composer,&raster,&image_id)==QUANTAPDF_OK);
    quantapdf_composer_image_options image_options{};image_options.struct_size=sizeof(image_options);
    image_options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;image_options.graphics_state_id=gs_id;
    const quantapdf_rect bounds{3.0f,0.0f,6.0f,3.0f};
    CHECK(quantapdf_composer_draw_image(composer,0u,image_id,&bounds,&image_options)==QUANTAPDF_OK);
    CHECK(context->references==2u && composer->ir.operation_count==2u);
    const auto snapshot=[&]() {
        std::vector<unsigned char> bytes;
        const auto append=[&](const void *value,size_t n) {
            const auto ptr=static_cast<const unsigned char*>(value);bytes.insert(bytes.end(),ptr,ptr+n);
        };
        append(composer,sizeof(*composer));append(composer->ir.pages,sizeof(*composer->ir.pages));
        append(composer->ir.operations,2u*sizeof(*composer->ir.operations));
        append(composer->ir.operations[0].value.path.commands,5u*sizeof(*rectangle));
        append(composer->ir.resources.images,sizeof(*composer->ir.resources.images));
        append(composer->ir.resources.images[0].data,3u);
        append(composer->ir.resources.graphics_states,sizeof(*composer->ir.resources.graphics_states));
        append(composer->ir.resources.clips,sizeof(*composer->ir.resources.clips));
        append(composer->ir.resources.clips[0].commands,5u*sizeof(*rectangle));
        return bytes;
    };
    const auto original=snapshot();
    const size_t retained=composer->page_capacity*sizeof(*composer->ir.pages)+
        composer->operation_capacity*sizeof(*composer->ir.operations)+
        composer->ir.resources.image_capacity*sizeof(*composer->ir.resources.images)+
        composer->ir.resources.graphics_state_capacity*sizeof(*composer->ir.resources.graphics_states)+
        composer->ir.resources.clip_capacity*sizeof(*composer->ir.resources.clips)+
        10u*sizeof(*rectangle)+3u;
    quantapdf_ir_raster_image held{};
    for(unsigned dpi:{72u,96u,144u}) {
        quantapdf_native_source_raster_device_v1 device{};device.dpi=(float)dpi;
        quantapdf_ir_raster_scoped_mixed_v5_limits caps{512u,4u,512u,8000000u,1u,512u};
        quantapdf_native_execution_receipt_v1 receipt{};
        quantapdf_ir_raster_image out{};
        CHECK(quantapdf_native_composer_execute_v1(composer,&domain,&caps,&device,&receipt,&out)==QUANTAPDF_OK);
        const size_t side=dpi==72u?6u:dpi==96u?8u:12u,half=side/2u;
        CHECK(out.width==side && out.height==side && receipt.source_pixels==1u &&
              receipt.drawn_pixels==half*half && receipt.retained_bytes==retained &&
              receipt.completed_leases==2u && receipt.compiled_edges>0u);
        for(size_t y=0u;y<side;++y)for(size_t x=0u;x<side;++x)
            CHECK(rgba_at(out,x,y)==(y>=half?UINT32_C(0xffffffff):
                x<half?UINT32_C(0x0000ffff):UINT32_C(0xff0000ff)));
        const size_t work=receipt.charged_sample_work;
        const auto retry=[&](quantapdf_status want) {
            quantapdf_ir_raster_image result{};quantapdf_native_execution_receipt_v1 bill{};
            const auto status=quantapdf_native_composer_execute_v1(composer,&domain,&caps,&device,&bill,&result);
            const bool ok=status==want && snapshot()==original && context->references==2u &&
                domain.context==context && (want==QUANTAPDF_OK?
                  result.size==out.size && std::memcmp(result.pixels,out.pixels,out.size)==0:
                  zero_bitmap(result) && bill.output_bytes==0u && bill.charged_sample_work==0u);
            quantapdf_ir_raster_image_dispose(&result);return ok;
        };
        caps.max_sample_tests=work;CHECK(retry(QUANTAPDF_OK));
        caps.max_sample_tests=work-1u;CHECK(retry(QUANTAPDF_ERROR_UNSUPPORTED));caps.max_sample_tests=8000000u;
        const size_t capacity=composer->operation_capacity;composer->operation_capacity=SIZE_MAX;
        CHECK(quantapdf_native_composer_execute_v1(composer,&domain,&caps,&device,&receipt,&held)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(zero_bitmap(held));composer->operation_capacity=capacity;
        composer->ir.operations[0].value.path.options.transform.e=1.0f;
        CHECK(quantapdf_native_composer_execute_v1(composer,&domain,&caps,&device,&receipt,&held)==QUANTAPDF_ERROR_UNSUPPORTED);
        composer->ir.operations[0].value.path.options.transform.e=0.0f;
        CHECK(zero_bitmap(held) && snapshot()==original);
        CHECK(quantapdf_native_composer_execute_v1(composer,&wrong,&caps,&device,&receipt,&held)==QUANTAPDF_ERROR_ARGUMENT);
        CHECK(zero_bitmap(held) && context->references==2u && wrong_context->references==1u);
        CHECK(retry(QUANTAPDF_OK));
        if(dpi==144u)held=out;else quantapdf_ir_raster_image_dispose(&out);
    }
    quantapdf_output *output=nullptr;const unsigned char *serialized=nullptr;size_t serialized_size=0u;
    CHECK(quantapdf_composer_finish(composer,&output)==QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output,&serialized,&serialized_size)==QUANTAPDF_OK && serialized!=nullptr && serialized_size>0u);
    quantapdf_drop_output(output);CHECK(snapshot()==original && context->references==2u);
    quantapdf_drop_composer(composer);CHECK(context->references==1u);
    quantapdf_context_lease_drop_internal(&domain);quantapdf_context_lease_drop_internal(&wrong);
    CHECK(held.pixels!=nullptr && rgba_at(held,1u,1u)==UINT32_C(0x0000ffff));
    quantapdf_ir_raster_image_dispose(&held);
    std::puts("Composer preparation: real public PATH/RAW/Clip, exact capacity bytes, analytic 3-DPI RGBA, N/N-1 and unchanged finish passed");
    return EXIT_SUCCESS;
}

static int legacy_prepared_contracts(const quantapdf_qpdf_source_raw_image_limits& source_limits)
{
    const auto original=original_pdf(true);
    const quantapdf_ir_raster_scoped_mixed_v5_limits caps={1000000u,4u,512u,8000000u,1000000u,4000000u};
    for(const auto profile:{QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9}) {
        quantapdf_ir ir{};
        const auto reader=profile==QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8?
            quantapdf_qpdf_source_scoped_mixed_v8_to_ir:quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir;
        CHECK(reader(reinterpret_cast<const unsigned char*>(original.data()),original.size(),&source_limits,&ir)==QUANTAPDF_OK);
        quantapdf_native_source_raster_device_v1 device{};device.dpi=72.0f;
        quantapdf_native_prepared_page_v1 prepared{};
        CHECK(quantapdf_native_prepare_page_v1(&ir,&caps,&device,profile,&prepared)==QUANTAPDF_OK);
        CHECK(prepared.route==QP_NATIVE_PREPARED_LEGACY && prepared.usage.images_used==1u);
        quantapdf_ir_raster_image baseline{},got{};
        CHECK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&caps,&baseline)==QUANTAPDF_OK);
        quantapdf_native_execution_receipt_v1 receipt{};
        CHECK(quantapdf_native_execute_page_v1(&prepared,&receipt,&got)==QUANTAPDF_OK);
        CHECK(receipt.source_pixels==prepared.usage.image_pixels && receipt.drawn_pixels>0u &&
              receipt.retained_bytes==prepared.usage.retained_bytes && receipt.output_bytes==got.size &&
              receipt.peak_live_bytes>=receipt.retained_bytes+got.size &&
              receipt.peak_live_bytes<=prepared.resource_limits.max_live_bytes);
        CHECK(got.size==baseline.size && std::memcmp(got.pixels,baseline.pixels,got.size)==0);
        quantapdf_ir_raster_image_dispose(&baseline);quantapdf_ir_raster_image_dispose(&got);
        device.dpi=96.0f;
        CHECK(quantapdf_native_prepare_page_v1(&ir,&caps,&device,profile,&prepared)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(prepared.source==nullptr && prepared.route==QP_NATIVE_PREPARED_NONE);
        quantapdf_ir_dispose(&ir);
    }
    return EXIT_SUCCESS;
}


/* M2b.2b original Source ROOT Clip stage. One QPDF parser and one
 * detached owned IR; the public Bitmap route is deliberately still
 * unsupported until the jointly correlated V5 compiled page is ready. */
static int m2b2b_clipped_source_owner_contract(
    const quantapdf_qpdf_source_raw_image_limits& limits)
{
    target=0u;armed=false;track_c_allocation=false;c_fail_index=0u;
    const quantapdf_ir_raster_scoped_mixed_v5_limits raster={
        1000000u,4u,512u,8000000u,
        limits.max_source_pixels,limits.max_drawn_pixels
    };
    for(int mode:{83,84}) {
        const std::string original=native_ttf_fixture(mode);
        const auto read=[&](quantapdf_ir& output) {
            return quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(original.data()),
                original.size(),&limits,0u,&output);
        };
        quantapdf_ir ir{};
        const auto status=read(ir);
        if(status!=QUANTAPDF_OK)
            std::fprintf(stderr,"M2b2b Source mode=%d status=%d\n",
                mode,(int)status);
        CHECK(status==QUANTAPDF_OK &&
              ir.page_count==1u && ir.operation_count==3u &&
              ir.resources.image_count==1u &&
              ir.resources.graphics_state_count==1u &&
              ir.resources.graphics_state_capacity==1u &&
              ir.resources.clip_count==1u &&
              ir.resources.clip_capacity==1u &&
              ir.resources.clips!=nullptr);
        const auto& c=ir.resources.clips[0];
        CHECK(c.kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
              c.command_count==5u && c.commands!=nullptr &&
              c.fill_rule==(mode==83?
                  QUANTAPDF_COMPOSER_FILL_NONZERO:
                  QUANTAPDF_COMPOSER_FILL_EVEN_ODD) &&
              c.transform.a==1.0f && c.transform.d==1.0f);
        float minx=c.commands[0].point1.x,maxx=minx;
        float miny=c.commands[0].point1.y,maxy=miny;
        for(size_t i=0u;i<4u;++i) {
            const auto& p=c.commands[i].point1;
            if(p.x<minx)minx=p.x;
            if(p.x>maxx)maxx=p.x;
            if(p.y<miny)miny=p.y;
            if(p.y>maxy)maxy=p.y;
        }
        /* Exact authored Clip projection, not synthesized from
         * the Image footprint or post-raster output Bitmap. */
        CHECK(minx==12.0f && maxx==28.0f &&
              miny==24.0f && maxy==44.0f);
        const auto& gs=ir.resources.graphics_states[0];
        CHECK(gs.clip_id==1u && gs.fill_alpha==1.0f &&
              gs.stroke_alpha==1.0f &&
              gs.blend_mode==QUANTAPDF_COMPOSER_BLEND_NORMAL);
        const quantapdf_composer_operation_kind expected[3]={
            mode==83?QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES:
                QUANTAPDF_COMPOSER_OPERATION_IMAGE,
            QUANTAPDF_COMPOSER_OPERATION_PATH,
            mode==83?QUANTAPDF_COMPOSER_OPERATION_IMAGE:
                QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES
        };
        for(size_t i=0u;i<3u;++i)
            CHECK(ir.operations[i].kind==expected[i] &&
                  ir.operations[i].graphics_state_id==1u &&
                  ir.operations[i].page_index==0u);
        CHECK(ir.operations[1].value.path.options.graphics_state_id==1u);
        const size_t image_at=mode==83?2u:0u;
        CHECK(ir.operations[image_at].value.image.options.graphics_state_id==1u &&
              ir.operations[image_at].value.image.options.struct_size==
                  QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE &&
              ir.resources.images[0].size==12u);
        quantapdf_native_source_raster_device_v1 device{};
        device.dpi=96.0f;
        quantapdf_native_prepared_page_v1 denied;
        std::memset(&denied,0xa5,sizeof(denied));
        const auto prep=quantapdf_native_prepare_page_v1(
            &ir,&raster,&device,QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,&denied);
        CHECK(prep==QUANTAPDF_ERROR_UNSUPPORTED &&
              denied.source==nullptr);
        /* M2b.2c: ONE private compiled C11 V5 page from the actual
         * original Source-owned frozen Clip+Text+Fill+Image. The public
         * Bitmap adapter above must STILL fail closed. */
        for(unsigned dpi:{72u,96u,144u}) {
            quantapdf_native_device_plan physical{};
            CHECK(quantapdf_native_device_plan_create_physical(
                "0 0 128 64","0 0 128 64","1",0,dpi,
                raster.max_pixels,&physical)==QUANTAPDF_OK);
            quantapdf_native_work_ledger_v1 ledger{};
            const quantapdf_native_resource_limits_v1 resource_caps={
                raster.max_source_pixels,raster.max_drawn_pixels,8000000u
            };
            quantapdf_native_work_init_v1(
                &ledger,raster.max_sample_tests,raster.max_edges);
            CHECK(quantapdf_native_resources_init_v1(
                &ledger,&resource_caps,0u));
            m2a_malloc_fault_state observed_fault{};
            if(mode==83 && dpi==72u)
                ledger.malloc_fault={&observed_fault,m2a_malloc_fault_callback};
            quantapdf_native_scoped_page_v5 compiled{};
            const auto compiled_status=
                quantapdf_native_scoped_prepare_m2b2c_text_fill_image_v1(
                    &ir,&raster,&physical,&ledger,&compiled);
            if(compiled_status!=QUANTAPDF_OK)
                std::fprintf(stderr,
                    "M2b2c V5 compile mode=%d dpi=%u status=%d\n",
                    mode,dpi,(int)compiled_status);
            CHECK(compiled_status==QUANTAPDF_OK &&
                  compiled.ready && compiled.count==3u &&
                  compiled.width==physical.width &&
                  compiled.height==physical.height &&
                  ledger.resources.source_pixels==4u &&
                  ledger.resources.drawn_pixels>0u);
            for(size_t layer=0u;layer<3u;++layer) {
                quantapdf_native_geometry_info_v1 info{};
                CHECK(quantapdf_native_geometry_info_get_v1(
                    &compiled.geometry[layer],&info)==QUANTAPDF_OK &&
                      info.clips==1u && info.draws>0u);
            }
            quantapdf_ir_raster_image pixels{};
            const auto rendered=quantapdf_native_scoped_execute_v5(
                &compiled,&pixels);
            if(rendered!=QUANTAPDF_OK)
                std::fprintf(stderr,
                    "M2b2c V5 execute mode=%d dpi=%u status=%d\n",
                    mode,dpi,(int)rendered);
            CHECK(rendered==QUANTAPDF_OK &&
                  pixels.pixels!=nullptr && pixels.width==physical.width &&
                  pixels.height==physical.height &&
                  pixels.stride==physical.width*4u &&
                  pixels.size==physical.size &&
                  ledger.used.sample_work<=raster.max_sample_tests &&
                  ledger.used.edges<=raster.max_edges);
            const double scale=(double)dpi/72.0;
            size_t colored=0u;
            for(size_t y=0u;y<pixels.height;++y)
            for(size_t x=0u;x<pixels.width;++x) {
                const uint32_t pixel=rgba_at(pixels,x,y);
                CHECK((pixel&255u)==255u);
                if((double)(x+1u)<=12.0*scale ||
                   (double)x>=28.0*scale ||
                   (double)(y+1u)<=24.0*scale ||
                   (double)y>=44.0*scale)
                    CHECK(pixel==UINT32_C(0xffffffff));
                else if(pixel!=UINT32_C(0xffffffff))++colored;
            }
            CHECK(colored>0u && rgba_at(pixels,0u,0u)==UINT32_C(0xffffffff));
            /* M2b.2c first exact allocator/resource fault gate.
             * These all compile the SAME ORIGINAL Source-owned W leaf
             * and 3 paints, but never publish a partial result. Preserve
             * the independent old C11 four-malloc and PDF goldens. */
            if(mode==83 && dpi==72u) {
                CHECK(observed_fault.attempts>=6u &&
                      observed_fault.attempts<=64u &&
                      observed_fault.failures==0u &&
                      ledger.resources.peak_live_bytes>0u);
                const size_t attempts=observed_fault.attempts;
                const size_t peak=ledger.resources.peak_live_bytes;
                const std::vector<unsigned char> expected_rgba(
                    pixels.pixels,pixels.pixels+pixels.size);
                const auto trial=[&](
                    size_t fail_on,size_t source_cap,size_t memory_cap,
                    quantapdf_status wanted) {
                    quantapdf_native_work_ledger_v1 fault_ledger{};
                    quantapdf_native_work_init_v1(
                        &fault_ledger,raster.max_sample_tests,raster.max_edges);
                    quantapdf_native_resource_limits_v1 reduced_caps={
                        source_cap,raster.max_drawn_pixels,memory_cap
                    };
                    if(!quantapdf_native_resources_init_v1(
                        &fault_ledger,&reduced_caps,0u))return false;
                    m2a_malloc_fault_state fault{};
                    fault.fail_on=fail_on;
                    fault_ledger.malloc_fault={&fault,m2a_malloc_fault_callback};
                    quantapdf_native_scoped_page_v5 candidate{};
                    quantapdf_ir_raster_image out{};
                    quantapdf_status result=
                        quantapdf_native_scoped_prepare_m2b2c_text_fill_image_v1(
                            &ir,&raster,&physical,&fault_ledger,&candidate);
                    if(result==QUANTAPDF_OK)
                        result=quantapdf_native_scoped_execute_v5(
                            &candidate,&out);
                    bool ok=result==wanted;
                    if(wanted==QUANTAPDF_OK)
                        ok=ok && out.pixels!=nullptr &&
                            out.size==expected_rgba.size() &&
                            std::memcmp(out.pixels,expected_rgba.data(),
                                        expected_rgba.size())==0 &&
                            fault.failures==0u;
                    else
                        ok=ok && zero_bitmap(out) &&
                            (fail_on==0u ||
                             (fault.failures==1u &&
                              fault.attempts==fail_on));
                    quantapdf_native_raster_image_dispose_v1(&fault_ledger,&out);
                    quantapdf_native_scoped_dispose_v5(&candidate);
                    ok=ok && fault_ledger.resources.live_bytes==0u &&
                        fault_ledger.reserved.sample_work==0u &&
                        fault_ledger.reserved.edges==0u;
                    if(!ok)
                        std::fprintf(stderr,
                            "M2b2c fault fail=%zu source=%zu live=%zu got=%d wanted=%d seen=%zu\n",
                            fail_on,source_cap,memory_cap,
                            (int)result,(int)wanted,fault.attempts);
                    return ok;
                };
                const size_t marks[3]={1u,(attempts+1u)/2u,attempts};
                for(size_t n:marks)
                    CHECK(trial(n,resource_caps.max_source_pixels,
                                resource_caps.max_live_bytes,
                                QUANTAPDF_ERROR_NOMEM));
                CHECK(trial(0u,3u,resource_caps.max_live_bytes,
                            QUANTAPDF_ERROR_UNSUPPORTED));
                CHECK(peak>1u);
                CHECK(trial(0u,resource_caps.max_source_pixels,peak-1u,
                            QUANTAPDF_ERROR_UNSUPPORTED));
                CHECK(trial(0u,resource_caps.max_source_pixels,peak,
                            QUANTAPDF_OK));
            }
            quantapdf_native_raster_image_dispose_v1(&ledger,&pixels);
            quantapdf_native_scoped_dispose_v5(&compiled);
            CHECK(ledger.resources.live_bytes==0u &&
                  ledger.reserved.sample_work==0u &&
                  ledger.reserved.edges==0u);
        }
        quantapdf_ir_dispose(&ir);
        CHECK(cleared(ir));
        CHECK(read(ir)==QUANTAPDF_OK &&
              ir.resources.clip_count==1u &&
              ir.resources.graphics_state_count==1u);
        quantapdf_ir_dispose(&ir);
        CHECK(cleared(ir));
        if(mode==83) {
            /* Same original and exact C allocation hook as existing M2b.1:
             * the new owned GS+Clip is part of the one Source transaction.
             * First/middle/last failures must publish zero IR; a good
             * re-open after each proves no damaged source owner escapes. */
            quantapdf_ir reference{};
            track_c_allocation=true;c_fail_index=0u;
            c_allocations=0u;c_faults=0u;
            const auto counted=read(reference);
            const size_t calls=c_allocations;
            track_c_allocation=false;
            CHECK(counted==QUANTAPDF_OK && calls>=7u && calls<=64u &&
                  c_faults==0u && reference.resources.clip_count==1u);
            quantapdf_ir_dispose(&reference);
            CHECK(cleared(reference));
            const size_t probes[3]={1u,(calls+1u)/2u,calls};
            for(size_t index:probes) {
                quantapdf_ir failed;
                std::memset(&failed,0xa5,sizeof(failed));
                track_c_allocation=true;c_fail_index=index;
                c_allocations=0u;c_faults=0u;
                const auto denied_status=read(failed);
                track_c_allocation=false;c_fail_index=0u;
                CHECK(denied_status==QUANTAPDF_ERROR_NOMEM &&
                      c_faults==1u && c_allocations==index &&
                      cleared(failed));
                quantapdf_ir recovered{};
                CHECK(read(recovered)==QUANTAPDF_OK &&
                      recovered.resources.clip_count==1u &&
                      recovered.resources.graphics_state_count==1u &&
                      recovered.operation_count==3u);
                quantapdf_ir_dispose(&recovered);
                CHECK(cleared(recovered));
            }
            auto smaller=limits;
            smaller.max_operations=2u;
            quantapdf_ir refused;
            std::memset(&refused,0xa5,sizeof(refused));
            const auto budget_status=quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
                reinterpret_cast<const unsigned char*>(original.data()),
                original.size(),&smaller,0u,&refused);
            CHECK(budget_status==QUANTAPDF_ERROR_UNSUPPORTED && cleared(refused));
        }
    }
    /* Every later invalid op/resource must refuse the WHOLE original,
     * even though the earlier Text/Fill/Image+Clip prefix is valid. */
    for(int mode=85;mode<=90;++mode) {
        const auto original=native_ttf_fixture(mode);
        quantapdf_ir denied;
        std::memset(&denied,0xa5,sizeof(denied));
        const auto status=quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
            reinterpret_cast<const unsigned char*>(original.data()),
            original.size(),&limits,0u,&denied);
        CHECK((status==QUANTAPDF_ERROR_UNSUPPORTED ||
               status==QUANTAPDF_ERROR_FORMAT) && cleared(denied));
    }
    std::puts("M2b2b: original W/W* immutable Source GS/Clip owner, late refusals, public Text+Clip still closed PASS");
    return EXIT_SUCCESS;
}

/* M2b.2a private C11 correlated glyph/Clip fault acceptance. This
 * exercises the REAL exact C11 mallocs in the SAME existing Source/OOM
 * executable; the public original W/W* Source profile is still closed. */
static int m2b2a_private_clipped_text_faults()
{
    quantapdf_composer_page_state page{};
    quantapdf_composer_path_command commands[5]{},clip_commands[5]{};
    quantapdf_outline_glyph glyph{};
    quantapdf_composer_operation op{};
    quantapdf_composer_clip_state clip{};
    quantapdf_ir ir{};
    quantapdf_native_device_plan physical{};
    const quantapdf_ir_raster_v1_limits caps={16u,1u,64u,800000u};
    page.width_points=2.0f;page.height_points=2.0f;
    page.background_argb=UINT32_C(0xffffffff);
    commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1.x=0.75f;
    commands[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[2].point1.x=0.75f;commands[2].point1.y=1.0f;
    commands[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[3].point1.y=1.0f;
    commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    clip_commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    clip_commands[0].point1.x=0.5f;
    clip_commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    clip_commands[1].point1.x=1.0f;
    clip_commands[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    clip_commands[2].point1.x=1.0f;clip_commands[2].point1.y=1.0f;
    clip_commands[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    clip_commands[3].point1.x=0.5f;clip_commands[3].point1.y=1.0f;
    clip_commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    glyph.commands=commands;glyph.command_count=5u;
    op.kind=QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES;
    op.value.text_outlines.glyphs=&glyph;
    op.value.text_outlines.glyph_count=1u;
    op.value.text_outlines.argb=UINT32_C(0xff000000);
    clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clip.commands=clip_commands;clip.command_count=5u;
    clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    clip.transform.a=clip.transform.d=1.0f;
    ir.pages=&page;ir.page_count=1u;
    ir.operations=&op;ir.operation_count=1u;
    CHECK(quantapdf_native_device_plan_create_physical(
        "0 0 2 2","0 0 2 2","1",0,96u,16u,&physical)==QUANTAPDF_OK);
    std::vector<unsigned char> golden;
    size_t successful_calls=0u;
    auto trial=[&](size_t failure_index,bool baseline) -> bool {
        quantapdf_native_work_ledger_v1 ledger{};
        quantapdf_native_resource_limits_v1 resource_caps={0u,0u,100000u};
        quantapdf_native_geometry_v1 prepared{};
        quantapdf_ir_raster_image output{};
        quantapdf_native_work_init_v1(&ledger,800000u,64u);
        if(!quantapdf_native_resources_init_v1(&ledger,&resource_caps,0u))
            return false;
        c_fail_index=failure_index;c_allocations=0u;c_faults=0u;
        track_c_allocation=true;
        auto result=quantapdf_native_geometry_prepare_text_clipped_v1(
            &ir,&caps,&physical,&clip,&ledger,&prepared);
        if(result==QUANTAPDF_OK)
            result=quantapdf_native_geometry_execute_v1(
                &prepared,nullptr,&output);
        track_c_allocation=false;c_fail_index=0u;
        const size_t attempts=c_allocations,faults_seen=c_faults;
        bool valid=false;
        if(baseline) {
            valid=result==QUANTAPDF_OK && output.pixels!=nullptr &&
                output.size==output.width*output.height*4u &&
                attempts>=4u && attempts<=12u && faults_seen==0u;
            if(valid && golden.empty()) {
                golden.assign(output.pixels,output.pixels+output.size);
                successful_calls=attempts;
            } else if(valid) {
                valid=golden.size()==output.size &&
                    std::memcmp(golden.data(),output.pixels,output.size)==0 &&
                    successful_calls==attempts;
            }
        } else {
            valid=result==QUANTAPDF_ERROR_NOMEM && output.pixels==nullptr &&
                output.size==0u && output.width==0u &&
                faults_seen==1u && attempts==failure_index;
        }
        quantapdf_native_geometry_dispose_v1(&prepared);
        quantapdf_native_raster_image_dispose_v1(&ledger,&output);
        valid=valid && ledger.resources.live_bytes==0u &&
            ledger.reserved.sample_work==0u;
        if(!valid)
            std::fprintf(stderr,
                "M2b2a C11 malloc failure=%zu result=%d attempts=%zu faults=%zu baseline=%d\n",
                failure_index,(int)result,attempts,faults_seen,(int)baseline);
        return valid;
    };
    CHECK(trial(0u,true));
    for(size_t fail=1u;fail<=successful_calls;++fail) {
        CHECK(trial(fail,false));
        CHECK(trial(0u,true)); /* same private owner lifecycle recovery */
    }
    return EXIT_SUCCESS;
}

int main(int argc,char **argv)
{
    const quantapdf_qpdf_source_raw_image_limits limits={
        2u*1024u*1024u,65536u,4u,4u,1000000u,1000000u,4000000u,8u
    };
    /* The production master #2307 sanitizer RED was CTest's 40-second
     * deadline for TWO independent Source/OOM suites in one invocation,
     * not an allocator, geometry or Bitmap failure. Run the exact same
     * M2a suite through this existing test-linked production executable,
     * but with its own 40-second CTest lease. No test case is discarded. */
    if(argc==2 && std::strcmp(argv[1],"--m2a")==0) {
        CHECK(m2a_ordered_text_image_source_contract(limits)==EXIT_SUCCESS);
        CHECK(m2b1_ordered_source_ir_contract(limits)==EXIT_SUCCESS);
        CHECK(m2b2b_clipped_source_owner_contract(limits)==EXIT_SUCCESS);
        return m2b2a_private_clipped_text_faults();
    }
    if(argc!=3)return EXIT_FAILURE;
    CHECK(native_ttf_subset_contracts()==EXIT_SUCCESS);
    const std::string pdf=original_pdf();
    CHECK(owned_page_contracts()==EXIT_SUCCESS);
    CHECK(composer_preparation_contracts()==EXIT_SUCCESS);
    CHECK(legacy_prepared_contracts(limits)==EXIT_SUCCESS);
    CHECK(native_ttf_source_and_raster_contracts(limits)==EXIT_SUCCESS);
    CHECK(native_ttf_source_and_raster_contracts(limits,32)==EXIT_SUCCESS);
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
    for(unsigned dpi:{72u,96u,144u})
        if(!physical_texample_page_fringe(dpi))return EXIT_FAILURE;
    if(!physical_texample_page_fringe(72u,true))return EXIT_FAILURE;
    if(!authentic_texample_source_m3(argv[2],limits))
        return EXIT_FAILURE;
    if(!public_texample_complete_page(argv[2]))return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

