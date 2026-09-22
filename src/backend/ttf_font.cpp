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


void put16(std::vector<unsigned char>& bytes, size_t at, uint16_t value)
{
    bytes[at] = static_cast<unsigned char>(value >> 8u);
    bytes[at + 1u] = static_cast<unsigned char>(value & 0xffu);
}

void put32(std::vector<unsigned char>& bytes, size_t at, uint32_t value)
{
    bytes[at] = static_cast<unsigned char>(value >> 24u);
    bytes[at + 1u] = static_cast<unsigned char>((value >> 16u) & 0xffu);
    bytes[at + 2u] = static_cast<unsigned char>((value >> 8u) & 0xffu);
    bytes[at + 3u] = static_cast<unsigned char>(value & 0xffu);
}

void append16(std::vector<unsigned char>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<unsigned char>(value >> 8u));
    bytes.push_back(static_cast<unsigned char>(value & 0xffu));
}

void append32(std::vector<unsigned char>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<unsigned char>(value >> 24u));
    bytes.push_back(static_cast<unsigned char>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<unsigned char>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<unsigned char>(value & 0xffu));
}

void pad4(std::vector<unsigned char>& bytes)
{
    while ((bytes.size() & 3u) != 0u)
        bytes.push_back(0u);
}

uint32_t checksum_bytes(std::vector<unsigned char> const& bytes)
{
    uint32_t sum = 0u;
    for (size_t at = 0u; at < bytes.size(); at += 4u) {
        uint32_t word = 0u;
        for (size_t j = 0u; j < 4u; ++j) {
            word <<= 8u;
            if (at + j < bytes.size())
                word |= bytes[at + j];
        }
        sum += word;
    }
    return sum;
}

bool read_table_directory(
    unsigned char const* data,
    size_t size,
    std::map<std::string, table_view>* out)
{
    if (data == nullptr || out == nullptr || size < 12u)
        return false;
    out->clear();
    uint16_t const count = be16(data + 4u);
    if (count == 0u || count > (size - 12u) / 16u)
        return false;
    for (uint16_t i = 0u; i < count; ++i) {
        size_t const at = 12u + static_cast<size_t>(i) * 16u;
        std::string tag(
            reinterpret_cast<char const*>(data + at), 4u);
        size_t const offset = be32(data + at + 8u);
        size_t const length = be32(data + at + 12u);
        if (offset > size || length > size - offset ||
            out->find(tag) != out->end())
            return false;
        (*out)[tag] = {offset, length, true};
    }
    return true;
}

bool read_loca_offsets(
    ttf_font_face const& face,
    std::map<std::string, table_view> const& tables,
    std::vector<uint32_t>* out)
{
    auto const head_it = tables.find("head");
    auto const loca_it = tables.find("loca");
    auto const glyf_it = tables.find("glyf");
    if (head_it == tables.end() || loca_it == tables.end() ||
        glyf_it == tables.end() || head_it->second.size < 54u)
        return false;
    int16_t const format =
        bes16(face.data + head_it->second.offset + 50u);
    size_t const count = static_cast<size_t>(face.num_glyphs) + 1u;
    out->assign(count, 0u);
    if (format == 0) {
        if (loca_it->second.size < count * 2u)
            return false;
        for (size_t i = 0u; i < count; ++i)
            (*out)[i] =
                static_cast<uint32_t>(
                    be16(face.data + loca_it->second.offset + i * 2u)) *
                2u;
    } else if (format == 1) {
        if (loca_it->second.size < count * 4u)
            return false;
        for (size_t i = 0u; i < count; ++i)
            (*out)[i] =
                be32(face.data + loca_it->second.offset + i * 4u);
    } else {
        return false;
    }
    uint32_t previous = 0u;
    for (uint32_t offset: *out) {
        if (offset < previous ||
            offset > glyf_it->second.size)
            return false;
        previous = offset;
    }
    return true;
}

quantapdf_status collect_subset_glyph(
    ttf_font_face const& face,
    table_view glyf,
    std::vector<uint32_t> const& loca,
    uint16_t glyph,
    size_t depth,
    std::vector<unsigned char>* state,
    std::vector<unsigned char>* included)
{
    if (glyph >= face.num_glyphs || depth > 64u)
        return QUANTAPDF_ERROR_FORMAT;
    if ((*state)[glyph] == 1u)
        return QUANTAPDF_ERROR_FORMAT;
    if ((*state)[glyph] == 2u) {
        (*included)[glyph] = 1u;
        return QUANTAPDF_OK;
    }

    (*state)[glyph] = 1u;
    (*included)[glyph] = 1u;
    uint32_t const start = loca[glyph];
    uint32_t const end = loca[static_cast<size_t>(glyph) + 1u];
    if (end < start || end > glyf.size) {
        (*state)[glyph] = 0u;
        return QUANTAPDF_ERROR_FORMAT;
    }
    size_t const length = static_cast<size_t>(end - start);
    if (length == 0u) {
        (*state)[glyph] = 2u;
        return QUANTAPDF_OK;
    }
    if (length < 10u) {
        (*state)[glyph] = 0u;
        return QUANTAPDF_ERROR_FORMAT;
    }

    auto const* bytes = face.data + glyf.offset + start;
    int16_t const contours = bes16(bytes);
    if (contours < 0) {
        constexpr uint16_t arg_words = 0x0001u;
        constexpr uint16_t have_scale = 0x0008u;
        constexpr uint16_t more_components = 0x0020u;
        constexpr uint16_t have_xy_scale = 0x0040u;
        constexpr uint16_t have_two_by_two = 0x0080u;
        constexpr uint16_t have_instructions = 0x0100u;
        size_t at = 10u;
        uint16_t flags = 0u;
        do {
            if (at > length || length - at < 4u) {
                (*state)[glyph] = 0u;
                return QUANTAPDF_ERROR_FORMAT;
            }
            flags = be16(bytes + at);
            uint16_t const component = be16(bytes + at + 2u);
            at += 4u;
            quantapdf_status const status = collect_subset_glyph(
                face,
                glyf,
                loca,
                component,
                depth + 1u,
                state,
                included);
            if (status != QUANTAPDF_OK) {
                (*state)[glyph] = 0u;
                return status;
            }
            size_t extra = (flags & arg_words) != 0u ? 4u : 2u;
            if ((flags & have_scale) != 0u)
                extra += 2u;
            else if ((flags & have_xy_scale) != 0u)
                extra += 4u;
            else if ((flags & have_two_by_two) != 0u)
                extra += 8u;
            if (extra > length - at) {
                (*state)[glyph] = 0u;
                return QUANTAPDF_ERROR_FORMAT;
            }
            at += extra;
        } while ((flags & more_components) != 0u);

        if ((flags & have_instructions) != 0u) {
            if (at > length || length - at < 2u) {
                (*state)[glyph] = 0u;
                return QUANTAPDF_ERROR_FORMAT;
            }
            uint16_t const instruction_length = be16(bytes + at);
            at += 2u;
            if (instruction_length > length - at) {
                (*state)[glyph] = 0u;
                return QUANTAPDF_ERROR_FORMAT;
            }
        }
    }

    (*state)[glyph] = 2u;
    return QUANTAPDF_OK;
}

uint16_t source_advance(
    ttf_font_face const& face,
    uint16_t glyph)
{
    uint16_t const metric = glyph < face.num_h_metrics
        ? glyph
        : static_cast<uint16_t>(face.num_h_metrics - 1u);
    return be16(
        face.data + face.hmtx_offset +
        static_cast<size_t>(metric) * 4u);
}

int16_t source_lsb(
    ttf_font_face const& face,
    uint16_t glyph)
{
    size_t at;
    if (glyph < face.num_h_metrics) {
        at = face.hmtx_offset + static_cast<size_t>(glyph) * 4u + 2u;
    } else {
        at = face.hmtx_offset +
            static_cast<size_t>(face.num_h_metrics) * 4u +
            static_cast<size_t>(glyph - face.num_h_metrics) * 2u;
    }
    return bes16(face.data + at);
}

std::vector<unsigned char> build_subset_cmap(
    std::map<uint16_t, uint32_t> const& used_glyphs,
    uint16_t glyph_count)
{
    std::map<uint32_t, uint16_t> by_codepoint;
    for (auto const& entry: used_glyphs) {
        if (entry.first < glyph_count && entry.second <= 0x10ffffu)
            by_codepoint.emplace(entry.second, entry.first);
    }

    std::vector<std::pair<uint16_t, uint16_t>> bmp;
    for (auto const& entry: by_codepoint) {
        if (entry.first <= 0xfffeu)
            bmp.emplace_back(
                static_cast<uint16_t>(entry.first), entry.second);
    }

    std::vector<unsigned char> format4;
    size_t const seg_count = bmp.size() + 1u;
    bool const can_format4 =
        seg_count <= 0x7fffu &&
        16u + seg_count * 8u <= 0xffffu;
    if (can_format4) {
        size_t const length = 16u + seg_count * 8u;
        format4.assign(length, 0u);
        put16(format4, 0u, 4u);
        put16(format4, 2u, static_cast<uint16_t>(length));
        put16(format4, 4u, 0u);
        put16(
            format4, 6u,
            static_cast<uint16_t>(seg_count * 2u));
        size_t power = 1u;
        uint16_t selector = 0u;
        while (power * 2u <= seg_count) {
            power *= 2u;
            ++selector;
        }
        put16(
            format4, 8u,
            static_cast<uint16_t>(power * 2u));
        put16(format4, 10u, selector);
        put16(
            format4, 12u,
            static_cast<uint16_t>(seg_count * 2u - power * 2u));

        size_t const end_at = 14u;
        size_t const start_at = end_at + seg_count * 2u + 2u;
        size_t const delta_at = start_at + seg_count * 2u;
        size_t const range_at = delta_at + seg_count * 2u;
        for (size_t i = 0u; i < bmp.size(); ++i) {
            put16(format4, end_at + i * 2u, bmp[i].first);
            put16(format4, start_at + i * 2u, bmp[i].first);
            put16(
                format4,
                delta_at + i * 2u,
                static_cast<uint16_t>(
                    bmp[i].second - bmp[i].first));
        }
        size_t const sentinel = bmp.size();
        put16(format4, end_at + sentinel * 2u, 0xffffu);
        put16(format4, start_at + sentinel * 2u, 0xffffu);
        put16(format4, delta_at + sentinel * 2u, 1u);
        for (size_t i = 0u; i < seg_count; ++i)
            put16(format4, range_at + i * 2u, 0u);
    }

    struct group {
        uint32_t first;
        uint32_t last;
        uint32_t glyph;
    };
    std::vector<group> groups;
    for (auto const& entry: by_codepoint) {
        if (!groups.empty()) {
            group& last = groups.back();
            uint64_t const expected_glyph =
                static_cast<uint64_t>(last.glyph) +
                static_cast<uint64_t>(entry.first - last.first);
            if (entry.first == last.last + 1u &&
                expected_glyph == entry.second) {
                last.last = entry.first;
                continue;
            }
        }
        groups.push_back(
            {entry.first, entry.first, entry.second});
    }

    std::vector<unsigned char> format12;
    format12.reserve(16u + groups.size() * 12u);
    append16(format12, 12u);
    append16(format12, 0u);
    append32(
        format12,
        static_cast<uint32_t>(16u + groups.size() * 12u));
    append32(format12, 0u);
    append32(format12, static_cast<uint32_t>(groups.size()));
    for (auto const& item: groups) {
        append32(format12, item.first);
        append32(format12, item.last);
        append32(format12, item.glyph);
    }

    uint16_t const record_count =
        static_cast<uint16_t>(can_format4 ? 2u : 1u);
    size_t const header_size = 4u + static_cast<size_t>(record_count) * 8u;
    std::vector<unsigned char> cmap(header_size, 0u);
    put16(cmap, 0u, 0u);
    put16(cmap, 2u, record_count);
    size_t offset = header_size;
    size_t record = 0u;
    if (can_format4) {
        put16(cmap, 4u + record * 8u, 3u);
        put16(cmap, 6u + record * 8u, 1u);
        put32(
            cmap, 8u + record * 8u,
            static_cast<uint32_t>(offset));
        cmap.insert(cmap.end(), format4.begin(), format4.end());
        offset += format4.size();
        ++record;
    }
    while ((offset & 3u) != 0u) {
        cmap.push_back(0u);
        ++offset;
    }
    put16(cmap, 4u + record * 8u, 3u);
    put16(cmap, 6u + record * 8u, 10u);
    put32(
        cmap, 8u + record * 8u,
        static_cast<uint32_t>(offset));
    cmap.insert(cmap.end(), format12.begin(), format12.end());
    return cmap;
}

bool copy_subset_safe_table(std::string const& tag)
{
    return tag == "OS/2" || tag == "name" || tag == "cvt " ||
        tag == "fpgm" || tag == "prep" || tag == "gasp";
}

quantapdf_status build_subset_sfnt(
    ttf_font_face const& face,
    std::map<uint16_t, uint32_t> const& used_glyphs,
    std::vector<unsigned char>* out)
{
    std::map<std::string, table_view> source_tables;
    if (!read_table_directory(
            face.data, face.size, &source_tables))
        return QUANTAPDF_ERROR_FORMAT;

    auto const head_it = source_tables.find("head");
    auto const hhea_it = source_tables.find("hhea");
    auto const hmtx_it = source_tables.find("hmtx");
    auto const maxp_it = source_tables.find("maxp");
    auto const loca_it = source_tables.find("loca");
    auto const glyf_it = source_tables.find("glyf");
    if (head_it == source_tables.end() ||
        hhea_it == source_tables.end() ||
        hmtx_it == source_tables.end() ||
        maxp_it == source_tables.end() ||
        loca_it == source_tables.end() ||
        glyf_it == source_tables.end())
        return QUANTAPDF_ERROR_UNSUPPORTED;

    std::vector<uint32_t> source_loca;
    if (!read_loca_offsets(face, source_tables, &source_loca))
        return QUANTAPDF_ERROR_FORMAT;

    std::vector<unsigned char> state(face.num_glyphs, 0u);
    std::vector<unsigned char> included(face.num_glyphs, 0u);
    quantapdf_status status = collect_subset_glyph(
        face,
        glyf_it->second,
        source_loca,
        0u,
        0u,
        &state,
        &included);
    if (status != QUANTAPDF_OK)
        return status;
    for (auto const& entry: used_glyphs) {
        if (entry.first >= face.num_glyphs)
            return QUANTAPDF_ERROR_FORMAT;
        status = collect_subset_glyph(
            face,
            glyf_it->second,
            source_loca,
            entry.first,
            0u,
            &state,
            &included);
        if (status != QUANTAPDF_OK)
            return status;
    }

    uint16_t max_glyph = 0u;
    for (uint32_t glyph = 0u; glyph < face.num_glyphs; ++glyph) {
        if (included[glyph] != 0u)
            max_glyph = static_cast<uint16_t>(glyph);
    }
    uint16_t const glyph_count =
        static_cast<uint16_t>(max_glyph + 1u);
    if (glyph_count == 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    std::vector<unsigned char> glyf;
    std::vector<uint32_t> loca(
        static_cast<size_t>(glyph_count) + 1u, 0u);
    for (uint32_t glyph = 0u; glyph < glyph_count; ++glyph) {
        loca[glyph] = static_cast<uint32_t>(glyf.size());
        if (included[glyph] == 0u)
            continue;
        uint32_t const start = source_loca[glyph];
        uint32_t const end = source_loca[glyph + 1u];
        if (end < start || end > glyf_it->second.size)
            return QUANTAPDF_ERROR_FORMAT;
        auto const* begin =
            face.data + glyf_it->second.offset + start;
        glyf.insert(glyf.end(), begin, begin + (end - start));
        pad4(glyf);
        if (glyf.size() > std::numeric_limits<uint32_t>::max())
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    loca[glyph_count] = static_cast<uint32_t>(glyf.size());

    std::vector<unsigned char> loca_table;
    loca_table.reserve(loca.size() * 4u);
    for (uint32_t offset: loca)
        append32(loca_table, offset);

    std::vector<unsigned char> hmtx;
    hmtx.reserve(static_cast<size_t>(glyph_count) * 4u);
    for (uint32_t glyph = 0u; glyph < glyph_count; ++glyph) {
        append16(
            hmtx,
            source_advance(face, static_cast<uint16_t>(glyph)));
        append16(
            hmtx,
            static_cast<uint16_t>(
                source_lsb(face, static_cast<uint16_t>(glyph))));
    }

    std::map<std::string, std::vector<unsigned char>> tables;
    for (auto const& entry: source_tables) {
        if (!copy_subset_safe_table(entry.first))
            continue;
        auto const* begin = face.data + entry.second.offset;
        tables[entry.first] = std::vector<unsigned char>(
            begin, begin + entry.second.size);
    }

    tables["glyf"] = std::move(glyf);
    tables["loca"] = std::move(loca_table);
    tables["hmtx"] = std::move(hmtx);
    tables["cmap"] = build_subset_cmap(used_glyphs, glyph_count);

    {
        auto const& source = head_it->second;
        auto const* begin = face.data + source.offset;
        std::vector<unsigned char> head(
            begin, begin + source.size);
        if (head.size() < 54u)
            return QUANTAPDF_ERROR_FORMAT;
        put32(head, 8u, 0u);
        put16(head, 50u, 1u);
        tables["head"] = std::move(head);
    }
    {
        auto const& source = hhea_it->second;
        auto const* begin = face.data + source.offset;
        std::vector<unsigned char> hhea(
            begin, begin + source.size);
        if (hhea.size() < 36u)
            return QUANTAPDF_ERROR_FORMAT;
        put16(hhea, 34u, glyph_count);
        tables["hhea"] = std::move(hhea);
    }
    {
        auto const& source = maxp_it->second;
        auto const* begin = face.data + source.offset;
        std::vector<unsigned char> maxp(
            begin, begin + source.size);
        if (maxp.size() < 6u)
            return QUANTAPDF_ERROR_FORMAT;
        put16(maxp, 4u, glyph_count);
        tables["maxp"] = std::move(maxp);
    }
    {
        std::vector<unsigned char> post(32u, 0u);
        auto const post_it = source_tables.find("post");
        if (post_it != source_tables.end()) {
            size_t const copy =
                std::min<size_t>(32u, post_it->second.size);
            std::memcpy(
                post.data(),
                face.data + post_it->second.offset,
                copy);
        }
        put32(post, 0u, 0x00030000u);
        tables["post"] = std::move(post);
    }

    if (tables.size() > std::numeric_limits<uint16_t>::max())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    uint16_t const table_count =
        static_cast<uint16_t>(tables.size());
    if (table_count == 0u)
        return QUANTAPDF_ERROR_FORMAT;

    size_t power = 1u;
    uint16_t selector = 0u;
    while (power * 2u <= table_count) {
        power *= 2u;
        ++selector;
    }
    size_t const directory_size =
        12u + static_cast<size_t>(table_count) * 16u;
    out->assign(directory_size, 0u);
    put32(*out, 0u, be32(face.data));
    put16(*out, 4u, table_count);
    put16(
        *out, 6u,
        static_cast<uint16_t>(power * 16u));
    put16(*out, 8u, selector);
    put16(
        *out, 10u,
        static_cast<uint16_t>(
            static_cast<size_t>(table_count) * 16u -
            power * 16u));

    size_t record_index = 0u;
    size_t head_output_offset = 0u;
    for (auto const& entry: tables) {
        pad4(*out);
        if (out->size() >
            std::numeric_limits<uint32_t>::max())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        uint32_t const offset =
            static_cast<uint32_t>(out->size());
        uint32_t const length =
            static_cast<uint32_t>(entry.second.size());
        uint32_t const checksum =
            checksum_bytes(entry.second);

        size_t const record_at =
            12u + record_index * 16u;
        std::memcpy(
            out->data() + record_at,
            entry.first.data(),
            4u);
        put32(*out, record_at + 4u, checksum);
        put32(*out, record_at + 8u, offset);
        put32(*out, record_at + 12u, length);
        out->insert(
            out->end(),
            entry.second.begin(),
            entry.second.end());
        if (entry.first == "head")
            head_output_offset = offset;
        ++record_index;
    }
    pad4(*out);
    if (head_output_offset == 0u ||
        head_output_offset > out->size() ||
        out->size() - head_output_offset < 12u)
        return QUANTAPDF_ERROR_FORMAT;

    uint32_t const sum = checksum_bytes(*out);
    uint32_t const adjustment = 0xb1b0afbau - sum;
    put32(*out, head_output_offset + 8u, adjustment);
    if (checksum_bytes(*out) != 0xb1b0afbAu)
        return QUANTAPDF_ERROR_BACKEND;
    return QUANTAPDF_OK;
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
    bool const true_type_sfnt =
        sfnt == 0x00010000u || sfnt == 0x74727565u;
    bool const open_type_sfnt = sfnt == 0x4f54544fu;
    if (!true_type_sfnt && !open_type_sfnt)
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
    table_view cff;
    table_view cff2;
    if (!find_table(bytes, byte_count, "head", &head) ||
        !find_table(bytes, byte_count, "hhea", &hhea) ||
        !find_table(bytes, byte_count, "hmtx", &hmtx) ||
        !find_table(bytes, byte_count, "cmap", &cmap) ||
        !find_table(bytes, byte_count, "maxp", &maxp) ||
        !find_table(bytes, byte_count, "name", &name) ||
        !find_table(bytes, byte_count, "OS/2", &os2) ||
        !find_table(bytes, byte_count, "post", &post) ||
        !find_table(bytes, byte_count, "glyf", &glyf) ||
        !find_table(bytes, byte_count, "loca", &loca) ||
        !find_table(bytes, byte_count, "CFF ", &cff) ||
        !find_table(bytes, byte_count, "CFF2", &cff2))
        return QUANTAPDF_ERROR_FORMAT;
    if (!head.found || head.size < 54u ||
        !hhea.found || hhea.size < 36u ||
        !hmtx.found || !cmap.found ||
        !maxp.found || maxp.size < 6u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    sfnt_outline_kind outline_kind;
    if (true_type_sfnt) {
        if (!glyf.found || !loca.found || cff.found || cff2.found)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        outline_kind = sfnt_outline_kind::true_type;
    } else {
        if (glyf.found || loca.found || cff.found == cff2.found)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (cff.found) {
            if (cff.size < 4u ||
                bytes[cff.offset] != 1u ||
                bytes[cff.offset + 2u] < 4u ||
                bytes[cff.offset + 2u] > cff.size ||
                bytes[cff.offset + 3u] < 1u ||
                bytes[cff.offset + 3u] > 4u)
                return QUANTAPDF_ERROR_FORMAT;
            outline_kind = sfnt_outline_kind::cff;
        } else {
            if (cff2.size < 5u ||
                bytes[cff2.offset] != 2u ||
                bytes[cff2.offset + 2u] < 5u ||
                bytes[cff2.offset + 2u] > cff2.size)
                return QUANTAPDF_ERROR_FORMAT;
            size_t const top_dict_length =
                be16(bytes + cff2.offset + 3u);
            if (top_dict_length >
                cff2.size - bytes[cff2.offset + 2u])
                return QUANTAPDF_ERROR_FORMAT;
            outline_kind = sfnt_outline_kind::cff2;
        }
    }

    ttf_font_face result;
    result.data = bytes;
    result.outline_kind = outline_kind;
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


quantapdf_status subset_true_type_font(
    ttf_font_face const& face,
    std::map<uint16_t, uint32_t> const& used_glyphs,
    std::vector<unsigned char>* out_font)
{
    if (out_font == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (face.outline_kind != sfnt_outline_kind::true_type)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    out_font->clear();
    try {
        return build_subset_sfnt(face, used_glyphs, out_font);
    } catch (std::bad_alloc const&) {
        out_font->clear();
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        out_font->clear();
        return QUANTAPDF_ERROR_BACKEND;
    }
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


extern "C" quantapdf_status quantapdf_ttf_glyph_count(
    unsigned char const* data,
    size_t size,
    uint32_t* out_glyph_count)
{
    if (out_glyph_count == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_glyph_count = 0u;
    try {
        quantapdf::detail::ttf_font_face face;
        quantapdf_status const status =
            quantapdf::detail::ttf_font_face::parse(data, size, &face);
        if (status != QUANTAPDF_OK)
            return status;
        *out_glyph_count = face.num_glyphs;
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
