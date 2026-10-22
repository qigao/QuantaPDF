/* Test-only linear Stroke source-PDF conformance. PDFium is an independent oracle:
 * Original source PDF -> PDFium versus Source -> detached Graph -> owned IR
 * -> private pure-C11 scoped Clip DAG raster. Never export IR to PDF as a
 * production rendering intermediate and never invoke a PDFium fallback. */
#include "document/page_composer_ir.h"
#include "render/ir_scoped_operations_v1.h"
#include "render/ir_raster.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <fpdfview.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#define CHECK(e) do { if (!(e)) { \
    std::fprintf(stderr, "%s:%d: source q/Q oracle: %s\n", \
                 __FILE__,__LINE__,#e); std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

namespace {
std::string source_prefix;
size_t negative_index=0u;
constexpr size_t PAGE = 120u;
const quantapdf_ir_raster_v1_limits raster_limits = {
    PAGE * PAGE, 4u, 2048u, 200000000u
};
const quantapdf_graph_limits graph_limits = {
    10000u, 30000u, 1048576u, 1048576u, 4194304u, 256u
};
const quantapdf_graph_stream_limits decode_limits = {8192u,128u,100000u};
const quantapdf_pdf_content_limits parse_limits = {8192u,256u,16u};
const quantapdf_pdf_path_lower_limits lower_limits = {4u,256u};

struct probe {
    int x, y_pdf;
    unsigned r, g, b;
};

struct rgb_image {
    int width=0,height=0,stride=0,components=0;
    std::vector<unsigned char> pixels;
    const unsigned char *at(size_t x, size_t y) const
    {
        CHECK(x < (size_t)width && y < (size_t)height);
        return pixels.data() + y * (size_t)stride + x * 3u;
    }
};

/* Real, well-formed source PDF document; all q/Q/W/W* operators are native
 * PDF source syntax parsed after an independent Graph snapshot capture. */
std::vector<unsigned char> make_source(const char *path,const char *content)
{
    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto root = pdf->getRoot().getKey("/Pages");
    auto page = pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    root.replaceKey("/Kids", QPDFObjectHandle::newArray({page}));
    root.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    root.replaceKey("/MediaBox",
                    QPDFObjectHandle::parse("[0 0 120 120]"));
    page.replaceKey("/Parent", root);
    page.replaceKey("/Contents", QPDFObjectHandle::newArray({
        pdf->newStream("q "), pdf->newStream(content), pdf->newStream(" Q")}));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    auto blob = writer.getBufferSharedPointer();
    FILE *f = std::fopen(path,"wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),f) ==
          blob->getSize());
    CHECK(std::fclose(f) == 0);
    return {blob->getBuffer(),blob->getBuffer()+blob->getSize()};
}

std::vector<unsigned char> source_bytes(const char *path)
{
    FILE *f=std::fopen(path,"rb"); CHECK(f);
    CHECK(std::fseek(f,0,SEEK_END)==0); const long length=std::ftell(f);
    CHECK(length>0 && length<=1048576); CHECK(std::fseek(f,0,SEEK_SET)==0);
    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    CHECK(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size()); CHECK(std::fclose(f)==0);
    return bytes;
}

quantapdf_status snapshot_lower(const char *input_path,quantapdf_ir *out,
                                size_t page_index=0u,size_t max_streams=3u,
                                size_t max_parent_steps=2u)
{
    quantapdf_document *doc=nullptr;
    quantapdf_graph_snapshot *snap=nullptr;
    quantapdf_status st;
    CHECK(out != nullptr);
    std::memset(out,0,sizeof(*out));
    st = quantapdf_open(input_path,nullptr,&doc);
    if(st != QUANTAPDF_OK) return st;
    st = quantapdf_document_capture_graph(doc,&graph_limits,&snap);
    quantapdf_close(doc);
    if(st != QUANTAPDF_OK) return st;
    st = quantapdf_graph_snapshot_page_composer_ir(
        snap,page_index,max_parent_steps,&decode_limits,&parse_limits,max_streams,&lower_limits,out);
    /* Deliberately sever all source-page ownership before rasterization. */
    quantapdf_graph_snapshot_drop(snap);
    return st;
}

/* Render an unchanged source PDF directly through PDFium's public C API.
 * This oracle bypasses QuantaPDF's document and render wrappers. */
rgb_image render_direct_pdfium_reference(const char *source_path,int scale=1)
{
    struct pdfium_runtime {
        pdfium_runtime() { FPDF_InitLibrary(); }
        ~pdfium_runtime() { FPDF_DestroyLibrary(); }
    };
    static pdfium_runtime runtime;
    (void)runtime;
    FPDF_DOCUMENT document = FPDF_LoadDocument(source_path, nullptr);
    CHECK(document != nullptr);
    CHECK(FPDF_GetPageCount(document) > 0);
    FPDF_PAGE page = FPDF_LoadPage(document, 0);
    CHECK(page != nullptr);
    const double pw = static_cast<double>(FPDF_GetPageWidthF(page));
    const double ph = static_cast<double>(FPDF_GetPageHeightF(page));
    CHECK(std::isfinite(pw) && std::isfinite(ph) && pw > 0.0 && ph > 0.0);
    CHECK(std::ceil(pw) <= 8192.0 && std::ceil(ph) <= 8192.0 && std::ceil(pw) * std::ceil(ph) <= 60000000.0);
    rgb_image out;
    out.width = static_cast<int>(std::ceil(pw))*scale;
    out.height = static_cast<int>(std::ceil(ph))*scale;
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(out.width, out.height, FPDFBitmap_BGR, nullptr, 0);
    CHECK(bitmap != nullptr);
    FPDFBitmap_FillRect(bitmap, 0, 0, out.width, out.height, 0xFFFFFFFFu);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, out.width, out.height, 0, FPDF_ANNOT);
    const int source_stride = FPDFBitmap_GetStride(bitmap);
    const auto *source = static_cast<const unsigned char *>(FPDFBitmap_GetBuffer(bitmap));
    CHECK(source != nullptr && source_stride >= out.width * 3);
    out.stride = out.width * 3;
    out.components = 3;
    out.pixels.resize(static_cast<size_t>(out.height) * static_cast<size_t>(out.stride));
    for (int y = 0; y < out.height; ++y) {
        const unsigned char *src = source + static_cast<size_t>(y) * static_cast<size_t>(source_stride);
        unsigned char *dst = out.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(out.stride);
        for (int x = 0; x < out.width; ++x) {
            dst[static_cast<size_t>(x) * 3u] = src[static_cast<size_t>(x) * 3u + 2u];
            dst[static_cast<size_t>(x) * 3u + 1u] = src[static_cast<size_t>(x) * 3u + 1u];
            dst[static_cast<size_t>(x) * 3u + 2u] = src[static_cast<size_t>(x) * 3u];
        }
    }
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(document);
    return out;
}

/* Page background in the source PDF is implicit transparent. PDFium's
 * RGB output is white-composited, whereas native IR returns straight RGBA8.
 * Compare the native value composited over white, without altering it. */
unsigned on_white(const unsigned char *rgba, size_t channel)
{
    unsigned alpha = rgba[3];
    return ((unsigned)rgba[channel]*alpha + 255u*(255u-alpha) + 127u)/255u;
}

void compare(const char *name,const char *content,
             const std::vector<probe>& probes={},int scale=1)
{
    const std::string file=source_prefix+"."+name+".pdf";
    const auto original=make_source(file.c_str(),content);
    CHECK(source_bytes(file.c_str())==original);
    quantapdf_ir ir{}; OK(snapshot_lower(file.c_str(),&ir));
    CHECK(source_bytes(file.c_str())==original);
    CHECK(ir.operation_count>0u && ir.operation_count<=4u);
    quantapdf_ir_raster_image native{},repeat{};
    quantapdf_native_device_plan plan{};
    auto bounds=raster_limits;
    CHECK(scale==1 || scale==2);
    const size_t side=PAGE*static_cast<size_t>(scale);
    bounds.max_pixels=side*side; bounds.max_sample_tests*=static_cast<size_t>(scale*scale);
    if(scale==2) OK(quantapdf_native_device_plan_create("0 0 120 120","0 0 120 120","1",0,144,side*side,&plan));
    const auto render=[&](quantapdf_ir_raster_image *out) {
        return scale==1 ? quantapdf_ir_raster_scoped_operations_v2(&ir,0,&bounds,out) :
            quantapdf_ir_raster_scoped_operations_v2_planned(&ir,0,&bounds,&plan,out);
    };
    OK(render(&native)); OK(render(&repeat));
    CHECK(native.width==side && native.height==side && native.size==side*side*4u);
    CHECK(native.size==repeat.size && std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&ir); /* Pixels survive all source/Graph/IR ownership. */
    const auto reference=render_direct_pdfium_reference(file.c_str(),scale);
    CHECK(source_bytes(file.c_str())==original);
    double error=0; size_t gross=0;
    for(size_t y=0;y<side;++y) for(size_t x=0;x<side;++x) {
        const auto *a=native.pixels+y*native.stride+x*4u;
        const auto *b=reference.at(x,y); unsigned largest=0;
        for(size_t c=0;c<3;++c) {
            const unsigned delta=static_cast<unsigned>(std::abs(static_cast<int>(on_white(a,c))-static_cast<int>(b[c])));
            error+=delta; largest=std::max(largest,delta);
        }
        if(largest>64u) ++gross;
    }
    const double mean=error/static_cast<double>(side*side*3u);
    std::fprintf(stderr,"stroke original oracle %s: mean=%.4f gross=%zu\n",name,mean,gross);
    /* Different edge antialiasing is expected. Large geometric differences
     * cannot hide behind a mean: separately cap >64 errors and probe solids. */
    CHECK(mean<2.0 && gross<side*side/100u);
    for(const auto& p:probes) {
        size_t y=side-1u-static_cast<size_t>(p.y_pdf*scale), x=static_cast<size_t>(p.x*scale);
        const auto *a=native.pixels+y*native.stride+x*4u;
        const auto *b=reference.at(x,y); const unsigned expected[]={p.r,p.g,p.b};
        for(size_t c=0;c<3;++c) {
            CHECK(std::abs(static_cast<int>(on_white(a,c))-static_cast<int>(expected[c]))<=3);
            if(std::abs(static_cast<int>(b[c])-static_cast<int>(expected[c]))>3) std::fprintf(stderr,"probe %s (%d,%d) native=%u reference=%u expected=%u channel=%zu\n",name,p.x,p.y_pdf,on_white(a,c),b[c],expected[c],c);
            CHECK(std::abs(static_cast<int>(b[c])-static_cast<int>(expected[c]))<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native); CHECK(std::remove(file.c_str())==0);
}
void negative(const char *content)
{
    const std::string file=source_prefix+".negative-"+std::to_string(++negative_index)+".pdf";
    const auto original=make_source(file.c_str(),content);
    quantapdf_ir ir{}; OK(snapshot_lower(file.c_str(),&ir));
    quantapdf_ir_raster_image image{};
    CHECK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&raster_limits,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size && !image.width && !image.height && !image.stride);
    quantapdf_ir_dispose(&ir); CHECK(source_bytes(file.c_str())==original); CHECK(std::remove(file.c_str())==0);
}
void test_round_source()
{
    compare("round-horizontal-caps","1 0 0 RG 8 w 1 J 40 40 m 80 40 l S",
        {{37,40,255,0,0},{36,36,255,255,255},{82,40,255,0,0}});
    compare("round-vertical-caps","1 0 0 RG 8 w 1 J 60 40 m 60 80 l S",
        {{60,37,255,0,0},{56,36,255,255,255}});
    compare("round-diagonal-caps","1 0 0 RG 6 w 1 J 20 20 m 90 80 l S");
    compare("round-outer-join","1 0 0 RG 8 w 1 j 40 40 m 80 40 l 80 90 l S",
        {{83,36,255,255,255},{82,38,255,0,0}});
    compare("round-opposite-turn","1 0 0 RG 8 w 1 j 40 80 m 80 80 l 80 30 l S");
    compare("round-short-butt-end","1 0 0 RG 8 w 1 j 40 40 m 80 40 l 80 42 l S",
        {{81,43,255,255,255},{81,41,255,0,0}});
    compare("round-acute-join","1 0 0 RG 6 w 1 j 20 20 m 90 20 l 25 40 l S");
    compare("round-closed","1 0 0 RG 6 w 1 J 1 j 30 30 m 90 30 l 90 90 l h S");
    compare("round-both","1 0 0 RG 8 w 1 J 1 j 40 40 m 80 40 l 80 90 l S");
    compare("round-subpaths","0 0 1 RG 6 w 1 J 1 j 20 30 m 90 75 l 20 75 m 90 30 l S");
    compare("round-dash-caps","1 0 0 RG 4 w 1 J [16 12] 3 d 20 40 m 100 40 l S");
    compare("round-dash-join","1 0 0 RG 8 w 1 J 1 j [60 20] 0 d 40 40 m 80 40 l 80 90 l S");
    compare("round-dash-vertex","1 0 0 RG 8 w 1 J 1 j [40 20] 0 d 40 40 m 80 40 l 80 90 l S");
    compare("round-dash-seam","1 0 0 RG 8 w 1 J 1 j [50 20] 0 d 40 40 m 80 40 l 80 80 l 40 80 l h S");
    compare("round-affine","1 0 0 RG 6 w 1 J 1 j 1.5 .2 .3 .7 0 0 cm 20 20 m 55 20 l 55 85 l S");
    compare("round-reflection","1 0 0 RG 6 w 1 J 1 j -1 .2 .3 .8 95 5 cm 20 20 m 75 20 l 75 90 l S");
    compare("round-clip-restore","q 30 20 40 60 re W n 1 0 0 RG 8 w 1 J 1 j 20 40 m 90 40 l 90 70 l S Q 0 0 1 rg 10 10 90 10 re f");
    compare("round-nested-evenodd","q 10 10 100 100 re W n q 20 20 80 80 re 40 40 40 40 re W* n 1 0 0 RG 6 w 1 J [16 12] 0 d 10 60 m 110 60 l S Q 0 0 1 RG 4 w 1 J 10 30 m 110 30 l S Q");
    compare("round-all-off","4 w 1 J 1 j [1 200] 2 d 20 40 m 100 40 l S");
    compare("round-thin","1 0 0 RG .5 w 1 J 20 40 m 100 40 l S");
    compare("round-fit-144","1 0 0 RG 5 w 1 J 1 j 20 30 m 90 30 l 90 85 l S",{},2);
    compare("round-dash-fit-144","1 0 0 RG 4 w 1 J 1 j [24 16] 3 d 20 30 m 90 30 l 90 85 l S",{},2);
    negative("0 w 1 J 1 j 20 20 m 90 20 l S");
    negative("4 w 1 J 1 j 20 20 m 40 50 60 50 90 20 c S");
    negative("4 w 1 J [0 .0001] 0 d 20 20 m 90 20 l S");
}

/* Mathematical modulo equivalence is independent of an oracle's treatment
 * of zero intervals. Keep the unreduced source intact for both consumers. */
void test_zero_phase_equivalence()
{
    quantapdf_ir_raster_image native[2]{};
    rgb_image reference[2];
    const char *content[]={
        "1 0 0 RG 4 w 1 J [0 16] 5 d 20 40 m 100 40 l S",
        "1 0 0 RG 4 w 1 J [0 16] 16005 d 20 40 m 100 40 l S"};
    for(size_t i=0;i<2;++i) {
        const std::string path=source_prefix+".phase-equivalence-"+std::to_string(i)+".pdf";
        const auto original=make_source(path.c_str(),content[i]);
        quantapdf_ir ir{}; OK(snapshot_lower(path.c_str(),&ir));
        CHECK(source_bytes(path.c_str())==original);
        OK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&raster_limits,&native[i]));
        quantapdf_ir_dispose(&ir);
        reference[i]=render_direct_pdfium_reference(path.c_str());
        CHECK(source_bytes(path.c_str())==original); CHECK(std::remove(path.c_str())==0);
    }
    CHECK(native[0].size==native[1].size && std::memcmp(native[0].pixels,native[1].pixels,native[0].size)==0);
    for(int x:{31,47,63,79,95}) {
        const auto *p=native[1].pixels+79u*native[1].stride+static_cast<size_t>(x)*4u;
        CHECK(p[0]==255 && p[1]==0 && p[2]==0 && p[3]==255);
    }
    size_t differing=0;
    for(size_t y=0;y<PAGE;++y) for(size_t x=0;x<PAGE;++x) {
        const auto *a=reference[0].at(x,y),*b=reference[1].at(x,y);
        if(std::memcmp(a,b,3)!=0) ++differing;
    }
    /* Diagnostic only: a future corrected PDFium may render equal phases
     * identically. This pair is not counted as a raster-oracle positive. */
    std::fprintf(stderr,"zero-dash original phase 5/16005: native identical; direct PDFium differing pixels=%zu\n",differing);
    for(auto& image:native) quantapdf_ir_raster_image_dispose(&image);
}
void test_zero_dash_source()
{
    compare("zero-round-dots","1 0 0 RG 6 w 1 J [0 16] 0 d 20 40 m 100 40 l S",
        {{20,40,255,0,0},{28,40,255,255,255},{100,40,255,255,255}});
    compare("zero-square-dots","1 0 0 RG 6 w 2 J [0 16] 0 d 20 40 m 100 40 l S",
        {{18,38,255,0,0},{28,40,255,255,255},{102,42,255,255,255}});
    compare("zero-butt-empty","1 0 0 RG 6 w [0 16] 0 d 20 40 m 100 40 l S",
        {{28,40,255,255,255},{44,40,255,255,255}});
    compare("zero-round-phase","1 0 0 RG 4 w 1 J [0 16] 5 d 20 40 m 100 40 l S");
    compare("zero-round-wrapped-phase","1 0 0 RG 4 w 1 J [0 16] 37 d 20 40 m 100 40 l S");
    compare("zero-square-diagonal","1 0 0 RG 4 w 2 J [0 16] 0 d 20 20 m 90 80 l S");
    compare("zero-round-odd","1 0 0 RG 4 w 1 J [0 12 8] 0 d 20 40 m 100 40 l S");
    compare("zero-square-odd","1 0 0 RG 4 w 2 J [8 0 4] 0 d 20 40 m 100 40 l S");
    compare("zero-consecutive","1 0 0 RG 4 w 2 J [0 0 0 16] 0 d 20 40 m 100 40 l S");
    compare("zero-gap-butt","1 0 0 RG 8 w [40 0] 0 d 40 40 m 80 40 l 80 90 l S",
        {{82,37,255,255,255},{80,50,255,0,0}});
    compare("zero-gap-square","1 0 0 RG 8 w 2 J [40 0] 0 d 40 40 m 80 40 l 80 90 l S",
        {{82,37,255,0,0}});
    compare("zero-round-vertex","1 0 0 RG 6 w 1 J [0 40] 0 d 40 40 m 80 40 l 80 90 l S");
    compare("zero-square-vertex","1 0 0 RG 6 w 2 J [0 40] 0 d 40 40 m 80 40 l 80 90 l S");
    compare("zero-square-oblique-vertex","1 0 0 RG 6 w 2 J [0 40] 0 d 40 40 m 80 40 l 100 80 l S");
    compare("zero-round-closed","1 0 0 RG 4 w 1 J [0 40] 0 d 40 40 m 80 40 l 80 80 l 40 80 l h S");
    compare("zero-square-seam","1 0 0 RG 4 w 2 J [0 40] 0 d 40 40 m 80 40 l 80 80 l 40 80 l h S");
    compare("zero-reset","1 0 0 RG 4 w 1 J [0 16] 5 d 20 40 m 100 40 l 20 70 m 100 70 l S");
    compare("zero-affine","1 0 0 RG 4 w 2 J [0 16] 0 d -1 .2 .3 .8 95 5 cm 20 20 m 75 20 l 75 90 l S");
    compare("zero-clip-restore","q 30 20 40 60 re W n 1 0 0 RG 6 w 1 J [0 16] 0 d 20 40 m 100 40 l S Q 0 0 1 rg 10 10 90 10 re f");
    compare("zero-nested-evenodd","q 10 10 100 100 re W n q 20 20 80 80 re 40 40 40 40 re W* n 1 0 0 RG 4 w 1 J [0 16] 0 d 10 60 m 110 60 l S Q Q");
    compare("zero-state-restore","1 0 0 RG 4 w 1 J [0 16] 5 d q [8 0] 0 d 20 40 m 100 40 l S Q 20 70 m 100 70 l S");
    compare("zero-fit-144","1 0 0 RG 4 w 1 J [0 16] 0 d 20 40 m 100 40 l S",{},2);
    negative("4 w 1 J [0 .0001] 0 d 20 20 m 90 20 l S");
    negative("4 w [0 .0001] 0 d 20 20 m 90 20 l S");
    negative("4 w 1 J [0 16] 0 d 20 20 m 90 20 l 20 20 l S");
    test_zero_phase_equivalence();
}

}
int main(int argc,char **argv)
{
#ifdef _WIN32
    const auto pid=_getpid();
#else
    const auto pid=getpid();
#endif
    source_prefix=std::string(STROKE_SOURCE_PDF)+"."+std::to_string(pid);
    if(argc==2 && std::strcmp(argv[1],"--round")==0) {
        source_prefix+=".round"; test_round_source(); return EXIT_SUCCESS;
    }
    if(argc==2 && std::strcmp(argv[1],"--zero-dash")==0) {
        source_prefix+=".zero-dash"; test_zero_dash_source(); return EXIT_SUCCESS;
    }
    CHECK(argc==1);
    compare("butt-miter-open","1 0 0 RG 8 w 0 J 0 j 10 M 40 40 m 80 40 l 80 80 l S",
        {{82,37,255,0,0},{37,40,255,255,255},{60,60,255,255,255},{60,40,255,0,0}});
    compare("square-bevel","1 0 0 RG 8 w 2 J 2 j 40 40 m 80 40 l 80 80 l S",
        {{82,37,255,255,255},{37,40,255,0,0},{80,82,255,0,0}});
    compare("miter-limit-bevel","1 0 0 RG 8 w 0 J 0 j 1 M 40 40 m 80 40 l 80 80 l S",
        {{82,37,255,255,255},{60,40,255,0,0}});
    compare("closed","1 0 0 RG 4 w 40 40 m 80 40 l 80 80 l h S",
        {{60,60,255,0,0},{70,50,255,255,255}});
    compare("multiple-overlapping-subpaths","0 0 1 RG 7 w 2 J 2 j 20 30 m 90 75 l 20 75 m 90 30 l 20 30 m 90 75 l S");
    compare("nonuniform-scale","1 0 0 RG 5 w 2 J 0 j 1.5 0 0 .6 0 10 cm 20 40 m 60 40 l 60 90 l S");
    compare("shear","1 0 0 RG 5 w 2 J 0 j 1 .2 .4 1 0 0 cm 20 20 m 60 20 l 60 70 l S");
    compare("reflection","1 0 0 RG 5 w 2 J 0 j -1 0 .2 1 100 0 cm 20 20 m 60 20 l 60 70 l S");
    compare("rotation","1 0 0 RG 5 w 2 J 0 j .8 .6 -.6 .8 55 0 cm 20 20 m 60 20 l 60 70 l S");
    compare("acute-miter","1 0 0 RG 4 w 0 J 0 j 10 M 20 20 m 90 20 l 25 40 l S");
    compare("clip-and-restore","q 30 20 40 60 re W n 1 0 0 RG 8 w 20 40 m 90 40 l 90 70 l S Q 0 0 1 RG 4 w 10 10 m 100 10 l S",
        {{40,40,255,0,0},{20,40,255,255,255},{90,40,255,255,255},{20,10,0,0,255}});
    compare("nested-evenodd-clip","q 10 10 100 100 re W n q 20 20 80 80 re 40 40 40 40 re W* n 1 0 0 RG 6 w 10 60 m 110 60 l S Q 0 0 1 RG 4 w 10 30 m 110 30 l S Q",
        {{30,60,255,0,0},{60,60,255,255,255},{60,30,0,0,255}});
    compare("dash-butt-phase","1 0 0 RG 4 w [8 6] 3 d 20 40 m 100 40 l S",
        {{21,40,255,0,0},{27,40,255,255,255},{33,40,255,0,0}});
    compare("dash-square","1 0 0 RG 4 w 2 J [8 8] 0 d 20 40 m 100 40 l S",
        {{19,40,255,0,0},{29,40,255,0,0},{32,40,255,255,255}});
    compare("dash-odd","0 0 1 RG 4 w [8 4 4] 0 d 20 40 m 100 40 l S");
    compare("dash-large-phase","1 0 0 RG 4 w [8 6] 14003 d 20 40 m 100 40 l S");
    compare("dash-move-reset","1 0 0 RG 4 w [8 6] 3 d 20 40 m 47 40 l 20 70 m 100 70 l S");
    compare("dash-cross-join","1 0 0 RG 8 w [60 20] 0 d 40 40 m 80 40 l 80 90 l S",
        {{82,37,255,0,0},{80,50,255,0,0},{80,70,255,255,255}});
    compare("dash-vertex-boundary","1 0 0 RG 8 w [40 20] 0 d 40 40 m 80 40 l 80 90 l S",
        {{82,37,255,255,255},{80,50,255,255,255},{80,70,255,0,0}});
    compare("dash-closed-seam","1 0 0 RG 8 w [50 20] 0 d 40 40 m 80 40 l 80 80 l 40 80 l h S",
        {{37,37,255,255,255}});
    compare("dash-exact-closed-boundary","1 0 0 RG 8 w [160 20] 0 d 40 40 m 80 40 l 80 80 l 40 80 l h S",
        {{37,37,255,255,255}});
    compare("dash-affine","1 0 0 RG 4 w 2 J [9 7] 3 d -1 .2 .3 .8 95 5 cm 20 20 m 75 20 l 75 90 l S");
    compare("dash-clip-restore","q 30 20 40 60 re W n 1 0 0 RG 6 w [8 6] 2 d 20 40 m 90 40 l S Q 0 0 1 RG 4 w 10 10 m 100 10 l S");
    compare("dash-all-off","4 w [1 200] 2 d 20 40 m 100 40 l S",
        {{20,40,255,255,255},{60,40,255,255,255}});
    compare("dash-phase-starts-off","1 0 0 RG 4 w [3 5] 6 d 20 40 m 100 40 l S",
        {{20,40,255,255,255},{22,40,255,0,0},{26,40,255,255,255}});
    compare("dash-single-odd","1 0 0 RG 4 w [8] 3 d 20 40 m 100 40 l S");
    compare("dash-square-closed","1 0 0 RG 8 w 2 J [50 20] 0 d 40 40 m 80 40 l 80 80 l 40 80 l h S",
        {{37,37,255,0,0}});
    compare("dash-dense-mixed-budget","1 0 0 RG 2 w [1 1] 0 d 10 40 m 110 40 l S 0 0 1 rg 10 10 90 10 re f",
        {{20,15,0,0,255}});
    compare("dash-state-restore","1 0 0 RG 4 w [8 6] 3 d q [3] 0 d 20 40 m 100 40 l S Q 20 70 m 100 70 l S");
    compare("dash-bevel-join","1 0 0 RG 8 w 2 j [60 20] 0 d 40 40 m 80 40 l 80 90 l S",
        {{82,37,255,255,255},{80,50,255,0,0}});
    negative("1 0 0 RG 4 w 20 20 m 90 20 l S 0 w 20 40 m 90 40 l S");
    negative("4 w 20 20 m 90 20 l h S");
    negative("4 w 1 j 20 20 m 90 20 l 20 20 l S");
    negative("4 w [0 .0001] 0 d 20 20 m 90 20 l S");
    negative("4 w 20 20 m 40 50 60 50 90 20 c S");
    return EXIT_SUCCESS;
}
