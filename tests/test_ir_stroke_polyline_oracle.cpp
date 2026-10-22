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

#define CHECK(e) do { if (!(e)) { \
    std::fprintf(stderr, "%s:%d: source q/Q oracle: %s\n", \
                 __FILE__,__LINE__,#e); std::exit(EXIT_FAILURE); \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

namespace {
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
void make_source(const char *content)
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
    FILE *f = std::fopen(STROKE_SOURCE_PDF,"wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),f) ==
          blob->getSize());
    CHECK(std::fclose(f) == 0);
}

quantapdf_status snapshot_lower(quantapdf_ir *out,
                                size_t page_index=0u,size_t max_streams=3u,
                                size_t max_parent_steps=2u,
                                const char *source_path=STROKE_SOURCE_PDF)
{
    quantapdf_document *doc=nullptr;
    quantapdf_graph_snapshot *snap=nullptr;
    quantapdf_status st;
    CHECK(out != nullptr);
    std::memset(out,0,sizeof(*out));
    st = quantapdf_open(source_path,nullptr,&doc);
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
rgb_image render_direct_pdfium_reference(const char *source_path)
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
    out.width = static_cast<int>(std::ceil(pw));
    out.height = static_cast<int>(std::ceil(ph));
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
             const std::vector<probe>& probes={})
{
    make_source(content);
    quantapdf_ir ir{}; OK(snapshot_lower(&ir));
    CHECK(ir.operation_count>0u && ir.operation_count<=4u);
    quantapdf_ir_raster_image native{},repeat{};
    OK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&raster_limits,&native));
    OK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&raster_limits,&repeat));
    CHECK(native.width==PAGE && native.height==PAGE && native.size==PAGE*PAGE*4u);
    CHECK(native.size==repeat.size && std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&ir); /* Pixels survive all source/Graph/IR ownership. */
    const auto reference=render_direct_pdfium_reference(STROKE_SOURCE_PDF);
    double error=0; size_t gross=0;
    for(size_t y=0;y<PAGE;++y) for(size_t x=0;x<PAGE;++x) {
        const auto *a=native.pixels+y*native.stride+x*4u;
        const auto *b=reference.at(x,y); unsigned largest=0;
        for(size_t c=0;c<3;++c) {
            const unsigned delta=static_cast<unsigned>(std::abs(static_cast<int>(on_white(a,c))-static_cast<int>(b[c])));
            error+=delta; largest=std::max(largest,delta);
        }
        if(largest>64u) ++gross;
    }
    const double mean=error/static_cast<double>(PAGE*PAGE*3u);
    std::fprintf(stderr,"stroke original oracle %s: mean=%.4f gross=%zu\n",name,mean,gross);
    /* Different edge antialiasing is expected. Large geometric differences
     * cannot hide behind a mean: separately cap >64 errors and probe solids. */
    CHECK(mean<2.0 && gross<PAGE*PAGE/100u);
    for(const auto& p:probes) {
        size_t y=PAGE-1u-static_cast<size_t>(p.y_pdf), x=static_cast<size_t>(p.x);
        const auto *a=native.pixels+y*native.stride+x*4u;
        const auto *b=reference.at(x,y); const unsigned expected[]={p.r,p.g,p.b};
        for(size_t c=0;c<3;++c) {
            CHECK(std::abs(static_cast<int>(on_white(a,c))-static_cast<int>(expected[c]))<=3);
            if(std::abs(static_cast<int>(b[c])-static_cast<int>(expected[c]))>3) std::fprintf(stderr,"probe %s (%d,%d) native=%u reference=%u expected=%u channel=%zu\n",name,p.x,p.y_pdf,on_white(a,c),b[c],expected[c],c);
            CHECK(std::abs(static_cast<int>(b[c])-static_cast<int>(expected[c]))<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native); CHECK(std::remove(STROKE_SOURCE_PDF)==0);
}
void negative(const char *content)
{
    make_source(content); quantapdf_ir ir{}; OK(snapshot_lower(&ir));
    quantapdf_ir_raster_image image{};
    CHECK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&raster_limits,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size && !image.width && !image.height && !image.stride);
    quantapdf_ir_dispose(&ir); CHECK(std::remove(STROKE_SOURCE_PDF)==0);
}
}
int main()
{
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
    negative("4 w 1 J 20 20 m 90 20 l S");
    negative("4 w 1 j 20 20 m 90 20 l 90 90 l S");
    negative("4 w [0 3] 0 d 20 20 m 90 20 l S");
    negative("4 w 20 20 m 40 50 60 50 90 20 c S");
    return EXIT_SUCCESS;
}
