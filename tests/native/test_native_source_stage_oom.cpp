/* Real Source V10 std::vector new failures. The EXACT production QPDF
 * Source unit is compiled independently with eight isolated test-only
 * pre-allocation arming points, not with a replacement source/provider.
 * QPDF authors the original PDF; PDFium is never linked. */
#include <quantapdf/quantapdf.h>
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
static bool original_texample_three_layer(const quantapdf_ir& source,
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
    const auto render=physical_dpi?
        quantapdf_ir_raster_render_v1_texample_physical_dpi_three_layer:shared?
        quantapdf_ir_raster_render_v1_texample_shared_three_layer:
        quantapdf_ir_raster_render_v1_texample_physical_three_layer;
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
        const auto& text=ir.operations[i].value.text_outlines;
        append(text.glyphs,text.glyph_count*sizeof(*text.glyphs));
        for(size_t g=0u;g<text.glyph_count;++g)
            append(text.glyphs[g].commands,text.glyphs[g].command_count*sizeof(*text.glyphs[g].commands));
    }
    return result;
}
static int native_ttf_source_and_raster_contracts(
    const quantapdf_qpdf_source_raw_image_limits& source_limits)
{
    const auto original=native_ttf_fixture(1);
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
    CHECK(accepted==QUANTAPDF_OK && source_allocations==9u && ir.operation_count==3u);
    for(size_t i=0u;i<3u;++i)CHECK(ir.operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES);
    CHECK(ir.operations[0].value.text_outlines.glyph_count==2u &&
          ir.operations[1].value.text_outlines.glyph_count==1u &&
          ir.operations[2].value.text_outlines.glyph_count==1u);
    const auto snapshot=text_ir_snapshot(ir);
    const auto goldens=std::filesystem::path(__FILE__).parent_path()/"goldens";
    quantapdf_ir_raster_image held{};std::vector<unsigned char> held_bytes;
    for(unsigned dpi:{72u,96u,144u}) {
        quantapdf_native_device_plan plan{};
        CHECK(quantapdf_native_device_plan_create_physical("0 0 128 64","0 0 128 64","1",0,dpi,1000000u,&plan)==QUANTAPDF_OK);
        quantapdf_ir_raster_v1_limits limits={1000000u,4u,512u,8000000u};
        size_t work=0u;quantapdf_ir_raster_image full{};
        track_c_allocation=true;c_fail_index=0u;c_allocations=0u;c_faults=0u;
        const auto status=quantapdf_ir_raster_render_v1_text_outlines(&ir,&limits,&plan,&work,&full);
        track_c_allocation=false;
        CHECK(status==QUANTAPDF_OK && c_allocations==4u && work>0u && work<8000000u);
        std::ifstream file(goldens/("roboto-layers-"+std::to_string(dpi)+"-m0.rgba"),std::ios::binary);
        const std::vector<unsigned char> expected{std::istreambuf_iterator<char>(file),{}};
        CHECK(expected.size()==full.size && std::memcmp(expected.data(),full.pixels,full.size)==0);
        const auto retry=[&](const quantapdf_ir_raster_v1_limits& budget,quantapdf_status want,size_t fail_index=0u) {
            quantapdf_ir_raster_image result;std::memset(&result,0xa5,sizeof(result));size_t seen=999u;
            track_c_allocation=fail_index!=0u;c_fail_index=fail_index;c_allocations=0u;c_faults=0u;
            const auto got=quantapdf_ir_raster_render_v1_text_outlines(&ir,&budget,&plan,&seen,&result);
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

int main(int argc,char **argv)
{
    const std::string pdf=original_pdf();
    const quantapdf_qpdf_source_raw_image_limits limits={
        2u*1024u*1024u,65536u,4u,4u,1000000u,1000000u,4000000u,8u
    };
    CHECK(native_ttf_source_and_raster_contracts(limits)==EXIT_SUCCESS);
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

