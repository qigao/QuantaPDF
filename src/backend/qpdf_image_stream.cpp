#include "qpdf_image_stream.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>

#include <memory>
#include <utility>

#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
extern "C" void quantapdf_source_stage_oom_arm(unsigned) noexcept;
#endif

namespace quantapdf::detail {
namespace {

using Obj = QPDFObjectHandle;
constexpr size_t jpeg_work_limit = 64u * 1024u * 1024u;

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

bool qpdf_has_dct_filter(Obj filter)
{
    if (!filter.isArray()) return is_dct(filter);
    const int count = filter.getArrayNItems();
    require(count <= 16, QUANTAPDF_ERROR_UNSUPPORTED);
    for (int i = 0; i < count; ++i)
        if (is_dct(filter.getArrayItem(i))) return true;
    return false;
}

std::vector<unsigned char> qpdf_decode_dct_image(Obj image,
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
    require(sample_size < jpeg_work_limit, QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t encoded_limit = jpeg_work_limit - sample_size;
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
        {jpeg_work_limit, QUANTAPDF_JPEG_MAX_SCAN_BLOCKS},
        color_transform, layout, pixels.data());
    require(status == QUANTAPDF_OK, status);
    return pixels;
}

} // namespace quantapdf::detail
