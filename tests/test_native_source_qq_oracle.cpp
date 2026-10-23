/* Test-only source-PDF q/Q conformance. PDFium is an independent oracle:
 * Original source PDF -> PDFium versus Source -> detached Graph -> owned IR
 * -> private pure-C11 scoped Clip DAG raster. Never export IR to PDF as a
 * production rendering intermediate and never invoke a PDFium fallback. */
#include "document/page_composer_ir.h"
#include "render/ir_scoped_operations_v1.h"
#include "render/ir_clip_dag_v3.h"
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
#include <filesystem>
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
    PAGE * PAGE, 3u, 768u, 60000000u
};
const quantapdf_graph_limits graph_limits = {
    10000u, 30000u, 1048576u, 1048576u, 4194304u, 256u
};
const quantapdf_graph_stream_limits decode_limits = {8192u,128u,100000u};
const quantapdf_pdf_content_limits parse_limits = {8192u,256u,16u};
const quantapdf_pdf_path_lower_limits lower_limits = {3u,256u};

/* Exactly one call per generated source-PDF fixture, including rejected
 * fixtures. The manifest runner checks actual work against declared counts. */
size_t corpus_fixture_executions = 0u;
void track_corpus_fixture()
{
    CHECK(corpus_fixture_executions < 256u);
    ++corpus_fixture_executions;
}

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
    page.replaceKey("/Contents", pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    auto blob = writer.getBufferSharedPointer();
    FILE *f = std::fopen(SOURCE_QQ_PDF,"wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),f) ==
          blob->getSize());
    CHECK(std::fclose(f) == 0);
}

quantapdf_status snapshot_lower(quantapdf_ir *out,
                                size_t page_index=0u,size_t max_streams=1u,
                                size_t max_parent_steps=2u,
                                const char *source_path=SOURCE_QQ_PDF,
                                const quantapdf_pdf_path_lower_limits *path_limits=nullptr)
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
        snap,page_index,max_parent_steps,&decode_limits,&parse_limits,max_streams,
        path_limits != nullptr ? path_limits : &lower_limits,out);
    /* Deliberately sever all source-page ownership before rasterization. */
    quantapdf_graph_snapshot_drop(snap);
    return st;
}

rgb_image render_original_pdfium(size_t page_index=0u,
                                const char *source_path=SOURCE_QQ_PDF)
{
    rgb_image out;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    const unsigned char *bytes=nullptr;
    size_t size=0u;
    OK(quantapdf_open(source_path,nullptr,&doc));
    OK(quantapdf_load_page(doc,page_index,&page));
    OK(quantapdf_render_page(page,&bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap,&out.width,&out.height,
                                   &out.stride,&out.components));
    OK(quantapdf_bitmap_data(bitmap,&bytes,&size));
    CHECK(out.width>0 && out.height>0 &&
          out.components==3 && out.stride >= out.width*3 &&
          size >= (size_t)out.height * (size_t)out.stride);
    out.pixels.assign(bytes, bytes + (size_t)out.height*(size_t)out.stride);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
    return out;
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

void compare(const char *content, const probe *goldens,
             size_t golden_count, size_t min_clip_resources)
{
    track_corpus_fixture();
    quantapdf_ir ir{};
    quantapdf_ir_raster_image native{}, again{};
    size_t gross = 0u;
    double error=0.0;
    make_source(content);
    OK(snapshot_lower(&ir));
    CHECK(ir.page_count==1u && ir.operation_count==3u);
    CHECK(ir.pages[0].width_points==(float)PAGE &&
          ir.pages[0].height_points==(float)PAGE);
    CHECK(ir.pages[0].suppress_background == 1);
    CHECK(ir.resources.clip_count >= min_clip_resources);
    /* No more than 4 normal-opacity operations or 8 DAG nodes admitted. */
    CHECK(ir.resources.clip_count<=8u &&
          ir.resources.graphics_state_count<=4u);
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &ir,0u,&raster_limits,&native));
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &ir,0u,&raster_limits,&again));
    CHECK(native.width==PAGE && native.height==PAGE &&
          native.stride==PAGE*4u && native.size==PAGE*PAGE*4u);
    CHECK(again.size==native.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_dispose(&ir);
    rgb_image reference=render_original_pdfium();

    for(size_t y=0u;y<PAGE;++y) for(size_t x=0u;x<PAGE;++x) {
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=reference.at(x,y);
        unsigned largest=0u;
        for(size_t c=0u;c<3u;++c) {
            unsigned actual=on_white(a,c), desired=b[c];
            unsigned delta=actual>desired ? actual-desired : desired-actual;
            if(delta>largest)largest=delta;
            error+=(double)delta;
        }
        if(largest>64u)++gross;
    }
    double mean=error/(double)(PAGE*PAGE*3u);
    if(mean>10.0 || gross>PAGE*PAGE/12u) {
        std::fprintf(stderr,
            "source PDF q/Q native/PDFium mismatch mean=%.3f gross=%zu\n",
            mean,gross);
        for(const auto& p: {probe{5,5,0,0,0}, probe{30,30,0,0,0},
                            probe{80,80,0,0,0}, probe{105,105,0,0,0}}) {
            size_t y=PAGE-1u-(size_t)p.y_pdf;
            const unsigned char *n=native.pixels+y*native.stride+4u*(size_t)p.x;
            const unsigned char *o=reference.at((size_t)p.x,y);
            std::fprintf(stderr,"zero-clip probe x=%d y=%d native=%u,%u,%u pdfium=%u,%u,%u\n",
                p.x,p.y_pdf,on_white(n,0),on_white(n,1),on_white(n,2),
                (unsigned)o[0],(unsigned)o[1],(unsigned)o[2]);
        }
        CHECK(false);
    }
    for(size_t i=0u;i<golden_count;++i) {
        const auto& p=goldens[i];
        CHECK(p.x>=0 && p.y_pdf>=0 && p.x<(int)PAGE &&
              p.y_pdf<(int)PAGE);
        size_t yy=PAGE-1u-(size_t)p.y_pdf;
        const unsigned char *a =
            native.pixels+yy*native.stride+(size_t)p.x*4u;
        const unsigned char *b=reference.at((size_t)p.x,yy);
        const unsigned expected[3]={p.r,p.g,p.b};
        for(size_t c=0u;c<3u;++c) {
            unsigned actual=on_white(a,c);
            if(std::abs((int)actual-(int)expected[c])>3 ||
               std::abs((int)b[c]-(int)expected[c])>3)
                std::fprintf(stderr,
                    "golden failure point=%zu x=%d y=%d channel=%zu native=%u pdfium=%u expected=%u\n",
                    i,p.x,p.y_pdf,c,actual,(unsigned)b[c],expected[c]);
            CHECK(std::abs((int)actual-(int)expected[c])<=3);
            CHECK(std::abs((int)b[c]-(int)expected[c])<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native);
    CHECK(std::remove(SOURCE_QQ_PDF)==0);
}

void test_nested_restore()
{
    /* Red uses inner+outer intersection; blue draws after Q and is clipped
     * only to the OUTER scope; green draws after outer Q unrestricted. */
    const char *content =
        "q 10 10 80 80 re W n "
        "q 20 20 40 40 re W n "
        "1 0 0 rg 0 0 120 120 re f "
        "Q 0 0 1 rg 50 0 70 120 re f "
        "Q 0 1 0 rg 0 0 12 12 re f";
    const probe points[]={
        {30,30,255,0,0}, {75,35,0,0,255},
        {5,5,0,255,0}, {5,110,255,255,255},
        {95,35,255,255,255}, {15,35,255,255,255}
    };
    compare(content,points,sizeof(points)/sizeof(points[0]),3u);
}


/* Real PDF with two nonoverlapping clips: the nested intersection is empty.
 * Neither the red full-page fill nor an earlier clip may leak through.
 * Popping the inner state restores the outer clip for blue; popping that
 * state restores the unclipped page for green. */
void test_disjoint_clip_restoration()
{
    const char *content =
        "q 10 10 60 60 re W n "
        "q 90 90 20 20 re W n "
        "1 0 0 rg 0 0 120 120 re f "
        "Q 0 0 1 rg 20 20 40 40 re f "
        "Q 0 1 0 rg 100 100 15 15 re f";
    const probe points[]={
        {30,30,0,0,255}, {55,55,0,0,255},
        {100,100,0,255,0}, {110,110,0,255,0},
        {80,80,255,255,255}, {5,5,255,255,255},
        {15,65,255,255,255}, {95,95,255,255,255}
    };
    compare(content,points,sizeof(points)/sizeof(points[0]),3u);
}


/* A duplicated closed contour has zero even-odd interior everywhere.
 * Restore the outer rectangular clip before blue paint and then restore
 * the page before green paint. PDFium compares the original PDF, not IR. */
void test_evenodd_cancelled_clip_restore()
{
    const char *content =
        "q 10 10 80 80 re W n "
        "q 20 20 40 40 re 20 20 40 40 re W* n "
        "1 0 0 rg 0 0 120 120 re f "
        "Q 0 0 1 rg 25 25 35 35 re f "
        "Q 0 1 0 rg 100 100 15 15 re f";
    const probe points[]={
        {30,30,0,0,255}, {50,50,0,0,255},
        {100,100,0,255,0}, {110,110,0,255,0},
        {15,15,255,255,255}, {95,95,255,255,255},
        {70,70,255,255,255}, {5,5,255,255,255}
    };
    compare(content,points,sizeof(points)/sizeof(points[0]),3u);
}

void test_evenodd_restore()
{
    /* W* hole under inner scope, then a blue paint restored to outer W.
     * The hole must be white, not leak into the later blue operation. */
    const char *content =
        "q 0 0 100 100 re W n "
        "q 20 20 60 60 re 35 35 20 20 re W* n "
        "1 0 0 rg 0 0 120 120 re f "
        "Q 0 0 1 rg 75 10 40 40 re f "
        "Q 0 1 0 rg 0 0 12 12 re f";
    const probe points[]={
        {25,70,255,0,0}, {40,40,255,255,255},
        {85,35,0,0,255}, {5,5,0,255,0},
        {110,110,255,255,255}, {15,70,255,255,255}
    };
    compare(content,points,sizeof(points)/sizeof(points[0]),3u);
}

void test_clip_definition_ctm()
{
    /* Clip geometry freezes the +10 CTM on W n; subsequent -10 cm cancels
     * the paint transform without moving the already defined Clip.
     * Outer Q then removes the entire Clip before painting blue/green. */
    const char *content =
        "q 1 0 0 1 10 0 cm 0 0 60 60 re W n "
        "1 0 0 1 -10 0 cm 1 0 0 rg 0 0 120 120 re f "
        "Q 0 0 1 rg 0 60 100 20 re f "
        "0 1 0 rg 0 0 10 10 re f";
    const probe points[]={
        {20,20,255,0,0}, {5,20,255,255,255},
        {30,70,0,0,255}, {5,5,0,255,0},
        {110,110,255,255,255}
    };
    compare(content,points,sizeof(points)/sizeof(points[0]),1u);
}

/* Source page geometry is specified independently of the mapped IR:
 * nonzero inherited MediaBox, inherited or leaf CropBox and /Rotate,
 * plus a leaf /UserUnit. Comparing against the ORIGINAL PDFium pixels is
 * essential: serializing the IR again would not test this chain. */
void make_geometry_source(int rotate, int user_unit,
                          bool inherited_crop, bool inherited_rotation)
{
    CHECK(rotate==0 || rotate==90 || rotate==180 || rotate==270);
    CHECK(user_unit==1 || user_unit==2);
    static const char *content =
        "q 50 45 75 50 re W n "
        "q 65 55 45 30 re W n "
        "1 0 0 rg 40 40 100 60 re f "
        "Q 0 0 1 rg 95 40 45 60 re f "
        "Q 0 1 0 rg 42 42 12 12 re f";
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto leaf=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    root.replaceKey("/Kids",QPDFObjectHandle::newArray({leaf}));
    root.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
    root.replaceKey("/MediaBox",
                    QPDFObjectHandle::parse("[20 30 220 150]"));
    auto crop=QPDFObjectHandle::parse("[40 40 140 100]");
    if(inherited_crop)root.replaceKey("/CropBox",crop);
    else leaf.replaceKey("/CropBox",crop);
    if(inherited_rotation)
        root.replaceKey("/Rotate",QPDFObjectHandle::newInteger(rotate));
    else
        leaf.replaceKey("/Rotate",QPDFObjectHandle::newInteger(rotate));
    leaf.replaceKey("/UserUnit",QPDFObjectHandle::newInteger(user_unit));
    leaf.replaceKey("/Parent",root);
    leaf.replaceKey("/Contents",pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    auto blob=writer.getBufferSharedPointer();
    FILE *f=std::fopen(SOURCE_QQ_PDF,"wb");
    CHECK(f!=nullptr);
    CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),f)==
          blob->getSize() && std::fclose(f)==0);
}

struct source_point { int x, y; unsigned r,g,b; };

void test_mapped_geometry(int rotate,int unit,
                          bool inherited_crop,bool inherited_rotation)
{
    track_corpus_fixture();
    quantapdf_ir ir{};
    quantapdf_ir_raster_image native{},again{};
    make_geometry_source(rotate,unit,inherited_crop,inherited_rotation);
    OK(snapshot_lower(&ir)); /* releases source document and Graph Snapshot */
    const size_t width=(size_t)(unit * ((rotate%180)==0 ? 100:60));
    const size_t height=(size_t)(unit * ((rotate%180)==0 ? 60:100));
    CHECK(ir.page_count==1u && ir.operation_count==3u &&
          ir.pages[0].suppress_background==1 &&
          ir.pages[0].width_points==(float)width &&
          ir.pages[0].height_points==(float)height &&
          ir.resources.clip_count>=3u && ir.resources.clip_count<=8u);
    quantapdf_ir_raster_v1_limits budget={
        width*height,3u,768u,70000000u
    };
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &ir,0u,&budget,&native));
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &ir,0u,&budget,&again));
    CHECK(native.width==width && native.height==height &&
          native.stride==4u*width && native.size==4u*width*height);
    CHECK(native.size==again.size &&
          std::memcmp(native.pixels,again.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_dispose(&ir);
    rgb_image reference=render_original_pdfium();
    CHECK(reference.width==(int)width && reference.height==(int)height);

    /* Compare every pixel, with unchanged strict AA error thresholds. */
    double aggregate=0.0;
    size_t gross=0u;
    for(size_t y=0;y<height;++y)for(size_t x=0;x<width;++x){
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=reference.at(x,y);
        unsigned worst=0u;
        for(size_t channel=0;channel<3u;++channel){
            unsigned p=on_white(a,channel),q=b[channel];
            unsigned diff=p>q?p-q:q-p;
            if(diff>worst)worst=diff;
            aggregate+=(double)diff;
        }
        if(worst>64u)++gross;
    }
    const double mean=aggregate/(double)(width*height*3u);
    if(mean>10.0 || gross>width*height/12u){
        std::fprintf(stderr,
            "source geometry rotate=%d unit=%d crop_root=%d rot_root=%d mean=%.4f gross=%zu\n",
            rotate,unit,(int)inherited_crop,(int)inherited_rotation,mean,gross);
        CHECK(false);
    }

    /* Independently calculate PDF source-space point → expected display pixel,
     * rather than trusting Graph's own affine map when selecting goldens. */
    const source_point goldens[]={
        {80,70,255,0,0}, {115,70,0,0,255},
        {44,44,0,255,0}, {130,70,255,255,255},
        {48,88,255,255,255}, {67,65,255,0,0}
    };
    for(const auto& p:goldens){
        int px=0,py=0;
        switch(rotate){
        case 0:   px=p.x-40; py=p.y-40; break;
        case 90:  px=p.y-40; py=140-p.x; break;
        case 180: px=140-p.x; py=100-p.y; break;
        case 270: px=100-p.y; py=p.x-40; break;
        default: CHECK(false);
        }
        px*=unit;
        py=(int)height-1-py*unit;
        CHECK(px>=0 && py>=0 && (size_t)px<width && (size_t)py<height);
        const unsigned char *a=native.pixels+
            (size_t)py*native.stride+(size_t)px*4u;
        const unsigned char *b=reference.at((size_t)px,(size_t)py);
        const unsigned want[3]={p.r,p.g,p.b};
        for(size_t channel=0;channel<3u;++channel){
            CHECK(std::abs((int)on_white(a,channel)-(int)want[channel])<=3);
            CHECK(std::abs((int)b[channel]-(int)want[channel])<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native);
    CHECK(std::remove(SOURCE_QQ_PDF)==0);
}

void test_page_geometry_matrix()
{
    /* Include all four quarter-turns, inherited and leaf boxes/rotation,
     * and high-dpi page UserUnit without changing paint or clip semantics. */
    struct test_case { int rotation,unit; bool root_crop,root_rotation; };
    const test_case cases[]={
        {0,1,true,true}, {90,1,false,true},
        {180,1,true,false}, {270,1,false,false},
        {0,2,false,true}, {90,2,true,false},
        {180,2,true,true}, {270,2,false,false}
    };
    for(const auto& t:cases)
        test_mapped_geometry(
            t.rotation,t.unit,t.root_crop,t.root_rotation);
}

/* Two independently encoded /Contents streams make one operator tape.
 * Deliberately put the terminal f and both Q restores in the second stream.
 * Another page draws a completely different color; selected-page lowering
 * must not inherit the sibling page's graphics state or paint. */
void make_multipage_multistream_source()
{
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page0=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    auto page1=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    root.replaceKey("/Kids",QPDFObjectHandle::newArray({page0,page1}));
    root.replaceKey("/Count",QPDFObjectHandle::newInteger(2));
    root.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 120 120]"));
    page0.replaceKey("/Parent",root);
    page1.replaceKey("/Parent",root);
    page0.replaceKey("/Contents",pdf->newStream(
        "q 0 0 1 rg 0 0 120 120 re f Q"));
    auto part_a=pdf->newStream(
        "q 10 10 80 80 re W n "
        "q 20 20 40 40 re W n "
        "1 0 0 rg 0 0 120 120 re ");
    auto part_b=pdf->newStream(
        "f Q 0 0 1 rg 50 0 70 120 re f "
        "Q 0 1 0 rg 0 0 12 12 re f");
    page1.replaceKey("/Contents",QPDFObjectHandle::newArray({
        part_a,part_b
    }));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    auto blob=writer.getBufferSharedPointer();
    FILE *file=std::fopen(SOURCE_QQ_PDF,"wb");
    CHECK(file!=nullptr);
    CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),file)==
          blob->getSize() && std::fclose(file)==0);
}

void test_selected_page_multistream()
{
    track_corpus_fixture();
    make_multipage_multistream_source();
    quantapdf_ir rejected{};
    CHECK(snapshot_lower(&rejected,1u,1u)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(rejected.pages==nullptr && rejected.operations==nullptr &&
          rejected.operation_count==0u && rejected.resources.clip_count==0u);

    /* The other page is independent and contains only one blue paint. */
    quantapdf_ir first{};
    OK(snapshot_lower(&first,0u,1u));
    CHECK(first.page_count==1u && first.operation_count==1u &&
          first.resources.clip_count==0u);
    quantapdf_ir_dispose(&first);

    quantapdf_ir ir{};
    quantapdf_ir_raster_image pixels{},repeat{};
    OK(snapshot_lower(&ir,1u,2u));
    CHECK(ir.page_count==1u && ir.operation_count==3u &&
          ir.resources.clip_count>=3u &&
          ir.pages[0].width_points==(float)PAGE &&
          ir.pages[0].height_points==(float)PAGE &&
          ir.pages[0].suppress_background==1);
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &ir,0u,&raster_limits,&pixels));
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &ir,0u,&raster_limits,&repeat));
    CHECK(pixels.size==PAGE*PAGE*4u && repeat.size==pixels.size &&
          std::memcmp(pixels.pixels,repeat.pixels,pixels.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&ir);

    rgb_image baseline=render_original_pdfium(1u);
    CHECK(baseline.width==(int)PAGE && baseline.height==(int)PAGE);
    double total_error=0.0;
    size_t gross=0u;
    for(size_t y=0;y<PAGE;++y)for(size_t x=0;x<PAGE;++x){
        const unsigned char *a=pixels.pixels+y*pixels.stride+4u*x;
        const unsigned char *b=baseline.at(x,y);
        unsigned worst=0u;
        for(size_t channel=0u;channel<3u;++channel){
            unsigned lhs=on_white(a,channel),rhs=b[channel];
            unsigned delta=lhs>rhs?lhs-rhs:rhs-lhs;
            if(delta>worst)worst=delta;
            total_error+=(double)delta;
        }
        if(worst>64u)++gross;
    }
    const double mean=total_error/(double)(PAGE*PAGE*3u);
    if(mean>10.0 || gross>PAGE*PAGE/12u){
        std::fprintf(stderr,
            "multi-page two-stream native/PDFium mean=%.3f gross=%zu\n",
            mean,gross);
        CHECK(false);
    }

    const probe samples[]={
        {30,30,255,0,0}, {75,35,0,0,255},
        {5,5,0,255,0}, {5,110,255,255,255},
        {95,35,255,255,255}, {15,35,255,255,255}
    };
    for(const auto& p:samples){
        size_t yy=PAGE-1u-(size_t)p.y_pdf;
        const unsigned char *a=pixels.pixels+yy*pixels.stride+
                                4u*(size_t)p.x;
        const unsigned char *b=baseline.at((size_t)p.x,yy);
        const unsigned want[3]={p.r,p.g,p.b};
        for(size_t channel=0u;channel<3u;++channel){
            CHECK(std::abs((int)on_white(a,channel)-(int)want[channel])<=3);
            CHECK(std::abs((int)b[channel]-(int)want[channel])<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&pixels);
    CHECK(std::remove(SOURCE_QQ_PDF)==0);
}


/* Zero-segment W/W* must clip everything and restore on Q. */
void test_zero_segment_clip_native_restore()
{
    const char *cases[]={
        "q W n 1 0 0 rg 0 0 120 120 re f Q "
        "0 0 1 rg 20 20 40 40 re f "
        "0 1 0 rg 100 100 15 15 re f",
        "q W* n 1 0 0 rg 0 0 120 120 re f Q "
        "0 0 1 rg 20 20 40 40 re f "
        "0 1 0 rg 100 100 15 15 re f"
    };
    const probe points[]={
        {30,30,0,0,255}, {50,50,0,0,255},
        {105,105,0,255,0}, {5,5,255,0,0},
        {80,80,255,0,0}
    };
    for(const char *content:cases)
        compare(content,points,sizeof(points)/sizeof(points[0]),0u);
}

/* Negative original-source page-tree geometry: reject malformed inherited
 * numeric/box metadata before native pixels or partially owned IR escape. */
/* Three-level Page Tree: leaf values shadow malformed ancestor values,
 * while inherited MediaBox remains resolved from the root Pages.
 * Strict Graph lowering must prefer the nearer wider CropBox. */
void test_multilevel_page_tree_override()
{
    track_corpus_fixture();
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto middle=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Pages >>"));
    auto leaf=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    root.replaceKey("/Kids",QPDFObjectHandle::newArray({middle}));
    root.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
    root.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 120 120]"));
    root.replaceKey("/CropBox",QPDFObjectHandle::parse("[0 0 60 60]"));
    middle.replaceKey("/Parent",root);
    middle.replaceKey("/Kids",QPDFObjectHandle::newArray({leaf}));
    middle.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
    middle.replaceKey("/CropBox",QPDFObjectHandle::parse("[0 0 120 120]"));
    leaf.replaceKey("/Parent",middle);
    leaf.replaceKey("/Contents",pdf->newStream(
        "q 10 10 80 80 re W n "
        "1 0 0 rg 0 0 120 120 re f "
        "Q 0 0 1 rg 0 0 12 12 re f "
        "0 1 0 rg 100 100 12 12 re f"));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    auto blob=writer.getBufferSharedPointer();
    FILE *file=std::fopen(SOURCE_QQ_PDF,"wb");
    CHECK(file!=nullptr);
    CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),file)==
          blob->getSize());
    CHECK(std::fclose(file)==0);
    quantapdf_ir ir{};
    OK(snapshot_lower(&ir,0u,1u,3u)); /* leaf, middle, root */
    CHECK(ir.page_count==1u && ir.operation_count==3u);
    CHECK(ir.pages[0].width_points==120.0f &&
          ir.pages[0].height_points==120.0f);
    quantapdf_ir_raster_image pixels{};
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &ir,0u,&raster_limits,&pixels));
    CHECK(pixels.width==120u && pixels.height==120u);
    quantapdf_ir_raster_image_dispose(&pixels);
    quantapdf_ir_dispose(&ir);
    CHECK(std::remove(SOURCE_QQ_PDF)==0);
}

void test_invalid_inherited_geometry()
{
    struct bad_case { const char *key; const char *value; bool leaf; };
    const bad_case cases[]={
        {"/MediaBox","[0 0 0 120]"},
        {"/MediaBox","[0 0 120 0]"},
        {"/MediaBox","[0 0 120]"},
        {"/CropBox","[25 25 25 90]"},
        {"/CropBox","[25 25 90 25]"},
        {"/Rotate","45"},
        {"/Rotate","/Invalid"},
        {"/CropBox","[-1 0 100 100]"},
        {"/CropBox","[0 0 121 120]"},
        {"/CropBox","[130 0 140 120]"},
        {"/UserUnit","0",true},
        {"/UserUnit","-1",true},
        {"/UserUnit","/Invalid",true}
    };
    for(const auto& t:cases) {
        track_corpus_fixture();
        auto pdf=QPDF::create();
        pdf->emptyPDF();
        auto root=pdf->getRoot().getKey("/Pages");
        auto page=pdf->makeIndirectObject(
            QPDFObjectHandle::parse("<< /Type /Page >>"));
        root.replaceKey("/Kids",QPDFObjectHandle::newArray({page}));
        root.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
        root.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 120 120]"));
        if(t.leaf)page.replaceKey(t.key,QPDFObjectHandle::parse(t.value));
        else root.replaceKey(t.key,QPDFObjectHandle::parse(t.value));
        page.replaceKey("/Parent",root);
        page.replaceKey("/Contents",pdf->newStream(
            "q 10 10 60 60 re W n 1 0 0 rg 0 0 120 120 re f Q"));
        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.write();
        auto blob=writer.getBufferSharedPointer();
        FILE *file=std::fopen(SOURCE_QQ_PDF,"wb");
        CHECK(file!=nullptr);
        CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),file)==
              blob->getSize());
        CHECK(std::fclose(file)==0);
        quantapdf_ir rejected{};
        quantapdf_status status=snapshot_lower(&rejected);
        CHECK(status!=QUANTAPDF_OK);
        CHECK(rejected.pages==nullptr && rejected.page_count==0u &&
              rejected.operations==nullptr && rejected.operation_count==0u &&
              rejected.resources.clip_count==0u);
        quantapdf_ir_dispose(&rejected);
        CHECK(std::remove(SOURCE_QQ_PDF)==0);
    }
}

/* Real source PDF: inherited /Resources resolves /ExtGState through the
 * page tree. Missing and unsupported states must never publish partial IR. */
/* Parent resource inheritance and leaf dictionary replacement use real
 * original PDF bytes, detached IR, and independent PDFium pixels. */
void test_extgstate_inheritance_and_override()
{
    const char *normal =
        "<< /ExtGState << /GS << /ca 1 /CA 1 /BM /Normal >> >> >>";
    const char *unsupported =
        "<< /ExtGState << /GS << /SMask /None >> >> >>";
    for(int leaf_override=0;leaf_override!=2;++leaf_override) {
        track_corpus_fixture();
        auto pdf=QPDF::create();
        pdf->emptyPDF();
        auto root=pdf->getRoot().getKey("/Pages");
        auto page=pdf->makeIndirectObject(
            QPDFObjectHandle::parse("<< /Type /Page >>"));
        root.replaceKey("/Kids",QPDFObjectHandle::newArray({page}));
        root.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
        root.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 120 120]"));
        root.replaceKey("/Resources",QPDFObjectHandle::parse(
            leaf_override ? unsupported : normal));
        page.replaceKey("/Parent",root);
        if(leaf_override)
            page.replaceKey("/Resources",QPDFObjectHandle::parse(normal));
        page.replaceKey("/Contents",pdf->newStream(
            "q /GS gs 10 10 80 80 re W n "
            "1 0 0 rg 0 0 120 120 re f "
            "Q 0 0 1 rg 0 0 12 12 re f "
            "0 1 0 rg 100 100 12 12 re f"));
        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.write();
        auto blob=writer.getBufferSharedPointer();
        FILE *file=std::fopen(SOURCE_QQ_PDF,"wb");
        CHECK(file!=nullptr);
        CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),file)==blob->getSize());
        CHECK(std::fclose(file)==0);
        quantapdf_ir ir{};
        OK(snapshot_lower(&ir));
        CHECK(ir.page_count==1u && ir.operation_count==3u);
        quantapdf_ir_raster_image native{},again{};
        OK(quantapdf_ir_raster_scoped_operations_v1(&ir,0u,&raster_limits,&native));
        OK(quantapdf_ir_raster_scoped_operations_v1(&ir,0u,&raster_limits,&again));
        CHECK(native.size==again.size &&
              std::memcmp(native.pixels,again.pixels,native.size)==0);
        quantapdf_ir_raster_image_dispose(&again);
        quantapdf_ir_dispose(&ir);
        rgb_image original=render_original_pdfium();
        const probe goldens[]={
            {30,30,255,0,0}, {5,5,0,0,255},
            {105,105,0,255,0}, {95,35,255,255,255}
        };
        for(const auto& p:goldens){
            size_t y=PAGE-1u-(size_t)p.y_pdf;
            const unsigned char *a=native.pixels+y*native.stride+4u*(size_t)p.x;
            const unsigned char *b=original.at((size_t)p.x,y);
            const unsigned expected[]={p.r,p.g,p.b};
            for(size_t channel=0;channel<3u;++channel){
                CHECK(std::abs((int)on_white(a,channel)-(int)expected[channel])<=3);
                CHECK(std::abs((int)b[channel]-(int)expected[channel])<=3);
            }
        }
        quantapdf_ir_raster_image_dispose(&native);
        CHECK(std::remove(SOURCE_QQ_PDF)==0);
    }
}

/* Two real source pages own different indirect resource dictionaries.
 * Resolving page 0 must not leak page 1's unsupported ExtGState. */
void test_indirect_resource_page_isolation()
{
    track_corpus_fixture();
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto first=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    auto second=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    root.replaceKey("/Kids",QPDFObjectHandle::newArray({first,second}));
    root.replaceKey("/Count",QPDFObjectHandle::newInteger(2));
    root.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 120 120]"));
    auto valid=pdf->makeIndirectObject(QPDFObjectHandle::parse(
        "<< /ca 1 /CA 1 /BM /Normal >>"));
    auto invalid=pdf->makeIndirectObject(QPDFObjectHandle::parse(
        "<< /SMask /None >>"));
    auto resources0=pdf->makeIndirectObject(QPDFObjectHandle::newDictionary());
    auto resources1=pdf->makeIndirectObject(QPDFObjectHandle::newDictionary());
    auto gs0=pdf->makeIndirectObject(QPDFObjectHandle::newDictionary());
    auto gs1=pdf->makeIndirectObject(QPDFObjectHandle::newDictionary());
    gs0.replaceKey("/GS",valid);
    gs1.replaceKey("/GS",invalid);
    resources0.replaceKey("/ExtGState",gs0);
    resources1.replaceKey("/ExtGState",gs1);
    first.replaceKey("/Parent",root);
    second.replaceKey("/Parent",root);
    first.replaceKey("/Resources",resources0);
    second.replaceKey("/Resources",resources1);
    const char *content=
        "q /GS gs 10 10 80 80 re W n "
        "1 0 0 rg 0 0 120 120 re f Q "
        "0 0 1 rg 0 0 12 12 re f "
        "0 1 0 rg 100 100 12 12 re f";
    first.replaceKey("/Contents",pdf->newStream(content));
    second.replaceKey("/Contents",pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.write();
    auto blob=writer.getBufferSharedPointer();
    FILE *file=std::fopen(SOURCE_QQ_PDF,"wb");
    CHECK(file!=nullptr);
    CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),file)==blob->getSize());
    CHECK(std::fclose(file)==0);
    quantapdf_ir rejected{};
    CHECK(snapshot_lower(&rejected,1u)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(rejected.pages==nullptr && rejected.page_count==0u &&
          rejected.operations==nullptr && rejected.operation_count==0u &&
          rejected.resources.clip_count==0u &&
          rejected.resources.graphics_state_count==0u);
    quantapdf_ir_dispose(&rejected);

    /* Rendering the other page must not borrow or inherit page 1's /GS.
     * The source document and Graph are closed before native raster use. */
    quantapdf_ir accepted{};
    OK(snapshot_lower(&accepted,0u));
    CHECK(accepted.page_count==1u && accepted.operation_count==3u);
    CHECK(accepted.pages[0].width_points==(float)PAGE &&
          accepted.pages[0].height_points==(float)PAGE);
    quantapdf_ir_raster_image native{},repeat{};
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &accepted,0u,&raster_limits,&native));
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &accepted,0u,&raster_limits,&repeat));
    CHECK(native.width==PAGE && native.height==PAGE &&
          native.stride==PAGE*4u && native.size==PAGE*PAGE*4u);
    CHECK(repeat.size==native.size &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&accepted);

    /* Independently render the ORIGINAL page 0 through test-only PDFium.
     * Never serialize the native IR back to PDF as the reference. */
    rgb_image reference=render_original_pdfium(0u);
    CHECK(reference.width==(int)PAGE && reference.height==(int)PAGE);
    double error=0.0;
    size_t gross=0u;
    for(size_t y=0u;y<PAGE;++y)for(size_t x=0u;x<PAGE;++x){
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=reference.at(x,y);
        unsigned worst=0u;
        for(size_t channel=0u;channel<3u;++channel){
            unsigned actual=on_white(a,channel), desired=b[channel];
            unsigned delta=actual>desired?actual-desired:desired-actual;
            if(delta>worst)worst=delta;
            error+=(double)delta;
        }
        if(worst>64u)++gross;
    }
    const double mean=error/(double)(PAGE*PAGE*3u);
    if(mean>10.0 || gross>PAGE*PAGE/12u)
        std::fprintf(stderr,
            "indirect resource source/PDFium mean=%.3f gross=%zu\n",
            mean,gross);
    CHECK(mean<=10.0 && gross<=PAGE*PAGE/12u);

    /* Interior source-space goldens also protect restoration and colors. */
    const probe points[]={
        {30,30,255,0,0}, {5,5,0,0,255}, {105,105,0,255,0},
        {95,35,255,255,255}, {50,100,255,255,255}
    };
    for(const auto& p:points){
        size_t row=PAGE-1u-(size_t)p.y_pdf;
        const unsigned char *a=native.pixels+row*native.stride+
                               4u*(size_t)p.x;
        const unsigned char *b=reference.at((size_t)p.x,row);
        const unsigned expected[]={p.r,p.g,p.b};
        for(size_t channel=0u;channel<3u;++channel){
            CHECK(std::abs((int)on_white(a,channel)-
                           (int)expected[channel])<=3);
            CHECK(std::abs((int)b[channel]-
                           (int)expected[channel])<=3);
        }
    }
    quantapdf_ir_raster_image_dispose(&native);

    /* Re-evaluate the unsupported sibling AFTER successful native paint. */
    quantapdf_ir rejected_again{};
    CHECK(snapshot_lower(&rejected_again,1u)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(rejected_again.pages==nullptr &&
          rejected_again.operations==nullptr &&
          rejected_again.operation_count==0u &&
          rejected_again.resources.clip_count==0u);
    quantapdf_ir_dispose(&rejected_again);
    CHECK(std::remove(SOURCE_QQ_PDF)==0);
}

void test_inherited_extgstate_fail_closed()
{
    struct scenario { const char *resources; quantapdf_status expected; };
    const scenario cases[]={
        {"<< /ExtGState << /Other << /ca 1 >> >> >>", QUANTAPDF_ERROR_FORMAT},
        {"<< /ExtGState << /GS << /SMask /None >> >> >>",
         QUANTAPDF_ERROR_UNSUPPORTED}
    };
    for(const auto& tc:cases) {
        track_corpus_fixture();
        auto pdf=QPDF::create();
        pdf->emptyPDF();
        auto root=pdf->getRoot().getKey("/Pages");
        auto page=pdf->makeIndirectObject(
            QPDFObjectHandle::parse("<< /Type /Page >>"));
        root.replaceKey("/Kids",QPDFObjectHandle::newArray({page}));
        root.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
        root.replaceKey("/MediaBox",QPDFObjectHandle::parse("[0 0 120 120]"));
        root.replaceKey("/Resources",QPDFObjectHandle::parse(tc.resources));
        page.replaceKey("/Parent",root);
        page.replaceKey("/Contents",pdf->newStream(
            "q /GS gs 10 10 80 80 re W n "
            "1 0 0 rg 0 0 120 120 re f Q"));
        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.write();
        auto blob=writer.getBufferSharedPointer();
        FILE *file=std::fopen(SOURCE_QQ_PDF,"wb");
        CHECK(file!=nullptr);
        CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),file)==
              blob->getSize());
        CHECK(std::fclose(file)==0);
        quantapdf_ir ir{};
        CHECK(snapshot_lower(&ir)==tc.expected);
        CHECK(ir.pages==nullptr && ir.page_count==0u &&
              ir.operations==nullptr && ir.operation_count==0u &&
              ir.resources.clip_count==0u);
        quantapdf_ir_dispose(&ir);
        CHECK(std::remove(SOURCE_QQ_PDF)==0);
    }
}

void test_unsupported_operator_fails_closed()
{
    track_corpus_fixture();
    quantapdf_ir failed{};
    make_source("q BT ET Q");
    quantapdf_status st=snapshot_lower(&failed);
    CHECK(st==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(failed.pages==nullptr && failed.operations==nullptr &&
          failed.operation_count==0u && failed.resources.clip_count==0u);
    CHECK(std::remove(SOURCE_QQ_PDF)==0);
}


/* An independently authored, externally published CC0 PDF. This is not
 * generated by the QPDF fixture code, and is kept out of the 14/36 registry.
 * The public PDFium reference must render visible original-source content;
 * native Graph -> detached IR must reject the unsupported PDF text. */
int test_external_cc0_text_rejection(const char *filename,
                                     const char *report_path)
{
    CHECK(filename != nullptr && *filename != '\0');
    CHECK(report_path != nullptr && *report_path != '\0');
    const std::string pending = std::string(report_path) + ".tmp";
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
    errno=0;
    if(std::remove(pending.c_str())!=0) CHECK(errno==ENOENT);
    CHECK(corpus_fixture_executions == 0u);

    quantapdf_document *pdfium_doc=nullptr;
    quantapdf_page *pdfium_page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    const unsigned char *rgb=nullptr;
    size_t bitmap_size=0u;
    int width=0,height=0,stride=0,components=0;
    OK(quantapdf_open(filename,nullptr,&pdfium_doc));
    OK(quantapdf_load_page(pdfium_doc,0u,&pdfium_page));
    OK(quantapdf_render_page(pdfium_page,&bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap,
        &width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(bitmap,&rgb,&bitmap_size));
    CHECK(width>0 && width<=2000 && height>0 && height<=2000 &&
          components==3 && stride>=width*3 && rgb!=nullptr &&
          bitmap_size>=(size_t)stride*(size_t)height);
    bool visible_ink=false;
    for(int y=0;y<height;++y){
        for(int x=0;x<width;++x){
            const unsigned char *px=rgb+(size_t)y*(size_t)stride+3u*(size_t)x;
            if(px[0]<240u || px[1]<240u || px[2]<240u)
                visible_ink=true;
        }
    }
    CHECK(visible_ink); /* Original file is renderable, not an empty page. */
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(pdfium_page);
    quantapdf_close(pdfium_doc);

    quantapdf_document *source=nullptr;
    quantapdf_graph_snapshot *snapshot=nullptr;
    quantapdf_ir detached{};
    OK(quantapdf_open(filename,nullptr,&source));
    quantapdf_status status=quantapdf_document_capture_graph(
        source,&graph_limits,&snapshot);
    quantapdf_close(source);
    CHECK(status==QUANTAPDF_OK && snapshot!=nullptr);
    status=quantapdf_graph_snapshot_page_composer_ir(
        snapshot,0u,2u,&decode_limits,&parse_limits,1u,
        &lower_limits,&detached);
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(detached.pages==nullptr && detached.page_count==0u &&
          detached.operations==nullptr && detached.operation_count==0u &&
          detached.resources.clip_count==0u &&
          detached.resources.graphics_state_count==0u);
    quantapdf_ir_dispose(&detached);

    /* Successful status-only conformance MUST NOT increment native-render
     * supported document count or the generated PDF execution count. */
    CHECK(corpus_fixture_executions==0u);
    FILE *report=std::fopen(pending.c_str(),"wb");
    CHECK(report!=nullptr);
    std::fprintf(report,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"corpus_id\": \"external-source-pdf\",\n"
        "  \"document_id\": \"tinytoolkit.sample-1-page-v1\",\n"
        "  \"fixture_kind\": \"external\",\n"
        "  \"reference_oracle\": \"original-pdfium\",\n"
        "  \"pdfium_result\": \"rendered_nonblank\",\n"
        "  \"native_expected_outcome\": \"unsupported\",\n"
        "  \"native_observed_outcome\": \"unsupported\",\n"
        "  \"native_ir\": \"empty\",\n"
        "  \"status_conformance_passed_count\": 1,\n"
        "  \"native_supported_passed_count\": 0,\n"
        "  \"result\": \"passed\",\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n");
    CHECK(std::ferror(report)==0);
    CHECK(std::fclose(report)==0);
    CHECK(std::rename(pending.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[external-corpus] id=tinytoolkit.sample-1-page-v1 "
        "pdfium=rendered native=unsupported ir=empty "
        "status_conformance=passed native_supported=0\n");
    return EXIT_SUCCESS;
}


/* An independently published CC0 blank PDF, not one of our QPDF-generated
 * fixtures. Blank positive is intentionally a DIFFERENT capability from
 * opaque Path/Clip paint. Original PDFium pixels are a test-only oracle. */
int test_external_cc0_blank_native_parity(const char *filename,
                                          const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    const std::string pending=std::string(report_path)+".tmp";
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
    errno=0;
    if(std::remove(pending.c_str())!=0) CHECK(errno==ENOENT);
    CHECK(corpus_fixture_executions==0u);

    constexpr size_t WIDTH=612u, HEIGHT=792u;
    quantapdf_document *pdfium_doc=nullptr;
    quantapdf_page *pdfium_page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    const unsigned char *rgb=nullptr;
    size_t bitmap_size=0u;
    int width=0,height=0,stride=0,components=0;
    OK(quantapdf_open(filename,nullptr,&pdfium_doc));
    OK(quantapdf_load_page(pdfium_doc,0u,&pdfium_page));
    OK(quantapdf_render_page(pdfium_page,&bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap,
        &width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(bitmap,&rgb,&bitmap_size));
    CHECK(width==(int)WIDTH && height==(int)HEIGHT &&
          components==3 && stride>=width*3 &&
          bitmap_size>=(size_t)stride*HEIGHT && rgb!=nullptr);

    quantapdf_document *source=nullptr;
    quantapdf_graph_snapshot *snapshot=nullptr;
    quantapdf_ir detached{};
    OK(quantapdf_open(filename,nullptr,&source));
    quantapdf_status status=quantapdf_document_capture_graph(
        source,&graph_limits,&snapshot);
    quantapdf_close(source);
    CHECK(status==QUANTAPDF_OK && snapshot!=nullptr);
    status=quantapdf_graph_snapshot_page_composer_ir(
        snapshot,0u,2u,&decode_limits,&parse_limits,1u,
        &lower_limits,&detached);
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(status==QUANTAPDF_OK);
    CHECK(detached.pages && detached.page_count==1u &&
          detached.operation_count==0u && detached.operations==nullptr &&
          detached.pages[0].suppress_background==1 &&
          detached.pages[0].width_points==(float)WIDTH &&
          detached.pages[0].height_points==(float)HEIGHT);
    CHECK(detached.resources.clip_count==0u &&
          detached.resources.graphics_state_count==0u &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.soft_mask_count==0u &&
          detached.resources.paint_count==0u &&
          detached.resources.form_count==0u);

    const quantapdf_ir_raster_limits limits={WIDTH*HEIGHT,1u};
    quantapdf_ir_raster_image native{},repeat{};
    OK(quantapdf_ir_raster_render_v0(&detached,0u,&limits,&native));
    OK(quantapdf_ir_raster_render_v0(&detached,0u,&limits,&repeat));
    CHECK(native.width==WIDTH && native.height==HEIGHT &&
          native.stride==WIDTH*4u && native.size==WIDTH*HEIGHT*4u);
    CHECK(repeat.size==native.size &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&detached);

    size_t mismatch=0u;
    for(size_t y=0u;y<HEIGHT;++y)for(size_t x=0u;x<WIDTH;++x){
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=rgb+y*(size_t)stride+x*3u;
        CHECK(a[0]==0u && a[1]==0u && a[2]==0u && a[3]==0u);
        for(size_t c=0u;c<3u;++c)
            if(on_white(a,c)!=b[c])++mismatch;
    }
    CHECK(mismatch==0u);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(pdfium_page);
    quantapdf_close(pdfium_doc);
    CHECK(corpus_fixture_executions==0u);

    FILE *report=std::fopen(pending.c_str(),"wb");
    CHECK(report!=nullptr);
    std::fprintf(report,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"corpus_id\": \"external-source-pdf\",\n"
        "  \"document_id\": \"tinytoolkit.sample-blank-letter-v1\",\n"
        "  \"fixture_kind\": \"external\",\n"
        "  \"reference_oracle\": \"original-pdfium\",\n"
        "  \"pdfium_result\": \"rendered_blank\",\n"
        "  \"native_expected_outcome\": \"supported-blank-page\",\n"
        "  \"native_observed_outcome\": \"supported-blank-page\",\n"
        "  \"page_width\": %zu,\n"
        "  \"page_height\": %zu,\n"
        "  \"mean_rgb_error\": 0,\n"
        "  \"mismatched_channels\": %zu,\n"
        "  \"native_blank_page_supported_count\": 1,\n"
        "  \"native_path_clip_supported_count\": 0,\n"
        "  \"native_text_supported_count\": 0,\n"
        "  \"generated_fixture_count\": 0,\n"
        "  \"result\": \"passed\"\n"
        "}\n",WIDTH,HEIGHT,mismatch);
    CHECK(std::ferror(report)==0);
    CHECK(std::fclose(report)==0);
    CHECK(std::rename(pending.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[external-corpus] id=tinytoolkit.sample-blank-letter-v1 "
        "pdfium=blank native=blank-equal path-clip-supported=0\n");
    return EXIT_SUCCESS;
}


/* Exact externally published PDFium test fixture: one opaque black PATH.
 * Source PDF is preserved unchanged; test-only PDFium is independent of
 * Source -> Graph Snapshot -> detached IR -> pure C11 native raster. */
int test_external_pdfium_black_path(const char *filename,const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    const std::string staged=std::string(report_path)+".tmp";
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
    errno=0;
    if(std::remove(staged.c_str())!=0) CHECK(errno==ENOENT);
    CHECK(corpus_fixture_executions==0u);
    constexpr size_t WIDTH=612u, HEIGHT=792u;
    quantapdf_document *pdfium_doc=nullptr;
    quantapdf_page *pdfium_page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    const unsigned char *rgb=nullptr;
    size_t bitmap_bytes=0u;
    int width=0,height=0,stride=0,components=0;
    OK(quantapdf_open(filename,nullptr,&pdfium_doc));
    OK(quantapdf_load_page(pdfium_doc,0u,&pdfium_page));
    OK(quantapdf_render_page(pdfium_page,&bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap,&width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(bitmap,&rgb,&bitmap_bytes));
    CHECK(width==(int)WIDTH && height==(int)HEIGHT &&
          components==3 && stride>=width*3 && rgb!=nullptr &&
          bitmap_bytes>=(size_t)stride*HEIGHT);

    quantapdf_document *source=nullptr;
    quantapdf_graph_snapshot *snapshot=nullptr;
    quantapdf_ir detached{};
    OK(quantapdf_open(filename,nullptr,&source));
    quantapdf_status status=quantapdf_document_capture_graph(
        source,&graph_limits,&snapshot);
    quantapdf_close(source);
    CHECK(status==QUANTAPDF_OK && snapshot!=nullptr);
    status=quantapdf_graph_snapshot_page_composer_ir(
        snapshot,0u,2u,&decode_limits,&parse_limits,1u,
        &lower_limits,&detached);
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(status==QUANTAPDF_OK);
    CHECK(detached.pages && detached.page_count==1u &&
          detached.pages[0].width_points==(float)WIDTH &&
          detached.pages[0].height_points==(float)HEIGHT &&
          detached.pages[0].suppress_background==1);
    CHECK(detached.operations && detached.operation_count==1u &&
          detached.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
          detached.resources.graphics_state_count<=1u &&
          detached.resources.clip_count==0u &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.soft_mask_count==0u &&
          detached.resources.paint_count==0u &&
          detached.resources.form_count==0u);
    if(detached.resources.graphics_state_count==1u){
        CHECK(detached.resources.graphics_states!=nullptr);
        CHECK(detached.resources.graphics_states[0].fill_alpha==1.0f &&
              detached.resources.graphics_states[0].stroke_alpha==1.0f &&
              detached.resources.graphics_states[0].blend_mode==
                  QUANTAPDF_COMPOSER_BLEND_NORMAL &&
              detached.resources.graphics_states[0].soft_mask_id==0u);
    }

    const quantapdf_ir_raster_v1_limits limits={
        WIDTH*HEIGHT,1u,768u,60000000u
    };
    quantapdf_ir_raster_image native{},repeat{};
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&native));
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&repeat));
    CHECK(native.pixels && native.width==WIDTH && native.height==HEIGHT &&
          native.stride==WIDTH*4u && native.size==WIDTH*HEIGHT*4u);
    CHECK(native.size==repeat.size &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&detached);

    double total_difference=0.0;
    size_t mismatched_channels=0u,gross_pixels=0u;
    for(size_t y=0u;y<HEIGHT;++y)for(size_t x=0u;x<WIDTH;++x){
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=rgb+y*(size_t)stride+x*3u;
        CHECK(a[0]==0u && a[1]==0u && a[2]==0u && a[3]==255u);
        unsigned worst=0u;
        for(size_t channel=0u;channel<3u;++channel){
            const unsigned actual=on_white(a,channel),expected=b[channel];
            const unsigned delta=actual>expected?actual-expected:expected-actual;
            total_difference+=(double)delta;
            if(delta!=0u)++mismatched_channels;
            if(delta>worst)worst=delta;
        }
        if(worst>64u)++gross_pixels;
    }
    const double mean=total_difference/(double)(WIDTH*HEIGHT*3u);
    if(mean>3.0 || gross_pixels!=0u)
        std::fprintf(stderr,
            "source black/PDFium mean=%.5f gross=%zu mismatched=%zu\n",
            mean,gross_pixels,mismatched_channels);
    CHECK(mean<=3.0 && gross_pixels==0u);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(pdfium_page);
    quantapdf_close(pdfium_doc);
    CHECK(corpus_fixture_executions==0u);

    FILE *report=std::fopen(staged.c_str(),"wb");
    CHECK(report!=nullptr);
    std::fprintf(report,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"corpus_id\": \"external-source-pdf\",\n"
        "  \"document_id\": \"pdfium.black-original-v1\",\n"
        "  \"fixture_kind\": \"external\",\n"
        "  \"reference_oracle\": \"original-pdfium\",\n"
        "  \"pdfium_result\": \"rendered_opaque_black\",\n"
        "  \"native_expected_outcome\": \"supported-path-clip\",\n"
        "  \"native_observed_outcome\": \"supported-path-clip\",\n"
        "  \"native_painted_path_count\": 1,\n"
        "  \"native_clipped_path_supported_count\": 0,\n"
        "  \"native_path_clip_supported_count\": 1,\n"
        "  \"native_blank_page_supported_count\": 0,\n"
        "  \"native_text_supported_count\": 0,\n"
        "  \"page_width\": %zu,\n"
        "  \"page_height\": %zu,\n"
        "  \"mean_rgb_error\": %.6f,\n"
        "  \"mismatched_channels\": %zu,\n"
        "  \"gross_error_pixels\": %zu,\n"
        "  \"generated_fixture_count\": 0,\n"
        "  \"result\": \"passed\"\n"
        "}\n",WIDTH,HEIGHT,mean,mismatched_channels,gross_pixels);
    CHECK(std::ferror(report)==0);
    CHECK(std::fclose(report)==0);
    CHECK(std::rename(staged.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[external-corpus] id=pdfium.black-original-v1 "
        "path-painted=1 clipped=0 native-source-pixel-oracle=passed\n");
    return EXIT_SUCCESS;
}


/* Pages /Group conformance uses real source PDF bytes and genuine page
 * selection. These independent negative/positive probes are NOT admitted
 * external PDFs and do not modify the 14/36 generated-corpus registry. */
void test_page_group_admission()
{
    struct scenario {
        const char *group;
        quantapdf_status expected;
        bool indirect;
    };
    const scenario cases[] = {
        {nullptr,QUANTAPDF_OK,false},
        {"<< /S /Transparency /I true /CS /DeviceRGB >>",
         QUANTAPDF_OK,false},
        {"<< /Type /Group /S /Transparency /I true /K false "
         "/CS /DeviceRGB >>",QUANTAPDF_OK,true},
        {"<< /S /Transparency /I false /CS /DeviceRGB >>",
         QUANTAPDF_ERROR_UNSUPPORTED,false},
        {"<< /S /Transparency /I true /K true /CS /DeviceRGB >>",
         QUANTAPDF_ERROR_UNSUPPORTED,true},
        {"<< /S /Transparency /I true /CS /DeviceCMYK >>",
         QUANTAPDF_ERROR_UNSUPPORTED,false},
        {"<< /S /Other /I true /CS /DeviceRGB >>",
         QUANTAPDF_ERROR_UNSUPPORTED,false},
        {"<< /S /Transparency /I true /CS /DeviceRGB /Extra 1 >>",
         QUANTAPDF_ERROR_UNSUPPORTED,false},
        {"<< /S /Transparency /I /true /CS /DeviceRGB >>",
         QUANTAPDF_ERROR_FORMAT,false},
        {"<< /S /Transparency /I true /CS [ /DeviceRGB ] >>",
         QUANTAPDF_ERROR_FORMAT,false},
        {"<< /S /Transparency /I true /K /false /CS /DeviceRGB >>",
         QUANTAPDF_ERROR_FORMAT,false},
        {"<< /I true /CS /DeviceRGB >>",
         QUANTAPDF_ERROR_UNSUPPORTED,false},
        {"<< /S /Transparency /CS /DeviceRGB >>",
         QUANTAPDF_ERROR_UNSUPPORTED,false},
        {"<< /S /Transparency /I true >>",
         QUANTAPDF_ERROR_UNSUPPORTED,false},
        {"<< /Type /Wrong /S /Transparency /I true /CS /DeviceRGB >>",
         QUANTAPDF_ERROR_FORMAT,false},
        {"null",QUANTAPDF_OK,false},
        {"4",QUANTAPDF_ERROR_FORMAT,false}
    };
    auto write_source = [](QPDF& pdf) {
        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.write();
        auto bytes=writer.getBufferSharedPointer();
        FILE *output=std::fopen(GROUP_SOURCE_PDF,"wb");
        CHECK(output!=nullptr);
        CHECK(std::fwrite(bytes->getBuffer(),1,bytes->getSize(),output)==
              bytes->getSize());
        CHECK(std::fclose(output)==0);
    };
    const char *content =
        "q 1 0 0 rg 10 10 80 80 re f Q "
        "0 0 1 rg 0 0 20 20 re f";
    const probe expected_colors[] = {
        {5,5,0,0,255}, {15,15,0,0,255},
        {60,60,255,0,0}, {100,100,255,255,255}
    };
    for(size_t index=0;index<sizeof(cases)/sizeof(cases[0]);++index){
        const auto& tc=cases[index];
        auto pdf=QPDF::create();
        pdf->emptyPDF();
        auto root=pdf->getRoot().getKey("/Pages");
        auto leaf=pdf->makeIndirectObject(
            QPDFObjectHandle::parse("<< /Type /Page >>"));
        root.replaceKey("/Kids",QPDFObjectHandle::newArray({leaf}));
        root.replaceKey("/Count",QPDFObjectHandle::newInteger(1));
        root.replaceKey("/MediaBox",
            QPDFObjectHandle::parse("[0 0 120 120]"));
        leaf.replaceKey("/Parent",root);
        leaf.replaceKey("/Contents",pdf->newStream(content));
        if(tc.group!=nullptr){
            auto value=QPDFObjectHandle::parse(tc.group);
            if(tc.indirect)value=pdf->makeIndirectObject(value);
            leaf.replaceKey("/Group",value);
        }
        write_source(*pdf);
        quantapdf_ir lowered{};
        const quantapdf_status status=snapshot_lower(&lowered,0u,1u,2u,GROUP_SOURCE_PDF);
        if(status!=tc.expected)
            std::fprintf(stderr,
                "page Group case=%zu expected=%d got=%d\n",
                index,(int)tc.expected,(int)status);
        CHECK(status==tc.expected);
        if(status==QUANTAPDF_OK){
            CHECK(lowered.page_count==1u &&
                  lowered.operation_count==2u &&
                  lowered.resources.clip_count==0u);
            quantapdf_ir_raster_image native{},repeat{};
            OK(quantapdf_ir_raster_scoped_operations_v1(
                &lowered,0u,&raster_limits,&native));
            OK(quantapdf_ir_raster_scoped_operations_v1(
                &lowered,0u,&raster_limits,&repeat));
            CHECK(native.width==PAGE && native.height==PAGE &&
                  repeat.size==native.size &&
                  std::memcmp(native.pixels,repeat.pixels,native.size)==0);
            quantapdf_ir_raster_image_dispose(&repeat);
            quantapdf_ir_dispose(&lowered);
            rgb_image reference=render_original_pdfium(0u,GROUP_SOURCE_PDF);
            CHECK(reference.width==(int)PAGE && reference.height==(int)PAGE);
            for(const auto& p:expected_colors){
                size_t y=PAGE-1u-(size_t)p.y_pdf;
                const unsigned char *a=native.pixels+
                    y*native.stride+4u*(size_t)p.x;
                const unsigned char *b=reference.at((size_t)p.x,y);
                const unsigned colors[]={p.r,p.g,p.b};
                for(size_t c=0;c<3u;++c){
                    CHECK(std::abs((int)on_white(a,c)-(int)colors[c])<=3);
                    CHECK(std::abs((int)b[c]-(int)colors[c])<=3);
                }
            }
            quantapdf_ir_raster_image_dispose(&native);
        }else{
            CHECK(lowered.pages==nullptr && lowered.page_count==0u &&
                  lowered.operations==nullptr &&
                  lowered.operation_count==0u &&
                  lowered.resources.clip_count==0u &&
                  lowered.resources.graphics_state_count==0u);
            quantapdf_ir_dispose(&lowered);
        }
        CHECK(std::remove(GROUP_SOURCE_PDF)==0);
    }

    /* /Group is not inherited from /Pages. An unsupported sibling must
     * not alter or poison the selected page's admission. */
    auto pdf=QPDF::create();
    pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto bad=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    auto good=pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    root.replaceKey("/Kids",QPDFObjectHandle::newArray({bad,good}));
    root.replaceKey("/Count",QPDFObjectHandle::newInteger(2));
    root.replaceKey("/MediaBox",
        QPDFObjectHandle::parse("[0 0 120 120]"));
    /* An ancestor /Group is not inherited, even if not supported. */
    root.replaceKey("/Group",QPDFObjectHandle::parse(
        "<< /S /Transparency /I false /CS /DeviceCMYK >>"));
    bad.replaceKey("/Parent",root);
    good.replaceKey("/Parent",root);
    bad.replaceKey("/Group",QPDFObjectHandle::parse(
        "<< /S /Transparency /I false /CS /DeviceRGB >>"));
    bad.replaceKey("/Contents",pdf->newStream(content));
    good.replaceKey("/Contents",pdf->newStream(content));
    write_source(*pdf);
    quantapdf_ir refused{};
    CHECK(snapshot_lower(&refused,0u,1u,2u,GROUP_SOURCE_PDF)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(refused.pages==nullptr && refused.operation_count==0u);
    quantapdf_ir_dispose(&refused);
    quantapdf_ir admitted{};
    OK(snapshot_lower(&admitted,1u,1u,2u,GROUP_SOURCE_PDF));
    CHECK(admitted.page_count==1u && admitted.operation_count==2u);
    quantapdf_ir_dispose(&admitted);
    CHECK(std::remove(GROUP_SOURCE_PDF)==0);
    CHECK(corpus_fixture_executions==0u);
    std::fprintf(stderr,
        "[quantapdf.native_page_group_admission] cases=%zu "
        "page-isolation=passed\n",
        sizeof(cases)/sizeof(cases[0]));
}


/* Original independent upstream PDF: W n rectangular clip and a solid
 * one-pixel closed blue stroked triangle, no text or image. Neither this
 * source nor the PDFium oracle is manufactured from detached Composer IR. */
int test_external_pdfium_clip_stroke(const char *filename,
                                    const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    const std::string pending=std::string(report_path)+".tmp";
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
    errno=0;
    if(std::remove(pending.c_str())!=0) CHECK(errno==ENOENT);
    CHECK(corpus_fixture_executions==0u);
    constexpr size_t WIDTH=100u, HEIGHT=50u;
    rgb_image reference = render_direct_pdfium_reference(filename);
    const unsigned char *rgb = reference.pixels.data();
    const int width = reference.width, height = reference.height;
    const int stride = reference.stride, components = reference.components;
    CHECK(width==(int)WIDTH && height==(int)HEIGHT && components==3 &&
          stride>=(int)WIDTH*3 && rgb && reference.pixels.size()>=HEIGHT*(size_t)stride);

    quantapdf_document *source=nullptr;
    quantapdf_graph_snapshot *snapshot=nullptr;
    quantapdf_ir detached{};
    OK(quantapdf_open(filename,nullptr,&source));
    quantapdf_status status=quantapdf_document_capture_graph(
        source,&graph_limits,&snapshot);
    quantapdf_close(source);
    CHECK(status==QUANTAPDF_OK && snapshot!=nullptr);
    status=quantapdf_graph_snapshot_page_composer_ir(
        snapshot,0u,2u,&decode_limits,&parse_limits,1u,
        &lower_limits,&detached);
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(status==QUANTAPDF_OK);
    CHECK(detached.page_count==1u && detached.pages &&
          detached.pages[0].width_points==(float)WIDTH &&
          detached.pages[0].height_points==(float)HEIGHT &&
          detached.pages[0].suppress_background==1);
    CHECK(detached.operation_count==1u && detached.operations &&
          detached.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
          detached.operations[0].value.path.options.fill==0 &&
          detached.operations[0].value.path.options.stroke==1 &&
          detached.operations[0].value.path.options.stroke_width==1.0f &&
          detached.operations[0].value.path.options.stroke_argb==UINT32_C(0xff0000ff) &&
          detached.resources.clip_count==1u &&
          detached.resources.graphics_state_count==1u &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.form_count==0u &&
          detached.resources.soft_mask_count==0u &&
          detached.resources.paint_count==0u);
    const quantapdf_ir_raster_v1_limits limits={
        WIDTH*HEIGHT,1u,128u,5000000u
    };
    quantapdf_ir_raster_image native{},repeat{};
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&native));
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&repeat));
    CHECK(native.pixels && native.width==WIDTH && native.height==HEIGHT &&
          native.stride==WIDTH*4u && native.size==WIDTH*HEIGHT*4u &&
          repeat.size==native.size &&
          std::memcmp(repeat.pixels,native.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&detached);

    size_t reference_ink=0u,native_ink=0u,common_ink=0u,gross=0u;
    double total=0.0;
    for(size_t y=0u;y<HEIGHT;++y)for(size_t x=0u;x<WIDTH;++x){
        const unsigned char *a=native.pixels+y*native.stride+4u*x;
        const unsigned char *b=rgb+y*(size_t)stride+3u*x;
        unsigned worst=0u;
        unsigned red=on_white(a,0u),green=on_white(a,1u),
                 blue=on_white(a,2u);
        const bool ref_blue=b[0]<240u && b[1]<240u && b[2]>=240u;
        const bool native_blue=red<240u && green<240u && blue>=240u;
        reference_ink+=(size_t)ref_blue;
        native_ink+=(size_t)native_blue;
        common_ink+=(size_t)(ref_blue && native_blue);
        const unsigned actual[]={red,green,blue};
        for(size_t c=0u;c<3u;++c){
            const unsigned delta=actual[c]>b[c] ?
                actual[c]-b[c] : b[c]-actual[c];
            total+=(double)delta;
            if(delta>worst)worst=delta;
        }
        if(worst>64u)++gross;
    }
    const double mean=total/(double)(WIDTH*HEIGHT*3u);
    if(mean>6.0 || gross>WIDTH*HEIGHT/12u ||
       reference_ink<15u || native_ink<15u ||
       common_ink*2u<reference_ink)
        std::fprintf(stderr,
            "external clip stroke mean=%.4f gross=%zu "
            "referenceInk=%zu nativeInk=%zu overlap=%zu\n",
            mean,gross,reference_ink,native_ink,common_ink);
    CHECK(mean<=6.0 && gross<=WIDTH*HEIGHT/12u);
    CHECK(reference_ink>=15u && native_ink>=15u &&
          common_ink*2u>=reference_ink);
    CHECK(native.pixels[0u+3u]==0u);
    CHECK(native.pixels[(25u*WIDTH+25u)*4u+3u]==0u);
    quantapdf_ir_raster_image_dispose(&native);
    CHECK(corpus_fixture_executions==0u);
    FILE *report=std::fopen(pending.c_str(),"wb");
    CHECK(report!=nullptr);
    std::fprintf(report,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"corpus_id\": \"external-source-pdf\",\n"
        "  \"document_id\": \"pdfium.clip-path-v1\",\n"
        "  \"fixture_kind\": \"external\",\n"
        "  \"reference_oracle\": \"original-pdfium\",\n"
        "  \"native_expected_outcome\": \"supported-clipped-stroke\",\n"
        "  \"native_observed_outcome\": \"supported-clipped-stroke\",\n"
        "  \"pdfium_result\": \"rendered_clipped_blue_stroke\",\n"
        "  \"native_path_clip_supported_count\": 1,\n"
        "  \"native_clipped_path_supported_count\": 1,\n"
        "  \"native_stroke_1px_supported_count\": 1,\n"
        "  \"native_text_supported_count\": 0,\n"
        "  \"native_blank_page_supported_count\": 0,\n"
        "  \"page_width\": %zu,\n"
        "  \"page_height\": %zu,\n"
        "  \"mean_rgb_error\": %.6f,\n"
        "  \"gross_error_pixels\": %zu,\n"
        "  \"reference_blue_pixels\": %zu,\n"
        "  \"native_blue_pixels\": %zu,\n"
        "  \"common_blue_pixels\": %zu,\n"
        "  \"generated_fixture_count\": 0,\n"
        "  \"result\": \"passed\"\n"
        "}\n",WIDTH,HEIGHT,mean,gross,
        reference_ink,native_ink,common_ink);
    CHECK(std::ferror(report)==0);
    CHECK(std::fclose(report)==0);
    CHECK(std::rename(pending.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[external-corpus] id=pdfium.clip-path-v1 "
        "native=clipped-one-pixel-stroke pixel-oracle=passed\n");
    return EXIT_SUCCESS;
}



/* #384: minimal causal Clip A/B test. Keep this independent of the older
 * qp377/qp378/qp379 branch-only capture and reporting helpers. */
struct qp384_clip_capture {
    std::vector<unsigned char> reference_rgb;
    std::vector<unsigned char> native_rgba;
    bool native_supported = false;
};

static std::vector<unsigned char> qp384_read_bytes(const char *path)
{
    CHECK(path && *path);
    FILE *f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    CHECK(std::fseek(f, 0, SEEK_END) == 0);
    const long end = std::ftell(f);
    CHECK(end >= 0 && end <= 1024L * 1024L);
    CHECK(std::fseek(f, 0, SEEK_SET) == 0);
    std::vector<unsigned char> bytes((size_t)end);
    CHECK(std::fread(bytes.data(), 1u, bytes.size(), f) == bytes.size());
    CHECK(std::fclose(f) == 0);
    return bytes;
}

/* Issue #264: exact upstream W* source has visible text AFTER the
 * earlier even-odd Clip/fill. PDFium must render the unchanged original,
 * while native Graph -> detached IR rejects the whole page atomically. */
static int test_external_pdfium_wstar_late_text_rejection(
    const char *filename,const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    const std::string staged=std::string(report_path)+".tmp";
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
    errno=0;
    if(std::remove(staged.c_str())!=0) CHECK(errno==ENOENT);
    CHECK(corpus_fixture_executions==0u);

    const std::vector<unsigned char> original=qp384_read_bytes(filename);
    CHECK(original.size()==724u);
    const rgb_image reference=render_direct_pdfium_reference(filename);
    CHECK(reference.width==200 && reference.height==300 &&
          reference.components==3 && reference.stride>=reference.width*3);
    size_t dark_pixels=0u;
    for(int y=0;y<reference.height;++y) {
        for(int x=0;x<reference.width;++x) {
            const unsigned char *px=reference.at((size_t)x,(size_t)y);
            if(px[0]<240u || px[1]<240u || px[2]<240u) ++dark_pixels;
        }
    }
    CHECK(dark_pixels>0u); /* The late (Text) Tj is visible in original PDFium. */
    CHECK(qp384_read_bytes(filename)==original);

    quantapdf_document *source=nullptr;
    quantapdf_graph_snapshot *snapshot=nullptr;
    quantapdf_ir detached{};
    OK(quantapdf_open(filename,nullptr,&source));
    quantapdf_status status=quantapdf_document_capture_graph(
        source,&graph_limits,&snapshot);
    quantapdf_close(source);
    CHECK(status==QUANTAPDF_OK && snapshot!=nullptr);
    status=quantapdf_graph_snapshot_page_composer_ir(
        snapshot,0u,2u,&decode_limits,&parse_limits,1u,
        &lower_limits,&detached);
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(detached.pages==nullptr && detached.page_count==0u &&
          detached.operations==nullptr && detached.operation_count==0u &&
          detached.resources.clip_count==0u &&
          detached.resources.graphics_state_count==0u);
    quantapdf_ir_dispose(&detached);
    CHECK(corpus_fixture_executions==0u);
    CHECK(qp384_read_bytes(filename)==original);

    FILE *out=std::fopen(staged.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"corpus_id\": \"external-source-pdf\",\n"
        "  \"document_id\": \"pdfium.wstar-late-text-v1\",\n"
        "  \"fixture_kind\": \"external\",\n"
        "  \"reference_oracle\": \"original-pdfium\",\n"
        "  \"pdfium_result\": \"rendered_nonblank\",\n"
        "  \"pdfium_dark_pixels\": %zu,\n"
        "  \"native_expected_outcome\": \"unsupported\",\n"
        "  \"native_observed_outcome\": \"unsupported\",\n"
        "  \"native_ir\": \"empty\",\n"
        "  \"late_text_after_wstar_rejected\": true,\n"
        "  \"source_bytes\": 724,\n"
        "  \"source_bytes_unchanged_after_reference_and_native\": true,\n"
        "  \"pdfium_fallback_used\": false,\n"
        "  \"status_conformance_passed_count\": 1,\n"
        "  \"native_supported_passed_count\": 0,\n"
        "  \"native_wstar_clip_supported_count\": 0,\n"
        "  \"result\": \"passed\",\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n",dark_pixels);
    CHECK(std::ferror(out)==0);
    CHECK(std::fclose(out)==0);
    CHECK(std::rename(staged.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[external-corpus] id=pdfium.wstar-late-text-v1 "
        "pdfium=rendered_nonblank native=unsupported ir=empty "
        "late_text_after_wstar=refused source_bytes=724\n");
    return EXIT_SUCCESS;
}

static std::string qp384_control_path(const char *prefix, unsigned variant)
{
    CHECK(prefix && *prefix && variant >= 1u && variant <= 4u);
    char path[4096];
    const int n = std::snprintf(path, sizeof(path), "%s-%u.pdf",
                                prefix, variant);
    CHECK(n > 0 && (size_t)n < sizeof(path));
    return std::string(path);
}

static void qp384_write_control_pdf(const char *path, unsigned variant)
{
    CHECK(path && *path && variant >= 1u && variant <= 4u);
    static const char *const source_clip =
        "10 15 m\n40 15 l\n40 35 l\n10 35 l\nW n\n";
    static const char *const full_page_clip = "0 0 100 50 re W n\n";
    static const char *const narrower_clip =
        "10 16 m\n40 16 l\n40 34 l\n10 34 l\nW n\n";
    static const char *const blue_stroke =
        "0 0 1 RG\n10 10 m\n25 40 l\n40 10 l\ns\n";

    std::string commands;
    if (variant == 2u) commands = full_page_clip;
    else if (variant == 3u) commands = source_clip;
    else if (variant == 4u) commands = narrower_clip;
    commands += blue_stroke;

    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto pages = pdf->getRoot().getKey("/Pages");
    auto page = pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    pages.replaceKey("/Kids", QPDFObjectHandle::newArray({page}));
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pages.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 100 50]"));
    page.replaceKey("/Parent", pages);
    page.replaceKey("/Contents", pdf->newStream(commands));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.7");
    writer.setOutputMemory();
    writer.write();
    auto data = writer.getBufferSharedPointer();
    CHECK(data && data->getSize() > 0u && data->getSize() <= 8192u);
    FILE *out = std::fopen(path, "wb");
    CHECK(out != nullptr);
    CHECK(std::fwrite(data->getBuffer(), 1u, data->getSize(), out) ==
          data->getSize());
    CHECK(std::fclose(out) == 0);
}

static qp384_clip_capture qp384_capture(const char *path,
                                        bool expected_clip,
                                        bool expected_native)
{
    CHECK(path && *path);
    const std::vector<unsigned char> immutable = qp384_read_bytes(path);
    CHECK(!immutable.empty() && immutable.size() <= 8192u);

    /* Render the unchanged source twice, directly through PDFium's public
     * C API, so this reference bypasses QuantaPDF's document/render wrappers. */
    const rgb_image reference = render_direct_pdfium_reference(path);
    const rgb_image repeat_reference = render_direct_pdfium_reference(path);
    constexpr size_t W = 100u, H = 50u;
    CHECK(reference.width == (int)W && reference.height == (int)H &&
          reference.components == 3 && reference.stride >= (int)(W * 3u) &&
          reference.pixels.size() >= (size_t)reference.stride * H &&
          reference.width == repeat_reference.width &&
          reference.height == repeat_reference.height &&
          reference.stride == repeat_reference.stride &&
          reference.pixels == repeat_reference.pixels);

    qp384_clip_capture result;
    result.reference_rgb.resize(W * H * 3u);
    for (size_t y = 0u; y < H; ++y)
        std::memcpy(result.reference_rgb.data() + y * W * 3u,
                    reference.pixels.data() + y * (size_t)reference.stride,
                    W * 3u);

    /* Capture and lower the original PDF first; then rasterize only the owned
     * IR. Unsupported no-Clip/full-page variants must return no partial image. */
    quantapdf_ir detached{};
    OK(snapshot_lower(&detached, 0u, 1u, 2u, path));
    CHECK(detached.page_count == 1u && detached.pages &&
          detached.pages[0].width_points == (float)W &&
          detached.pages[0].height_points == (float)H &&
          detached.pages[0].suppress_background == 1 &&
          detached.operations && detached.operation_count == 1u &&
          detached.operations[0].kind == QUANTAPDF_COMPOSER_OPERATION_PATH &&
          detached.operations[0].value.path.options.fill == 0 &&
          detached.operations[0].value.path.options.stroke == 1 &&
          detached.operations[0].value.path.options.stroke_width == 1.0f &&
          detached.operations[0].value.path.options.stroke_argb ==
              UINT32_C(0xff0000ff) &&
          detached.resources.clip_count == (expected_clip ? 1u : 0u) &&
          detached.resources.font_count == 0u &&
          detached.resources.image_count == 0u &&
          detached.resources.soft_mask_count == 0u &&
          detached.resources.form_count == 0u &&
          detached.resources.paint_count == 0u);

    const quantapdf_ir_raster_v1_limits bounds = {
        W * H, 1u, 128u, 5000000u
    };
    quantapdf_ir_raster_image native{}, repeat{};
    const quantapdf_status first =
        quantapdf_ir_raster_scoped_operations_v1(&detached, 0u, &bounds,
                                                  &native);
    const quantapdf_status second =
        quantapdf_ir_raster_scoped_operations_v1(&detached, 0u, &bounds,
                                                  &repeat);
    if (expected_native) {
        CHECK(first == QUANTAPDF_OK && second == QUANTAPDF_OK &&
              native.pixels && repeat.pixels &&
              native.width == W && native.height == H &&
              native.stride == W * 4u && native.size == W * H * 4u &&
              repeat.size == native.size &&
              std::memcmp(repeat.pixels, native.pixels, native.size) == 0);
        result.native_rgba.assign(native.pixels,
                                  native.pixels + native.size);
        result.native_supported = true;
    } else {
        CHECK(first == QUANTAPDF_ERROR_UNSUPPORTED &&
              second == QUANTAPDF_ERROR_UNSUPPORTED &&
              native.pixels == nullptr && native.size == 0u &&
              native.width == 0u && native.height == 0u &&
              repeat.pixels == nullptr && repeat.size == 0u &&
              repeat.width == 0u && repeat.height == 0u);
    }
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_dispose(&detached);
    CHECK(qp384_read_bytes(path) == immutable);
    return result;
}

static size_t qp384_native_reference_mismatch_pixels(
    const qp384_clip_capture& sample)
{
    constexpr size_t W = 100u, H = 50u;
    CHECK(sample.native_supported &&
          sample.reference_rgb.size() == W * H * 3u &&
          sample.native_rgba.size() == W * H * 4u);
    size_t different = 0u;
    for (size_t p = 0u; p < W * H; ++p) {
        bool pixel_differs = false;
        for (size_t c = 0u; c < 3u; ++c) {
            const unsigned ref = sample.reference_rgb[p * 3u + c];
            const unsigned native = on_white(&sample.native_rgba[p * 4u], c);
            pixel_differs |= ref != native;
        }
        different += (size_t)pixel_differs;
    }
    return different;
}

static int test_external_pdfium_clip_causal_ab(const char *published,
                                                const char *generated_prefix,
                                                const char *report_path)
{
    CHECK(published && *published && generated_prefix && *generated_prefix &&
          report_path && *report_path &&
          std::strcmp(published, report_path) != 0 &&
          std::strcmp(generated_prefix, published) != 0 &&
          std::strcmp(generated_prefix, report_path) != 0 &&
          corpus_fixture_executions == 0u);
    const std::vector<unsigned char> immutable_original =
        qp384_read_bytes(published);
    CHECK(immutable_original.size() == 514u);

    const std::string generated[4] = {
        qp384_control_path(generated_prefix, 1u),
        qp384_control_path(generated_prefix, 2u),
        qp384_control_path(generated_prefix, 3u),
        qp384_control_path(generated_prefix, 4u)
    };
    for (const auto& path : generated) {
        CHECK(path != published && path != report_path);
        errno = 0;
        if (std::remove(path.c_str()) != 0) CHECK(errno == ENOENT);
    }
    const std::string staged = std::string(report_path) + ".tmp";
    errno = 0;
    if (std::remove(report_path) != 0) CHECK(errno == ENOENT);
    errno = 0;
    if (std::remove(staged.c_str()) != 0) CHECK(errno == ENOENT);

    qp384_clip_capture observed[5];
    observed[0] = qp384_capture(published, true, true);
    for (unsigned i = 1u; i <= 4u; ++i) {
        qp384_write_control_pdf(generated[i - 1u].c_str(), i);
        observed[i] = qp384_capture(generated[i - 1u].c_str(),
                                    i != 1u, i == 3u || i == 4u);
    }
    CHECK(qp384_read_bytes(published) == immutable_original);

    const size_t reference_bytes = 100u * 50u * 3u;
    const size_t native_bytes = 100u * 50u * 4u;
    CHECK(observed[1].reference_rgb.size() == reference_bytes &&
          observed[2].reference_rgb.size() == reference_bytes &&
          observed[3].reference_rgb.size() == reference_bytes &&
          observed[4].reference_rgb.size() == reference_bytes &&
          observed[1].native_rgba.empty() && observed[2].native_rgba.empty() &&
          !observed[1].native_supported && !observed[2].native_supported &&
          observed[0].native_supported && observed[3].native_supported &&
          observed[4].native_supported &&
          observed[0].native_rgba.size() == native_bytes &&
          observed[3].native_rgba.size() == native_bytes &&
          observed[4].native_rgba.size() == native_bytes &&
          observed[1].reference_rgb == observed[2].reference_rgb &&
          observed[0].reference_rgb == observed[3].reference_rgb &&
          observed[0].native_rgba == observed[3].native_rgba);

    size_t reference_changed = 0u, native_changed = 0u, both_changed = 0u;
    for (size_t p = 0u; p < 100u * 50u; ++p) {
        bool ref_change = false, native_change = false;
        for (size_t c = 0u; c < 3u; ++c) {
            ref_change |= observed[0].reference_rgb[p * 3u + c] !=
                          observed[4].reference_rgb[p * 3u + c];
            const unsigned old_native =
                on_white(&observed[0].native_rgba[p * 4u], c);
            const unsigned new_native =
                on_white(&observed[4].native_rgba[p * 4u], c);
            native_change |= old_native != new_native;
        }
        reference_changed += (size_t)ref_change;
        native_changed += (size_t)native_change;
        both_changed += (size_t)(ref_change && native_change);
    }
    CHECK(reference_changed > 0u && reference_changed <= 5000u &&
          native_changed <= 5000u && both_changed <=
              std::min(reference_changed, native_changed));

    const size_t original_mismatch =
        qp384_native_reference_mismatch_pixels(observed[0]);
    const size_t narrower_mismatch =
        qp384_native_reference_mismatch_pixels(observed[4]);

    FILE *report = std::fopen(staged.c_str(), "wb");
    CHECK(report != nullptr);
    std::fprintf(report,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-384-stroke-clip-causal-ab\",\n"
        "  \"reference_oracle\": \"direct-public-pdfium-c-api\",\n"
        "  \"published_source_bytes\": 514,\n"
        "  \"generated_control_count\": 4,\n"
        "  \"native_raster_changed\": false,\n"
        "  \"pdfium_fallback_used\": false,\n"
        "  \"unsupported_no_clip_and_full_page_clip_return_zero_output\": true,\n"
        "  \"published_and_same_clip_control_reference_equal\": true,\n"
        "  \"published_and_same_clip_control_native_equal\": true,\n"
        "  \"narrower_clip_reference_changed_pixels\": %zu,\n"
        "  \"narrower_clip_native_changed_pixels\": %zu,\n"
        "  \"narrower_clip_both_changed_pixels\": %zu,\n"
        "  \"original_native_reference_mismatch_pixels\": %zu,\n"
        "  \"narrower_native_reference_mismatch_pixels\": %zu,\n"
        "  \"result\": \"passed\"\n"
        "}\n",
        reference_changed, native_changed, both_changed,
        original_mismatch, narrower_mismatch);
    CHECK(std::ferror(report) == 0 && std::fclose(report) == 0);
#ifdef _WIN32
    errno = 0;
    if (std::remove(report_path) != 0) CHECK(errno == ENOENT);
#endif
    CHECK(std::rename(staged.c_str(), report_path) == 0);
    for (const auto& path : generated)
        CHECK(std::remove(path.c_str()) == 0);
    CHECK(qp384_read_bytes(published) == immutable_original &&
          corpus_fixture_executions == 0u);
    std::fprintf(stderr,
        "[issue-384] clip causal A/B passed: ref_changed=%zu "
        "native_changed=%zu both_changed=%zu; unsupported controls "
        "returned zero output; no raster behavior or threshold changes\n",
        reference_changed, native_changed, both_changed);
    return EXIT_SUCCESS;
}



/* #384 second step: isolated open-line Stroke controls. These intentionally
 * reuse only the current direct-PDFium and owned-IR helpers, not qp379_* or
 * decimal360_* experimental reporting machinery. */
static void qp384_write_open_line_pdf(const char *path, unsigned variant)
{
    CHECK(path && *path && variant <= 2u);
    std::string commands;
    if (variant == 1u) commands = "0 0 100 50 re W n\n";
    if (variant == 2u) commands = "18 15 17 15 re W n\n";
    commands += "0 0 1 RG\n10 10 m\n40 35 l\nS\n";
    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto pages = pdf->getRoot().getKey("/Pages");
    auto page = pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    pages.replaceKey("/Kids", QPDFObjectHandle::newArray({page}));
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pages.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 100 50]"));
    page.replaceKey("/Parent", pages);
    page.replaceKey("/Contents", pdf->newStream(commands));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.7");
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setOutputMemory();
    writer.write();
    auto data = writer.getBufferSharedPointer();
    CHECK(data && data->getSize() > 0u && data->getSize() <= 8192u);
    FILE *out = std::fopen(path, "wb");
    CHECK(out != nullptr);
    CHECK(std::fwrite(data->getBuffer(), 1u, data->getSize(), out) ==
          data->getSize());
    CHECK(std::fclose(out) == 0);
}

struct qp384_open_line_capture {
    std::vector<unsigned char> reference_rgb;
    std::vector<unsigned char> native_rgba;
};

static qp384_open_line_capture qp384_capture_open_line(const char *path,
                                                       unsigned expected_clips,
                                                       bool expect_native_output)
{
    constexpr size_t W = 100u, H = 50u, P = W * H;
    const auto immutable = qp384_read_bytes(path);
    CHECK(!immutable.empty() && immutable.size() <= 8192u);
    const rgb_image ref = render_direct_pdfium_reference(path);
    const rgb_image again_ref = render_direct_pdfium_reference(path);
    CHECK(ref.width == (int)W && ref.height == (int)H &&
          ref.components == 3 && ref.stride >= (int)(W * 3u) &&
          ref.pixels.size() >= (size_t)ref.stride * H &&
          ref.width == again_ref.width && ref.height == again_ref.height &&
          ref.stride == again_ref.stride && ref.pixels == again_ref.pixels);

    qp384_open_line_capture result;
    result.reference_rgb.resize(P * 3u);
    for (size_t y = 0u; y < H; ++y)
        std::memcpy(result.reference_rgb.data() + y * W * 3u,
                    ref.pixels.data() + y * (size_t)ref.stride, W * 3u);

    quantapdf_ir detached{};
    OK(snapshot_lower(&detached, 0u, 1u, 2u, path));
    CHECK(detached.page_count == 1u && detached.pages &&
          detached.pages[0].width_points == (float)W &&
          detached.pages[0].height_points == (float)H &&
          detached.pages[0].suppress_background == 1 &&
          detached.operations && detached.operation_count == 1u &&
          detached.operations[0].kind == QUANTAPDF_COMPOSER_OPERATION_PATH &&
          detached.operations[0].value.path.options.fill == 0 &&
          detached.operations[0].value.path.options.stroke == 1 &&
          detached.operations[0].value.path.options.stroke_width == 1.0f &&
          detached.operations[0].value.path.options.stroke_argb ==
              UINT32_C(0xff0000ff) &&
          detached.resources.clip_count == expected_clips &&
          detached.resources.font_count == 0u &&
          detached.resources.image_count == 0u &&
          detached.resources.soft_mask_count == 0u &&
          detached.resources.form_count == 0u &&
          detached.resources.paint_count == 0u);

    const quantapdf_ir_raster_v1_limits limits = {P, 1u, 128u, 5000000u};
    quantapdf_ir_raster_image native{}, repeated{};
    const quantapdf_status s1 =
        quantapdf_ir_raster_scoped_operations_v1(&detached, 0u, &limits,
                                                  &native);
    const quantapdf_status s2 =
        quantapdf_ir_raster_scoped_operations_v1(&detached, 0u, &limits,
                                                  &repeated);
    if (!expect_native_output) {
        /* The current bounded Native subset deliberately rejects the no-Clip
         * and full-page-W controls. Rejection must be transactional. */
        CHECK(s1 == QUANTAPDF_ERROR_UNSUPPORTED &&
              s2 == QUANTAPDF_ERROR_UNSUPPORTED &&
              !native.pixels && !repeated.pixels &&
              native.size == 0u && repeated.size == 0u &&
              native.width == 0u && native.height == 0u &&
              native.stride == 0u && repeated.width == 0u &&
              repeated.height == 0u && repeated.stride == 0u);
    } else {
        CHECK(s1 == QUANTAPDF_OK && s2 == QUANTAPDF_OK &&
              native.pixels && repeated.pixels &&
              native.width == W && native.height == H &&
              native.stride == W * 4u && native.size == P * 4u &&
              repeated.size == native.size &&
              std::memcmp(native.pixels, repeated.pixels, native.size) == 0);
        result.native_rgba.assign(native.pixels, native.pixels + native.size);
    }
    quantapdf_ir_raster_image_dispose(&repeated);
    quantapdf_ir_raster_image_dispose(&native);
    quantapdf_ir_dispose(&detached);
    CHECK(qp384_read_bytes(path) == immutable);
    return result;
}

static size_t qp384_open_line_changed_pixels(
    const std::vector<unsigned char>& a,
    const std::vector<unsigned char>& b, bool native)
{
    constexpr size_t P = 5000u;
    const size_t components = native ? 4u : 3u;
    CHECK(a.size() == P * components && b.size() == a.size());
    size_t changed = 0u;
    for (size_t p = 0u; p < P; ++p) {
        bool diff = false;
        for (size_t c = 0u; c < 3u; ++c) {
            const unsigned av = native ? on_white(&a[p * 4u], (unsigned)c)
                                        : a[p * 3u + c];
            const unsigned bv = native ? on_white(&b[p * 4u], (unsigned)c)
                                        : b[p * 3u + c];
            diff |= av != bv;
        }
        changed += (size_t)diff;
    }
    return changed;
}

static int test_external_pdfium_open_line_clip_controls(
    const char *generated_prefix, const char *report_path)
{
    CHECK(generated_prefix && *generated_prefix && report_path &&
          *report_path && std::strcmp(generated_prefix, report_path) != 0 &&
          corpus_fixture_executions == 0u);
    const std::string paths[3] = {
        std::string(generated_prefix) + "-no-clip.pdf",
        std::string(generated_prefix) + "-full-page-w.pdf",
        std::string(generated_prefix) + "-restrictive-w.pdf"
    };
    for (const auto& path : paths) {
        CHECK(path != report_path);
        errno = 0;
        if (std::remove(path.c_str()) != 0) CHECK(errno == ENOENT);
    }
    const std::string staged = std::string(report_path) + ".tmp";
    errno = 0;
    if (std::remove(report_path) != 0) CHECK(errno == ENOENT);
    errno = 0;
    if (std::remove(staged.c_str()) != 0) CHECK(errno == ENOENT);

    qp384_open_line_capture rows[3];
    for (unsigned i = 0u; i < 3u; ++i) {
        qp384_write_open_line_pdf(paths[i].c_str(), i);
        rows[i] = qp384_capture_open_line(paths[i].c_str(), i == 0u ? 0u : 1u,
                                          false);
    }
    constexpr size_t RGB_N = 100u * 50u * 3u;
    for (const auto& row : rows)
        CHECK(row.reference_rgb.size() == RGB_N);
    CHECK(rows[0].native_rgba.empty() && rows[1].native_rgba.empty() &&
          rows[2].native_rgba.empty());

    /* PDFium proves no-Clip/full-page-W semantic equivalence. The bounded
     * Native source subset remains fail-closed for these source-Clip controls. */
    CHECK(rows[0].reference_rgb == rows[1].reference_rgb);
    const size_t ref_changed = qp384_open_line_changed_pixels(
        rows[0].reference_rgb, rows[2].reference_rgb, false);
    /* PDFium confirms the restrictive clip changes the open-line result.
     * The bounded Native source subset currently rejects this generated
     * source-Clip representation; capture_open_line verifies that rejection
     * is transactional (unsupported status and zero output). */
    CHECK(ref_changed > 0u && ref_changed <= 5000u);

    FILE *out = std::fopen(staged.c_str(), "wb");
    CHECK(out != nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-384-open-line-clip-controls\",\n"
        "  \"reference_oracle\": \"direct-public-pdfium-c-api\",\n"
        "  \"open_line_only\": true,\n"
        "  \"native_source_clip_fail_closed\": true,\n"
        "  \"unsupported_controls_zero_output\": true,\n"
        "  \"source_bytes_unchanged_after_reference_and_native\": true,\n"
        "  \"pdfium_fallback_used\": false,\n"
        "  \"no_clip_and_fullpage_w_reference_equal\": true,\n"
        "  \"restrictive_w_reference_changed_pixels\": %zu,\n"
        "  \"restrictive_w_native_rejected_zero_output\": true,\n"
        "  \"native_raster_changed\": false,\n"
        "  \"pixel_thresholds_changed\": false,\n"
        "  \"result\": \"passed\"\n"
        "}\n", ref_changed);
    CHECK(std::ferror(out) == 0 && std::fclose(out) == 0);
#ifdef _WIN32
    errno = 0;
    if (std::remove(report_path) != 0) CHECK(errno == ENOENT);
#endif
    CHECK(std::rename(staged.c_str(), report_path) == 0);
    for (const auto& path : paths) CHECK(std::remove(path.c_str()) == 0);
    CHECK(corpus_fixture_executions == 0u);
    std::fprintf(stderr,
        "[issue-384] open-line Clip controls passed: restrictive W changed "
        "%zu PDFium pixels; Native rejected unsupported source Clip "
        "transactionally; no production changes\n", ref_changed);
    return EXIT_SUCCESS;
}


/* #269: generated source-PDF NEGATIVE control for fractional page points.
 * This is NOT the independently published jsPDF original, cannot increase
 * the external-source-positive numerator, and changes no raster admission. */
static void qp269_write_fractional_page(const char *path, bool inherited_crop)
{
    CHECK(path && *path);
    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto pages = pdf->getRoot().getKey("/Pages");
    auto page = pdf->makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Page >>"));
    pages.replaceKey("/Kids", QPDFObjectHandle::newArray({page}));
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pages.replaceKey("/MediaBox", QPDFObjectHandle::parse(
        inherited_crop ? "[0 0 120 80]" : "[0 0 100.25 50.5]"));
    if (inherited_crop)
        pages.replaceKey("/CropBox", QPDFObjectHandle::parse(
            "[5.125 7.25 105.375 57.75]"));
    page.replaceKey("/Parent", pages);
    page.replaceKey("/Contents", pdf->newStream(
        "1 0 0 rg 10 10 20 10 re f"));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.7");
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setOutputMemory();
    writer.write();
    auto bytes = writer.getBufferSharedPointer();
    CHECK(bytes && bytes->getSize() > 0u && bytes->getSize() <= 8192u);
    FILE *out = std::fopen(path, "wb");
    CHECK(out != nullptr);
    CHECK(std::fwrite(bytes->getBuffer(), 1u, bytes->getSize(), out) ==
          bytes->getSize());
    CHECK(std::fclose(out) == 0);
}

static int test_fractional_page_negative(const char *generated_prefix,
                                         const char *report_path)
{
    CHECK(generated_prefix && *generated_prefix && report_path &&
          *report_path && std::strcmp(generated_prefix, report_path) != 0 &&
          corpus_fixture_executions == 0u);
    const std::string paths[2] = {
        std::string(generated_prefix) + "-media.pdf",
        std::string(generated_prefix) + "-inherited-crop.pdf"
    };
    const std::string staged = std::string(report_path) + ".tmp";
    for (const auto& path : paths) {
        CHECK(path != report_path);
        errno = 0;
        if (std::remove(path.c_str()) != 0) CHECK(errno == ENOENT);
    }
    errno = 0;
    if (std::remove(report_path) != 0) CHECK(errno == ENOENT);
    errno = 0;
    if (std::remove(staged.c_str()) != 0) CHECK(errno == ENOENT);

    for (unsigned i = 0u; i < 2u; ++i) {
        const bool inherited_crop = i == 1u;
        qp269_write_fractional_page(paths[i].c_str(), inherited_crop);
        const auto original = qp384_read_bytes(paths[i].c_str());
        CHECK(!original.empty() && original.size() <= 8192u);

        /* PDFium directly renders the generated original, never a re-export
         * of Graph/IR. Both boxes have exact 100.25 x 50.5 visible points. */
        const rgb_image pdfium = render_direct_pdfium_reference(paths[i].c_str());
        const rgb_image repeated = render_direct_pdfium_reference(paths[i].c_str());
        CHECK(pdfium.width == 101 && pdfium.height == 51 &&
              pdfium.components == 3 && pdfium.stride == 303 &&
              pdfium.pixels.size() == 101u * 51u * 3u &&
              pdfium.width == repeated.width &&
              pdfium.height == repeated.height &&
              pdfium.pixels == repeated.pixels);
        bool visible = false;
        for (const unsigned char channel : pdfium.pixels)
            visible |= channel < 240u;
        CHECK(visible);

        quantapdf_ir detached{};
        OK(snapshot_lower(&detached, 0u, 1u, 2u, paths[i].c_str()));
        CHECK(detached.page_count == 1u && detached.pages &&
              detached.pages[0].width_points == 100.25f &&
              detached.pages[0].height_points == 50.5f &&
              detached.pages[0].suppress_background == 1 &&
              detached.operation_count == 1u && detached.operations &&
              detached.operations[0].kind == QUANTAPDF_COMPOSER_OPERATION_PATH &&
              detached.operations[0].value.path.options.fill == 1 &&
              detached.operations[0].value.path.options.stroke == 0 &&
              detached.resources.clip_count == 0u &&
              detached.resources.image_count == 0u &&
              detached.resources.font_count == 0u &&
              detached.resources.graphics_state_count == 0u);

        const quantapdf_ir_raster_v1_limits bounds = {
            101u * 51u, 1u, 128u, 5000000u
        };
        quantapdf_ir_raster_image native{}, repeat{};
        const quantapdf_status first = quantapdf_ir_raster_scoped_operations_v1(
            &detached, 0u, &bounds, &native);
        const quantapdf_status second = quantapdf_ir_raster_scoped_operations_v1(
            &detached, 0u, &bounds, &repeat);
        CHECK(first == QUANTAPDF_ERROR_UNSUPPORTED &&
              second == QUANTAPDF_ERROR_UNSUPPORTED &&
              native.pixels == nullptr && repeat.pixels == nullptr &&
              native.size == 0u && repeat.size == 0u &&
              native.width == 0u && native.height == 0u &&
              native.stride == 0u && repeat.width == 0u &&
              repeat.height == 0u && repeat.stride == 0u);
        quantapdf_ir_raster_image_dispose(&native);
        quantapdf_ir_raster_image_dispose(&repeat);
        quantapdf_ir_dispose(&detached);
        CHECK(qp384_read_bytes(paths[i].c_str()) == original);
        CHECK(std::remove(paths[i].c_str()) == 0);
    }

    CHECK(corpus_fixture_executions == 0u);
    FILE *out = std::fopen(staged.c_str(), "wb");
    CHECK(out != nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-269-fractional-page-negative\",\n"
        "  \"source_kind\": \"test-generated-not-external\",\n"
        "  \"case_count\": 2,\n"
        "  \"fractional_visible_width_points\": 100.25,\n"
        "  \"fractional_visible_height_points\": 50.5,\n"
        "  \"pdfium_reference_grid_width\": 101,\n"
        "  \"pdfium_reference_grid_height\": 51,\n"
        "  \"pdfium_reference_repeat_identical\": true,\n"
        "  \"native_ir_lowered\": true,\n"
        "  \"native_expected_outcome\": \"unsupported\",\n"
        "  \"native_observed_outcome\": \"unsupported\",\n"
        "  \"native_rejected_zero_output\": true,\n"
        "  \"external_native_supported_passed_count\": 0,\n"
        "  \"production_raster_changed\": false,\n"
        "  \"result\": \"passed\"\n"
        "}\n");
    CHECK(std::ferror(out) == 0 && std::fclose(out) == 0);
    CHECK(std::rename(staged.c_str(), report_path) == 0);
    std::fprintf(stderr,
        "[issue-269] fractional MediaBox/inherited CropBox: PDFium "
        "101x51, native unsupported with zero output; external positive=0\n");
    return EXIT_SUCCESS;
}

}

struct source_corpus_family {
    const char *id;
    const char *features;
    const char *expected;
    const char *oracle;
    size_t expected_fixtures;
    void (*run)();
};

/* Registry and execution are inseparable: adding a row causes the existing
 * CTest target to execute it and validate its fixture cardinality. */
static const source_corpus_family source_corpus_families[] = {
#define QP_SOURCE_CORPUS_FAMILY(ID, TAGS, EXPECTED, ORACLE, COUNT, FN) \
    {ID, TAGS, EXPECTED, ORACLE, COUNT, &FN},
#include "native_source_corpus.def"
#undef QP_SOURCE_CORPUS_FAMILY
};

/* Report bytes are stable across OSes and contain no time, host paths or
 * PDF writer binary hashes. The report is published only after every
 * generated fixture and all family cardinality assertions have passed. */
static std::string corpus_json_string(const char *value)
{
    static const char digits[] = "0123456789abcdef";
    std::string result(1u, '"');
    for(const unsigned char *p =
            reinterpret_cast<const unsigned char *>(value); *p; ++p) {
        if(*p == '"' || *p == '\\') {
            result.push_back('\\');
            result.push_back(static_cast<char>(*p));
        } else if(*p < 0x20u) {
            result += "\\u00";
            result.push_back(digits[*p >> 4u]);
            result.push_back(digits[*p & 15u]);
        } else {
            result.push_back(static_cast<char>(*p));
        }
    }
    result.push_back('"');
    return result;
}

static void publish_corpus_report(const char *destination,
                                  const size_t *executed,
                                  size_t family_count,size_t fixture_count)
{
    const std::string staged = std::string(destination) + ".tmp";
    FILE *out = std::fopen(staged.c_str(),"wb");
    CHECK(out != nullptr);
    std::fprintf(out,
        "{\n  \"schema_version\": 1,\n"
        "  \"corpus_id\": \"native-source-pdf\",\n"
        "  \"fixture_kind\": \"generated\",\n"
        "  \"result\": \"passed\",\n"
        "  \"family_count\": %zu,\n"
        "  \"fixture_count\": %zu,\n"
        "  \"family_results\": [\n",family_count,fixture_count);
    for(size_t i=0u;i<family_count;++i){
        const source_corpus_family& family=source_corpus_families[i];
        const std::string id=corpus_json_string(family.id);
        const std::string features=corpus_json_string(family.features);
        const std::string expected=corpus_json_string(family.expected);
        const std::string oracle=corpus_json_string(family.oracle);
        std::fprintf(out,
            "    {\"id\": %s, \"feature_tags\": %s, "
            "\"expected_outcome\": %s, \"oracle\": %s, "
            "\"declared_fixtures\": %zu, \"executed_fixtures\": %zu, "
            "\"result\": \"passed\"}%s\n",
            id.c_str(),features.c_str(),expected.c_str(),
            oracle.c_str(),family.expected_fixtures,executed[i],
            (i+1u==family_count) ? "" : ",");
    }
    std::fprintf(out,"  ]\n}\n");
    CHECK(std::ferror(out)==0);
    CHECK(std::fclose(out)==0);
    CHECK(std::rename(staged.c_str(),destination)==0);
    std::fprintf(stderr,"[source-corpus] report=%s\n",destination);
}

struct clip_leaf_counts {
    size_t nonzero=0u,evenodd=0u,intersections=0u;
};
clip_leaf_counts inspect_clip_fill_rules(const quantapdf_ir& ir)
{
    CHECK(ir.resources.clip_count<=16u);
    CHECK(ir.resources.clip_count==0u || ir.resources.clips!=nullptr);
    clip_leaf_counts out{};
    for(size_t i=0u;i<ir.resources.clip_count;++i) {
        const auto& clip=ir.resources.clips[i];
        if(clip.kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL) {
            if(clip.fill_rule==QUANTAPDF_COMPOSER_FILL_NONZERO) ++out.nonzero;
            else if(clip.fill_rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD) ++out.evenodd;
            else CHECK(false);
        } else if(clip.kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL) {
            ++out.intersections;
        } else {
            CHECK(false);
        }
    }
    return out;
}

int test_external_yworks_svg_w_pixels(const char *filename,
                                      const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    namespace fs=std::filesystem;
    std::error_code ec;
    const fs::path source(filename),report(report_path);
    const std::string staged=std::string(report_path)+".tmp";
    const fs::path stage(staged);
    CHECK(fs::is_regular_file(source,ec) && !ec);
    CHECK(!fs::is_symlink(source,ec) && !ec);
    const fs::path original=fs::weakly_canonical(source,ec);
    CHECK(!ec);
    const fs::path output=fs::weakly_canonical(report,ec);
    CHECK(!ec && original!=output);
    const fs::path temp=fs::weakly_canonical(stage,ec);
    CHECK(!ec && original!=temp);
    for(const fs::path& candidate:{report,stage}) {
        const bool exists=fs::exists(candidate,ec);
        CHECK(!ec);
        if(exists) {
            CHECK(!fs::is_symlink(candidate,ec) && !ec);
            CHECK(!fs::equivalent(source,candidate,ec) && !ec);
        }
    }
    constexpr size_t WIDTH=500u,HEIGHT=800u;
    CHECK(corpus_fixture_executions==0u);
    rgb_image pdfium=render_direct_pdfium_reference(filename);
    CHECK(pdfium.width==(int)WIDTH && pdfium.height==(int)HEIGHT &&
          pdfium.components==3 && pdfium.stride>=(int)WIDTH*3);
    quantapdf_pdf_path_lower_limits bounded=lower_limits;
    bounded.max_painted_paths=8u;  /* Explicit test-only capacity, no retry. */
    quantapdf_ir detached{};
    OK(snapshot_lower(&detached,0u,4u,8u,filename,&bounded));
    CHECK(detached.page_count==1u && detached.pages!=nullptr &&
          detached.pages[0].width_points==(float)WIDTH &&
          detached.pages[0].height_points==(float)HEIGHT &&
          detached.operations!=nullptr && detached.operation_count==6u &&
          detached.resources.clip_count==9u &&
          detached.resources.graphics_state_count==3u &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.form_count==0u &&
          detached.resources.soft_mask_count==0u &&
          detached.resources.paint_count==0u);
    const clip_leaf_counts winding=inspect_clip_fill_rules(detached);
    CHECK(winding.nonzero==5u && winding.evenodd==0u &&
          winding.intersections==4u);

    const quantapdf_ir_raster_v1_limits limits={
        WIDTH*HEIGHT,6u,4096u,3200000000ULL
    };
    quantapdf_ir_raster_image native{},repeat{};
    const quantapdf_status status=
        quantapdf_ir_raster_scoped_operations_v1(
            &detached,0u,&limits,&native);
    if(status!=QUANTAPDF_OK) {
        std::fprintf(stderr,
            "[svg-W-pixel] native C11 status=%d ops=%zu clips=%zu states=%zu\n",
            (int)status,detached.operation_count,
            detached.resources.clip_count,detached.resources.graphics_state_count);
        /* Test-only deterministic scope attribution. Borrow the already
         * detached original IR, never rewrite it or invoke a fallback. */
        unsigned all_states=0u,all_clips=0u;
        for(size_t i=0u;i<detached.operation_count;++i) {
            const auto& op=detached.operations[i];
            const auto& path=op.value.path;
            const size_t state_id=(size_t)op.graphics_state_id;
            size_t clip_id=0u,leaf_count=0u;
            unsigned visited=0u;
            const quantapdf_composer_clip_state *leaves
                [QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES]={};
            quantapdf_status collect=QUANTAPDF_OK;
            if(state_id>0u && state_id<=detached.resources.graphics_state_count) {
                all_states|=1u<<(unsigned)(state_id-1u);
                clip_id=(size_t)detached.resources.graphics_states[state_id-1u].clip_id;
                if(clip_id>0u)
                    collect=quantapdf_ir_clip_dag_collect_v3(
                        &detached,(quantapdf_composer_clip_id)clip_id,
                        leaves,&leaf_count,&visited);
                if(collect==QUANTAPDF_OK) all_clips|=visited;
            }
            quantapdf_composer_operation projected=op;
            projected.graphics_state_id=0u;
            projected.value.path.options.graphics_state_id=0u;
            quantapdf_composer_page_state copied=*detached.pages;
            copied.suppress_background=1;
            quantapdf_ir local{};
            local.pages=&copied;local.page_count=1u;
            local.operations=&projected;local.operation_count=1u;
            quantapdf_ir_raster_image sub{};
            quantapdf_ir_raster_v1_limits one_limit=limits;
            one_limit.max_operations=1u;
            one_limit.max_sample_tests/=detached.operation_count;
            one_limit.max_edges/=detached.operation_count;
            quantapdf_status sub_status=QUANTAPDF_ERROR_UNSUPPORTED;
            if(collect==QUANTAPDF_OK) {
                if(leaf_count==0u)
                    sub_status=quantapdf_ir_raster_render_v1(
                        &local,0u,&one_limit,&sub);
                else
                    sub_status=quantapdf_ir_raster_render_v1_multi_clipped(
                        &local,0u,&one_limit,leaves,leaf_count,&sub);
            }
            quantapdf_ir_raster_image_dispose(&sub);
            std::fprintf(stderr,
                "[svg-W-scope] i=%zu path_commands=%zu stroke=%d fill=%d "
                "width=%.5f cap=%d join=%d miter=%.5f ctm=[%.4f %.4f %.4f %.4f %.4f %.4f] "
                "gs=%zu clip=%zu leaves=%zu visited=%u collect=%d raster=%d\n",
                i,path.command_count,path.options.stroke,path.options.fill,
                (double)path.options.stroke_width,(int)path.options.line_cap,
                (int)path.options.line_join,(double)path.options.miter_limit,
                (double)path.options.transform.a,(double)path.options.transform.b,
                (double)path.options.transform.c,(double)path.options.transform.d,
                (double)path.options.transform.e,(double)path.options.transform.f,
                state_id,clip_id,leaf_count,visited,
                (int)collect,(int)sub_status);
        }
        std::fprintf(stderr,
            "[svg-W-resource] used_states=%u used_clips=%u all_states=%u all_clips=%u\n",
            all_states,all_clips,
            (1u<<(unsigned)detached.resources.graphics_state_count)-1u,
            (1u<<(unsigned)detached.resources.clip_count)-1u);
    }
    CHECK(status==QUANTAPDF_OK);
    /* Reject a forged orphan: this copy changes only an intermediate
     * prefix descriptor, never the byte-pinned original/owned detached IR. */
    {
        std::vector<quantapdf_composer_clip_state> altered(
            detached.resources.clips,
            detached.resources.clips+detached.resources.clip_count);
        quantapdf_ir invalid=detached;
        invalid.resources.clips=altered.data();
        altered[2].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        quantapdf_ir_raster_image rejected{};
        CHECK(quantapdf_ir_raster_scoped_operations_v1(
            &invalid,0u,&limits,&rejected)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(rejected.pixels==nullptr && rejected.size==0u);
    }
    /* Every explicit capacity is a strict ceiling; test projections borrow
     * only descriptor copies, never mutate/dispose the original owned IR. */
    {
        std::vector<quantapdf_composer_operation> over(
            detached.operations,detached.operations+detached.operation_count);
        over.push_back(detached.operations[0]);
        quantapdf_ir invalid=detached;
        invalid.operations=over.data();
        invalid.operation_count=over.size();
        quantapdf_ir_raster_image rejected{};
        CHECK(quantapdf_ir_raster_scoped_operations_v1(
            &invalid,0u,&limits,&rejected)==QUANTAPDF_ERROR_ARGUMENT);
        CHECK(rejected.pixels==nullptr && rejected.size==0u);
    }
    {
        std::vector<quantapdf_composer_clip_state> over(
            detached.resources.clips,
            detached.resources.clips+detached.resources.clip_count);
        over.push_back(detached.resources.clips[0]);
        quantapdf_ir invalid=detached;
        invalid.resources.clips=over.data();
        invalid.resources.clip_count=over.size();
        quantapdf_ir_raster_image rejected{};
        CHECK(quantapdf_ir_raster_scoped_operations_v1(
            &invalid,0u,&limits,&rejected)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(rejected.pixels==nullptr && rejected.size==0u);
    }
    {
        /* A shadow intersection must be an ORDERED suffix of the active
         * later root; matching leaf IDs with swapped parents is not enough. */
        std::vector<quantapdf_composer_clip_state> altered(
            detached.resources.clips,
            detached.resources.clips+detached.resources.clip_count);
        CHECK(altered[2].member_count==2u && altered[2].members!=nullptr);
        std::vector<quantapdf_composer_clip_id> ids(
            altered[2].members,altered[2].members+altered[2].member_count);
        const auto tmp=ids[0];ids[0]=ids[1];ids[1]=tmp;
        altered[2].members=ids.data();
        quantapdf_ir invalid=detached;
        invalid.resources.clips=altered.data();
        quantapdf_ir_raster_image rejected{};
        CHECK(quantapdf_ir_raster_scoped_operations_v1(
            &invalid,0u,&limits,&rejected)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(rejected.pixels==nullptr && rejected.size==0u);
    }
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&repeat));
    CHECK(native.pixels!=nullptr && native.width==WIDTH &&
          native.height==HEIGHT && native.stride==WIDTH*4u &&
          native.size==WIDTH*HEIGHT*4u &&
          repeat.pixels!=nullptr && repeat.size==native.size &&
          std::memcmp(repeat.pixels,native.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&detached);

    /* Independent published SVG semantics: objectBoundingBox top W clips
     * y=220..280 to black-left/blue-right; lower W clips y=520..580 to
     * black-left/white-right. Red 1px mitered rectangle/vertical line
     * probes are billed separately by whole-image red ink overlap. */
    struct sample {size_t x,y;unsigned r,g,b;};
    const sample goldens[]={
        {200u,240u,0u,0u,0u},
        {300u,240u,0u,0u,255u},
        {200u,550u,0u,0u,0u},
        {300u,550u,255u,255u,255u},
        {200u,190u,255u,255u,255u},
        {300u,600u,255u,255u,255u},
        {50u,50u,255u,255u,255u}
    };
    for(const sample& p:goldens) {
        const unsigned char *b=pdfium.at(p.x,p.y);
        const unsigned char *a=native.pixels+p.y*native.stride+p.x*4u;
        const unsigned want[]={p.r,p.g,p.b};
        for(size_t c=0u;c<3u;++c) {
            const int n=(int)on_white(a,c),r=(int)b[c],v=(int)want[c];
            if(std::abs(n-v)>8 || std::abs(r-v)>8)
                std::fprintf(stderr,
                    "[svg-W-golden] x=%zu y=%zu c=%zu native=%d pdfium=%d expected=%d\n",
                    p.x,p.y,c,n,r,v);
            CHECK(std::abs(n-v)<=8 && std::abs(r-v)<=8);
        }
    }
    double total=0.0;
    size_t gross=0u,ref_red=0u,native_red=0u,overlap_red=0u;
    for(size_t y=0u;y<HEIGHT;++y)for(size_t x=0u;x<WIDTH;++x) {
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=pdfium.at(x,y);
        unsigned rgb[3],worst=0u;
        for(size_t c=0u;c<3u;++c) {
            rgb[c]=on_white(a,c);
            const unsigned diff=rgb[c]>b[c]?rgb[c]-b[c]:b[c]-rgb[c];
            total+=(double)diff;
            if(diff>worst)worst=diff;
        }
        if(worst>64u)++gross;
        const bool ref=(b[0]>=190u && b[1]<=150u && b[2]<=150u);
        const bool nat=(rgb[0]>=190u && rgb[1]<=150u && rgb[2]<=150u);
        ref_red+=(size_t)ref;
        native_red+=(size_t)nat;
        overlap_red+=(size_t)(ref&&nat);
    }
    const double mean=total/(double)(WIDTH*HEIGHT*3u);
    if(mean>0.05 || gross!=0u ||
       ref_red<5000u || native_red<5000u ||
       overlap_red*100u<ref_red*99u)
        std::fprintf(stderr,
            "[svg-W-parity] mean=%.6f gross=%zu "
            "ref_red=%zu native_red=%zu overlap=%zu\n",
            mean,gross,ref_red,native_red,overlap_red);
    CHECK(mean<=0.05 && gross==0u);
    CHECK(ref_red>=5000u && native_red>=5000u &&
          overlap_red*100u>=ref_red*99u);
    quantapdf_ir_raster_image_dispose(&native);
    CHECK(corpus_fixture_executions==0u);

    FILE *out=std::fopen(staged.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"candidate_pixel_parity_passed_not_admitted\",\n"
        "  \"document_id\": \"yworks.clippath-svg-v1\",\n"
        "  \"fixture_kind\": \"quarantined-external-original\",\n"
        "  \"original_source_pdfium_oracle\": \"passed\",\n"
        "  \"detached_c11_native_raster\": \"passed\",\n"
        "  \"source_and_graph_released_before_raster\": true,\n"
        "  \"official_corpus_admitted\": false,\n"
        "  \"page_width\": %zu,\n"
        "  \"page_height\": %zu,\n"
        "  \"native_painted_paths\": 6,\n"
        "  \"native_w_nonzero_clip_path_leaves\": 5,\n"
        "  \"native_wstar_evenodd_clip_path_leaves\": 0,\n"
        "  \"native_clip_intersection_nodes\": 4,\n"
        "  \"native_total_clip_nodes\": 9,\n"
        "  \"fixed_interior_goldens\": %zu,\n"
        "  \"repeat_rgba_identical\": true,\n"
        "  \"mean_rgb_error\": %.6f,\n"
        "  \"gross_error_pixels\": %zu,\n"
        "  \"reference_red_pixels\": %zu,\n"
        "  \"native_red_pixels\": %zu,\n"
        "  \"overlap_red_pixels\": %zu,\n"
        "  \"external_wstar_pixel_supported_count\": 0,\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n",WIDTH,HEIGHT,sizeof(goldens)/sizeof(goldens[0]),
        mean,gross,ref_red,native_red,overlap_red);
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(staged.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[svg-W-original-pixel] passed mean=%.6f gross=%zu W=5 intersections=4 "
        "official_positive=0\n",mean,gross);
    return EXIT_SUCCESS;
}

int test_external_yworks_wstar_pixels(const char *filename,
                                         const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    namespace fs=std::filesystem;
    std::error_code ec;
    const fs::path source(filename),report(report_path);
    const std::string staged=std::string(report_path)+".tmp";
    const fs::path stage(staged);
    CHECK(fs::is_regular_file(source,ec) && !ec);
    CHECK(!fs::is_symlink(source,ec) && !ec);
    const fs::path original=fs::weakly_canonical(source,ec);
    CHECK(!ec);
    const fs::path output=fs::weakly_canonical(report,ec);
    CHECK(!ec && original!=output);
    const fs::path temp=fs::weakly_canonical(stage,ec);
    CHECK(!ec && original!=temp);
    for(const fs::path& candidate:{report,stage}) {
        const bool exists=fs::exists(candidate,ec);
        CHECK(!ec);
        if(exists) {
            CHECK(!fs::is_symlink(candidate,ec) && !ec);
            CHECK(!fs::equivalent(source,candidate,ec) && !ec);
        }
    }
    constexpr size_t WIDTH=600u,HEIGHT=600u;
    CHECK(corpus_fixture_executions==0u);

    /* Independent reference: unchanged original PDF directly through PDFium. */
    const rgb_image pdfium=render_direct_pdfium_reference(filename);
    CHECK(pdfium.width==(int)WIDTH && pdfium.height==(int)HEIGHT &&
          pdfium.components==3 && pdfium.stride>=(int)WIDTH*3);

    /* One explicit test-only capacity, no retries or fallback basis. */
    quantapdf_pdf_path_lower_limits bounded=lower_limits;
    bounded.max_painted_paths=8u;
    quantapdf_ir detached{};
    OK(snapshot_lower(&detached,0u,4u,8u,filename,&bounded));
    const size_t painted_count=detached.operation_count;
    const size_t clip_count=detached.resources.clip_count;
    const size_t state_count=detached.resources.graphics_state_count;
    const clip_leaf_counts winding=inspect_clip_fill_rules(detached);
    std::fprintf(stderr,
        "[wstar-candidate-ir] pages=%zu paths=%zu clips=%zu states=%zu "
        "W=%zu Wstar=%zu intersections=%zu\\n",
        detached.page_count,painted_count,clip_count,state_count,
        winding.nonzero,winding.evenodd,winding.intersections);
    CHECK(detached.page_count==1u && detached.pages!=nullptr &&
          detached.pages[0].width_points==(float)WIDTH &&
          detached.pages[0].height_points==(float)HEIGHT &&
          detached.operations!=nullptr && painted_count==3u &&
          clip_count==7u && state_count==3u &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.form_count==0u &&
          detached.resources.soft_mask_count==0u &&
          detached.resources.paint_count==0u);
    CHECK(winding.nonzero==2u && winding.evenodd==2u &&
          winding.intersections==3u);

    const quantapdf_ir_raster_v1_limits limits={
        WIDTH*HEIGHT,6u,4096u,3200000000ULL
    };
    quantapdf_ir_raster_image native{},repeat{};
    const quantapdf_status status=
        quantapdf_ir_raster_scoped_operations_v1(
            &detached,0u,&limits,&native);
    if(status!=QUANTAPDF_OK)
        std::fprintf(stderr,
            "[wstar-candidate] native status=%d ops=%zu clips=%zu states=%zu "
            "W=%zu Wstar=%zu intersections=%zu\n",
            (int)status,detached.operation_count,detached.resources.clip_count,
            detached.resources.graphics_state_count,winding.nonzero,
            winding.evenodd,winding.intersections);
    CHECK(status==QUANTAPDF_OK);
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&repeat));
    CHECK(native.pixels!=nullptr && repeat.pixels!=nullptr &&
          native.width==WIDTH && native.height==HEIGHT &&
          native.stride==WIDTH*4u && native.size==WIDTH*HEIGHT*4u &&
          repeat.size==native.size &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    /* No source/Graph objects remain; now also release the owned IR. */
    quantapdf_ir_dispose(&detached);

    struct sample {size_t x,y;unsigned r,g,b;};
    const sample goldens[]={
        {40u,40u,255u,0u,0u},       /* red line survives W* */
        {100u,40u,255u,255u,255u},  /* inner even-odd hole clips line */
        {200u,40u,255u,0u,0u},      /* red line outside hole */
        {85u,95u,0u,0u,255u},       /* nonzero star outer lobe */
        {80u,125u,0u,0u,255u},      /* nonzero star fills center */
        {185u,95u,0u,0u,255u},      /* even-odd star outer lobe */
        {180u,125u,255u,255u,255u}, /* even-odd star leaves center empty */
        {10u,10u,255u,255u,255u}    /* untouched page */
    };
    for(const sample& p:goldens) {
        const unsigned char *b=pdfium.at(p.x,p.y);
        const unsigned char *a=native.pixels+p.y*native.stride+4u*p.x;
        const unsigned want[]={p.r,p.g,p.b};
        for(size_t c=0u;c<3u;++c) {
            const int n=(int)on_white(a,c),r=(int)b[c],v=(int)want[c];
            if(std::abs(n-v)>8 || std::abs(r-v)>8)
                std::fprintf(stderr,
                    "[wstar-golden] x=%zu y=%zu c=%zu native=%d PDFium=%d expected=%d\n",
                    p.x,p.y,c,n,r,v);
            CHECK(std::abs(n-v)<=8 && std::abs(r-v)<=8);
        }
    }

    double total=0.0;
    size_t gross=0u,ref_red=0u,native_red=0u,overlap_red=0u;
    size_t ref_blue=0u,native_blue=0u,overlap_blue=0u;
    for(size_t y=0u;y<HEIGHT;++y)for(size_t x=0u;x<WIDTH;++x) {
        const unsigned char *a=native.pixels+y*native.stride+4u*x;
        const unsigned char *b=pdfium.at(x,y);
        unsigned rgb[3],worst=0u;
        for(size_t c=0u;c<3u;++c) {
            rgb[c]=on_white(a,c);
            const unsigned diff=rgb[c]>(unsigned)b[c]
                ? rgb[c]-(unsigned)b[c] : (unsigned)b[c]-rgb[c];
            total+=(double)diff;
            if(diff>worst)worst=diff;
        }
        if(worst>64u)++gross;
        const bool refR=b[0]>=190u && b[1]<=150u && b[2]<=150u;
        const bool natR=rgb[0]>=190u && rgb[1]<=150u && rgb[2]<=150u;
        const bool refB=b[2]>=190u && b[0]<=150u && b[1]<=150u;
        const bool natB=rgb[2]>=190u && rgb[0]<=150u && rgb[1]<=150u;
        ref_red+=(size_t)refR;native_red+=(size_t)natR;
        overlap_red+=(size_t)(refR&&natR);
        ref_blue+=(size_t)refB;native_blue+=(size_t)natB;
        overlap_blue+=(size_t)(refB&&natB);
    }
    const double mean=total/(double)(WIDTH*HEIGHT*3u);
    if(mean>0.05 || gross!=0u || ref_red<150u || native_red<150u ||
       ref_blue<2000u || native_blue<2000u)
        std::fprintf(stderr,
            "[wstar-parity] mean=%.6f gross=%zu red=%zu/%zu/%zu "
            "blue=%zu/%zu/%zu\n",mean,gross,ref_red,native_red,overlap_red,
            ref_blue,native_blue,overlap_blue);
    CHECK(mean<=0.05 && gross==0u);
    CHECK(ref_red>=150u && native_red>=150u &&
          overlap_red*100u>=ref_red*99u);
    CHECK(ref_blue>=2000u && native_blue>=2000u &&
          overlap_blue*100u>=ref_blue*99u);

    FILE *out=std::fopen(staged.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"candidate_wstar_pixel_parity_passed_not_admitted\",\n"
        "  \"document_id\": \"yworks.clippath-cliprule-v1\",\n"
        "  \"fixture_kind\": \"quarantined-external-original\",\n"
        "  \"original_source_pdfium_oracle\": \"passed\",\n"
        "  \"detached_c11_native_raster\": \"passed\",\n"
        "  \"source_and_graph_released_before_raster\": true,\n"
        "  \"official_corpus_admitted\": false,\n"
        "  \"page_width\": %zu,\n"
        "  \"page_height\": %zu,\n"
        "  \"native_painted_paths\": %zu,\n"
        "  \"native_w_nonzero_clip_path_leaves\": %zu,\n"
        "  \"native_wstar_evenodd_clip_path_leaves\": %zu,\n"
        "  \"native_clip_intersection_nodes\": %zu,\n"
        "  \"native_total_clip_nodes\": %zu,\n"
        "  \"fixed_interior_goldens\": %zu,\n"
        "  \"repeat_rgba_identical\": true,\n"
        "  \"mean_rgb_error\": %.6f,\n"
        "  \"gross_error_pixels\": %zu,\n"
        "  \"reference_red_pixels\": %zu,\n"
        "  \"native_red_pixels\": %zu,\n"
        "  \"overlap_red_pixels\": %zu,\n"
        "  \"reference_blue_pixels\": %zu,\n"
        "  \"native_blue_pixels\": %zu,\n"
        "  \"overlap_blue_pixels\": %zu,\n"
        "  \"external_wstar_pixel_supported_count\": 0,\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n",
        WIDTH,HEIGHT,painted_count,winding.nonzero,winding.evenodd,
        winding.intersections,clip_count,
        sizeof(goldens)/sizeof(goldens[0]),mean,gross,ref_red,native_red,
        overlap_red,ref_blue,native_blue,overlap_blue);
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(staged.c_str(),report_path)==0);
    CHECK(corpus_fixture_executions==0u);
    std::fprintf(stderr,
        "[wstar-original-pixel] passed mean=%.6f gross=%zu W=%zu Wstar=%zu "
        "official_positive=0\n",mean,gross,winding.nonzero,winding.evenodd);
    return EXIT_SUCCESS;
}


int main(int argc,char **argv)
{
    if(argc==2 && std::strcmp(argv[1],"--page-group-admission")==0){
        test_page_group_admission();
        return EXIT_SUCCESS;
    }
    if(argc==5 && std::strcmp(argv[1],"--external-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_cc0_text_rejection(argv[2],argv[4]);
    if(argc==5 && std::strcmp(argv[1],"--external-blank-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_cc0_blank_native_parity(argv[2],argv[4]);
    if(argc==5 && std::strcmp(argv[1],"--external-black-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_pdfium_black_path(argv[2],argv[4]);
    if(argc==5 && std::strcmp(argv[1],"--external-clip-stroke-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_pdfium_clip_stroke(argv[2],argv[4]);
    if(argc==5 && std::strcmp(argv[1],"--external-wstar-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_pdfium_wstar_late_text_rejection(argv[2],argv[4]);
    if(argc==5 && std::strcmp(argv[1],"--external-svg-w-candidate-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_yworks_svg_w_pixels(argv[2],argv[4]);
    if(argc==5 && std::strcmp(argv[1],"--external-wstar-candidate-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_yworks_wstar_pixels(argv[2],argv[4]);
    if(argc==7 && std::strcmp(argv[1],"--clip-causal-ab")==0 &&
       std::strcmp(argv[3],"--generated-prefix")==0 &&
       std::strcmp(argv[5],"--external-report")==0)
        return test_external_pdfium_clip_causal_ab(argv[2],argv[4],argv[6]);
    if(argc==5 && std::strcmp(argv[1],"--open-line-clip-controls")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_pdfium_open_line_clip_controls(argv[2],argv[4]);
    if(argc==5 && std::strcmp(argv[1],"--fractional-page-negative")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_fractional_page_negative(argv[2],argv[4]);
    const char *report_path = nullptr;
    if(argc != 1){
        CHECK(argc==3 && std::strcmp(argv[1],"--corpus-report")==0 &&
              argv[2]!=nullptr && *argv[2]!='\0');
        report_path=argv[2];
        const std::string staged=std::string(report_path)+".tmp";
        /* Never let a prior successful CTest run masquerade as a failed
         * run's coverage artifact on rerun or on Windows. */
        errno=0;
        if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
        errno=0;
        if(std::remove(staged.c_str())!=0) CHECK(errno==ENOENT);
    }
    constexpr size_t expected_families = 14u;
    constexpr size_t expected_fixtures = 36u;
    const size_t families =
        sizeof(source_corpus_families)/sizeof(source_corpus_families[0]);
    size_t fixture_total = 0u;
    size_t executed[expected_families] = {};
    CHECK(families == expected_families);
    for(size_t i=0u;i<families;++i){
        const source_corpus_family& entry=source_corpus_families[i];
        CHECK(entry.id && *entry.id && entry.features && *entry.features &&
              entry.expected && entry.oracle && entry.run);
        CHECK(entry.expected_fixtures>0u && entry.expected_fixtures<=256u);
        CHECK(std::strcmp(entry.expected,"accepted")==0 ||
              std::strcmp(entry.expected,"rejected")==0 ||
              std::strcmp(entry.expected,"mixed")==0);
        CHECK(std::strcmp(entry.oracle,"original-pdfium")==0 ||
              std::strcmp(entry.oracle,"native-only")==0 ||
              std::strcmp(entry.oracle,"status-only")==0);
        for(size_t j=0u;j<i;++j)
            CHECK(std::strcmp(entry.id,source_corpus_families[j].id)!=0);
        const size_t before=corpus_fixture_executions;
        entry.run();
        executed[i]=corpus_fixture_executions-before;
        CHECK(executed[i]==entry.expected_fixtures);
        fixture_total+=executed[i];
        std::fprintf(stderr,
            "[source-corpus] PASS id=%s expected=%s oracle=%s fixtures=%zu tags=%s\n",
            entry.id,entry.expected,entry.oracle,entry.expected_fixtures,
            entry.features);
    }
    CHECK(fixture_total==expected_fixtures &&
          corpus_fixture_executions==expected_fixtures);
    std::fprintf(stderr,
        "[source-corpus] complete families=%zu fixtures=%zu\n",
        families,fixture_total);
    if(report_path)
        publish_corpus_report(report_path,executed,families,fixture_total);
    return EXIT_SUCCESS;
}
