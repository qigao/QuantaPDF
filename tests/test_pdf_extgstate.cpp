#include "backend/qpdf_composer.h"
#include "document/page_composer_ir.h"
#include "graph/graph_edit.h"

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
static quantapdf_graph_stream_limits decode_budget() { return {8192, 128, 100000}; }
static quantapdf_pdf_content_limits content_budget() { return {8192, 256, 16}; }
static quantapdf_pdf_path_lower_limits lower_budget() { return {64, 1024}; }

static const char *valid_resources =
    "<< /ExtGState << "
    "/Half << /Type /ExtGState /ca 0.5 >> "
    "/Stroke << /CA 0.25 >> "
    "/Multi << /BM /Multiply >> "
    "/Screen << /BM /Screen /ca 0.25 >> "
    ">> >>";

static void save(const char *filename, const unsigned char *bytes, size_t size)
{
    FILE *file = std::fopen(filename, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(bytes, 1, size, file) == size && std::fclose(file) == 0);
}

static void source_pdf(const char *resources, const char *content,
    int rotate, int unit, bool empty_leaf_resources)
{
    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto root = pdf->getRoot().getKey("/Pages");
    auto page = pdf->makeIndirectObject(QPDFObjectHandle::parse(
        "<< /Type /Page >>"));
    root.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 120 120]"));
    root.replaceKey("/Kids", QPDFObjectHandle::newArray({page}));
    root.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    root.replaceKey("/Resources", QPDFObjectHandle::parse(resources));
    root.replaceKey("/Rotate", QPDFObjectHandle::newInteger(rotate));
    page.replaceKey("/Parent", root);
    if (unit != 1) {
        page.replaceKey("/CropBox", QPDFObjectHandle::parse("[10 10 110 110]"));
        page.replaceKey("/UserUnit", QPDFObjectHandle::newInteger(unit));
    }
    if (empty_leaf_resources)
        page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
    page.replaceKey("/Contents", pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    auto buffer = writer.getBufferSharedPointer();
    save(EXTGSTATE_SOURCE_PDF, buffer->getBuffer(), buffer->getSize());
}

struct bitmap_image {
    int width = 0, height = 0, stride = 0, channels = 0;
    std::vector<unsigned char> pixels;
    const unsigned char *at(int x, int y) const {
        CHECK(x >= 0 && y >= 0 && x < width && y < height);
        return pixels.data() + (size_t)y * stride + (size_t)x * 3;
    }
};

static bitmap_image bitmap(const char *name)
{
    quantapdf_document *document = nullptr;
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *image = nullptr;
    const unsigned char *ptr = nullptr;
    size_t bytes = 0;
    bitmap_image result;
    OK(quantapdf_open(name, nullptr, &document));
    OK(quantapdf_load_page(document, 0, &page));
    OK(quantapdf_render_page(page, &image));
    OK(quantapdf_bitmap_dimensions(image, &result.width, &result.height,
        &result.stride, &result.channels));
    OK(quantapdf_bitmap_data(image, &ptr, &bytes));
    CHECK(result.channels == 3 && result.width > 0 &&
        result.height > 0 && result.stride >= result.width * 3 &&
        bytes >= (size_t)result.height * result.stride);
    result.pixels.assign(ptr, ptr + (size_t)result.height * result.stride);
    quantapdf_drop_bitmap(image);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    return result;
}

static quantapdf_status lower_source(quantapdf_ir *ir)
{
    quantapdf_document *document = nullptr;
    quantapdf_graph_snapshot *snapshot = nullptr;
    auto graph = graph_budget();
    auto decode = decode_budget();
    auto content = content_budget();
    auto lower = lower_budget();
    OK(quantapdf_open(EXTGSTATE_SOURCE_PDF, nullptr, &document));
    OK(quantapdf_document_capture_graph(document, &graph, &snapshot));
    quantapdf_close(document);
    quantapdf_status status = quantapdf_graph_snapshot_page_composer_ir(
        snapshot, 0, 2, &decode, &content, 1, &lower, ir);
    quantapdf_graph_snapshot_drop(snapshot);
    return status;
}

static void export_composer(quantapdf_ir *ir)
{
    unsigned char *first = nullptr, *second = nullptr;
    size_t first_size = 0, second_size = 0;
    OK(quantapdf_qpdf_compose(ir, &first, &first_size));
    OK(quantapdf_qpdf_compose(ir, &second, &second_size));
    CHECK(first_size != 0 && first_size == second_size &&
        std::memcmp(first, second, first_size) == 0);
    quantapdf_ir_dispose(ir);
    save(EXTGSTATE_LOWERED_PDF, first, first_size);
    std::free(first);
    std::free(second);
}

static void compare_pixels(int expected_width, int expected_height)
{
    bitmap_image source = bitmap(EXTGSTATE_SOURCE_PDF);
    bitmap_image lowered = bitmap(EXTGSTATE_LOWERED_PDF);
    CHECK(source.width == expected_width && source.height == expected_height);
    CHECK(lowered.width == source.width && lowered.height == source.height);
    size_t changed = 0, count = (size_t)source.width * source.height;
    for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < source.width; ++x) {
            auto a = source.at(x,y), b = lowered.at(x,y);
            if (std::abs((int)a[0] - (int)b[0]) > 8 ||
                std::abs((int)a[1] - (int)b[1]) > 8 ||
                std::abs((int)a[2] - (int)b[2]) > 8) ++changed;
        }
    if (changed > count / 200) {
        std::fprintf(stderr, "ExtGState pixel mismatch %zu / %zu\n",
            changed, count);
        CHECK(false);
    }
    auto white = expected_width == 120 ?
        source.at(115, 5) : source.at(2, 2);
    auto purple = source.at(expected_width == 120 ? 30 : 40,
        expected_height == 120 ? 89 : 130);
    /* Pixel-by-pixel source/export comparison above is authoritative.
     * Keep a color sanity check at a sample inside the alpha-overlap region;
     * do not assume an unrotated device-space coordinate is white after the
     * source renderer applies Rotate and UserUnit. */
    if (expected_width == 120) {
        CHECK(purple[0] > 100 && purple[0] < 170 &&
            purple[2] > 100 && purple[2] < 170);
    }
}

static void remove_files()
{
    CHECK(std::remove(EXTGSTATE_SOURCE_PDF) == 0);
    CHECK(std::remove(EXTGSTATE_LOWERED_PDF) == 0);
}

static void test_transparency_and_blend(bool rotated)
{
    static const char *content =
        "0 0 1 rg 5 5 80 80 re f "
        "q /Half gs 1 0 0 rg 25 25 75 55 re f Q "
        "q /Multi gs 0 1 0 rg 40 40 35 30 re f Q "
        "q 15 15 80 80 re W n /Half gs 1 0 0 rg "
        "75 75 30 25 re f Q "
        "q /Stroke gs 1 0 0 RG 5 w 10 105 m 110 105 l S Q";
    source_pdf(valid_resources, content, rotated ? 90 : 0,
        rotated ? 2 : 1, false);
    quantapdf_ir ir{};
    OK(lower_source(&ir));
    CHECK(ir.page_count == 1 && ir.operation_count == 5 &&
        ir.resources.clip_count == 1 &&
        ir.resources.graphics_state_count >= 3);
    CHECK(ir.resources.graphics_states[
        ir.operations[1].graphics_state_id - 1].fill_alpha == 0.5f);
    CHECK(ir.resources.graphics_states[
        ir.operations[2].graphics_state_id - 1].blend_mode ==
            QUANTAPDF_COMPOSER_BLEND_MULTIPLY);
    CHECK(ir.resources.graphics_states[
        ir.operations[3].graphics_state_id - 1].clip_id != 0);
    export_composer(&ir);
    compare_pixels(rotated ? 200 : 120, rotated ? 200 : 120);
    remove_files();
}

static void test_unsupported_and_missing()
{
    struct {
        const char *resource;
        const char *content;
        bool leaf_override;
        quantapdf_status expected;
    } cases[] = {
        {"<< /ExtGState << /Half << /ca -0.25 >> >> >>",
            "/Half gs 0 0 10 10 re f", false, QUANTAPDF_ERROR_FORMAT},
        {"<< /ExtGState << /Half << /BM /ColorDodge >> >> >>",
            "/Half gs 0 0 10 10 re f", false, QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /ExtGState << /Half << /SMask /None >> >> >>",
            "/Half gs 0 0 10 10 re f", false, QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /ExtGState << /Half << /Font [/F1 12] >> >> >>",
            "/Half gs 0 0 10 10 re f", false, QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /ExtGState << /Half << /BM [/Multiply /Normal] >> >> >>",
            "/Half gs 0 0 10 10 re f", false, QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /ExtGState << /Half << /Type /Other /ca 0.5 >> >> >>",
            "/Half gs 0 0 10 10 re f", false, QUANTAPDF_ERROR_FORMAT},
        {valid_resources, "/Missing gs 0 0 10 10 re f",
            false, QUANTAPDF_ERROR_FORMAT},
        {valid_resources, "/Half gs 0 0 10 10 re f",
            true, QUANTAPDF_ERROR_FORMAT}
    };
    for (const auto& entry : cases) {
        source_pdf(entry.resource, entry.content, 0, 1, entry.leaf_override);
        quantapdf_ir ir;
        std::memset(&ir, 0xab, sizeof(ir));
        CHECK(lower_source(&ir) == entry.expected);
        CHECK(ir.pages == nullptr && ir.operations == nullptr &&
            ir.resources.graphics_states == nullptr);
        CHECK(std::remove(EXTGSTATE_SOURCE_PDF) == 0);
    }
}

static void test_selected_version_alpha()
{
    source_pdf(valid_resources, "/Half gs 0 0 1 rg 10 10 60 60 re f",
        0, 1, false);
    auto graph = graph_budget();
    auto decode = decode_budget();
    auto content = content_budget();
    auto lower = lower_budget();
    quantapdf_document *document = nullptr;
    quantapdf_graph_snapshot *base = nullptr, *next = nullptr;
    quantapdf_validation_report report{};
    OK(quantapdf_open(EXTGSTATE_SOURCE_PDF, nullptr, &document));
    OK(quantapdf_document_capture_graph(document, &graph, &base));
    quantapdf_close(document);
    auto model = quantapdf_graph_view(base->graph);
    auto pages = quantapdf_graph_find(model, model->catalog, "/Pages", 6);
    CHECK(pages != nullptr);
    auto resources = quantapdf_graph_find(model, pages->target, "/Resources", 10);
    CHECK(resources != nullptr);
    auto ext = quantapdf_graph_find(model, resources->target, "/ExtGState", 10);
    CHECK(ext != nullptr);
    auto half = quantapdf_graph_find(model, ext->target, "/Half", 5);
    CHECK(half != nullptr);
    auto alpha = quantapdf_graph_find(model, half->target, "/ca", 3);
    CHECK(alpha != nullptr);
    auto original = quantapdf_graph_node_at(model, alpha->target);
    CHECK(original->kind == QUANTAPDF_GRAPH_REAL);
    static const char new_alpha[] = "0.25";
    quantapdf_graph_replacement replacement{};
    replacement.id = alpha->target;
    replacement.value.kind = QUANTAPDF_GRAPH_REAL;
    replacement.value.indirect = original->indirect;
    replacement.value.data = new_alpha;
    replacement.value.data_size = sizeof(new_alpha) - 1;
    quantapdf_graph_change_set changes{};
    changes.lineage = quantapdf_graph_lineage(base->graph);
    changes.base_version = quantapdf_graph_version(base->graph);
    changes.replacements = &replacement;
    changes.replacement_count = 1;
    OK(quantapdf_graph_snapshot_commit(base, &changes, &graph, &next, &report));
    quantapdf_ir old_ir{}, new_ir{};
    OK(quantapdf_graph_snapshot_page_composer_ir(base, 0, 2,
        &decode, &content, 1, &lower, &old_ir));
    OK(quantapdf_graph_snapshot_page_composer_ir(next, 0, 2,
        &decode, &content, 1, &lower, &new_ir));
    CHECK(old_ir.resources.graphics_state_count == 1 &&
        new_ir.resources.graphics_state_count == 1);
    CHECK(old_ir.resources.graphics_states[0].fill_alpha == 0.5f);
    CHECK(new_ir.resources.graphics_states[0].fill_alpha == 0.25f);
    quantapdf_graph_snapshot_drop(base);
    quantapdf_graph_snapshot_drop(next);
    quantapdf_ir_dispose(&old_ir);
    export_composer(&new_ir);
    auto image = bitmap(EXTGSTATE_LOWERED_PDF);
    CHECK(image.width == 120 && image.height == 120);
    remove_files();
}

int main()
{
    test_transparency_and_blend(false);
    test_transparency_and_blend(true);
    test_unsupported_and_missing();
    test_selected_version_alpha();
    return 0;
}
