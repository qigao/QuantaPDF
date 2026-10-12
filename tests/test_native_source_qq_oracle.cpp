/* Test-only source-PDF q/Q conformance. PDFium is an independent oracle:
 * Original source PDF -> PDFium versus Source -> detached Graph -> owned IR
 * -> private pure-C11 scoped Clip DAG raster. Never export IR to PDF as a
 * production rendering intermediate and never invoke a PDFium fallback. */
#include "document/page_composer_ir.h"
#include "render/ir_scoped_operations_v1.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
                                size_t max_parent_steps=2u)
{
    quantapdf_document *doc=nullptr;
    quantapdf_graph_snapshot *snap=nullptr;
    quantapdf_status st;
    CHECK(out != nullptr);
    std::memset(out,0,sizeof(*out));
    st = quantapdf_open(SOURCE_QQ_PDF,nullptr,&doc);
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

rgb_image render_original_pdfium(size_t page_index=0u)
{
    rgb_image out;
    quantapdf_document *doc=nullptr;
    quantapdf_page *page=nullptr;
    quantapdf_bitmap *bitmap=nullptr;
    const unsigned char *bytes=nullptr;
    size_t size=0u;
    OK(quantapdf_open(SOURCE_QQ_PDF,nullptr,&doc));
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

void compare(const char *content, const probe *goldens,
             size_t golden_count, size_t min_clip_resources)
{
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
void test_inherited_extgstate_fail_closed()
{
    struct scenario { const char *resources; quantapdf_status expected; };
    const scenario cases[]={
        {"<< /ExtGState << /Other << /ca 1 >> >> >>", QUANTAPDF_ERROR_FORMAT},
        {"<< /ExtGState << /GS << /SMask /None >> >> >>",
         QUANTAPDF_ERROR_UNSUPPORTED}
    };
    for(const auto& tc:cases) {
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
    quantapdf_ir failed{};
    make_source("q BT ET Q");
    quantapdf_status st=snapshot_lower(&failed);
    CHECK(st==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(failed.pages==nullptr && failed.operations==nullptr &&
          failed.operation_count==0u && failed.resources.clip_count==0u);
    CHECK(std::remove(SOURCE_QQ_PDF)==0);
}
}

int main()
{
    test_nested_restore();
    test_disjoint_clip_restoration();
    test_evenodd_cancelled_clip_restore();
    test_evenodd_restore();
    test_clip_definition_ctm();
    test_page_geometry_matrix();
    test_selected_page_multistream();
    test_zero_segment_clip_native_restore();
    test_multilevel_page_tree_override();
    test_invalid_inherited_geometry();
    test_inherited_extgstate_fail_closed();
    test_unsupported_operator_fails_closed();
    std::fprintf(stderr,"[quantapdf.native_source_qq_oracle] complete\n");
    return EXIT_SUCCESS;
}
