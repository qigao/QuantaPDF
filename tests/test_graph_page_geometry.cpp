#include "backend/qpdf_graph.h"
#include "backend/qpdf_composer.h"
#include "document/page_composer_ir.h"
#include "graph/graph_page_geometry.h"
#include "graph/graph_edit.h"
#include "decimal_rational_probe.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <limits>
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
                                 const char *crop_literal,
                                 bool crop_on_leaf = false,
                                 int rotation = -1,
                                 bool rotation_on_leaf = false,
                                 const char *unit_ascii = "7")
{
    CHECK(crop_literal && *crop_literal);
    CHECK(rotation == -1 || rotation == 0 || rotation == 90 ||
          rotation == 180 || rotation == 270);
    const std::string drawing = "0 0 1 rg 4 4 5 4 re f\n";
    std::string pdf = "%PDF-1.7\n";
    size_t offsets[5] = {};
    const auto put = [&](unsigned id, const std::string& body) {
        CHECK(id >= 1u && id <= 4u && offsets[id] == 0u);
        offsets[id] = pdf.size();
        pdf += std::to_string(id) + " 0 obj\n" + body + "\nendobj\n";
    };
    put(1u, "<< /Type /Catalog /Pages 2 0 R >>");
    const std::string crop = std::string(" /CropBox [") + crop_literal + "]";
    const std::string rotate = rotation < 0 ? "" :
        " /Rotate " + std::to_string(rotation);
    put(2u, std::string("<< /Type /Pages /Count 1 /Kids [3 0 R] ") +
        "/MediaBox [0 0 20 20]" +
        (crop_on_leaf ? "" : crop) +
        (rotation_on_leaf ? "" : rotate) + " >>");
    CHECK(unit_ascii && *unit_ascii);
    put(3u, std::string("<< /Type /Page /Parent 2 0 R /UserUnit ") +
        unit_ascii + (crop_on_leaf ? crop : "") +
        (rotation_on_leaf ? rotate : "") +
        " /Contents 4 0 R >>");
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
    const std::string& path, int expected_width, int expected_height,
    double *out_crop_width_points, double *out_crop_height_points,
    unsigned source_dpi = 72u, int source_rotation = 0,
    double source_user_unit = 7.0)
{
    CHECK(out_crop_width_points && out_crop_height_points);
    *out_crop_width_points = 0.0;
    *out_crop_height_points = 0.0;
    FPDF_DOCUMENT document = FPDF_LoadDocument(path.c_str(), nullptr);
    CHECK(document != nullptr && FPDF_GetPageCount(document) == 1);
    FPDF_PAGE page = FPDF_LoadPage(document, 0);
    CHECK(page != nullptr && source_dpi >= 72u && source_dpi <= 144u &&
          (source_rotation == 0 || source_rotation == 90 ||
           source_rotation == 180 || source_rotation == 270));
    /* The PDFium page rotation must follow the actual source /Rotate,
     * including inheritance from /Pages. */
    CHECK(FPDFPage_GetRotation(page) == source_rotation / 90);
    const double width = (double)FPDF_GetPageWidthF(page);
    const double height = (double)FPDF_GetPageHeightF(page);
    /* FPDF_GetPageWidthF/HeightF report the unscaled visible CropBox
     * dimensions, not a UserUnit-scaled device pixel grid. The literal
     * original PDF sets /UserUnit 7; apply that independent source value
     * at 72 dpi before comparing mathematically expected pixel ceilings. */
    CHECK(std::isfinite(source_user_unit) && source_user_unit > 0.0 &&
          std::isfinite(width) && std::isfinite(height) &&
          width > 0.0 && height > 0.0);
    const double device_width =
        (width * source_user_unit * (double)source_dpi) / 72.0;
    const double device_height =
        (height * source_user_unit * (double)source_dpi) / 72.0;
    std::fprintf(stderr,
        "[issue-360] original PDFium page=%s crop-points=(%.17g,%.17g) "
        "source-UserUnit=%.17g dpi=%u effective-grid=(%.17g,%.17g) "
        "ceil=%d,%d expected=%d,%d\n",
        path.c_str(), width, height, source_user_unit, source_dpi,
        device_width, device_height,
        (int)std::ceil(device_width), (int)std::ceil(device_height),
        expected_width, expected_height);
    CHECK(std::isfinite(device_width) && std::isfinite(device_height) &&
          (int)std::ceil(device_width) == expected_width &&
          (int)std::ceil(device_height) == expected_height);
    *out_crop_width_points = width;
    *out_crop_height_points = height;
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

/* #360 alternative NUMERICAL OBSERVATION only: parse a bounded source
 * decimal as an integer significand and base-10 scale, then divide ONCE.
 * No strtod/locale, exponent syntax, arbitrary precision, or production use.
 *
 * The test-only admission contract deliberately rejects unsafe large integers
 * (above 2^53-1), >12 fractional digits, malformed signs/dots/exponents and
 * >20 bytes per token. On EVERY rejection the output is reset to zero.
 * Do not interpret this as an approved production/PDF-wide numeric grammar. */
static bool decimal_source_significand_scale_try(const char *begin,
                                                const char *end,
                                                double *out)
{
    if (out) *out = 0.0;
    if (!begin || !end || !out || end <= begin || end - begin > 20)
        return false;
    bool negative = false, dot = false, saw_digit = false;
    if (*begin == '-' || *begin == '+') {
        negative = *begin == '-';
        ++begin;
    }
    uint64_t significand = 0u, denominator = 1u;
    for (const char *p = begin; p != end; ++p) {
        if (*p == '.' && !dot) {
            dot = true;
            continue;
        }
        if (*p < '0' || *p > '9') return false;
        const unsigned digit = (unsigned)(*p - '0');
        if (significand > (UINT64_MAX - digit) / 10u)
            return false;
        significand = significand * 10u + digit;
        if (dot) {
            if (denominator > UINT64_C(1000000000000) / 10u)
                return false;
            denominator *= 10u;
        }
        saw_digit = true;
    }
    if (!saw_digit || significand > UINT64_C(9007199254740991))
        return false; /* No silent binary64 rounding of large integers. */
    const double value = (double)significand / (double)denominator;
    if (!std::isfinite(value)) return false;
    *out = negative ? -value : value;
    return true;
}

/* Transactional parser for one bounded source-CropBox numeric tuple.
 * On failure it exposes no accepted partial coordinate. */
static bool decimal_source_crop_try(const char *literal, double out[4])
{
    if (out) for (size_t i = 0u; i < 4u; ++i) out[i] = 0.0;
    if (!literal || !out) return false;
    /* Avoid an unbounded strlen() walk on malformed/oversized input. */
    size_t bounded_length = 0u;
    while (bounded_length <= 96u && literal[bounded_length] != '\0')
        ++bounded_length;
    if (bounded_length > 96u) return false;
    double temporary[4] = {};
    const char *p = literal;
    for (unsigned i = 0u; i < 4u; ++i) {
        while (*p == ' ') ++p;
        const char *start = p;
        while (*p && *p != ' ') ++p;
        if (!decimal_source_significand_scale_try(
                start, p, &temporary[i]))
            return false;
    }
    while (*p == ' ') ++p;
    if (*p != '\0' || !(temporary[2] > temporary[0]) ||
        !(temporary[3] > temporary[1]))
        return false;
    std::memcpy(out, temporary, sizeof(temporary));
    return true;
}

static void decimal_source_crop_candidate(const char *literal,
                                          double out[4])
{
    CHECK(decimal_source_crop_try(literal, out));
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
        double pdfium_crop_width_points = 0.0;
        double pdfium_crop_height_points = 0.0;
        double candidate_crop[4] = {};
        double candidate_display_width_points = 0.0;
        double candidate_display_height_points = 0.0;
        int candidate_ceil_width = 0;
        int candidate_ceil_height = 0;
        int graph_ceil_width = 0;
        int graph_ceil_height = 0;
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
    size_t candidate_ceil_mismatches = 0u;
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
        double first_pdfium_width = 0.0, first_pdfium_height = 0.0;
        const auto first_pdfium = decimal_pdfium_original(
            path, item.expected_width, item.expected_height,
            &first_pdfium_width, &first_pdfium_height);
        rows[i].reference = decimal_pdfium_original(
            path, item.expected_width, item.expected_height,
            &rows[i].pdfium_crop_width_points,
            &rows[i].pdfium_crop_height_points);
        CHECK(rows[i].reference == first_pdfium &&
              decimal_double_bits(first_pdfium_width) ==
                  decimal_double_bits(rows[i].pdfium_crop_width_points) &&
              decimal_double_bits(first_pdfium_height) ==
                  decimal_double_bits(rows[i].pdfium_crop_height_points));

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
        rows[i].graph_ceil_width = estimated_width;
        rows[i].graph_ceil_height = estimated_height;

        /* Derive a separate candidate from the ORIGINAL literal tokens,
         * not the Graph-parsed binary64 page edges. UserUnit=7 is encoded
         * in the authored source PDF. This does not round the grid. */
        decimal_source_crop_candidate(item.crop, rows[i].candidate_crop);
        rows[i].candidate_display_width_points =
            (rows[i].candidate_crop[2] - rows[i].candidate_crop[0]) * 7.0;
        rows[i].candidate_display_height_points =
            (rows[i].candidate_crop[3] - rows[i].candidate_crop[1]) * 7.0;
        rows[i].candidate_ceil_width =
            (int)std::ceil(rows[i].candidate_display_width_points);
        rows[i].candidate_ceil_height =
            (int)std::ceil(rows[i].candidate_display_height_points);
        CHECK(std::isfinite(rows[i].candidate_display_width_points) &&
              std::isfinite(rows[i].candidate_display_height_points) &&
              std::abs(rows[i].candidate_display_width_points -
                       item.exact_width_points) < 0.00001 &&
              std::abs(rows[i].candidate_display_height_points -
                       item.exact_height_points) < 0.00001 &&
              rows[i].candidate_ceil_width == item.expected_width &&
              rows[i].candidate_ceil_height == item.expected_height);
        candidate_ceil_mismatches +=
            (size_t)(rows[i].candidate_ceil_width != item.expected_width ||
                     rows[i].candidate_ceil_height != item.expected_height);
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
          decimal_double_bits(rows[0].pdfium_crop_width_points) ==
          decimal_double_bits(rows[1].pdfium_crop_width_points) &&
          decimal_double_bits(rows[0].pdfium_crop_height_points) ==
          decimal_double_bits(rows[1].pdfium_crop_height_points) &&
          rows[0].graph_ceil_width == rows[1].graph_ceil_width &&
          rows[0].graph_ceil_height == rows[1].graph_ceil_height &&
          rows[0].candidate_ceil_width == rows[1].candidate_ceil_width &&
          rows[0].candidate_ceil_height == rows[1].candidate_ceil_height &&
          decimal_double_bits(rows[0].candidate_display_width_points) ==
              decimal_double_bits(rows[1].candidate_display_width_points) &&
          decimal_double_bits(rows[0].candidate_display_height_points) ==
              decimal_double_bits(rows[1].candidate_display_height_points) &&
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
        "  \"bounded_significand_scale_mismatch_count\": %zu,\n"
        "  \"cases\": [\n",
        graph_ceil_mismatches, candidate_ceil_mismatches);
    /* Persist the actual IEEE-754 observations, not a synthetic expected
     * value. This keeps CI artifacts sufficient for cross-platform review. */
    for (size_t i = 0u; i < 4u; ++i) {
        const auto& g = rows[i].source;
        const auto& d = rows[i].display;
        std::fprintf(report,
            "    {\"id\":\"%s\",\"source_crop\":\"%s\","
            "\"graph_crop_points\":[%.17g,%.17g,%.17g,%.17g],"
            "\"graph_crop_bits\":[\"%016llx\",\"%016llx\","
            "\"%016llx\",\"%016llx\"],"
            "\"graph_display_points\":[%.17g,%.17g],"
            "\"graph_display_bits\":[\"%016llx\",\"%016llx\"],"
            "\"candidate_display_points\":[%.17g,%.17g],"
            "\"candidate_display_bits\":[\"%016llx\",\"%016llx\"],"
            "\"pdfium_crop_page_points\":[%.17g,%.17g],"
            "\"source_user_unit\":7,"
            "\"reference_grid_72dpi\":[%d,%d],"
            "\"graph_ceil_estimate_72dpi\":[%d,%d],"
            "\"candidate_ceil_estimate_72dpi\":[%d,%d],"
            "\"graph_grid_mismatch\":%s}%s\n",
            cases[i].name, cases[i].crop,
            g.crop_box[0], g.crop_box[1], g.crop_box[2], g.crop_box[3],
            (unsigned long long)decimal_double_bits(g.crop_box[0]),
            (unsigned long long)decimal_double_bits(g.crop_box[1]),
            (unsigned long long)decimal_double_bits(g.crop_box[2]),
            (unsigned long long)decimal_double_bits(g.crop_box[3]),
            d.width_points, d.height_points,
            (unsigned long long)decimal_double_bits(d.width_points),
            (unsigned long long)decimal_double_bits(d.height_points),
            rows[i].candidate_display_width_points,
            rows[i].candidate_display_height_points,
            (unsigned long long)decimal_double_bits(
                rows[i].candidate_display_width_points),
            (unsigned long long)decimal_double_bits(
                rows[i].candidate_display_height_points),
            rows[i].pdfium_crop_width_points,
            rows[i].pdfium_crop_height_points,
            cases[i].expected_width, cases[i].expected_height,
            rows[i].graph_ceil_width, rows[i].graph_ceil_height,
            rows[i].candidate_ceil_width, rows[i].candidate_ceil_height,
            (rows[i].graph_ceil_width != cases[i].expected_width ||
             rows[i].graph_ceil_height != cases[i].expected_height)
                ? "true" : "false",
            i + 1u == 4u ? "" : ",");
    }
    std::fprintf(report,
        "  ],\n"
        "  \"master_native_device_grid_qualified\": false,\n"
        "  \"production_decimal_decoder_changed\": false,\n"
        "  \"result\": \"diagnostic-only\"\n"
        "}\n");
    CHECK(std::ferror(report) == 0 && std::fclose(report) == 0);
    CHECK(std::rename(staged.c_str(), report_path) == 0);
    std::fprintf(stderr,
        "[issue-360] source decimal diagnostic complete: graph ceil "
        "mismatches=%zu/4 candidate_mismatches=%zu/4; "
        "no production grid or raster change\n",
        graph_ceil_mismatches, candidate_ceil_mismatches);
    return EXIT_SUCCESS;
}


/* #360 expanded REAL-SOURCE diagnosis: 7 exact binary fractions × root/leaf
 * CropBox × root/leaf Rotate × 4 quarter-turns × 72/96/144 DPI.
 * These are original, test-generated PDF files, not Graph-exported PDFs.
 * The independent grid is derived with bounded INTEGER arithmetic from
 * authored source geometry (10×8 points), /UserUnit=7 and the requested DPI.
 * This is not a production/native DPI grid acceptance lane. */
static int decimal_matrix_grid(unsigned points, unsigned dpi)
{
    CHECK((points == 8u || points == 10u) &&
          (dpi == 72u || dpi == 96u || dpi == 144u));
    const unsigned numerator = points * 7u * dpi;
    CHECK(numerator < 100000u);
    return (int)((numerator + 71u) / 72u);
}

static uint64_t decimal_matrix_reference_hash(
    const std::vector<unsigned char>& pixels)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char byte : pixels) {
        hash ^= (uint64_t)byte;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int test_source_decimal_grid_matrix(const char *prefix,
                                           const char *report_path)
{
    CHECK(prefix && *prefix && report_path && *report_path &&
          std::strcmp(prefix, report_path) != 0);
    const char *const fractions[] = {
        "125", "25", "375", "5", "625", "75", "875"
    };
    const unsigned dpis[] = {72u, 96u, 144u};
    constexpr size_t FRACTIONS = 7u, ROTATIONS = 4u, DPIS = 3u;
    constexpr size_t CASES = FRACTIONS * 2u * 2u * ROTATIONS;
    constexpr size_t FRAMES = CASES * DPIS;
    std::vector<unsigned char> canonical[FRACTIONS * ROTATIONS * DPIS];
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
    FILE *out = std::fopen(staged.c_str(), "wb");
    CHECK(out != nullptr);
    std::fprintf(out,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-360-source-decimal-grid-matrix\",\n"
        "  \"fixture_kind\": \"test-generated-original-pdf\",\n"
        "  \"reference_oracle\": \"direct-public-pdfium-c-api\",\n"
        "  \"fraction_count\": 7,\n"
        "  \"crop_ownership_options\": 2,\n"
        "  \"rotation_ownership_options\": 2,\n"
        "  \"rotation_options\": 4,\n"
        "  \"dpi_values\": [72,96,144],\n"
        "  \"expected_generated_sources\": %zu,\n"
        "  \"expected_reference_frames\": %zu,\n"
        "  \"cases\": [\n", CASES, FRAMES);

    size_t sources = 0u, frames = 0u, graph_mismatches = 0u;
    size_t candidate_mismatches = 0u, rational_mismatches = 0u;
    for (size_t fraction = 0u; fraction < FRACTIONS; ++fraction) {
        const std::string digits(fractions[fraction]);
        /* Every upper/lower endpoint difference is EXACTLY 10×8 in source
         * rational decimal notation; the fraction family itself varies. */
        const std::string crop =
            "2." + digits + " 3." + digits + " 12." + digits +
            " 11." + digits;
        double alternative[4] = {};
        decimal_source_crop_candidate(crop.c_str(), alternative);
        CHECK(alternative[2] - alternative[0] == 10.0 &&
              alternative[3] - alternative[1] == 8.0);

        for (unsigned crop_leaf = 0u; crop_leaf < 2u; ++crop_leaf)
        for (unsigned rotate_leaf = 0u; rotate_leaf < 2u; ++rotate_leaf)
        for (unsigned rotation_index = 0u; rotation_index < 4u;
             ++rotation_index) {
            const int rotation = (int)(rotation_index * 90u);
            const bool swapped = rotation == 90 || rotation == 270;
            const unsigned source_width = swapped ? 8u : 10u;
            const unsigned source_height = swapped ? 10u : 8u;
            const std::string path = std::string(prefix) +
                "-f" + std::to_string(fraction) +
                "-c" + std::to_string(crop_leaf) +
                "-r" + std::to_string(rotate_leaf) +
                "-a" + std::to_string(rotation_index) + ".pdf";
            CHECK(path != report_path && path != staged);
            errno = 0;
            if (std::remove(path.c_str()) != 0) CHECK(errno == ENOENT);
            decimal_write_source(path, crop.c_str(),
                                 crop_leaf != 0u, rotation,
                                 rotate_leaf != 0u);
            const auto original = decimal_read_source(path);
            const std::string original_text(original.begin(), original.end());
            CHECK(original_text.find(crop) != std::string::npos &&
                  original_text.find("/UserUnit 7") != std::string::npos &&
                  original_text.find("/Rotate " + std::to_string(rotation)) !=
                      std::string::npos);

            quantapdf_document *document = nullptr;
            quantapdf_graph_snapshot *snapshot = nullptr;
            const auto budget = graph_budget();
            OK(quantapdf_open(path.c_str(), nullptr, &document));
            OK(quantapdf_document_capture_graph(document, &budget, &snapshot));
            quantapdf_close(document);
            CHECK(snapshot && snapshot->graph);
            quantapdf_graph_page_geometry graph_geometry{};
            quantapdf_graph_page_display graph_display{};
            OK(quantapdf_graph_query_page_geometry(
                snapshot->graph, 0u, 2u, &graph_geometry));
            OK(quantapdf_graph_page_display_map(
                &graph_geometry, &graph_display));
            quantapdf_graph_snapshot_drop(snapshot);

            CHECK(graph_geometry.rotation == rotation &&
                  graph_geometry.user_unit == 7.0 &&
                  graph_geometry.media_box[0] == 0.0 &&
                  graph_geometry.media_box[1] == 0.0 &&
                  graph_geometry.media_box[2] == 20.0 &&
                  graph_geometry.media_box[3] == 20.0);
            for (size_t edge = 0u; edge < 4u; ++edge)
                CHECK(std::abs(graph_geometry.crop_box[edge] -
                               alternative[edge]) < 0.000000000001);
            const double exact_display_w = (double)source_width * 7.0;
            const double exact_display_h = (double)source_height * 7.0;
            CHECK(std::abs(graph_display.width_points - exact_display_w) <
                      0.0000000001 &&
                  std::abs(graph_display.height_points - exact_display_h) <
                      0.0000000001);
            const double alternative_w =
                (swapped ? alternative[3] - alternative[1] :
                           alternative[2] - alternative[0]) * 7.0;
            const double alternative_h =
                (swapped ? alternative[2] - alternative[0] :
                           alternative[3] - alternative[1]) * 7.0;
            CHECK(alternative_w == exact_display_w &&
                  alternative_h == exact_display_h);

            if (sources++ != 0u) std::fprintf(out, ",\n");
            std::fprintf(out,
                "    {\"fraction\":\".%s\","
                "\"crop_owner\":\"%s\",\"rotation_owner\":\"%s\","
                "\"rotation_degrees\":%d,\"source_crop\":\"%s\","
                "\"graph_crop_points\":[%.17g,%.17g,%.17g,%.17g],"
                "\"graph_crop_bits\":[\"%016llx\",\"%016llx\","
                "\"%016llx\",\"%016llx\"],"
                "\"graph_display_points\":[%.17g,%.17g],"
                "\"graph_display_bits\":[\"%016llx\",\"%016llx\"],"
                "\"candidate_display_points\":[%.17g,%.17g],"
                "\"dpi_cases\":[",
                fractions[fraction], crop_leaf ? "leaf" : "root",
                rotate_leaf ? "leaf" : "root", rotation, crop.c_str(),
                graph_geometry.crop_box[0], graph_geometry.crop_box[1],
                graph_geometry.crop_box[2], graph_geometry.crop_box[3],
                (unsigned long long)decimal_double_bits(
                    graph_geometry.crop_box[0]),
                (unsigned long long)decimal_double_bits(
                    graph_geometry.crop_box[1]),
                (unsigned long long)decimal_double_bits(
                    graph_geometry.crop_box[2]),
                (unsigned long long)decimal_double_bits(
                    graph_geometry.crop_box[3]),
                graph_display.width_points, graph_display.height_points,
                (unsigned long long)decimal_double_bits(
                    graph_display.width_points),
                (unsigned long long)decimal_double_bits(
                    graph_display.height_points),
                alternative_w, alternative_h);

            for (size_t d = 0u; d < DPIS; ++d) {
                const unsigned dpi = dpis[d];
                const int expected_width = decimal_matrix_grid(source_width,dpi);
                const int expected_height = decimal_matrix_grid(source_height,dpi);
                double pdfium_w1 = 0.0, pdfium_h1 = 0.0;
                double pdfium_w2 = 0.0, pdfium_h2 = 0.0;
                const auto reference = decimal_pdfium_original(
                    path, expected_width, expected_height,
                    &pdfium_w1, &pdfium_h1, dpi, rotation);
                const auto repeated = decimal_pdfium_original(
                    path, expected_width, expected_height,
                    &pdfium_w2, &pdfium_h2, dpi, rotation);
                CHECK(reference == repeated &&
                      decimal_double_bits(pdfium_w1) ==
                          decimal_double_bits(pdfium_w2) &&
                      decimal_double_bits(pdfium_h1) ==
                          decimal_double_bits(pdfium_h2) &&
                      pdfium_w1 == (double)source_width &&
                      pdfium_h1 == (double)source_height);
                const size_t canonical_index =
                    (fraction * ROTATIONS + rotation_index) * DPIS + d;
                if (canonical[canonical_index].empty())
                    canonical[canonical_index] = reference;
                else
                    CHECK(canonical[canonical_index] == reference);

                const int graph_w = (int)std::ceil(
                    graph_display.width_points * (double)dpi / 72.0);
                const int graph_h = (int)std::ceil(
                    graph_display.height_points * (double)dpi / 72.0);
                const int candidate_w =
                    (int)std::ceil(alternative_w * (double)dpi / 72.0);
                const int candidate_h =
                    (int)std::ceil(alternative_h * (double)dpi / 72.0);
                quantapdf_test_rational_grid exact_grid = {};
                CHECK(quantapdf_test_rational_page_grid(
                    crop.c_str(), rotation, 7u, dpi, 100000u, &exact_grid));
                const bool rational_mismatch =
                    exact_grid.width != (size_t)expected_width ||
                    exact_grid.height != (size_t)expected_height;
                rational_mismatches += (size_t)rational_mismatch;
                CHECK(!rational_mismatch);
                CHECK(std::abs(graph_w - expected_width) <= 1 &&
                      std::abs(graph_h - expected_height) <= 1 &&
                      candidate_w == expected_width &&
                      candidate_h == expected_height);
                const bool graph_mismatch =
                    graph_w != expected_width || graph_h != expected_height;
                const bool candidate_mismatch =
                    candidate_w != expected_width ||
                    candidate_h != expected_height;
                graph_mismatches += (size_t)graph_mismatch;
                candidate_mismatches += (size_t)candidate_mismatch;
                if (d != 0u) std::fprintf(out, ",");
                std::fprintf(out,
                    "{\"dpi\":%u,\"reference_grid\":[%d,%d],"
                    "\"pdfium_crop_page_points\":[%.17g,%.17g],"
                    "\"pdfium_reference_rgb_fnv64\":\"%016llx\","
                    "\"graph_ceil_estimate\":[%d,%d],"
                    "\"candidate_ceil_estimate\":[%d,%d],"
                    "\"exact_rational_ceil_estimate\":[%zu,%zu],"
                    "\"graph_mismatch\":%s}",
                    dpi, expected_width, expected_height,
                    pdfium_w1, pdfium_h1,
                    (unsigned long long)decimal_matrix_reference_hash(reference),
                    graph_w, graph_h, candidate_w, candidate_h,
                    exact_grid.width, exact_grid.height,
                    graph_mismatch ? "true" : "false");
                ++frames;
            }
            std::fprintf(out, "]}");
            CHECK(decimal_read_source(path) == original);
            CHECK(std::remove(path.c_str()) == 0);
        }
    }
    CHECK(sources == CASES && frames == FRAMES &&
          candidate_mismatches == 0u && rational_mismatches == 0u);
    std::fprintf(out,
        "\n  ],\n"
        "  \"generated_source_count\": %zu,\n"
        "  \"reference_frame_count\": %zu,\n"
        "  \"graph_ceil_mismatch_count\": %zu,\n"
        "  \"candidate_ceil_mismatch_count\": %zu,\n"
        "  \"exact_rational_ceil_mismatch_count\": %zu,\n"
        "  \"master_native_device_grid_qualified\": false,\n"
        "  \"production_decimal_decoder_changed\": false,\n"
        "  \"result\": \"diagnostic-only\"\n"
        "}\n", sources, frames, graph_mismatches, candidate_mismatches,
        rational_mismatches);
    CHECK(std::ferror(out) == 0 && std::fclose(out) == 0);
    CHECK(std::rename(staged.c_str(), report_path) == 0);
    std::fprintf(stderr,
        "[issue-360] source fraction matrix: %zu original PDFs, %zu "
        "original-PDFium frames, graph mismatches=%zu, "
        "candidate mismatches=%zu; no native device grid or "
        "production raster admitted\n",
        sources, frames, graph_mismatches, candidate_mismatches);
    return EXIT_SUCCESS;
}


/* #360 next separate admission gate: numeric parsing/bounds only.
 * This is deliberately a TEST-ONLY candidate contract, not a change to
 * Graph decimal_number(), PDFium, the device DPI grid or native raster. */
static bool decimal_test_grid_bounded(double width_points,
                                      double height_points, unsigned dpi,
                                      size_t max_pixels,
                                      size_t *out_width,size_t *out_height)
{
    if (out_width) *out_width = 0u;
    if (out_height) *out_height = 0u;
    if (!out_width || !out_height || out_width == out_height ||
        max_pixels == 0u || max_pixels > 1000000u ||
        (dpi != 72u && dpi != 96u && dpi != 144u) ||
        !std::isfinite(width_points) || !std::isfinite(height_points) ||
        width_points <= 0.0 || height_points <= 0.0 ||
        width_points > (double)max_pixels ||
        height_points > (double)max_pixels)
        return false;
    const double scale = 7.0 * (double)dpi / 72.0;
    const double dw = std::ceil(width_points * scale);
    const double dh = std::ceil(height_points * scale);
    if (!std::isfinite(dw) || !std::isfinite(dh) ||
        dw <= 0.0 || dh <= 0.0 ||
        dw > (double)max_pixels || dh > (double)max_pixels)
        return false;
    const size_t width = (size_t)dw, height = (size_t)dh;
    if (height == 0u || width > max_pixels / height)
        return false;
    *out_width = width;
    *out_height = height;
    return true;
}

static int test_source_decimal_candidate_admission(const char *report_path)
{
    CHECK(report_path && *report_path);
    const std::string staged = std::string(report_path) + ".tmp";
    errno = 0;
    if (std::remove(report_path) != 0) CHECK(errno == ENOENT);
    errno = 0;
    if (std::remove(staged.c_str()) != 0) CHECK(errno == ENOENT);

    const char *valid_tokens[] = {
        "0", "-0", "+0", "-0.0", "0.", ".125", "0.125",
        "2.375", "+2.3750", "-2.375", ".5", "0.625", "0.75",
        "0.875", "1.000000000000", "1.000000000001",
        "0.000000000001", "-0.000000000001",
        "9007199254740991", "-9007199254740991",
        "00000000000000000001"
    };
    size_t valid_count = 0u, invalid_count = 0u;
    for (const char *token : valid_tokens) {
        double output = 999.0;
        CHECK(decimal_source_significand_scale_try(
                  token, token + std::strlen(token), &output) &&
              std::isfinite(output));
        ++valid_count;
    }
    /* Every rejected token must zero the previously nonzero output. */
    const char *invalid_tokens[] = {
        "", ".", "+", "-", "+.", "-.", "--1", "++1", "-+1",
        "1..0", "1.2.3", "1e2", "1E2", "NaN", "nan", "Inf", "inf",
        "1a", "1_0", "0x1", "1/2", "1,2", " 1", "1 ", "1-0",
        "0.0000000000001", "1.0000000000000",
        "9007199254740992", "-9007199254740992",
        "18446744073709551615", "18446744073709551616",
        "99999999999999999999", "123456789012345678901",
        "000000000000000000000", "1e309", "1.2e-3"
    };
    for (const char *token : invalid_tokens) {
        double output = 999.0;
        CHECK(!decimal_source_significand_scale_try(
                   token, token + std::strlen(token), &output) &&
              output == 0.0 && !std::signbit(output));
        ++invalid_count;
    }
    double sentinel = 999.0;
    CHECK(!decimal_source_significand_scale_try(
               nullptr, nullptr, &sentinel) && sentinel == 0.0);
    ++invalid_count;
    CHECK(!decimal_source_significand_scale_try(
               "0", nullptr, &sentinel) && sentinel == 0.0);
    ++invalid_count;
    CHECK(!decimal_source_significand_scale_try(
               "0", "0" + 1, nullptr));
    ++invalid_count;

    auto parse = [](const char *token) {
        double value = 0.0;
        CHECK(decimal_source_significand_scale_try(
                  token, token + std::strlen(token), &value));
        return value;
    };
    CHECK(decimal_double_bits(parse("2.375")) ==
          decimal_double_bits(parse("2.3750")) &&
          decimal_double_bits(parse("2.375")) ==
          decimal_double_bits(parse("+02.375000")) &&
          decimal_double_bits(parse(".125")) ==
          decimal_double_bits(parse("0.125000")) &&
          decimal_double_bits(parse("-0")) ==
          decimal_double_bits(parse("-0.0")) &&
          std::signbit(parse("-0")) && !std::signbit(parse("+0")));

    const char *ascending[] = {
        "-1.000000000001", "-1.000000000000", "-0.999999999999",
        "-0.5", "-0.000000000001", "0", "0.000000000001",
        "0.5", "0.999999999999", "1.000000000000",
        "1.000000000001", "2.374999999999", "2.375",
        "2.375000000001"
    };
    size_t monotonic_steps = 0u;
    double previous = parse(ascending[0]);
    for (size_t i = 1u; i < sizeof(ascending)/sizeof(ascending[0]); ++i) {
        const double current = parse(ascending[i]);
        CHECK(current > previous);
        previous = current;
        ++monotonic_steps;
    }

    /* CropBox parsing must be transactional and reject missing/extra edges,
     * reversed boxes, malformed tokens, and invalid UTF-8-free ASCII input. */
    const char *valid_boxes[] = {
        "2.375 3.375 12.375 11.375",
        "2.3750 3.3750 12.3750 11.3750",
        "  2.375  3.375  12.375  11.375  ",
        "-2.375 -3.375 12.375 11.375"
    };
    size_t accepted_boxes = 0u, rejected_boxes = 0u;
    double reference_box[4] = {};
    CHECK(decimal_source_crop_try(valid_boxes[0], reference_box));
    for (const char *box : valid_boxes) {
        double output[4] = {9.0, 9.0, 9.0, 9.0};
        CHECK(decimal_source_crop_try(box, output) &&
              output[2] > output[0] && output[3] > output[1]);
        if (accepted_boxes == 1u || accepted_boxes == 2u)
            for (size_t k = 0u; k < 4u; ++k)
                CHECK(decimal_double_bits(output[k]) ==
                      decimal_double_bits(reference_box[k]));
        ++accepted_boxes;
    }
    const char *invalid_boxes[] = {
        "", "    ", "2.375 3.375 12.375",
        "2.375 3.375 12.375 11.375 9",
        "2.375 3.375 2.375 11.375",
        "2.375 3.375 12.375 3.375",
        "2.375 3.375 2.0 11.375",
        "2.375 3.375 12.375 2.0",
        "2.375 3.375 NaN 11.375",
        "2.375 3.375 9007199254740992 11.375",
        "2.375 3.375 12.375 11.375e10",
        "2.375 3.375 12.375 11.375/",
        "2.375\t3.375 12.375 11.375",
        "2.375 3.375 12.375 11.375\n"
    };
    for (const char *box : invalid_boxes) {
        double output[4] = {9.0, 9.0, 9.0, 9.0};
        CHECK(!decimal_source_crop_try(box, output));
        for (double value : output) CHECK(value == 0.0);
        ++rejected_boxes;
    }
    std::string overlong_box(97u, '9');
    double output[4] = {9.0, 9.0, 9.0, 9.0};
    CHECK(!decimal_source_crop_try(overlong_box.c_str(), output));
    for (double value : output) CHECK(value == 0.0);
    ++rejected_boxes;
    double null_box[4] = {9.0, 9.0, 9.0, 9.0};
    CHECK(!decimal_source_crop_try(nullptr, null_box));
    for (double value : null_box) CHECK(value == 0.0);
    ++rejected_boxes;
    CHECK(!decimal_source_crop_try(valid_boxes[0], nullptr));
    ++rejected_boxes;

    /* Rationally ordered source widths must not shrink a candidate grid.
     * The under/integer/over sequence explicitly preserves ceil boundaries. */
    const char *upper[] = {
        "12.374875", "12.374999999999", "12.375",
        "12.375000000001", "12.375125"
    };
    const unsigned dpis[] = {72u, 96u, 144u};
    const unsigned expected_widths[3][5] = {
        {70u, 70u, 70u, 71u, 71u},
        {94u, 94u, 94u, 94u, 94u},
        {140u, 140u, 140u, 141u, 141u}
    };
    const unsigned expected_heights[3][5] = {
        {56u, 56u, 56u, 57u, 57u},
        {75u, 75u, 75u, 75u, 75u},
        {112u, 112u, 112u, 113u, 113u}
    };
    size_t grid_steps = 0u, grid_budget_rejections = 0u;
    for (size_t d = 0u; d < 3u; ++d) {
        size_t previous_width = 0u, previous_height = 0u;
        for (size_t k = 0u; k < 5u; ++k) {
            const double upper_bound = parse(upper[k]);
            const double lower_bound = parse("2.375");
            const double upper_h = upper_bound - 1.0;
            const double lower_h = parse("3.375");
            const double width_points = upper_bound - lower_bound;
            const double height_points = upper_h - lower_h;
            size_t width = 0u, height = 0u;
            CHECK(decimal_test_grid_bounded(
                      width_points, height_points, dpis[d], 100000u,
                      &width, &height) &&
                  width == expected_widths[d][k] &&
                  height == expected_heights[d][k] &&
                  width >= previous_width && height >= previous_height);
            previous_width = width;
            previous_height = height;
            ++grid_steps;
        }
    }
    size_t width = 99u, height = 99u;
    CHECK(decimal_test_grid_bounded(
        10.0, 8.0, 72u, 3920u, &width, &height) &&
        width == 70u && height == 56u);
    struct budget_case {
        double w, h; unsigned dpi; size_t capacity;
    };
    const budget_case rejected_grids[] = {
        {10.0,8.0,72u,3919u}, {10.0,8.0,72u,0u},
        {10.0,8.0,72u,1000001u}, {10.0,8.0,120u,100000u},
        {10.0,8.0,0u,100000u},
        {0.0,8.0,72u,100000u}, {-1.0,8.0,72u,100000u},
        {10.0,0.0,72u,100000u},
        {1e300,8.0,72u,100000u},
        {10.0,std::numeric_limits<double>::quiet_NaN(),72u,100000u},
        {std::numeric_limits<double>::infinity(),8.0,72u,100000u},
        {10.0,-std::numeric_limits<double>::infinity(),72u,100000u}
    };
    for (const auto &c : rejected_grids) {
        width = 99u; height = 99u;
        CHECK(!decimal_test_grid_bounded(
                  c.w,c.h,c.dpi,c.capacity,&width,&height) &&
              width == 0u && height == 0u);
        ++grid_budget_rejections;
    }
    CHECK(!decimal_test_grid_bounded(
        10.0,8.0,72u,100000u,nullptr,&height));
    CHECK(height == 0u);

    /* Counterexample beyond binary-exact eighth fractions:
     * Both source spellings express an EXACT rational CropBox width 10,
     * although independently rounded binary64 endpoints 6.1 and 16.1
     * can subtract to 10.000000000000002. This is test-only evidence
     * AGAINST treating significand/scale endpoint conversion as a complete
     * fix for strict device ceil. Original PDFium reads each original PDF;
     * no IR PDF export, native grid, production decoder or epsilon. */
    const char *const nondyadic_crop[2] = {
        "6.1 3.375 16.1 11.375",
        "6.10 3.3750 16.10 11.3750"
    };
    const int rational_width_grid =
        (int)(((161u - 61u) * 7u + 10u - 1u) / 10u);
    const int rational_height_grid =
        (int)(((11375u - 3375u) * 7u + 1000u - 1u) / 1000u);
    CHECK(rational_width_grid == 70 && rational_height_grid == 56);
    std::vector<unsigned char> first_reference;
    uint64_t initial_graph_width_bits = 0u;
    uint64_t initial_candidate_width_bits = 0u;
    double nondyadic_graph_width_points = 0.0;
    double nondyadic_candidate_width_points = 0.0;
    int nondyadic_graph_width = 0, nondyadic_graph_height = 0;
    int nondyadic_candidate_width = 0, nondyadic_candidate_height = 0;
    size_t nondyadic_candidate_overcounts = 0u;
    size_t nondyadic_rational_mismatches = 0u;
    struct nondyadic_pdfium_guard {
        nondyadic_pdfium_guard() { FPDF_InitLibrary(); }
        ~nondyadic_pdfium_guard() { FPDF_DestroyLibrary(); }
    } nondyadic_pdfium;
    (void)nondyadic_pdfium;
    for (size_t i = 0u; i < 2u; ++i) {
        const std::string source_path = std::string(report_path) +
            (i == 0u ? ".nondyadic-short.pdf" : ".nondyadic-zeros.pdf");
        errno = 0;
        if (std::remove(source_path.c_str()) != 0)
            CHECK(errno == ENOENT);
        decimal_write_source(source_path, nondyadic_crop[i]);
        const auto immutable_source = decimal_read_source(source_path);
        const std::string original_text(immutable_source.begin(),
                                        immutable_source.end());
        CHECK(original_text.find(nondyadic_crop[i]) != std::string::npos &&
              original_text.find("/UserUnit 7") != std::string::npos);

        double pdfium_width = 0.0, pdfium_height = 0.0;
        double repeated_width = 0.0, repeated_height = 0.0;
        const auto reference = decimal_pdfium_original(
            source_path, rational_width_grid, rational_height_grid,
            &pdfium_width, &pdfium_height);
        const auto repeated = decimal_pdfium_original(
            source_path, rational_width_grid, rational_height_grid,
            &repeated_width, &repeated_height);
        CHECK(reference == repeated &&
              pdfium_width == 10.0 && pdfium_height == 8.0 &&
              decimal_double_bits(pdfium_width) ==
                  decimal_double_bits(repeated_width) &&
              decimal_double_bits(pdfium_height) ==
                  decimal_double_bits(repeated_height));
        if (i == 0u) first_reference = reference;
        else CHECK(first_reference == reference);

        quantapdf_document *document = nullptr;
        quantapdf_graph_snapshot *snapshot = nullptr;
        const auto budget = graph_budget();
        OK(quantapdf_open(source_path.c_str(), nullptr, &document));
        OK(quantapdf_document_capture_graph(document, &budget, &snapshot));
        quantapdf_close(document);
        CHECK(snapshot && snapshot->graph);
        quantapdf_graph_page_geometry geometry{};
        quantapdf_graph_page_display display{};
        OK(quantapdf_graph_query_page_geometry(
            snapshot->graph, 0u, 2u, &geometry));
        OK(quantapdf_graph_page_display_map(&geometry, &display));
        quantapdf_graph_snapshot_drop(snapshot);
        CHECK(geometry.rotation == 0 && geometry.user_unit == 7.0 &&
              std::abs(display.width_points - 70.0) < 1e-10 &&
              std::abs(display.height_points - 56.0) < 1e-10);

        double crop[4] = {};
        decimal_source_crop_candidate(nondyadic_crop[i], crop);
        const double candidate_width_points = (crop[2] - crop[0]) * 7.0;
        const double candidate_height_points = (crop[3] - crop[1]) * 7.0;
        const int graph_width = (int)std::ceil(display.width_points);
        const int graph_height = (int)std::ceil(display.height_points);
        const int candidate_width = (int)std::ceil(candidate_width_points);
        const int candidate_height = (int)std::ceil(candidate_height_points);
        quantapdf_test_rational_grid exact_grid = {};
        CHECK(quantapdf_test_rational_page_grid(
            nondyadic_crop[i], 0, 7u, 72u, 100000u, &exact_grid));
        const bool rational_mismatch =
            exact_grid.width != (size_t)rational_width_grid ||
            exact_grid.height != (size_t)rational_height_grid;
        nondyadic_rational_mismatches += (size_t)rational_mismatch;
        CHECK(!rational_mismatch);
        CHECK(candidate_width == 71 && candidate_height == 56 &&
              (graph_width == 70 || graph_width == 71) &&
              (graph_height == 56 || graph_height == 57));
        nondyadic_candidate_overcounts +=
            (size_t)(candidate_width != rational_width_grid);
        if (i == 0u) {
            initial_graph_width_bits =
                decimal_double_bits(display.width_points);
            initial_candidate_width_bits =
                decimal_double_bits(candidate_width_points);
            nondyadic_graph_width_points = display.width_points;
            nondyadic_candidate_width_points = candidate_width_points;
            nondyadic_graph_width = graph_width;
            nondyadic_graph_height = graph_height;
            nondyadic_candidate_width = candidate_width;
            nondyadic_candidate_height = candidate_height;
        } else {
            CHECK(initial_graph_width_bits ==
                      decimal_double_bits(display.width_points) &&
                  initial_candidate_width_bits ==
                      decimal_double_bits(candidate_width_points) &&
                  nondyadic_graph_width == graph_width &&
                  nondyadic_graph_height == graph_height &&
                  nondyadic_candidate_width == candidate_width &&
                  nondyadic_candidate_height == candidate_height);
        }
        CHECK(decimal_read_source(source_path) == immutable_source);
        CHECK(std::remove(source_path.c_str()) == 0);
    }
    CHECK(nondyadic_candidate_overcounts == 2u &&
          nondyadic_rational_mismatches == 0u);

    FILE *report = std::fopen(staged.c_str(), "wb");
    CHECK(report != nullptr);
    std::fprintf(report,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-360-decimal-candidate-admission\",\n"
        "  \"scope\": \"test-only-bounded-decimal-candidate\",\n"
        "  \"token_max_length\": 20,\n"
        "  \"max_fractional_digits\": 12,\n"
        "  \"max_significand\": 9007199254740991,\n"
        "  \"accepted_token_cases\": %zu,\n"
        "  \"rejected_token_cases\": %zu,\n"
        "  \"accepted_crop_cases\": %zu,\n"
        "  \"rejected_crop_cases\": %zu,\n"
        "  \"monotonic_decimal_transitions\": %zu,\n"
        "  \"monotonic_grid_cases\": %zu,\n"
        "  \"rejected_grid_budget_cases\": %zu,\n"
        "  \"nondyadic_source_case_count\": 2,\n"
        "  \"nondyadic_exact_rational_grid_72dpi\": [%d,%d],\n"
        "  \"nondyadic_graph_width_points\": %.17g,\n"
        "  \"nondyadic_ratio_candidate_width_points\": %.17g,\n"
        "  \"nondyadic_graph_ceil_grid_72dpi\": [%d,%d],\n"
        "  \"nondyadic_ratio_candidate_ceil_grid_72dpi\": [%d,%d],\n"
        "  \"nondyadic_candidate_width_overcount_cases\": %zu,\n"
        "  \"nondyadic_exact_rational_mismatch_count\": %zu,\n"
        "  \"nondyadic_original_pdfium_repeat_identical\": true,\n"
        "  \"nondyadic_equivalent_spellings_reference_equal\": true,\n"
        "  \"nondyadic_source_bytes_unchanged\": true,\n"
        "  \"failure_outputs_reset\": true,\n"
        "  \"production_parser_changed\": false,\n"
        "  \"native_device_grid_qualified\": false,\n"
        "  \"result\": \"passed\"\n"
        "}\n",
        valid_count, invalid_count, accepted_boxes, rejected_boxes,
        monotonic_steps, grid_steps, grid_budget_rejections,
        rational_width_grid, rational_height_grid,
        nondyadic_graph_width_points, nondyadic_candidate_width_points,
        nondyadic_graph_width, nondyadic_graph_height,
        nondyadic_candidate_width, nondyadic_candidate_height,
        nondyadic_candidate_overcounts, nondyadic_rational_mismatches);
    CHECK(std::ferror(report) == 0 && std::fclose(report) == 0);
    CHECK(std::rename(staged.c_str(), report_path) == 0);
    std::fprintf(stderr,
        "[issue-360] candidate admission: tokens=%zu/%zu "
        "crop=%zu/%zu monotonic=%zu grid=%zu budget-reject=%zu\n",
        valid_count, invalid_count, accepted_boxes, rejected_boxes,
        monotonic_steps, grid_steps, grid_budget_rejections);
    return EXIT_SUCCESS;
}


/* #360: independent source-backed DECIMAL UserUnit test-only qualification.
 * Inputs are literal original PDFs with a leaf /UserUnit real, not
 * Graph-exported or admitted external-source artifacts. The independent grid
 * uses exact small integer source dimensions and the authored Unit fraction.
 * Graph/binary64 ceil errors are observations, not successful qualifications. */
static int q360_source_grid_dimension(unsigned source_points,
                                      unsigned unit_numerator,
                                      unsigned unit_denominator,
                                      unsigned dpi)
{
    CHECK(source_points > 0u && source_points <= 10u &&
          unit_numerator > 0u && unit_numerator <= 8750u &&
          unit_denominator > 0u && unit_denominator <= 10000u &&
          (dpi == 72u || dpi == 96u || dpi == 144u));
    const uint64_t n=(uint64_t)source_points *
                     (uint64_t)unit_numerator * (uint64_t)dpi;
    const uint64_t d=(uint64_t)unit_denominator * UINT64_C(72);
    CHECK(n > 0u && d > 0u && n < UINT64_C(1000000000));
    return (int)(n/d + (n%d != 0u));
}

static int test_source_fractional_userunit(const char *prefix,
                                           const char *report_path)
{
    CHECK(prefix && *prefix && report_path && *report_path &&
          std::strcmp(prefix, report_path) != 0);
    struct unit_case {
        const char *spelling;
        unsigned numerator,denominator;
        int rotation;
    };
    const unit_case cases[] = {
        {".5",5u,10u,0}, {"0.50",50u,100u,0},
        {"1.1",11u,10u,90}, {"1.10",110u,100u,90},
        {"1.25",125u,100u,180}, {"1.250",1250u,1000u,180},
        {"0.875",875u,1000u,270}, {"0.8750",8750u,10000u,270}
    };
    constexpr size_t SOURCES=sizeof(cases)/sizeof(cases[0]);
    constexpr unsigned dpis[]={72u,96u,144u};
    constexpr size_t DPIS=sizeof(dpis)/sizeof(dpis[0]);
    constexpr size_t FRAMES=SOURCES*DPIS;
    static const char *const crop="6.1 3.375 16.1 11.375";
    const std::string staged=std::string(report_path)+".tmp";
    errno=0;
    if(std::remove(report_path)!=0)CHECK(errno==ENOENT);
    errno=0;
    if(std::remove(staged.c_str())!=0)CHECK(errno==ENOENT);
    FILE *report=std::fopen(staged.c_str(),"wb");
    CHECK(report!=nullptr);
    std::fprintf(report,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-360-source-fractional-userunit\",\n"
        "  \"fixture_kind\": \"test-generated-original-pdf\",\n"
        "  \"reference_oracle\": \"direct-public-pdfium-c-api\",\n"
        "  \"generated_source_count\": %zu,\n"
        "  \"reference_frame_count\": %zu,\n"
        "  \"reference_render_calls\": %zu,\n"
        "  \"source_crop\": \"%s\",\n"
        "  \"cases\": [\n",
        SOURCES,FRAMES,FRAMES*2u,crop);
    struct pdfium_guard {
        pdfium_guard() { FPDF_InitLibrary(); }
        ~pdfium_guard() { FPDF_DestroyLibrary(); }
    } pdfium;
    (void)pdfium;
    std::vector<unsigned char> first_reference[4][3];
    size_t graph_mismatches=0u, ratio_mismatches=0u;
    size_t exact_mismatches=0u, total_frames=0u;
    double crop_candidate[4] = {};
    decimal_source_crop_candidate(crop,crop_candidate);
    for(size_t i=0u;i<SOURCES;++i) {
        const unit_case &entry=cases[i];
        const bool leaf=(i%2u)!=0u;
        const int rotation=entry.rotation;
        const bool swapped=rotation==90 || rotation==270;
        const unsigned source_width=swapped?8u:10u;
        const unsigned source_height=swapped?10u:8u;
        const double exact_unit=(double)entry.numerator/
                                (double)entry.denominator;
        const std::string source_path=std::string(prefix)+
            "-unit-"+std::to_string(i)+".pdf";
        CHECK(source_path!=report_path && source_path!=staged);
        errno=0;
        if(std::remove(source_path.c_str())!=0) CHECK(errno==ENOENT);
        decimal_write_source(source_path,crop,leaf,rotation,leaf,
                             entry.spelling);
        const auto immutable=decimal_read_source(source_path);
        const std::string original_text(immutable.begin(),immutable.end());
        CHECK(original_text.find(std::string("/UserUnit ")+
              entry.spelling)!=std::string::npos &&
              original_text.find(std::string("/CropBox [")+crop+"]")!=
                  std::string::npos);
        quantapdf_document *document=nullptr;
        quantapdf_graph_snapshot *snapshot=nullptr;
        const auto graph_limit=graph_budget();
        OK(quantapdf_open(source_path.c_str(),nullptr,&document));
        OK(quantapdf_document_capture_graph(document,&graph_limit,&snapshot));
        quantapdf_close(document);
        CHECK(snapshot && snapshot->graph);
        quantapdf_graph_page_geometry graph{};
        quantapdf_graph_page_display display{};
        OK(quantapdf_graph_query_page_geometry(
             snapshot->graph,0u,2u,&graph));
        OK(quantapdf_graph_page_display_map(&graph,&display));
        quantapdf_graph_snapshot_drop(snapshot);
        CHECK(graph.rotation==rotation &&
              graph.media_box[0]==0.0 && graph.media_box[1]==0.0 &&
              graph.media_box[2]==20.0 && graph.media_box[3]==20.0 &&
              std::isfinite(graph.user_unit) &&
              std::abs(graph.user_unit-exact_unit)<1e-12 &&
              std::abs(display.width_points-
                       (double)source_width*exact_unit)<1e-9 &&
              std::abs(display.height_points-
                       (double)source_height*exact_unit)<1e-9);
        if(i!=0u)std::fprintf(report,",\n");
        std::fprintf(report,
            "    {\"source_index\":%zu,\"unit_literal\":\"%s\","
            "\"unit_fraction\":[%u,%u],\"crop_owner\":\"%s\","
            "\"rotate_owner\":\"%s\",\"rotation\":%d,"
            "\"graph_user_unit\":%.17g,"
            "\"graph_user_unit_bits\":\"%016llx\","
            "\"graph_display_points\":[%.17g,%.17g],"
            "\"graph_display_bits\":[\"%016llx\",\"%016llx\"],"
            "\"dpi_cases\":[",
            i,entry.spelling,entry.numerator,entry.denominator,
            leaf?"leaf":"root",leaf?"leaf":"root",rotation,
            graph.user_unit,
            (unsigned long long)decimal_double_bits(graph.user_unit),
            display.width_points,display.height_points,
            (unsigned long long)decimal_double_bits(display.width_points),
            (unsigned long long)decimal_double_bits(display.height_points));
        for(size_t d=0u;d<DPIS;++d) {
            const int expected_width=q360_source_grid_dimension(
                source_width,entry.numerator,entry.denominator,dpis[d]);
            const int expected_height=q360_source_grid_dimension(
                source_height,entry.numerator,entry.denominator,dpis[d]);
            quantapdf_test_rational_grid exact_grid={};
            CHECK(quantapdf_test_rational_page_grid_decimal_unit(
                crop,rotation,entry.spelling,dpis[d],100000u,&exact_grid));
            const bool rational_mismatch=
                exact_grid.width!=(size_t)expected_width ||
                exact_grid.height!=(size_t)expected_height;
            exact_mismatches+=(size_t)rational_mismatch;
            CHECK(!rational_mismatch);
            double pdfium_w=0.0,pdfium_h=0.0;
            double second_w=0.0,second_h=0.0;
            const auto reference=decimal_pdfium_original(
                source_path,expected_width,expected_height,
                &pdfium_w,&pdfium_h,dpis[d],rotation,exact_unit);
            const auto repeated=decimal_pdfium_original(
                source_path,expected_width,expected_height,
                &second_w,&second_h,dpis[d],rotation,exact_unit);
            CHECK(reference==repeated &&
                  pdfium_w==(double)source_width &&
                  pdfium_h==(double)source_height &&
                  decimal_double_bits(pdfium_w)==
                      decimal_double_bits(second_w) &&
                  decimal_double_bits(pdfium_h)==
                      decimal_double_bits(second_h));
            const size_t family=i/2u;
            if(first_reference[family][d].empty())
                first_reference[family][d]=reference;
            else CHECK(first_reference[family][d]==reference);
            const int graph_width=(int)std::ceil(
                display.width_points*(double)dpis[d]/72.0);
            const int graph_height=(int)std::ceil(
                display.height_points*(double)dpis[d]/72.0);
            const double ratio_width=
                (swapped?crop_candidate[3]-crop_candidate[1]:
                         crop_candidate[2]-crop_candidate[0])*
                exact_unit*(double)dpis[d]/72.0;
            const double ratio_height=
                (swapped?crop_candidate[2]-crop_candidate[0]:
                         crop_candidate[3]-crop_candidate[1])*
                exact_unit*(double)dpis[d]/72.0;
            const int ratio_grid_width=(int)std::ceil(ratio_width);
            const int ratio_grid_height=(int)std::ceil(ratio_height);
            CHECK(std::abs(graph_width-expected_width)<=1 &&
                  std::abs(graph_height-expected_height)<=1 &&
                  std::abs(ratio_grid_width-expected_width)<=1 &&
                  std::abs(ratio_grid_height-expected_height)<=1);
            const bool graph_diff=
                graph_width!=expected_width || graph_height!=expected_height;
            const bool ratio_diff=
                ratio_grid_width!=expected_width ||
                ratio_grid_height!=expected_height;
            graph_mismatches+=(size_t)graph_diff;
            ratio_mismatches+=(size_t)ratio_diff;
            if(d!=0u)std::fprintf(report,",");
            std::fprintf(report,
                "{\"dpi\":%u,\"expected_grid\":[%d,%d],"
                "\"pdfium_crop_points\":[%.17g,%.17g],"
                "\"pdfium_reference_rgb_fnv64\":\"%016llx\","
                "\"graph_ceil_estimate\":[%d,%d],"
                "\"binary64_ratio_estimate\":[%d,%d],"
                "\"exact_rational_grid\":[%zu,%zu],"
                "\"graph_mismatch\":%s,\"ratio_mismatch\":%s}",
                dpis[d],expected_width,expected_height,
                pdfium_w,pdfium_h,
                (unsigned long long)decimal_matrix_reference_hash(reference),
                graph_width,graph_height,
                ratio_grid_width,ratio_grid_height,
                exact_grid.width,exact_grid.height,
                graph_diff?"true":"false",ratio_diff?"true":"false");
            ++total_frames;
        }
        std::fprintf(report,"]}");
        CHECK(decimal_read_source(source_path)==immutable);
        CHECK(std::remove(source_path.c_str())==0);
    }
    CHECK(total_frames==FRAMES && exact_mismatches==0u);
    std::fprintf(report,
        "\n  ],\n"
        "  \"reference_frame_count_observed\": %zu,\n"
        "  \"graph_ceil_mismatch_count\": %zu,\n"
        "  \"binary64_ratio_mismatch_count\": %zu,\n"
        "  \"exact_rational_mismatch_count\": %zu,\n"
        "  \"equivalent_unit_spellings_reference_equal\": true,\n"
        "  \"original_pdf_bytes_unchanged\": true,\n"
        "  \"production_decoder_changed\": false,\n"
        "  \"native_device_grid_qualified\": false,\n"
        "  \"external_source_positive_count_increment\": 0,\n"
        "  \"result\": \"diagnostic-only\"\n"
        "}\n",total_frames,graph_mismatches,ratio_mismatches,exact_mismatches);
    CHECK(std::ferror(report)==0 && std::fclose(report)==0);
    CHECK(std::rename(staged.c_str(),report_path)==0);
    std::fprintf(stderr,
        "[issue-360] source fractional UserUnit: sources=%zu frames=%zu "
        "graph_mismatches=%zu binary64_mismatches=%zu rational=%zu; "
        "no production change\n",
        SOURCES,total_frames,graph_mismatches,ratio_mismatches,
        exact_mismatches);
    return EXIT_SUCCESS;
}


/* OPTIONAL geometry-only inspection of the independently published jsPDF
 * w3s_clip.pdf ORIGINAL supplied by the caller. No source file is bundled,
 * fetched, rewritten, rasterized or promoted by this executable. The
 * companion Python runner MUST hash-pin the source before/after invocation;
 * this function intentionally emits only un-attested diagnostic JSON. */
static int test_quarantined_jspdf_geometry(const char *source_path)
{
    CHECK(source_path && *source_path);
    const auto immutable = decimal_read_source(source_path);
    CHECK(immutable.size() == 3344u);
    const std::string source_text(immutable.begin(), immutable.end());
    CHECK(source_text.find("%PDF-1.3") == 0u &&
          source_text.find("/MediaBox [0 0 595.28 841.89]") !=
              std::string::npos &&
          source_text.find("/Producer (jsPDF 0.0.0)") !=
              std::string::npos);

    struct pdfium_guard {
        pdfium_guard() { FPDF_InitLibrary(); }
        ~pdfium_guard() { FPDF_DestroyLibrary(); }
    } pdfium;
    (void)pdfium;
    FPDF_DOCUMENT original = FPDF_LoadDocument(source_path, nullptr);
    CHECK(original != nullptr && FPDF_GetPageCount(original) == 1);
    FPDF_PAGE page = FPDF_LoadPage(original, 0);
    CHECK(page && FPDFPage_GetRotation(page) == 0);
    const double pdfium_w = (double)FPDF_GetPageWidthF(page);
    const double pdfium_h = (double)FPDF_GetPageHeightF(page);
    CHECK(std::isfinite(pdfium_w) && std::isfinite(pdfium_h) &&
          std::abs(pdfium_w - 595.28) < 0.01 &&
          std::abs(pdfium_h - 841.89) < 0.01);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(original);

    /* Inspect exact source -> owned Graph page geometry independently of
     * the public PDFium C page-width query. Source is closed before output. */
    quantapdf_document *document = nullptr;
    quantapdf_graph_snapshot *snapshot = nullptr;
    const auto budget = graph_budget();
    OK(quantapdf_open(source_path, nullptr, &document));
    OK(quantapdf_document_capture_graph(document, &budget, &snapshot));
    quantapdf_close(document);
    CHECK(snapshot && snapshot->graph);
    quantapdf_graph_page_geometry geometry{};
    quantapdf_graph_page_display display{};
    OK(quantapdf_graph_query_page_geometry(
        snapshot->graph, 0u, 32u, &geometry));
    OK(quantapdf_graph_page_display_map(&geometry, &display));
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(geometry.rotation == 0 && geometry.user_unit == 1.0);
    const double exact_box[4] = {0.0, 0.0, 595.28, 841.89};
    for (size_t i = 0u; i < 4u; ++i) {
        CHECK(std::isfinite(geometry.media_box[i]) &&
              std::isfinite(geometry.crop_box[i]) &&
              std::abs(geometry.media_box[i] - exact_box[i]) < 0.001 &&
              std::abs(geometry.crop_box[i] - exact_box[i]) < 0.001);
    }
    CHECK(std::isfinite(display.width_points) &&
          std::isfinite(display.height_points) &&
          std::abs(display.width_points - 595.28) < 0.001 &&
          std::abs(display.height_points - 841.89) < 0.001);

    constexpr unsigned dpis[] = {72u, 96u, 144u};
    constexpr size_t widths[] = {596u, 794u, 1191u};
    constexpr size_t heights[] = {842u, 1123u, 1684u};
    constexpr size_t cap = 1000000u;
    quantapdf_test_rational_grid admitted[3] = {};
    int pdfium_grid[3][2] = {};
    int graph_grid[3][2] = {};
    for (size_t i = 0u; i < 3u; ++i) {
        const unsigned dpi = dpis[i];
        const double pw = pdfium_w * (double)dpi / 72.0;
        const double ph = pdfium_h * (double)dpi / 72.0;
        CHECK(std::isfinite(pw) && std::isfinite(ph));
        pdfium_grid[i][0] = (int)std::ceil(pw);
        pdfium_grid[i][1] = (int)std::ceil(ph);
        graph_grid[i][0] = (int)std::ceil(
            display.width_points * (double)dpi / 72.0);
        graph_grid[i][1] = (int)std::ceil(
            display.height_points * (double)dpi / 72.0);
        CHECK(pdfium_grid[i][0] == (int)widths[i] &&
              pdfium_grid[i][1] == (int)heights[i] &&
              std::abs(graph_grid[i][0] - (int)widths[i]) <= 1 &&
              std::abs(graph_grid[i][1] - (int)heights[i]) <= 1);
        quantapdf_test_rational_grid repeat = {999u, 999u};
        admitted[i] = {999u, 999u};
        const int first = quantapdf_test_rational_page_grid(
            "0 0 595.28 841.89", 0, 1u, dpi, cap, &admitted[i]);
        const int second = quantapdf_test_rational_page_grid(
            "0 0 595.28 841.89", 0, 1u, dpi, cap, &repeat);
        const bool fits = widths[i] <= cap / heights[i];
        CHECK((first != 0) == fits && first == second &&
              admitted[i].width == repeat.width &&
              admitted[i].height == repeat.height);
        if (fits) {
            CHECK(admitted[i].width == widths[i] &&
                  admitted[i].height == heights[i]);
        } else {
            /* 144 DPI is too large: reject without a partial device grid. */
            CHECK(admitted[i].width == 0u &&
                  admitted[i].height == 0u);
        }
    }
    CHECK(decimal_read_source(source_path) == immutable);

    /* JSON on stdout; NOT a published or SHA-attested formal corpus report.
     * The hash-checking Python runner captures, validates and publishes it
     * only if the original's SHA-256 remains unchanged before and after. */
    std::printf(
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-269-quarantined-original-geometry\",\n"
        "  \"fixture_kind\": \"external-original-unvendored\",\n"
        "  \"source_size_bytes\": 3344,\n"
        "  \"pdfium_original_page_points\": [%.17g,%.17g],\n"
        "  \"graph_display_points\": [%.17g,%.17g],\n"
        "  \"source_user_unit\": 1,\n"
        "  \"source_rotation\": 0,\n"
        "  \"grid_cases\": [\n",
        pdfium_w, pdfium_h,
        display.width_points, display.height_points);
    for (size_t i = 0u; i < 3u; ++i) {
        const bool fits = widths[i] <= cap / heights[i];
        std::printf(
            "    {\"dpi\":%u,\"expected_grid\":[%zu,%zu],"
            "\"pdfium_geometry_grid\":[%d,%d],"
            "\"graph_ceil_diagnostic\":[%d,%d],"
            "\"rational_candidate_status\":\"%s\","
            "\"rational_candidate_output\":[%zu,%zu]}%s\n",
            dpis[i], widths[i], heights[i],
            pdfium_grid[i][0], pdfium_grid[i][1],
            graph_grid[i][0], graph_grid[i][1],
            fits ? "admitted" : "unsupported-capacity",
            admitted[i].width, admitted[i].height,
            i == 2u ? "" : ",");
    }
    std::printf(
        "  ],\n"
        "  \"pixel_budget\": 1000000,\n"
        "  \"pdfium_bitmap_created\": false,\n"
        "  \"native_raster_invoked\": false,\n"
        "  \"external_native_positive_increment\": 0,\n"
        "  \"official_corpus_admitted\": false,\n"
        "  \"result\": \"geometry-only-diagnostic\"\n"
        "}\n");
    CHECK(std::fflush(stdout) == 0);
    return EXIT_SUCCESS;
}


/* Issue #269 independent ORIGINAL PDFium device mapping A/B observation.
 * Fit mode is FPDF_RenderPageBitmap(... ceil(width*dpi/72),...): it maps the
 * full fractional CropBox into the full integer viewport (possibly a tiny
 * anisotropic stretch). Physical mode passes a uniform source-point scale
 * to FPDF_RenderPageBitmapWithMatrix and clips to the SAME integer grid.
 * These two hypotheses are observed, never silently conflated.
 *
 * The bitmap references are temporary, bounded and direct public PDFium C
 * API only: no private Native Raster, no PDF re-export, no external corpus
 * admission or pixel-positive claim. The outer SHA wrapper attests original
 * source bytes before/after the probe. */
struct qp269_viewport_observation {
    int width=0,height=0;
    size_t nonwhite=0u;
    uint64_t rgb_fnv64=UINT64_C(14695981039346656037);
    std::vector<unsigned char> rgb;
};

static qp269_viewport_observation qp269_render_pdfium_viewport(
    FPDF_PAGE page, int width, int height, unsigned dpi, bool physical)
{
    CHECK(page != nullptr && width > 0 && height > 0 &&
          (dpi == 72u || dpi == 96u) &&
          (size_t)width <= 1000000u / (size_t)height &&
          (size_t)width * (size_t)height <= SIZE_MAX / 3u);
    const FPDF_BITMAP bitmap=FPDFBitmap_CreateEx(
        width,height,FPDFBitmap_BGR,nullptr,0);
    CHECK(bitmap != nullptr);
    FPDFBitmap_FillRect(bitmap,0,0,width,height,UINT32_C(0xffffffff));
    if(physical) {
        /* PDFium documents this matrix in DEVICE/page-display coordinates.
         * With original source CropBox at (0,0), identity is the 72-DPI
         * source map, unlike the default integer-viewport stretch. */
        const float scale=(float)dpi/72.0f;
        const FS_MATRIX matrix={scale,0.0f,0.0f,scale,0.0f,0.0f};
        const FS_RECTF clip={0.0f,0.0f,(float)width,(float)height};
        FPDF_RenderPageBitmapWithMatrix(bitmap,page,&matrix,&clip,FPDF_ANNOT);
    } else {
        FPDF_RenderPageBitmap(
            bitmap,page,0,0,width,height,0,FPDF_ANNOT);
    }
    const int stride=FPDFBitmap_GetStride(bitmap);
    const unsigned char *src=static_cast<const unsigned char *>(
        FPDFBitmap_GetBuffer(bitmap));
    CHECK(src != nullptr && stride >= width*3);
    qp269_viewport_observation result;
    result.width=width;result.height=height;
    result.rgb.resize((size_t)width*(size_t)height*3u);
    for(int y=0;y<height;++y) {
        for(int x=0;x<width;++x) {
            const unsigned char *in=src+(size_t)y*(size_t)stride+
                                    (size_t)x*3u;
            unsigned char *out=result.rgb.data()+
                               ((size_t)y*(size_t)width+(size_t)x)*3u;
            /* BGR reference -> deterministic compact packed RGB8. */
            out[0]=in[2];out[1]=in[1];out[2]=in[0];
            const bool nonwhite=out[0]<250u||out[1]<250u||out[2]<250u;
            result.nonwhite+=(size_t)nonwhite;
            for(unsigned channel=0u;channel<3u;++channel) {
                result.rgb_fnv64^=(uint64_t)out[channel];
                result.rgb_fnv64*=UINT64_C(1099511628211);
            }
        }
    }
    FPDFBitmap_Destroy(bitmap);
    CHECK(result.nonwhite>=100u &&
          result.nonwhite<(size_t)width*(size_t)height/2u);
    return result;
}

static const unsigned char *qp269_rgb_at(
    const qp269_viewport_observation &bitmap,int x,int y)
{
    CHECK(x>=0 && y>=0 && x<bitmap.width && y<bitmap.height);
    return bitmap.rgb.data()+((size_t)y*(size_t)bitmap.width+
                              (size_t)x)*3u;
}

static void qp269_validate_original_interior(
    const qp269_viewport_observation &bitmap,unsigned dpi)
{
    const double scale=(double)dpi/72.0;
    /* Pinned upstream same-revision generator:
     * context2d.w3school.spec.js draws and clips (50,20,200,120)
     * then paints red (0,0,150,100). Two interior points distinguish
     * clipping from unconditional red painting. No anti-aliasing edges. */
    const int rx=(int)std::floor(75.0*scale);
    const int ry=(int)std::floor(60.0*scale);
    const int ox=(int)std::floor(10.0*scale);
    const int oy=(int)std::floor(10.0*scale);
    const int cx=(int)std::floor(180.0*scale);
    const int cy=(int)std::floor(50.0*scale);
    const unsigned char *red=qp269_rgb_at(bitmap,rx,ry);
    const unsigned char *outside=qp269_rgb_at(bitmap,ox,oy);
    const unsigned char *clipped=qp269_rgb_at(bitmap,cx,cy);
    CHECK(red[0]>=180u && red[1]<100u && red[2]<100u);
    CHECK(outside[0]>245u && outside[1]>245u && outside[2]>245u);
    CHECK(clipped[0]>245u && clipped[1]>245u && clipped[2]>245u);
}

static int test_quarantined_jspdf_pixel_mapping(const char *source_path)
{
    CHECK(source_path && *source_path);
    const auto original_bytes=decimal_read_source(source_path);
    CHECK(original_bytes.size()==3344u);
    const std::string original(original_bytes.begin(),original_bytes.end());
    CHECK(original.find("%PDF-1.3")==0u &&
          original.find("/MediaBox [0 0 595.28 841.89]")!=
              std::string::npos &&
          original.find("/Producer (jsPDF 0.0.0)")!=
              std::string::npos);
    struct pdfium_guard {
        pdfium_guard() { FPDF_InitLibrary(); }
        ~pdfium_guard() { FPDF_DestroyLibrary(); }
    } guard;
    (void)guard;
    FPDF_DOCUMENT document=FPDF_LoadDocument(source_path,nullptr);
    CHECK(document && FPDF_GetPageCount(document)==1);
    FPDF_PAGE page=FPDF_LoadPage(document,0);
    CHECK(page && FPDFPage_GetRotation(page)==0);
    const double page_w=(double)FPDF_GetPageWidthF(page);
    const double page_h=(double)FPDF_GetPageHeightF(page);
    CHECK(std::abs(page_w-595.28)<0.01 &&
          std::abs(page_h-841.89)<0.01);

    constexpr unsigned dpis[]={72u,96u,144u};
    constexpr size_t widths[]={596u,794u,1191u};
    constexpr size_t heights[]={842u,1123u,1684u};
    constexpr size_t max_pixels=1000000u;
    const char *const source_crop="0 0 595.28 841.89";

    std::printf(
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"test_id\": \"issue-269-original-pdfium-viewport-hypotheses\",\n"
        "  \"fixture_kind\": \"external-original-unvendored\",\n"
        "  \"reference_kind\": \"direct-public-pdfium-c-api-two-independent-modes\",\n"
        "  \"pdfium_original_page_points\": [%.17g,%.17g],\n"
        "  \"pixel_budget\": %zu,\n"
        "  \"dpi_cases\": [\n",page_w,page_h,max_pixels);
    for(size_t i=0;i<3u;++i) {
        const unsigned dpi=dpis[i];
        const size_t w=widths[i],h=heights[i];
        quantapdf_test_rational_grid grid={999u,999u};
        const int admitted=quantapdf_test_rational_page_grid(
            source_crop,0,1u,dpi,max_pixels,&grid);
        CHECK((admitted!=0)==(i!=2u));
        if(i==2u) {
            CHECK(grid.width==0u && grid.height==0u &&
                  w>max_pixels/h);
            std::printf(
                "    {\"dpi\":%u,\"expected_grid\":[%zu,%zu],"
                "\"status\":\"unsupported-capacity\","
                "\"pdfium_bitmap_allocated\":false,"
                "\"candidate_output\":[0,0]}%s\n",
                dpi,w,h,i==2u?"":",");
            continue;
        }
        CHECK(grid.width==w && grid.height==h);
        const int width=(int)w,height=(int)h;
        const auto fitted=qp269_render_pdfium_viewport(
            page,width,height,dpi,false);
        const auto repeated_fit=qp269_render_pdfium_viewport(
            page,width,height,dpi,false);
        const auto physical=qp269_render_pdfium_viewport(
            page,width,height,dpi,true);
        const auto repeated_physical=qp269_render_pdfium_viewport(
            page,width,height,dpi,true);
        CHECK(fitted.rgb==repeated_fit.rgb &&
              physical.rgb==repeated_physical.rgb &&
              fitted.rgb.size()==physical.rgb.size() &&
              fitted.nonwhite>0u && physical.nonwhite>0u);
        qp269_validate_original_interior(fitted,dpi);
        qp269_validate_original_interior(physical,dpi);

        size_t differing_pixels=0u,channel_abs=0u;
        unsigned largest_channel_delta=0u;
        for(size_t px=0u;px<w*h;++px) {
            unsigned highest=0u;
            for(size_t c=0u;c<3u;++c) {
                const size_t v=px*3u+c;
                const unsigned a=fitted.rgb[v],b=physical.rgb[v];
                const unsigned diff=a>b?a-b:b-a;
                channel_abs+=(size_t)diff;
                if(diff>highest)highest=diff;
                if(diff>largest_channel_delta)
                    largest_channel_delta=diff;
            }
            differing_pixels+=(size_t)(highest!=0u);
        }
        /* Deterministic numerical pixel-center mappings for the same
         * integer viewport; NOT a choice of one model over the other. */
        const double fit_sx=(double)w/page_w;
        const double fit_sy=(double)h/page_h;
        const double physical_s=(double)dpi/72.0;
        CHECK(std::isfinite(fit_sx) && std::isfinite(fit_sy) &&
              fit_sx>physical_s && fit_sy>physical_s);
        const double cx=((double)w-0.5)/fit_sx;
        const double physical_cx=((double)w-0.5)/physical_s;
        CHECK(physical_cx>cx);
        std::printf(
            "    {\"dpi\":%u,\"expected_grid\":[%zu,%zu],"
            "\"status\":\"diagnostic-only\","
            "\"bitmap_modes\":[\"fitted-integer-viewport\","
            "\"physical-dpi-matrix\"],"
            "\"pdfium_bitmap_allocated\":true,"
            "\"reference_repeats_identical\":true,"
            "\"upstream_clip_red_interior_goldens\":true,"
            "\"fitted_scale\":[%.17g,%.17g],"
            "\"physical_scale\":%.17g,"
            "\"rightmost_center_source_x_fitted\":%.17g,"
            "\"rightmost_center_source_x_physical\":%.17g,"
            "\"fitted_nonwhite_pixels\":%zu,"
            "\"physical_nonwhite_pixels\":%zu,"
            "\"fitted_rgb_fnv64\":\"%016llx\","
            "\"physical_rgb_fnv64\":\"%016llx\","
            "\"differing_rgb_pixels\":%zu,"
            "\"total_rgb_channel_absolute_difference\":%zu,"
            "\"largest_rgb_channel_difference\":%u}%s\n",
            dpi,w,h,fit_sx,fit_sy,physical_s,
            cx,physical_cx,
            fitted.nonwhite,physical.nonwhite,
            (unsigned long long)fitted.rgb_fnv64,
            (unsigned long long)physical.rgb_fnv64,
            differing_pixels,channel_abs,largest_channel_delta,
            i==2u?"":",");
    }
    std::printf(
        "  ],\n"
        "  \"bitmap_render_count\": 8,\n"
        "  \"models_are_pdfium_only\": true,\n"
        "  \"native_raster_invoked\": false,\n"
        "  \"native_pixel_positive\": false,\n"
        "  \"official_corpus_admitted\": false,\n"
        "  \"production_decoder_changed\": false,\n"
        "  \"result\": \"two-hypothesis-diagnostic-only\"\n"
        "}\n");
    FPDF_ClosePage(page);
    FPDF_CloseDocument(document);
    CHECK(decimal_read_source(source_path)==original_bytes &&
          std::fflush(stdout)==0);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::strcmp(argv[1],
             "--quarantined-jspdf-pixel-map") == 0)
        return test_quarantined_jspdf_pixel_mapping(argv[2]);
    if (argc == 3 && std::strcmp(argv[1],
             "--quarantined-jspdf-geometry") == 0)
        return test_quarantined_jspdf_geometry(argv[2]);
    if (argc == 4 && std::strcmp(argv[1], "--decimal-grid-diagnostic") == 0)
        return test_source_decimal_grid_diagnostic(argv[2], argv[3]);
    if (argc == 4 && std::strcmp(argv[1], "--decimal-grid-matrix") == 0)
        return test_source_decimal_grid_matrix(argv[2], argv[3]);
    if (argc == 4 && std::strcmp(argv[1], "--fractional-userunit-source") == 0)
        return test_source_fractional_userunit(argv[2], argv[3]);
    if (argc == 3 && std::strcmp(argv[1], "--decimal-parser-admission") == 0)
        return test_source_decimal_candidate_admission(argv[2]);
    CHECK(argc == 1);
    test_inherited_geometry();
    test_overrides_and_noncanonical();
    test_crop_media_intersection_policy();
    test_selected_version();
    test_snapshot_to_composer();
}
