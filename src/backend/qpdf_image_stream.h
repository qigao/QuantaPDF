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

// Filters and predictors are resolved before consumers inspect sample values.
image_samples qpdf_read_image_samples(QPDFObjectHandle image, unsigned channels,
    bool indexed, size_t pixel_limit);

struct device_image_samples {
    std::vector<unsigned char> pixels;
    bool wide;
};

// Device samples are tightly packed, with a big-endian pair for each wide sample.
device_image_samples qpdf_normalize_device_samples(image_samples&& samples);

} // namespace quantapdf::detail

#endif
