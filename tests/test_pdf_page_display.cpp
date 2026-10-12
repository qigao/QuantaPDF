#include "backend/qpdf_composer.h"
#include "document/page_composer_ir.h"
#include "graph/graph_edit.h"
#include "graph/graph_page_geometry.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static quantapdf_graph_limits graph_budget() { return {10000, 30000, 1048576, 1048576, 4194304, 256}; }
static quantapdf_graph_stream_limits stream_budget() { return {8192, 128, 100000}; }
static quantapdf_pdf_content_limits content_budget() { return {8192, 256, 16}; }
static quantapdf_pdf_path_lower_limits lower_budget() { return {64, 1024}; }

static void source_pdf(int rotate, int unit)
{
    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto root = pdf->getRoot().getKey("/Pages");
    auto page = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Page >>"));
    root.replaceKey("/MediaBox", QPDFObjectHandle::parse("[20 30 220 150]"));
    root.replaceKey("/Rotate", QPDFObjectHandle::newInteger(rotate));
    root.replaceKey("/Kids", QPDFObjectHandle::newArray({page}));
    root.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    page.replaceKey("/Parent", root);
    page.replaceKey("/CropBox", QPDFObjectHandle::parse("[40 40 140 100]"));
    page.replaceKey("/UserUnit", QPDFObjectHandle::newInteger(unit));
    // The source graphics CTM intentionally translates the path before
    // the page's crop/rotate/UserUnit mapping must be applied.
    page.replaceKey("/Contents", pdf->newStream(
        "q 1 0 0 1 2 3 cm 0 0 1 rg 50 50 20 20 re f Q"));

    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    auto buffer = writer.getBufferSharedPointer();
    FILE *f = std::fopen(DISPLAY_SOURCE_PDF, "wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(buffer->getBuffer(), 1, buffer->getSize(), f) == buffer->getSize());
    CHECK(std::fclose(f) == 0);
}

struct image {
    int width = 0, height = 0, stride = 0, components = 0;
    std::vector<unsigned char> bytes;
    unsigned char const *at(int x, int y) const {
        CHECK(x >= 0 && x < width && y >= 0 && y < height);
        return bytes.data() + (size_t)y * (size_t)stride + (size_t)x * (size_t)components;
    }
};

static image raster(const char *path)
{
    quantapdf_document *document = nullptr;
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *bitmap = nullptr;
    const unsigned char *pixels = nullptr;
    size_t size = 0;
    image result;
    OK(quantapdf_open(path, nullptr, &document));
    OK(quantapdf_load_page(document, 0, &page));
    OK(quantapdf_render_page(page, &bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap, &result.width,
        &result.height, &result.stride, &result.components));
    OK(quantapdf_bitmap_data(bitmap, &pixels, &size));
    CHECK(result.components == 3 && result.width > 0 && result.height > 0 &&
        result.stride >= result.width * result.components &&
        size >= (size_t)result.stride * (size_t)result.height);
    result.bytes.assign(pixels, pixels + (size_t)result.stride * (size_t)result.height);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    return result;
}

static void save_composer(const quantapdf_ir *ir)
{
    unsigned char *first = nullptr, *again = nullptr;
    size_t size = 0, again_size = 0;
    OK(quantapdf_qpdf_compose(ir, &first, &size));
    OK(quantapdf_qpdf_compose(ir, &again, &again_size));
    CHECK(first != nullptr && size > 0 && again_size == size &&
        std::memcmp(first, again, size) == 0);
    FILE *f = std::fopen(DISPLAY_RESULT_PDF, "wb");
    CHECK(f != nullptr);
    CHECK(std::fwrite(first, 1, size, f) == size);
    CHECK(std::fclose(f) == 0);
    std::free(first);
    std::free(again);
}

static quantapdf_graph_page_display get_plan(quantapdf_graph_snapshot *snapshot)
{
    quantapdf_graph_page_geometry geometry = {};
    quantapdf_graph_page_display display = {};
    OK(quantapdf_graph_query_page_geometry(snapshot->graph, 0, 2, &geometry));
    OK(quantapdf_graph_page_display_map(&geometry, &display));
    return display;
}

static void sample_blue(image const& src, image const& dst,
    quantapdf_graph_page_display const& display)
{
    CHECK(src.width == dst.width && src.height == dst.height &&
        src.width == (int)display.width_points &&
        src.height == (int)display.height_points);
    // The source path rectangle spans (50..70,50..70), and its CTM
    // translates by (2,3): test its true interior at (62,63).
    double px = 62 * display.from_source[0] + 63 * display.from_source[2] +
        display.from_source[4];
    double py = 62 * display.from_source[1] + 63 * display.from_source[3] +
        display.from_source[5];
    int x = (int)std::floor(px);
    int y = (int)std::floor(display.height_points - py);
    for (int dy = -2; dy <= 2; dy += 2)
        for (int dx = -2; dx <= 2; dx += 2) {
            const unsigned char *a = src.at(x + dx, y + dy);
            const unsigned char *b = dst.at(x + dx, y + dy);
            CHECK(a[0] < 20 && a[1] < 20 && a[2] > 230);
            CHECK(b[0] < 20 && b[1] < 20 && b[2] > 230);
        }
    auto a = src.at(2, 2), b = dst.at(2, 2);
    CHECK(a[0] > 230 && a[1] > 230 && a[2] > 230);
    CHECK(b[0] > 230 && b[1] > 230 && b[2] > 230);
}

static void test_actual_pdf(int rotation, int unit)
{
    source_pdf(rotation, unit);
    image baseline = raster(DISPLAY_SOURCE_PDF);
    auto gb = graph_budget();
    auto db = stream_budget();
    auto cb = content_budget();
    auto lb = lower_budget();
    quantapdf_document *document = nullptr;
    quantapdf_graph_snapshot *snapshot = nullptr;
    quantapdf_ir ir = {};
    OK(quantapdf_open(DISPLAY_SOURCE_PDF, nullptr, &document));
    OK(quantapdf_document_capture_graph(document, &gb, &snapshot));
    quantapdf_close(document);
    auto display = get_plan(snapshot);
    double width = unit * (rotation % 180 ? 60.0 : 100.0);
    double height = unit * (rotation % 180 ? 100.0 : 60.0);
    CHECK(display.width_points == width && display.height_points == height);
    OK(quantapdf_graph_snapshot_page_composer_ir(snapshot, 0, 2,
        &db, &cb, 1, &lb, &ir));
    CHECK(ir.page_count == 1 && ir.operation_count == 1 &&
        ir.pages[0].width_points == (float)width &&
        ir.pages[0].height_points == (float)height);
    quantapdf_graph_snapshot_drop(snapshot);
    save_composer(&ir);
    quantapdf_ir_dispose(&ir);
    image output = raster(DISPLAY_RESULT_PDF);
    sample_blue(baseline, output, display);
    CHECK(std::remove(DISPLAY_SOURCE_PDF) == 0);
    CHECK(std::remove(DISPLAY_RESULT_PDF) == 0);
}

static void test_malformed_or_outside()
{
    quantapdf_graph_page_geometry geom = {};
    quantapdf_graph_page_display plan = {};
    geom.media_box[0] = 20; geom.media_box[1] = 30;
    geom.media_box[2] = 220; geom.media_box[3] = 150;
    geom.crop_box[0] = 40; geom.crop_box[1] = 40;
    geom.crop_box[2] = 140; geom.crop_box[3] = 100;
    geom.user_unit = 1;
    geom.rotation = 90;
    OK(quantapdf_graph_page_display_map(&geom, &plan));
    CHECK(plan.width_points == 60 && plan.height_points == 100);
    CHECK(plan.from_source[0] == 0 && plan.from_source[1] == -1 &&
        plan.from_source[2] == 1 && plan.from_source[3] == 0 &&
        plan.from_source[4] == -40 && plan.from_source[5] == 140);

    geom.crop_box[0] = 10; // CropBox outside MediaBox is a later contract.
    plan.width_points = 1234;
    CHECK(quantapdf_graph_page_display_map(&geom, &plan) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(plan.width_points == 0 && plan.from_source[5] == 0);
    geom.crop_box[0] = 40;
    geom.rotation = 45;
    CHECK(quantapdf_graph_page_display_map(&geom, &plan) == QUANTAPDF_ERROR_ARGUMENT);
    geom.rotation = 0;
    geom.user_unit = 0;
    CHECK(quantapdf_graph_page_display_map(&geom, &plan) == QUANTAPDF_ERROR_ARGUMENT);
    geom.user_unit = 1;
    geom.crop_box[2] = geom.crop_box[0];
    CHECK(quantapdf_graph_page_display_map(&geom, &plan) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_graph_page_display_map(nullptr, &plan) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(plan.height_points == 0 && plan.from_source[0] == 0);
}

static void test_selected_rotation_version()
{
    source_pdf(0, 1);
    auto gb = graph_budget();
    auto db = stream_budget();
    auto cb = content_budget();
    auto lb = lower_budget();
    quantapdf_document *document = nullptr;
    quantapdf_graph_snapshot *base = nullptr, *next = nullptr;
    quantapdf_validation_report report{};
    quantapdf_ir a{}, b{};
    OK(quantapdf_open(DISPLAY_SOURCE_PDF, nullptr, &document));
    OK(quantapdf_document_capture_graph(document, &gb, &base));
    quantapdf_close(document);
    auto model = quantapdf_graph_view(base->graph);
    auto root = quantapdf_graph_find(model, model->catalog, "/Pages", 6);
    CHECK(root != nullptr);
    auto rotation = quantapdf_graph_find(model, root->target, "/Rotate", 7);
    CHECK(rotation != nullptr && model->nodes[rotation->target - 1].kind == QUANTAPDF_GRAPH_INTEGER);
    quantapdf_graph_replacement replacement{};
    replacement.id = rotation->target;
    replacement.value.kind = QUANTAPDF_GRAPH_INTEGER;
    replacement.value.indirect = model->nodes[rotation->target - 1].indirect;
    replacement.value.integer = 90;
    quantapdf_graph_change_set changes{};
    changes.lineage = quantapdf_graph_lineage(base->graph);
    changes.base_version = quantapdf_graph_version(base->graph);
    changes.replacements = &replacement;
    changes.replacement_count = 1;
    OK(quantapdf_graph_snapshot_commit(base, &changes, &gb, &next, &report));
    OK(quantapdf_graph_snapshot_page_composer_ir(base, 0, 2,
        &db, &cb, 1, &lb, &a));
    OK(quantapdf_graph_snapshot_page_composer_ir(next, 0, 2,
        &db, &cb, 1, &lb, &b));
    CHECK(a.pages[0].width_points == 100 && a.pages[0].height_points == 60);
    CHECK(b.pages[0].width_points == 60 && b.pages[0].height_points == 100);
    quantapdf_graph_snapshot_drop(base);
    quantapdf_graph_snapshot_drop(next);
    quantapdf_ir_dispose(&a);
    save_composer(&b);
    quantapdf_ir_dispose(&b);
    image output = raster(DISPLAY_RESULT_PDF);
    CHECK(output.width == 60 && output.height == 100);
    int x = 23, y = 22; // Rotated center after the content's translation.
    const unsigned char *inside = output.at(x, y);
    CHECK(inside[0] < 20 && inside[1] < 20 && inside[2] > 230);
    CHECK(std::remove(DISPLAY_SOURCE_PDF) == 0);
    CHECK(std::remove(DISPLAY_RESULT_PDF) == 0);
}

int main()
{
    test_malformed_or_outside();
    test_actual_pdf(0, 1);
    test_actual_pdf(90, 1);
    test_actual_pdf(180, 1);
    test_actual_pdf(270, 1);
    test_actual_pdf(90, 2);
    test_selected_rotation_version();
    return 0;
}
