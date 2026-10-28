/* Real Source V10 std::vector new failures. The EXACT production QPDF
 * Source unit is compiled independently with seven isolated test-only
 * pre-allocation arming points, not with a replacement source/provider.
 * QPDF authors the original PDF; PDFium is never linked. */
#include "backend/qpdf_native_image_ir.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
int main()
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
    return EXIT_SUCCESS;
}
