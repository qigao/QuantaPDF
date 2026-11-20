#include "qpdf_image_stream.h"
#include "qpdf_composer.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>

#include <algorithm>
#include <cmath>
#include <memory>
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

image_samples qpdf_read_image_samples(Obj image, unsigned channels, bool indexed, size_t pixel_limit)
{
    Obj dict = image.getDict();
    image_samples result;
    result.width = image_dimension(dict.getKey("/Width"), pixel_limit);
    result.height = image_dimension(dict.getKey("/Height"), pixel_limit / result.width);
    result.channels = channels;
    Obj bits = dict.getKey("/BitsPerComponent");
    require(bits.isInteger());
    const auto bpc = bits.getIntValue();
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

device_image_samples qpdf_normalize_device_samples(image_samples&& samples)
{
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
    if (identity && samples.bits == output_bits)
        return {std::move(samples.bytes), wide};

    const size_t bytes_per_sample = output_bits / 8u;
    const size_t output_size = static_cast<size_t>(samples.width) * samples.height *
        samples.channels * bytes_per_sample;
    std::vector<unsigned char> expanded;
    auto *destination = samples.bytes.data();
    if (samples.bits != output_bits) {
        require(output_size <= image_work_limit - samples.bytes.size(), QUANTAPDF_ERROR_UNSUPPORTED);
#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
        quantapdf_source_stage_oom_arm(14u);
#endif
        expanded.resize(output_size);
        destination = expanded.data();
    }
    const unsigned maximum = wide ? 65535u : 255u;
    for (size_t y = 0u; y < samples.height; ++y) {
        for (size_t x = 0u; x < samples.width; ++x) {
            for (unsigned c = 0u; c < samples.channels; ++c) {
                const unsigned value = samples.value(x, y, c, maximum);
                if (wide) *destination++ = static_cast<unsigned char>(value >> 8u);
                *destination++ = static_cast<unsigned char>(value);
            }
        }
    }
    return {samples.bits == output_bits ? std::move(samples.bytes) : std::move(expanded), wide};
}

} // namespace quantapdf::detail
