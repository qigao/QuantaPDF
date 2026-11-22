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
    std::array<double, 8u> decode{};

    unsigned raw(size_t x, size_t y, unsigned channel) const;
    double decoded(size_t x, size_t y, unsigned channel) const;
    unsigned value(size_t x, size_t y, unsigned channel, unsigned maximum) const;
    std::array<double, 3u> cmyk_rgb(size_t x, size_t y) const;
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
    size_t pixel_limit, bool resolve_names);

std::vector<unsigned> qpdf_read_color_key(QPDFObjectHandle mask, const image_samples& samples);
bool image_sample_is_masked(const image_samples& samples, const std::vector<unsigned>& key, size_t x, size_t y);

struct image_alpha_plane {
    std::vector<unsigned char> bytes;
    uint32_t width = 0, height = 0;
    bool interpolate = false;
    unsigned bits = 0; // 1 for a stencil expanded to bytes, 8 or 16 for soft alpha.
};

image_alpha_plane qpdf_read_stencil(QPDFObjectHandle mask, size_t pixel_limit, size_t color_bytes);
image_alpha_plane qpdf_read_soft_mask(QPDFObjectHandle mask, size_t pixel_limit, size_t color_bytes, bool allow_matte);

struct device_image_samples {
    std::vector<unsigned char> pixels;
    image_alpha_plane alpha;
    bool wide;
    int components;
};

// Device samples are tightly packed, with a big-endian pair for each wide sample.
device_image_samples qpdf_normalize_image_colors(image_colors&& colors, QPDFObjectHandle image_dict, size_t mask_pixel_limit);

} // namespace quantapdf::detail

#endif
