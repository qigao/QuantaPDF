#ifndef QUANTAPDF_NATIVE_IMAGE_DEPTH_FIXTURE_H
#define QUANTAPDF_NATIVE_IMAGE_DEPTH_FIXTURE_H

#include "native_content_stream_fixture.h"
#include <array>

namespace native_image_depth {
using O = QPDFObjectHandle;
enum class storage { raw, flate, tiff, png };
enum class fault { none, short_rows, predictor_width, decode_length, depth };
inline constexpr std::array<int, 6u> identity{0, 16, 0, 16, 0, 16};

struct specimen {
    unsigned width = 3u, height = 2u, bits = 8u, channels = 1u;
    std::vector<unsigned> samples;
    std::array<int, 6u> decode = identity;
    storage encoding = storage::raw;
    bool interpolate = false;
    int rotation = 0;
};

inline specimen grid(unsigned bits, unsigned channels, storage encoding)
{
    specimen image;
    image.bits = bits;
    image.channels = channels;
    image.encoding = encoding;
    const unsigned max = (1u << bits) - 1u;
    const std::array values = {0u, max, max / 2u, 1u, max - 1u, max / 3u, max * 3u / 4u};
    for (size_t i = 0u; i < image.width * image.height * channels; ++i)
        image.samples.push_back(values[i % values.size()]);
    if (encoding == storage::tiff) image.decode = {16, 0, 0, 16, 16, 0};
    if (encoding == storage::png) image.decode = {-2, 18, 13, 3, 4, 12};
    if (bits == 16u && channels == 3u) image.rotation = 90;
    return image;
}

inline std::string pack_rows(const specimen& image)
{
    const size_t row_samples = image.width * image.channels;
    const size_t row_bits = row_samples * image.bits;
    const size_t row_bytes = (row_bits + 7u) / 8u;
    const unsigned max = (1u << image.bits) - 1u;
    std::string packed(row_bytes * image.height, '\0');
    for (size_t y = 0u; y < image.height; ++y) {
        for (size_t i = 0u; i < row_samples; ++i) {
            const size_t position = y * row_samples + i;
            unsigned sample = image.samples[position];
            if (image.encoding == storage::tiff && i >= image.channels)
                sample = (sample - image.samples[position - image.channels]) & max;
            const size_t bit = i * image.bits;
            const size_t offset = y * row_bytes + bit / 8u;
            if (image.bits == 16u) {
                packed[offset] = static_cast<char>(sample >> 8u);
                packed[offset + 1u] = static_cast<char>(sample & 255u);
            } else {
                const unsigned shift = static_cast<unsigned>(8u - image.bits - bit % 8u);
                const unsigned old = static_cast<unsigned char>(packed[offset]);
                packed[offset] = static_cast<char>(old | (sample << shift));
            }
        }
        // The spare bits deliberately have a different value from the next row's samples.
        const unsigned spare = static_cast<unsigned>(row_bytes * 8u - row_bits);
        auto& last = packed[(y + 1u) * row_bytes - 1u];
        last = static_cast<char>(static_cast<unsigned char>(last) | ((1u << spare) - 1u));
    }
    if (image.encoding != storage::png) return packed;

    const size_t pixel_bytes = (image.channels * image.bits + 7u) / 8u;
    std::string filtered;
    for (size_t y = 0u; y < image.height; ++y) {
        // PNG row 0 uses Sub, later rows use Up. Filter over bytes, including padding.
        filtered += y == 0u ? '\x01' : '\x02';
        for (size_t x = 0u; x < row_bytes; ++x) {
            const size_t offset = y * row_bytes + x;
            const unsigned a = static_cast<unsigned char>(packed[offset]);
            const unsigned b = y != 0u ? static_cast<unsigned char>(packed[offset - row_bytes]) :
                x >= pixel_bytes ? static_cast<unsigned char>(packed[offset - pixel_bytes]) : 0u;
            filtered += static_cast<char>((a - b) & 255u);
        }
    }
    return filtered;
}

inline std::string create(const specimen& image, fault invalid = fault::none,
    const char *media = "[0 0 3 2]", const char *content = "q 3 0 0 2 0 0 cm /I Do Q")
{
    QPDF pdf;
    pdf.emptyPDF();
    std::string rows = pack_rows(image);
    if (invalid == fault::short_rows) rows.pop_back();
    auto stream = pdf.newStream();
    auto filter = O::newNull(), parms = O::newNull();
    if (image.encoding != storage::raw) {
        filter = O::newName("/FlateDecode");
        if (image.encoding == storage::tiff || image.encoding == storage::png) {
            parms = O::newDictionary();
            parms.replaceKey("/Predictor", O::newInteger(image.encoding == storage::tiff ? 2 : 15));
            parms.replaceKey("/Colors", O::newInteger(image.channels));
            parms.replaceKey("/Columns", O::newInteger(image.width + unsigned(invalid == fault::predictor_width)));
            parms.replaceKey("/BitsPerComponent", O::newInteger(image.bits));
            if (image.encoding == storage::png) {
                filter = O::newArray({filter});
                parms = O::newArray({parms});
            }
        }
        rows = native_content_fixture::flate(rows);
    }
    stream.replaceStreamData(rows, filter, parms);
    stream.setFilterOnWrite(false);
    auto dict = stream.getDict();
    dict.replaceKey("/Type", O::newName("/XObject"));
    dict.replaceKey("/Subtype", O::newName("/Image"));
    dict.replaceKey("/Width", O::newInteger(image.width));
    dict.replaceKey("/Height", O::newInteger(image.height));
    dict.replaceKey("/BitsPerComponent", O::newInteger(invalid == fault::depth ? 3u : image.bits));
    dict.replaceKey("/ColorSpace", O::newName(image.channels == 1u ? "/DeviceGray" : "/DeviceRGB"));
    if (image.interpolate) dict.replaceKey("/Interpolate", O::newBool(true));
    if (image.decode != identity || invalid == fault::decode_length) {
        std::vector<O> decode;
        for (size_t c = 0u; c < image.channels * 2u; ++c)
            decode.push_back(O::parse(std::to_string(image.decode[c] / 16.0)));
        if (invalid == fault::decode_length) decode.pop_back();
        dict.replaceKey("/Decode", O::newArray(decode));
    }

    auto objects = O::newDictionary(), resources = O::newDictionary();
    objects.replaceKey("/I", stream);
    resources.replaceKey("/XObject", objects);
    auto page = pdf.makeIndirectObject(O::parse("<< /Type /Page >>"));
    page.replaceKey("/MediaBox", O::parse(media));
    page.replaceKey("/Rotate", O::newInteger(image.rotation));
    page.replaceKey("/Contents", pdf.newStream(content));
    page.replaceKey("/Resources", resources);
    pdf.addPage(page, false);
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.setMinimumPDFVersion("1.5");
    writer.setStreamDataMode(qpdf_s_preserve);
    writer.setCompressStreams(false);
    writer.write();
    const auto bytes = writer.getBufferSharedPointer();
    return {reinterpret_cast<const char*>(bytes->getBuffer()), bytes->getSize()};
}

inline std::vector<unsigned char> rgb(const specimen& image, bool rotated)
{
    std::vector<unsigned char> expected(image.width * image.height * 3u);
    const int64_t peak = (1u << image.bits) - 1u;
    const int64_t denominator = 16 * peak;
    for (size_t y = 0u; y < image.height; ++y) {
        for (size_t x = 0u; x < image.width; ++x) {
            const size_t src = (y * image.width + x) * image.channels;
            const size_t dst = rotated ? x * image.height + image.height - 1u - y : y * image.width + x;
            for (size_t c = 0u; c < 3u; ++c) {
                const size_t lane = image.channels == 1u ? 0u : c;
                const int64_t sample = image.samples[src + lane];
                const int64_t value = image.decode[2u * lane] * (peak - sample) + image.decode[2u * lane + 1u] * sample;
                const int64_t clipped = std::clamp<int64_t>(value, 0, denominator);
                expected[dst * 3u + c] = static_cast<unsigned char>((255 * clipped + denominator / 2) / denominator);
            }
        }
    }
    return expected;
}
} // namespace native_image_depth
#endif
