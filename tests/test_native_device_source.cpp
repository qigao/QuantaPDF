/* M1: unchanged literal source PDF -> owned Graph -> detached IR + device
 * plan -> Native. PDFium is an independent original-source FIT oracle.
 * The expected grid and 4x4 coverage below use fixture integer coordinates,
 * never a Graph value, production affine, or Native-rendered reference. */
#include "document/page_native_ir.h"
#include "graph/graph_page_geometry.h"
#include "render/ir_scoped_operations_v1.h"

#include <fpdfview.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, \
    "%s:%d: native device source: %s\n", __FILE__, __LINE__, #x); \
    std::exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

namespace {
const quantapdf_graph_limits graph_limits = {
    10000u, 30000u, 1048576u, 1048576u, 4194304u, 256u
};
const quantapdf_graph_stream_limits decode_limits = {8192u, 128u, 100000u};
const quantapdf_pdf_content_limits content_limits = {8192u, 256u, 16u};
const quantapdf_pdf_path_lower_limits lower_limits = {4u, 256u};
const quantapdf_ir_raster_v1_limits raster_limits = {
    1000000u, 4u, 4096u, 320000000u
};

struct fixture {
    const char *name;
    int x, y, width, height, step; /* exact source hundredths */
    const char *unit;
    unsigned unit_num, unit_den;
    int rotation;
    bool inherited;
};

std::string decimal(int value)
{
    CHECK(value >= 0);
    char text[32];
    CHECK(std::snprintf(text, sizeof(text), "%d.%02d", value / 100,
                        value % 100) > 0);
    return text;
}

std::string crop(const fixture& f)
{
    return decimal(f.x) + " " + decimal(f.y) + " " +
           decimal(f.x + f.width) + " " + decimal(f.y + f.height);
}

std::string rectangle(int x, int y, int width, int height)
{
    return decimal(x) + " " + decimal(y) + " " + decimal(width) +
           " " + decimal(height) + " re ";
}

std::string content(const fixture& f, int stroke_position)
{
    const std::string stroke = "0 G 1 w " +
        rectangle(f.x + f.step, f.y + f.step, 6 * f.step, 4 * f.step) +
        "S\n";
    std::string result = stroke_position == 1 ? stroke : "";
    result += "q " +
        rectangle(f.x + f.step, f.y + f.step, 6 * f.step, 4 * f.step) +
        "W n 1 0 0 rg " +
        rectangle(f.x, f.y, 6 * f.step, 6 * f.step) + "f Q\n";
    if (stroke_position == 2) result += stroke;
    return result;
}

/* Literal original bytes preserve decimal tokens and a valid xref. No QPDF
 * writer, Composer export, or Native geometry is used to create the oracle. */
std::string make_pdf(const fixture& f, int stroke_position = 0)
{
    std::string parent = "<< /Type /Pages /Kids [3 0 R] /Count 1 "
        "/MediaBox [0 0 40 40] /UserUnit 64 ";
    std::string page = "<< /Type /Page /Parent 2 0 R /Resources << >> "
        "/Contents 4 0 R ";
    const std::string selected = "/CropBox [" + crop(f) + "] /Rotate " +
                                 std::to_string(f.rotation) + " ";
    if (f.inherited) parent += selected;
    else {
        /* A valid but different ancestor must not override the leaf. */
        parent += "/CropBox [0 0 39 39] /Rotate 90 ";
        page += selected;
    }
    if (f.unit) page += std::string("/UserUnit ") + f.unit + " ";
    parent += ">>";
    page += ">>";
    const std::string stream = content(f, stroke_position);
    const std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>", parent, page,
        "<< /Length " + std::to_string(stream.size()) + ">>\nstream\n" +
            stream + "endstream"
    };
    std::string pdf = "%PDF-1.6\n";
    std::vector<size_t> offsets(1u, 0u);
    for (size_t i = 0u; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1u) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 5\n0000000000 65535 f \n";
    for (size_t i = 1u; i < offsets.size(); ++i) {
        char entry[40];
        CHECK(std::snprintf(entry, sizeof(entry), "%010lu 00000 n \n",
                            (unsigned long)offsets[i]) > 0);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

void write_file(const std::string& path, const std::string& bytes)
{
    FILE *file = std::fopen(path.c_str(), "wb");
    CHECK(file && std::fwrite(bytes.data(), 1u, bytes.size(), file) == bytes.size());
    CHECK(std::fclose(file) == 0);
}

std::string read_file(const char *path)
{
    FILE *file = std::fopen(path, "rb");
    CHECK(file);
    char bytes[16385];
    const size_t count = std::fread(bytes, 1u, sizeof(bytes), file);
    CHECK(!std::ferror(file) && count < sizeof(bytes));
    CHECK(std::fclose(file) == 0);
    return std::string(bytes, count);
}

bool all_zero(const void *value, size_t size)
{
    const auto *bytes = static_cast<const unsigned char *>(value);
    for (size_t i = 0u; i < size; ++i) if (bytes[i] != 0u) return false;
    return true;
}

quantapdf_status lower(const char *path, unsigned dpi, size_t max_pixels,
    quantapdf_ir *ir, quantapdf_native_device_plan *plan,
    quantapdf_graph_page_source_numbers *numbers = nullptr)
{
    quantapdf_document *document = nullptr;
    quantapdf_graph_snapshot *snapshot = nullptr;
    OK(quantapdf_open(path, nullptr, &document));
    OK(quantapdf_document_capture_graph(document, &graph_limits, &snapshot));
    quantapdf_close(document);
    if (numbers) {
        OK(quantapdf_graph_query_page_source_numbers(snapshot->graph, 0u,
                                                     2u, numbers));
        quantapdf_graph_page_source_numbers short_walk;
        std::memset(&short_walk, 0xa5, sizeof(short_walk));
        CHECK(quantapdf_graph_query_page_source_numbers(snapshot->graph,
                  0u, 1u, &short_walk) == QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(all_zero(&short_walk, sizeof(short_walk)));
    }
    const quantapdf_status status = quantapdf_graph_snapshot_page_native_ir(
        snapshot, 0u, 2u, &decode_limits, &content_limits, 1u,
        &lower_limits, dpi, max_pixels, ir, plan);
    /* All subsequent plan/IR inspections and raster calls occur after both
     * source and Graph ownership have been destroyed. */
    quantapdf_graph_snapshot_drop(snapshot);
    return status;
}

size_t grid_dimension(unsigned hundredths, const fixture& f, unsigned dpi)
{
    const uint64_t numerator = (uint64_t)hundredths * f.unit_num * dpi;
    const uint64_t denominator = (uint64_t)100u * f.unit_den * 72u;
    return (size_t)(numerator / denominator + (numerator % denominator != 0u));
}

struct rational_rect { int x0, x1, y0, y1, dx, dy; };

/* Independent clockwise-viewed rectangle bounds in normalized device
 * coordinates. Unit cancels in FIT; it affects only the integer grid. */
rational_rect painted_rectangle(const fixture& f)
{
    const int a = f.step, b = 6 * f.step, c = f.step, d = 5 * f.step;
    switch (f.rotation) {
    case 0: return {a, b, f.height - d, f.height - c, f.width, f.height};
    case 90: return {c, d, a, b, f.height, f.width};
    case 180: return {f.width - b, f.width - a, c, d, f.width, f.height};
    case 270: return {f.height - d, f.height - c,
                      f.width - b, f.width - a, f.height, f.width};
    default: CHECK(false); return {};
    }
}

struct sample_bounds { unsigned strict, inclusive; };

/* Composer IR retains float coordinates. If an exact source edge passes
 * THROUGH a sample center, that representation may place it on either side.
 * Derive this ambiguity by integer equality only: no epsilon or measured
 * residual. Away from exact ties, strict == inclusive and RGBA is exact.
 * Source-exact tie canonicalization would require a separate IR contract. */
sample_bounds covered_samples(size_t x, size_t y, size_t width, size_t height,
                              const rational_rect& r)
{
    sample_bounds count{};
    for (unsigned sy = 0u; sy < 4u; ++sy) {
        const int64_t py = (int64_t)(8u * y + 2u * sy + 1u) * r.dy;
        for (unsigned sx = 0u; sx < 4u; ++sx) {
            const int64_t px = (int64_t)(8u * x + 2u * sx + 1u) * r.dx;
            count.strict += px > (int64_t)8 * r.x0 * (int64_t)width &&
                            px < (int64_t)8 * r.x1 * (int64_t)width &&
                            py > (int64_t)8 * r.y0 * (int64_t)height &&
                            py < (int64_t)8 * r.y1 * (int64_t)height;
            count.inclusive += px >= (int64_t)8 * r.x0 * (int64_t)width &&
                               px <= (int64_t)8 * r.x1 * (int64_t)width &&
                               py >= (int64_t)8 * r.y0 * (int64_t)height &&
                               py <= (int64_t)8 * r.y1 * (int64_t)height;
        }
    }
    return count;
}

std::vector<unsigned char> pdfium_fit(const char *path, size_t width, size_t height)
{
    CHECK(width && height && width * height <= 1000000u);
    FPDF_DOCUMENT document = FPDF_LoadDocument(path, nullptr);
    CHECK(document && FPDF_GetPageCount(document) == 1);
    FPDF_PAGE page = FPDF_LoadPage(document, 0);
    CHECK(page);
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx((int)width, (int)height,
                                             FPDFBitmap_BGR, nullptr, 0);
    CHECK(bitmap);
    FPDFBitmap_FillRect(bitmap, 0, 0, (int)width, (int)height, 0xffffffffu);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, (int)width, (int)height, 0, FPDF_ANNOT);
    const auto *src = static_cast<const unsigned char *>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    CHECK(src && stride >= (int)(width * 3u));
    std::vector<unsigned char> rgb(width * height * 3u);
    for (size_t y = 0u; y < height; ++y)
        for (size_t x = 0u; x < width; ++x)
            for (size_t c = 0u; c < 3u; ++c)
                rgb[(y * width + x) * 3u + c] = src[y * (size_t)stride + x * 3u + 2u - c];
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(document);
    return rgb;
}

unsigned on_white(const unsigned char *rgba, size_t c)
{
    return ((unsigned)rgba[c] * rgba[3] + 255u * (255u - rgba[3]) + 127u) / 255u;
}

/* PDFium chromium/8021 (784a524ddaa26d86c0f499625b97902095c26dfe)
 * CFX_RenderDevice::DrawPath has an integer FillRect fast path, and the AGG
 * rectangle Clip uses GetOuterRect/IntersectRect. It need not antialias a
 * fractional rectangle, so a universal 128/channel edge cap is invalid:
 * an opaque reference corner can differ by 223 from Native's 2/16 coverage.
 *
 * Admit differences ONLY in pixels whose CLOSED unit square touches the
 * independently derived rectangle boundary. Including exact contact accounts
 * for PDFium's float transform moving an integer Clip edge outward before
 * rounding (the translated 6.10 control exercises this). Pixels strictly
 * inside/outside that footprint and every red channel are exact.
 * The per-channel boundary envelope is max(A,255-A), where
 * A is the independent Native coverage (the endpoint envelope for an exact
 * sample tie), allowing PDFium to snap either inward or outward. Its sum is
 * the full-RGB L1 budget. No observed
 * residual or renderer-produced geometry sets these bounds; Native RGBA
 * must satisfy the independent integer sample bounds at EVERY pixel. */
bool boundary_pixel(size_t x, size_t y, size_t width, size_t height,
                    const rational_rect& r)
{
    const int64_t x0 = (int64_t)x * r.dx, x1 = (int64_t)(x + 1u) * r.dx;
    const int64_t y0 = (int64_t)y * r.dy, y1 = (int64_t)(y + 1u) * r.dy;
    const int64_t left = (int64_t)r.x0 * (int64_t)width;
    const int64_t right = (int64_t)r.x1 * (int64_t)width;
    const int64_t top = (int64_t)r.y0 * (int64_t)height;
    const int64_t bottom = (int64_t)r.y1 * (int64_t)height;
    const bool outside = x1 < left || x0 > right || y1 < top || y0 > bottom;
    const bool inside = x0 > left && x1 < right && y0 > top && y1 < bottom;
    return !outside && !inside;
}

void compare(const fixture& f, unsigned dpi, const char *path,
             size_t width, size_t height, const quantapdf_ir_raster_image& native)
{
    const rational_rect rect = painted_rectangle(f);
    if (std::strcmp(f.name, "non-dyadic-cancellation") == 0 && dpi == 96u) {
        /* Source top edge is exactly 28.125 device pixels: the retained
         * float IR can place its tied sample row on either side. */
        const sample_bounds regression = covered_samples(9u, 28u, width, height, rect);
        CHECK(width == 94u && height == 75u &&
              regression.strict == 6u && regression.inclusive == 8u);
    }
    const auto reference = pdfium_fit(path, width, height);
    CHECK(reference == pdfium_fit(path, width, height));
    size_t red = 0u, white = 0u, band = 0u, different = 0u, l1 = 0u, l1_budget = 0u;
    size_t tie_pixels = 0u, tie_samples = 0u;
    unsigned max_delta = 0u;
    for (size_t y = 0u; y < height; ++y) {
        for (size_t x = 0u; x < width; ++x) {
            const size_t pixel = y * width + x;
            const unsigned char *rgba = native.pixels + 4u * pixel;
            const sample_bounds samples = covered_samples(x, y, width, height, rect);
            bool alpha_allowed = false;
            for (unsigned n = samples.strict; n <= samples.inclusive; ++n)
                alpha_allowed |= rgba[3] == (n * 255u + 8u) / 16u;
            CHECK(alpha_allowed);
            CHECK(rgba[0] == (rgba[3] ? 255u : 0u) && rgba[1] == 0u && rgba[2] == 0u);
            red += samples.strict == 16u;
            white += samples.inclusive == 0u;
            tie_pixels += samples.strict != samples.inclusive;
            tie_samples += samples.inclusive - samples.strict;
            const bool edge = boundary_pixel(x, y, width, height, rect);
            const unsigned low_alpha = (samples.strict * 255u + 8u) / 16u;
            const unsigned high_alpha = (samples.inclusive * 255u + 8u) / 16u;
            const unsigned envelope = edge ? std::max(high_alpha, 255u - low_alpha) : 0u;
            band += edge;
            l1_budget += 2u * envelope;
            CHECK(reference[3u * pixel + 1u] == reference[3u * pixel + 2u]);
            unsigned largest = 0u;
            for (size_t c = 0u; c < 3u; ++c) {
                const unsigned a = on_white(rgba, c), b = reference[3u * pixel + c];
                const unsigned delta = a > b ? a - b : b - a;
                if ((!edge || c == 0u) && delta != 0u)
                    std::fprintf(stderr, "%s dpi=%u pixel(%zu,%zu) channel=%zu native=%u PDFium=%u samples=[%u,%u] boundary=%d\n",
                        f.name, dpi, x, y, c, a, b, samples.strict, samples.inclusive, (int)edge);
                if (!edge || c == 0u) CHECK(delta == 0u);
                CHECK(delta <= envelope);
                largest = std::max(largest, delta);
                l1 += delta;
            }
            different += largest != 0u;
            max_delta = std::max(max_delta, largest);
        }
    }
    std::printf("M1 %s dpi=%u %zux%zu: full RGB different=%zu L1=%zu/%zu max=%u boundary_pixels=%zu; 4x4 source sample bounds passed, exact-source ties=%zu pixels/%zu samples\n",
                f.name, dpi, width, height, different, l1, l1_budget, max_delta, band,
                tie_pixels, tie_samples);
    CHECK(red > 0u && white > 0u && different <= band);
    CHECK(l1 <= l1_budget);
}

void positive(const fixture& f, const std::string& path, unsigned dpi)
{
    const std::string original = make_pdf(f);
    write_file(path, original);
    quantapdf_ir ir{};
    quantapdf_native_device_plan plan{};
    quantapdf_graph_page_source_numbers numbers{};
    OK(lower(path.c_str(), dpi, 1000000u, &ir, &plan, &numbers));
    CHECK(std::strcmp(numbers.media_box, "0 0 40 40") == 0 &&
          std::strcmp(numbers.crop_box, crop(f).c_str()) == 0 &&
          std::strcmp(numbers.user_unit, f.unit ? f.unit : "1") == 0 &&
          numbers.rotation == f.rotation);
    const bool quarter = f.rotation == 90 || f.rotation == 270;
    const size_t width = grid_dimension((unsigned)(quarter ? f.height : f.width), f, dpi);
    const size_t height = grid_dimension((unsigned)(quarter ? f.width : f.height), f, dpi);
    CHECK(plan.width == width && plan.height == height &&
          plan.stride == width * 4u && plan.size == width * height * 4u);
    CHECK(ir.page_count == 1u && ir.operation_count == 1u &&
          ir.resources.clip_count == 1u && ir.resources.graphics_state_count == 1u &&
          ir.operations[0].kind == QUANTAPDF_COMPOSER_OPERATION_PATH &&
          ir.operations[0].value.path.options.fill == 1 &&
          ir.operations[0].value.path.options.stroke == 0 &&
          ir.pages[0].suppress_background == 1);
    const quantapdf_native_device_plan before = plan;
    quantapdf_ir_raster_image first{}, second{};
    OK(quantapdf_ir_raster_scoped_operations_v1_planned(&ir, 0u,
        &raster_limits, &plan, &first));
    OK(quantapdf_ir_raster_scoped_operations_v1_planned(&ir, 0u,
        &raster_limits, &plan, &second));
    CHECK(first.pixels && second.pixels && first.width == width && first.height == height &&
          first.stride == width * 4u && first.size == width * height * 4u &&
          second.size == first.size && std::memcmp(first.pixels, second.pixels, first.size) == 0 &&
          std::memcmp(&plan, &before, sizeof(plan)) == 0);
    quantapdf_ir_dispose(&ir);
    compare(f, dpi, path.c_str(), width, height, first);
    quantapdf_ir_raster_image_dispose(&first);
    quantapdf_ir_raster_image_dispose(&second);
    CHECK(read_file(path.c_str()) == original);
    CHECK(std::remove(path.c_str()) == 0);
}

void rejections(const fixture& f, const std::string& path)
{
    for (int order = 1; order <= 2; ++order) {
        const std::string original = make_pdf(f, order);
        write_file(path, original);
        quantapdf_ir ir{};
        quantapdf_native_device_plan plan{};
        OK(lower(path.c_str(), 96u, 1000000u, &ir, &plan));
        CHECK(ir.operation_count == 2u && ir.resources.clip_count == 1u &&
              ir.resources.graphics_state_count == 1u);
        const size_t stroke = order == 1 ? 0u : 1u, fill = 1u - stroke;
        CHECK(ir.operations[stroke].value.path.options.stroke == 1 &&
              ir.operations[stroke].value.path.options.fill == 0 &&
              ir.operations[stroke].value.path.command_count == 5u &&
              ir.operations[fill].value.path.options.fill == 1);
        for (unsigned repeat = 0u; repeat < 2u; ++repeat) {
            quantapdf_ir_raster_image image;
            std::memset(&image, 0xa5, sizeof(image));
            CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
                &ir, 0u, &raster_limits, &plan, &image) == QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(all_zero(&image, sizeof(image)));
        }
        quantapdf_ir_dispose(&ir);
        CHECK(read_file(path.c_str()) == original);
    }
    /* Device budget rejection must publish neither detached IR nor plan. */
    quantapdf_ir ir;
    quantapdf_native_device_plan plan;
    std::memset(&ir, 0xa5, sizeof(ir));
    std::memset(&plan, 0xa5, sizeof(plan));
    CHECK(lower(path.c_str(), 96u, 1u, &ir, &plan) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(all_zero(&ir, sizeof(ir)) && all_zero(&plan, sizeof(plan)));
    CHECK(std::remove(path.c_str()) == 0);
}

/* Optional observation only: an external SHA-pinning caller must attest the
 * original before/after. This binary performs no network access or vendoring.
 * M1 keeps both original painted Paths and still refuses rectangular Stroke. */
void original_jspdf(const char *path)
{
    const std::string original = read_file(path);
    CHECK(original.size() == 3344u && original.find("%PDF-1.3") == 0u &&
          original.find("/MediaBox [0 0 595.28 841.89]") != std::string::npos);
    const unsigned dpis[] = {72u, 96u, 144u};
    const size_t widths[] = {596u, 794u, 1191u};
    const size_t heights[] = {842u, 1123u, 1684u};
    for (size_t i = 0u; i < 3u; ++i) {
        quantapdf_ir ir{};
        quantapdf_native_device_plan plan{};
        const quantapdf_status status = lower(path, dpis[i], 1000000u, &ir, &plan);
        if (i == 2u) {
            CHECK(status == QUANTAPDF_ERROR_UNSUPPORTED &&
                  all_zero(&ir, sizeof(ir)) && all_zero(&plan, sizeof(plan)));
        } else {
            OK(status);
            CHECK(plan.width == widths[i] && plan.height == heights[i] &&
                  ir.page_count == 1u && ir.operation_count == 2u &&
                  ir.resources.clip_count == 1u && ir.resources.graphics_state_count == 1u &&
                  ir.operations[0].kind == QUANTAPDF_COMPOSER_OPERATION_PATH &&
                  ir.operations[1].kind == QUANTAPDF_COMPOSER_OPERATION_PATH &&
                  ir.operations[0].value.path.options.stroke == 1 &&
                  ir.operations[0].value.path.options.fill == 0 &&
                  ir.operations[1].value.path.options.fill == 1 &&
                  ir.operations[1].value.path.options.stroke == 0);
            for (unsigned repeat = 0u; repeat < 2u; ++repeat) {
                quantapdf_ir_raster_image image;
                std::memset(&image, 0xa5, sizeof(image));
                CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
                    &ir, 0u, &raster_limits, &plan, &image) == QUANTAPDF_ERROR_UNSUPPORTED);
                CHECK(all_zero(&image, sizeof(image)));
            }
            quantapdf_ir_dispose(&ir);
        }
        std::printf("M1 unchanged jsPDF dpi=%u expected=%zux%zu plan=%s Native=%s; pixel_positive=false\n",
            dpis[i], widths[i], heights[i], i == 2u ? "capacity-rejected" : "admitted",
            i == 2u ? "not-invoked" : "unsupported-zero-output-twice");
    }
    CHECK(read_file(path) == original);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--quarantined-jspdf-plan") == 0) {
        original_jspdf(argv[2]);
        return EXIT_SUCCESS;
    }
    CHECK(argc == 1 || (argc == 2 && argv[1] && *argv[1]));
    const char *prefix = argc == 2 ? argv[1] : NATIVE_DEVICE_SOURCE_PDF;
    const fixture cases[] = {
        {"fractional-origin", 0, 0, 2050, 1625, 200, nullptr, 1, 1, 0, true},
        {"translated-inherited", 225, 350, 2050, 1625, 200, nullptr, 1, 1, 0, true},
        {"translated-leaf", 225, 350, 2050, 1625, 200, nullptr, 1, 1, 0, false},
        {"quarter-half-unit-inherited", 225, 350, 2050, 1625, 200, "0.50", 1, 2, 90, true},
        {"quarter-half-unit-leaf", 225, 350, 2050, 1625, 200, "0.50", 1, 2, 90, false},
        {"half-turn-fractional-unit", 225, 350, 2050, 1625, 200, "1.250", 5, 4, 180, true},
        {"three-quarter-fractional-unit", 225, 350, 2050, 1625, 200, "1.250", 5, 4, 270, false},
        {"non-dyadic-height", 225, 350, 2050, 1610, 200, nullptr, 1, 1, 0, false},
        {"non-dyadic-cancellation", 610, 337, 1000, 800, 100, "7", 7, 1, 0, false}
    };
    FPDF_InitLibrary();
    for (const auto& f : cases) {
        const std::string path = std::string(prefix) + "-" + f.name + ".pdf";
        positive(f, path, 72u);
        positive(f, path, 96u);
    }
    rejections(cases[1], std::string(prefix) + "-stroke-negative.pdf");
    FPDF_DestroyLibrary();
    std::puts("M1 source device plan: 18 source-PDF FIT frames, exact non-tie coverage and bounded source/sample ties, copied tokens, detached ownership and complete-stroke refusal passed");
    return EXIT_SUCCESS;
}
