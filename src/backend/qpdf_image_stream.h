#ifndef QUANTAPDF_BACKEND_QPDF_IMAGE_STREAM_H
#define QUANTAPDF_BACKEND_QPDF_IMAGE_STREAM_H

#include <quantapdf/quantapdf.h>
#include <qpdf/QPDFObjectHandle.hh>
#include <array>
#include <vector>

namespace quantapdf::detail {

struct image_stream_error { quantapdf_status status; };

std::vector<unsigned char> qpdf_read_stream(QPDFObjectHandle stream,
    size_t limit, qpdf_stream_decode_level_e level);

struct image_samples {
    uint32_t width = 0, height = 0;
    unsigned channels = 0, bits = 0;
    size_t row_bytes = 0;
    std::vector<unsigned char> bytes;
    std::array<double, 6u> decode{};

    unsigned raw(size_t x, size_t y, unsigned channel) const;
    double decoded(size_t x, size_t y, unsigned channel) const;
    unsigned value(size_t x, size_t y, unsigned channel, unsigned maximum) const;
};

struct image_colors {
    image_samples samples;
    unsigned high = 0, palette_channels = 0;
    std::vector<unsigned char> palette;

    bool indexed() const { return palette_channels != 0u; }
    const unsigned char *indexed_color(size_t x, size_t y) const;
    std::array<unsigned char, 3u> rgb(size_t x, size_t y) const;
};

// XObject color spaces are explicit PDF objects. Extraction also accepts resource names.
image_colors qpdf_read_image_colors(QPDFObjectHandle image, QPDFObjectHandle resources,
    size_t pixel_limit);

struct device_image_samples {
    std::vector<unsigned char> pixels;
    bool wide;
};

// Device samples are tightly packed, with a big-endian pair for each wide sample.
device_image_samples qpdf_normalize_image_colors(image_colors&& colors);

} // namespace quantapdf::detail

#endif
