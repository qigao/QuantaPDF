#ifndef QUANTAPDF_NATIVE_JPEG_FIXTURE_H
#define QUANTAPDF_NATIVE_JPEG_FIXTURE_H

#include "native_clip_graph_fixture.h"
#include "native_jpeg_progressive_payload.h"
#include <span>

namespace native_jpeg {

using O = QPDFObjectHandle;

inline constexpr unsigned side = 16u;
inline constexpr size_t source_pixels = side * side;

// Sequential 8-bit JPEGs with four constant 8x8 blocks per channel.
// The fixture encodes DC = 8*(sample - 128) with quantization 8 and zero AC.
// The reference colors below come from those samples and the YCbCr transform.
inline constexpr unsigned char gray_jpeg[] = {
    0xff, 0xd8, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0xff,
    0xc0, 0x00, 0x0b, 0x08, 0x00, 0x10, 0x00, 0x10, 0x01, 0x01, 0x11, 0x00,
    0xff, 0xc4, 0x00, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0x72, 0x07,
    0x88, 0x78, 0x87, 0x88, 0xff, 0xd9,
};
inline constexpr unsigned char color_jpeg[] = {
    0xff, 0xd8, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0xff,
    0xc0, 0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03, 0x52, 0x11, 0x00,
    0x47, 0x11, 0x00, 0x42, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x2e, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x0c, 0x03, 0x52,
    0x00, 0x47, 0x00, 0x42, 0x00, 0x00, 0x3f, 0x00, 0x63, 0xcb, 0x62, 0xf8,
    0xbf, 0x32, 0x67, 0x7a, 0x6f, 0x48, 0x06, 0xd9, 0x0f, 0x86, 0xb0, 0xb7,
    0x7f, 0xff, 0xd9,
};
inline constexpr unsigned char jfif_marker[] = {
    0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
};
inline constexpr unsigned char adobe_rgb_marker[] = {
    0xff, 0xee, 0x00, 0x0e, 0x41, 0x64, 0x6f, 0x62, 0x65, 0x00, 0x64, 0x00,
    0x00, 0x00, 0x00, 0x00,
};
inline constexpr unsigned char adobe_ycbcr_marker[] = {
    0xff, 0xee, 0x00, 0x0e, 0x41, 0x64, 0x6f, 0x62, 0x65, 0x00, 0x64, 0x00,
    0x00, 0x00, 0x00, 0x01,
};

enum class container { name, array, wrapped };
enum class fault { none, width, height, channels, truncated, bad_transform, nonterminal_dct };

struct specimen {
    const char *name;
    bool gray;
    bool jfif;
    int adobe;
    int transform;
    bool invert;
    std::span<const unsigned char> payload;
};

inline constexpr specimen progressive_gray = {
    "progressive Gray", true, true, -1, 1, true, progressive_gray_jpeg};
inline constexpr specimen progressive_rgb = {
    "progressive APP14 RGB", false, false, 0, 1, true, progressive_color_jpeg};
inline constexpr specimen progressive_ycbcr = {
    "progressive JFIF", false, true, -1, -1, false, progressive_color_jpeg};

inline constexpr specimen cases[] = {
    {"gray", true, true, -1, 1, true, gray_jpeg},
    {"rgb", false, false, -1, 0, false, color_jpeg},
    {"default YCbCr", false, false, -1, -1, false, color_jpeg},
    {"JFIF", false, true, -1, -1, false, color_jpeg},
    {"explicit YCbCr", false, false, -1, 1, true, color_jpeg},
    {"APP14 RGB", false, false, 0, 1, true, color_jpeg},
    {"APP14 YCbCr", false, false, 1, 0, false, color_jpeg},
    progressive_gray, progressive_rgb, progressive_ycbcr,
};

inline constexpr unsigned subsampled_side = 17u;

inline std::vector<unsigned char> subsampled_rgb()
{
    // Quantized horizontal frequency 4 gives +13, -13, -13, +13
    // around each luma block's mean. Chroma is uniform across the image.
    constexpr int y[] = {84, 58, 58, 84, 84, 58, 58, 84, 126, 100, 100,
        126, 126, 100, 100, 126, 174};
    std::vector<unsigned char> rgb;
    rgb.reserve(subsampled_side * subsampled_side * 3u);
    for (unsigned row = 0u; row < subsampled_side; ++row) {
        for (int value : y) {
            rgb.push_back(static_cast<unsigned char>(value + 36));
            rgb.push_back(static_cast<unsigned char>(value - 22));
            rgb.push_back(static_cast<unsigned char>(value + 16));
        }
    }
    return rgb;
}

inline std::array<unsigned char, 12> colors(const specimen& spec)
{
    const std::array<unsigned char, 12> gray = {
        17, 17, 17, 85, 85, 85, 153, 153, 153, 221, 221, 221};
    const std::array<unsigned char, 12> rgb = {
        80, 150, 158, 111, 106, 92, 172, 91, 146, 41, 135, 169};
    const std::array<unsigned char, 12> ycbcr_to_rgb = {
        122, 51, 119, 61, 144, 72, 197, 172, 106, 98, 9, 53};
    const bool convert = spec.adobe >= 0 ? spec.adobe == 1 : spec.transform != 0;
    auto result = spec.gray ? gray : (convert ? ycbcr_to_rgb : rgb);
    if (spec.invert) {
        for (size_t i = 0u; i < result.size(); ++i)
            if (spec.gray || i % 3u != 1u) result[i] = 255u - result[i];
    }
    return result;
}

inline native_clip_graph::original create(const specimen& spec, container storage,
    bool encrypted, fault bad = fault::none)
{
    native_clip_graph::options opt;
    if (storage != container::name) {
        opt = native_clip_graph::viewport_case(opt, storage == container::array ? 0u : 3u);
        opt.image_affine = storage == container::array ? 4u : 6u;
        opt.order = storage == container::array ? 3u : 5u;
        opt.rotation = storage == container::array ? 90 : 270;
        opt.user_quarters = storage == container::array ? 5u : 3u;
        opt.primitive = 4u;
    }
    auto original = native_clip_graph::create(opt, false);
    auto pdf = QPDF::create();
    pdf->processMemoryFile("JPEG scene authoring", original.bytes.data(), original.bytes.size());
    auto image = pdf->getRoot().getKey("/Pages").getKey("/Resources")
        .getKey("/XObject").getKey("/I");

    std::string jpeg(reinterpret_cast<const char*>(spec.payload.data()), spec.payload.size());
    if (spec.jfif)
        jpeg.insert(2u, reinterpret_cast<const char*>(jfif_marker), sizeof(jfif_marker));
    if (spec.adobe == 0)
        jpeg.insert(2u, reinterpret_cast<const char*>(adobe_rgb_marker), sizeof(adobe_rgb_marker));
    if (spec.adobe == 1)
        jpeg.insert(2u, reinterpret_cast<const char*>(adobe_ycbcr_marker), sizeof(adobe_ycbcr_marker));
    if (bad == fault::truncated) jpeg.resize(jpeg.size() - 6u);

    O filter = O::newName("/DCTDecode"), parameters = O::newNull();
    if (spec.transform >= 0 || bad == fault::bad_transform) {
        parameters = O::newDictionary();
        parameters.replaceKey("/ColorTransform",
            O::newInteger(bad == fault::bad_transform ? 2 : spec.transform));
    }
    if (storage == container::array) {
        filter = O::newArray({filter});
        parameters = O::newArray({parameters});
    }
    if (storage == container::wrapped) {
        jpeg = native_content_fixture::ascii85(native_content_fixture::flate(jpeg));
        filter = O::parse("[/ASCII85Decode /FlateDecode /DCTDecode]");
        parameters = O::newArray({O::newNull(), O::newNull(), parameters});
    }
    if (bad == fault::nonterminal_dct) {
        filter = O::parse("[/DCTDecode /FlateDecode]");
        parameters = O::newNull();
    }
    image.replaceStreamData(jpeg, filter, parameters);
    image.setFilterOnWrite(false);
    auto dict = image.getDict();
    dict.replaceKey("/Width", O::newInteger(side + unsigned(bad == fault::width)));
    dict.replaceKey("/Height", O::newInteger(side + unsigned(bad == fault::height)));
    const bool declared_gray = spec.gray != (bad == fault::channels);
    dict.replaceKey("/ColorSpace", O::newName(declared_gray ? "/DeviceGray" : "/DeviceRGB"));
    if (spec.invert)
        dict.replaceKey("/Decode", O::parse(spec.gray ? "[1 0]" : "[1 0 0 1 1 0]"));
    for (auto& paint : original.paints)
        if (paint.image) paint.colors = colors(spec);

    QPDFWriter writer(*pdf);
    writer.setOutputMemory();
    writer.setStreamDataMode(qpdf_s_preserve);
    if (encrypted)
        writer.setR6EncryptionParameters("reader", "owner", true, true, true, true,
            true, true, qpdf_r3p_full, true);
    writer.write();
    const auto buffer = writer.getBufferSharedPointer();
    original.bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
    return original;
}

} // namespace native_jpeg

#endif
