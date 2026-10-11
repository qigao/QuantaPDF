#include "backend/qpdf_composer.h"
#include "document/page_composer_ir.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

struct pixel {
    unsigned char r, g, b;
};
struct raster {
    int width = 0, height = 0, stride = 0, components = 0;
    std::vector<unsigned char> bytes;
    pixel at(int x, int y) const
    {
        CHECK(x >= 0 && y >= 0 && x < width && y < height && components == 3);
        const unsigned char *p = bytes.data() + (size_t)y * stride + (size_t)x * 3;
        return {p[0], p[1], p[2]};
    }
};

static raster render(const char *name)
{
    quantapdf_document *document = nullptr;
    quantapdf_page *page = nullptr;
    quantapdf_bitmap *bitmap = nullptr;
    const unsigned char *bytes = nullptr;
    size_t count = 0;
    raster result;
    OK(quantapdf_open(name, nullptr, &document));
    OK(quantapdf_load_page(document, 0, &page));
    OK(quantapdf_render_page(page, &bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap, &result.width, &result.height,
        &result.stride, &result.components));
    OK(quantapdf_bitmap_data(bitmap, &bytes, &count));
    CHECK(result.components == 3 && result.width > 0 &&
        result.height > 0 && result.stride >= result.width * 3 &&
        count >= (size_t)result.height * (size_t)result.stride);
    result.bytes.assign(bytes, bytes + (size_t)result.height * result.stride);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    return result;
}

static void file_write(const char *name, unsigned char const *data, size_t size)
{
    FILE *file = std::fopen(name, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(data, 1, size, file) == size && std::fclose(file) == 0);
}

static void pdf_source(const char *content, int rotation, int userunit)
{
    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto tree = pdf->getRoot().getKey("/Pages");
    auto page = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Page >>"));
    tree.replaceKey("/Kids", QPDFObjectHandle::newArray({page}));
    tree.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    if (rotation == 0 && userunit == 1) {
        tree.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 120 120]"));
    } else {
        tree.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 120 120]"));
        tree.replaceKey("/Rotate", QPDFObjectHandle::newInteger(rotation));
        page.replaceKey("/CropBox", QPDFObjectHandle::parse("[10 10 110 110]"));
        page.replaceKey("/UserUnit", QPDFObjectHandle::newInteger(userunit));
    }
    page.replaceKey("/Parent", tree);
    page.replaceKey("/Contents", pdf->newStream(content));
    QPDFWriter writer(*pdf);
    writer.setMinimumPDFVersion("1.6");
    writer.setOutputMemory();
    writer.write();
    auto blob = writer.getBufferSharedPointer();
    file_write(CLIP_SOURCE_PDF, blob->getBuffer(), blob->getSize());
}

static void pdf_lower()
{
    quantapdf_document *document = nullptr;
    quantapdf_graph_snapshot *snapshot = nullptr;
    quantapdf_ir ir{};
    quantapdf_graph_limits graph = {10000, 30000, 1048576, 1048576, 4194304, 256};
    quantapdf_graph_stream_limits decode = {8192, 128, 100000};
    quantapdf_pdf_content_limits content = {8192, 512, 16};
    quantapdf_pdf_path_lower_limits lower = {64, 1024};
    OK(quantapdf_open(CLIP_SOURCE_PDF, nullptr, &document));
    OK(quantapdf_document_capture_graph(document, &graph, &snapshot));
    quantapdf_close(document);
    OK(quantapdf_graph_snapshot_page_composer_ir(snapshot, 0, 2,
        &decode, &content, 1, &lower, &ir));
    CHECK(ir.page_count == 1 && ir.resources.clip_count != 0);
    quantapdf_graph_snapshot_drop(snapshot);

    unsigned char *first = nullptr, *again = nullptr;
    size_t size = 0, again_size = 0;
    OK(quantapdf_qpdf_compose(&ir, &first, &size));
    OK(quantapdf_qpdf_compose(&ir, &again, &again_size));
    CHECK(size > 0 && size == again_size &&
        std::memcmp(first, again, size) == 0);
    quantapdf_ir_dispose(&ir);
    file_write(CLIP_LOWERED_PDF, first, size);
    std::free(first); std::free(again);
}

static void check_color(pixel p, char color)
{
    switch (color) {
    case 'R': CHECK(p.r > 230 && p.g < 20 && p.b < 20); break;
    case 'G': CHECK(p.r < 20 && p.g > 230 && p.b < 20); break;
    case 'B': CHECK(p.r < 20 && p.g < 20 && p.b > 230); break;
    case 'K': CHECK(p.r < 20 && p.g < 20 && p.b < 20); break;
    case 'W': CHECK(p.r > 230 && p.g > 230 && p.b > 230); break;
    default: CHECK(false);
    }
}
struct expected {
    int x, y; // PDF Y-up space before source display conversion
    char color;
};

static void compare(const expected *samples, size_t sample_count,
    int expected_width, int expected_height)
{
    raster source = render(CLIP_SOURCE_PDF);
    raster lowered = render(CLIP_LOWERED_PDF);
    CHECK(source.width == expected_width && source.height == expected_height);
    CHECK(lowered.width == source.width && lowered.height == source.height);
    size_t changed = 0, pixels = (size_t)source.width * source.height;
    for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < source.width; ++x) {
            pixel a = source.at(x, y), b = lowered.at(x, y);
            if (std::abs((int)a.r - (int)b.r) > 8 ||
                std::abs((int)a.g - (int)b.g) > 8 ||
                std::abs((int)a.b - (int)b.b) > 8) ++changed;
        }
    if (changed > pixels / 200) {
        std::fprintf(stderr, "clip raster mismatch: %zu of %zu pixels\n",
            changed, pixels);
        CHECK(false);
    }
    for (size_t i = 0; i < sample_count; ++i) {
        const auto& s = samples[i];
        // Unrotated destination canvas; pixel sample well inside geometry.
        int bitmap_y = expected_height - 1 - s.y;
        check_color(source.at(s.x, bitmap_y), s.color);
        check_color(lowered.at(s.x, bitmap_y), s.color);
    }
}

static void run(const char *content, const expected *samples, size_t count)
{
    pdf_source(content, 0, 1);
    pdf_lower();
    compare(samples, count, 120, 120);
    CHECK(std::remove(CLIP_SOURCE_PDF) == 0);
    CHECK(std::remove(CLIP_LOWERED_PDF) == 0);
}

static void test_nested_q_q_and_evenodd(void)
{
    static const char *content =
        "q 0 0 80 120 re W n "
        "0 0 1 rg 0 0 120 36 re f "
        "q 20 36 40 48 re 26 50 8 12 re W* n "
        "1 0 0 rg 0 36 120 48 re f Q "
        "0 1 0 rg 0 84 120 36 re f Q "
        "0 0 0 rg 90 0 20 20 re f";
    static const expected samples[] = {
        {10, 10, 'B'}, {85, 10, 'W'},
        {22, 45, 'R'}, {30, 55, 'W'}, {70, 47, 'W'},
        {10, 96, 'G'}, {85, 96, 'W'}, {100, 10, 'K'}
    };
    run(content, samples, sizeof(samples) / sizeof(samples[0]));
}

static void test_delayed_clip_stroke(void)
{
    static const char *content =
        "q 1 0 0 RG 10 w 20 20 40 40 re W S "
        "0 0 1 rg 0 0 120 120 re f Q";
    static const expected samples[] = {
        {17, 40, 'R'}, {40, 40, 'B'}, {75, 40, 'W'}
    };
    run(content, samples, sizeof(samples) / sizeof(samples[0]));
}

static void test_clip_definition_ctm_and_page_map(void)
{
    static const char *content =
        "q 1 0 0 1 10 0 cm 0 0 60 60 re W n "
        "1 0 0 1 -10 0 cm 0 0 1 rg 0 0 120 120 re f Q";
    pdf_source(content, 90, 2);
    pdf_lower();
    // 100 by 100 source CropBox at UserUnit 2 and Rotate 90.
    static const expected samples[] = {
        {60, 120, 'B'}, {160, 160, 'W'}, {30, 50, 'W'}
    };
    compare(samples, sizeof(samples)/sizeof(samples[0]), 200, 200);
    CHECK(std::remove(CLIP_SOURCE_PDF) == 0);
    CHECK(std::remove(CLIP_LOWERED_PDF) == 0);
}

int main()
{
    test_nested_q_q_and_evenodd();
    test_delayed_clip_stroke();
    test_clip_definition_ctm_and_page_map();
    return 0;
}
