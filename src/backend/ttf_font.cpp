#include "ttf_font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace quantapdf::detail {
namespace {

uint16_t be16(unsigned char const* p)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(p[0]) << 8u) |
        static_cast<uint16_t>(p[1]));
}

int16_t bes16(unsigned char const* p)
{
    return static_cast<int16_t>(be16(p));
}

uint32_t be32(unsigned char const* p)
{
    return (static_cast<uint32_t>(p[0]) << 24u) |
        (static_cast<uint32_t>(p[1]) << 16u) |
        (static_cast<uint32_t>(p[2]) << 8u) |
        static_cast<uint32_t>(p[3]);
}

int32_t bes32(unsigned char const* p)
{
    return static_cast<int32_t>(be32(p));
}

struct table_view {
    size_t offset = 0u;
    size_t size = 0u;
    bool found = false;
};

bool find_table(
    unsigned char const* data,
    size_t size,
    char const tag[4],
    table_view* out)
{
    if (data == nullptr || out == nullptr || size < 12u)
        return false;
    uint16_t const count = be16(data + 4u);
    if (count > (size - 12u) / 16u)
        return false;
    for (uint16_t i = 0u; i < count; ++i) {
        size_t const at = 12u + static_cast<size_t>(i) * 16u;
        auto const* entry = data + at;
        if (std::memcmp(entry, tag, 4u) != 0)
            continue;
        size_t const offset = be32(entry + 8u);
        size_t const length = be32(entry + 12u);
        if (offset > size || length > size - offset)
            return false;
        out->offset = offset;
        out->size = length;
        out->found = true;
        return true;
    }
    return true;
}

bool select_cmap(
    unsigned char const* data,
    table_view cmap,
    size_t* out_offset,
    size_t* out_size,
    uint16_t* out_format)
{
    if (!cmap.found || cmap.size < 4u)
        return false;
    auto const* table = data + cmap.offset;
    uint16_t const count = be16(table + 2u);
    if (count > (cmap.size - 4u) / 8u)
        return false;

    int best_priority = -1;
    size_t best_offset = 0u;
    size_t best_size = 0u;
    uint16_t best_format = 0u;
    for (uint16_t i = 0u; i < count; ++i) {
        size_t const rec_at = 4u + static_cast<size_t>(i) * 8u;
        auto const* rec = table + rec_at;
        uint16_t const platform = be16(rec);
        uint16_t const encoding = be16(rec + 2u);
        size_t const relative = be32(rec + 4u);
        if (relative > cmap.size || cmap.size - relative < 4u)
            continue;
        auto const* sub = table + relative;
        uint16_t const format = be16(sub);
        size_t length = 0u;
        if (format == 4u) {
            length = be16(sub + 2u);
            if (length < 16u)
                continue;
        } else if (format == 12u) {
            if (cmap.size - relative < 16u)
                continue;
            length = be32(sub + 4u);
            if (length < 16u)
                continue;
        } else {
            continue;
        }
        if (length > cmap.size - relative)
            continue;

        int priority = -1;
        if (format == 12u && platform == 3u && encoding == 10u)
            priority = 50;
        else if (format == 12u && platform == 0u)
            priority = 40;
        else if (format == 4u && platform == 3u && encoding == 1u)
            priority = 30;
        else if (format == 4u && platform == 0u)
            priority = 20;
        if (priority <= best_priority)
            continue;
        best_priority = priority;
        best_offset = cmap.offset + relative;
        best_size = length;
        best_format = format;
    }
    if (best_priority < 0)
        return false;
    *out_offset = best_offset;
    *out_size = best_size;
    *out_format = best_format;
    return true;
}

std::string extract_postscript_name(
    unsigned char const* data,
    table_view name)
{
    if (!name.found || name.size < 6u)
        return "QuantaPDFFont";
    auto const* table = data + name.offset;
    uint16_t const count = be16(table + 2u);
    uint16_t const string_offset = be16(table + 4u);
    if (count > (name.size - 6u) / 12u || string_offset > name.size)
        return "QuantaPDFFont";

    std::string fallback;
    for (uint16_t i = 0u; i < count; ++i) {
        auto const* rec = table + 6u + static_cast<size_t>(i) * 12u;
        uint16_t const platform = be16(rec);
        uint16_t const name_id = be16(rec + 6u);
        size_t const length = be16(rec + 8u);
        size_t const offset = be16(rec + 10u);
        if (name_id != 6u || offset > name.size - string_offset ||
            length > name.size - string_offset - offset)
            continue;
        auto const* source = table + string_offset + offset;
        std::string candidate;
        if (platform == 0u || platform == 3u) {
            for (size_t j = 0u; j + 1u < length; j += 2u) {
                uint16_t const cp = be16(source + j);
                if (cp >= 33u && cp <= 126u && cp != '#' &&
                    cp != '%' && cp != '(' && cp != ')' &&
                    cp != '/' && cp != '<' && cp != '>' &&
                    cp != '[' && cp != ']' && cp != '{' &&
                    cp != '}')
                    candidate.push_back(static_cast<char>(cp));
                else if (cp == ' ')
                    candidate.push_back('-');
            }
        } else if (platform == 1u) {
            for (size_t j = 0u; j < length; ++j) {
                unsigned char const ch = source[j];
                if (ch >= 33u && ch <= 126u && ch != '#' &&
                    ch != '%' && ch != '(' && ch != ')' &&
                    ch != '/' && ch != '<' && ch != '>' &&
                    ch != '[' && ch != ']' && ch != '{' &&
                    ch != '}')
                    candidate.push_back(static_cast<char>(ch));
                else if (ch == ' ')
                    candidate.push_back('-');
            }
        }
        if (!candidate.empty())
            return candidate;
        if (fallback.empty())
            fallback = candidate;
    }
    return fallback.empty() ? "QuantaPDFFont" : fallback;
}

uint16_t glyph_format4(
    unsigned char const* sub,
    size_t size,
    uint32_t codepoint)
{
    if (codepoint > 0xffffu || size < 16u)
        return 0u;
    uint16_t const seg_count_x2 = be16(sub + 6u);
    if ((seg_count_x2 & 1u) != 0u)
        return 0u;
    uint16_t const seg_count = seg_count_x2 / 2u;
    size_t const end_at = 14u;
    size_t const start_at = end_at + seg_count_x2 + 2u;
    size_t const delta_at = start_at + seg_count_x2;
    size_t const range_at = delta_at + seg_count_x2;
    if (range_at > size || seg_count_x2 > size - range_at)
        return 0u;

    for (uint16_t i = 0u; i < seg_count; ++i) {
        uint16_t const end = be16(sub + end_at + static_cast<size_t>(i) * 2u);
        if (codepoint > end)
            continue;
        uint16_t const start =
            be16(sub + start_at + static_cast<size_t>(i) * 2u);
        if (codepoint < start)
            return 0u;
        int16_t const delta =
            bes16(sub + delta_at + static_cast<size_t>(i) * 2u);
        uint16_t const range =
            be16(sub + range_at + static_cast<size_t>(i) * 2u);
        if (range == 0u) {
            return static_cast<uint16_t>(
                (codepoint + static_cast<uint16_t>(delta)) & 0xffffu);
        }
        size_t const range_pos =
            range_at + static_cast<size_t>(i) * 2u;
        size_t const glyph_pos =
            range_pos + range +
            static_cast<size_t>(codepoint - start) * 2u;
        if (glyph_pos > size || size - glyph_pos < 2u)
            return 0u;
        uint16_t glyph = be16(sub + glyph_pos);
        if (glyph == 0u)
            return 0u;
        glyph = static_cast<uint16_t>(
            (glyph + static_cast<uint16_t>(delta)) & 0xffffu);
        return glyph;
    }
    return 0u;
}

uint16_t glyph_format12(
    unsigned char const* sub,
    size_t size,
    uint32_t codepoint)
{
    if (size < 16u)
        return 0u;
    uint32_t const groups = be32(sub + 12u);
    if (groups > (size - 16u) / 12u)
        return 0u;
    size_t low = 0u;
    size_t high = groups;
    while (low < high) {
        size_t const mid = low + (high - low) / 2u;
        auto const* group = sub + 16u + mid * 12u;
        uint32_t const first = be32(group);
        uint32_t const last = be32(group + 4u);
        if (codepoint < first) {
            high = mid;
        } else if (codepoint > last) {
            low = mid + 1u;
        } else {
            uint64_t const glyph =
                static_cast<uint64_t>(be32(group + 8u)) +
                static_cast<uint64_t>(codepoint - first);
            return glyph <= 0xffffu
                ? static_cast<uint16_t>(glyph)
                : 0u;
        }
    }
    return 0u;
}

} // namespace

quantapdf_status ttf_font_face::parse(
    unsigned char const* bytes,
    size_t byte_count,
    ttf_font_face* out)
{
    if (bytes == nullptr || out == nullptr || byte_count < 12u)
        return QUANTAPDF_ERROR_FORMAT;
    uint32_t const sfnt = be32(bytes);
    if (sfnt != 0x00010000u && sfnt != 0x74727565u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    table_view head;
    table_view hhea;
    table_view hmtx;
    table_view cmap;
    table_view maxp;
    table_view name;
    table_view os2;
    table_view post;
    table_view glyf;
    table_view loca;
    if (!find_table(bytes, byte_count, "head", &head) ||
        !find_table(bytes, byte_count, "hhea", &hhea) ||
        !find_table(bytes, byte_count, "hmtx", &hmtx) ||
        !find_table(bytes, byte_count, "cmap", &cmap) ||
        !find_table(bytes, byte_count, "maxp", &maxp) ||
        !find_table(bytes, byte_count, "name", &name) ||
        !find_table(bytes, byte_count, "OS/2", &os2) ||
        !find_table(bytes, byte_count, "post", &post) ||
        !find_table(bytes, byte_count, "glyf", &glyf) ||
        !find_table(bytes, byte_count, "loca", &loca))
        return QUANTAPDF_ERROR_FORMAT;
    if (!head.found || head.size < 54u ||
        !hhea.found || hhea.size < 36u ||
        !hmtx.found || !cmap.found ||
        !maxp.found || maxp.size < 6u ||
        !glyf.found || !loca.found)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    ttf_font_face result;
    result.data = bytes;
    result.size = byte_count;
    result.units_per_em = be16(bytes + head.offset + 18u);
    result.num_glyphs = be16(bytes + maxp.offset + 4u);
    result.num_h_metrics = be16(bytes + hhea.offset + 34u);
    if (result.units_per_em < 16u || result.units_per_em > 16384u ||
        result.num_glyphs == 0u || result.num_h_metrics == 0u ||
        result.num_h_metrics > result.num_glyphs)
        return QUANTAPDF_ERROR_FORMAT;

    size_t const required_hmtx =
        static_cast<size_t>(result.num_h_metrics) * 4u +
        static_cast<size_t>(result.num_glyphs - result.num_h_metrics) * 2u;
    if (hmtx.size < required_hmtx)
        return QUANTAPDF_ERROR_FORMAT;
    result.hmtx_offset = hmtx.offset;
    result.hmtx_size = hmtx.size;

    if (!select_cmap(
            bytes, cmap, &result.cmap_offset, &result.cmap_size,
            &result.cmap_format))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    result.bbox[0] = bes16(bytes + head.offset + 36u);
    result.bbox[1] = bes16(bytes + head.offset + 38u);
    result.bbox[2] = bes16(bytes + head.offset + 40u);
    result.bbox[3] = bes16(bytes + head.offset + 42u);
    uint16_t const mac_style = be16(bytes + head.offset + 44u);
    int16_t const hhea_ascent = bes16(bytes + hhea.offset + 4u);
    int16_t const hhea_descent = bes16(bytes + hhea.offset + 6u);

    int16_t ascent = hhea_ascent;
    int16_t descent = hhea_descent;
    int16_t cap_height = 0;
    uint16_t fs_selection = 0u;
    uint16_t weight_class = 400u;
    if (os2.found && os2.size >= 78u) {
        weight_class = be16(bytes + os2.offset + 4u);
        fs_selection = be16(bytes + os2.offset + 62u);
        int16_t const typo_ascent = bes16(bytes + os2.offset + 68u);
        int16_t const typo_descent = bes16(bytes + os2.offset + 70u);
        if (typo_ascent != 0)
            ascent = typo_ascent;
        if (typo_descent != 0)
            descent = typo_descent;
        uint16_t const version = be16(bytes + os2.offset);
        if (version >= 2u && os2.size >= 90u)
            cap_height = bes16(bytes + os2.offset + 88u);
    }
    result.ascent =
        static_cast<double>(ascent) * 1000.0 / result.units_per_em;
    result.descent =
        static_cast<double>(descent) * 1000.0 / result.units_per_em;
    result.cap_height = cap_height != 0
        ? static_cast<double>(cap_height) * 1000.0 / result.units_per_em
        : result.ascent * 0.7;

    if (post.found && post.size >= 12u) {
        result.italic_angle =
            static_cast<double>(bes32(bytes + post.offset + 4u)) / 65536.0;
        if (post.size >= 16u && be32(bytes + post.offset + 12u) != 0u)
            result.flags |= 1;
    }
    if ((mac_style & 2u) != 0u || (fs_selection & 1u) != 0u ||
        result.italic_angle != 0.0)
        result.flags |= 64;
    result.stem_v = weight_class >= 700u ? 120.0 : 80.0;
    result.postscript_name = extract_postscript_name(bytes, name);

    *out = std::move(result);
    return QUANTAPDF_OK;
}

uint16_t ttf_font_face::glyph_for(uint32_t codepoint) const
{
    if (cmap_offset > size || cmap_size > size - cmap_offset)
        return 0u;
    auto const* sub = data + cmap_offset;
    uint16_t glyph = cmap_format == 12u
        ? glyph_format12(sub, cmap_size, codepoint)
        : glyph_format4(sub, cmap_size, codepoint);
    return glyph < num_glyphs ? glyph : 0u;
}

int ttf_font_face::width_for(uint16_t glyph) const
{
    if (glyph >= num_glyphs || hmtx_offset > size ||
        hmtx_size > size - hmtx_offset)
        return 0;
    uint16_t const metric =
        glyph < num_h_metrics ? glyph : static_cast<uint16_t>(num_h_metrics - 1u);
    size_t const at = hmtx_offset + static_cast<size_t>(metric) * 4u;
    if (at > size || size - at < 2u)
        return 0;
    uint16_t const advance = be16(data + at);
    return static_cast<int>(
        (static_cast<uint64_t>(advance) * 1000u + units_per_em / 2u) /
        units_per_em);
}

quantapdf_status decode_utf8_codepoints(
    char const* text,
    std::vector<uint32_t>* out)
{
    if (text == nullptr || out == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    out->clear();
    auto cursor = reinterpret_cast<unsigned char const*>(text);
    try {
        while (*cursor != 0u) {
            uint32_t codepoint;
            size_t count;
            if (*cursor < 0x80u) {
                codepoint = *cursor;
                count = 1u;
            } else if (*cursor >= 0xc2u && *cursor <= 0xdfu) {
                codepoint = *cursor & 0x1fu;
                count = 2u;
            } else if (*cursor >= 0xe0u && *cursor <= 0xefu) {
                codepoint = *cursor & 0x0fu;
                count = 3u;
            } else if (*cursor >= 0xf0u && *cursor <= 0xf4u) {
                codepoint = *cursor & 0x07u;
                count = 4u;
            } else {
                return QUANTAPDF_ERROR_FORMAT;
            }
            for (size_t i = 1u; i < count; ++i) {
                if (cursor[i] == 0u || (cursor[i] & 0xc0u) != 0x80u)
                    return QUANTAPDF_ERROR_FORMAT;
                codepoint =
                    (codepoint << 6u) | (cursor[i] & 0x3fu);
            }
            if ((count == 2u && codepoint < 0x80u) ||
                (count == 3u && codepoint < 0x800u) ||
                (count == 4u && codepoint < 0x10000u) ||
                (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
                codepoint > 0x10ffffu)
                return QUANTAPDF_ERROR_FORMAT;
            out->push_back(codepoint);
            cursor += count;
        }
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        out->clear();
        return QUANTAPDF_ERROR_NOMEM;
    }
}

} // namespace quantapdf::detail

extern "C" quantapdf_status quantapdf_ttf_validate(
    unsigned char const* data,
    size_t size)
{
    try {
        quantapdf::detail::ttf_font_face face;
        return quantapdf::detail::ttf_font_face::parse(data, size, &face);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_ttf_validate_text(
    unsigned char const* data,
    size_t size,
    char const* text_utf8)
{
    try {
        quantapdf::detail::ttf_font_face face;
        quantapdf_status status =
            quantapdf::detail::ttf_font_face::parse(data, size, &face);
        if (status != QUANTAPDF_OK)
            return status;
        std::vector<uint32_t> codepoints;
        status =
            quantapdf::detail::decode_utf8_codepoints(text_utf8, &codepoints);
        if (status != QUANTAPDF_OK)
            return status;
        for (uint32_t cp: codepoints) {
            if (cp == '\n' || cp == '\r' || cp == '\t')
                continue;
            if (cp < 0x20u)
                return QUANTAPDF_ERROR_FORMAT;
            uint32_t const mapped = cp == '\t' ? 0x20u : cp;
            if (face.glyph_for(mapped) == 0u)
                return QUANTAPDF_ERROR_FORMAT;
        }
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
