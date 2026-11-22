#include "qpdf_image_stream.h"
#include "qpdf_composer.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <utility>

#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
extern "C" void quantapdf_source_stage_oom_arm(unsigned) noexcept;
#endif

namespace quantapdf::detail {
namespace {

using Obj = QPDFObjectHandle;
constexpr size_t image_work_limit = 64u * 1024u * 1024u;

void require(bool ok, quantapdf_status status = QUANTAPDF_ERROR_FORMAT)
{
    if (!ok) throw image_stream_error{status};
}

class bounded_stream_sink final : public Pipeline {
public:
    explicit bounded_stream_sink(size_t capacity)
        : Pipeline("bounded PDF stream", nullptr), limit(capacity) {}

    void write(const unsigned char *data, size_t count) override
    {
        require(count <= limit - bytes.size(), QUANTAPDF_ERROR_UNSUPPORTED);
        if (count != 0u) bytes.insert(bytes.end(), data, data + count);
    }

    void finish() override { finished = true; }
    size_t limit;
    bool finished = false;
    std::vector<unsigned char> bytes;
};

constexpr double cmyk_channel(double ink, double black, double maximum)
{
    return maximum - std::min(maximum, ink + black);
}

bool is_dct(Obj filter)
{
    return filter.isName() &&
        (filter.getName() == "/DCTDecode" || filter.getName() == "/DCT");
}

std::vector<unsigned char> unwrap_jpeg(std::vector<unsigned char>& encoded,
    const std::vector<Obj>& filters, const std::vector<Obj>& parameters, size_t limit)
{
    // QPDF decodes the lossless prefix from a view of the opened stream bytes.
    // Keep the view alive through both the pipeline and wrapper destruction.
    QPDF wrapper;
    wrapper.emptyPDF();
    auto stream = wrapper.newStream(std::make_shared<Buffer>(encoded.data(), encoded.size()));
    stream.getDict().replaceKey("/Filter", Obj::newArray(filters));
    stream.getDict().replaceKey("/DecodeParms", Obj::newArray(parameters));
    return qpdf_read_stream(stream, limit, qpdf_dl_specialized);
}

} // namespace

std::vector<unsigned char> qpdf_read_stream(Obj stream, size_t limit,
    qpdf_stream_decode_level_e level)
{
    require(stream.isStream());
    Obj dict = stream.getDict();
    require(dict.getKey("/F").isNull() && dict.getKey("/FFilter").isNull() &&
        dict.getKey("/FDecodeParms").isNull(), QUANTAPDF_ERROR_UNSUPPORTED);
    Obj filter = dict.getKey("/Filter");
    require(filter.isNull() || filter.isName() || filter.isArray());
    auto crypt = [](Obj value) { return value.isName() && value.getName() == "/Crypt"; };
    require(!crypt(filter), QUANTAPDF_ERROR_UNSUPPORTED);
    if (filter.isArray()) {
        require(filter.getArrayNItems() <= 16, QUANTAPDF_ERROR_UNSUPPORTED);
        for (int i = 0; i < filter.getArrayNItems(); ++i)
            require(!crypt(filter.getArrayItem(i)), QUANTAPDF_ERROR_UNSUPPORTED);
    }
    if (level != qpdf_dl_none)
        require(stream.pipeStreamData(nullptr, nullptr, 0, level, true, false),
            QUANTAPDF_ERROR_UNSUPPORTED);
    bounded_stream_sink output(limit);
    bool filtered = false;
    require(stream.pipeStreamData(&output, &filtered, 0, level, true, false) &&
        (filtered || level == qpdf_dl_none) && output.finished);
    return std::move(output.bytes);
}

namespace {

bool has_dct_filter(Obj filter)
{
    if (!filter.isArray()) return is_dct(filter);
    const int count = filter.getArrayNItems();
    require(count <= 16, QUANTAPDF_ERROR_UNSUPPORTED);
    for (int i = 0; i < count; ++i)
        if (is_dct(filter.getArrayItem(i))) return true;
    return false;
}

std::vector<unsigned char> decode_dct_image(Obj image,
    quantapdf_jpeg_layout layout)
{
    Obj dict = image.getDict();
    Obj filter = dict.getKey("/Filter");
    std::vector<Obj> filters = filter.isArray()
        ? filter.getArrayAsVector() : std::vector<Obj>{filter};
    require(is_dct(filters.back()), QUANTAPDF_ERROR_UNSUPPORTED);

    Obj entry = dict.getKey("/DecodeParms");
    std::vector<Obj> parameters;
    if (entry.isArray()) {
        parameters = entry.getArrayAsVector();
        require(parameters.size() == filters.size());
    } else {
        require(entry.isNull() || filters.size() == 1u);
        parameters.resize(filters.size(), Obj::newNull());
        parameters.back() = entry;
    }
    int color_transform = -1;
    if (!parameters.back().isNull()) {
        Obj dct = parameters.back();
        require(dct.isDictionary());
        for (const auto& key : dct.getKeys())
            require(key == "/ColorTransform", QUANTAPDF_ERROR_UNSUPPORTED);
        Obj transform = dct.getKey("/ColorTransform");
        if (!transform.isNull()) {
            require(transform.isInteger());
            const auto value = transform.getIntValue();
            require(value == 0 || value == 1);
            color_transform = static_cast<int>(value);
        }
    }

    const size_t sample_size = size_t(layout.width) * layout.height * layout.components;
    require(sample_size < image_work_limit, QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t encoded_limit = image_work_limit - sample_size;
    auto encoded = qpdf_read_stream(image, encoded_limit, qpdf_dl_none);
    filters.pop_back();
    parameters.pop_back();
    if (!filters.empty())
        encoded = unwrap_jpeg(encoded, filters, parameters, encoded_limit - encoded.size());
    require(!encoded.empty());

#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
    quantapdf_source_stage_oom_arm(13u);
#endif
    std::vector<unsigned char> pixels(sample_size);
    const auto status = quantapdf_jpeg_decode(encoded.data(), encoded.size(),
        {image_work_limit, QUANTAPDF_JPEG_MAX_SCAN_BLOCKS},
        color_transform, layout, pixels.data());
    require(status == QUANTAPDF_OK, status);
    return pixels;
}

long long image_parameter(Obj dictionary, char const *key, long long default_value)
{
    Obj value = dictionary.getKey(key);
    if (value.isNull()) return default_value;
    require(value.isInteger());
    return value.getIntValue();
}

void check_lossless_predictors(Obj filter, Obj parameters, const image_samples& samples)
{
    if (parameters.isNull()) return;
    require(filter.isName() || filter.isArray());
    const bool filter_array = filter.isArray();
    const bool parameter_array = parameters.isArray();
    const int count = filter_array ? filter.getArrayNItems() : 1;
    require(count <= 16, QUANTAPDF_ERROR_UNSUPPORTED);
    require(parameter_array ? parameters.getArrayNItems() == count
                            : count == 1 && parameters.isDictionary());
    for (int i = 0; i < count; ++i) {
        Obj name = filter_array ? filter.getArrayItem(i) : filter;
        Obj entry = parameter_array ? parameters.getArrayItem(i) : parameters;
        require(name.isName());
        if (entry.isNull()) continue;
        require(entry.isDictionary());
        if (!name.isNameAndEquals("/FlateDecode") && !name.isNameAndEquals("/LZWDecode")) continue;
        const auto predictor = image_parameter(entry, "/Predictor", 1);
        require(predictor == 1 || predictor == 2 || (predictor >= 10 && predictor <= 15));
        if (predictor == 1) continue;
        require(image_parameter(entry, "/Columns", 1) == samples.width &&
                image_parameter(entry, "/Colors", 1) == samples.channels &&
                image_parameter(entry, "/BitsPerComponent", 8) == samples.bits);
    }
}

uint32_t image_dimension(Obj value, size_t limit)
{
    require(value.isInteger() && value.getIntValue() > 0);
    const auto dimension = static_cast<unsigned long long>(value.getIntValue());
    require(dimension <= limit && dimension <= UINT32_MAX, QUANTAPDF_ERROR_UNSUPPORTED);
    return static_cast<uint32_t>(dimension);
}

} // namespace

unsigned image_samples::raw(size_t x, size_t y, unsigned channel) const
{
    const size_t bit = (x * channels + channel) * bits;
    const auto *sample = bytes.data() + y * row_bytes + bit / 8u;
    if (bits == 16u) return (static_cast<unsigned>(sample[0]) << 8u) | sample[1];
    return (static_cast<unsigned>(sample[0]) >> (8u - bits - bit % 8u)) & ((1u << bits) - 1u);
}

double image_samples::decoded(size_t x, size_t y, unsigned channel) const
{
    const double t = static_cast<double>(raw(x, y, channel)) / ((1u << bits) - 1u);
    return std::lerp(decode[2u * channel], decode[2u * channel + 1u], t);
}

unsigned image_samples::value(size_t x, size_t y, unsigned channel, unsigned maximum) const
{
    return static_cast<unsigned>(std::clamp(decoded(x, y, channel), 0.0, 1.0) * maximum + 0.5);
}

std::array<double, 3u> image_samples::cmyk_rgb(size_t x, size_t y) const
{
    const auto ink = [&](unsigned channel) {
        return std::clamp(decoded(x, y, channel), 0.0, 1.0);
    };
    const double black = ink(3u);
    return {cmyk_channel(ink(0u), black, 1.0), cmyk_channel(ink(1u), black, 1.0),
        cmyk_channel(ink(2u), black, 1.0)};
}

namespace {

image_samples read_image_samples(Obj image, unsigned channels, bool indexed, size_t pixel_limit,
    bool stencil = false)
{
    Obj dict = image.getDict();
    image_samples result;
    result.width = image_dimension(dict.getKey("/Width"), pixel_limit);
    result.height = image_dimension(dict.getKey("/Height"), pixel_limit / result.width);
    result.channels = channels;
    Obj bits = dict.getKey("/BitsPerComponent");
    require(bits.isInteger() || (stencil && bits.isNull()));
    const auto bpc = bits.isNull() ? 1 : bits.getIntValue();
    require(!stencil || bpc == 1);
    require(bpc == 1 || bpc == 2 || bpc == 4 || bpc == 8 || bpc == 16,
            QUANTAPDF_ERROR_UNSUPPORTED);
    result.bits = static_cast<unsigned>(bpc);
    const size_t bits_per_pixel = channels * result.bits;
    require(result.width <= (SIZE_MAX - 7u) / bits_per_pixel, QUANTAPDF_ERROR_UNSUPPORTED);
    result.row_bytes = (result.width * bits_per_pixel + 7u) / 8u;
    require(result.row_bytes <= image_work_limit / result.height, QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t expected = result.row_bytes * result.height;

    Obj decode = dict.getKey("/Decode");
    if (decode.isNull()) {
        for (unsigned c = 0u; c < channels; ++c)
            result.decode[2u * c + 1u] = indexed ? (1u << result.bits) - 1u : 1u;
    } else {
        require(decode.isArray() && decode.getArrayNItems() == static_cast<int>(2u * channels));
        for (unsigned i = 0u; i < 2u * channels; ++i) {
            Obj value = decode.getArrayItem(static_cast<int>(i));
            require(value.isNumber());
            result.decode[i] = value.getNumericValue();
            require(std::isfinite(result.decode[i]));
        }
    }

    if (stencil)
        require((result.decode[0] == 0.0 && result.decode[1] == 1.0) ||
                (result.decode[0] == 1.0 && result.decode[1] == 0.0));
    Obj filter = dict.getKey("/Filter");
    if (has_dct_filter(filter)) {
        require(result.bits == 8u && !indexed);
        result.bytes = decode_dct_image(image,
            {result.width, result.height, static_cast<int>(channels)});
    } else {
        check_lossless_predictors(filter, dict.getKey("/DecodeParms"), result);
        result.bytes = qpdf_read_stream(image, expected,
            filter.isNull() ? qpdf_dl_none : qpdf_dl_specialized);
    }
    require(result.bytes.size() == expected);
    return result;
}

unsigned device_channels(const Obj& value)
{
    if (value.isNameAndEquals("/DeviceGray")) return 1u;
    if (value.isNameAndEquals("/DeviceRGB")) return 3u;
    if (value.isNameAndEquals("/DeviceCMYK")) return 4u;
    return 0u;
}

Obj resolve_image_color_space(Obj value, Obj resources)
{
    std::set<std::string> seen;
    while (value.isName() && device_channels(value) == 0u) {
        std::string name = value.getName();
        Obj spaces = resources.isDictionary() ? resources.getKey("/ColorSpace") : Obj::newNull();
        require(spaces.isDictionary(), QUANTAPDF_ERROR_UNSUPPORTED);
        require(seen.size() < 16u && seen.insert(name).second);
        value = spaces.getKey(name);
        require(!value.isNull(), QUANTAPDF_ERROR_UNSUPPORTED);
    }
    return value;
}

} // namespace

const unsigned char *image_colors::indexed_color(size_t x, size_t y) const
{
    const double value = samples.decoded(x, y, 0u);
    const size_t index = static_cast<size_t>(std::clamp(value + 0.5, 0.0, static_cast<double>(high)));
    return palette.data() + index * palette_channels;
}

std::array<unsigned char, 3u> image_colors::rgb(size_t x, size_t y) const
{
    if (indexed()) {
        const auto *color = indexed_color(x, y);
        if (palette_channels == 1u) return {color[0], color[0], color[0]};
        return {color[0], color[1], color[2]};
    }
    if (samples.channels == 4u) {
        const auto color = samples.cmyk_rgb(x, y);
        return {static_cast<unsigned char>(color[0] * 255u + 0.5),
            static_cast<unsigned char>(color[1] * 255u + 0.5),
            static_cast<unsigned char>(color[2] * 255u + 0.5)};
    }
    const auto channel = [&](unsigned c) {
        return static_cast<unsigned char>(samples.value(x, y, c, 255u));
    };
    if (samples.channels == 1u) {
        const auto gray = channel(0u);
        return {gray, gray, gray};
    }
    return {channel(0u), channel(1u), channel(2u)};
}

image_colors qpdf_read_image_colors(Obj image, Obj resources,
    size_t pixel_limit, bool resolve_names)
{
    Obj dict = image.getDict();
    image_colors result;
    const auto resolve = [&](Obj value) {
        return resolve_names ? resolve_image_color_space(value, resources) : value;
    };
    const auto channels_for = [&](const Obj& space) {
        const unsigned channels = device_channels(space);
        require(channels != 0u, QUANTAPDF_ERROR_UNSUPPORTED);
        if (channels == 4u && resources.isDictionary())
            require(resources.getKeyIfDict("/ColorSpace").getKeyIfDict("/DefaultCMYK").isNull(),
                QUANTAPDF_ERROR_UNSUPPORTED);
        return channels;
    };
    Obj space = resolve(dict.getKey("/ColorSpace"));
    unsigned channels;
    if (space.isArray()) {
        require(space.getArrayNItems() == 4 && space.getArrayItem(0).isName() &&
            space.getArrayItem(0).getName() == "/Indexed", QUANTAPDF_ERROR_UNSUPPORTED);
        const unsigned base_channels = channels_for(resolve(space.getArrayItem(1)));
        Obj high = space.getArrayItem(2);
        require(high.isInteger() && high.getIntValue() >= 0 && high.getIntValue() <= 255);
        result.high = static_cast<unsigned>(high.getIntValue());
        const size_t count = static_cast<size_t>(result.high) + 1u;
        const size_t length = count * base_channels;
        Obj lookup = space.getArrayItem(3);
        if (lookup.isString()) {
            const std::string data = lookup.getStringValue();
            require(data.size() == length);
            result.palette.assign(data.begin(), data.end());
        } else {
            result.palette = qpdf_read_stream(lookup, length, qpdf_dl_specialized);
        }
        require(result.palette.size() == length);
        result.palette_channels = base_channels;
        if (base_channels == 4u) {
            // Convert each palette entry once, before image sampling.
            for (size_t i = 0u; i < count; ++i) {
                const unsigned black = result.palette[4u * i + 3u];
                for (size_t c = 0u; c < 3u; ++c)
                    result.palette[3u * i + c] = static_cast<unsigned char>(
                        cmyk_channel(result.palette[4u * i + c], black, 255u));
            }
            result.palette.resize(3u * count);
            result.palette_channels = 3u;
        }
        channels = 1u;
    } else {
        channels = channels_for(space);
    }
    result.samples = read_image_samples(image, channels, result.indexed(), pixel_limit);
    return result;
}

std::vector<unsigned> qpdf_read_color_key(Obj mask, const image_samples& samples)
{
    if (mask.isNull()) return {};
    require(mask.isArray(), QUANTAPDF_ERROR_UNSUPPORTED);
    require(mask.getArrayNItems() == static_cast<int>(samples.channels * 2u));
    const unsigned maximum = (1u << samples.bits) - 1u;
    std::vector<unsigned> key;
    for (unsigned i = 0u; i < samples.channels * 2u; ++i) {
        Obj value = mask.getArrayItem(static_cast<int>(i));
        require(value.isInteger() && value.getIntValue() >= 0 && value.getIntValue() <= maximum);
        key.push_back(static_cast<unsigned>(value.getIntValue()));
    }
    for (unsigned c = 0u; c < samples.channels; ++c)
        require(key[2u * c] <= key[2u * c + 1u]);
    return key;
}

bool image_sample_is_masked(const image_samples& samples, const std::vector<unsigned>& key, size_t x, size_t y)
{
    for (unsigned c = 0u; c < samples.channels; ++c) {
        const unsigned sample = samples.raw(x, y, c);
        if (sample < key[2u * c] || sample > key[2u * c + 1u]) return false;
    }
    return true;
}

namespace {

image_alpha_plane allocate_mask_plane(const image_samples& samples, Obj dict,
    unsigned bits, size_t available_bytes)
{
    Obj interpolate = dict.getKey("/Interpolate");
    require(interpolate.isNull() || interpolate.isBool());
    const size_t count = static_cast<size_t>(samples.width) * samples.height;
    const size_t stride = bits == 16u ? 2u : 1u;
    require(samples.bytes.size() <= available_bytes &&
        count <= (available_bytes - samples.bytes.size()) / stride, QUANTAPDF_ERROR_UNSUPPORTED);
#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
    quantapdf_source_stage_oom_arm(15u);
#endif
    return {std::vector<unsigned char>(count * stride), samples.width, samples.height,
        interpolate.isBool() && interpolate.getBoolValue(), bits};
}

} // namespace

image_alpha_plane qpdf_read_stencil(Obj mask, size_t pixel_limit, size_t color_bytes)
{
    Obj dict = mask.getDict();
    Obj type = dict.getKey("/Type");
    require((type.isNull() || type.isNameAndEquals("/XObject")) &&
        dict.getKey("/Subtype").isNameAndEquals("/Image") &&
        dict.getKey("/ImageMask").isBool() && dict.getKey("/ImageMask").getBoolValue() &&
        dict.getKey("/ColorSpace").isNull() && dict.getKey("/Mask").isNull() &&
        dict.getKey("/SMask").isNull());
    require(dict.getKey("/OC").isNull() && dict.getKey("/SMaskInData").isNull(),
        QUANTAPDF_ERROR_UNSUPPORTED);
    require(color_bytes <= image_work_limit, QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t available_bytes = image_work_limit - color_bytes;
    const auto samples = read_image_samples(mask, 1u, false, std::min(pixel_limit, available_bytes), true);
    auto alpha = allocate_mask_plane(samples, dict, 1u, available_bytes);
    const unsigned painted_bit = samples.decode[0] == 0.0 ? 0u : 1u;
    for (size_t y = 0u; y < samples.height; ++y)
        for (size_t x = 0u; x < samples.width; ++x)
            alpha.bytes[y * samples.width + x] = samples.raw(x, y, 0u) == painted_bit ? 255u : 0u;
    return alpha;
}

image_alpha_plane qpdf_read_soft_mask(Obj mask, size_t pixel_limit, size_t color_bytes, bool allow_matte)
{
    require(mask.isStream(), QUANTAPDF_ERROR_UNSUPPORTED);
    Obj dict = mask.getDict();
    Obj type = dict.getKey("/Type"), stencil = dict.getKey("/ImageMask");
    require((type.isNull() || type.isNameAndEquals("/XObject")) &&
        dict.getKey("/Subtype").isNameAndEquals("/Image") &&
        dict.getKey("/ColorSpace").isNameAndEquals("/DeviceGray") &&
        (stencil.isNull() || (stencil.isBool() && !stencil.getBoolValue())) &&
        dict.getKey("/Mask").isNull() && dict.getKey("/SMask").isNull());
    require(dict.getKey("/OC").isNull() && dict.getKey("/SMaskInData").isNull() &&
        (allow_matte || dict.getKey("/Matte").isNull()),
        QUANTAPDF_ERROR_UNSUPPORTED);

    require(color_bytes <= image_work_limit, QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t available_bytes = image_work_limit - color_bytes;
    const auto samples = read_image_samples(mask, 1u, false, std::min(pixel_limit, available_bytes));
    const bool direct = samples.decode[0] == 0.0 && samples.decode[1] == 1.0;
    const bool inverted = samples.decode[0] == 1.0 && samples.decode[1] == 0.0;
    const bool wide = samples.bits == 16u || (!direct && !inverted);
    auto alpha = allocate_mask_plane(samples, dict, wide ? 16u : 8u, available_bytes);
    const unsigned maximum = wide ? 65535u : 255u;
    auto *destination = alpha.bytes.data();
    for (size_t y = 0u; y < samples.height; ++y)
        for (size_t x = 0u; x < samples.width; ++x) {
            const auto value = samples.value(x, y, 0u, maximum);
            if (wide) *destination++ = static_cast<unsigned char>(value >> 8u);
            *destination++ = static_cast<unsigned char>(value);
        }
    return alpha;
}

namespace {

device_image_samples normalize_device_samples(image_samples&& samples, image_alpha_plane alpha)
{
    const bool cmyk = samples.channels == 4u;
    const int components = cmyk ? 3 : static_cast<int>(samples.channels);
    bool identity = true;
    bool wide = samples.bits == 16u;
    for (unsigned c = 0u; c < samples.channels; ++c) {
        const double low = samples.decode[2u * c], high = samples.decode[2u * c + 1u];
        const bool direct = low == 0.0 && high == 1.0;
        const bool inverted = low == 1.0 && high == 0.0;
        identity &= direct;
        wide |= !direct && !inverted;
    }
    const unsigned output_bits = wide ? 16u : 8u;
    if (!cmyk && identity && samples.bits == output_bits)
        return {std::move(samples.bytes), std::move(alpha), wide, components};

    const size_t bytes_per_sample = output_bits / 8u;
    const size_t output_size = static_cast<size_t>(samples.width) * samples.height *
        components * bytes_per_sample;
    const bool separate_buffer = cmyk || samples.bits != output_bits;
    std::vector<unsigned char> expanded;
    auto *destination = samples.bytes.data();
    if (separate_buffer) {
        require(output_size <= image_work_limit - samples.bytes.size() - alpha.bytes.size(), QUANTAPDF_ERROR_UNSUPPORTED);
#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
        quantapdf_source_stage_oom_arm(14u);
#endif
        expanded.resize(output_size);
        destination = expanded.data();
    }
    const unsigned maximum = wide ? 65535u : 255u;
    const auto write_sample = [&](unsigned value) {
        if (wide) *destination++ = static_cast<unsigned char>(value >> 8u);
        *destination++ = static_cast<unsigned char>(value);
    };
    for (size_t y = 0u; y < samples.height; ++y) {
        for (size_t x = 0u; x < samples.width; ++x) {
            if (cmyk) {
                for (double value : samples.cmyk_rgb(x, y))
                    write_sample(static_cast<unsigned>(value * maximum + 0.5));
            } else {
                for (unsigned c = 0u; c < samples.channels; ++c)
                    write_sample(samples.value(x, y, c, maximum));
            }
        }
    }
    return {separate_buffer ? std::move(expanded) : std::move(samples.bytes), std::move(alpha), wide, components};
}

} // namespace

device_image_samples qpdf_normalize_image_colors(image_colors&& colors, Obj dict, size_t mask_pixel_limit)
{
    const auto& samples = colors.samples;
    image_alpha_plane alpha;
    Obj soft = dict.getKey("/SMask"), mask = dict.getKey("/Mask");
    const size_t color_bytes = samples.bytes.size() + colors.palette.size();
    if (!soft.isNull()) {
        alpha = qpdf_read_soft_mask(soft, mask_pixel_limit, color_bytes, false);
    } else if (mask.isStream()) {
        alpha = qpdf_read_stencil(mask, mask_pixel_limit, color_bytes);
    } else if (const auto key = qpdf_read_color_key(mask, samples); !key.empty()) {
        const size_t count = static_cast<size_t>(samples.width) * samples.height;
        require(count <= image_work_limit - samples.bytes.size(), QUANTAPDF_ERROR_UNSUPPORTED);
#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
        quantapdf_source_stage_oom_arm(15u);
#endif
        alpha.bytes.resize(count);
        for (size_t y = 0u; y < samples.height; ++y)
            for (size_t x = 0u; x < samples.width; ++x)
                alpha.bytes[y * samples.width + x] = image_sample_is_masked(samples, key, x, y) ? 0u : 255u;
    }
    if (!colors.indexed()) return normalize_device_samples(std::move(colors.samples), std::move(alpha));

    const size_t output_size = static_cast<size_t>(samples.width) * samples.height * colors.palette_channels;
    require(output_size + colors.palette.size() <= image_work_limit - samples.bytes.size() - alpha.bytes.size(),
        QUANTAPDF_ERROR_UNSUPPORTED);
#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
    quantapdf_source_stage_oom_arm(14u);
#endif
    std::vector<unsigned char> pixels(output_size);
    auto *destination = pixels.data();
    // The Scene interpolates base colors after palette lookup.
    for (size_t y = 0u; y < samples.height; ++y) {
        for (size_t x = 0u; x < samples.width; ++x) {
            const auto *color = colors.indexed_color(x, y);
            destination = std::copy_n(color, colors.palette_channels, destination);
        }
    }
    return {std::move(pixels), std::move(alpha), false, static_cast<int>(colors.palette_channels)};
}

} // namespace quantapdf::detail
