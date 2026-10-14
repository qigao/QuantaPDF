/* Test-only source-PDF q/Q conformance. PDFium is an independent oracle:
 * Original source PDF -> PDFium versus Source -> detached Graph -> owned IR
 * -> private pure-C11 scoped Clip DAG raster. Never export IR to PDF as a
 * production rendering intermediate and never invoke a PDFium fallback. */
#include "document/page_composer_ir.h"
#include "document/page_content_ir.h"
#include "graph/graph_page_geometry.h"
#include "render/ir_scoped_operations_v1.h"
#include "render/ir_scoped_device_grid_v0.h"
#include "render/ir_rect_fill_right_preview_v0.h"
#include "render/ir_device_polyline_stroke_v0.h"
#include "render/ir_ordered_layer_composite_v0.h"
#include "original_jspdf_diff_attribution.h"
#include "render/ir_clip_dag_v3.h"
#include "render/ir_raster.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
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
void make_source(const char *content,
                 const char *source_output=SOURCE_QQ_PDF)
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
    FILE *f = std::fopen(source_output,"wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(blob->getBuffer(),1,blob->getSize(),f) ==
          blob->getSize());
    CHECK(std::fclose(f) == 0);
}

quantapdf_status snapshot_lower(quantapdf_ir *out,
                                size_t page_index=0u,size_t max_streams=1u,
                                size_t max_parent_steps=2u,
                                const char *source_path=SOURCE_QQ_PDF,
                                const quantapdf_pdf_path_lower_limits *explicit_limits=&lower_limits)
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
        snap,page_index,max_parent_steps,&decode_limits,&parse_limits,max_streams,explicit_limits,out);
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

/* Page background in the source PDF is implicit transparent. PDFium's
 * RGB output is white-composited, whereas native IR returns straight RGBA8.
 * Compare the native value composited over white, without altering it. */
unsigned on_white(const unsigned char *rgba, size_t channel)
{
    unsigned alpha = rgba[3];
    return ((unsigned)rgba[channel]*alpha + 255u*(255u-alpha) + 127u)/255u;
}

/* Owned detached-IR Clip semantics are independent of any source-token
 * heuristic. A candidate W* must actually lower to an EVEN_ODD path leaf,
 * not silently become W nonzero, nor be counted from DAG intersections. */
struct clip_leaf_counts {
    size_t nonzero=0u,evenodd=0u,intersections=0u;
};
clip_leaf_counts inspect_clip_fill_rules(const quantapdf_ir& ir)
{
    clip_leaf_counts out{};
    /* Typed IR may hold more owned Clip nodes than the currently supported
     * private C11 pixel raster's maximum of eight. Do not conflate the two
     * admission gates or report native pixel parity from typed lowering. */
    CHECK(ir.resources.clip_count<=16u);
    CHECK(ir.resources.clip_count==0u || ir.resources.clips!=nullptr);
    for(size_t i=0u;i<ir.resources.clip_count;++i){
        const auto& item=ir.resources.clips[i];
        if(item.kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL){
            CHECK(item.fill_rule==QUANTAPDF_COMPOSER_FILL_NONZERO ||
                  item.fill_rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD);
            if(item.fill_rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD)
                ++out.evenodd;
            else
                ++out.nonzero;
        }else{
            CHECK(item.kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL);
            ++out.intersections;
        }
    }
    CHECK(out.nonzero+out.evenodd+out.intersections==
          ir.resources.clip_count);
    return out;
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
    /* Every generated original-PDFium vs native oracle with a nonempty
     * Clip graph must retain W vs W* on actual owned Clip Path leaves.
     * The zero-segment W/W* tests remain separate explicit semantics. */
    if(min_clip_resources>0u){
        const clip_leaf_counts rules=inspect_clip_fill_rules(ir);
        const bool source_wstar=std::strstr(content,"W* n")!=nullptr;
        CHECK(rules.nonzero>=1u);
        CHECK((rules.evenodd>0u)==source_wstar);
        /* A single W Clip legitimately has a direct leaf and no DAG
         * intersection. Require an intersection only for multi-Clip
         * generated q/Q cases (at least three registered resources). */
        if(min_clip_resources>=3u)
            CHECK(rules.intersections>=1u);
    }
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

/* This fixture is deliberately a late rejection, not a trivial early BT.
 * The entire prefix is identical to the independently PDFium-qualified
 * nested W/W* three-paint fixture in test_evenodd_restore(). Unsupported
 * text must reject atomically even after Clip DAG and PATH resources could
 * otherwise have been lowered. One generated PDF: 14/36 stays unchanged. */
void test_unsupported_operator_fails_closed()
{
    track_corpus_fixture();
    quantapdf_ir failed{};
    make_source(
        "q 0 0 100 100 re W n "
        "q 20 20 60 60 re 35 35 20 20 re W* n "
        "1 0 0 rg 0 0 120 120 re f "
        "Q 0 0 1 rg 75 10 40 40 re f "
        "Q 0 1 0 rg 0 0 12 12 re f "
        "q BT ET Q");
    quantapdf_status st=snapshot_lower(&failed);
    CHECK(st==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(failed.pages==nullptr && failed.page_count==0u &&
          failed.operations==nullptr && failed.operation_count==0u &&
          failed.resources.clips==nullptr &&
          failed.resources.clip_count==0u &&
          failed.resources.graphics_states==nullptr &&
          failed.resources.graphics_state_count==0u);
    quantapdf_ir_dispose(&failed);
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
    quantapdf_document *source_pdfium=nullptr;
    quantapdf_page *source_page=nullptr;
    quantapdf_bitmap *source_bitmap=nullptr;
    const unsigned char *rgb=nullptr;
    size_t image_bytes=0u;
    int width=0,height=0,stride=0,components=0;
    OK(quantapdf_open(filename,nullptr,&source_pdfium));
    OK(quantapdf_load_page(source_pdfium,0u,&source_page));
    OK(quantapdf_render_page(source_page,&source_bitmap));
    OK(quantapdf_bitmap_dimensions(source_bitmap,
        &width,&height,&stride,&components));
    OK(quantapdf_bitmap_data(source_bitmap,&rgb,&image_bytes));
    CHECK(width==(int)WIDTH && height==(int)HEIGHT &&
          components==3 && stride>=(int)WIDTH*3 && rgb &&
          image_bytes>=HEIGHT*(size_t)stride);

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
    quantapdf_drop_bitmap(source_bitmap);
    quantapdf_drop_page(source_page);
    quantapdf_close(source_pdfium);
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
        "  \"native_w_clip_supported_count\": 1,\n"
        "  \"native_wstar_clip_supported_count\": 0,\n"
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

/* Published original PDFium W* n + f* then late Text. Test-only PDFium
 * must visibly render original source; native detached IR rejects atomically.
 * This is UNSUPPORTED conformance, never native W* pixel compatibility. */
int test_external_pdfium_wstar_text_rejection(const char *filename,
                                              const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    const std::string pending=std::string(report_path)+".tmp";
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
    errno=0;
    if(std::remove(pending.c_str())!=0) CHECK(errno==ENOENT);
    CHECK(corpus_fixture_executions==0u);
    const rgb_image reference=render_original_pdfium(0u,filename);
    CHECK(reference.width==200 && reference.height==300 &&
          reference.components==3 && reference.stride>=600);
    size_t ink=0u;
    for(size_t y=0u;y<300u;++y)for(size_t x=0u;x<200u;++x){
        const unsigned char *p=reference.at(x,y);
        ink+=(size_t)(p[0]<200u || p[1]<200u || p[2]<200u);
    }
    CHECK(ink>=8u);
    quantapdf_ir rejected{};
    CHECK(snapshot_lower(&rejected,0u,1u,2u,filename)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(rejected.pages==nullptr && rejected.page_count==0u &&
          rejected.operations==nullptr && rejected.operation_count==0u &&
          rejected.resources.clips==nullptr &&
          rejected.resources.clip_count==0u &&
          rejected.resources.graphics_states==nullptr &&
          rejected.resources.graphics_state_count==0u &&
          rejected.resources.font_count==0u &&
          rejected.resources.image_count==0u &&
          rejected.resources.form_count==0u &&
          rejected.resources.soft_mask_count==0u);
    quantapdf_ir_dispose(&rejected);
    CHECK(corpus_fixture_executions==0u);
    FILE *report=std::fopen(pending.c_str(),"wb");
    CHECK(report!=nullptr);
    std::fprintf(report,R"JSON({
  "schema_version": 1,
  "corpus_id": "external-source-pdf",
  "document_id": "pdfium.bug-1574-wstar-text-v1",
  "fixture_kind": "external",
  "reference_oracle": "original-pdfium",
  "native_expected_outcome": "unsupported",
  "native_observed_outcome": "unsupported",
  "pdfium_result": "rendered_nonblank",
  "native_ir": "empty",
  "page_width": 200,
  "page_height": 300,
  "reference_ink_pixels": %zu,
  "status_conformance_passed_count": 1,
  "native_supported_passed_count": 0,
  "native_path_clip_supported_count": 0,
  "native_w_clip_supported_count": 0,
  "native_wstar_clip_supported_count": 0,
  "generated_fixture_count": 0,
  "result": "passed"
})JSON"
    "\n",ink);
    CHECK(std::ferror(report)==0 && std::fclose(report)==0);
    CHECK(std::rename(pending.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[external-corpus] id=pdfium.bug-1574-wstar-text-v1 "
        "pdfium=nonblank native=unsupported ir=empty "
        "wstar-positive=0 ink=%zu\n",ink);
    return EXIT_SUCCESS;
}

/* Original yWorks MIT PDF, unchanged source byte stream, candidate-only
 * pixel parity. No corpus admission count until exact-head three-platform
 * evidence + review. Uses independent PDFium page -> RGB and owned detached
 * Graph -> pure C11 Raster, with no fallback or Composer export to PDF. */
/* Candidate-only original yWorks W n nested Clip pixel oracle.
 * The independently published source has one visible red Path under
 * two original W clips, plus source-side hidden/empty Clip controls.
 * This is NOT official six-source corpus admission and NOT a W* positive. */
/* Quarantined original yWorks 500x800 W-only nested-Clip page. The entire
 * actual original is rendered independently through PDFium; the private C11
 * raster consumes only owned detached Source->Graph->Composer IR after both
 * source/Graph objects are disposed. No QPDF export and no PDFium fallback.
 * Six painted Paths, five W Clip leaves and four Clip intersection nodes. */
/* Original published jsPDF A4 W n selected page. Full typed Source->Graph
 * lowering is supported, while private C11 pixel raster deliberately has
 * no fractional-point PDF-to-device grid contract yet (#269). Do not
 * silently truncate, round or rewrite the original MediaBox to pass. */

/* Fourth PRIVATE original-PDF A/B is a separate CTest process: the #287
 * historical page-negative test takes ~38 seconds under ASan/UBSan and
 * MUST keep its unchanged 40-second timeout. This independently lowers
 * the same immutable source and compares ALL 596x842 original PDFium RGB
 * pixels, without rerunning the old modes 0/1/2 or changing their paths. */
int test_external_jspdf_single_vertical_analytic_ab(
    const char *filename, const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    namespace fs=std::filesystem;
    std::error_code ec;
    const fs::path source(filename), report(report_path);
    const std::string pending=std::string(report_path)+".tmp";
    CHECK(fs::is_regular_file(source,ec) && !ec);
    CHECK(!fs::is_symlink(source,ec) && !ec);
    const fs::path pinned=fs::weakly_canonical(source,ec);
    CHECK(!ec && fs::weakly_canonical(report,ec)!=pinned && !ec);
    CHECK(fs::weakly_canonical(fs::path(pending),ec)!=pinned && !ec);
    for(const fs::path& candidate:{report,fs::path(pending)}) {
        const bool exists=fs::exists(candidate,ec);
        CHECK(!ec);
        if(exists) {
            CHECK(!fs::is_symlink(candidate,ec) && !ec);
            CHECK(!fs::equivalent(source,candidate,ec) && !ec);
        }
    }
    CHECK(corpus_fixture_executions==0u);
    quantapdf_ir detached{};
    OK(snapshot_lower(&detached,0u,4u,8u,filename));
    CHECK(detached.page_count==1u && detached.operation_count==2u &&
          detached.pages!=nullptr && detached.operations!=nullptr &&
          detached.pages[0].width_points==(float)595.28 &&
          detached.pages[0].height_points==(float)841.89 &&
          detached.pages[0].suppress_background==1 &&
          detached.operations[0].value.path.options.stroke==1 &&
          detached.operations[0].value.path.options.fill==0 &&
          detached.operations[1].value.path.options.stroke==0 &&
          detached.operations[1].value.path.options.fill==1 &&
          detached.resources.clip_count==1u);
    const rgb_image reference=render_original_pdfium(0u,filename);
    CHECK(reference.width==596 && reference.height==842 &&
          reference.components==3 && reference.stride>=596*3 &&
          reference.pixels.size()>=(size_t)reference.stride*842u);
    const quantapdf_native_device_grid_geometry geometry={
        595.28,841.89,0.0,0.0,1.0,0,0,72.0f
    };
    /* New mode 4 charges count*4 full-page edge work, separately from
     * mode 3's count*3. Raising only THIS opt-in test-local limit does
     * not change admission of the old original or any prior previews. */
    const quantapdf_native_device_grid_limits stroke_grid={
        600u*850u,600u*850u*4u,768u,300000000u
    };
    const quantapdf_ir_raster_v1_limits stroke_budget={
        600u*850u,1u,768u,300000000u
    };
    const quantapdf_native_device_grid_limits fill_grid={
        600u*850u,600u*850u*4u,768u,160000000u
    };
    const quantapdf_ir_raster_v1_limits fill_budget={
        600u*850u,1u,768u,160000000u
    };
    const quantapdf_ir_ordered_composite_v0_limits composite_budget={
        600u*850u,600u*850u*4u
    };
    quantapdf_ir only_stroke=detached;
    only_stroke.operations=&detached.operations[0];
    only_stroke.operation_count=1u;
    only_stroke.resources=quantapdf_resource_registry{};
    only_stroke.links=nullptr;
    only_stroke.link_count=0u;
    only_stroke.outlines=nullptr;
    only_stroke.outline_count=0u;
    quantapdf_ir only_fill=detached;
    only_fill.operations=&detached.operations[1];
    only_fill.operation_count=1u;

    quantapdf_ir_raster_image baseline_stroke{},analytic_stroke{},repeat_stroke{};
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &only_stroke,0u,&geometry,&stroke_grid,&stroke_budget,&baseline_stroke));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &only_stroke,0u,&geometry,&stroke_grid,&stroke_budget,&analytic_stroke));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
        &only_stroke,0u,&geometry,&stroke_grid,&stroke_budget,&repeat_stroke));
    CHECK(baseline_stroke.pixels && analytic_stroke.pixels && repeat_stroke.pixels &&
          baseline_stroke.width==596u && baseline_stroke.height==842u &&
          baseline_stroke.size==596u*842u*4u &&
          baseline_stroke.stride==596u*4u &&
          analytic_stroke.size==baseline_stroke.size &&
          analytic_stroke.stride==baseline_stroke.stride &&
          repeat_stroke.size==baseline_stroke.size &&
          std::memcmp(analytic_stroke.pixels,repeat_stroke.pixels,
                      analytic_stroke.size)==0);
    quantapdf_ir_raster_image fill{};
    OK(quantapdf_ir_raster_scoped_device_grid_v0(
        &only_fill,0u,&geometry,&fill_grid,&fill_budget,&fill));
    CHECK(fill.pixels && fill.size==baseline_stroke.size);
    quantapdf_ir_raster_image base_page{},trial_page{},repeat_page{};
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &baseline_stroke,&fill,&composite_budget,&base_page));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &analytic_stroke,&fill,&composite_budget,&trial_page));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &repeat_stroke,&fill,&composite_budget,&repeat_page));
    CHECK(base_page.pixels && trial_page.pixels && repeat_page.pixels &&
          base_page.size==baseline_stroke.size &&
          trial_page.size==base_page.size &&
          repeat_page.size==base_page.size &&
          std::memcmp(trial_page.pixels,repeat_page.pixels,
                      trial_page.size)==0);

    struct page_metrics {
        std::uint64_t abs_rgb=0u;
        std::uint64_t fnv=14695981039346656037ULL;
        size_t changed=0u,gross=0u;
        unsigned max_delta=0u;
    };
    const auto measure=[&](const quantapdf_ir_raster_image &image){
        page_metrics metric;
        for(size_t y=0u;y<842u;++y)for(size_t x=0u;x<596u;++x) {
            const unsigned char *pixel=image.pixels+y*image.stride+x*4u;
            const unsigned char *truth=reference.at(x,y);
            unsigned worst=0u;
            for(size_t c=0u;c<3u;++c) {
                const unsigned val=on_white(pixel,c),expected=truth[c];
                const unsigned d=val>=expected?val-expected:expected-val;
                metric.abs_rgb+=(std::uint64_t)d;
                if(d>worst)worst=d;
                metric.fnv^=(std::uint64_t)val;
                metric.fnv*=1099511628211ULL;
            }
            if(worst)++metric.changed;
            if(worst>64u)++metric.gross;
            if(worst>metric.max_delta)metric.max_delta=worst;
        }
        CHECK(metric.changed<=501832u && metric.gross<=metric.changed &&
              metric.abs_rgb<=383901480u && metric.max_delta<=255u);
        return metric;
    };
    const page_metrics old=measure(base_page);
    const page_metrics candidate=measure(trial_page);

    /* Fifth PRIVATE original A/B: only single-pass source-horizontal
     * interiors, still using the genuine first Stroke and unchanged later
     * W Clip Fill from the same owned Detached IR and raw source PDFium. */
    const quantapdf_native_device_grid_limits h_grid={
        600u*850u,600u*850u*4u,768u,450000000u
    };
    const quantapdf_ir_raster_v1_limits h_budget={
        600u*850u,1u,768u,450000000u
    };
    quantapdf_ir_raster_image h_stroke{},h_repeat{},h_page{},h_page_repeat{};
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &only_stroke,0u,&geometry,&h_grid,&h_budget,&h_stroke));
    OK(quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
        &only_stroke,0u,&geometry,&h_grid,&h_budget,&h_repeat));
    CHECK(h_stroke.pixels && h_repeat.pixels &&
          h_stroke.size==analytic_stroke.size &&
          h_stroke.stride==analytic_stroke.stride &&
          h_repeat.size==h_stroke.size &&
          std::memcmp(h_stroke.pixels,h_repeat.pixels,h_stroke.size)==0);
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &h_stroke,&fill,&composite_budget,&h_page));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &h_repeat,&fill,&composite_budget,&h_page_repeat));
    CHECK(h_page.pixels && h_page_repeat.pixels &&
          h_page.size==trial_page.size &&
          h_page_repeat.size==h_page.size &&
          std::memcmp(h_page.pixels,h_page_repeat.pixels,h_page.size)==0);
    const page_metrics horizontal=measure(h_page);

    /* Sixth test-only A/B: the effective red right boundary belongs to
     * the later FILL rectangle, NOT the preceding W clip's right edge.
     * Admit actual source rectangle geometry and compare an explicit
     * C11 one-edge coverage trial, never a PDFium fallback. */
    quantapdf_ir_raster_image right_fill{},right_repeat{},right_page{},
                              right_page_repeat{};
    OK(quantapdf_ir_raster_rect_fill_right_preview_v0(
        &only_fill,0u,&geometry,&fill_grid,&fill_budget,&right_fill));
    OK(quantapdf_ir_raster_rect_fill_right_preview_v0(
        &only_fill,0u,&geometry,&fill_grid,&fill_budget,&right_repeat));
    CHECK(right_fill.pixels && right_repeat.pixels &&
          right_fill.size==fill.size &&
          right_fill.stride==fill.stride &&
          right_repeat.size==right_fill.size &&
          std::memcmp(right_fill.pixels,right_repeat.pixels,
                      right_fill.size)==0);
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &h_stroke,&right_fill,&composite_budget,&right_page));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &h_repeat,&right_repeat,&composite_budget,&right_page_repeat));
    CHECK(right_page.pixels && right_page_repeat.pixels &&
          right_page.size==h_page.size &&
          right_page_repeat.size==right_page.size &&
          std::memcmp(right_page.pixels,right_page_repeat.pixels,
                      right_page.size)==0);
    const page_metrics right_metric=measure(right_page);

    /* Seventh EXPLICIT PRIVATE A/B: source-owned left nonzero W Clip
     * partial pixel, deliberately after the independent Fill-right pass.
     * The immutable original first Stroke, red Fill order, raw PDFium
     * reference and all previous preview entrypoints stay identical. */
    quantapdf_ir_raster_image left_fill{},left_repeat{},left_page{},
                              left_page_repeat{};
    OK(quantapdf_ir_raster_rect_fill_left_clip_preview_v0(
        &only_fill,0u,&geometry,&fill_grid,&fill_budget,&left_fill));
    OK(quantapdf_ir_raster_rect_fill_left_clip_preview_v0(
        &only_fill,0u,&geometry,&fill_grid,&fill_budget,&left_repeat));
    CHECK(left_fill.pixels && left_repeat.pixels &&
          left_fill.size==right_fill.size &&
          left_fill.stride==right_fill.stride &&
          left_repeat.size==left_fill.size &&
          std::memcmp(left_fill.pixels,left_repeat.pixels,
                      left_fill.size)==0);
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &h_stroke,&left_fill,&composite_budget,&left_page));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &h_repeat,&left_repeat,&composite_budget,&left_page_repeat));
    CHECK(left_page.pixels && left_page_repeat.pixels &&
          left_page.size==right_page.size &&
          left_page_repeat.size==left_page.size &&
          std::memcmp(left_page.pixels,left_page_repeat.pixels,
                      left_page.size)==0);
    const page_metrics left_metric=measure(left_page);

    /* New exact-original mode7 read-only attribution. The SOURCE pinned
     * windows are test-only labels, never C11 admission or rendering rules.
     * Every 596*842 pixel is assigned to one and only one group; the
     * genuine PDFium oracle is referenced directly, never re-exported. */
    struct mode7_region {
        const char *name;
        size_t pixels=0u,changed=0u,gross=0u,worst_x=0u,worst_y=0u;
        std::uint64_t error=0u,brighter=0u,darker=0u;
        unsigned max_delta=0u;
    };
    mode7_region groups[]={
        {"bottom_left_endpoint"},{"bottom_right_endpoint"},
        {"bottom_single_horizontal_interior"},
        {"top_repeated_left_endpoint"},{"top_repeated_right_endpoint"},
        {"top_repeated_horizontal_interior"},
        {"right_vertical_stroke"},{"left_vertical_below_red"},
        {"red_left_w_clip_stroke_overlap"},{"red_left_w_clip_only"},
        {"red_right_fill_boundary"},{"red_top_right_coupled_corner"},
        {"red_top_stroke_overlap"},{"red_fill_bottom_edge"},
        {"red_bottom_uncovered_fringe"},{"other"}
    };
    const auto group_of=[](size_t x,size_t y,
                           unsigned stroke_alpha,unsigned red_alpha)->size_t {
        if(x>=49u&&x<=51u&&y>=138u&&y<=140u&&
           stroke_alpha!=0u&&red_alpha==0u)return 0u;
        if(x>=249u&&x<=251u&&y>=138u&&y<=140u&&
           stroke_alpha!=0u&&red_alpha==0u)return 1u;
        if(x>=52u&&x<=248u&&y>=138u&&y<=140u&&
           stroke_alpha!=0u&&red_alpha==0u)return 2u;
        if(x>=49u&&x<=51u&&y>=19u&&y<=21u&&
           stroke_alpha!=0u&&red_alpha==0u)return 3u;
        if(x>=249u&&x<=251u&&y>=19u&&y<=21u&&
           stroke_alpha!=0u&&red_alpha==0u)return 4u;
        if(x>=52u&&x<=248u&&y>=19u&&y<=21u&&
           stroke_alpha!=0u&&red_alpha==0u)return 5u;
        if(x>=249u&&x<=251u&&y>=22u&&y<=137u&&
           stroke_alpha!=0u&&red_alpha==0u)return 6u;
        if(x>=49u&&x<=51u&&y>=101u&&y<=137u&&
           stroke_alpha!=0u&&red_alpha==0u)return 7u;
        if(x>=49u&&x<=51u&&y>=21u&&y<=99u&&
           stroke_alpha!=0u&&red_alpha!=0u)return 8u;
        if(x>=49u&&x<=51u&&y>=21u&&y<=99u&&
           stroke_alpha==0u&&red_alpha!=0u)return 9u;
        if(x==150u&&y>=21u&&y<=99u&&
           stroke_alpha==0u&&red_alpha!=0u)return 10u;
        if(x==150u&&y>=19u&&y<=21u&&red_alpha!=0u)
            return 11u;
        if(x>=50u&&x<=149u&&y>=19u&&y<=21u&&
           red_alpha!=0u&&stroke_alpha!=0u)return 12u;
        if(x>=50u&&x<=150u&&y>=99u&&y<=100u&&
           red_alpha!=0u)return 13u;
        if(x>=52u&&x<=149u&&y==100u&&
           stroke_alpha==0u&&red_alpha==0u)return 14u;
        return 15u;
    };
    struct worst_point {
        size_t x,y,region;
        unsigned max_delta,rgb_error,stroke_alpha,fill_alpha;
        unsigned char native_rgb[3],pdfium_rgb[3];
    };
    std::vector<worst_point> mode7_worst;
    mode7_worst.reserve(16u);
    for(size_t y=0u;y<842u;++y)for(size_t x=0u;x<596u;++x) {
        const size_t off=y*h_stroke.stride+x*4u;
        const unsigned sa=h_stroke.pixels[off+3u];
        const unsigned fa=left_fill.pixels[
            y*left_fill.stride+x*4u+3u];
        const size_t region_id=group_of(x,y,sa,fa);
        CHECK(region_id<sizeof(groups)/sizeof(groups[0]));
        mode7_region &group=groups[region_id];
        const unsigned char *native_pixel=
            left_page.pixels+y*left_page.stride+x*4u;
        const unsigned char *pdfium=reference.at(x,y);
        worst_point p{};
        p.x=x;p.y=y;p.region=region_id;
        p.stroke_alpha=sa;p.fill_alpha=fa;
        ++group.pixels;
        unsigned pixel_max=0u,pixel_error=0u;
        for(size_t ch=0u;ch<3u;++ch) {
            const unsigned actual=on_white(native_pixel,ch);
            const unsigned oracle=pdfium[ch];
            const unsigned delta=
                actual>=oracle?actual-oracle:oracle-actual;
            group.error+=(std::uint64_t)delta;
            pixel_error+=delta;
            if(actual>oracle)group.brighter+=(std::uint64_t)delta;
            if(actual<oracle)group.darker+=(std::uint64_t)delta;
            if(delta>pixel_max)pixel_max=delta;
            p.native_rgb[ch]=(unsigned char)actual;
            p.pdfium_rgb[ch]=(unsigned char)oracle;
        }
        if(pixel_max==0u)continue;
        ++group.changed;
        if(pixel_max>64u)++group.gross;
        if(pixel_max>group.max_delta) {
            group.max_delta=pixel_max;
            group.worst_x=x;
            group.worst_y=y;
        }
        p.max_delta=pixel_max;
        p.rgb_error=pixel_error;
        const auto worse=[](const worst_point &a,const worst_point &b){
            if(a.max_delta!=b.max_delta)
                return a.max_delta>b.max_delta;
            if(a.rgb_error!=b.rgb_error)
                return a.rgb_error>b.rgb_error;
            if(a.y!=b.y)return a.y<b.y;
            return a.x<b.x;
        };
        size_t index=0u;
        while(index<mode7_worst.size() &&
              !worse(p,mode7_worst[index]))++index;
        if(index<16u) {
            mode7_worst.insert(
                mode7_worst.begin()+(std::ptrdiff_t)index,p);
            if(mode7_worst.size()>16u)mode7_worst.pop_back();
        }
    }
    size_t mode7_pixels=0u,mode7_changed=0u,mode7_gross=0u;
    std::uint64_t mode7_error=0u,mode7_brighter=0u,mode7_darker=0u;
    unsigned mode7_max=0u;
    std::string mode7_regions_json=
        "{\"schema_version\":1,\"observation_only\":true,"
        "\"pixel_parity_qualified\":false,\"regions\":{";
    for(size_t i=0u;i<sizeof(groups)/sizeof(groups[0]);++i) {
        const mode7_region &r=groups[i];
        CHECK(r.changed<=r.pixels && r.gross<=r.changed &&
              r.error==r.brighter+r.darker);
        mode7_pixels+=r.pixels;
        mode7_changed+=r.changed;
        mode7_gross+=r.gross;
        mode7_error+=r.error;
        mode7_brighter+=r.brighter;
        mode7_darker+=r.darker;
        if(r.max_delta>mode7_max)mode7_max=r.max_delta;
        if(i)mode7_regions_json+=",";
        mode7_regions_json+="\""+std::string(r.name)+"\":{";
        mode7_regions_json+="\"pixels\":"+std::to_string(r.pixels)+",";
        mode7_regions_json+="\"changed\":"+std::to_string(r.changed)+",";
        mode7_regions_json+="\"gross\":"+std::to_string(r.gross)+",";
        mode7_regions_json+="\"abs_rgb_error\":"+
                            std::to_string(r.error)+",";
        mode7_regions_json+="\"brighter_rgb_error\":"+
                            std::to_string(r.brighter)+",";
        mode7_regions_json+="\"darker_rgb_error\":"+
                            std::to_string(r.darker)+",";
        mode7_regions_json+="\"max_channel_delta\":"+
                            std::to_string(r.max_delta)+",";
        mode7_regions_json+="\"worst_x\":"+std::to_string(r.worst_x)+",";
        mode7_regions_json+="\"worst_y\":"+std::to_string(r.worst_y)+"}";
    }
    CHECK(mode7_pixels==501832u &&
          mode7_changed==left_metric.changed &&
          mode7_gross==left_metric.gross &&
          mode7_error==left_metric.abs_rgb &&
          mode7_error==mode7_brighter+mode7_darker &&
          mode7_max==left_metric.max_delta &&
          !mode7_worst.empty() &&
          mode7_worst[0].max_delta==left_metric.max_delta);
    mode7_regions_json+="},\"worst_pixels\":[";
    for(size_t i=0u;i<mode7_worst.size();++i) {
        const worst_point &p=mode7_worst[i];
        if(i)mode7_regions_json+=",";
        mode7_regions_json+="{\"x\":"+std::to_string(p.x)+",";
        mode7_regions_json+="\"y\":"+std::to_string(p.y)+",";
        mode7_regions_json+="\"region\":\""+
            std::string(groups[p.region].name)+"\",";
        mode7_regions_json+="\"max_channel_delta\":"+
            std::to_string(p.max_delta)+",";
        mode7_regions_json+="\"rgb_error\":"+
            std::to_string(p.rgb_error)+",";
        mode7_regions_json+="\"stroke_alpha\":"+
            std::to_string(p.stroke_alpha)+",";
        mode7_regions_json+="\"fill_alpha\":"+
            std::to_string(p.fill_alpha)+",";
        mode7_regions_json+="\"native_rgb\":["+
            std::to_string((unsigned)p.native_rgb[0])+","+
            std::to_string((unsigned)p.native_rgb[1])+","+
            std::to_string((unsigned)p.native_rgb[2])+"],";
        mode7_regions_json+="\"pdfium_rgb\":["+
            std::to_string((unsigned)p.pdfium_rgb[0])+","+
            std::to_string((unsigned)p.pdfium_rgb[1])+","+
            std::to_string((unsigned)p.pdfium_rgb[2])+"]}";
    }
    mode7_regions_json+="],\"witnesses\":[";
    struct witness {const char *name;size_t x,y;};
    const witness witnesses[]={
        {"bottom_outer_left",49u,139u},
        {"bottom_left_join",50u,139u},
        {"bottom_interior",100u,139u},
        {"bottom_right_join",250u,139u},
        {"bottom_right_upper",250u,138u},
        {"top_outer_left",49u,20u},
        {"top_right_corner",250u,19u},
        {"right_vertical",250u,60u},
        {"left_vertical_lower",50u,110u},
        {"red_left_overlap",50u,40u},
        {"left_outer_stroke",49u,40u},
        {"red_right",150u,40u},
        {"red_top_right_corner",150u,20u},
        {"red_bottom_interior",100u,99u},
        {"red_bottom_fringe",100u,100u},
        {"red_top_interior",100u,20u}
    };
    for(size_t i=0u;i<sizeof(witnesses)/sizeof(witnesses[0]);++i) {
        const witness &w=witnesses[i];
        const unsigned char *stroke=h_stroke.pixels+
            w.y*h_stroke.stride+w.x*4u;
        const unsigned char *red=left_fill.pixels+
            w.y*left_fill.stride+w.x*4u;
        const unsigned char *native=left_page.pixels+
            w.y*left_page.stride+w.x*4u;
        const unsigned char *oracle=reference.at(w.x,w.y);
        const size_t id=group_of(w.x,w.y,stroke[3],red[3]);
        CHECK(id<sizeof(groups)/sizeof(groups[0]));
        if(i)mode7_regions_json+=",";
        mode7_regions_json+="{\"name\":\""+std::string(w.name)+"\",";
        mode7_regions_json+="\"x\":"+std::to_string(w.x)+",";
        mode7_regions_json+="\"y\":"+std::to_string(w.y)+",";
        mode7_regions_json+="\"region\":\""+
            std::string(groups[id].name)+"\",";
        mode7_regions_json+="\"stroke_alpha\":"+
            std::to_string((unsigned)stroke[3])+",";
        mode7_regions_json+="\"fill_alpha\":"+
            std::to_string((unsigned)red[3])+",";
        mode7_regions_json+="\"native_rgb\":["+
            std::to_string(on_white(native,0u))+","+
            std::to_string(on_white(native,1u))+","+
            std::to_string(on_white(native,2u))+"],";
        mode7_regions_json+="\"pdfium_rgb\":["+
            std::to_string((unsigned)oracle[0])+","+
            std::to_string((unsigned)oracle[1])+","+
            std::to_string((unsigned)oracle[2])+"]}";
    }
    mode7_regions_json+="],\"full_original_reconciled\":true}";
    std::fprintf(stderr,
        "[jsPDF mode7 original source endpoint attribution, NOT parity] %s\n",
        mode7_regions_json.c_str());

    size_t left_alpha_changed=0u,left_outside=0u;
    for(size_t y=0u;y<842u;++y)for(size_t x=0u;x<596u;++x) {
        const size_t off=y*right_fill.stride+x*4u;
        if(right_fill.pixels[off+3u]==left_fill.pixels[off+3u])
            continue;
        ++left_alpha_changed;
        if(x!=50u || y<21u || y>99u)
            ++left_outside;
        CHECK(std::memcmp(right_fill.pixels+off,
                          left_fill.pixels+off,3u)==0);
    }
    CHECK(left_alpha_changed>0u && left_outside==0u);
    const unsigned char *left_site=
        left_page.pixels+40u*left_page.stride+50u*4u;
    const unsigned char *left_truth=reference.at(50u,40u);
    std::fprintf(stderr,
        "[jsPDF original nonzero W LEFT border A/B, NOT parity] "
        "mode6_rgb=%llu left_rgb=%llu changed=%zu gross=%zu "
        "source_alpha_changes=%zu out_of_scope=%zu "
        "left_native=(%u,%u,%u) source_pdfium=(%u,%u,%u) fnv64=%016llx\n",
        (unsigned long long)right_metric.abs_rgb,
        (unsigned long long)left_metric.abs_rgb,
        left_metric.changed,left_metric.gross,
        left_alpha_changed,left_outside,
        on_white(left_site,0u),on_white(left_site,1u),
        on_white(left_site,2u),
        (unsigned)left_truth[0],(unsigned)left_truth[1],
        (unsigned)left_truth[2],
        (unsigned long long)left_metric.fnv);

    size_t right_alpha_changes=0u,right_outside=0u;
    for(size_t y=0u;y<842u;++y)for(size_t x=0u;x<596u;++x) {
        const size_t off=y*fill.stride+x*4u;
        if(fill.pixels[off+3u]==right_fill.pixels[off+3u])
            continue;
        ++right_alpha_changes;
        if(x!=150u || y<21u || y>99u)
            ++right_outside;
        CHECK(std::memcmp(fill.pixels+off,right_fill.pixels+off,3u)==0);
    }
    CHECK(right_alpha_changes>0u && right_outside==0u);
    const unsigned char *edge_pixel=
        right_page.pixels+40u*right_page.stride+150u*4u;
    const unsigned char *edge_oracle=reference.at(150u,40u);
    std::fprintf(stderr,
        "[jsPDF original FILL-right analytic A/B, NOT parity] "
        "mode5_rgb=%llu right_rgb=%llu changed=%zu gross=%zu "
        "source_alpha_changes=%zu corner_exceptions=%zu "
        "edge_native=(%u,%u,%u) raw_pdfium=(%u,%u,%u) fnv64=%016llx\n",
        (unsigned long long)horizontal.abs_rgb,
        (unsigned long long)right_metric.abs_rgb,
        right_metric.changed,right_metric.gross,
        right_alpha_changes,right_outside,
        on_white(edge_pixel,0u),on_white(edge_pixel,1u),
        on_white(edge_pixel,2u),
        (unsigned)edge_oracle[0],(unsigned)edge_oracle[1],
        (unsigned)edge_oracle[2],
        (unsigned long long)right_metric.fnv);

    /* Only a READ-ONLY post-right-Fill-original audit. Source geometry
     * windows are pinned to this immutable jsPDF, never native renderer
     * recognition rules or PDFium implementation assumptions.
     * Every one of 596*842 original source pixels belongs to exactly
     * ONE region, including all "other" pixels and uncovered fringes. */
    struct final_region {
        const char *name;
        size_t pixels=0u,changed=0u,gross=0u,worst_x=0u,worst_y=0u;
        std::uint64_t abs_rgb=0u,brighter=0u,darker=0u;
        unsigned max_delta=0u;
    };
    final_region last_regions[]={
        {"bottom_single_horizontal_stroke"},
        {"right_vertical_stroke"},
        {"left_vertical_below_red"},
        {"top_repeated_stroke"},
        {"red_fill_right_boundary"},
        {"red_left_shared_stroke_clip"},
        {"red_left_clip_only"},
        {"red_top_stroke_shared"},
        {"red_fill_bottom_boundary"},
        {"red_bottom_uncovered_fringe"},
        {"other"}
    };
    const auto classify_last=[](size_t x,size_t y,
                                unsigned sa,unsigned fa)->size_t {
        if(x>=49u && x<=250u && y>=138u && y<=140u &&
           sa!=0u && fa==0u) return 0u;
        if(x>=249u && x<=251u && y>=22u && y<=137u &&
           sa!=0u && fa==0u) return 1u;
        if(x>=49u && x<=51u && y>=101u && y<=137u &&
           sa!=0u && fa==0u) return 2u;
        if(x>=49u && x<=250u && y>=19u && y<=21u &&
           sa!=0u && fa==0u) return 3u;
        if(x==150u && y>=21u && y<=99u &&
           sa==0u && fa!=0u) return 4u;
        if(x>=49u && x<=51u && y>=21u && y<=99u &&
           fa!=0u && sa!=0u) return 5u;
        if(x>=49u && x<=51u && y>=21u && y<=99u &&
           fa!=0u && sa==0u) return 6u;
        if(x>=52u && x<=149u && y>=19u && y<=21u &&
           fa!=0u) return 7u;
        if(x>=52u && x<=149u && y>=99u && y<=100u &&
           fa!=0u) return 8u;
        if(x>=52u && x<=148u && y==100u &&
           sa==0u && fa==0u) return 9u;
        return 10u;
    };
    struct worst_site {
        size_t x,y,category;
        unsigned channel_delta,rgb_error,stroke_alpha,fill_alpha;
        unsigned char native[3],reference[3];
    };
    std::vector<worst_site> last_worst;
    last_worst.reserve(12u);
    for(size_t y=0u;y<842u;++y) for(size_t x=0u;x<596u;++x) {
        const size_t so=y*h_stroke.stride+x*4u;
        const unsigned sa=h_stroke.pixels[so+3u];
        const unsigned fa=right_fill.pixels[
            y*right_fill.stride+x*4u+3u];
        const size_t category=classify_last(x,y,sa,fa);
        CHECK(category<sizeof(last_regions)/sizeof(last_regions[0]));
        final_region &region=last_regions[category];
        const unsigned char *pixel=
            right_page.pixels+y*right_page.stride+x*4u;
        const unsigned char *oracle=reference.at(x,y);
        unsigned max_channel=0u,rgb_error=0u;
        worst_site current{};
        current.x=x;current.y=y;current.category=category;
        current.stroke_alpha=sa;current.fill_alpha=fa;
        ++region.pixels;
        for(size_t ch=0u;ch<3u;++ch) {
            const unsigned actual=on_white(pixel,ch);
            const unsigned truth=oracle[ch];
            const unsigned delta=actual>=truth?actual-truth:truth-actual;
            region.abs_rgb+=(std::uint64_t)delta;
            rgb_error+=delta;
            if(actual>truth)region.brighter+=(std::uint64_t)delta;
            if(actual<truth)region.darker+=(std::uint64_t)delta;
            if(delta>max_channel)max_channel=delta;
            current.native[ch]=(unsigned char)actual;
            current.reference[ch]=(unsigned char)truth;
        }
        if(max_channel==0u)continue;
        ++region.changed;
        if(max_channel>64u)++region.gross;
        if(max_channel>region.max_delta) {
            region.max_delta=max_channel;
            region.worst_x=x;
            region.worst_y=y;
        }
        current.channel_delta=max_channel;
        current.rgb_error=rgb_error;
        const auto better=[](const worst_site &a,const worst_site &b){
            if(a.channel_delta!=b.channel_delta)
                return a.channel_delta>b.channel_delta;
            if(a.rgb_error!=b.rgb_error)
                return a.rgb_error>b.rgb_error;
            if(a.y!=b.y)return a.y<b.y;
            return a.x<b.x;
        };
        size_t at=0u;
        while(at<last_worst.size() &&
              !better(current,last_worst[at]))++at;
        if(at<12u) {
            last_worst.insert(last_worst.begin()+(std::ptrdiff_t)at,current);
            if(last_worst.size()>12u)last_worst.pop_back();
        }
    }
    size_t final_pixels=0u,final_changed=0u,final_gross=0u;
    std::uint64_t final_rgb=0u,final_brighter=0u,final_darker=0u;
    unsigned final_max_delta=0u;
    std::string final_region_json=
        "{\"schema_version\":1,\"observation_only\":true,"
        "\"pixel_parity_qualified\":false,\"regions\":{";
    for(size_t i=0u;i<sizeof(last_regions)/sizeof(last_regions[0]);++i) {
        const final_region &a=last_regions[i];
        CHECK(a.changed<=a.pixels && a.gross<=a.changed &&
              a.abs_rgb==a.brighter+a.darker);
        final_pixels+=a.pixels;
        final_changed+=a.changed;
        final_gross+=a.gross;
        final_rgb+=a.abs_rgb;
        final_brighter+=a.brighter;
        final_darker+=a.darker;
        if(a.max_delta>final_max_delta)final_max_delta=a.max_delta;
        if(i!=0u)final_region_json+=",";
        final_region_json+="\""+std::string(a.name)+"\":{";
        final_region_json+="\"pixels\":"+std::to_string(a.pixels)+",";
        final_region_json+="\"changed\":"+std::to_string(a.changed)+",";
        final_region_json+="\"gross\":"+std::to_string(a.gross)+",";
        final_region_json+="\"abs_rgb_error\":"+
                           std::to_string(a.abs_rgb)+",";
        final_region_json+="\"brighter_rgb_error\":"+
                           std::to_string(a.brighter)+",";
        final_region_json+="\"darker_rgb_error\":"+
                           std::to_string(a.darker)+",";
        final_region_json+="\"max_channel_delta\":"+
                           std::to_string(a.max_delta)+",";
        final_region_json+="\"worst_x\":"+std::to_string(a.worst_x)+",";
        final_region_json+="\"worst_y\":"+std::to_string(a.worst_y)+"}";
    }
    CHECK(final_pixels==501832u &&
          final_changed==right_metric.changed &&
          final_gross==right_metric.gross &&
          final_rgb==right_metric.abs_rgb &&
          final_rgb==final_brighter+final_darker &&
          final_max_delta==right_metric.max_delta &&
          !last_worst.empty() &&
          last_worst[0].channel_delta==right_metric.max_delta);
    final_region_json+="},\"worst_pixels\":[";
    for(size_t i=0u;i<last_worst.size();++i) {
        const worst_site &p=last_worst[i];
        if(i)final_region_json+=",";
        final_region_json+="{\"x\":"+std::to_string(p.x)+",";
        final_region_json+="\"y\":"+std::to_string(p.y)+",";
        final_region_json+="\"region\":\""+
            std::string(last_regions[p.category].name)+"\",";
        final_region_json+="\"max_channel_delta\":"+
            std::to_string(p.channel_delta)+",";
        final_region_json+="\"rgb_error\":"+
            std::to_string(p.rgb_error)+",";
        final_region_json+="\"stroke_alpha\":"+
            std::to_string(p.stroke_alpha)+",";
        final_region_json+="\"fill_alpha\":"+
            std::to_string(p.fill_alpha)+",";
        final_region_json+="\"native_rgb\":["+
            std::to_string((unsigned)p.native[0])+","+
            std::to_string((unsigned)p.native[1])+","+
            std::to_string((unsigned)p.native[2])+"],";
        final_region_json+="\"pdfium_rgb\":["+
            std::to_string((unsigned)p.reference[0])+","+
            std::to_string((unsigned)p.reference[1])+","+
            std::to_string((unsigned)p.reference[2])+"]}";
    }
    final_region_json+="],\"witnesses\":[";
    struct real_witness {const char *name;size_t x,y;};
    const real_witness final_witnesses[]={
        {"bottom_left",50u,139u},
        {"bottom_center",100u,139u},
        {"bottom_right",250u,139u},
        {"right_vertical",250u,60u},
        {"left_vertical_lower",50u,110u},
        {"left_red_stroke_overlap",50u,40u},
        {"left_outside",49u,40u},
        {"top_corner",250u,19u},
        {"red_top",100u,20u},
        {"red_bottom",100u,99u},
        {"red_right",150u,40u},
        {"red_bottom_uncovered",100u,100u}
    };
    for(size_t i=0u;i<sizeof(final_witnesses)/
                           sizeof(final_witnesses[0]);++i) {
        const real_witness &w=final_witnesses[i];
        const unsigned char *stroke=h_stroke.pixels+
            w.y*h_stroke.stride+w.x*4u;
        const unsigned char *fill_pixel=right_fill.pixels+
            w.y*right_fill.stride+w.x*4u;
        const unsigned char *native=right_page.pixels+
            w.y*right_page.stride+w.x*4u;
        const unsigned char *truth=reference.at(w.x,w.y);
        const size_t category=
            classify_last(w.x,w.y,stroke[3],fill_pixel[3]);
        CHECK(category<sizeof(last_regions)/sizeof(last_regions[0]));
        if(i)final_region_json+=",";
        final_region_json+="{\"name\":\""+std::string(w.name)+"\",";
        final_region_json+="\"x\":"+std::to_string(w.x)+",";
        final_region_json+="\"y\":"+std::to_string(w.y)+",";
        final_region_json+="\"region\":\""+
             std::string(last_regions[category].name)+"\",";
        final_region_json+="\"stroke_alpha\":"+
             std::to_string((unsigned)stroke[3])+",";
        final_region_json+="\"fill_alpha\":"+
             std::to_string((unsigned)fill_pixel[3])+",";
        final_region_json+="\"native_rgb\":["+
             std::to_string(on_white(native,0u))+","+
             std::to_string(on_white(native,1u))+","+
             std::to_string(on_white(native,2u))+"],";
        final_region_json+="\"pdfium_rgb\":["+
             std::to_string((unsigned)truth[0])+","+
             std::to_string((unsigned)truth[1])+","+
             std::to_string((unsigned)truth[2])+"]}";
    }
    final_region_json+="],\"full_original_reconciled\":true}";
    std::fprintf(stderr,
       "[jsPDF private Fill-right candidate original residual audit] %s\n",
       final_region_json.c_str());

    size_t horizontal_alpha_changes=0u,outside_horizontal=0u;
    for(size_t y=0u;y<842u;++y)for(size_t x=0u;x<596u;++x) {
        const size_t off=y*h_stroke.stride+x*4u;
        if(h_stroke.pixels[off+3u]==analytic_stroke.pixels[off+3u])
            continue;
        ++horizontal_alpha_changes;
        if(x<52u || x>248u || y<138u || y>141u)
            ++outside_horizontal;
        CHECK(std::memcmp(h_stroke.pixels+off,
                          analytic_stroke.pixels+off,3u)==0);
    }
    CHECK(horizontal_alpha_changes>0u && outside_horizontal==0u);
    CHECK(h_stroke.pixels[139u*h_stroke.stride+100u*4u+3u]!=
          analytic_stroke.pixels[
              139u*analytic_stroke.stride+100u*4u+3u]);
    std::fprintf(stderr,
        "[jsPDF fifth private horizontal A/B, NOT parity] "
        "mode4_rgb=%llu mode5_rgb=%llu changed=%zu gross=%zu "
        "source_alpha_changes=%zu fnv64=%016llx\n",
        (unsigned long long)candidate.abs_rgb,
        (unsigned long long)horizontal.abs_rgb,
        horizontal.changed,horizontal.gross,horizontal_alpha_changes,
        (unsigned long long)horizontal.fnv);
    size_t changed_stroke_pixels=0u,other_geometry_changes=0u;
    for(size_t y=0u;y<842u;++y)for(size_t x=0u;x<596u;++x) {
        const size_t off=y*baseline_stroke.stride+x*4u;
        const unsigned a=baseline_stroke.pixels[off+3u];
        const unsigned b=analytic_stroke.pixels[off+3u];
        if(a==b) continue;
        ++changed_stroke_pixels;
        if(!((x>=49u && x<=51u)||(x>=249u && x<=251u)) ||
           y<22u || y>137u)
            ++other_geometry_changes;
        CHECK(std::memcmp(baseline_stroke.pixels+off,
                          analytic_stroke.pixels+off,3u)==0);
    }
    CHECK(changed_stroke_pixels>0u && other_geometry_changes==0u);
    CHECK(corpus_fixture_executions==0u);
    std::fprintf(stderr,
        "[jsPDF original vertical analytic A/B, NOT pixel parity] "
        "mode3_error=%llu mode4_error=%llu old_changed=%zu "
        "trial_changed=%zu sampled_vertical_changes=%zu "
        "mode3_gross=%zu trial_gross=%zu mode4_fnv=%016llx\n",
        (unsigned long long)old.abs_rgb,
        (unsigned long long)candidate.abs_rgb,
        old.changed,candidate.changed,changed_stroke_pixels,
        old.gross,candidate.gross,
        (unsigned long long)candidate.fnv);


    /* Read-only fourth-mode original-page region audit; the windows below
     * describe ONLY the immutable source PDF, never production heuristics. */
    struct source_region {
        const char *name;
        size_t pixels=0u,changed=0u,gross=0u,worst_x=0u,worst_y=0u;
        std::uint64_t abs_rgb=0u,brighter=0u,darker=0u;
        unsigned max_delta=0u;
    };
    source_region regions[]={
        {"bottom_single_horizontal_stroke"},{"right_vertical_stroke"},
        {"left_vertical_stroke"},{"top_repeated_stroke"},
        {"clip_right_red_boundary"},{"clip_bottom_weak_fringe"},{"other"}
    };
    const auto locate=[](size_t x,size_t y,unsigned sa,unsigned fa)->size_t {
        if(x>=49u&&x<=250u&&y>=138u&&y<=140u&&sa&&!fa)return 0u;
        if(x>=249u&&x<=251u&&y>=22u&&y<=137u&&sa&&!fa)return 1u;
        if(x>=49u&&x<=51u&&y>=22u&&y<=137u&&sa&&!fa)return 2u;
        if(x>=49u&&x<=250u&&y>=19u&&y<=21u&&sa&&!fa)return 3u;
        if(x==150u&&y>=21u&&y<=99u&&!sa&&fa)return 4u;
        if(x>=52u&&x<=148u&&y==100u&&!sa&&!fa)return 5u;
        return 6u;
    };
    for(size_t y=0u;y<842u;++y)for(size_t x=0u;x<596u;++x) {
        const size_t so=y*analytic_stroke.stride+x*4u;
        const unsigned sa=analytic_stroke.pixels[so+3u];
        const unsigned fa=fill.pixels[y*fill.stride+x*4u+3u];
        source_region &b=regions[locate(x,y,sa,fa)];
        const unsigned char *n=trial_page.pixels+y*trial_page.stride+x*4u;
        const unsigned char *o=reference.at(x,y);
        ++b.pixels;
        unsigned largest=0u;
        for(size_t ch=0u;ch<3u;++ch) {
            const unsigned a=on_white(n,ch),r=o[ch];
            const unsigned d=a>=r?a-r:r-a;
            b.abs_rgb+=(std::uint64_t)d;
            if(a>r)b.brighter+=(std::uint64_t)d;
            if(a<r)b.darker+=(std::uint64_t)d;
            if(d>largest)largest=d;
        }
        if(largest!=0u)++b.changed;
        if(largest>64u)++b.gross;
        if(largest>b.max_delta) {
            b.max_delta=largest;b.worst_x=x;b.worst_y=y;
        }
    }
    size_t region_pixels=0u,region_changed=0u,region_gross=0u;
    std::uint64_t region_error=0u,region_pos=0u,region_neg=0u;
    unsigned region_max=0u;
    std::string mode4_regions_json=
        "{\"schema_version\":1,\"observation_only\":true,"
        "\"pixel_parity_qualified\":false,\"regions\":{";
    for(size_t i=0u;i<7u;++i) {
        const source_region &b=regions[i];
        CHECK(b.changed<=b.pixels&&b.gross<=b.changed&&
              b.abs_rgb==b.brighter+b.darker);
        region_pixels+=b.pixels;region_changed+=b.changed;
        region_gross+=b.gross;region_error+=b.abs_rgb;
        region_pos+=b.brighter;region_neg+=b.darker;
        if(b.max_delta>region_max)region_max=b.max_delta;
        if(i)mode4_regions_json+=",";
        mode4_regions_json+="\""+std::string(b.name)+"\":{";
        mode4_regions_json+="\"pixels\":"+std::to_string(b.pixels)+",";
        mode4_regions_json+="\"changed\":"+std::to_string(b.changed)+",";
        mode4_regions_json+="\"gross\":"+std::to_string(b.gross)+",";
        mode4_regions_json+="\"abs_rgb_error\":"+std::to_string(b.abs_rgb)+",";
        mode4_regions_json+="\"brighter_rgb_error\":"+
                            std::to_string(b.brighter)+",";
        mode4_regions_json+="\"darker_rgb_error\":"+
                            std::to_string(b.darker)+",";
        mode4_regions_json+="\"max_delta\":"+std::to_string(b.max_delta)+",";
        mode4_regions_json+="\"worst_x\":"+std::to_string(b.worst_x)+",";
        mode4_regions_json+="\"worst_y\":"+std::to_string(b.worst_y)+"}";
    }
    CHECK(region_pixels==501832u&&region_changed==candidate.changed&&
          region_gross==candidate.gross&&region_error==candidate.abs_rgb&&
          region_error==region_pos+region_neg&&
          region_max==candidate.max_delta);
    mode4_regions_json+="},\"witnesses\":[";
    struct witness{const char *name;size_t x,y,region;};
    const witness witnesses[]={
        {"bottom_left",50u,139u,0u},{"bottom_interior",100u,139u,0u},
        {"bottom_right",250u,139u,0u},{"right_vertical",250u,60u,1u},
        {"left_vertical",50u,110u,2u},{"top_corner",250u,19u,3u},
        {"clip_right",150u,40u,4u},{"clip_bottom",100u,100u,5u}
    };
    for(size_t i=0u;i<8u;++i) {
        const witness &w=witnesses[i];
        const unsigned sa=analytic_stroke.pixels[
            w.y*analytic_stroke.stride+w.x*4u+3u];
        const unsigned fa=fill.pixels[w.y*fill.stride+w.x*4u+3u];
        const unsigned char *n=trial_page.pixels+
            w.y*trial_page.stride+w.x*4u;
        const unsigned char *o=reference.at(w.x,w.y);
        if(locate(w.x,w.y,sa,fa)!=w.region)
            std::fprintf(stderr,
                "[jsPDF original witness invalid] %s (%zu,%zu) "
                "expected=%zu actual=%zu stroke_alpha=%u fill_alpha=%u\n",
                w.name,w.x,w.y,w.region,locate(w.x,w.y,sa,fa),sa,fa);
        CHECK(locate(w.x,w.y,sa,fa)==w.region);
        if(i)mode4_regions_json+=",";
        mode4_regions_json+="{\"name\":\""+std::string(w.name)+"\",";
        mode4_regions_json+="\"region\":\""+
                            std::string(regions[w.region].name)+"\",";
        mode4_regions_json+="\"x\":"+std::to_string(w.x)+",";
        mode4_regions_json+="\"y\":"+std::to_string(w.y)+",";
        mode4_regions_json+="\"stroke_alpha\":"+std::to_string(sa)+",";
        mode4_regions_json+="\"fill_alpha\":"+std::to_string(fa)+",";
        mode4_regions_json+="\"native_rgb\":["+
            std::to_string(on_white(n,0u))+","+
            std::to_string(on_white(n,1u))+","+
            std::to_string(on_white(n,2u))+"],";
        mode4_regions_json+="\"pdfium_rgb\":["+
            std::to_string((unsigned)o[0])+","+
            std::to_string((unsigned)o[1])+","+
            std::to_string((unsigned)o[2])+"]}";
    }
    mode4_regions_json+="],\"full_original_reconciled\":true}";
    std::fprintf(stderr,"[jsPDF mode4 original residual regions] %s\n",
                 mode4_regions_json.c_str());

    quantapdf_ir_raster_image_dispose(&left_page_repeat);
    quantapdf_ir_raster_image_dispose(&left_page);
    quantapdf_ir_raster_image_dispose(&left_repeat);
    quantapdf_ir_raster_image_dispose(&left_fill);
    quantapdf_ir_raster_image_dispose(&right_page_repeat);
    quantapdf_ir_raster_image_dispose(&right_page);
    quantapdf_ir_raster_image_dispose(&right_repeat);
    quantapdf_ir_raster_image_dispose(&right_fill);
    quantapdf_ir_raster_image_dispose(&h_page_repeat);
    quantapdf_ir_raster_image_dispose(&h_page);
    quantapdf_ir_raster_image_dispose(&h_repeat);
    quantapdf_ir_raster_image_dispose(&h_stroke);
    quantapdf_ir_raster_image_dispose(&repeat_page);
    quantapdf_ir_raster_image_dispose(&trial_page);
    quantapdf_ir_raster_image_dispose(&base_page);
    quantapdf_ir_raster_image_dispose(&fill);
    quantapdf_ir_raster_image_dispose(&repeat_stroke);
    quantapdf_ir_raster_image_dispose(&analytic_stroke);
    quantapdf_ir_raster_image_dispose(&baseline_stroke);
    quantapdf_ir_dispose(&detached);

    FILE *out=std::fopen(pending.c_str(),"wb");
    CHECK(out);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"original_jspdf_private_vertical_analytic_ab\",\n"
        "  \"document_id\": \"jspdf.w3s-clip-v1\",\n"
        "  \"observation_only\": true,\n"
        "  \"pixel_parity_qualified\": false,\n"
        "  \"default_renderer_changed\": false,\n"
        "  \"source_and_graph_released\": true,\n"
        "  \"original_pdfium_width\": 596,\n"
        "  \"original_pdfium_height\": 842,\n"
        "  \"original_pdfium_pixel_count\": 501832,\n"
        "  \"original_rgb_channels\": 1505496,\n"
        "  \"paint_order\": \"stroke_then_w_clipped_fill\",\n"
        "  \"candidate_mode\": \"explicit_private_single_vertical_analytic\",\n"
        "  \"candidate_repeat_equal\": true,\n"
        "  \"source_stroke_alpha_changed_pixels\": %zu,\n"
        "  \"source_stroke_changes_outside_vertical_interior\": %zu,\n"
        "  \"mode3_abs_rgb_error\": %llu,\n"
        "  \"mode3_changed_pixels\": %zu,\n"
        "  \"mode3_gross_delta64_pixels\": %zu,\n"
        "  \"mode3_max_channel_delta\": %u,\n"
        "  \"mode3_rgb_fnv64\": \"%016llx\",\n"
        "  \"mode4_abs_rgb_error\": %llu,\n"
        "  \"mode4_mean_abs_rgb_error\": %.9f,\n"
        "  \"mode4_changed_pixels\": %zu,\n"
        "  \"mode4_gross_delta64_pixels\": %zu,\n"
        "  \"mode4_max_channel_delta\": %u,\n"
        "  \"mode4_rgb_fnv64\": \"%016llx\",\n"
        "  \"mode4_original_source_regions\": %s,\n"
        "  \"mode5_status\": \"measured_unqualified\",\n"
        "  \"mode5_default_changed\": false,\n"
        "  \"mode5_pixel_parity_qualified\": false,\n"
        "  \"mode5_repeat_equal\": true,\n"
        "  \"mode5_source_alpha_changed_pixels\": %zu,\n"
        "  \"mode5_changes_outside_horizontal_interior\": %zu,\n"
        "  \"mode5_abs_rgb_error\": %llu,\n"
        "  \"mode5_mean_abs_rgb_error\": %.9f,\n"
        "  \"mode5_changed_pixels\": %zu,\n"
        "  \"mode5_gross_delta64_pixels\": %zu,\n"
        "  \"mode5_max_channel_delta\": %u,\n"
        "  \"mode5_rgb_fnv64\": \"%016llx\",\n"
        "  \"rect_fill_right_status\": \"measured_unqualified\",\n"
        "  \"rect_fill_right_default_changed\": false,\n"
        "  \"rect_fill_right_pixel_parity_qualified\": false,\n"
        "  \"rect_fill_right_repeat_equal\": true,\n"
        "  \"rect_fill_right_source_alpha_changed_pixels\": %zu,\n"
        "  \"rect_fill_right_changes_outside_fill_right_interior\": %zu,\n"
        "  \"rect_fill_right_abs_rgb_error\": %llu,\n"
        "  \"rect_fill_right_mean_abs_rgb_error\": %.9f,\n"
        "  \"rect_fill_right_changed_pixels\": %zu,\n"
        "  \"rect_fill_right_gross_delta64_pixels\": %zu,\n"
        "  \"rect_fill_right_max_channel_delta\": %u,\n"
        "  \"rect_fill_right_rgb_fnv64\": \"%016llx\",\n"        "  \"rect_fill_right_post_ab_residual_regions\": %s,\n"        "  \"rect_clip_left_status\": \"measured_unqualified\",\n"
        "  \"rect_clip_left_default_changed\": false,\n"
        "  \"rect_clip_left_pixel_parity_qualified\": false,\n"
        "  \"rect_clip_left_repeat_equal\": true,\n"
        "  \"rect_clip_left_source_alpha_changed_pixels\": %zu,\n"
        "  \"rect_clip_left_changes_outside_w_clip_left_interior\": %zu,\n"
        "  \"rect_clip_left_abs_rgb_error\": %llu,\n"
        "  \"rect_clip_left_mean_abs_rgb_error\": %.9f,\n"
        "  \"rect_clip_left_changed_pixels\": %zu,\n"
        "  \"rect_clip_left_gross_delta64_pixels\": %zu,\n"
        "  \"rect_clip_left_max_channel_delta\": %u,\n"
        "  \"rect_clip_left_rgb_fnv64\": \"%016llx\",\n"        "  \"rect_clip_left_original_residual_regions\": %s,\n"
        "  \"native_original_pixel_positive_count\": 0,\n"
        "  \"official_w_wstar_positive_count_increase\": 0\n"
        "}\n",
        changed_stroke_pixels,other_geometry_changes,
        (unsigned long long)old.abs_rgb,old.changed,old.gross,
        old.max_delta,(unsigned long long)old.fnv,
        (unsigned long long)candidate.abs_rgb,
        (double)candidate.abs_rgb/1505496.0,
        candidate.changed,candidate.gross,candidate.max_delta,
        (unsigned long long)candidate.fnv,
        mode4_regions_json.c_str(),
        horizontal_alpha_changes,outside_horizontal,
        (unsigned long long)horizontal.abs_rgb,
        (double)horizontal.abs_rgb/1505496.0,
        horizontal.changed,horizontal.gross,horizontal.max_delta,
        (unsigned long long)horizontal.fnv,
        right_alpha_changes,right_outside,
        (unsigned long long)right_metric.abs_rgb,
        (double)right_metric.abs_rgb/1505496.0,
        right_metric.changed,right_metric.gross,right_metric.max_delta,
        (unsigned long long)right_metric.fnv,
        final_region_json.c_str(),
        left_alpha_changed,left_outside,
        (unsigned long long)left_metric.abs_rgb,
        (double)left_metric.abs_rgb/1505496.0,
        left_metric.changed,left_metric.gross,left_metric.max_delta,
        (unsigned long long)left_metric.fnv,
        mode7_regions_json.c_str());
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0)CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(pending.c_str(),report_path)==0);
    return EXIT_SUCCESS;
}

int test_external_jspdf_fractional_page_rejection(
    const char *filename, const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    namespace fs=std::filesystem;
    std::error_code ec;
    const fs::path source(filename),report(report_path);
    const std::string pending=std::string(report_path)+".tmp";
    const fs::path stage(pending);
    CHECK(fs::is_regular_file(source,ec) && !ec);
    CHECK(!fs::is_symlink(source,ec) && !ec);
    const fs::path pinned=fs::weakly_canonical(source,ec);
    CHECK(!ec);
    CHECK(fs::weakly_canonical(report,ec)!=pinned && !ec);
    CHECK(fs::weakly_canonical(stage,ec)!=pinned && !ec);
    for(const fs::path& candidate:{report,stage}) {
        const bool exists=fs::exists(candidate,ec);
        CHECK(!ec);
        if(exists) {
            CHECK(!fs::is_symlink(candidate,ec) && !ec);
            CHECK(!fs::equivalent(source,candidate,ec) && !ec);
        }
    }
    CHECK(corpus_fixture_executions==0u);
    quantapdf_ir detached{};
    OK(snapshot_lower(&detached,0u,4u,8u,filename));
    CHECK(detached.pages!=nullptr && detached.page_count==1u &&
          detached.pages[0].width_points==(float)595.28 &&
          detached.pages[0].height_points==(float)841.89 &&
          detached.pages[0].suppress_background==1 &&
          detached.operations!=nullptr && detached.operation_count==2u &&
          detached.resources.clip_count==1u &&
          detached.resources.graphics_state_count==1u &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.form_count==0u &&
          detached.resources.soft_mask_count==0u);
    const clip_leaf_counts winding=inspect_clip_fill_rules(detached);
    CHECK(winding.nonzero==1u && winding.evenodd==0u &&
          winding.intersections==0u);
    const quantapdf_ir_raster_v1_limits limits={
        600u*850u,2u,768u,70000000u
    };
    quantapdf_ir_raster_image native{};
    const quantapdf_status status=
        quantapdf_ir_raster_scoped_operations_v1(
            &detached,0u,&limits,&native);
    CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(native.pixels==nullptr && native.size==0u &&
          native.width==0u && native.height==0u && native.stride==0u);
    /* Independently exercise #276's opt-in C11 grid on the SAME immutable
     * original, after Source/Graph lifetime ends. Its fill-only admission
     * deliberately refuses real jsPDF stroked Path content and must publish
     * zero RGBA. The stroke classification is test-authored, not an API
     * rejection-reason enum. */
    size_t original_stroke_paths=0u, original_fill_paths=0u;
    for (size_t i=0u;i<detached.operation_count;++i) {
        const quantapdf_composer_operation& op=detached.operations[i];
        CHECK(op.kind==QUANTAPDF_COMPOSER_OPERATION_PATH);
        if (op.value.path.options.stroke!=0) ++original_stroke_paths;
        if (op.value.path.options.fill!=0) ++original_fill_paths;
    }
    CHECK(original_stroke_paths>=1u && original_fill_paths>=1u);
    const quantapdf_native_device_grid_geometry source_geometry={
        595.28,841.89,0.0,0.0,1.0,0,0,72.0f
    };
    const quantapdf_native_device_grid_limits grid_limits={
        600u*850u,600u*850u*4u,768u,70000000u
    };
    quantapdf_native_device_grid device_grid{};
    CHECK(quantapdf_native_device_grid_create_v0(
        &source_geometry,&grid_limits,&device_grid)==
        QUANTAPDF_NATIVE_DEVICE_GRID_OK);
    CHECK(device_grid.width==596u && device_grid.height==842u);
    CHECK(quantapdf_native_device_grid_check_ir_v0(
        &device_grid,detached.pages[0].width_points,
        detached.pages[0].height_points)==
        QUANTAPDF_NATIVE_DEVICE_GRID_OK);
    quantapdf_ir_raster_image optin_native{};
    const quantapdf_status optin_status=
        quantapdf_ir_raster_scoped_device_grid_v0(
            &detached,0u,&source_geometry,&grid_limits,&limits,&optin_native);
    CHECK(optin_status==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(optin_native.pixels==nullptr && optin_native.size==0u &&
          optin_native.width==0u && optin_native.height==0u &&
          optin_native.stride==0u);
    /* Independently query PDFium's *original-page* 72-DPI, zero-rotation
     * device-grid geometry. Rendering is only a geometry reference, not a
     * claim that the C11 negative case has achieved pixel parity. */
    const rgb_image reference=render_original_pdfium(0u,filename);
    CHECK(reference.width==596 && reference.height==842 &&
          reference.components==3 && reference.stride>=596*3 &&
          reference.pixels.size()>=(size_t)reference.stride*842u);
    /* Original S operator was painted BEFORE the later W Clip became
     * active. Borrow only the first stroked Path without adopting future
     * graphics state/Clip nodes. New translation-only C11 stroke support
     * correctly maps source local (x,-y) + (0,page_height) to displayed
     * device points, leaving source ownership completely unchanged.
     * This isolates a real-original Stroke layer, NOT full-page parity. */
    CHECK(detached.operations[0].value.path.options.stroke==1 &&
          detached.operations[0].value.path.options.fill==0);
    quantapdf_ir isolated_stroke=detached;
    isolated_stroke.operations=&detached.operations[0];
    isolated_stroke.operation_count=1u;
    isolated_stroke.resources=quantapdf_resource_registry{};
    isolated_stroke.links=nullptr;
    isolated_stroke.link_count=0u;
    isolated_stroke.outlines=nullptr;
    isolated_stroke.outline_count=0u;
    quantapdf_native_device_grid_limits stroke_grid_limits=grid_limits;
    stroke_grid_limits.max_sample_tests=160000000u;
    const quantapdf_ir_raster_v1_limits stroke_raster_limits={
        600u*850u,1u,768u,160000000u
    };
    quantapdf_ir_raster_image source_stroke{};
    quantapdf_ir_raster_image repeat_stroke{};
    OK(quantapdf_ir_raster_device_polyline_stroke_v0(
        &isolated_stroke,0u,&source_geometry,&stroke_grid_limits,
        &stroke_raster_limits,&source_stroke));
    CHECK(source_stroke.pixels!=nullptr &&
          source_stroke.width==596u && source_stroke.height==842u &&
          source_stroke.stride==596u*4u && source_stroke.size==596u*842u*4u);
    /* Exact source interior anchors outside later red fill:
     * PDFium's raw original has the earlier black stroke here, whereas
     * the isolated native layer has only stroke RGBA, transparent elsewhere.
     * The coarse oracle is intentionally NOT edge/per-pixel parity. */
    CHECK(source_stroke.pixels[20u*source_stroke.stride+200u*4u+3u]>0u);
    CHECK(source_stroke.pixels[70u*source_stroke.stride+250u*4u+3u]>0u);
    CHECK(source_stroke.pixels[20u*source_stroke.stride+20u*4u+3u]==0u);
    CHECK(source_stroke.pixels[20u*source_stroke.stride+100u*4u+3u]==
          source_stroke.pixels[20u*source_stroke.stride+200u*4u+3u]);
    CHECK(reference.at(200u,20u)[0]<245u &&
          reference.at(200u,20u)[1]<245u &&
          reference.at(200u,20u)[2]<245u);
    CHECK(reference.at(250u,70u)[0]<245u &&
          reference.at(250u,70u)[1]<245u &&
          reference.at(250u,70u)[2]<245u);
    CHECK(reference.at(20u,20u)[0]>245u &&
          reference.at(20u,20u)[1]>245u &&
          reference.at(20u,20u)[2]>245u);
    OK(quantapdf_ir_raster_device_polyline_stroke_v0(
        &isolated_stroke,0u,&source_geometry,&stroke_grid_limits,
        &stroke_raster_limits,&repeat_stroke));
    CHECK(repeat_stroke.size==source_stroke.size &&
          std::memcmp(repeat_stroke.pixels,source_stroke.pixels,
                      source_stroke.size)==0);
    /* Retain both repeat-verified C11 Stroke layers until source-order
     * composition with the later W-clipped Fill has been measured. */
    /* The second original operation is the later FILL, so its frozen W
     * Clip applies only to this operation, not to the already-painted S.
     * The source Path and its Clip leaf share translation-only (x,-y)+H. */
    CHECK(detached.operations[1].value.path.options.fill==1 &&
          detached.operations[1].value.path.options.stroke==0);
    quantapdf_ir isolated_fill=detached;
    isolated_fill.operations=&detached.operations[1];
    isolated_fill.operation_count=1u;
    /* Original Fill and W Clip each retain repeated source edges; exact
     * sample-edge billing needs a separate, explicit bounded per-layer
     * allowance. This does not change #270 whole-original admission. */
    quantapdf_native_device_grid_limits fill_grid_limits=grid_limits;
    fill_grid_limits.max_sample_tests=160000000u;
    const quantapdf_ir_raster_v1_limits fill_raster_limits={
        600u*850u,1u,768u,160000000u
    };
    quantapdf_ir_raster_image source_fill{};
    quantapdf_ir_raster_image repeat_fill{};
    const quantapdf_status source_fill_status=
        quantapdf_ir_raster_scoped_device_grid_v0(
            &isolated_fill,0u,&source_geometry,&fill_grid_limits,
            &fill_raster_limits,&source_fill);
    if (source_fill_status!=QUANTAPDF_OK)
        std::fprintf(stderr,"source-fill isolated status=%d\n",
                     (int)source_fill_status);
    OK(source_fill_status);
    CHECK(source_fill.pixels!=nullptr &&
          source_fill.width==596u && source_fill.height==842u &&
          source_fill.stride==596u*4u && source_fill.size==596u*842u*4u);
    const size_t fill_inside=50u*source_fill.stride+100u*4u;
    CHECK(source_fill.pixels[fill_inside]==255u &&
          source_fill.pixels[fill_inside+1u]==0u &&
          source_fill.pixels[fill_inside+2u]==0u &&
          source_fill.pixels[fill_inside+3u]==255u);
    CHECK(source_fill.pixels[50u*source_fill.stride+25u*4u+3u]==0u);
    CHECK(source_fill.pixels[50u*source_fill.stride+200u*4u+3u]==0u);
    CHECK(reference.at(100u,50u)[0]>245u &&
          reference.at(100u,50u)[1]<10u &&
          reference.at(100u,50u)[2]<10u);
    CHECK(reference.at(25u,50u)[0]>245u &&
          reference.at(25u,50u)[1]>245u &&
          reference.at(25u,50u)[2]>245u);
    CHECK(reference.at(200u,50u)[0]>245u &&
          reference.at(200u,50u)[1]>245u &&
          reference.at(200u,50u)[2]>245u);
    const quantapdf_status repeat_fill_status=
        quantapdf_ir_raster_scoped_device_grid_v0(
            &isolated_fill,0u,&source_geometry,&fill_grid_limits,
            &fill_raster_limits,&repeat_fill);
    if (repeat_fill_status!=QUANTAPDF_OK)
        std::fprintf(stderr,"source-fill repeat status=%d\n",
                     (int)repeat_fill_status);
    OK(repeat_fill_status);
    CHECK(repeat_fill.size==source_fill.size &&
          std::memcmp(repeat_fill.pixels,source_fill.pixels,
                      source_fill.size)==0);
    /* Explicit, bounded PRIVATE stroke-only *hypothesis* from same owned
     * original IR. Never replace the V0 C11 Stroke baseline, never publish
     * a native whole-original positive, and do not modify W Clip / Fill.
     * PDFium's 72/96dpi thin horizontal scan rows were observed to be more
     * opaque than simple C11 4x4 area sampling; this test measures whether
     * that limited alternative improves ALL 596x842 source pixels. */
    quantapdf_ir_raster_image preview_stroke{},preview_repeat{};
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &isolated_stroke,0u,&source_geometry,&stroke_grid_limits,
        &stroke_raster_limits,&preview_stroke));
    OK(quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
        &isolated_stroke,0u,&source_geometry,&stroke_grid_limits,
        &stroke_raster_limits,&preview_repeat));
    CHECK(preview_stroke.pixels!=nullptr &&
          preview_stroke.width==596u && preview_stroke.height==842u &&
          preview_stroke.stride==596u*4u &&
          preview_stroke.size==596u*842u*4u &&
          preview_repeat.size==preview_stroke.size &&
          std::memcmp(preview_stroke.pixels,preview_repeat.pixels,
                      preview_stroke.size)==0);
    /* Source horizontal line at x=200,y=19/20 is black in raw PDFium.
     * Vertical Stroke at x=250,y=60 is not changed by horizontal preview. */
    CHECK(source_stroke.pixels[19u*source_stroke.stride+200u*4u+3u]==128u);
    CHECK(preview_stroke.pixels[19u*preview_stroke.stride+200u*4u+3u]==255u);
    CHECK(preview_stroke.pixels[20u*preview_stroke.stride+200u*4u+3u]==255u);
    CHECK(preview_stroke.pixels[60u*preview_stroke.stride+250u*4u+3u]==
          source_stroke.pixels[60u*source_stroke.stride+250u*4u+3u]);

    /* Candidate WHOLE-PAGE image: native Stroke first (no future W Clip),
     * then native W-clipped Fill. Entirely pure C11 layer source-over.
     * Both layers were independently rendered from the genuine original
     * owned IR after source/graph lifetime ended. No PDFium fallback. */
    const quantapdf_ir_ordered_composite_v0_limits composite_budget={
        600u*850u,600u*850u*4u
    };
    quantapdf_ir_raster_image composite{},composite_repeat{};
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &source_stroke,&source_fill,&composite_budget,&composite));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &repeat_stroke,&repeat_fill,&composite_budget,&composite_repeat));
    CHECK(composite.pixels!=nullptr &&
          composite.width==596u && composite.height==842u &&
          composite.stride==596u*4u && composite.size==596u*842u*4u &&
          composite_repeat.size==composite.size &&
          std::memcmp(composite.pixels,composite_repeat.pixels,
                      composite.size)==0);
    CHECK(on_white(composite.pixels+50u*composite.stride+100u*4u,0u)>245u &&
          on_white(composite.pixels+50u*composite.stride+100u*4u,1u)<10u &&
          on_white(composite.pixels+50u*composite.stride+100u*4u,2u)<10u);
    CHECK(on_white(composite.pixels+20u*composite.stride+20u*4u,0u)==255u &&
          on_white(composite.pixels+20u*composite.stride+20u*4u,1u)==255u &&
          on_white(composite.pixels+20u*composite.stride+20u*4u,2u)==255u);

    /* **Non-admitted experimental alternative**. Source operation order
     * and bounded layer composition are identical to the baseline #281.
     * Only the isolated horizontal thin source Stroke inner-coverage trial
     * differs. Entire original-PDFium-vs-C11 metrics are independently
     * measured on EVERY pixel, never judged from a single scanline. */
    quantapdf_ir_raster_image preview_ordered{},preview_ordered_repeat{};
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &preview_stroke,&source_fill,&composite_budget,&preview_ordered));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &preview_repeat,&repeat_fill,&composite_budget,&preview_ordered_repeat));
    CHECK(preview_ordered.width==596u && preview_ordered.height==842u &&
          preview_ordered.stride==596u*4u &&
          preview_ordered.size==596u*842u*4u &&
          preview_ordered_repeat.size==preview_ordered.size &&
          std::memcmp(preview_ordered.pixels,preview_ordered_repeat.pixels,
                      preview_ordered.size)==0);
    std::uint64_t preview_abs_error=0u;
    size_t preview_changed_pixels=0u,preview_gross_delta64_pixels=0u;
    unsigned preview_max_delta=0u;
    unsigned long long preview_rgb_fnv64=14695981039346656037ULL;
    for(size_t py=0u;py<842u;++py) for(size_t px=0u;px<596u;++px) {
        const unsigned char *trial=
            preview_ordered.pixels+py*preview_ordered.stride+px*4u;
        const unsigned char *oracle=reference.at(px,py);
        unsigned largest=0u;
        for(size_t channel=0u;channel<3u;++channel) {
            unsigned sample=on_white(trial,channel);
            unsigned truth=oracle[channel];
            unsigned delta=sample>=truth ? sample-truth : truth-sample;
            preview_abs_error+=(std::uint64_t)delta;
            if(delta>largest)largest=delta;
            if(delta>preview_max_delta)preview_max_delta=delta;
            preview_rgb_fnv64^=(unsigned long long)sample;
            preview_rgb_fnv64*=1099511628211ULL;
        }
        if(largest!=0u)++preview_changed_pixels;
        if(largest>64u)++preview_gross_delta64_pixels;
    }
    const double preview_mean_abs_rgb_error=
        (double)preview_abs_error/(double)(596u*842u*3u);
    CHECK(std::isfinite(preview_mean_abs_rgb_error) &&
          preview_mean_abs_rgb_error>=0.0 &&
          preview_mean_abs_rgb_error<=255.0 &&
          preview_changed_pixels<=596u*842u &&
          preview_gross_delta64_pixels<=preview_changed_pixels &&
          preview_max_delta<=255u);
    /* Revisit ALL 596x842 candidate pixels through the independently
     * tested bounded spatial-attribution helper. This is not a guess from
     * 2 or 12 selected sites: category sums must match independently
     * calculated whole-original preview counts/absolute error. */
    using jspdf_pixel_attribution::plane;
    const auto repeated_preview_attribution=
        jspdf_pixel_attribution::analyze(
            plane{preview_stroke.pixels,preview_stroke.stride,
                  preview_stroke.size},
            plane{source_fill.pixels,source_fill.stride,source_fill.size},
            plane{preview_ordered.pixels,preview_ordered.stride,
                  preview_ordered.size},
            plane{reference.pixels.data(),(size_t)reference.stride,
                  reference.pixels.size()},
            596u,842u);
    CHECK(repeated_preview_attribution.valid);
    CHECK(repeated_preview_attribution.changed_total()==
          preview_changed_pixels);
    CHECK(repeated_preview_attribution.gross_total()==
          preview_gross_delta64_pixels);
    CHECK(repeated_preview_attribution.error_total()==preview_abs_error);
    CHECK(repeated_preview_attribution.worst_count<=
          jspdf_pixel_attribution::report::max_hotspots);
    CHECK(repeated_preview_attribution.flat_far_from_partial_3x3+
          repeated_preview_attribution.flat_near_partial_3x3==
          repeated_preview_attribution.changed[0]);
    if(preview_changed_pixels!=0u) {
        CHECK(repeated_preview_attribution.worst_count!=0u);
        CHECK(repeated_preview_attribution.worst[0].max_delta==
              preview_max_delta);
    }
    const std::string repeated_preview_attribution_json=
        jspdf_pixel_attribution::json(repeated_preview_attribution);
    std::fprintf(stderr,
        "[jsPDF repeat-only preview residual] changed=[%zu,%zu,%zu,%zu] "
        "gross=[%zu,%zu,%zu,%zu] far_flat=%zu\n",
        repeated_preview_attribution.changed[0],
        repeated_preview_attribution.changed[1],
        repeated_preview_attribution.changed[2],
        repeated_preview_attribution.changed[3],
        repeated_preview_attribution.gross[0],
        repeated_preview_attribution.gross[1],
        repeated_preview_attribution.gross[2],
        repeated_preview_attribution.gross[3],
        repeated_preview_attribution.flat_far_from_partial_3x3);
    for(size_t i=0u;i<repeated_preview_attribution.worst_count;++i) {
        const auto& h=repeated_preview_attribution.worst[i];
        std::fprintf(stderr,
            "  preview-residual %zu (%zu,%zu) delta=%u sum=%u "
            "stroke_alpha=%u fill_alpha=%u "
            "native=(%u,%u,%u) original_pdfium=(%u,%u,%u)\n",
            i,h.x,h.y,h.max_delta,h.rgb_error,
            h.stroke_alpha,h.fill_alpha,
            h.candidate[0],h.candidate[1],h.candidate[2],
            h.oracle[0],h.oracle[1],h.oracle[2]);
    }

    /* One more explicitly invoked A/B experiment: include only the
     * pixel intervals at geometrically repeated source horizontal
     * endpoints. Source/Graph are still released; the real S and W+Fill
     * ownership and paint order stay intact. No original native
     * positive is authorized by the result. */
    quantapdf_ir_raster_image endpoint_stroke{},endpoint_repeat{};
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &isolated_stroke,0u,&source_geometry,&stroke_grid_limits,
        &stroke_raster_limits,&endpoint_stroke));
    OK(quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
        &isolated_stroke,0u,&source_geometry,&stroke_grid_limits,
        &stroke_raster_limits,&endpoint_repeat));
    CHECK(endpoint_stroke.pixels!=nullptr &&
          endpoint_stroke.width==596u && endpoint_stroke.height==842u &&
          endpoint_stroke.size==preview_stroke.size &&
          endpoint_stroke.stride==preview_stroke.stride &&
          endpoint_repeat.size==endpoint_stroke.size &&
          std::memcmp(endpoint_stroke.pixels,endpoint_repeat.pixels,
                      endpoint_stroke.size)==0);
    /* These two actual source pixels correspond exactly to the last
     * >64-delta sites observed by the already CI-green #285 report.
     * The new endpoint preview must actually exercise both sites. */
    CHECK(preview_stroke.pixels[19u*preview_stroke.stride+
                                50u*4u+3u]<255u);
    CHECK(preview_stroke.pixels[19u*preview_stroke.stride+
                                250u*4u+3u]<255u);
    CHECK(endpoint_stroke.pixels[19u*endpoint_stroke.stride+
                                 50u*4u+3u]==255u);
    CHECK(endpoint_stroke.pixels[19u*endpoint_stroke.stride+
                                 250u*4u+3u]==255u);
    quantapdf_ir_raster_image endpoint_composite{},endpoint_composite_repeat{};
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &endpoint_stroke,&source_fill,&composite_budget,&endpoint_composite));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &endpoint_repeat,&repeat_fill,&composite_budget,
        &endpoint_composite_repeat));
    CHECK(endpoint_composite.width==596u &&
          endpoint_composite.height==842u &&
          endpoint_composite.size==596u*842u*4u &&
          endpoint_composite_repeat.size==endpoint_composite.size &&
          std::memcmp(endpoint_composite.pixels,
                      endpoint_composite_repeat.pixels,
                      endpoint_composite.size)==0);
    const auto endpoint_attr=jspdf_pixel_attribution::analyze(
        plane{endpoint_stroke.pixels,endpoint_stroke.stride,
              endpoint_stroke.size},
        plane{source_fill.pixels,source_fill.stride,source_fill.size},
        plane{endpoint_composite.pixels,endpoint_composite.stride,
              endpoint_composite.size},
        plane{reference.pixels.data(),(size_t)reference.stride,
              reference.pixels.size()},
        596u,842u);
    CHECK(endpoint_attr.valid);
    const std::uint64_t endpoint_error=endpoint_attr.error_total();
    const size_t endpoint_changed=endpoint_attr.changed_total();
    const size_t endpoint_gross=endpoint_attr.gross_total();
    const unsigned endpoint_max_delta=endpoint_attr.worst_count!=0u ?
        endpoint_attr.worst[0].max_delta : 0u;
    const double endpoint_mean_error=
        (double)endpoint_error/(double)(596u*842u*3u);
    CHECK(std::isfinite(endpoint_mean_error) &&
          endpoint_mean_error>=0.0 && endpoint_mean_error<=255.0 &&
          endpoint_changed<=596u*842u && endpoint_gross<=endpoint_changed &&
          endpoint_max_delta<=255u);
    const std::string endpoint_attr_json=
        jspdf_pixel_attribution::json(endpoint_attr);
    unsigned long long endpoint_rgb_fnv64=14695981039346656037ULL;
    for(size_t y=0u;y<842u;++y) for(size_t x=0u;x<596u;++x) {
        const unsigned char *pixel=
            endpoint_composite.pixels+y*endpoint_composite.stride+x*4u;
        for(size_t c=0u;c<3u;++c) {
            endpoint_rgb_fnv64^=(unsigned long long)on_white(pixel,c);
            endpoint_rgb_fnv64*=1099511628211ULL;
        }
    }
    std::fprintf(stderr,
        "[jsPDF repeated endpoint preview, NOT pixel parity] "
        "error=%llu mean=%.9f changed=%zu gross=%zu max=%u "
        "rgb_fnv64=%016llx\n",
        (unsigned long long)endpoint_error,endpoint_mean_error,
        endpoint_changed,endpoint_gross,endpoint_max_delta,
        endpoint_rgb_fnv64);
    for(size_t i=0u;i<endpoint_attr.worst_count;++i) {
        const auto& h=endpoint_attr.worst[i];
        std::fprintf(stderr,
            "  endpoint-trial-residual %zu (%zu,%zu) delta=%u sum=%u "
            "stroke_alpha=%u fill_alpha=%u "
            "native=(%u,%u,%u) original_pdfium=(%u,%u,%u)\n",
            i,h.x,h.y,h.max_delta,h.rgb_error,
            h.stroke_alpha,h.fill_alpha,
            h.candidate[0],h.candidate[1],h.candidate[2],
            h.oracle[0],h.oracle[1],h.oracle[2]);
    }
    /* Third *explicit/private* C11 A/B: preserve the #286 same-source
     * repeated horizontal endpoints but soften only the exterior of a
     * real right-going horizontal -> downward vertical corner. The
     * independent source PDFium reference, original Detached IR, later
     * W-clipped Fill, output ownership and old raster negatives remain. */
    quantapdf_ir_raster_image corner_stroke{},corner_repeat{};
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &isolated_stroke,0u,&source_geometry,&stroke_grid_limits,
        &stroke_raster_limits,&corner_stroke));
    OK(quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
        &isolated_stroke,0u,&source_geometry,&stroke_grid_limits,
        &stroke_raster_limits,&corner_repeat));
    CHECK(corner_stroke.pixels!=nullptr &&
          corner_stroke.width==596u && corner_stroke.height==842u &&
          corner_stroke.size==endpoint_stroke.size &&
          corner_repeat.size==corner_stroke.size &&
          std::memcmp(corner_stroke.pixels,corner_repeat.pixels,
                      corner_stroke.size)==0);
    CHECK(corner_stroke.pixels[19u*corner_stroke.stride+50u*4u+3u]==
          endpoint_stroke.pixels[19u*endpoint_stroke.stride+50u*4u+3u]);
    CHECK(corner_stroke.pixels[19u*corner_stroke.stride+250u*4u+3u]>0u &&
          corner_stroke.pixels[19u*corner_stroke.stride+250u*4u+3u]<
          endpoint_stroke.pixels[19u*endpoint_stroke.stride+250u*4u+3u]);
    CHECK(corner_stroke.pixels[20u*corner_stroke.stride+250u*4u+3u]==
          endpoint_stroke.pixels[20u*endpoint_stroke.stride+250u*4u+3u]);

    quantapdf_ir_raster_image corner_composite{},corner_composite_repeat{};
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &corner_stroke,&source_fill,&composite_budget,&corner_composite));
    OK(quantapdf_ir_ordered_layer_composite_v0(
        &corner_repeat,&repeat_fill,&composite_budget,
        &corner_composite_repeat));
    CHECK(corner_composite.width==596u && corner_composite.height==842u &&
          corner_composite.stride==596u*4u &&
          corner_composite.size==596u*842u*4u &&
          corner_composite_repeat.size==corner_composite.size &&
          std::memcmp(corner_composite.pixels,
                      corner_composite_repeat.pixels,
                      corner_composite.size)==0);
    const auto corner_attr=jspdf_pixel_attribution::analyze(
        plane{corner_stroke.pixels,corner_stroke.stride,corner_stroke.size},
        plane{source_fill.pixels,source_fill.stride,source_fill.size},
        plane{corner_composite.pixels,corner_composite.stride,
              corner_composite.size},
        plane{reference.pixels.data(),(size_t)reference.stride,
              reference.pixels.size()},
        596u,842u);
    CHECK(corner_attr.valid);
    const std::uint64_t corner_error=corner_attr.error_total();
    const size_t corner_changed=corner_attr.changed_total();
    const size_t corner_gross=corner_attr.gross_total();
    const unsigned corner_max_delta=corner_attr.worst_count ?
        corner_attr.worst[0].max_delta : 0u;
    const double corner_mean_error=
        (double)corner_error/(double)(596u*842u*3u);
    CHECK(std::isfinite(corner_mean_error) &&
          corner_mean_error>=0.0 && corner_mean_error<=255.0 &&
          corner_changed<=596u*842u && corner_gross<=corner_changed &&
          corner_max_delta<=255u);
    const std::string corner_attr_json=
        jspdf_pixel_attribution::json(corner_attr);
    unsigned long long corner_rgb_fnv64=14695981039346656037ULL;
    for(size_t y=0u;y<842u;++y) for(size_t x=0u;x<596u;++x) {
        const unsigned char *pixel=
            corner_composite.pixels+y*corner_composite.stride+x*4u;
        for(size_t c=0u;c<3u;++c) {
            corner_rgb_fnv64^=(unsigned long long)on_white(pixel,c);
            corner_rgb_fnv64*=1099511628211ULL;
        }
    }
    std::fprintf(stderr,
        "[jsPDF right-angle corner A/B, NOT pixel parity] "
        "error=%llu mean=%.9f changed=%zu gross=%zu max=%u "
        "rgb_fnv64=%016llx\n",
        (unsigned long long)corner_error,corner_mean_error,
        corner_changed,corner_gross,corner_max_delta,corner_rgb_fnv64);
    for(size_t i=0u;i<corner_attr.worst_count;++i) {
        const auto& h=corner_attr.worst[i];
        std::fprintf(stderr,
            "  corner-trial-residual %zu (%zu,%zu) delta=%u sum=%u "
            "stroke_alpha=%u fill_alpha=%u "
            "native=(%u,%u,%u) original_pdfium=(%u,%u,%u)\n",
            i,h.x,h.y,h.max_delta,h.rgb_error,
            h.stroke_alpha,h.fill_alpha,
            h.candidate[0],h.candidate[1],h.candidate[2],
            h.oracle[0],h.oracle[1],h.oracle[2]);
    }

    /* Fourth, read-only original-page diagnostic: seven disjoint bins
     * partition ALL pixels into bottom single-pass Stroke, left/right
     * vertical Stroke, top repeated Stroke, W Clip right/bottom, and remaining.
     * The bins are anchored to the immutable jsPDF source, NOT raster
     * heuristics, a production renderer, or a PDFium fallback. */
    struct roi_error {
        const char *name;
        size_t pixels=0u, changed=0u, gross=0u, worst_x=0u, worst_y=0u;
        std::uint64_t abs_rgb_error=0u;
        unsigned max_delta=0u;
    };
    roi_error regions[]={
        {"bottom_single_pass_stroke"},{"right_vertical_stroke"},
        {"left_vertical_stroke"},{"top_repeated_stroke"},
        {"clip_right_antialias"},{"clip_bottom_weak_fringe"},{"other"}
    };
    const auto category=[](size_t x,size_t y,unsigned sa,unsigned fa)->size_t {
        if(x>=49u && x<=250u && y>=138u && y<=140u &&
           sa!=0u && fa==0u) return 0u;
        if(x>=249u && x<=251u && y>=22u && y<=137u &&
           sa!=0u && fa==0u) return 1u;
        if(x>=49u && x<=51u && y>=22u && y<=137u &&
           sa!=0u && fa==0u) return 2u;
        if(x>=49u && x<=250u && y>=19u && y<=21u &&
           sa!=0u && fa==0u) return 3u;
        if(x==150u && y>=21u && y<=99u &&
           sa==0u && fa!=0u) return 4u;
        if(x>=52u && x<=148u && y==100u &&
           sa==0u && fa==0u) return 5u;
        return 6u;
    };
    for(size_t y=0u;y<842u;++y) for(size_t x=0u;x<596u;++x) {
        const unsigned char *s=corner_stroke.pixels+
            y*corner_stroke.stride+x*4u;
        const unsigned char *f=source_fill.pixels+
            y*source_fill.stride+x*4u;
        const unsigned char *p=corner_composite.pixels+
            y*corner_composite.stride+x*4u;
        const unsigned char *o=reference.at(x,y);
        roi_error &r=regions[category(x,y,s[3],f[3])];
        ++r.pixels;
        unsigned dmax=0u;
        for(size_t c=0u;c<3u;++c) {
            const unsigned v=on_white(p,c),t=o[c];
            const unsigned d=v>=t?v-t:t-v;
            r.abs_rgb_error+=(std::uint64_t)d;
            if(d>dmax) dmax=d;
        }
        if(dmax!=0u) ++r.changed;
        if(dmax>64u) ++r.gross;
        if(dmax>r.max_delta) {
            r.max_delta=dmax;
            r.worst_x=x; r.worst_y=y;
        }
    }
    size_t region_pixels=0u,region_changed=0u,region_gross=0u;
    std::uint64_t region_error=0u;
    unsigned region_max=0u;
    std::string residual_regions_json=
        "{\"schema_version\":1,\"observation_only\":true,"
        "\"pixel_parity_qualified\":false,\"regions\":{";
    for(size_t i=0u;i<sizeof(regions)/sizeof(regions[0]);++i) {
        const roi_error &r=regions[i];
        CHECK(r.changed<=r.pixels && r.gross<=r.changed &&
              r.max_delta<=255u);
        region_pixels+=r.pixels;
        region_changed+=r.changed;
        region_gross+=r.gross;
        region_error+=r.abs_rgb_error;
        if(r.max_delta>region_max) region_max=r.max_delta;
        if(i) residual_regions_json+=",";
        residual_regions_json+="\""+std::string(r.name)+"\":{";
        residual_regions_json+="\"pixels\":"+std::to_string(r.pixels)+",";
        residual_regions_json+="\"changed\":"+std::to_string(r.changed)+",";
        residual_regions_json+="\"gross\":"+std::to_string(r.gross)+",";
        residual_regions_json+="\"abs_rgb_error\":"+
                                std::to_string(r.abs_rgb_error)+",";
        residual_regions_json+="\"max_channel_delta\":"+
                                std::to_string(r.max_delta)+",";
        residual_regions_json+="\"worst_x\":"+std::to_string(r.worst_x)+",";
        residual_regions_json+="\"worst_y\":"+std::to_string(r.worst_y)+"}";
    }
    CHECK(region_pixels==501832u && region_changed==corner_changed &&
          region_gross==corner_gross && region_error==corner_error &&
          region_max==corner_max_delta);
    residual_regions_json+="},\"anchors\":[";
    struct anchor {const char *name;size_t x,y,group;};
    const anchor anchors[]={
        {"bottom_left",50u,139u,0u},{"bottom_right",250u,139u,0u},
        {"clip_right",150u,21u,4u},{"clip_bottom",100u,100u,5u},
        {"repeated_corner",250u,19u,3u}
    };
    for(size_t i=0u;i<sizeof(anchors)/sizeof(anchors[0]);++i) {
        const anchor &a=anchors[i];
        const unsigned char *s=corner_stroke.pixels+
            a.y*corner_stroke.stride+a.x*4u;
        const unsigned char *f=source_fill.pixels+
            a.y*source_fill.stride+a.x*4u;
        const unsigned char *p=corner_composite.pixels+
            a.y*corner_composite.stride+a.x*4u;
        const unsigned char *o=reference.at(a.x,a.y);
        CHECK(category(a.x,a.y,s[3],f[3])==a.group);
        if(i) residual_regions_json+=",";
        residual_regions_json+="{\"name\":\""+std::string(a.name)+"\",";
        residual_regions_json+="\"region\":\""+
            std::string(regions[a.group].name)+"\",";
        residual_regions_json+="\"x\":"+std::to_string(a.x)+",";
        residual_regions_json+="\"y\":"+std::to_string(a.y)+",";
        residual_regions_json+="\"stroke_alpha\":"+
            std::to_string((unsigned)s[3])+",";
        residual_regions_json+="\"fill_alpha\":"+
            std::to_string((unsigned)f[3])+",";
        residual_regions_json+="\"native_rgb\":["+
            std::to_string(on_white(p,0u))+","+
            std::to_string(on_white(p,1u))+","+
            std::to_string(on_white(p,2u))+"],";
        residual_regions_json+="\"pdfium_rgb\":["+
            std::to_string((unsigned)o[0])+","+
            std::to_string((unsigned)o[1])+","+
            std::to_string((unsigned)o[2])+"]}";
    }
    residual_regions_json+="],\"full_image_partition_reconciled\":true}";
    std::fprintf(stderr,"[jsPDF pinned original residual region scan] %s\n",
                 residual_regions_json.c_str());

    quantapdf_ir_raster_image_dispose(&corner_composite_repeat);
    quantapdf_ir_raster_image_dispose(&corner_composite);
    quantapdf_ir_raster_image_dispose(&corner_repeat);
    quantapdf_ir_raster_image_dispose(&corner_stroke);

    quantapdf_ir_raster_image_dispose(&endpoint_composite_repeat);
    quantapdf_ir_raster_image_dispose(&endpoint_composite);
    quantapdf_ir_raster_image_dispose(&endpoint_repeat);
    quantapdf_ir_raster_image_dispose(&endpoint_stroke);

    std::fprintf(stderr,
        "[jsPDF horizontal 1pt preview, NOT pixel parity] "
        "error=%llu mean=%.9f changed=%zu gross_delta64=%zu "
        "max_delta=%u rgb_fnv64=%016llx\n",
        (unsigned long long)preview_abs_error,preview_mean_abs_rgb_error,
        preview_changed_pixels,preview_gross_delta64_pixels,
        preview_max_delta,preview_rgb_fnv64);
    quantapdf_ir_raster_image_dispose(&preview_ordered_repeat);
    quantapdf_ir_raster_image_dispose(&preview_ordered);
    quantapdf_ir_raster_image_dispose(&preview_repeat);
    quantapdf_ir_raster_image_dispose(&preview_stroke);

    /* Full-image QUANTITATIVE DIAGNOSTIC, not an independent assertion that
     * PDFium's coverage/sample-center contract has been reproduced.
     * Aggregate RGBA coverage can lose per-subpixel stroke/fill correlation
     * where two antialiased edges overlap. Record all errors explicitly. */
    const size_t composed_pixels=composite.width*composite.height;
    const size_t composed_rgb_samples=composed_pixels*3u;
    size_t composed_changed_pixels=0u,composed_gross_delta64_pixels=0u;
    size_t composed_partial_coverage_pixels=0u;
    size_t composed_partial_coverage_changed_pixels=0u;
    size_t composed_flat_coverage_changed_pixels=0u;
    unsigned composed_max_delta=0u;
    std::uint64_t composed_total_abs_rgb_error=0u;
    unsigned long long composed_rgb_fnv64=14695981039346656037ULL;
    for(size_t y=0u;y<composite.height;++y) {
        for(size_t x=0u;x<composite.width;++x) {
            const unsigned char *pixel=
                composite.pixels+y*composite.stride+x*4u;
            const unsigned char *old=
                source_stroke.pixels+y*source_stroke.stride+x*4u;
            const unsigned char *fill=
                source_fill.pixels+y*source_fill.stride+x*4u;
            const unsigned char *oracle=reference.at(x,y);
            const bool partial=(old[3]!=0u && old[3]!=255u) ||
                               (fill[3]!=0u && fill[3]!=255u);
            unsigned largest=0u;
            if(partial)++composed_partial_coverage_pixels;
            for(size_t c=0u;c<3u;++c) {
                const unsigned observed=on_white(pixel,c);
                const unsigned expected=oracle[c];
                const unsigned delta=(observed>=expected) ?
                    observed-expected : expected-observed;
                composed_total_abs_rgb_error+=(std::uint64_t)delta;
                composed_rgb_fnv64^=(unsigned long long)observed;
                composed_rgb_fnv64*=1099511628211ULL;
                if(delta>largest)largest=delta;
                if(delta>composed_max_delta)composed_max_delta=delta;
            }
            if(largest!=0u) {
                ++composed_changed_pixels;
                if(partial)++composed_partial_coverage_changed_pixels;
                else ++composed_flat_coverage_changed_pixels;
            }
            if(largest>64u)++composed_gross_delta64_pixels;
        }
    }
    const double composed_mean_abs_rgb_error=
        (double)composed_total_abs_rgb_error/(double)composed_rgb_samples;
    CHECK(composed_pixels==501832u && composed_rgb_samples==1505496u);
    CHECK(std::isfinite(composed_mean_abs_rgb_error) &&
          composed_mean_abs_rgb_error>=0.0 &&
          composed_mean_abs_rgb_error<=255.0);
    CHECK(composed_max_delta<=255u &&
          composed_changed_pixels<=composed_pixels &&
          composed_gross_delta64_pixels<=composed_changed_pixels &&
          composed_partial_coverage_pixels<=composed_pixels &&
          composed_partial_coverage_changed_pixels<=
              composed_partial_coverage_pixels &&
          composed_flat_coverage_changed_pixels+
              composed_partial_coverage_changed_pixels==
              composed_changed_pixels);
    /* An independent bounded PASS over exactly the same original pixels
     * now locates the large errors and the 100 mismatches whose local C11
     * coverage is flat. It never identifies PDFium's sampling algorithm
     * or authorizes an original native pixel-positive. */
    using jspdf_pixel_attribution::plane;
    const auto attribution=jspdf_pixel_attribution::analyze(
        plane{source_stroke.pixels,source_stroke.stride,source_stroke.size},
        plane{source_fill.pixels,source_fill.stride,source_fill.size},
        plane{composite.pixels,composite.stride,composite.size},
        plane{reference.pixels.data(),(size_t)reference.stride,
              reference.pixels.size()},
        composite.width,composite.height);
    CHECK(attribution.valid);
    CHECK(attribution.changed_total()==composed_changed_pixels);
    CHECK(attribution.gross_total()==composed_gross_delta64_pixels);
    CHECK(attribution.error_total()==composed_total_abs_rgb_error);
    CHECK(attribution.changed[0]==composed_flat_coverage_changed_pixels);
    CHECK(attribution.changed[1]+attribution.changed[2]+
          attribution.changed[3]==
          composed_partial_coverage_changed_pixels);
    CHECK(attribution.flat_near_partial_3x3+
          attribution.flat_far_from_partial_3x3==
          attribution.changed[0]);
    CHECK(attribution.worst_count<=
          jspdf_pixel_attribution::report::max_hotspots);
    if(composed_changed_pixels!=0u) {
        CHECK(attribution.worst_count>0u);
        CHECK(attribution.worst[0].max_delta==composed_max_delta);
    }
    const std::string attribution_json=
        jspdf_pixel_attribution::json(attribution);
    std::fprintf(stderr,
        "[jsPDF pixel attribution] flat=%zu (3x3-near-partial=%zu far=%zu), "
        "stroke-partial=%zu fill-partial=%zu both-partial=%zu "
        "uncovered-reference=%zu bbox=[%zu,%zu..%zu,%zu]\n",
        attribution.changed[0],attribution.flat_near_partial_3x3,
        attribution.flat_far_from_partial_3x3,
        attribution.changed[1],attribution.changed[2],
        attribution.changed[3],
        attribution.native_uncovered_reference_foreground,
        attribution.min_x,attribution.min_y,attribution.max_x,
        attribution.max_y);
    for(size_t i=0u;i<attribution.worst_count;++i) {
        const auto& h=attribution.worst[i];
        std::fprintf(stderr,
            "  pixel-hotspot %zu (%zu,%zu) delta=%u sum=%u "
            "stroke_alpha=%u fill_alpha=%u "
            "native=(%u,%u,%u) PDFium=(%u,%u,%u)\n",
            i,h.x,h.y,h.max_delta,h.rgb_error,
            h.stroke_alpha,h.fill_alpha,
            h.candidate[0],h.candidate[1],h.candidate[2],
            h.oracle[0],h.oracle[1],h.oracle[2]);
    }

    std::fprintf(stderr,
        "[jsPDF full-image diagnostic] original 596x842 Stroke->W-Fill "
        "mean_rgb=%.9f max_delta=%u gross_delta64=%zu "
        "changed=%zu fractional_coverage=%zu flat_changed=%zu "
        "rgb_fnv64=%016llx; NOT pixel parity certification\n",
        composed_mean_abs_rgb_error,composed_max_delta,
        composed_gross_delta64_pixels,composed_changed_pixels,
        composed_partial_coverage_pixels,
        composed_flat_coverage_changed_pixels,composed_rgb_fnv64);

    quantapdf_ir_raster_image_dispose(&composite_repeat);
    quantapdf_ir_raster_image_dispose(&composite);
    quantapdf_ir_raster_image_dispose(&repeat_fill);
    quantapdf_ir_raster_image_dispose(&source_fill);
    quantapdf_ir_raster_image_dispose(&repeat_stroke);
    quantapdf_ir_raster_image_dispose(&source_stroke);

    quantapdf_ir_dispose(&detached);
    CHECK(corpus_fixture_executions==0u);

    FILE *out=std::fopen(pending.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"quarantined_original_fractional_page_native_rejection\",\n"
        "  \"document_id\": \"jspdf.w3s-clip-v1\",\n"
        "  \"fixture_kind\": \"quarantined-external-original\",\n"
        "  \"observation_only\": true,\n"
        "  \"typed_native_lower_status\": \"lowered\",\n"
        "  \"native_raster_status\": \"unsupported\",\n"
        "  \"native_raster_rejection_reason\": \"fractional_page_points\",\n"
        "  \"page_width_points\": 595.28,\n"
        "  \"page_height_points\": 841.89,\n"
        "  \"detached_ir_painted_paths\": 2,\n"
        "  \"detached_ir_w_nonzero_clip_leaves\": 1,\n"
        "  \"detached_ir_wstar_clip_leaves\": 0,\n"
        "  \"source_and_graph_released_before_raster\": true,\n"
        "  \"native_partial_pixels_published\": false,\n"
        "  \"optin_grid_dimensions_qualified\": true,\n"
        "  \"optin_native_raster_status\": \"unsupported\",\n"
        "  \"optin_rejection_classification\": \"stroked_source_not_admitted\",\n"
        "  \"optin_original_stroke_paths\": %zu,\n"
        "  \"optin_original_fill_paths\": %zu,\n"
        "  \"optin_native_partial_pixels_published\": false,\n"
        "  \"isolated_original_stroke_native_status\": \"rasterized_candidate\",\n"
        "  \"isolated_original_stroke_native_nonzero_anchors\": 2,\n"
        "  \"isolated_original_stroke_pdfium_anchor_observations\": \"passed\",\n"
        "  \"isolated_original_stroke_native_repeat_equal\": true,\n"
        "  \"isolated_original_stroke_is_full_page_positive\": false,\n"
        "  \"isolated_original_fill_native_status\": \"rasterized_candidate\",\n"
        "  \"isolated_original_fill_native_clip_count\": 1,\n"
        "  \"isolated_original_fill_pdfium_anchor_observations\": \"passed\",\n"
        "  \"isolated_original_fill_native_repeat_equal\": true,\n"
        "  \"isolated_original_fill_is_full_page_positive\": false,\n"
        "  \"original_pdfium_default_dpi\": 72,\n"
        "  \"original_pdfium_geometry_oracle\": \"passed\",\n"
        "  \"original_pdfium_device_width\": 596,\n"
        "  \"original_pdfium_device_height\": 842,\n"
        "  \"original_pdfium_pixel_oracle\": \"compared_diagnostic_unqualified\",\n"
        "  \"ordered_native_composite_status\": \"full_image_measured_diagnostic\",\n"
        "  \"ordered_native_composite_sequence\": \"stroke_then_w_clipped_fill\",\n"
        "  \"ordered_native_composite_repeat_equal\": true,\n"
        "  \"ordered_native_composite_rgba_is_candidate\": true,\n"
        "  \"ordered_native_composite_pixel_parity_qualified\": false,\n"
        "  \"ordered_native_composite_pixel_count\": %zu,\n"
        "  \"ordered_native_composite_rgb_samples\": %zu,\n"
        "  \"ordered_native_composite_abs_rgb_error\": %llu,\n"
        "  \"ordered_native_composite_mean_abs_rgb_error\": %.9f,\n"
        "  \"ordered_native_composite_max_channel_delta\": %u,\n"
        "  \"ordered_native_composite_changed_pixels\": %zu,\n"
        "  \"ordered_native_composite_gross_delta64_pixels\": %zu,\n"
        "  \"ordered_native_composite_partial_coverage_pixels\": %zu,\n"
        "  \"ordered_native_composite_partial_coverage_changed_pixels\": %zu,\n"
        "  \"ordered_native_composite_flat_coverage_changed_pixels\": %zu,\n"
        "  \"ordered_native_composite_rgb_fnv64\": \"%016llx\",\n"
        "  \"ordered_native_spatial_attribution\": %s,\n"
        "  \"horizontal_thin_1pt_preview_status\": \"measured_unqualified\",\n"
        "  \"horizontal_thin_1pt_preview_default_changed\": false,\n"
        "  \"horizontal_thin_1pt_preview_pixel_parity_qualified\": false,\n"
        "  \"horizontal_thin_1pt_preview_repeat_equal\": true,\n"
        "  \"horizontal_thin_1pt_preview_original_pdfium_pixel_count\": 501832,\n"
        "  \"horizontal_thin_1pt_preview_abs_rgb_error\": %llu,\n"
        "  \"horizontal_thin_1pt_preview_mean_abs_rgb_error\": %.9f,\n"
        "  \"horizontal_thin_1pt_preview_changed_pixels\": %zu,\n"
        "  \"horizontal_thin_1pt_preview_gross_delta64_pixels\": %zu,\n"
        "  \"horizontal_thin_1pt_preview_max_channel_delta\": %u,\n"
        "  \"horizontal_thin_1pt_preview_rgb_fnv64\": \"%016llx\",\n"
        "  \"horizontal_thin_1pt_preview_spatial_attribution\": %s,\n"
        "  \"horizontal_repeated_endpoint_preview_status\": \"measured_unqualified\",\n"
        "  \"horizontal_repeated_endpoint_preview_default_changed\": false,\n"
        "  \"horizontal_repeated_endpoint_preview_pixel_parity_qualified\": false,\n"
        "  \"horizontal_repeated_endpoint_preview_original_pdfium_pixel_count\": 501832,\n"
        "  \"horizontal_repeated_endpoint_preview_repeat_equal\": true,\n"
        "  \"horizontal_repeated_endpoint_preview_abs_rgb_error\": %llu,\n"
        "  \"horizontal_repeated_endpoint_preview_mean_abs_rgb_error\": %.9f,\n"
        "  \"horizontal_repeated_endpoint_preview_changed_pixels\": %zu,\n"
        "  \"horizontal_repeated_endpoint_preview_gross_delta64_pixels\": %zu,\n"
        "  \"horizontal_repeated_endpoint_preview_max_channel_delta\": %u,\n"
        "  \"horizontal_repeated_endpoint_preview_rgb_fnv64\": \"%016llx\",\n"
        "  \"horizontal_repeated_endpoint_preview_spatial_attribution\": %s,\n"
        "  \"horizontal_right_down_corner_preview_status\": \"measured_unqualified\",\n"
        "  \"horizontal_right_down_corner_preview_default_changed\": false,\n"
        "  \"horizontal_right_down_corner_preview_pixel_parity_qualified\": false,\n"
        "  \"horizontal_right_down_corner_preview_original_pdfium_pixel_count\": 501832,\n"
        "  \"horizontal_right_down_corner_preview_repeat_equal\": true,\n"
        "  \"horizontal_right_down_corner_preview_abs_rgb_error\": %llu,\n"
        "  \"horizontal_right_down_corner_preview_mean_abs_rgb_error\": %.9f,\n"
        "  \"horizontal_right_down_corner_preview_changed_pixels\": %zu,\n"
        "  \"horizontal_right_down_corner_preview_gross_delta64_pixels\": %zu,\n"
        "  \"horizontal_right_down_corner_preview_max_channel_delta\": %u,\n"
        "  \"horizontal_right_down_corner_preview_rgb_fnv64\": \"%016llx\",\n"
        "  \"horizontal_right_down_corner_preview_spatial_attribution\": %s,\n"
        "  \"horizontal_right_down_corner_residual_regions\": %s,\n"
        "  \"native_pixel_positive_count\": 0,\n"
        "  \"official_corpus_admitted\": false,\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n",original_stroke_paths,original_fill_paths,
        composed_pixels,composed_rgb_samples,
        (unsigned long long)composed_total_abs_rgb_error,
        composed_mean_abs_rgb_error,composed_max_delta,
        composed_changed_pixels,composed_gross_delta64_pixels,
        composed_partial_coverage_pixels,
        composed_partial_coverage_changed_pixels,
        composed_flat_coverage_changed_pixels,composed_rgb_fnv64,
        attribution_json.c_str(),
        (unsigned long long)preview_abs_error,preview_mean_abs_rgb_error,
        preview_changed_pixels,preview_gross_delta64_pixels,
        preview_max_delta,preview_rgb_fnv64,
        repeated_preview_attribution_json.c_str(),
        (unsigned long long)endpoint_error,endpoint_mean_error,
        endpoint_changed,endpoint_gross,endpoint_max_delta,
        endpoint_rgb_fnv64,endpoint_attr_json.c_str(),
        (unsigned long long)corner_error,corner_mean_error,
        corner_changed,corner_gross,corner_max_delta,
        corner_rgb_fnv64,corner_attr_json.c_str(),
        residual_regions_json.c_str());
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(pending.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[fractional-page-jsPDF] typed=lowered raster=UNSUPPORTED "
        "page=595.28x841.89 device=596x842@72dpi W=1 W*=0 "
        "original=unchanged native_pixels=0\n");
    return EXIT_SUCCESS;
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
    rgb_image pdfium=render_original_pdfium(0u,filename);
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

int test_external_yworks_hidden_w_pixels(
    const char *filename,const char *report_path)
{
    CHECK(filename && *filename && report_path && *report_path);
    namespace fs=std::filesystem;
    std::error_code ec;
    const fs::path source=fs::path(filename);
    const fs::path report=fs::path(report_path);
    const std::string staged=std::string(report_path)+".tmp";
    const fs::path stage=fs::path(staged);
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
    CHECK(corpus_fixture_executions==0u);
    constexpr size_t WIDTH=500u,HEIGHT=500u;
    rgb_image pdfium=render_original_pdfium(0u,filename);
    CHECK(pdfium.width==(int)WIDTH && pdfium.height==(int)HEIGHT &&
          pdfium.components==3 && pdfium.stride>=(int)WIDTH*3);
    quantapdf_ir detached{};
    OK(snapshot_lower(&detached,0u,4u,8u,filename));
    CHECK(detached.page_count==1u && detached.pages!=nullptr &&
          detached.pages[0].width_points==(float)WIDTH &&
          detached.pages[0].height_points==(float)HEIGHT &&
          detached.operations!=nullptr && detached.operation_count==1u &&
          detached.resources.clip_count==3u &&
          detached.resources.graphics_state_count==1u &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.form_count==0u &&
          detached.resources.soft_mask_count==0u);
    const clip_leaf_counts winding=inspect_clip_fill_rules(detached);
    CHECK(winding.nonzero==2u && winding.evenodd==0u &&
          winding.intersections==1u);
    CHECK(detached.operations[0].value.path.options.fill==1 &&
          detached.operations[0].value.path.options.stroke==0 &&
          detached.operations[0].value.path.options.fill_argb==
              UINT32_C(0xffff0000));

    const quantapdf_ir_raster_v1_limits limits={
        WIDTH*HEIGHT,1u,768u,120000000u
    };
    quantapdf_ir_raster_image native{},repeat{};
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&native));
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&repeat));
    CHECK(native.pixels && native.width==WIDTH &&
          native.height==HEIGHT && native.stride==WIDTH*4u &&
          native.size==WIDTH*HEIGHT*4u &&
          repeat.size==native.size && repeat.pixels!=nullptr &&
          std::memcmp(native.pixels,repeat.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&detached);

    /* Fixed semantic probes from the independent upstream SVG: visible
     * red 25..75 square; hidden clip2 and empty clip3 must paint nothing. */
    struct pixel_probe {size_t x,y;unsigned r,g,b;};
    const pixel_probe probes[]={
        {50u,50u,255u,0u,0u},
        {10u,10u,255u,255u,255u},
        {90u,90u,255u,255u,255u},
        {50u,165u,255u,255u,255u},
        {50u,275u,255u,255u,255u},
        {250u,250u,255u,255u,255u}
    };
    for(const pixel_probe& p:probes) {
        const unsigned char *ref=pdfium.at(p.x,p.y);
        const unsigned char *raw=native.pixels+p.y*native.stride+4u*p.x;
        const unsigned want[]={p.r,p.g,p.b};
        for(size_t c=0;c<3;c++) {
            const int a=(int)on_white(raw,c),b=(int)ref[c],goal=(int)want[c];
            if(std::abs(a-goal)>8 || std::abs(b-goal)>8)
                std::fprintf(stderr,
                    "[hidden-W-golden] x=%zu y=%zu channel=%zu "
                    "native=%d pdfium=%d expected=%d\n",
                    p.x,p.y,c,a,b,goal);
            CHECK(std::abs(a-goal)<=8 && std::abs(b-goal)<=8);
        }
    }

    double total_error=0.0;
    size_t gross=0u,ref_red=0u,native_red=0u,shared_red=0u;
    for(size_t y=0u;y<HEIGHT;++y)for(size_t x=0u;x<WIDTH;++x) {
        const unsigned char *a=native.pixels+y*native.stride+4u*x;
        const unsigned char *b=pdfium.at(x,y);
        unsigned actual[3],worst=0u;
        for(size_t c=0u;c<3u;++c) {
            actual[c]=on_white(a,c);
            const unsigned diff=actual[c]>b[c]?
                actual[c]-b[c] : b[c]-actual[c];
            total_error+=(double)diff;
            if(diff>worst)worst=diff;
        }
        if(worst>64u)++gross;
        const bool ref=(b[0]>=220u && b[1]<=60u && b[2]<=60u);
        const bool nat=(actual[0]>=220u &&
                        actual[1]<=60u && actual[2]<=60u);
        ref_red+=(size_t)ref;
        native_red+=(size_t)nat;
        shared_red+=(size_t)(ref && nat);
    }
    const double mean=total_error/(double)(WIDTH*HEIGHT*3u);
    if(mean>3.0 || gross>WIDTH*HEIGHT/100u ||
       ref_red<2000u || native_red<2000u ||
       shared_red*20u<ref_red*19u)
        std::fprintf(stderr,
            "[hidden-W-parity] mean=%.6f gross=%zu "
            "ref_red=%zu native_red=%zu overlap=%zu\n",
            mean,gross,ref_red,native_red,shared_red);
    CHECK(mean<=3.0 && gross<=WIDTH*HEIGHT/100u);
    CHECK(ref_red>=2000u && native_red>=2000u &&
          shared_red*20u>=ref_red*19u);
    quantapdf_ir_raster_image_dispose(&native);
    CHECK(corpus_fixture_executions==0u);

    /* Stage only fully validated reports. Never delete prior evidence
     * ahead of preflight/pixel checks, and never overwrite source aliases. */
    FILE *out=std::fopen(staged.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"candidate_pixel_parity_passed_not_admitted\",\n"
        "  \"fixture_kind\": \"quarantined-external-original\",\n"
        "  \"document_id\": \"yworks.hidden-clippath-v1\",\n"
        "  \"reference_oracle\": \"original-pdfium\",\n"
        "  \"original_source_pdfium_oracle\": \"passed\",\n"
        "  \"detached_c11_native_raster\": \"passed\",\n"
        "  \"source_and_graph_released_before_raster\": true,\n"
        "  \"official_corpus_admitted\": false,\n"
        "  \"page_width\": %zu,\n"
        "  \"page_height\": %zu,\n"
        "  \"native_w_nonzero_clip_path_leaves\": 2,\n"
        "  \"native_wstar_evenodd_clip_path_leaves\": 0,\n"
        "  \"native_clip_intersection_nodes\": 1,\n"
        "  \"fixed_interior_goldens\": %zu,\n"
        "  \"repeat_rgba_identical\": true,\n"
        "  \"mean_rgb_error\": %.6f,\n"
        "  \"gross_error_pixels\": %zu,\n"
        "  \"reference_red_pixels\": %zu,\n"
        "  \"native_red_pixels\": %zu,\n"
        "  \"overlapping_red_pixels\": %zu,\n"
        "  \"external_wstar_pixel_supported_count\": 0,\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n",
        WIDTH,HEIGHT,sizeof(probes)/sizeof(probes[0]),
        mean,gross,ref_red,native_red,shared_red);
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(staged.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[candidate-hidden-W-pixel] passed mean=%.6f gross=%zu "
        "W=2 intersections=1 official_positive=0\n",mean,gross);
    return EXIT_SUCCESS;
}

int test_external_yworks_wstar_candidate_pixels(
    const char *filename,const char *report_path,bool admitted=false)
{
    CHECK(filename && *filename && report_path && *report_path);
    /* Preserve the immutable external original even if an output has a
     * different spelling, an intermediate symlink, or a hardlink alias. */
    namespace fs=std::filesystem;
    std::error_code path_error;
    const fs::path source=fs::path(filename);
    const fs::path report=fs::path(report_path);
    const std::string staged=std::string(report_path)+".tmp";
    const fs::path stage=fs::path(staged);
    CHECK(fs::is_regular_file(source,path_error) && !path_error);
    CHECK(!fs::is_symlink(source,path_error));
    CHECK(!path_error);
    const fs::path canonical_source=fs::weakly_canonical(source,path_error);
    CHECK(!path_error);
    const fs::path canonical_report=fs::weakly_canonical(report,path_error);
    CHECK(!path_error && canonical_source!=canonical_report);
    const fs::path canonical_stage=fs::weakly_canonical(stage,path_error);
    CHECK(!path_error && canonical_source!=canonical_stage);
    for(const fs::path& output:{report,stage}){
        const bool exists=fs::exists(output,path_error);
        CHECK(!path_error);
        if(exists){
            CHECK(!fs::is_symlink(output,path_error));
            CHECK(!path_error);
            CHECK(!fs::equivalent(source,output,path_error));
            CHECK(!path_error);
        }
    }
    CHECK(corpus_fixture_executions==0u);
    constexpr size_t WIDTH=600u, HEIGHT=600u;
    rgb_image pdfium=render_original_pdfium(0u,filename);
    CHECK(pdfium.width==(int)WIDTH && pdfium.height==(int)HEIGHT &&
          pdfium.components==3 && pdfium.stride>=(int)WIDTH*3);
    quantapdf_ir detached{};
    OK(snapshot_lower(&detached,0u,4u,8u,filename));
    CHECK(detached.pages && detached.page_count==1u &&
          detached.pages[0].width_points==(float)WIDTH &&
          detached.pages[0].height_points==(float)HEIGHT);
    CHECK(detached.operations && detached.operation_count==3u);
    CHECK(detached.resources.clip_count==7u &&
          detached.resources.graphics_state_count==3u &&
          detached.resources.font_count==0u &&
          detached.resources.image_count==0u &&
          detached.resources.form_count==0u &&
          detached.resources.soft_mask_count==0u);
    const clip_leaf_counts rules=inspect_clip_fill_rules(detached);
    CHECK(rules.nonzero==2u && rules.evenodd==2u &&
          rules.intersections==3u);

    const quantapdf_ir_raster_v1_limits limits={
        WIDTH*HEIGHT,3u,768u,420000000u
    };
    quantapdf_ir_raster_image native{},repeat{};
    const quantapdf_status raster_status =
        quantapdf_ir_raster_scoped_operations_v1(
            &detached,0u,&limits,&native);
    if(raster_status != QUANTAPDF_OK)
        std::fprintf(stderr,
            "[yworks-Wstar-raster] first-pass status=%d budget=%zu\n",
            (int)raster_status,limits.max_sample_tests);
    CHECK(raster_status == QUANTAPDF_OK);
    OK(quantapdf_ir_raster_scoped_operations_v1(
        &detached,0u,&limits,&repeat));
    CHECK(native.pixels && native.width==WIDTH && native.height==HEIGHT &&
          native.stride==WIDTH*4u && native.size==WIDTH*HEIGHT*4u);
    CHECK(repeat.pixels && repeat.size==native.size &&
          std::memcmp(repeat.pixels,native.pixels,native.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_dispose(&detached); /* No source/Graph/IR during comparison. */

    /* Independently published upstream SVG contains a nonzero-winding blue
     * star at x~80 and an even-odd blue star at x~180. Their self-crossing
     * centers must differ: nonzero paints center, even-odd leaves a hole.
     * PDFium AND C11 must separately match fixed interior semantic goldens. */
    struct sample {size_t x,y; unsigned red,green,blue;};
    const sample goldens[]={
        {80u,125u,0u,0u,255u},       /* W nonzero star center: painted */
        {180u,125u,255u,255u,255u}, /* W* even-odd star center: hole */
        {180u,95u,0u,0u,255u},      /* W* star tip: painted */
        {250u,125u,255u,255u,255u},
        {300u,300u,255u,255u,255u},
        {500u,500u,255u,255u,255u}
    };
    for(const sample& q:goldens){
        const unsigned char *b=pdfium.at(q.x,q.y);
        const unsigned char *a=native.pixels+q.y*native.stride+q.x*4u;
        const unsigned want[]={q.red,q.green,q.blue};
        for(size_t c=0u;c<3u;++c){
            const unsigned actual=on_white(a,c);
            if(std::abs((int)actual-(int)want[c])>8 ||
               std::abs((int)b[c]-(int)want[c])>8)
                std::fprintf(stderr,
                    "[yworks-Wstar-golden] x=%zu y=%zu c=%zu "
                    "expected=%u native=%u pdfium=%u\n",
                    q.x,q.y,c,want[c],actual,(unsigned)b[c]);
            CHECK(std::abs((int)actual-(int)want[c])<=8);
            CHECK(std::abs((int)b[c]-(int)want[c])<=8);
        }
    }
    double sum_diff=0.0;
    size_t gross=0u,ref_red=0u,native_red=0u,overlap_red=0u;
    for(size_t y=0u;y<HEIGHT;++y)for(size_t x=0u;x<WIDTH;++x){
        const unsigned char *a=native.pixels+y*native.stride+x*4u;
        const unsigned char *b=pdfium.at(x,y);
        unsigned delta_max=0u;
        unsigned rgb[3];
        for(size_t c=0u;c<3u;++c){
            rgb[c]=on_white(a,c);
            unsigned diff=rgb[c]>b[c] ? rgb[c]-b[c] : b[c]-rgb[c];
            sum_diff+=(double)diff;
            if(diff>delta_max)delta_max=diff;
        }
        if(delta_max>64u)++gross;
        if(y>=37u && y<=43u && x>=200u && x<=350u){
            const bool r=b[0]>=190u && b[1]<=150u && b[2]<=150u;
            const bool n=rgb[0]>=190u && rgb[1]<=150u && rgb[2]<=150u;
            ref_red+=(size_t)r;
            native_red+=(size_t)n;
            overlap_red+=(size_t)(r&&n);
        }
    }
    const double mean=sum_diff/(double)(WIDTH*HEIGHT*3u);
    if(mean>8.0 || gross>WIDTH*HEIGHT/20u ||
       ref_red<25u || native_red<25u ||
       overlap_red*2u<ref_red)
        std::fprintf(stderr,
            "[yworks-Wstar-pixel] mean=%.5f gross=%zu "
            "ref_red=%zu native_red=%zu overlap=%zu\n",
            mean,gross,ref_red,native_red,overlap_red);
    CHECK(mean<=8.0 && gross<=WIDTH*HEIGHT/20u);
    CHECK(ref_red>=25u && native_red>=25u &&
          overlap_red*2u>=ref_red);
    quantapdf_ir_raster_image_dispose(&native);
    CHECK(corpus_fixture_executions==0u);

    FILE *out=std::fopen(staged.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"corpus_id\": \"external-source-pdf\",\n"
        "  \"document_id\": \"yworks.clippath-cliprule-v1\",\n"
        "  \"reference_oracle\": \"original-pdfium\",\n"
        "  \"native_expected_outcome\": \"supported-path-clip\",\n"
        "  \"native_observed_outcome\": \"supported-path-clip\",\n"
        "  \"result\": \"%s\",\n"
        "  \"candidate\": \"yworks.clippath-cliprule-v1\",\n"
        "  \"fixture_kind\": \"%s\",\n"
        "  \"original_source_pdfium_oracle\": \"passed\",\n"
        "  \"detached_c11_native_raster\": \"passed\",\n"
        "  \"source_and_graph_released_before_raster\": true,\n"
        "  \"page_width\": %zu,\n"
        "  \"page_height\": %zu,\n"
        "  \"native_w_nonzero_clip_path_leaves\": %zu,\n"
        "  \"native_wstar_evenodd_clip_path_leaves\": %zu,\n"
        "  \"native_clip_intersection_nodes\": %zu,\n"
        "  \"fixed_interior_goldens\": %zu,\n"
        "  \"repeat_rgba_identical\": true,\n"
        "  \"mean_rgb_error\": %.6f,\n"
        "  \"gross_error_pixels\": %zu,\n"
        "  \"reference_red_pixels\": %zu,\n"
        "  \"native_red_pixels\": %zu,\n"
        "  \"overlap_red_pixels\": %zu,\n"
        "  \"native_path_clip_supported_count\": %u,\n"
        "  \"native_clipped_path_supported_count\": %u,\n"
        "  \"native_w_clip_supported_count\": %u,\n"
        "  \"native_wstar_clip_supported_count\": %u,\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n",
        admitted?"passed":"candidate_pixel_parity_passed_not_admitted",
        admitted?"external":"external_original_candidate_only",
        WIDTH,HEIGHT,rules.nonzero,rules.evenodd,rules.intersections,
        sizeof(goldens)/sizeof(goldens[0]),mean,gross,
        ref_red,native_red,overlap_red,
        admitted?1u:0u,admitted?1u:0u,admitted?1u:0u,admitted?1u:0u);
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(staged.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[yworks-Wstar-pixel] original-pdfium/C11 passed "
        "mean=%.5f gross=%zu repeat=identical pixel_positive=%u\n",
        mean,gross,admitted?1u:0u);
    return EXIT_SUCCESS;
}

/* Complete selected-page typed preflight of an unchanged ORIGINAL PDF.
 * This test-only diagnostic checks full bounded Graph -> detached IR lowering,
 * not keyword extraction. It deliberately runs neither PDFium oracle nor
 * native raster, and NEVER counts as positive native W* pixel conformance. */
/* Explicit test-only stage attribution for an unchanged external original.
 * No opaque production error is reclassified as pixel support. Unlike a
 * silent retry this is a separate opt-in CTest with the same fixed budget. */
int diagnose_original_stages(const char *source_path,const char *report_path)
{
    CHECK(source_path && *source_path && report_path && *report_path);
    namespace fs=std::filesystem;
    const std::string staged=std::string(report_path)+".tmp";
    std::error_code ec;
    const fs::path source=fs::path(source_path);
    const fs::path report=fs::path(report_path);
    const fs::path stage=fs::path(staged);
    CHECK(fs::is_regular_file(source,ec) && !ec);
    CHECK(!fs::is_symlink(source,ec) && !ec);
    const fs::path cs=fs::weakly_canonical(source,ec);
    CHECK(!ec);
    const fs::path cr=fs::weakly_canonical(report,ec);
    CHECK(!ec && cs!=cr);
    const fs::path ct=fs::weakly_canonical(stage,ec);
    CHECK(!ec && cs!=ct);
    for(const fs::path& output:{report,stage}) {
        const bool exists=fs::exists(output,ec);
        CHECK(!ec);
        if(exists) {
            CHECK(!fs::is_symlink(output,ec) && !ec);
            CHECK(!fs::equivalent(source,output,ec) && !ec);
        }
    }

    quantapdf_document *doc=nullptr;
    quantapdf_graph_snapshot *snapshot=nullptr;
    quantapdf_pdf_page_semantics *semantics=nullptr;
    quantapdf_graph_page_geometry geometry{};
    quantapdf_graph_page_display display{};
    quantapdf_ir detached{};
    quantapdf_status st=quantapdf_open(source_path,nullptr,&doc);
    if(st!=QUANTAPDF_OK) return 70;
    st=quantapdf_document_capture_graph(doc,&graph_limits,&snapshot);
    quantapdf_close(doc);
    if(st!=QUANTAPDF_OK) return 70;
    const char *failed_stage="none";
    size_t painted=0u,paths=0u,clips=0u,content_ops=0u;
    st=quantapdf_graph_query_page_geometry(snapshot->graph,0u,8u,&geometry);
    if(st!=QUANTAPDF_OK) failed_stage="page_geometry";
    if(st==QUANTAPDF_OK) {
        st=quantapdf_graph_page_display_map(&geometry,&display);
        if(st!=QUANTAPDF_OK) failed_stage="page_display";
    }
    if(st==QUANTAPDF_OK) {
        st=quantapdf_graph_admit_native_page_group(snapshot->graph,0u);
        if(st!=QUANTAPDF_OK) failed_stage="page_group";
    }
    if(st==QUANTAPDF_OK) {
        st=quantapdf_graph_snapshot_page_semantics(snapshot,0u,
            &decode_limits,&parse_limits,4u,&semantics);
        if(st!=QUANTAPDF_OK) failed_stage="page_semantics";
    }
    if(st==QUANTAPDF_OK) {
        CHECK(semantics!=nullptr && semantics->graphics!=nullptr &&
              semantics->content!=nullptr);
        paths=semantics->graphics->path_count;
        clips=semantics->graphics->clip_count;
        content_ops=semantics->content->count;
        for(size_t i=0u;i<paths;++i)
            if(semantics->graphics->paths[i].paint_opcode !=
               QUANTAPDF_PDF_OP_END_PATH)
                ++painted;
        const quantapdf_pdf_path_lower_limits diagnostic_limits={8u,256u};
        st=quantapdf_pdf_path_lower_to_composer_mapped(semantics,
            display.width_points,display.height_points,display.from_source,
            &diagnostic_limits,&detached);
        if(st!=QUANTAPDF_OK) failed_stage="path_lowering";
    }
    /* Diagnose the earliest incompatible ORIGINAL Path record using the
     * actual native lowerer on strictly bounded cumulative views. These
     * views borrow fully parsed semantics; only temporary IR is disposed.
     * No original rewrite, production retry, or pixel rendering occurs. */
    size_t first_failed_path=SIZE_MAX;
    size_t first_failed_operator=SIZE_MAX;
    size_t first_failed_terminal_operator=SIZE_MAX;
    size_t first_failed_clip_count=0u;
    size_t first_failed_is_clip=0u;
    int first_failed_status=0;
    int first_failed_paint_opcode=0;
    double first_failed_source_ctm[6]={0};
    if(std::strcmp(failed_stage,"path_lowering")==0) {
        CHECK(semantics!=nullptr && semantics->graphics!=nullptr);
        size_t prefix_clip_count=0u;
        const quantapdf_pdf_path_lower_limits diagnostic_limits={8u,256u};
        for(size_t i=0u;i<semantics->graphics->path_count;++i) {
            const quantapdf_pdf_path_record& record=semantics->graphics->paths[i];
            if(record.clip_after>prefix_clip_count)
                prefix_clip_count=record.clip_after;
            CHECK(prefix_clip_count<=semantics->graphics->clip_count);
            quantapdf_pdf_graphics_ir prefix_graphics=*semantics->graphics;
            prefix_graphics.path_count=i+1u;
            prefix_graphics.clip_count=prefix_clip_count;
            quantapdf_pdf_page_semantics prefix_page=*semantics;
            prefix_page.graphics=&prefix_graphics;
            quantapdf_ir prefix_ir{};
            const quantapdf_status prefix_status=
                quantapdf_pdf_path_lower_to_composer_mapped(
                    &prefix_page,display.width_points,display.height_points,
                    display.from_source,&diagnostic_limits,&prefix_ir);
            if(prefix_status==QUANTAPDF_OK) {
                quantapdf_ir_dispose(&prefix_ir);
                continue;
            }
            CHECK(prefix_status==QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(prefix_ir.pages==nullptr && prefix_ir.page_count==0u &&
                  prefix_ir.operations==nullptr && prefix_ir.operation_count==0u &&
                  prefix_ir.resources.clip_count==0u &&
                  prefix_ir.resources.graphics_state_count==0u);
            quantapdf_ir_dispose(&prefix_ir);
            first_failed_path=i;
            first_failed_operator=record.path_first_operator;
            first_failed_terminal_operator=record.terminal_operator;
            first_failed_clip_count=prefix_clip_count;
            first_failed_is_clip=record.clip_after!=record.graphics.clip_id?1u:0u;
            first_failed_paint_opcode=(int)record.paint_opcode;
            first_failed_status=(int)prefix_status;
            for(size_t k=0u;k<6u;++k)
                first_failed_source_ctm[k]=record.graphics.ctm[k];
            break;
        }
        CHECK(first_failed_path!=SIZE_MAX);
    }
    /* Verify the exact first failing W n Clip record's Composer CTM
     * admission algebra without changing the native implementation.
     * Composer command points are top-down: conjugating a source CTM
     * around the page-height flip can create a huge cancelling offset.
     * The path lowerer's strict finite +/-65536 guard rejects that value.
     */
    double first_failed_composer_f=0.0;
    if(std::strcmp(failed_stage,"path_lowering")==0) {
        CHECK(first_failed_path==3u && first_failed_operator==31u &&
              first_failed_terminal_operator==37u &&
              first_failed_clip_count==3u && first_failed_is_clip==1u &&
              first_failed_paint_opcode==(int)QUANTAPDF_PDF_OP_END_PATH);
        const double *source_ctm=first_failed_source_ctm;
        const double *page=display.from_source;
        const double mapped_d=source_ctm[2]*page[1]+source_ctm[3]*page[3];
        const double mapped_f=source_ctm[4]*page[1]+
                              source_ctm[5]*page[3]+page[5];
        first_failed_composer_f=(double)(float)display.height_points *
                                 (1.0-mapped_d)-mapped_f;
        CHECK(std::isfinite(first_failed_composer_f) &&
              std::fabs(first_failed_composer_f)>65536.0 &&
              std::fabs(first_failed_composer_f-240100.0)<1e-9);
    }
    const int code=(int)st;
    const bool unsupported=st==QUANTAPDF_ERROR_UNSUPPORTED;
    const bool lowered=st==QUANTAPDF_OK;
    if(!unsupported && !lowered) {
        quantapdf_ir_dispose(&detached);
        quantapdf_pdf_page_semantics_dispose(semantics);
        quantapdf_graph_snapshot_drop(snapshot);
        std::fprintf(stderr,
            "[original-stage-diag] unexpected result=%d stage=%s\n",
            code,failed_stage);
        return 71;
    }
    size_t native_pages=0u,native_ops=0u,native_clips=0u;
    size_t native_states=0u;
    clip_leaf_counts native_winding{};
    if(lowered) {
        CHECK(std::strcmp(failed_stage,"none")==0 &&
              detached.page_count==1u && detached.pages!=nullptr &&
              detached.operation_count==6u && detached.operations!=nullptr &&
              detached.resources.clip_count>=5u &&
              detached.resources.clip_count<=16u &&
              detached.resources.graphics_state_count<=8u &&
              detached.resources.font_count==0u &&
              detached.resources.image_count==0u &&
              detached.resources.form_count==0u &&
              detached.resources.soft_mask_count==0u);
        native_winding=inspect_clip_fill_rules(detached);
        CHECK(native_winding.nonzero==5u && native_winding.evenodd==0u);
        native_pages=detached.page_count;
        native_ops=detached.operation_count;
        native_clips=detached.resources.clip_count;
        native_states=detached.resources.graphics_state_count;
    } else {
        CHECK(detached.page_count==0u && detached.pages==nullptr &&
              detached.operation_count==0u && detached.operations==nullptr &&
              detached.resources.clip_count==0u &&
              detached.resources.graphics_state_count==0u);
    }
    quantapdf_ir_dispose(&detached);
    quantapdf_pdf_page_semantics_dispose(semantics);
    quantapdf_graph_snapshot_drop(snapshot);
    const std::string first_record=lowered?
        std::string("null"):std::to_string(first_failed_path);
    FILE *out=std::fopen(staged.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"test_only_original_source_stage_diagnostic\",\n"
        "  \"observation_only\": true,\n"
        "  \"original_source_provenance_verified\": false,\n"
        "  \"selected_page_index\": 0,\n"
        "  \"paint_budget\": 8,\n"
        "  \"native_lower_status\": \"%s\",\n"
        "  \"failed_stage\": \"%s\",\n"
        "  \"actual_status_code\": %d,\n"
        "  \"parsed_content_ops\": %zu,\n"
        "  \"parsed_path_records\": %zu,\n"
        "  \"parsed_clip_records\": %zu,\n"
        "  \"parsed_painted_paths\": %zu,\n"
        "  \"first_rejected_record_is_present\": %s,\n"
        "  \"first_rejected_path_record_index\": %s,\n"
        "  \"first_rejected_path_first_operator_index\": %zu,\n"
        "  \"first_rejected_path_terminal_operator_index\": %zu,\n"
        "  \"first_rejected_prefix_clip_count\": %zu,\n"
        "  \"first_rejected_introduces_clip\": %zu,\n"
        "  \"first_rejected_paint_opcode\": %d,\n"
        "  \"first_rejected_prefix_status\": %d,\n"
        "  \"first_rejected_source_ctm\": [%.17g, %.17g, %.17g, %.17g, %.17g, %.17g],\n"
        "  \"first_rejected_conjugated_y_translation\": %.17g,\n"
        "  \"composer_transform_component_abs_bound\": 65536,\n"
        "  \"first_rejected_numeric_guard\": \"%s\",\n"
        "  \"source_document_graph_released\": true,\n"
        "  \"detached_ir_pages\": %zu,\n"
        "  \"detached_ir_operations\": %zu,\n"
        "  \"detached_ir_clips\": %zu,\n"
        "  \"detached_ir_graphics_states\": %zu,\n"
        "  \"detached_ir_w_nonzero_clip_path_leaves\": %zu,\n"
        "  \"detached_ir_wstar_evenodd_clip_path_leaves\": %zu,\n"
        "  \"detached_ir_clip_intersection_nodes\": %zu,\n"
        "  \"original_pdfium_pixel_oracle\": \"not_run\",\n"
        "  \"native_raster\": \"not_run\",\n"
        "  \"external_wstar_pixel_supported_count\": 0,\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n",
        lowered?"lowered":"unsupported",
        failed_stage,code,content_ops,paths,clips,painted,
        lowered?"false":"true",first_record.c_str(),
        lowered?0u:first_failed_operator,
        lowered?0u:first_failed_terminal_operator,
        first_failed_clip_count,first_failed_is_clip,
        first_failed_paint_opcode,first_failed_status,
        first_failed_source_ctm[0],first_failed_source_ctm[1],
        first_failed_source_ctm[2],first_failed_source_ctm[3],
        first_failed_source_ctm[4],first_failed_source_ctm[5],
        first_failed_composer_f,
        lowered?"none":"conjugated_y_translation_exceeds_bound",
        native_pages,native_ops,native_clips,native_states,
        native_winding.nonzero,native_winding.evenodd,
        native_winding.intersections);
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
#ifdef _WIN32
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(staged.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[original-stage-diag] stage=%s status=%s parsed_paths=%zu "
        "clips=%zu painted=%zu owned_clip_nodes=%zu budget=8 pixels=not_run\n",
        failed_stage,lowered?"lowered":"unsupported",
        paths,clips,painted,native_clips);
    return 0;
}

int typed_original_preflight(const char *source_path,const char *page_str,
                             const char *expect,const char *report_path,
                             size_t max_painted_paths=3u)
{
    CHECK(source_path && *source_path && page_str && *page_str &&
          expect && *expect && report_path && *report_path);
    CHECK(std::strcmp(source_path,report_path)!=0);
    const std::string staged=std::string(report_path)+".tmp";
    CHECK(std::strcmp(source_path,staged.c_str())!=0);
    /* A candidate original is read-only. Never let report or staged path
     * aliases (including symlinked directories or hard links) overwrite
     * the original file under a different spelling. */
    namespace fs=std::filesystem;
    std::error_code path_error;
    const fs::path source=fs::path(source_path);
    const fs::path report=fs::path(report_path);
    const fs::path stage=fs::path(staged);
    CHECK(fs::is_regular_file(source,path_error) && !path_error);
    CHECK(!fs::is_symlink(source,path_error));
    const fs::path canonical_source=fs::weakly_canonical(source,path_error);
    CHECK(!path_error);
    const fs::path canonical_report=fs::weakly_canonical(report,path_error);
    CHECK(!path_error && canonical_source!=canonical_report);
    const fs::path canonical_stage=fs::weakly_canonical(stage,path_error);
    CHECK(!path_error && canonical_source!=canonical_stage);
    for(const fs::path& output:{report,stage}){
        const bool exists=fs::exists(output,path_error);
        CHECK(!path_error);
        if(exists){
            CHECK(!fs::is_symlink(output,path_error));
            CHECK(!path_error);
            CHECK(!fs::equivalent(source,output,path_error));
            CHECK(!path_error);
        }
    }
    const bool expect_lowered=std::strcmp(expect,"lowered")==0;
    const bool expect_rejected=std::strcmp(expect,"unsupported")==0;
    /* Source discovery can observe narrow typed status without guessing
     * an expected outcome; this is never independent pixel conformance. */
    const bool observe_only=std::strcmp(expect,"observe")==0;
    CHECK(expect_lowered || expect_rejected || observe_only);
    char *end=nullptr;
    errno=0;
    const unsigned long number=std::strtoul(page_str,&end,10);
    CHECK(errno==0 && end!=page_str && *end=='\0' && number<=255ul);
    const size_t page_index=(size_t)number;
    /* Previous report remains valid evidence if selected-page parsing
     * or strict typed admission fails. Do not unlink it preemptively. */
    quantapdf_ir detached{};
    /* Four content streams + eight page-tree steps are bounded by the
     * existing private Graph/Content limits. No partial prefix admitted. */
    CHECK(max_painted_paths>=lower_limits.max_painted_paths &&
          max_painted_paths<=8u);
    quantapdf_pdf_path_lower_limits bounded_limits=lower_limits;
    bounded_limits.max_painted_paths=max_painted_paths;
    const quantapdf_status status=snapshot_lower(
        &detached,page_index,4u,8u,source_path,&bounded_limits);
    /* Typed candidate rejection is not a generic crash/failure: return
     * distinct test-only codes and diagnostics so CI cannot mistake an
     * ordinary source error for the expected typed status mismatch. */
    if(status!=QUANTAPDF_OK && status!=QUANTAPDF_ERROR_UNSUPPORTED){
        std::fprintf(stderr,
            "[source-preflight-error] class=source_error status=%d\n",
            (int)status);
        quantapdf_ir_dispose(&detached);
        return 70;
    }
    const bool actual_lowered=status==QUANTAPDF_OK;
    if(!observe_only && actual_lowered!=expect_lowered){
        std::fprintf(stderr,
            "[source-preflight-error] class=status_mismatch expected=%s actual=%s\n",
            expect,actual_lowered?"lowered":"unsupported");
        quantapdf_ir_dispose(&detached);
        return 71;
    }
    size_t page_count=0u,op_count=0u,clip_count=0u,state_count=0u;
    clip_leaf_counts winding{};
    if(actual_lowered){
        CHECK(detached.pages!=nullptr && detached.page_count==1u &&
              detached.operations!=nullptr &&
              detached.operation_count>=1u &&
              detached.operation_count<=(max_painted_paths>4u?max_painted_paths:4u) &&
              detached.resources.clip_count<=(max_painted_paths>4u?16u:8u) &&
              detached.resources.graphics_state_count<=(max_painted_paths>4u?8u:4u) &&
              detached.resources.font_count==0u &&
              detached.resources.image_count==0u &&
              detached.resources.form_count==0u &&
              detached.resources.soft_mask_count==0u);
        page_count=detached.page_count;
        op_count=detached.operation_count;
        clip_count=detached.resources.clip_count;
        state_count=detached.resources.graphics_state_count;
        winding=inspect_clip_fill_rules(detached);
    } else {
        /* W* and painted Path may precede a late Text/XObject. Reject
         * transactionally: no pages, operations or owned resources leak. */
        CHECK(detached.pages==nullptr && detached.page_count==0u &&
              detached.operations==nullptr &&
              detached.operation_count==0u &&
              detached.resources.clips==nullptr &&
              detached.resources.clip_count==0u &&
              detached.resources.graphics_states==nullptr &&
              detached.resources.graphics_state_count==0u &&
              detached.resources.font_count==0u &&
              detached.resources.image_count==0u &&
              detached.resources.form_count==0u &&
              detached.resources.soft_mask_count==0u &&
              detached.resources.paint_count==0u);
    }
    quantapdf_ir_dispose(&detached);
    CHECK(corpus_fixture_executions==0u);
    FILE *out=std::fopen(staged.c_str(),"wb");
    CHECK(out!=nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"result\": \"typed_selected_page_preflight_only\",\n"
        "%s"
        "  \"original_source_provenance_verified\": false,\n"
        "  \"selected_page_index\": %zu,\n"
        "  \"native_lower_status\": \"%s\",\n"
        "  \"source_document_graph_released\": true,\n"
        "  \"detached_ir_pages\": %zu,\n"
        "  \"detached_ir_operations\": %zu,\n"
        "  \"detached_ir_clips\": %zu,\n"
        "  \"detached_ir_w_nonzero_clip_path_leaves\": %zu,\n"
        "  \"detached_ir_wstar_evenodd_clip_path_leaves\": %zu,\n"
        "  \"detached_ir_clip_intersection_nodes\": %zu,\n"
        "  \"detached_ir_graphics_states\": %zu,\n"
        "  \"original_pdfium_pixel_oracle\": \"not_run\",\n"
        "  \"native_raster\": \"not_run\",\n"
        "  \"external_wstar_pixel_supported_count\": 0,\n"
        "  \"generated_fixture_count\": 0\n"
        "}\n",
        observe_only?"  \"observation_only\": true,\n":"",
        page_index,actual_lowered?"lowered":"unsupported",
        page_count,op_count,clip_count,
        winding.nonzero,winding.evenodd,winding.intersections,state_count);
    CHECK(std::ferror(out)==0 && std::fclose(out)==0);
#ifdef _WIN32
    /* Windows rename cannot replace an existing report. Publish only after
     * the new staged report is fully written and closed. */
    errno=0;
    if(std::remove(report_path)!=0) CHECK(errno==ENOENT);
#endif
    CHECK(std::rename(staged.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[source-preflight] page=%zu typed=%s raster=not_run "
        "pdfium_pixel=not_run wstar_positive=0\n",
        page_index,actual_lowered?"lowered":"unsupported");
    return EXIT_SUCCESS;
}


/* Synthetic check for the actual typed observation mechanism. The generated
 * PDF is NOT an independently published PDF and never counts as external. */
int test_generated_wstar_typed_observe(const char *report_path)
{
    CHECK(report_path!=nullptr && *report_path!='\0');
    const char *content=
        "q 0 0 100 100 re W n "
        "q 20 20 60 60 re 35 35 20 20 re W* n "
        "1 0 0 rg 0 0 120 120 re f "
        "Q 0 0 1 rg 75 10 40 40 re f "
        "Q 0 1 0 rg 0 0 12 12 re f";
    /* Use an isolated build-local source path to avoid concurrent CTest
     * collisions with the existing native SOURCE_QQ_PDF pixel fixtures. */
    make_source(content,GENERATED_WSTAR_OBSERVE_PDF);
    const int rc=typed_original_preflight(
        GENERATED_WSTAR_OBSERVE_PDF,"0","observe",report_path);
    if(rc!=EXIT_SUCCESS) return rc;
    FILE *in=std::fopen(report_path,"rb");
    CHECK(in!=nullptr);
    char bytes[4096];
    const size_t n=std::fread(bytes,1,sizeof(bytes),in);
    CHECK(n>0u && n<sizeof(bytes) && !std::ferror(in));
    CHECK(std::fclose(in)==0);
    const std::string observed(bytes,n);
    CHECK(observed.find("\"observation_only\": true")!=std::string::npos);
    CHECK(observed.find("\"original_source_provenance_verified\": false")!=std::string::npos);
    CHECK(observed.find("\"native_lower_status\": \"lowered\"")!=std::string::npos);
    CHECK(observed.find("\"detached_ir_pages\": 1")!=std::string::npos);
    CHECK(observed.find("\"detached_ir_operations\": 3")!=std::string::npos);
    CHECK(observed.find("\"detached_ir_w_nonzero_clip_path_leaves\": 1")!=std::string::npos);
    CHECK(observed.find("\"detached_ir_wstar_evenodd_clip_path_leaves\": 1")!=std::string::npos);
    CHECK(observed.find("\"detached_ir_clip_intersection_nodes\": ")!=std::string::npos);
    CHECK(observed.find("\"original_pdfium_pixel_oracle\": \"not_run\"")!=std::string::npos);
    CHECK(observed.find("\"native_raster\": \"not_run\"")!=std::string::npos);
    CHECK(observed.find("\"external_wstar_pixel_supported_count\": 0")!=std::string::npos);
    CHECK(observed.find("\"generated_fixture_count\": 0")!=std::string::npos);
    CHECK(corpus_fixture_executions==0u);
    std::fprintf(stderr,
        "[generated-wstar-observe] typed=lowered w=1 wstar=1 "
        "pdfium_pixel=not_run native_raster=not_run external_positive=0\n");
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

int main(int argc,char **argv)
{
    if(argc==5 &&
       std::strcmp(argv[1],"--diagnose-original-pdf")==0 &&
       std::strcmp(argv[3],"--diagnostic-report")==0)
        return diagnose_original_stages(argv[2],argv[4]);
    if(argc==11 &&
       std::strcmp(argv[1],"--preflight-original-pdf")==0 &&
       std::strcmp(argv[3],"--page-index")==0 &&
       std::strcmp(argv[5],"--expect-status")==0 &&
       std::strcmp(argv[7],"--preflight-report")==0 &&
       std::strcmp(argv[9],"--max-painted-paths")==0) {
        char *tail=nullptr;
        errno=0;
        const unsigned long budget=std::strtoul(argv[10],&tail,10);
        if(errno!=0 || tail==argv[10] || *tail!='\0' ||
           budget<3ul || budget>8ul)
            return 70; /* Explicit test-only budget, never a fallback. */
        return typed_original_preflight(argv[2],argv[4],argv[6],argv[8],
                                        (size_t)budget);
    }
    if(argc==9 &&
       std::strcmp(argv[1],"--preflight-original-pdf")==0 &&
       std::strcmp(argv[3],"--page-index")==0 &&
       std::strcmp(argv[5],"--expect-status")==0 &&
       std::strcmp(argv[7],"--preflight-report")==0)
        return typed_original_preflight(argv[2],argv[4],argv[6],argv[8]);
    if(argc==4 &&
       std::strcmp(argv[1],"--generated-wstar-observe")==0 &&
       std::strcmp(argv[2],"--preflight-report")==0)
        return test_generated_wstar_typed_observe(argv[3]);
    if(argc==5 &&
       std::strcmp(argv[1],"--external-jspdf-vertical-analytic-ab")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_jspdf_single_vertical_analytic_ab(
            argv[2],argv[4]);
    if(argc==5 &&
       std::strcmp(argv[1],"--external-fractional-jspdf-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_jspdf_fractional_page_rejection(
            argv[2],argv[4]);
    if(argc==5 &&
       std::strcmp(argv[1],"--external-svg-w-candidate-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_yworks_svg_w_pixels(argv[2],argv[4]);
    if(argc==5 &&
       std::strcmp(argv[1],"--external-hidden-w-candidate-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_yworks_hidden_w_pixels(argv[2],argv[4]);
    if(argc==5 &&
       std::strcmp(argv[1],"--external-wstar-candidate-pixel-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_yworks_wstar_candidate_pixels(argv[2],argv[4]);
    if(argc==5 &&
       std::strcmp(argv[1],"--external-wstar-original-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_yworks_wstar_candidate_pixels(argv[2],argv[4],true);
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
    if(argc==5 && std::strcmp(argv[1],"--external-wstar-text-pdf")==0 &&
       std::strcmp(argv[3],"--external-report")==0)
        return test_external_pdfium_wstar_text_rejection(argv[2],argv[4]);
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
