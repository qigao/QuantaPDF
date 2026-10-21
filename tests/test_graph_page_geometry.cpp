#include "backend/qpdf_graph.h"
#include "backend/qpdf_composer.h"
#include "document/page_composer_ir.h"
#include "graph/graph_page_geometry.h"
#include "graph/graph_edit.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <fpdfview.h>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <vector>
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


/* #360: test-generated real PDF bytes with literal decimal CropBox spellings.
 * This is diagnostic only: the master branch has no private v0 DPI device-grid
 * qualification here. Never interpret a Graph ceil estimate as Native pixels. */
static std::vector<unsigned char> decimal_read_source(const std::string& path)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    CHECK(file != nullptr);
    CHECK(std::fseek(file, 0, SEEK_END) == 0);
    const long length = std::ftell(file);
    CHECK(length > 0 && length < 8192);
    CHECK(std::fseek(file, 0, SEEK_SET) == 0);
    std::vector<unsigned char> bytes((size_t)length);
    CHECK(std::fread(bytes.data(), 1u, bytes.size(), file) == bytes.size());
    CHECK(std::fclose(file) == 0);
    return bytes;
}

/* Write a minimal valid original PDF without normalizing its numeric tokens
 * through an exporter. Record exact object offsets in a real xref table. */
static void decimal_write_source(const std::string& path,
                                 const char *crop_literal)
{
    CHECK(crop_literal && *crop_literal);
    const std::string drawing = "0 0 1 rg 4 4 5 4 re f\n";
    std::string pdf = "%PDF-1.7\n";
    size_t offsets[5] = {};
    const auto put = [&](unsigned id, const std::string& body) {
        CHECK(id >= 1u && id <= 4u && offsets[id] == 0u);
        offsets[id] = pdf.size();
        pdf += std::to_string(id) + " 0 obj\n" + body + "\nendobj\n";
    };
    put(1u, "<< /Type /Catalog /Pages 2 0 R >>");
    put(2u, std::string("<< /Type /Pages /Count 1 /Kids [3 0 R] ") +
        "/MediaBox [0 0 20 20] /CropBox [" + crop_literal + "] >>");
    put(3u, "<< /Type /Page /Parent 2 0 R /UserUnit 7 "
            "/Contents 4 0 R >>");
    put(4u, "<< /Length " + std::to_string(drawing.size()) +
            " >>\nstream\n" + drawing + "endstream");
    const size_t xref_offset = pdf.size();
    pdf += "xref\n0 5\n0000000000 65535 f \n";
    for (unsigned id = 1u; id <= 4u; ++id) {
        char offset[32];
        const int n = std::snprintf(offset, sizeof(offset),
                                    "%010zu 00000 n \n", offsets[id]);
        CHECK(n == 20);
        pdf.append(offset, (size_t)n);
    }
    pdf += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xref_offset) + "\n%%EOF\n";
    CHECK(pdf.size() < 8192u);
    FILE *file = std::fopen(path.c_str(), "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(pdf.data(), 1u, pdf.size(), file) == pdf.size());
    CHECK(std::fclose(file) == 0);
}

/* Independent direct public PDFium reference of the SAME original bytes.
 * Width/height are observed from the original page, not a re-export of IR. */
static std::vector<unsigned char> decimal_pdfium_original(
    const std::string& path, int expected_width, int expected_height)
{
    FPDF_DOCUMENT document = FPDF_LoadDocument(path.c_str(), nullptr);
    CHECK(document != nullptr && FPDF_GetPageCount(document) == 1);
    FPDF_PAGE page = FPDF_LoadPage(document, 0);
    CHECK(page != nullptr);
    const double width = (double)FPDF_GetPageWidthF(page);
    const double height = (double)FPDF_GetPageHeightF(page);
    /* FPDF_GetPageWidthF/HeightF report the unscaled visible CropBox
     * dimensions, not a UserUnit-scaled device pixel grid. The literal
     * original PDF sets /UserUnit 7; apply that independent source value
     * at 72 dpi before comparing mathematically expected pixel ceilings. */
    constexpr double source_user_unit = 7.0;
    CHECK(std::isfinite(width) && std::isfinite(height) &&
          width > 0.0 && height > 0.0);
    const double device_width = width * source_user_unit;
    const double device_height = height * source_user_unit;
    std::fprintf(stderr,
        "[issue-360] original PDFium page=%s crop-points=(%.17g,%.17g) "
        "source-UserUnit=7 effective-72dpi=(%.17g,%.17g) "
        "ceil=%d,%d expected=%d,%d\n",
        path.c_str(), width, height, device_width, device_height,
        (int)std::ceil(device_width), (int)std::ceil(device_height),
        expected_width, expected_height);
    CHECK(std::isfinite(device_width) && std::isfinite(device_height) &&
          (int)std::ceil(device_width) == expected_width &&
          (int)std::ceil(device_height) == expected_height);
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(
        expected_width, expected_height, FPDFBitmap_BGR, nullptr, 0);
    CHECK(bitmap != nullptr);
    FPDFBitmap_FillRect(bitmap, 0, 0,
                        expected_width, expected_height, 0xffffffffu);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0,
                          expected_width, expected_height, 0, FPDF_ANNOT);
    const int stride = FPDFBitmap_GetStride(bitmap);
    const unsigned char *source = (const unsigned char*)FPDFBitmap_GetBuffer(bitmap);
    CHECK(source != nullptr && stride >= expected_width * 3);
    std::vector<unsigned char> rgb(
        (size_t)expected_width * (size_t)expected_height * 3u);
    bool has_ink = false;
    for (int y = 0; y < expected_height; ++y) {
        unsigned char *dst = rgb.data() + (size_t)y * (size_t)expected_width * 3u;
        const unsigned char *src = source + (size_t)y * (size_t)stride;
        std::memcpy(dst, src, (size_t)expected_width * 3u);
        for (int x = 0; x < expected_width * 3; ++x)
            has_ink |= dst[x] < 240u;
    }
    CHECK(has_ink);
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(document);
    return rgb;
}

static uint64_t decimal_double_bits(double value)
{
    uint64_t bits = 0u;
    static_assert(sizeof(bits) == sizeof(value), "requires binary64");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int test_source_decimal_grid_diagnostic(const char *prefix,
                                               const char *report_path)
{
    CHECK(prefix && *prefix && report_path && *report_path);
    struct sample {
        const char *name, *crop;
        int expected_width, expected_height;
        double exact_width_points, exact_height_points;
    };
    const sample cases[] = {
        {"base", "2.375 3.375 12.375 11.375", 70, 56, 70.0, 56.0},
        {"trailing-zero", "2.3750 3.3750 12.3750 11.3750",
         70, 56, 70.0, 56.0},
        {"above-integer", "2.375 3.375 12.375125 11.375125",
         71, 57, 70.000875, 56.000875},
        {"below-integer", "2.375 3.375 12.374875 11.374875",
         70, 56, 69.999125, 55.999125}
    };
    struct observed {
        quantapdf_graph_page_geometry source{};
        quantapdf_graph_page_display display{};
        std::vector<unsigned char> reference;
    };
    observed rows[4];
    struct pdfium_guard {
        pdfium_guard() { FPDF_InitLibrary(); }
        ~pdfium_guard() { FPDF_DestroyLibrary(); }
    } pdfium;
    (void)pdfium;
    const std::string staged = std::string(report_path) + ".tmp";
    errno = 0;
    if (std::remove(report_path) != 0) CHECK(errno == ENOENT);
    errno = 0;
    if (std::remove(staged.c_str()) != 0) CHECK(errno == ENOENT);
    size_t graph_ceil_mismatches = 0u;
    for (size_t i = 0u; i < 4u; ++i) {
        const sample& item = cases[i];
        const std::string path = std::string(prefix) + "-" + item.name + ".pdf";
        CHECK(path != report_path && path != staged);
        errno = 0;
        if (std::remove(path.c_str()) != 0) CHECK(errno == ENOENT);
        decimal_write_source(path, item.crop);
        const auto immutable = decimal_read_source(path);
        const std::string raw(immutable.begin(), immutable.end());
        CHECK(raw.find(item.crop) != std::string::npos);
        const auto first_pdfium = decimal_pdfium_original(
            path, item.expected_width, item.expected_height);
        rows[i].reference = decimal_pdfium_original(
            path, item.expected_width, item.expected_height);
        CHECK(rows[i].reference == first_pdfium);

        quantapdf_document *document = nullptr;
        quantapdf_graph_snapshot *snapshot = nullptr;
        const auto budget = graph_budget();
        OK(quantapdf_open(path.c_str(), nullptr, &document));
        OK(quantapdf_document_capture_graph(document, &budget, &snapshot));
        quantapdf_close(document);
        CHECK(snapshot != nullptr && snapshot->graph != nullptr);
        OK(quantapdf_graph_query_page_geometry(snapshot->graph, 0u, 2u,
                                               &rows[i].source));
        OK(quantapdf_graph_page_display_map(
            &rows[i].source, &rows[i].display));
        quantapdf_graph_snapshot_drop(snapshot);
        const auto& g = rows[i].source;
        const auto& d = rows[i].display;
        CHECK(g.user_unit == 7.0 && g.rotation == 0 &&
              g.crop_box[0] >= 0.0 && g.crop_box[2] <= g.media_box[2] &&
              g.crop_box[1] >= 0.0 && g.crop_box[3] <= g.media_box[3]);
        CHECK(std::isfinite(d.width_points) &&
              std::isfinite(d.height_points) &&
              std::abs(d.width_points - item.exact_width_points) < 0.00001 &&
              std::abs(d.height_points - item.exact_height_points) < 0.00001);
        const int estimated_width = (int)std::ceil(d.width_points);
        const int estimated_height = (int)std::ceil(d.height_points);
        /* Exact integers may ceil to +1 through the existing decimal
         * decoder; flag that as an observation, NOT a passing grid oracle.
         * Genuinely above/below integer cases must remain distinguished. */
        if (i >= 2u)
            CHECK(estimated_width == item.expected_width &&
                  estimated_height == item.expected_height);
        else
            CHECK((estimated_width == 70 || estimated_width == 71) &&
                  (estimated_height == 56 || estimated_height == 57));
        graph_ceil_mismatches +=
            (size_t)(estimated_width != item.expected_width ||
                     estimated_height != item.expected_height);
        std::fprintf(stderr,
            "[issue-360] %s parsed=(%.17g,%.17g)-(%.17g,%.17g) "
            "display=(%.17g,%.17g) source-PDFium=%dx%d graph-ceil=%dx%d\n",
            item.name, g.crop_box[0], g.crop_box[1],
            g.crop_box[2], g.crop_box[3],
            d.width_points, d.height_points, item.expected_width,
            item.expected_height, estimated_width, estimated_height);
        CHECK(decimal_read_source(path) == immutable);
        CHECK(std::remove(path.c_str()) == 0);
    }

    for (size_t j = 0u; j < 4u; ++j) {
        CHECK(decimal_double_bits(rows[0].source.crop_box[j]) ==
              decimal_double_bits(rows[1].source.crop_box[j]));
    }
    CHECK(decimal_double_bits(rows[0].display.width_points) ==
          decimal_double_bits(rows[1].display.width_points) &&
          decimal_double_bits(rows[0].display.height_points) ==
          decimal_double_bits(rows[1].display.height_points) &&
          rows[0].reference == rows[1].reference);

    FILE *report = std::fopen(staged.c_str(), "wb");
    CHECK(report != nullptr);
    std::fprintf(report,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-360-source-decimal-diagnostic\",\n"
        "  \"fixture_kind\": \"test-generated-original-pdf\",\n"
        "  \"reference_oracle\": \"direct-public-pdfium-c-api\",\n"
        "  \"case_count\": 4,\n"
        "  \"expected_integer_grid\": [70, 56],\n"
        "  \"equivalent_spellings_bytewise_double_equal\": true,\n"
        "  \"equivalent_spellings_pdfium_rgb_equal\": true,\n"
        "  \"graph_estimated_ceil_mismatch_count\": %zu,\n"
        "  \"master_native_device_grid_qualified\": false,\n"
        "  \"production_decimal_decoder_changed\": false,\n"
        "  \"result\": \"diagnostic-only\"\n"
        "}\n", graph_ceil_mismatches);
    CHECK(std::ferror(report) == 0 && std::fclose(report) == 0);
    CHECK(std::rename(staged.c_str(), report_path) == 0);
    std::fprintf(stderr,
        "[issue-360] source decimal diagnostic complete: graph ceil "
        "mismatches=%zu/4; no production grid or raster change\n",
        graph_ceil_mismatches);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--decimal-grid-diagnostic") == 0)
        return test_source_decimal_grid_diagnostic(argv[2], argv[3]);
    CHECK(argc == 1);
    test_inherited_geometry();
    test_overrides_and_noncanonical();
    test_crop_media_intersection_policy();
    test_selected_version();
    test_snapshot_to_composer();
}
