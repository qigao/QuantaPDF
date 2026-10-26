#include "image_recompression_test_helpers.h"

#include "backend/qpdf_document.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <zlib.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {

QPDFObjectHandle make_image(
    QPDF& pdf,
    int marker,
    int components,
    std::string const& samples)
{
    QPDFObjectHandle image = pdf.newStream(samples);
    QPDFObjectHandle dict = image.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width", QPDFObjectHandle::newInteger(2));
    dict.replaceKey("/Height", QPDFObjectHandle::newInteger(2));
    dict.replaceKey(
        "/ColorSpace",
        QPDFObjectHandle::newName(
            components == 1 ? "/DeviceGray" : "/DeviceRGB"));
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    dict.replaceKey(
        "/QuantaPDFImageId", QPDFObjectHandle::newInteger(marker));
    return image;
}

QPDFObjectHandle make_form(
    QPDF& pdf,
    QPDFObjectHandle image,
    char const *image_name)
{
    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey(image_name, image);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/XObject", xobjects);
    QPDFObjectHandle form = pdf.newStream(
        std::string("q 2 0 0 2 0 0 cm ") + image_name + " Do Q\n");
    QPDFObjectHandle dict = form.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/BBox", QPDFObjectHandle::parse("[0 0 2 2]"));
    dict.replaceKey("/Resources", resources);
    return form;
}

QPDFObjectHandle make_appearance(
    QPDF& pdf,
    int marker,
    int components,
    std::string const& samples)
{
    QPDFObjectHandle image = make_image(pdf, marker, components, samples);
    QPDFObjectHandle nested = make_form(pdf, image, "/AppearanceImage");
    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey("/NestedForm", nested);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/XObject", xobjects);
    QPDFObjectHandle appearance = pdf.newStream(
        "q 10 0 0 10 0 0 cm /NestedForm Do Q\n");
    QPDFObjectHandle dict = appearance.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/BBox", QPDFObjectHandle::parse("[0 0 20 20]"));
    dict.replaceKey("/Resources", resources);
    return appearance;
}

QPDFObjectHandle state_dictionary(QPDFObjectHandle appearance)
{
    QPDFObjectHandle states = QPDFObjectHandle::newDictionary();
    states.replaceKey("/On", appearance);
    return states;
}

void write_fixture(QPDF& pdf, char const *output_path)
{
    QPDFWriter writer(pdf, output_path);
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setStreamDataMode(qpdf_s_preserve);
    writer.write();
}

bool is_dct_image(QPDFObjectHandle image)
{
    if (!image.isStream() || !image.isIndirect()) {
        return false;
    }
    QPDFObjectHandle dict = image.getDict();
    if (!dict.getKey("/Filter").isNameAndEquals("/DCTDecode")) {
        return false;
    }
    QPDFObjectHandle color_space = dict.getKey("/ColorSpace");
    QPDFObjectHandle decode_parms = dict.getKey("/DecodeParms");
    if (color_space.isNameAndEquals("/DeviceRGB")) {
        return decode_parms.isDictionary() &&
            decode_parms.getKey("/ColorTransform").isInteger() &&
            decode_parms.getKey("/ColorTransform").getIntValue() == 0;
    }
    return color_space.isNameAndEquals("/DeviceGray") &&
        decode_parms.isNull();
}

QPDFObjectHandle make_policy_image(
    QPDF& pdf,
    int marker,
    int width,
    int height,
    QPDFObjectHandle color_space,
    int bits,
    std::string const& samples)
{
    QPDFObjectHandle image = pdf.newStream(samples);
    QPDFObjectHandle dict = image.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width", QPDFObjectHandle::newInteger(width));
    dict.replaceKey("/Height", QPDFObjectHandle::newInteger(height));
    dict.replaceKey("/ColorSpace", color_space);
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(bits));
    dict.replaceKey(
        "/QuantaPDFImageId", QPDFObjectHandle::newInteger(marker));
    return image;
}

std::map<long long, std::string> marked_raw_streams(QPDF& pdf)
{
    std::map<long long, std::string> result;
    for (QPDFObjectHandle object : pdf.getAllObjects()) {
        if (!object.isStream())
            continue;
        QPDFObjectHandle marker =
            object.getDict().getKey("/QuantaPDFImageId");
        if (!marker.isInteger())
            continue;
        std::shared_ptr<Buffer> raw = object.getRawStreamData();
        result.emplace(
            marker.getIntValue(),
            std::string(
                reinterpret_cast<char const *>(raw->getBuffer()),
                raw->getSize()));
    }
    return result;
}

std::map<long long, bool> marked_dct_states(QPDF& pdf)
{
    std::map<long long, bool> result;
    for (QPDFObjectHandle object : pdf.getAllObjects()) {
        if (!object.isStream())
            continue;
        QPDFObjectHandle marker =
            object.getDict().getKey("/QuantaPDFImageId");
        if (marker.isInteger())
            result.emplace(marker.getIntValue(), is_dct_image(object));
    }
    return result;
}

std::string base64_encode(unsigned char const *data, size_t size)
{
    static char const alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((size + 2u) / 3u) * 4u);
    for (size_t offset = 0; offset < size; offset += 3u) {
        size_t const remaining = size - offset;
        unsigned int const first = data[offset];
        unsigned int const second = remaining > 1u ? data[offset + 1u] : 0u;
        unsigned int const third = remaining > 2u ? data[offset + 2u] : 0u;
        unsigned int const value = (first << 16u) | (second << 8u) | third;
        result.push_back(alphabet[(value >> 18u) & 0x3fu]);
        result.push_back(alphabet[(value >> 12u) & 0x3fu]);
        result.push_back(remaining > 1u ? alphabet[(value >> 6u) & 0x3fu] : '=');
        result.push_back(remaining > 2u ? alphabet[value & 0x3fu] : '=');
    }
    return result;
}

std::string flate_compress(std::string const& input)
{
    uLongf compressed_size = compressBound(
        static_cast<uLong>(input.size()));
    std::string compressed(compressed_size, '\0');
    int const status = compress2(
        reinterpret_cast<Bytef *>(compressed.data()),
        &compressed_size,
        reinterpret_cast<Bytef const *>(input.data()),
        static_cast<uLong>(input.size()),
        Z_BEST_COMPRESSION);
    if (status != Z_OK)
        throw std::runtime_error("zlib compression failed");
    compressed.resize(static_cast<size_t>(compressed_size));
    return compressed;
}

} // namespace

extern "C" int image_recompression_check_work_budget_arithmetic(void)
{
    size_t limit = 0;
    size_t const largest = SIZE_MAX / 64u;
    if (quantapdf_qpdf_compute_work_budget_limit(largest, &limit) !=
            QUANTAPDF_OK ||
        limit != largest * 64u)
        return 0;
    return quantapdf_qpdf_compute_work_budget_limit(
               largest + 1u, &limit) == QUANTAPDF_ERROR_UNSUPPORTED;
}

extern "C" int image_recompression_create_positive_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        auto const& pages = pdf->getAllPages();
        if (pages.size() < 2u) {
            return 0;
        }
        QPDFObjectHandle page0 = pages[0];
        QPDFObjectHandle page1 = pages[1];

        std::string const rgb_samples(
            "\x00\x20\x40\x60\x80\xa0\xc0\xe0\xff\x30\x90\xf0", 12);
        std::string const gray_samples("\x00\x55\xaa\xff", 4);
        QPDFObjectHandle shared_rgb = make_image(*pdf, 1, 3, rgb_samples);
        QPDFObjectHandle gray = make_image(*pdf, 2, 1, gray_samples);
        QPDFObjectHandle nested_page_form =
            make_form(*pdf, shared_rgb, "/NestedSharedRGB");

        QPDFObjectHandle page0_xobjects = QPDFObjectHandle::newDictionary();
        page0_xobjects.replaceKey("/SharedRGB", shared_rgb);
        page0_xobjects.replaceKey("/Gray", gray);
        page0_xobjects.replaceKey("/NestedPageForm", nested_page_form);
        QPDFObjectHandle page0_resources = QPDFObjectHandle::newDictionary();
        page0_resources.replaceKey("/XObject", page0_xobjects);
        page0.replaceKey("/Resources", page0_resources);
        page0.replaceKey(
            "/Contents",
            pdf->newStream(
                "q 20 0 0 20 10 10 cm /SharedRGB Do Q\n"
                "q 20 0 0 20 40 10 cm /Gray Do Q\n"
                "q 20 0 0 20 70 10 cm /NestedPageForm Do Q\n"));

        QPDFObjectHandle page1_xobjects = QPDFObjectHandle::newDictionary();
        page1_xobjects.replaceKey("/SharedRGB", shared_rgb);
        QPDFObjectHandle page1_resources = QPDFObjectHandle::newDictionary();
        page1_resources.replaceKey("/XObject", page1_xobjects);
        page1.replaceKey("/Resources", page1_resources);
        page1.replaceKey(
            "/Contents",
            pdf->newStream("q 20 0 0 20 10 10 cm /SharedRGB Do Q\n"));

        QPDFObjectHandle direct_ap = QPDFObjectHandle::newDictionary();
        direct_ap.replaceKey(
            "/N", make_appearance(*pdf, 3, 3, rgb_samples));
        direct_ap.replaceKey(
            "/R", make_appearance(*pdf, 4, 1, gray_samples));
        direct_ap.replaceKey(
            "/D", make_appearance(*pdf, 5, 3, rgb_samples));
        QPDFObjectHandle direct_annotation = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Type", QPDFObjectHandle::newName("/Annot")},
                {"/Subtype", QPDFObjectHandle::newName("/Square")},
                {"/Rect", QPDFObjectHandle::parse("[10 40 30 60]")},
                {"/AP", direct_ap}}));

        QPDFObjectHandle state_ap = QPDFObjectHandle::newDictionary();
        state_ap.replaceKey(
            "/N",
            state_dictionary(make_appearance(*pdf, 6, 1, gray_samples)));
        state_ap.replaceKey(
            "/R",
            state_dictionary(make_appearance(*pdf, 7, 3, rgb_samples)));
        state_ap.replaceKey(
            "/D",
            state_dictionary(make_appearance(*pdf, 8, 1, gray_samples)));
        QPDFObjectHandle widget = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Type", QPDFObjectHandle::newName("/Annot")},
                {"/Subtype", QPDFObjectHandle::newName("/Widget")},
                {"/Rect", QPDFObjectHandle::parse("[40 40 60 60]")},
                {"/AP", state_ap}}));

        QPDFObjectHandle annotations = QPDFObjectHandle::newArray();
        annotations.appendItem(direct_annotation);
        annotations.appendItem(widget);
        page0.replaceKey("/Annots", annotations);

        write_fixture(*pdf, output_path);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int image_recompression_canonicalize_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->setAttemptRecovery(false);
        pdf->processFile(source_path);
        for (QPDFObjectHandle page : pdf->getAllPages()) {
            if (page.getKey("/Resources").isNull())
                page.replaceKey(
                    "/Resources", QPDFObjectHandle::newDictionary());
        }
        write_fixture(*pdf, output_path);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int image_recompression_check_positive_output(
    const unsigned char *data,
    size_t size,
    size_t expected_image_count)
{
    try {
        auto pdf = QPDF::create();
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "image-recompression-positive-output",
            reinterpret_cast<char const *>(data),
            size);
        auto const& pages = pdf->getAllPages();
        if (pages.size() < 2u) {
            return 0;
        }
        QPDFObjectHandle shared0 = pages[0]
            .getKey("/Resources")
            .getKey("/XObject")
            .getKey("/SharedRGB");
        QPDFObjectHandle shared1 = pages[1]
            .getKey("/Resources")
            .getKey("/XObject")
            .getKey("/SharedRGB");
        if (!shared0.isSameObjectAs(shared1)) {
            return 0;
        }

        std::set<QPDFObjGen> seen;
        for (QPDFObjectHandle object: pdf->getAllObjects()) {
            if (!object.isStream()) {
                continue;
            }
            QPDFObjectHandle marker =
                object.getDict().getKey("/QuantaPDFImageId");
            if (marker.isNull()) {
                continue;
            }
            if (!marker.isInteger() || !is_dct_image(object)) {
                return 0;
            }
            seen.insert(object.getObjGen());
        }
        return seen.size() == expected_image_count;
    } catch (...) {
        return 0;
    }
}

extern "C" int image_recompression_check_structural_preservation(
    const char *source_path,
    const unsigned char *data,
    size_t size)
{
    try {
        auto source = QPDF::create();
        source->setAttemptRecovery(false);
        source->processFile(source_path);
        auto output = QPDF::create();
        output->setAttemptRecovery(false);
        output->processMemoryFile(
            "image-recompression-structural-output",
            reinterpret_cast<char const *>(data),
            size);

        auto stream_data_equal = [](QPDFObjectHandle left,
                                    QPDFObjectHandle right) {
            if (!left.isStream() || !right.isStream())
                return false;
            return left.getRawStreamData()->view() ==
                right.getRawStreamData()->view();
        };
        auto content_equal = [&](auto const& self,
                                 QPDFObjectHandle left,
                                 QPDFObjectHandle right) -> bool {
            if (left.isNull() || right.isNull())
                return left.isNull() && right.isNull();
            if (left.isStream() || right.isStream())
                return stream_data_equal(left, right);
            if (!left.isArray() || !right.isArray() ||
                left.getArrayNItems() != right.getArrayNItems())
                return false;
            for (int index = 0; index < left.getArrayNItems(); ++index) {
                if (!self(
                        self,
                        left.getArrayItem(index),
                        right.getArrayItem(index)))
                    return false;
            }
            return true;
        };

        auto const& source_pages = source->getAllPages();
        auto const& output_pages = output->getAllPages();
        if (source_pages.size() != output_pages.size())
            return 0;
        for (size_t index = 0; index < source_pages.size(); ++index) {
            if (!content_equal(
                    content_equal,
                    source_pages[index].getKey("/Contents"),
                    output_pages[index].getKey("/Contents")))
                return 0;
        }

        for (QPDFObjectHandle object : source->getAllObjects()) {
            if (!object.isStream() ||
                !object.getDict().getKey("/Subtype").isNameAndEquals("/Form"))
                continue;
            QPDFObjectHandle counterpart =
                output->getObjectByObjGen(object.getObjGen());
            if (!counterpart.isStream() ||
                !counterpart.getDict().getKey("/Subtype").isNameAndEquals(
                    "/Form") ||
                !stream_data_equal(object, counterpart))
                return 0;
        }

        std::map<long long, QPDFObjectHandle> source_images;
        std::map<long long, QPDFObjectHandle> output_images;
        for (QPDFObjectHandle object : source->getAllObjects()) {
            if (!object.isStream())
                continue;
            QPDFObjectHandle marker =
                object.getDict().getKey("/QuantaPDFImageId");
            if (marker.isInteger())
                source_images.emplace(marker.getIntValue(), object);
        }
        for (QPDFObjectHandle object : output->getAllObjects()) {
            if (!object.isStream())
                continue;
            QPDFObjectHandle marker =
                object.getDict().getKey("/QuantaPDFImageId");
            if (marker.isInteger())
                output_images.emplace(marker.getIntValue(), object);
        }
        if (source_images.size() != output_images.size())
            return 0;
        for (auto const& entry : source_images) {
            auto const counterpart = output_images.find(entry.first);
            if (counterpart == output_images.end())
                return 0;
            QPDFObjectHandle left = entry.second.getDict();
            QPDFObjectHandle right = counterpart->second.getDict();
            for (char const *key : {
                     "/Width", "/Height", "/ColorSpace",
                     "/BitsPerComponent", "/ImageMask", "/Decode"}) {
                if (left.getKey(key).unparseResolved() !=
                    right.getKey(key).unparseResolved())
                    return 0;
            }
        }
        return 1;
    } catch (std::exception const& error) {
        std::fprintf(
            stderr, "structural preservation exception: %s\n", error.what());
        return 0;
    } catch (...) {
        return 0;
    }
}

extern "C" int image_recompression_compare_quality_outputs(
    const unsigned char *lower_quality_data,
    size_t lower_quality_size,
    const unsigned char *higher_quality_data,
    size_t higher_quality_size,
    size_t expected_image_count)
{
    try {
        auto lower = QPDF::create();
        lower->setAttemptRecovery(false);
        lower->processMemoryFile(
            "image-recompression-lower-quality",
            reinterpret_cast<char const *>(lower_quality_data),
            lower_quality_size);
        auto higher = QPDF::create();
        higher->setAttemptRecovery(false);
        higher->processMemoryFile(
            "image-recompression-higher-quality",
            reinterpret_cast<char const *>(higher_quality_data),
            higher_quality_size);
        auto const lower_streams = marked_raw_streams(*lower);
        auto const higher_streams = marked_raw_streams(*higher);
        if (lower_streams.size() != expected_image_count ||
            higher_streams.size() != expected_image_count)
            return 0;
        size_t lower_total = 0;
        size_t higher_total = 0;
        for (auto const& entry : lower_streams) {
            auto const counterpart = higher_streams.find(entry.first);
            if (counterpart == higher_streams.end() ||
                counterpart->second == entry.second)
                return 0;
            lower_total += entry.second.size();
            higher_total += counterpart->second.size();
        }
        return lower_total <= higher_total;
    } catch (...) {
        return 0;
    }
}

extern "C" int image_recompression_matches_expected_base64(
    const unsigned char *data,
    size_t size,
    const char *expected_path)
{
    try {
        if (data == nullptr || size == 0 || expected_path == nullptr)
            return 0;
        std::ifstream input(expected_path, std::ios::binary);
        if (!input)
            return 0;
        std::string expected(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        expected.erase(
            std::remove_if(
                expected.begin(),
                expected.end(),
                [](unsigned char value) { return std::isspace(value) != 0; }),
            expected.end());
        return base64_encode(data, size) == expected;
    } catch (...) {
        return 0;
    }
}

extern "C" int image_recompression_create_policy_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        auto const& pages = pdf->getAllPages();
        if (pages.empty())
            return 0;
        QPDFObjectHandle page = pages[0];
        QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
        std::string const rgb6("\x00\x20\x40\x80\xa0\xff", 6);

        xobjects.replaceKey(
            "/CMYK",
            make_policy_image(
                *pdf, 1, 2, 1,
                QPDFObjectHandle::newName("/DeviceCMYK"), 8,
                std::string("\x00\x20\x40\x60\x80\xa0\xc0\xff", 8)));

        QPDFObjectHandle profile = pdf->newStream("dummy ICC profile");
        profile.getDict().replaceKey(
            "/N", QPDFObjectHandle::newInteger(3));
        QPDFObjectHandle icc = QPDFObjectHandle::newArray();
        icc.appendItem(QPDFObjectHandle::newName("/ICCBased"));
        icc.appendItem(profile);
        xobjects.replaceKey(
            "/ICC",
            make_policy_image(*pdf, 2, 2, 1, icc, 8, rgb6));

        xobjects.replaceKey(
            "/Indexed",
            make_policy_image(
                *pdf, 3, 2, 1,
                QPDFObjectHandle::parse(
                    "[/Indexed /DeviceRGB 1 <000000ffffff>]"),
                8,
                std::string("\x00\x01", 2)));

        QPDFObjectHandle soft_mask = make_policy_image(
            *pdf, 90, 2, 1,
            QPDFObjectHandle::newName("/DeviceGray"), 8,
            std::string("\x00\xff", 2));
        soft_mask.getDict().removeKey("/QuantaPDFImageId");
        QPDFObjectHandle with_soft_mask = make_policy_image(
            *pdf, 4, 2, 1,
            QPDFObjectHandle::newName("/DeviceRGB"), 8, rgb6);
        with_soft_mask.getDict().replaceKey("/SMask", soft_mask);
        xobjects.replaceKey("/SoftMask", with_soft_mask);

        QPDFObjectHandle explicit_mask = make_policy_image(
            *pdf, 91, 2, 1,
            QPDFObjectHandle::newName("/DeviceGray"), 1,
            std::string("\xc0", 1));
        explicit_mask.getDict().removeKey("/QuantaPDFImageId");
        explicit_mask.getDict().replaceKey(
            "/ImageMask", QPDFObjectHandle::newBool(true));
        explicit_mask.getDict().removeKey("/ColorSpace");
        explicit_mask.getDict().removeKey("/BitsPerComponent");
        QPDFObjectHandle with_explicit_mask = make_policy_image(
            *pdf, 5, 2, 1,
            QPDFObjectHandle::newName("/DeviceRGB"), 8, rgb6);
        with_explicit_mask.getDict().replaceKey("/Mask", explicit_mask);
        xobjects.replaceKey("/ExplicitMask", with_explicit_mask);

        QPDFObjectHandle color_key = make_policy_image(
            *pdf, 6, 2, 1,
            QPDFObjectHandle::newName("/DeviceRGB"), 8, rgb6);
        color_key.getDict().replaceKey(
            "/Mask", QPDFObjectHandle::parse("[0 0 0 0 0 0]"));
        xobjects.replaceKey("/ColorKeyMask", color_key);

        QPDFObjectHandle stencil = make_policy_image(
            *pdf, 7, 2, 1,
            QPDFObjectHandle::newName("/DeviceGray"), 1,
            std::string("\xc0", 1));
        stencil.getDict().replaceKey(
            "/ImageMask", QPDFObjectHandle::newBool(true));
        stencil.getDict().removeKey("/ColorSpace");
        stencil.getDict().removeKey("/BitsPerComponent");
        xobjects.replaceKey("/Stencil", stencil);

        xobjects.replaceKey(
            "/Bpc1",
            make_policy_image(
                *pdf, 8, 2, 1,
                QPDFObjectHandle::newName("/DeviceGray"), 1,
                std::string("\xc0", 1)));
        xobjects.replaceKey(
            "/Bpc16",
            make_policy_image(
                *pdf, 9, 2, 1,
                QPDFObjectHandle::newName("/DeviceGray"), 16,
                std::string("\x00\x00\xff\xff", 4)));

        QPDFObjectHandle decoded = make_policy_image(
            *pdf, 10, 2, 1,
            QPDFObjectHandle::newName("/DeviceRGB"), 8, rgb6);
        decoded.getDict().replaceKey(
            "/Decode", QPDFObjectHandle::parse("[1 0 0 1 0 1]"));
        xobjects.replaceKey("/NonIdentityDecode", decoded);

        QPDFObjectHandle unsupported = make_policy_image(
            *pdf, 11, 2, 1,
            QPDFObjectHandle::newName("/DeviceRGB"), 8, rgb6);
        unsupported.replaceStreamData(
            rgb6,
            QPDFObjectHandle::newName("/JPXDecode"),
            QPDFObjectHandle::newNull());
        xobjects.replaceKey("/UnsupportedFilter", unsupported);

        xobjects.replaceKey(
            "/Boundary",
            make_policy_image(
                *pdf, 100, 2, 1,
                QPDFObjectHandle::newName("/DeviceRGB"), 8, rgb6));

        QPDFObjectHandle cycle_image = make_policy_image(
            *pdf, 101, 2, 1,
            QPDFObjectHandle::newName("/DeviceGray"), 8,
            std::string("\x00\xff", 2));
        QPDFObjectHandle form_a = pdf->newStream("/CycleImage Do /FormB Do\n");
        QPDFObjectHandle form_b = pdf->newStream("/FormA Do\n");
        for (QPDFObjectHandle form : {form_a, form_b}) {
            form.getDict().replaceKey(
                "/Type", QPDFObjectHandle::newName("/XObject"));
            form.getDict().replaceKey(
                "/Subtype", QPDFObjectHandle::newName("/Form"));
            form.getDict().replaceKey(
                "/BBox", QPDFObjectHandle::parse("[0 0 2 2]"));
        }
        QPDFObjectHandle form_a_xobjects = QPDFObjectHandle::newDictionary();
        form_a_xobjects.replaceKey("/CycleImage", cycle_image);
        form_a_xobjects.replaceKey("/FormB", form_b);
        QPDFObjectHandle form_a_resources = QPDFObjectHandle::newDictionary();
        form_a_resources.replaceKey("/XObject", form_a_xobjects);
        form_a.getDict().replaceKey("/Resources", form_a_resources);
        QPDFObjectHandle form_b_xobjects = QPDFObjectHandle::newDictionary();
        form_b_xobjects.replaceKey("/FormA", form_a);
        QPDFObjectHandle form_b_resources = QPDFObjectHandle::newDictionary();
        form_b_resources.replaceKey("/XObject", form_b_xobjects);
        form_b.getDict().replaceKey("/Resources", form_b_resources);
        xobjects.replaceKey("/CycleRoot", form_a);

        QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
        resources.replaceKey("/XObject", xobjects);
        page.replaceKey("/Resources", resources);
        page.replaceKey(
            "/Contents",
            pdf->newStream(
                "q BI /W 1 /H 1 /CS /RGB /BPC 8 ID abc EI Q\n"));
        write_fixture(*pdf, output_path);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int image_recompression_check_policy_output(
    const char *source_path,
    const unsigned char *data,
    size_t size,
    int expect_boundary_rewritten)
{
    try {
        auto source = QPDF::create();
        source->setAttemptRecovery(false);
        source->processFile(source_path);
        auto output = QPDF::create();
        output->setAttemptRecovery(false);
        output->processMemoryFile(
            "image-recompression-policy-output",
            reinterpret_cast<char const *>(data),
            size);
        auto const source_raw = marked_raw_streams(*source);
        auto const output_raw = marked_raw_streams(*output);
        auto const output_dct = marked_dct_states(*output);
        if (source_raw.size() != 13u || output_raw.size() != 13u ||
            output_dct.size() != 13u) {
            std::fprintf(
                stderr, "policy marker counts: source=%zu output=%zu dct=%zu\n",
                source_raw.size(), output_raw.size(), output_dct.size());
            return 0;
        }
        for (auto const& entry : source_raw) {
            long long const marker = entry.first;
            bool const should_rewrite = marker == 101 ||
                (marker == 100 && expect_boundary_rewritten != 0);
            auto const raw = output_raw.find(marker);
            auto const dct = output_dct.find(marker);
            if (raw == output_raw.end() || dct == output_dct.end() ||
                dct->second != should_rewrite) {
                std::fprintf(
                    stderr,
                    "policy marker %lld: missing=%d dct=%d expected=%d\n",
                    marker,
                    raw == output_raw.end() || dct == output_dct.end(),
                    dct == output_dct.end() ? -1 : dct->second,
                    should_rewrite);
                return 0;
            }
            if (!should_rewrite && raw->second != entry.second) {
                std::fprintf(
                    stderr, "policy marker %lld raw stream changed\n", marker);
                return 0;
            }
        }
        QPDFObjectHandle source_contents =
            source->getAllPages().at(0).getKey("/Contents");
        QPDFObjectHandle output_contents =
            output->getAllPages().at(0).getKey("/Contents");
        std::shared_ptr<Buffer> source_content_data =
            source_contents.getRawStreamData();
        std::shared_ptr<Buffer> output_content_data =
            output_contents.getRawStreamData();
        if (source_content_data->view() != output_content_data->view()) {
            std::fprintf(stderr, "policy inline content stream changed\n");
            return 0;
        }
        return 1;
    } catch (std::exception const& error) {
        std::fprintf(stderr, "policy output check exception: %s\n", error.what());
        return 0;
    } catch (...) {
        std::fprintf(stderr, "policy output check unknown exception\n");
        return 0;
    }
}

extern "C" int image_recompression_create_malformed_fixture(
    const char *source_path,
    const char *output_path,
    image_recompression_malformed_fixture fixture)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        auto const& pages = pdf->getAllPages();
        if (pages.empty())
            return 0;
        QPDFObjectHandle page = pages[0];
        if (fixture == IMAGE_RECOMPRESSION_MALFORMED_ANNOTS) {
            page.replaceKey("/Annots", QPDFObjectHandle::newInteger(7));
            write_fixture(*pdf, output_path);
            return 1;
        }
        if (fixture == IMAGE_RECOMPRESSION_MALFORMED_AP) {
            QPDFObjectHandle annotation = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/Type", QPDFObjectHandle::newName("/Annot")},
                    {"/Subtype", QPDFObjectHandle::newName("/Widget")},
                    {"/Rect", QPDFObjectHandle::parse("[0 0 10 10]")},
                    {"/AP", QPDFObjectHandle::newInteger(7)}}));
            QPDFObjectHandle annotations = QPDFObjectHandle::newArray();
            annotations.appendItem(annotation);
            page.replaceKey("/Annots", annotations);
            write_fixture(*pdf, output_path);
            return 1;
        }
        if (fixture == IMAGE_RECOMPRESSION_MALFORMED_RESOURCES) {
            page.replaceKey("/Resources", QPDFObjectHandle::newInteger(7));
            write_fixture(*pdf, output_path);
            return 1;
        }

        QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
        if (fixture == IMAGE_RECOMPRESSION_MALFORMED_XOBJECTS) {
            resources.replaceKey("/XObject", QPDFObjectHandle::newInteger(7));
            page.replaceKey("/Resources", resources);
            write_fixture(*pdf, output_path);
            return 1;
        }
        if (fixture == IMAGE_RECOMPRESSION_MALFORMED_APPEARANCE_STATE) {
            QPDFObjectHandle states = QPDFObjectHandle::newDictionary();
            states.replaceKey("/On", QPDFObjectHandle::newInteger(7));
            QPDFObjectHandle appearances = QPDFObjectHandle::newDictionary();
            appearances.replaceKey("/N", states);
            QPDFObjectHandle annotation = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/Type", QPDFObjectHandle::newName("/Annot")},
                    {"/Subtype", QPDFObjectHandle::newName("/Widget")},
                    {"/Rect", QPDFObjectHandle::parse("[0 0 10 10]")},
                    {"/AP", appearances}}));
            QPDFObjectHandle annotations = QPDFObjectHandle::newArray();
            annotations.appendItem(annotation);
            page.replaceKey("/Annots", annotations);
            page.replaceKey("/Resources", resources);
            write_fixture(*pdf, output_path);
            return 1;
        }

        QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
        if (fixture == IMAGE_RECOMPRESSION_MALFORMED_NONSTREAM_IMAGE ||
            fixture == IMAGE_RECOMPRESSION_MALFORMED_NONSTREAM_FORM) {
            QPDFObjectHandle fake = QPDFObjectHandle::newDictionary();
            fake.replaceKey(
                "/Subtype",
                QPDFObjectHandle::newName(
                    fixture == IMAGE_RECOMPRESSION_MALFORMED_NONSTREAM_IMAGE
                        ? "/Image"
                        : "/Form"));
            xobjects.replaceKey("/Bad", fake);
        } else {
            bool const filtered =
                fixture == IMAGE_RECOMPRESSION_MALFORMED_CORRUPT_FILTER ||
                fixture == IMAGE_RECOMPRESSION_MALFORMED_DECODED_OVERSIZE;
            QPDFObjectHandle image = make_policy_image(
                *pdf, 200, filtered ? 1 : 2, 1,
                QPDFObjectHandle::newName(
                    filtered ? "/DeviceGray" : "/DeviceRGB"), 8,
                fixture == IMAGE_RECOMPRESSION_MALFORMED_SAMPLE_COUNT
                    ? std::string("\x00\x20\x40\x80\xff", 5)
                    : std::string("\x00\x20\x40\x80\xa0\xff", 6));
            QPDFObjectHandle dict = image.getDict();
            if (fixture == IMAGE_RECOMPRESSION_MALFORMED_CORRUPT_FILTER) {
                image.replaceStreamData(
                    std::string("not-valid-flate-data"),
                    QPDFObjectHandle::newName("/FlateDecode"),
                    QPDFObjectHandle::newNull());
            } else if (
                fixture == IMAGE_RECOMPRESSION_MALFORMED_DECODED_OVERSIZE) {
                image.replaceStreamData(
                    flate_compress(std::string(1024u * 1024u, '\x5a')),
                    QPDFObjectHandle::newName("/FlateDecode"),
                    QPDFObjectHandle::newNull());
            }
            switch (fixture) {
            case IMAGE_RECOMPRESSION_MALFORMED_WIDTH:
                dict.replaceKey("/Width", QPDFObjectHandle::newName("/Bad"));
                break;
            case IMAGE_RECOMPRESSION_MALFORMED_HEIGHT:
                dict.replaceKey("/Height", QPDFObjectHandle::newName("/Bad"));
                break;
            case IMAGE_RECOMPRESSION_MALFORMED_BPC:
                dict.replaceKey(
                    "/BitsPerComponent", QPDFObjectHandle::newName("/Bad"));
                break;
            case IMAGE_RECOMPRESSION_MALFORMED_IMAGE_MASK:
                dict.replaceKey("/ImageMask", QPDFObjectHandle::newInteger(1));
                break;
            case IMAGE_RECOMPRESSION_MALFORMED_DECODE_SCALAR:
                dict.replaceKey("/Decode", QPDFObjectHandle::newName("/Bad"));
                break;
            case IMAGE_RECOMPRESSION_MALFORMED_DECODE_LENGTH:
                dict.replaceKey(
                    "/Decode", QPDFObjectHandle::parse("[0 1 0 1]"));
                break;
            case IMAGE_RECOMPRESSION_MALFORMED_DECODE_ENTRY:
                dict.replaceKey(
                    "/Decode",
                    QPDFObjectHandle::parse("[0 1 0 /Bad 0 1]"));
                break;
            case IMAGE_RECOMPRESSION_MALFORMED_DECODE_NONFINITE: {
                QPDFObjectHandle decode = QPDFObjectHandle::newArray();
                decode.appendItem(QPDFObjectHandle::newInteger(0));
                decode.appendItem(QPDFObjectHandle::newReal("1e9999"));
                decode.appendItem(QPDFObjectHandle::newInteger(0));
                decode.appendItem(QPDFObjectHandle::newInteger(1));
                decode.appendItem(QPDFObjectHandle::newInteger(0));
                decode.appendItem(QPDFObjectHandle::newInteger(1));
                dict.replaceKey("/Decode", decode);
                break;
            }
            case IMAGE_RECOMPRESSION_MALFORMED_DIMENSION_RANGE:
                dict.replaceKey(
                    "/Width", QPDFObjectHandle::newInteger(65501));
                break;
            case IMAGE_RECOMPRESSION_MALFORMED_SAMPLE_COUNT:
            case IMAGE_RECOMPRESSION_MALFORMED_CORRUPT_FILTER:
            case IMAGE_RECOMPRESSION_MALFORMED_DECODED_OVERSIZE:
                break;
            default:
                return 0;
            }
            xobjects.replaceKey("/BadImage", image);
        }
        resources.replaceKey("/XObject", xobjects);
        page.replaceKey("/Resources", resources);
        write_fixture(*pdf, output_path);
        return 1;
    } catch (...) {
        return 0;
    }
}
