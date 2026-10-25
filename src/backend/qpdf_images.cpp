#include "qpdf_document_internal.h"
#include "qpdf_image_ir.h"
#include "qpdf_composer.h"
#include "snapshot/image_snapshot.h"

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDFExc.hh>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Obj = QPDFObjectHandle;
constexpr size_t max_content_bytes = 8u * 1024u * 1024u;
constexpr size_t max_pixel_bytes = 64u * 1024u * 1024u;
constexpr size_t max_occurrences = 4096u;
struct failure { quantapdf_status status; };
void require(bool ok, quantapdf_status status = QUANTAPDF_ERROR_FORMAT)
{
    if (!ok) throw failure{status};
}

/* Streaming capacity is checked before accepting decoder output. Unsupported
 * filters never result in treating compressed bytes as image samples. */
class sink final : public Pipeline {
public:
    explicit sink(size_t capacity) : Pipeline("native image projection", nullptr), limit(capacity) {}
    void write(unsigned char const *data, size_t count) override
    {
        require(count <= limit - bytes.size(), QUANTAPDF_ERROR_UNSUPPORTED);
        if (count != 0) bytes.insert(bytes.end(), data, data + count);
    }
    void finish() override { finished = true; }
    size_t limit;
    bool finished = false;
    std::vector<unsigned char> bytes;
};

std::vector<unsigned char> decode_stream(Obj stream, size_t limit,
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
        require(stream.pipeStreamData(nullptr, nullptr, 0, level, true, true),
            QUANTAPDF_ERROR_UNSUPPORTED);
    sink output(limit);
    bool filtered = false;
    require(stream.pipeStreamData(&output, &filtered, 0, level, true, true) &&
        (filtered || level == qpdf_dl_none) && output.finished);
    return std::move(output.bytes);
}

struct jpeg_input { std::vector<unsigned char> bytes; int color_transform = -1; };
jpeg_input jpeg_source(Obj image)
{
    Obj dict = image.getDict(), filter = dict.getKey("/Filter");
    std::vector<Obj> filters, parms;
    if (filter.isName()) filters.push_back(filter);
    else {
        require(filter.isArray());
        for (int i = 0; i < filter.getArrayNItems(); ++i) filters.push_back(filter.getArrayItem(i));
    }
    require(!filters.empty() && filters.back().isName() &&
        (filters.back().getName() == "/DCTDecode" || filters.back().getName() == "/DCT"),
        QUANTAPDF_ERROR_UNSUPPORTED);
    Obj parameters = dict.getKey("/DecodeParms");
    if (parameters.isArray()) {
        require(parameters.getArrayNItems() == static_cast<int>(filters.size()));
        for (int i = 0; i < parameters.getArrayNItems(); ++i) parms.push_back(parameters.getArrayItem(i));
    } else {
        require(parameters.isNull() || filters.size() == 1u);
        parms.resize(filters.size(), Obj::newNull()); parms.back() = parameters;
    }
    jpeg_input result;
    if (!parms.back().isNull()) {
        require(parms.back().isDictionary());
        for (auto const& key : parms.back().getKeys())
            require(key == "/ColorTransform", QUANTAPDF_ERROR_UNSUPPORTED);
        Obj transform = parms.back().getKey("/ColorTransform");
        if (!transform.isNull()) {
            require(transform.isInteger() && (transform.getIntValue() == 0 || transform.getIntValue() == 1));
            result.color_transform = static_cast<int>(transform.getIntValue());
        }
    }
    filters.pop_back(); parms.pop_back();
    auto raw = decode_stream(image, max_pixel_bytes, qpdf_dl_none);
    if (filters.empty()) { result.bytes = std::move(raw); return result; }
    QPDF wrapper; wrapper.emptyPDF();
    Obj stream = wrapper.newStream(std::string(raw.begin(), raw.end()));
    stream.getDict().replaceKey("/Filter", Obj::newArray(filters));
    stream.getDict().replaceKey("/DecodeParms", Obj::newArray(parms));
    result.bytes = decode_stream(stream, max_pixel_bytes, qpdf_dl_specialized);
    return result;
}

struct budget {
    size_t content = 0, pixels = 0, work = 0;
    void step() { require(work++ < 1000000u, QUANTAPDF_ERROR_UNSUPPORTED); }
};
using matrix = std::array<double, 6>;
constexpr matrix identity = {1, 0, 0, 1, 0, 0};
matrix concat(matrix const& p, matrix const& c)
{
    matrix result = {p[0]*c[0]+p[2]*c[1], p[1]*c[0]+p[3]*c[1],
        p[0]*c[2]+p[2]*c[3], p[1]*c[2]+p[3]*c[3],
        p[0]*c[4]+p[2]*c[5]+p[4], p[1]*c[4]+p[3]*c[5]+p[5]};
    for (double value : result) require(std::isfinite(value));
    return result;
}
double number(Obj value)
{
    require(value.isNumber());
    double result = value.getNumericValue();
    require(std::isfinite(result));
    return result;
}
matrix read_matrix(Obj value)
{
    if (value.isNull()) return identity;
    require(value.isArray() && value.getArrayNItems() == 6);
    matrix result;
    for (int i = 0; i < 6; ++i) result[static_cast<size_t>(i)] = number(value.getArrayItem(i));
    return result;
}
size_t dimension(Obj value)
{
    require(value.isInteger());
    long long n = value.getIntValue();
    require(n > 0);
    require(n <= INT_MAX, QUANTAPDF_ERROR_UNSUPPORTED);
    return static_cast<size_t>(n);
}

struct samples {
    size_t width = 0, height = 0, row = 0;
    unsigned bits = 0, channels = 0;
    bool indexed = false;
    unsigned high = 0, palette_channels = 0;
    std::vector<unsigned char> bytes, palette;
    std::vector<double> decode;
    unsigned raw(size_t x, size_t y, unsigned channel) const
    {
        size_t bit = (x * channels + channel) * bits;
        size_t byte = y * row + bit / 8u;
        if (bits == 16) return static_cast<unsigned>(bytes[byte]) * 256u + bytes[byte+1u];
        return (static_cast<unsigned>(bytes[byte]) >> (8u - bits - bit % 8u)) & ((1u << bits)-1u);
    }
    unsigned char value(size_t x, size_t y, unsigned channel) const
    {
        double max = static_cast<double>((1u << bits)-1u);
        double t = raw(x,y,channel) / max;
        double v = decode[channel*2u]*(1.0-t) + decode[channel*2u+1u]*t;
        return static_cast<unsigned char>(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5);
    }
    std::array<unsigned char, 3> rgb(size_t x, size_t y) const
    {
        if (indexed) {
            double max = static_cast<double>((1u << bits)-1u);
            double t = raw(x,y,0) / max;
            double v = decode[0]*(1.0-t) + decode[1]*t;
            size_t i = static_cast<size_t>(std::clamp(v + 0.5, 0.0, static_cast<double>(high)));
            if (palette_channels == 1) return {palette[i], palette[i], palette[i]};
            return {palette[i*3u], palette[i*3u+1u], palette[i*3u+2u]};
        }
        if (channels == 1) { auto gray = value(x,y,0); return {gray,gray,gray}; }
        return {value(x,y,0), value(x,y,1), value(x,y,2)};
    }
};

Obj color_space(Obj value, Obj resources)
{
    std::set<std::string> seen;
    while (value.isName()) {
        std::string name = value.getName();
        if (name == "/DeviceRGB" || name == "/DeviceGray") return value;
        require(seen.size() < 16u && seen.insert(name).second);
        Obj spaces = resources.isDictionary() ? resources.getKey("/ColorSpace") : Obj::newNull();
        if (!spaces.isDictionary() || spaces.getKey(name).isNull())
            throw failure{QUANTAPDF_ERROR_UNSUPPORTED};
        value = spaces.getKey(name);
    }
    return value;
}

samples image_samples(Obj image, Obj resources)
{
    Obj dict = image.getDict();
    Obj stencil = dict.getKey("/ImageMask");
    require(stencil.isNull() || stencil.isBool());
    require(stencil.isNull() || !stencil.getBoolValue(), QUANTAPDF_ERROR_UNSUPPORTED);
    samples result;
    result.width = dimension(dict.getKey("/Width"));
    result.height = dimension(dict.getKey("/Height"));
    require(result.width <= max_pixel_bytes / 4u &&
        result.height <= max_pixel_bytes / 4u / result.width, QUANTAPDF_ERROR_UNSUPPORTED);
    Obj bits = dict.getKey("/BitsPerComponent");
    require(bits.isInteger());
    long long bpc = bits.getIntValue();
    require(bpc == 1 || bpc == 2 || bpc == 4 || bpc == 8 || bpc == 16,
        QUANTAPDF_ERROR_UNSUPPORTED);
    result.bits = static_cast<unsigned>(bpc);
    Obj space = color_space(dict.getKey("/ColorSpace"), resources);
    if (space.isArray()) {
        require(space.getArrayNItems() == 4 && space.getArrayItem(0).isName() &&
            space.getArrayItem(0).getName() == "/Indexed", QUANTAPDF_ERROR_UNSUPPORTED);
        Obj base = color_space(space.getArrayItem(1), resources);
        require(base.isName() && (base.getName() == "/DeviceRGB" ||
            base.getName() == "/DeviceGray"), QUANTAPDF_ERROR_UNSUPPORTED);
        Obj high = space.getArrayItem(2);
        require(high.isInteger() && high.getIntValue() >= 0 && high.getIntValue() <= 255);
        result.indexed = true;
        result.high = static_cast<unsigned>(high.getIntValue());
        result.palette_channels = base.getName() == "/DeviceRGB" ? 3u : 1u;
        size_t length = (static_cast<size_t>(result.high)+1u) * result.palette_channels;
        Obj lookup = space.getArrayItem(3);
        if (lookup.isString()) {
            std::string data = lookup.getStringValue();
            require(data.size() == length);
            result.palette.assign(data.begin(), data.end());
        } else {
            result.palette = decode_stream(lookup, length, qpdf_dl_specialized);
        }
        require(result.palette.size() == length);
        result.channels = 1;
    } else {
        require(space.isName() && (space.getName() == "/DeviceGray" ||
            space.getName() == "/DeviceRGB"), QUANTAPDF_ERROR_UNSUPPORTED);
        result.channels = space.getName() == "/DeviceGray" ? 1u : 3u;
    }
    require(result.width <= max_pixel_bytes / (result.channels * result.bits),
        QUANTAPDF_ERROR_UNSUPPORTED);
    result.row = (result.width * result.channels * result.bits + 7u) / 8u;
    require(result.height <= max_pixel_bytes / result.row, QUANTAPDF_ERROR_UNSUPPORTED);
    size_t expected = result.row * result.height;
    for (unsigned i = 0; i < result.channels; ++i) {
        result.decode.push_back(0);
        result.decode.push_back(result.indexed ? static_cast<double>((1u<<result.bits)-1u) : 1.0);
    }
    Obj decode = dict.getKey("/Decode");
    if (!decode.isNull()) {
        require(decode.isArray() && decode.getArrayNItems() == static_cast<int>(result.channels*2u));
        for (unsigned i = 0; i < result.channels*2u; ++i) result.decode[i] = number(decode.getArrayItem(static_cast<int>(i)));
    }
    Obj filter = dict.getKey("/Filter");
    bool dct = filter.isName() && (filter.getName() == "/DCTDecode" || filter.getName() == "/DCT");
    if (filter.isArray()) {
        require(filter.getArrayNItems() <= 16, QUANTAPDF_ERROR_UNSUPPORTED);
        for (int i = 0; i < filter.getArrayNItems(); ++i)
            if (filter.getArrayItem(i).isName() && (filter.getArrayItem(i).getName() == "/DCTDecode" ||
                filter.getArrayItem(i).getName() == "/DCT"))
                dct = true;
    }
    if (dct) {
        /* Decode lossless wrappers first, then preflight the JPEG itself before
         * its decoder can allocate from independent JPEG header dimensions. */
        auto jpeg = jpeg_source(image);
        require(!jpeg.bytes.empty());
        uint32_t width = 0, height = 0;
        int channels = 0;
        unsigned char *data = nullptr;
        auto status = quantapdf_jpeg_decode(jpeg.bytes.data(), jpeg.bytes.size(),
            max_pixel_bytes, jpeg.color_transform, &data, &width, &height, &channels);
        std::unique_ptr<unsigned char, decltype(&std::free)> decoded(data, std::free);
        require(status == QUANTAPDF_OK, status);
        require(result.bits == 8 && !result.indexed && width == result.width &&
            height == result.height && channels == static_cast<int>(result.channels));
        result.bytes.assign(data, data + expected);
    } else {
        result.bytes = decode_stream(image, expected, qpdf_dl_specialized);
    }
    require(result.bytes.size() == expected);
    return result;
}

struct image_value {
    quantapdf_image_occurrence_internal info = {};
    std::vector<unsigned char> pixels;
};

image_value decode_image(Obj image, Obj resources, budget& limits, double opacity)
{
    size_t width = dimension(image.getDict().getKey("/Width"));
    size_t height = dimension(image.getDict().getKey("/Height"));
    require(width <= (max_pixel_bytes - limits.pixels) / 4u &&
        height <= (max_pixel_bytes - limits.pixels) / 4u / width,
        QUANTAPDF_ERROR_UNSUPPORTED);
    samples source = image_samples(image, resources);
    require(source.width <= max_pixel_bytes / 4u &&
        source.height <= max_pixel_bytes / 4u / source.width, QUANTAPDF_ERROR_UNSUPPORTED);
    size_t count = source.width * source.height;
    require(count * 4u <= max_pixel_bytes - limits.pixels, QUANTAPDF_ERROR_UNSUPPORTED);
    Obj dict = image.getDict();
    Obj soft = dict.getKey("/SMask"), mask = dict.getKey("/Mask");
    require(soft.isNull() || mask.isNull());
    samples alpha;
    bool soft_mask = !soft.isNull() && !(soft.isName() && soft.getName() == "/None");
    std::vector<double> matte;
    if (soft_mask) {
        require(soft.isStream());
        require(soft.getDict().getKey("/SMask").isNull() &&
            soft.getDict().getKey("/Mask").isNull(), QUANTAPDF_ERROR_UNSUPPORTED);
        alpha = image_samples(soft, resources);
        require(alpha.channels == 1 && !alpha.indexed);
        Obj value = soft.getDict().getKey("/Matte");
        if (!value.isNull()) {
            require(!source.indexed && value.isArray() &&
                value.getArrayNItems() == static_cast<int>(source.channels));
            for (unsigned c = 0; c < source.channels; ++c)
                matte.push_back(number(value.getArrayItem(static_cast<int>(c))));
        }
    }
    std::vector<unsigned> color_key;
    if (!mask.isNull()) {
        require(mask.isArray(), QUANTAPDF_ERROR_UNSUPPORTED);
        require(mask.getArrayNItems() == static_cast<int>(source.channels*2u));
        for (unsigned i = 0; i < source.channels*2u; ++i) {
            Obj value = mask.getArrayItem(static_cast<int>(i));
            require(value.isInteger() && value.getIntValue() >= 0 &&
                value.getIntValue() <= static_cast<long long>((1u<<source.bits)-1u));
            color_key.push_back(static_cast<unsigned>(value.getIntValue()));
        }
        for (unsigned c = 0; c < source.channels; ++c)
            require(color_key[c*2u] <= color_key[c*2u+1u]);
    }
    image_value result;
    result.pixels.resize(count * 4u);
    bool has_alpha = false;
    for (size_t y = 0; y < source.height; ++y) {
        for (size_t x = 0; x < source.width; ++x) {
            auto rgb = source.rgb(x, y);
            unsigned a = soft_mask ? alpha.value(
                static_cast<size_t>(static_cast<uint64_t>(x)*alpha.width/source.width),
                static_cast<size_t>(static_cast<uint64_t>(y)*alpha.height/source.height), 0) : 255u;
            if (!color_key.empty()) {
                bool transparent = true;
                for (unsigned c = 0; c < source.channels; ++c) {
                    unsigned sample = source.raw(x,y,c);
                    transparent = transparent && sample >= color_key[c*2u] && sample <= color_key[c*2u+1u];
                }
                if (transparent) a = 0;
            }
            unsigned final_alpha = static_cast<unsigned>(a * opacity + 0.5);
            has_alpha = has_alpha || final_alpha != 255u;
            size_t offset = (y * source.width + x)*4u;
            for (unsigned c = 0; c < 3; ++c) {
                if (matte.empty()) result.pixels[offset+c] = static_cast<unsigned char>(rgb[c]*a/255.0*opacity+0.5);
                else {
                    double m = matte[source.channels == 1 ? 0u : c];
                    double premult = rgb[c]/255.0 - m*(1.0-a/255.0);
                    result.pixels[offset+c] = static_cast<unsigned char>(std::clamp(premult, 0.0, a/255.0)*255.0*opacity+0.5);
                }
            }
            result.pixels[offset+3u] = static_cast<unsigned char>(final_alpha);
        }
    }
    unsigned channels = has_alpha ? 4u : 3u;
    if (!has_alpha) {
        for (size_t i = 0; i < count; ++i)
            for (size_t c = 0; c < 3; ++c) result.pixels[i*3u+c] = result.pixels[i*4u+c];
        result.pixels.resize(count*3u);
    }
    limits.pixels += result.pixels.size();
    result.info.pixel_width = static_cast<int>(source.width);
    result.info.pixel_height = static_cast<int>(source.height);
    require(source.width <= static_cast<size_t>(INT_MAX)/channels, QUANTAPDF_ERROR_UNSUPPORTED);
    result.info.pixel_stride = static_cast<int>(source.width * channels);
    result.info.pixel_components = static_cast<int>(channels);
    result.info.components = source.indexed ? 3 : static_cast<int>(source.channels);
    result.info.bits_per_component = static_cast<int>(source.bits);
    result.info.has_alpha = has_alpha ? 1 : 0;
    result.info.pixel_size = result.pixels.size();
    return result;
}

struct image_graphics {
    matrix ctm = identity;
    double opacity = 1.0;
    bool external_mask = false, transfer = false;
};

struct projection {
    QPDF parser;
    quantapdf_qpdf_page_geometry geometry;
    budget limits;
    std::set<QPDFObjGen> active_forms;
    std::vector<image_value> images;
    /* Image occurrence observation accepts unrelated paint. The separate
     * renderable source-IR path must admit every content operator instead. */
    bool strict_image_paint;
    explicit projection(quantapdf_qpdf_page_geometry const& value,
                        bool strict = false) :
        geometry(value), strict_image_paint(strict)
    { parser.emptyPDF(); parser.setSuppressWarnings(true); }
    void content(Obj streams, Obj resources, image_graphics const& initial, size_t depth);
    void draw(Obj object, Obj resources, image_graphics const& graphics, size_t depth)
    {
        limits.step();
        require(object.isStream());
        Obj dict = object.getDict(), type = dict.getKey("/Subtype");
        require(type.isName());
        if (strict_image_paint) {
            require(type.getName() == "/Image", QUANTAPDF_ERROR_UNSUPPORTED);
            /* No source alpha/mask, interpolation, color-map, transfer,
             * colorspace ambiguity or decoder guessing in first IR bridge.
             * The observation projection remains independently broader. */
            Obj filter = dict.getKey("/Filter");
            require((filter.isNull() ||
                (filter.isName() && filter.getName() == "/FlateDecode")) &&
                dict.getKey("/Decode").isNull() &&
                dict.getKey("/DecodeParms").isNull() &&
                dict.getKey("/SMask").isNull() &&
                dict.getKey("/Mask").isNull() &&
                dict.getKey("/Alternates").isNull() &&
                dict.getKey("/Intent").isNull(), QUANTAPDF_ERROR_UNSUPPORTED);
            Obj stencil = dict.getKey("/ImageMask");
            require(stencil.isNull() || (stencil.isBool() && !stencil.getBoolValue()),
                QUANTAPDF_ERROR_UNSUPPORTED);
            Obj interpolate = dict.getKey("/Interpolate");
            require(interpolate.isNull() ||
                (interpolate.isBool() && !interpolate.getBoolValue()),
                QUANTAPDF_ERROR_UNSUPPORTED);
            Obj bits = dict.getKey("/BitsPerComponent");
            Obj color = dict.getKey("/ColorSpace");
            require(bits.isInteger() && bits.getIntValue() == 8 &&
                color.isName() &&
                (color.getName() == "/DeviceGray" ||
                 color.getName() == "/DeviceRGB"), QUANTAPDF_ERROR_UNSUPPORTED);
            Obj type_value = dict.getKey("/Type");
            require(type_value.isNull() ||
                (type_value.isName() && type_value.getName() == "/XObject"),
                QUANTAPDF_ERROR_UNSUPPORTED);
        }
        if (type.getName() == "/Form") {
            require(depth < 32u, QUANTAPDF_ERROR_UNSUPPORTED);
            require(active_forms.insert(object.getObjGen()).second);
            Obj own = dict.getKey("/Resources");
            require(own.isNull() || own.isDictionary());
            auto child = graphics;
            child.ctm = concat(graphics.ctm, read_matrix(dict.getKey("/Matrix")));
            content(object, own.isNull() ? resources : own, child, depth+1u);
            active_forms.erase(object.getObjGen());
            return;
        }
        require(type.getName() == "/Image", QUANTAPDF_ERROR_UNSUPPORTED);
        require(!graphics.external_mask && !graphics.transfer, QUANTAPDF_ERROR_UNSUPPORTED);
        require(images.size() < max_occurrences, QUANTAPDF_ERROR_UNSUPPORTED);
        auto image = decode_image(object, resources, limits, graphics.opacity);
        if (strict_image_paint) {
            require(graphics.opacity == 1.0 &&
                !image.info.has_alpha && image.info.pixel_components == 3 &&
                (image.info.components == 1 || image.info.components == 3),
                QUANTAPDF_ERROR_UNSUPPORTED);
        }
        auto const& ctm = graphics.ctm;
        auto point = [&](double x, double y) {
            double px = 0, py = 0;
            quantapdf_qpdf_pdf_point_to_public(geometry,
                ctm[0]*x+ctm[2]*y+ctm[4], ctm[1]*x+ctm[3]*y+ctm[5], &px, &py);
            quantapdf_point result = {static_cast<float>(px), static_cast<float>(py)};
            require(std::isfinite(result.x) && std::isfinite(result.y));
            return result;
        };
        image.info.quad = {point(0,1), point(1,1), point(0,0), point(1,0)};
        images.push_back(std::move(image));
    }
};

/* This observer interprets placement/resources and nonstroking opacity.
 * Other paint/text/Clip operators do not change image occurrence quads.
 * Every other standard operator is consumed with its grammar; unknown or
 * inline-image content is explicitly unsupported, never a partial inventory. */
class content_reader final : public Obj::ParserCallbacks {
public:
    content_reader(projection& owner, Obj resources, image_graphics const& initial, size_t depth) :
        owner(owner), resources(resources), graphics(initial), depth(depth) {}
    void handleObject(Obj object) override
    {
        owner.limits.step();
        require(!object.isInlineImage(), QUANTAPDF_ERROR_UNSUPPORTED);
        if (!object.isOperator()) {
            require(args.size() < 64u, QUANTAPDF_ERROR_UNSUPPORTED);
            args.push_back(object); return;
        }
        std::string op = object.getOperatorValue();
        if (op == "q") {
            require(args.empty()); require(stack.size() <
                (owner.strict_image_paint ? 16u : 128u), QUANTAPDF_ERROR_UNSUPPORTED);
            stack.push_back(graphics);
        } else if (op == "Q") {
            require(args.empty() && !stack.empty()); graphics = stack.back(); stack.pop_back();
        } else if (op == "cm") {
            require(args.size() == 6u); matrix change;
            for (size_t i = 0; i < 6; ++i) change[i] = number(args[i]);
            graphics.ctm = concat(graphics.ctm, change);
        } else if (op == "Do") {
            require(args.size() == 1u && args[0].isName());
            require(resources.isDictionary());
            Obj objects = resources.getKey("/XObject");
            require(objects.isDictionary());
            owner.draw(objects.getKey(args[0].getName()), resources, graphics, depth);
        } else if (owner.strict_image_paint) {
            /* The extraction observer may ignore unrelated graphics.
             * Native page rendering never may. Even a late text/path/sh
             * operator after an Image Do invalidates the whole page. */
            require(false, QUANTAPDF_ERROR_UNSUPPORTED);
        } else if (op == "gs") {
            require(args.size() == 1u && args[0].isName() && resources.isDictionary());
            Obj states = resources.getKey("/ExtGState"); require(states.isDictionary());
            Obj state = states.getKey(args[0].getName()); require(state.isDictionary());
            Obj alpha = state.getKey("/ca");
            if (!alpha.isNull()) graphics.opacity = std::clamp(number(alpha),0.0,1.0);
            Obj mask = state.getKey("/SMask");
            if (!mask.isNull()) graphics.external_mask = !(mask.isName() && mask.getName() == "/None");
            Obj transfer = state.getKey("/TR2");
            if (transfer.isNull()) transfer = state.getKey("/TR");
            if (!transfer.isNull()) graphics.transfer = !(transfer.isName() &&
                (transfer.getName() == "/Identity" || transfer.getName() == "/Default"));
        } else {
            struct grammar { char const *name; size_t count; };
            static const grammar ignored[] = {
                {"w",1},{"J",1},{"j",1},{"M",1},{"d",2},{"ri",1},{"i",1},
                {"m",2},{"l",2},{"c",6},{"v",4},{"y",4},{"h",0},{"re",4},
                {"S",0},{"s",0},{"f",0},{"F",0},{"f*",0},{"B",0},{"B*",0},
                {"b",0},{"b*",0},{"n",0},{"W",0},{"W*",0},{"BT",0},{"ET",0},
                {"Tc",1},{"Tw",1},{"Tz",1},{"TL",1},{"Tf",2},{"Tr",1},{"Ts",1},
                {"Td",2},{"TD",2},{"Tm",6},{"T*",0},{"Tj",1},{"TJ",1},{"'",1},{"\"",3},
                {"d0",2},{"d1",6},{"CS",1},{"cs",1},{"G",1},{"g",1},{"RG",3},{"rg",3},
                {"K",4},{"k",4},{"sh",1},{"MP",1},{"DP",2},{"BMC",1},{"BDC",2},
                {"EMC",0},{"BX",0},{"EX",0}};
            bool found = false;
            for (auto const& spec : ignored)
                if (op == spec.name) { require(args.size() == spec.count); found = true; break; }
            if (op == "SC" || op == "SCN" || op == "sc" || op == "scn") {
                require(!args.empty() && args.size() <= 32u); found = true;
            }
            require(found, QUANTAPDF_ERROR_UNSUPPORTED);
        }
        args.clear();
    }
    void handleEOF() override { require(args.empty() && stack.empty()); }
private:
    projection& owner;
    Obj resources;
    image_graphics graphics;
    size_t depth;
    std::vector<image_graphics> stack;
    std::vector<Obj> args;
};

void projection::content(Obj streams, Obj resources, image_graphics const& initial, size_t depth)
{
    std::vector<unsigned char> bytes;
    auto append = [&](Obj stream) {
        limits.step();
        require(limits.content < max_content_bytes, QUANTAPDF_ERROR_UNSUPPORTED);
        auto part = decode_stream(stream, max_content_bytes - limits.content - 1u, qpdf_dl_specialized);
        limits.content += part.size()+1u;
        bytes.insert(bytes.end(), part.begin(), part.end()); bytes.push_back('\n');
    };
    if (streams.isArray()) {
        require(streams.getArrayNItems() <= 1024, QUANTAPDF_ERROR_UNSUPPORTED);
        for (int i = 0; i < streams.getArrayNItems(); ++i) append(streams.getArrayItem(i));
    } else if (!streams.isNull()) append(streams);
    Obj decoded = parser.newStream(std::string(bytes.begin(), bytes.end()));
    content_reader reader(*this, resources, initial, depth);
    decoded.parseAsContents(&reader);
    require(!parser.anyWarnings());
}

void dispose(quantapdf_image_page *snapshot)
{
    if (snapshot == nullptr) return;
    for (size_t i = 0; i < snapshot->count; ++i) std::free(snapshot->items[i].pixels);
    std::free(snapshot->items); std::free(snapshot);
}
}

extern "C" quantapdf_status quantapdf_qpdf_extract_images(
    quantapdf_qpdf_document *document, int page_index, quantapdf_image_page **out_images)
{
    if (out_images == nullptr) return QUANTAPDF_ERROR_ARGUMENT;
    *out_images = nullptr;
    if (document == nullptr || page_index < 0) return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_qpdf_page_geometry geometry = {};
        auto status = quantapdf_qpdf_load_page_geometry(*document->pdf, page_index, &geometry);
        if (status != QUANTAPDF_OK) return status;
        Obj resources = quantapdf_qpdf_inherited_value(geometry.page, "/Resources");
        require(resources.isNull() || resources.isDictionary());
        projection project(geometry);
        project.content(geometry.page.getKey("/Contents"), resources, image_graphics{}, 0u);
        std::unique_ptr<quantapdf_image_page, decltype(&dispose)> result(
            static_cast<quantapdf_image_page *>(std::calloc(1u, sizeof(quantapdf_image_page))), dispose);
        require(result != nullptr, QUANTAPDF_ERROR_NOMEM);
        if (!project.images.empty()) {
            result->items = static_cast<quantapdf_image_occurrence_internal *>(
                std::calloc(project.images.size(), sizeof(*result->items)));
            require(result->items != nullptr, QUANTAPDF_ERROR_NOMEM);
        }
        result->count = project.images.size();
        for (size_t i = 0; i < project.images.size(); ++i) {
            auto const& source = project.images[i];
            auto& target = result->items[i]; target = source.info;
            target.pixels = static_cast<unsigned char *>(std::malloc(source.pixels.size()));
            require(target.pixels != nullptr, QUANTAPDF_ERROR_NOMEM);
            std::memcpy(target.pixels, source.pixels.data(), source.pixels.size());
        }
        *out_images = result.release(); return QUANTAPDF_OK;
    } catch (failure const& error) { return error.status; }
      catch (std::bad_alloc const&) { return QUANTAPDF_ERROR_NOMEM; }
      catch (std::length_error const&) { return QUANTAPDF_ERROR_UNSUPPORTED; }
      catch (QPDFExc const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch (std::runtime_error const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch (...) { return QUANTAPDF_ERROR_BACKEND; }
}


namespace {

/* The common image-observation projection is allowed to ignore text and
 * unrelated paint. This stricter entry point is the opposite: a renderable
 * page may not silently omit any source operator or source resource. */
Obj source_image_resources(Obj page, size_t max_parent_steps)
{
    std::set<QPDFObjGen> ancestors;
    Obj current = page;
    size_t count = 0u;
    while (!current.isNull()) {
        require(++count <= max_parent_steps &&
            max_parent_steps <= 256u, QUANTAPDF_ERROR_UNSUPPORTED);
        require(current.isDictionary() && current.isIndirect());
        require(ancestors.insert(current.getObjGen()).second);
        Obj resource = current.getKey("/Resources");
        if (!resource.isNull()) {
            require(resource.isDictionary());
            return resource;
        }
        current = current.getKey("/Parent");
    }
    throw failure{QUANTAPDF_ERROR_FORMAT};
}

struct source_image_ir_holder {
    quantapdf_ir ir = {};
    ~source_image_ir_holder()
    {
        if (ir.resources.images != nullptr) {
            for (size_t i = 0u; i < ir.resources.image_count; ++i) {
                std::free(ir.resources.images[i].data);
                std::free(ir.resources.images[i].alpha_data);
            }
        }
        std::free(ir.resources.images);
        std::free(ir.operations);
        std::free(ir.pages);
    }
    quantapdf_ir release()
    {
        quantapdf_ir owned = ir;
        ir = {};
        return owned;
    }
};

size_t source_image_integer(double input, size_t page_extent)
{
    require(std::isfinite(input) && input >= 0.0 &&
        input <= static_cast<double>(page_extent),
        QUANTAPDF_ERROR_UNSUPPORTED);
    size_t result = static_cast<size_t>(input);
    require(static_cast<double>(result) == input,
        QUANTAPDF_ERROR_UNSUPPORTED);
    return result;
}

void source_image_layer(const image_value& source,
                        size_t page_width, size_t page_height,
                        quantapdf_composer_operation *operation,
                        quantapdf_composer_image_state *image,
                        size_t image_index)
{
    const auto& quad = source.info.quad;
    /* Rotation, mirroring, shear, shape deformation, and fractional
     * placement are unsupported in this first original-Do bridge. */
    require(quad.ul.y == quad.ur.y && quad.ll.y == quad.lr.y &&
        quad.ul.x == quad.ll.x && quad.ur.x == quad.lr.x,
        QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t x0 = source_image_integer(quad.ul.x, page_width);
    const size_t x1 = source_image_integer(quad.ur.x, page_width);
    const size_t y0 = source_image_integer(quad.ul.y, page_height);
    const size_t y1 = source_image_integer(quad.ll.y, page_height);
    require(x1 > x0 && y1 > y0, QUANTAPDF_ERROR_UNSUPPORTED);
    require(source.info.pixel_width > 0 &&
        source.info.pixel_height > 0 &&
        source.info.pixel_components == 3 &&
        (source.info.components == 1 || source.info.components == 3) &&
        !source.info.has_alpha, QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t w = static_cast<size_t>(source.info.pixel_width);
    const size_t h = static_cast<size_t>(source.info.pixel_height);
    require(w <= SIZE_MAX / h && w * h <= SIZE_MAX / 3u &&
        source.info.pixel_stride > 0 &&
        static_cast<size_t>(source.info.pixel_stride) == w * 3u &&
        source.pixels.size() == w * h * 3u &&
        source.info.pixel_size == w * h * 3u,
        QUANTAPDF_ERROR_UNSUPPORTED);

    image->width = static_cast<uint32_t>(w);
    image->height = static_cast<uint32_t>(h);
    image->components = source.info.components;
    image->format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    image->size = w * h * static_cast<size_t>(image->components);
    image->data = static_cast<unsigned char *>(std::malloc(image->size));
    require(image->data != nullptr, QUANTAPDF_ERROR_NOMEM);
    if (image->components == 3) {
        std::memcpy(image->data, source.pixels.data(), image->size);
    } else {
        for (size_t i = 0u; i < w * h; ++i) {
            require(source.pixels[i * 3u] == source.pixels[i * 3u + 1u] &&
                source.pixels[i * 3u] == source.pixels[i * 3u + 2u],
                QUANTAPDF_ERROR_UNSUPPORTED);
            image->data[i] = source.pixels[i * 3u];
        }
    }
    operation->kind = QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    operation->page_index = 0u;
    operation->graphics_state_id = 0u;
    operation->bounds.x0 = static_cast<float>(x0);
    operation->bounds.y0 = static_cast<float>(y0);
    operation->bounds.x1 = static_cast<float>(x1);
    operation->bounds.y1 = static_cast<float>(y1);
    operation->value.image.image_id =
        static_cast<quantapdf_composer_image_id>(image_index + 1u);
    operation->value.image.options.struct_size =
        QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    operation->value.image.options.fit =
        QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
}
}

extern "C" quantapdf_status quantapdf_qpdf_source_image_ir(
    quantapdf_qpdf_document *document, int page_index,
    const quantapdf_qpdf_source_image_ir_limits *limits,
    quantapdf_ir *out_ir)
{
    if (out_ir != nullptr) std::memset(out_ir, 0, sizeof(*out_ir));
    if (out_ir == nullptr || document == nullptr ||
        document->pdf == nullptr || page_index < 0 ||
        limits == nullptr || limits->max_operations == 0u ||
        limits->max_page_pixels == 0u ||
        limits->max_source_pixels == 0u ||
        limits->max_parent_steps == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_qpdf_page_geometry geometry = {};
        auto status = quantapdf_qpdf_load_page_geometry(
            *document->pdf, page_index, &geometry);
        if (status != QUANTAPDF_OK) return status;
        require(geometry.rotation == 0 && geometry.user_unit == 1.0 &&
            geometry.media[0] == 0.0 && geometry.media[1] == 0.0 &&
            geometry.crop[0] == geometry.media[0] &&
            geometry.crop[1] == geometry.media[1] &&
            geometry.crop[2] == geometry.media[2] &&
            geometry.crop[3] == geometry.media[3] &&
            geometry.right == geometry.media[2] &&
            geometry.top == geometry.media[3],
            QUANTAPDF_ERROR_UNSUPPORTED);
        constexpr size_t hard_pixels = 4194304u, hard_images = 4u;
        const size_t width = source_image_integer(
            geometry.public_width, hard_pixels);
        const size_t height = source_image_integer(
            geometry.public_height, hard_pixels);
        require(width > 0u && height > 0u &&
            width <= hard_pixels / height &&
            width * height <= limits->max_page_pixels,
            QUANTAPDF_ERROR_UNSUPPORTED);
        /* Even if source pixels were admissible, page annotations can
         * contribute visible ink; never silently drop them from render. */
        Obj annots = geometry.page.getKey("/Annots");
        require(annots.isNull() || (annots.isArray() &&
            annots.getArrayNItems() == 0), QUANTAPDF_ERROR_UNSUPPORTED);
        require(geometry.page.getKey("/Group").isNull(),
            QUANTAPDF_ERROR_UNSUPPORTED);
        Obj resources = source_image_resources(
            geometry.page, limits->max_parent_steps);
        projection rendered(geometry, true);
        rendered.content(geometry.page.getKey("/Contents"), resources,
            image_graphics{}, 0u);
        const size_t count = rendered.images.size();
        require(count > 0u && count <= hard_images &&
            count <= limits->max_operations,
            QUANTAPDF_ERROR_UNSUPPORTED);

        size_t total_source_pixels = 0u;
        for (const image_value& val : rendered.images) {
            require(val.info.pixel_width > 0 &&
                val.info.pixel_height > 0, QUANTAPDF_ERROR_UNSUPPORTED);
            const size_t w = static_cast<size_t>(val.info.pixel_width);
            const size_t h = static_cast<size_t>(val.info.pixel_height);
            require(w <= hard_pixels / h &&
                w * h <= hard_pixels - total_source_pixels &&
                w * h <= limits->max_source_pixels - total_source_pixels,
                QUANTAPDF_ERROR_UNSUPPORTED);
            total_source_pixels += w * h;
        }

        source_image_ir_holder owned;
        owned.ir.pages = static_cast<quantapdf_composer_page_state *>(
            std::calloc(1u, sizeof(quantapdf_composer_page_state)));
        require(owned.ir.pages != nullptr, QUANTAPDF_ERROR_NOMEM);
        owned.ir.page_count = 1u;
        owned.ir.pages[0].width_points = static_cast<float>(width);
        owned.ir.pages[0].height_points = static_cast<float>(height);
        owned.ir.pages[0].background_argb = UINT32_C(0xffffffff);
        owned.ir.operations = static_cast<quantapdf_composer_operation *>(
            std::calloc(count, sizeof(quantapdf_composer_operation)));
        require(owned.ir.operations != nullptr, QUANTAPDF_ERROR_NOMEM);
        owned.ir.operation_count = count;
        owned.ir.resources.images = static_cast<quantapdf_composer_image_state *>(
            std::calloc(count, sizeof(quantapdf_composer_image_state)));
        require(owned.ir.resources.images != nullptr, QUANTAPDF_ERROR_NOMEM);
        owned.ir.resources.image_count = count;
        owned.ir.resources.image_capacity = count;
        for (size_t i = 0u; i < count; ++i)
            source_image_layer(rendered.images[i], width, height,
                &owned.ir.operations[i], &owned.ir.resources.images[i], i);
        *out_ir = owned.release();
        return QUANTAPDF_OK;
    } catch (failure const& error) { return error.status; }
      catch (std::bad_alloc const&) { return QUANTAPDF_ERROR_NOMEM; }
      catch (std::length_error const&) { return QUANTAPDF_ERROR_UNSUPPORTED; }
      catch (QPDFExc const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch (std::runtime_error const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch (...) { return QUANTAPDF_ERROR_BACKEND; }
}
