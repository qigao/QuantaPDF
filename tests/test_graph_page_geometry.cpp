#include "backend/qpdf_graph.h"
#include "backend/qpdf_composer.h"
#include "document/page_composer_ir.h"
#include "graph/graph_page_geometry.h"
#include "graph/graph_edit.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

using owner = std::unique_ptr<quantapdf_graph, decltype(&quantapdf_graph_drop)>;
static quantapdf_graph_limits graph_budget() { return {10000, 30000, 1048576, 1048576, 4194304, 256}; }
static quantapdf_graph_stream_limits decode_budget() { return {4096, 128, 100000}; }
static quantapdf_pdf_content_limits content_budget() { return {4096, 128, 16}; }
static quantapdf_pdf_path_lower_limits lower_budget() { return {64, 1024}; }

struct fixture {
    std::shared_ptr<QPDF> pdf;
    QPDFObjectHandle root, branch, page;
};

static fixture build()
{
    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto root = pdf->getRoot().getKey("/Pages");
    auto branch = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Pages /Count 1 >>"));
    auto page = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Page >>"));
    root.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 200 120]"));
    root.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    root.replaceKey("/Kids", QPDFObjectHandle::newArray({branch}));
    branch.replaceKey("/Parent", root);
    branch.replaceKey("/Kids", QPDFObjectHandle::newArray({page}));
    page.replaceKey("/Parent", branch);
    page.replaceKey("/Contents", pdf->newStream("0 0 1 rg 10 10 20 20 re f"));
    return {pdf, root, branch, page};
}

static owner capture(QPDF& pdf)
{
    quantapdf_graph *raw = nullptr;
    auto budget = graph_budget();
    OK(quantapdf_qpdf_import_graph(pdf, &budget, &raw));
    CHECK(raw != nullptr);
    return {raw, quantapdf_graph_drop};
}

static quantapdf_graph_page_geometry geometry(quantapdf_graph *graph)
{
    quantapdf_graph_page_geometry result{};
    OK(quantapdf_graph_query_page_geometry(graph, 0, 3, &result));
    return result;
}

static void test_inherited_geometry()
{
    auto f = build();
    f.branch.replaceKey("/UserUnit", QPDFObjectHandle::newInteger(7));
    auto graph = capture(*f.pdf);
    auto g = geometry(graph.get());
    CHECK(g.user_unit == 1 && g.rotation == 0);
    CHECK(g.media_box[0] == 0 && g.media_box[2] == 200 && g.media_box[3] == 120);
    CHECK(std::memcmp(g.media_box, g.crop_box, sizeof(g.crop_box)) == 0);
    double width = 0, height = 0;
    OK(quantapdf_graph_canonical_page_dimensions(&g, &width, &height));
    CHECK(width == 200 && height == 120);

    quantapdf_graph_page_geometry bad{};
    CHECK(quantapdf_graph_query_page_geometry(graph.get(), 0, 2, &bad) ==
        QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(bad.user_unit == 0 && bad.media_box[2] == 0);
    CHECK(quantapdf_graph_query_page_geometry(graph.get(), 1, 3, &bad) ==
        QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_graph_query_page_geometry(graph.get(), 0, 0, &bad) ==
        QUANTAPDF_ERROR_ARGUMENT);
    width = height = 999;
    CHECK(quantapdf_graph_canonical_page_dimensions(nullptr, &width, &height) ==
        QUANTAPDF_ERROR_ARGUMENT && width == 0 && height == 0);
}

static void test_overrides_and_noncanonical()
{
    auto check = [](std::function<void(fixture&)> mutate,
        quantapdf_status query_status, quantapdf_status canonical_status) {
        auto f = build();
        mutate(f);
        auto graph = capture(*f.pdf);
        quantapdf_graph_page_geometry g{};
        CHECK(quantapdf_graph_query_page_geometry(graph.get(), 0, 3, &g) == query_status);
        if (query_status == QUANTAPDF_OK) {
            double width = 111, height = 222;
            CHECK(quantapdf_graph_canonical_page_dimensions(&g, &width, &height) ==
                canonical_status);
            if (canonical_status != QUANTAPDF_OK)
                CHECK(width == 0 && height == 0);
        }
    };

    check([](fixture& f) {
        f.root.replaceKey("/Rotate", QPDFObjectHandle::newInteger(90));
    }, QUANTAPDF_OK, QUANTAPDF_ERROR_UNSUPPORTED);
    check([](fixture& f) {
        f.root.replaceKey("/Rotate", QPDFObjectHandle::newInteger(90));
        f.branch.replaceKey("/Rotate", QPDFObjectHandle::newInteger(-360));
    }, QUANTAPDF_OK, QUANTAPDF_OK);
    check([](fixture& f) {
        f.branch.replaceKey("/Rotate", QPDFObjectHandle::newInteger(90));
        f.page.replaceKey("/Rotate", QPDFObjectHandle::newInteger(0));
    }, QUANTAPDF_OK, QUANTAPDF_OK);
    check([](fixture& f) {
        f.page.replaceKey("/UserUnit", QPDFObjectHandle::newInteger(2));
    }, QUANTAPDF_OK, QUANTAPDF_ERROR_UNSUPPORTED);
    check([](fixture& f) {
        f.page.replaceKey("/UserUnit", QPDFObjectHandle::newInteger(0));
    }, QUANTAPDF_ERROR_FORMAT, QUANTAPDF_OK);
    check([](fixture& f) {
        f.root.replaceKey("/CropBox", QPDFObjectHandle::parse("[0 0 100 120]"));
    }, QUANTAPDF_OK, QUANTAPDF_ERROR_UNSUPPORTED);
    check([](fixture& f) {
        f.branch.replaceKey("/CropBox", QPDFObjectHandle::parse("[0 0 100 120]"));
        f.page.replaceKey("/CropBox", QPDFObjectHandle::parse("[0 0 200 120]"));
    }, QUANTAPDF_OK, QUANTAPDF_OK);
    check([](fixture& f) {
        f.page.replaceKey("/MediaBox", QPDFObjectHandle::parse("[10 0 210 120]"));
    }, QUANTAPDF_OK, QUANTAPDF_ERROR_UNSUPPORTED);
    check([](fixture& f) {
        f.page.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 200.125 120]"));
    }, QUANTAPDF_OK, QUANTAPDF_OK);
    check([](fixture& f) {
        f.root.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 200]"));
    }, QUANTAPDF_ERROR_FORMAT, QUANTAPDF_OK);
    check([](fixture& f) {
        f.root.replaceKey("/MediaBox", QPDFObjectHandle::newNull());
    }, QUANTAPDF_ERROR_FORMAT, QUANTAPDF_OK);
    check([](fixture& f) {
        f.root.replaceKey("/Rotate", QPDFObjectHandle::newInteger(45));
    }, QUANTAPDF_ERROR_FORMAT, QUANTAPDF_OK);
    check([](fixture& f) {
        f.page.replaceKey("/Parent", f.root); /* /Kids still lists branch. */
    }, QUANTAPDF_ERROR_FORMAT, QUANTAPDF_OK);
    check([](fixture& f) {
        f.root.replaceKey("/Parent", f.branch);
    }, QUANTAPDF_ERROR_FORMAT, QUANTAPDF_OK);
}

/* CropBox/MediaBox intersection policy is explicit: both a partial
 * overlap and a disjoint, otherwise-valid CropBox are UNSUPPORTED. Do not
 * silently clip to an intersection or report a successful empty page. */
static void test_crop_media_intersection_policy()
{
    const char *crop_boxes[] = {
        "[-10 10 100 110]", /* partial overlap; extends past the left edge */
        "[210 10 220 20]"   /* valid box, completely outside MediaBox */
    };
    for (const char *crop : crop_boxes) {
        auto f = build();
        f.page.replaceKey("/CropBox", QPDFObjectHandle::parse(crop));
        auto graph = capture(*f.pdf);
        quantapdf_graph_page_geometry g{};
        OK(quantapdf_graph_query_page_geometry(graph.get(), 0, 3, &g));

        quantapdf_graph_page_display display{};
        display.width_points = 999;
        display.height_points = 999;
        for (double &v : display.from_source) v = 999;
        CHECK(quantapdf_graph_page_display_map(&g, &display) ==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(display.width_points == 0 && display.height_points == 0);
        for (double v : display.from_source) CHECK(v == 0);

        double width = 999, height = 999;
        CHECK(quantapdf_graph_canonical_page_dimensions(&g, &width, &height) ==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(width == 0 && height == 0);
    }
}

static quantapdf_local_id media_upper_right_x(const quantapdf_graph_model *model)
{
    auto pages = quantapdf_graph_find(model, model->catalog, "/Pages", 6);
    CHECK(pages != nullptr);
    auto media = quantapdf_graph_find(model, pages->target, "/MediaBox", 9);
    CHECK(media != nullptr);
    auto box = quantapdf_graph_node_at(model, media->target);
    CHECK(box->kind == QUANTAPDF_GRAPH_ARRAY && box->edge_count == 4);
    return model->edges[box->edge_offset + 2].target;
}

static void replacement_for_x(
    const quantapdf_graph *graph, int64_t x,
    quantapdf_graph_change_set *change, quantapdf_graph_replacement *replacement)
{
    auto model = quantapdf_graph_view(graph);
    quantapdf_local_id id = media_upper_right_x(model);
    auto original = quantapdf_graph_node_at(model, id);
    CHECK(original->kind == QUANTAPDF_GRAPH_INTEGER);
    *replacement = {};
    replacement->id = id;
    replacement->value.kind = QUANTAPDF_GRAPH_INTEGER;
    replacement->value.indirect = original->indirect;
    replacement->value.integer = x;
    *change = {};
    change->lineage = quantapdf_graph_lineage(graph);
    change->base_version = quantapdf_graph_version(graph);
    change->replacements = replacement;
    change->replacement_count = 1;
}

static void test_selected_version()
{
    auto f = build();
    auto base = capture(*f.pdf);
    quantapdf_graph_change_set change{};
    quantapdf_graph_replacement replacement{};
    replacement_for_x(base.get(), 350, &change, &replacement);
    quantapdf_graph *next = nullptr;
    quantapdf_validation_report report{};
    auto budget = graph_budget();
    OK(quantapdf_graph_commit(base.get(), &change, &budget, &next, &report));
    owner descendant(next, quantapdf_graph_drop);
    double width = 0, height = 0;
    auto before = geometry(base.get()), after = geometry(descendant.get());
    OK(quantapdf_graph_canonical_page_dimensions(&before, &width, &height));
    CHECK(width == 200);
    OK(quantapdf_graph_canonical_page_dimensions(&after, &width, &height));
    CHECK(width == 350);
    base.reset();
    CHECK(geometry(descendant.get()).media_box[2] == 350);
}

static void write_to_file(QPDF& pdf, const char *path)
{
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.write();
    auto bytes = writer.getBufferSharedPointer();
    FILE *file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(bytes->getBuffer(), 1, bytes->getSize(), file) == bytes->getSize());
    CHECK(std::fclose(file) == 0);
}

static void test_snapshot_to_composer()
{
    auto f = build();
    write_to_file(*f.pdf, GEOMETRY_SOURCE_PDF);
    quantapdf_document *document = nullptr;
    quantapdf_graph_snapshot *snapshot = nullptr, *descendant = nullptr;
    auto gb = graph_budget();
    auto db = decode_budget();
    auto cb = content_budget();
    auto lb = lower_budget();
    OK(quantapdf_open(GEOMETRY_SOURCE_PDF, nullptr, &document));
    OK(quantapdf_document_capture_graph(document, &gb, &snapshot));
    quantapdf_close(document);
    CHECK(snapshot != nullptr);

    quantapdf_ir ir{};
    OK(quantapdf_graph_snapshot_page_composer_ir(snapshot, 0, 3,
        &db, &cb, 1, &lb, &ir));
    CHECK(ir.page_count == 1 && ir.operation_count == 1 &&
        ir.pages[0].width_points == 200 && ir.pages[0].height_points == 120);
    quantapdf_ir_dispose(&ir);

    /* Selected immutable version changes the page size, not the old capture. */
    quantapdf_graph_change_set change{};
    quantapdf_graph_replacement replacement{};
    quantapdf_validation_report report{};
    replacement_for_x(snapshot->graph, 300, &change, &replacement);
    OK(quantapdf_graph_snapshot_commit(snapshot, &change, &gb, &descendant, &report));
    OK(quantapdf_graph_snapshot_page_composer_ir(snapshot, 0, 3,
        &db, &cb, 1, &lb, &ir));
    CHECK(ir.pages[0].width_points == 200);
    quantapdf_ir_dispose(&ir);
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(descendant != nullptr);

    OK(quantapdf_graph_snapshot_page_composer_ir(descendant, 0, 3,
        &db, &cb, 1, &lb, &ir));
    CHECK(ir.page_count == 1 && ir.pages[0].width_points == 300);
    quantapdf_graph_snapshot_drop(descendant);

    unsigned char *bytes = nullptr;
    size_t size = 0;
    OK(quantapdf_qpdf_compose(&ir, &bytes, &size));
    CHECK(bytes != nullptr && size > 0);
    quantapdf_ir_dispose(&ir);

    FILE *out = std::fopen(GEOMETRY_RESULT_PDF, "wb");
    CHECK(out != nullptr && std::fwrite(bytes, 1, size, out) == size);
    CHECK(std::fclose(out) == 0);
    std::free(bytes);
    OK(quantapdf_open(GEOMETRY_RESULT_PDF, nullptr, &document));
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *bitmap = nullptr;
    OK(quantapdf_load_page(document, 0, &page));
    OK(quantapdf_render_page(page, &bitmap));
    int width = 0, height = 0, stride = 0, channels = 0;
    OK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &channels));
    CHECK(width == 300 && height == 120 && channels == 3);
    const unsigned char *pixels = nullptr;
    size_t length = 0;
    OK(quantapdf_bitmap_data(bitmap, &pixels, &length));
    CHECK(length >= (size_t)stride * (size_t)height);
    const unsigned char *inside = pixels + (size_t)100 * (size_t)stride + 15 * 3;
    CHECK(inside[0] < 20 && inside[1] < 20 && inside[2] > 230);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    CHECK(std::remove(GEOMETRY_SOURCE_PDF) == 0);
    CHECK(std::remove(GEOMETRY_RESULT_PDF) == 0);
}

int main()
{
    test_inherited_geometry();
    test_overrides_and_noncanonical();
    test_crop_media_intersection_policy();
    test_selected_version();
    test_snapshot_to_composer();
}
