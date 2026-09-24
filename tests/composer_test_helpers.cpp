#include "composer_test_helpers.h"

#include "backend/jpeg_encoder.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Pl_Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <locale>
#include <memory>
#include <string>
#include <vector>

namespace {

void append_u32(std::vector<unsigned char>& data, unsigned long value)
{
    data.push_back(static_cast<unsigned char>((value >> 24u) & 0xffu));
    data.push_back(static_cast<unsigned char>((value >> 16u) & 0xffu));
    data.push_back(static_cast<unsigned char>((value >> 8u) & 0xffu));
    data.push_back(static_cast<unsigned char>(value & 0xffu));
}

void append_chunk(
    std::vector<unsigned char>& png,
    char const type[4],
    unsigned char const* payload,
    size_t size)
{
    append_u32(png, static_cast<unsigned long>(size));
    size_t const crc_start = png.size();
    png.insert(png.end(), type, type + 4);
    if (size != 0u)
        png.insert(png.end(), payload, payload + size);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, png.data() + crc_start, static_cast<uInt>(size + 4u));
    append_u32(png, crc);
}

std::string page_content(
    unsigned char const* data,
    size_t size,
    size_t page_index)
{
    QPDF pdf;
    pdf.processMemoryFile(
        "composer-test.pdf", reinterpret_cast<char const*>(data), size);
    auto pages = pdf.getAllPages();
    if (page_index >= pages.size())
        return {};
    auto contents = pages[page_index].getKey("/Contents");
    if (contents.isStream()) {
        auto buffer = contents.getStreamData(qpdf_dl_all);
        return std::string(
            reinterpret_cast<char const*>(buffer->getBuffer()),
            buffer->getSize());
    }
    std::string result;
    if (contents.isArray()) {
        for (int i = 0; i < contents.getArrayNItems(); ++i) {
            auto buffer = contents.getArrayItem(i).getStreamData(qpdf_dl_all);
            result.append(
                reinterpret_cast<char const*>(buffer->getBuffer()),
                buffer->getSize());
        }
    }
    return result;
}

class comma_numpunct final : public std::numpunct<char> {
  protected:
    char do_decimal_point() const override
    {
        return ',';
    }
};

} // namespace

extern "C" int quantapdf_test_make_jpeg(
    unsigned char** out_data,
    size_t* out_size)
{
    unsigned char samples[8u * 4u * 3u];
    Pl_Buffer sink("composer-test-jpeg");
    std::unique_ptr<quantapdf::detail::jpeg_encoder> encoder;

    if (out_data == nullptr || out_size == nullptr)
        return 0;
    *out_data = nullptr;
    *out_size = 0u;
    for (size_t y = 0u; y < 4u; ++y) {
        for (size_t x = 0u; x < 8u; ++x) {
            size_t offset = (y * 8u + x) * 3u;
            samples[offset] = static_cast<unsigned char>(x * 32u);
            samples[offset + 1u] = static_cast<unsigned char>(y * 64u);
            samples[offset + 2u] = 32u;
        }
    }
    if (quantapdf::detail::jpeg_encoder::create(
            {8u, 4u, 3, 90}, &sink, &encoder) != QUANTAPDF_OK)
        return 0;
    encoder->pipeline()->write(samples, sizeof(samples));
    encoder->pipeline()->finish();
    std::unique_ptr<Buffer> buffer(sink.getBuffer());
    auto* copied = static_cast<unsigned char*>(std::malloc(buffer->getSize()));
    if (copied == nullptr)
        return 0;
    std::memcpy(copied, buffer->getBuffer(), buffer->getSize());
    *out_data = copied;
    *out_size = buffer->getSize();
    return 1;
}

extern "C" int quantapdf_test_make_png(
    int alpha,
    unsigned char** out_data,
    size_t* out_size)
{
    static unsigned char const signature[] = {
        0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au};
    unsigned char ihdr[13] = {
        0u, 0u, 0u, 2u, 0u, 0u, 0u, 1u, 8u,
        static_cast<unsigned char>(alpha ? 6u : 2u), 0u, 0u, 0u};
    unsigned char rgba_scanline[] = {
        0u, 255u, 0u, 0u, 0u, 0u, 0u, 255u, 255u};
    unsigned char rgb_scanline[] = {
        0u, 255u, 0u, 0u, 0u, 255u, 0u};
    unsigned char const* scanline = alpha ? rgba_scanline : rgb_scanline;
    uLong scanline_size = alpha ? sizeof(rgba_scanline) : sizeof(rgb_scanline);
    uLongf compressed_size = compressBound(scanline_size);
    std::vector<unsigned char> compressed(compressed_size);
    std::vector<unsigned char> png(signature, signature + sizeof(signature));

    if (out_data == nullptr || out_size == nullptr)
        return 0;
    *out_data = nullptr;
    *out_size = 0u;
    if (compress2(
            compressed.data(), &compressed_size, scanline, scanline_size,
            Z_BEST_COMPRESSION) != Z_OK)
        return 0;
    compressed.resize(compressed_size);
    append_chunk(png, "IHDR", ihdr, sizeof(ihdr));
    append_chunk(png, "IDAT", compressed.data(), compressed.size());
    append_chunk(png, "IEND", nullptr, 0u);
    auto* copied = static_cast<unsigned char*>(std::malloc(png.size()));
    if (copied == nullptr)
        return 0;
    std::memcpy(copied, png.data(), png.size());
    *out_data = copied;
    *out_size = png.size();
    return 1;
}

extern "C" int quantapdf_test_make_oversized_png(
    unsigned char** out_data,
    size_t* out_size)
{
    static unsigned char const signature[] = {
        0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au};
    unsigned char ihdr[13] = {
        0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 8u, 6u, 0u, 0u, 0u};
    unsigned char compressed[] = {0x78u, 0x9cu, 0x03u, 0x00u, 0x00u, 0x00u,
                                  0x00u, 0x01u};
    std::vector<unsigned char> png(signature, signature + sizeof(signature));

    if (out_data == nullptr || out_size == nullptr)
        return 0;
    *out_data = nullptr;
    *out_size = 0u;
    append_chunk(png, "IHDR", ihdr, sizeof(ihdr));
    append_chunk(png, "IDAT", compressed, sizeof(compressed));
    append_chunk(png, "IEND", nullptr, 0u);
    auto* copied = static_cast<unsigned char*>(std::malloc(png.size()));
    if (copied == nullptr)
        return 0;
    std::memcpy(copied, png.data(), png.size());
    *out_data = copied;
    *out_size = png.size();
    return 1;
}

extern "C" int quantapdf_test_make_truncated_jpeg(
    unsigned char const* data,
    size_t size,
    unsigned char** out_data,
    size_t* out_size)
{
    size_t offset = 2u;

    if (data == nullptr || out_data == nullptr || out_size == nullptr ||
        size < 4u)
        return 0;
    *out_data = nullptr;
    *out_size = 0u;
    while (offset + 4u <= size) {
        if (data[offset] != 0xffu) {
            ++offset;
            continue;
        }
        unsigned char marker = data[offset + 1u];
        if (marker == 0xdau) {
            size_t segment_size =
                (static_cast<size_t>(data[offset + 2u]) << 8u) |
                data[offset + 3u];
            size_t const end = offset + 2u + segment_size;
            if (segment_size < 2u || end > size)
                return 0;
            auto* copied = static_cast<unsigned char*>(std::malloc(end + 2u));
            if (copied == nullptr)
                return 0;
            std::memcpy(copied, data, end);
            copied[end] = 0xffu;
            copied[end + 1u] = 0xd9u;
            *out_data = copied;
            *out_size = end + 2u;
            return 1;
        }
        ++offset;
    }
    return 0;
}

extern "C" int quantapdf_test_make_huge_progressive_jpeg(
    unsigned char const* data,
    size_t size,
    unsigned char** out_data,
    size_t* out_size)
{
    if (data == nullptr || out_data == nullptr || out_size == nullptr ||
        size < 12u)
        return 0;
    *out_data = static_cast<unsigned char*>(std::malloc(size));
    *out_size = 0u;
    if (*out_data == nullptr)
        return 0;
    std::memcpy(*out_data, data, size);
    for (size_t offset = 2u; offset + 9u <= size; ++offset) {
        if ((*out_data)[offset] == 0xffu &&
            (*out_data)[offset + 1u] == 0xc0u) {
            (*out_data)[offset + 1u] = 0xc2u;
            (*out_data)[offset + 5u] = 0xfdu;
            (*out_data)[offset + 6u] = 0xe8u;
            (*out_data)[offset + 7u] = 0xfdu;
            (*out_data)[offset + 8u] = 0xe8u;
            *out_size = size;
            return 1;
        }
    }
    std::free(*out_data);
    *out_data = nullptr;
    return 0;
}

extern "C" int quantapdf_test_make_malformed_png(
    int variant,
    unsigned char** out_data,
    size_t* out_size)
{
    static unsigned char const signature[] = {
        0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au};
    unsigned char ihdr[13] = {
        0u, 0u, 0u, 2u, 0u, 0u, 0u, 1u, 8u, 2u, 0u, 0u, 0u};
    unsigned char scanline[] = {0u, 255u, 0u, 0u, 0u, 255u, 0u};
    unsigned char palette[] = {0u, 0u, 0u};
    unsigned char text[] = {'x'};
    uLongf compressed_size = compressBound(sizeof(scanline));
    std::vector<unsigned char> compressed(compressed_size);
    std::vector<unsigned char> png(signature, signature + sizeof(signature));

    if (out_data == nullptr || out_size == nullptr || variant < 0 ||
        variant > 6)
        return 0;
    *out_data = nullptr;
    *out_size = 0u;
    if (compress2(
            compressed.data(), &compressed_size, scanline, sizeof(scanline),
            Z_BEST_COMPRESSION) != Z_OK)
        return 0;
    compressed.resize(compressed_size);
    if (variant == 0)
        append_chunk(png, "tEXt", text, sizeof(text));
    append_chunk(png, "IHDR", ihdr, sizeof(ihdr));
    if (variant == 1 || variant == 2)
        append_chunk(png, "PLTE", palette, sizeof(palette));
    if (variant == 2)
        append_chunk(png, "PLTE", palette, sizeof(palette));
    if (variant == 3) {
        size_t const split = compressed.size() / 2u;
        append_chunk(png, "IDAT", compressed.data(), split);
        append_chunk(png, "tEXt", text, sizeof(text));
        append_chunk(
            png, "IDAT", compressed.data() + split,
            compressed.size() - split);
    } else {
        append_chunk(png, "IDAT", compressed.data(), compressed.size());
    }
    if (variant == 1)
        append_chunk(png, "PLTE", palette, sizeof(palette));
    if (variant == 4)
        append_chunk(png, "abca", text, sizeof(text));
    if (variant == 5)
        append_chunk(png, "a1Ca", text, sizeof(text));
    append_chunk(png, "IEND", nullptr, 0u);
    if (variant == 6)
        png.push_back(0u);
    auto* copied = static_cast<unsigned char*>(std::malloc(png.size()));
    if (copied == nullptr)
        return 0;
    std::memcpy(copied, png.data(), png.size());
    *out_data = copied;
    *out_size = png.size();
    return 1;
}

extern "C" int quantapdf_test_pdf_content_contains(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    char const* needle)
{
    try {
        return needle != nullptr &&
            page_content(data, size, page_index).find(needle) !=
                std::string::npos;
    } catch (...) {
        return 0;
    }
}

extern "C" int quantapdf_test_pdf_content_order(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    char const* first,
    char const* second)
{
    try {
        std::string const content = page_content(data, size, page_index);
        size_t const first_at = content.find(first == nullptr ? "" : first);
        size_t const second_at = content.find(second == nullptr ? "" : second);
        return first != nullptr && second != nullptr &&
            first_at != std::string::npos && second_at != std::string::npos &&
            first_at < second_at;
    } catch (...) {
        return 0;
    }
}

extern "C" size_t quantapdf_test_pdf_content_count(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    char const* needle)
{
    try {
        std::string const content = page_content(data, size, page_index);
        std::string const value = needle == nullptr ? "" : needle;
        size_t count = 0u;
        size_t offset = 0u;
        if (value.empty())
            return 0u;
        while ((offset = content.find(value, offset)) != std::string::npos) {
            ++count;
            offset += value.size();
        }
        return count;
    } catch (...) {
        return 0u;
    }
}

extern "C" int quantapdf_test_pdf_page_size_positive(
    unsigned char const* data,
    size_t size,
    size_t page_index)
{
    try {
        QPDF pdf;
        pdf.processMemoryFile(
            "composer-test.pdf", reinterpret_cast<char const*>(data), size);
        auto pages = pdf.getAllPages();
        if (page_index >= pages.size())
            return 0;
        auto box = pages[page_index].getKey("/MediaBox");
        return box.isArray() && box.getArrayNItems() == 4 &&
            box.getArrayItem(2).getNumericValue() > 0.0 &&
            box.getArrayItem(3).getNumericValue() > 0.0;
    } catch (...) {
        return 0;
    }
}

extern "C" int quantapdf_test_pdf_image_xobject_info(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    size_t image_id,
    int* out_components,
    int* out_has_smask)
{
    if (out_components == nullptr || out_has_smask == nullptr)
        return 0;
    *out_components = 0;
    *out_has_smask = 0;
    try {
        QPDF pdf;
        pdf.processMemoryFile(
            "composer-image-info.pdf",
            reinterpret_cast<char const*>(data),
            size);
        auto pages = pdf.getAllPages();
        if (page_index >= pages.size() || image_id == 0u)
            return 0;
        auto resources = pages[page_index].getKey("/Resources");
        auto xobjects = resources.getKey("/XObject");
        if (!xobjects.isDictionary())
            return 0;
        auto image = xobjects.getKey("/Im" + std::to_string(image_id));
        if (!image.isStream())
            return 0;
        auto color_space = image.getDict().getKey("/ColorSpace");
        if (!color_space.isName())
            return 0;
        std::string const name = color_space.getName();
        if (name == "/DeviceGray")
            *out_components = 1;
        else if (name == "/DeviceRGB")
            *out_components = 3;
        else
            return 0;
        *out_has_smask = !image.getDict().getKey("/SMask").isNull();
        return 1;
    } catch (...) {
        *out_components = 0;
        *out_has_smask = 0;
        return 0;
    }
}

extern "C" int quantapdf_test_pdf_extgstate_info(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    size_t graphics_state_id,
    double* out_fill_alpha,
    double* out_stroke_alpha,
    char* out_blend_mode,
    size_t blend_mode_capacity)
{
    if (out_fill_alpha == nullptr || out_stroke_alpha == nullptr ||
        out_blend_mode == nullptr || blend_mode_capacity == 0u)
        return 0;
    *out_fill_alpha = 0.0;
    *out_stroke_alpha = 0.0;
    out_blend_mode[0] = '\0';
    try {
        QPDF pdf;
        pdf.processMemoryFile(
            "composer-extgstate-info.pdf",
            reinterpret_cast<char const*>(data),
            size);
        auto pages = pdf.getAllPages();
        if (page_index >= pages.size() || graphics_state_id == 0u)
            return 0;
        auto resources = pages[page_index].getKey("/Resources");
        auto states = resources.getKey("/ExtGState");
        if (!states.isDictionary())
            return 0;
        auto state = states.getKey(
            "/GS" + std::to_string(graphics_state_id));
        if (!state.isDictionary())
            return 0;
        auto ca = state.getKey("/ca");
        auto CA = state.getKey("/CA");
        auto bm = state.getKey("/BM");
        if (!ca.isNumber() || !CA.isNumber() || !bm.isName())
            return 0;
        std::string const name = bm.getName();
        if (name.size() + 1u > blend_mode_capacity)
            return 0;
        *out_fill_alpha = ca.getNumericValue();
        *out_stroke_alpha = CA.getNumericValue();
        std::memcpy(
            out_blend_mode, name.c_str(), name.size() + 1u);
        return 1;
    } catch (...) {
        *out_fill_alpha = 0.0;
        *out_stroke_alpha = 0.0;
        out_blend_mode[0] = '\0';
        return 0;
    }
}

extern "C" int quantapdf_test_pdf_pattern_info(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    size_t paint_id,
    int* out_shading_type,
    int* out_function_type,
    double out_matrix[6])
{
    if (out_shading_type == nullptr || out_function_type == nullptr ||
        out_matrix == nullptr)
        return 0;
    *out_shading_type = 0;
    *out_function_type = 0;
    for (size_t i = 0u; i < 6u; ++i)
        out_matrix[i] = 0.0;
    try {
        QPDF pdf;
        pdf.processMemoryFile(
            "composer-pattern-info.pdf",
            reinterpret_cast<char const*>(data),
            size);
        auto pages = pdf.getAllPages();
        if (page_index >= pages.size() || paint_id == 0u)
            return 0;
        auto resources = pages[page_index].getKey("/Resources");
        auto patterns = resources.getKey("/Pattern");
        if (!patterns.isDictionary())
            return 0;
        auto pattern = patterns.getKey("/P" + std::to_string(paint_id));
        if (!pattern.isDictionary())
            return 0;
        auto shading = pattern.getKey("/Shading");
        auto matrix = pattern.getKey("/Matrix");
        if (!shading.isDictionary() ||
            !matrix.isArray() || matrix.getArrayNItems() != 6u)
            return 0;
        auto shading_type = shading.getKey("/ShadingType");
        auto function = shading.getKey("/Function");
        if (!shading_type.isInteger() || !function.isDictionary())
            return 0;
        auto function_type = function.getKey("/FunctionType");
        if (!function_type.isInteger())
            return 0;
        *out_shading_type =
            static_cast<int>(shading_type.getIntValue());
        *out_function_type =
            static_cast<int>(function_type.getIntValue());
        for (size_t i = 0u; i < 6u; ++i) {
            auto item = matrix.getArrayItem(static_cast<int>(i));
            if (!item.isNumber())
                return 0;
            out_matrix[i] = item.getNumericValue();
        }
        return 1;
    } catch (...) {
        *out_shading_type = 0;
        *out_function_type = 0;
        for (size_t i = 0u; i < 6u; ++i)
            out_matrix[i] = 0.0;
        return 0;
    }
}

extern "C" int quantapdf_test_pdf_form_xobject_info(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    size_t form_id,
    double out_bbox[4],
    int* out_has_resources)
{
    if (out_bbox == nullptr || out_has_resources == nullptr)
        return 0;
    for (size_t i = 0u; i < 4u; ++i)
        out_bbox[i] = 0.0;
    *out_has_resources = 0;
    try {
        QPDF pdf;
        pdf.processMemoryFile(
            "composer-form-info.pdf",
            reinterpret_cast<char const*>(data),
            size);
        auto pages = pdf.getAllPages();
        if (page_index >= pages.size() || form_id == 0u)
            return 0;
        auto resources = pages[page_index].getKey("/Resources");
        auto xobjects = resources.getKey("/XObject");
        if (!xobjects.isDictionary())
            return 0;
        auto form = xobjects.getKey("/Fm" + std::to_string(form_id));
        if (!form.isStream())
            return 0;
        auto dictionary = form.getDict();
        auto subtype = dictionary.getKey("/Subtype");
        auto bbox = dictionary.getKey("/BBox");
        if (!subtype.isName() || subtype.getName() != "/Form" ||
            !bbox.isArray() || bbox.getArrayNItems() != 4u)
            return 0;
        for (size_t i = 0u; i < 4u; ++i) {
            auto item = bbox.getArrayItem(static_cast<int>(i));
            if (!item.isNumber())
                return 0;
            out_bbox[i] = item.getNumericValue();
        }
        *out_has_resources =
            dictionary.getKey("/Resources").isDictionary() ? 1 : 0;
        return 1;
    } catch (...) {
        for (size_t i = 0u; i < 4u; ++i)
            out_bbox[i] = 0.0;
        *out_has_resources = 0;
        return 0;
    }
}

extern "C" int quantapdf_test_pdf_form_group_info(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    size_t form_id,
    int* out_has_group,
    int* out_isolated,
    int* out_knockout)
{
    if (out_has_group == nullptr || out_isolated == nullptr ||
        out_knockout == nullptr)
        return 0;
    *out_has_group = 0;
    *out_isolated = 0;
    *out_knockout = 0;
    try {
        QPDF pdf;
        pdf.processMemoryFile(
            "composer-form-group-info.pdf",
            reinterpret_cast<char const*>(data),
            size);
        auto pages = pdf.getAllPages();
        if (page_index >= pages.size() || form_id == 0u)
            return 0;
        auto xobjects =
            pages[page_index].getKey("/Resources").getKey("/XObject");
        if (!xobjects.isDictionary())
            return 0;
        auto form = xobjects.getKey("/Fm" + std::to_string(form_id));
        if (!form.isStream())
            return 0;
        auto group = form.getDict().getKey("/Group");
        if (group.isNull())
            return 1;
        if (!group.isDictionary())
            return 0;
        auto subtype = group.getKey("/S");
        auto color_space = group.getKey("/CS");
        auto isolated = group.getKey("/I");
        auto knockout = group.getKey("/K");
        if (!subtype.isName() || subtype.getName() != "/Transparency" ||
            !color_space.isName() || color_space.getName() != "/DeviceRGB" ||
            !isolated.isBool() || !knockout.isBool())
            return 0;
        *out_has_group = 1;
        *out_isolated = isolated.getBoolValue() ? 1 : 0;
        *out_knockout = knockout.getBoolValue() ? 1 : 0;
        return 1;
    } catch (...) {
        *out_has_group = 0;
        *out_isolated = 0;
        *out_knockout = 0;
        return 0;
    }
}

extern "C" int quantapdf_test_pdf_tiling_pattern_info(
    unsigned char const* data,
    size_t size,
    size_t page_index,
    size_t paint_id,
    double out_bbox[4],
    double* out_x_step,
    double* out_y_step,
    double out_matrix[6],
    int* out_has_tile_form)
{
    if (out_bbox == nullptr || out_x_step == nullptr ||
        out_y_step == nullptr || out_matrix == nullptr ||
        out_has_tile_form == nullptr)
        return 0;
    for (size_t i = 0u; i < 4u; ++i)
        out_bbox[i] = 0.0;
    for (size_t i = 0u; i < 6u; ++i)
        out_matrix[i] = 0.0;
    *out_x_step = 0.0;
    *out_y_step = 0.0;
    *out_has_tile_form = 0;
    try {
        QPDF pdf;
        pdf.processMemoryFile(
            "composer-tiling-pattern-info.pdf",
            reinterpret_cast<char const*>(data),
            size);
        auto pages = pdf.getAllPages();
        if (page_index >= pages.size() || paint_id == 0u)
            return 0;
        auto resources = pages[page_index].getKey("/Resources");
        auto patterns = resources.getKey("/Pattern");
        if (!patterns.isDictionary())
            return 0;
        auto pattern = patterns.getKey("/P" + std::to_string(paint_id));
        if (!pattern.isStream())
            return 0;
        auto dictionary = pattern.getDict();
        auto pattern_type = dictionary.getKey("/PatternType");
        auto paint_type = dictionary.getKey("/PaintType");
        auto bbox = dictionary.getKey("/BBox");
        auto x_step = dictionary.getKey("/XStep");
        auto y_step = dictionary.getKey("/YStep");
        auto matrix = dictionary.getKey("/Matrix");
        auto pattern_resources = dictionary.getKey("/Resources");
        if (!pattern_type.isInteger() || pattern_type.getIntValue() != 1 ||
            !paint_type.isInteger() || paint_type.getIntValue() != 1 ||
            !bbox.isArray() || bbox.getArrayNItems() != 4u ||
            !x_step.isNumber() || !y_step.isNumber() ||
            !matrix.isArray() || matrix.getArrayNItems() != 6u ||
            !pattern_resources.isDictionary())
            return 0;
        for (size_t i = 0u; i < 4u; ++i) {
            auto item = bbox.getArrayItem(static_cast<int>(i));
            if (!item.isNumber())
                return 0;
            out_bbox[i] = item.getNumericValue();
        }
        for (size_t i = 0u; i < 6u; ++i) {
            auto item = matrix.getArrayItem(static_cast<int>(i));
            if (!item.isNumber())
                return 0;
            out_matrix[i] = item.getNumericValue();
        }
        *out_x_step = x_step.getNumericValue();
        *out_y_step = y_step.getNumericValue();
        auto tile = pattern_resources.getKey("/XObject").getKey("/Tile");
        if (tile.isStream()) {
            auto subtype = tile.getDict().getKey("/Subtype");
            if (subtype.isName() && subtype.getName() == "/Form")
                *out_has_tile_form = 1;
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" void quantapdf_test_use_comma_locale(int enabled)
{
    std::locale::global(enabled
            ? std::locale(std::locale::classic(), new comma_numpunct())
            : std::locale::classic());
}
