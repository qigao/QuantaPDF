#include "truetype.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct quantapdf_sfnt_table {
    size_t offset;
    size_t length;
    int present;
} quantapdf_sfnt_table;

static uint16_t read_u16(const unsigned char *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8u) | (uint16_t)data[1]);
}

static int16_t read_s16(const unsigned char *data)
{
    return (int16_t)read_u16(data);
}

static uint32_t read_u32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24u) |
        ((uint32_t)data[1] << 16u) |
        ((uint32_t)data[2] << 8u) |
        (uint32_t)data[3];
}

static int range_valid(size_t offset, size_t length, size_t size)
{
    return offset <= size && length <= size - offset;
}

static int tag_equal(const unsigned char *tag, const char expected[4])
{
    return tag[0] == (unsigned char)expected[0] &&
        tag[1] == (unsigned char)expected[1] &&
        tag[2] == (unsigned char)expected[2] &&
        tag[3] == (unsigned char)expected[3];
}

static quantapdf_status find_table(
    const unsigned char *data,
    size_t size,
    const char tag[4],
    quantapdf_sfnt_table *out_table)
{
    uint16_t count;
    size_t i;

    memset(out_table, 0, sizeof(*out_table));
    if (data == NULL || size < 12u)
        return QUANTAPDF_ERROR_FORMAT;
    count = read_u16(data + 4u);
    if ((size_t)count > (size - 12u) / 16u)
        return QUANTAPDF_ERROR_FORMAT;

    for (i = 0u; i < (size_t)count; ++i) {
        const unsigned char *entry = data + 12u + i * 16u;
        size_t offset;
        size_t length;

        if (!tag_equal(entry, tag))
            continue;
        offset = (size_t)read_u32(entry + 8u);
        length = (size_t)read_u32(entry + 12u);
        if (!range_valid(offset, length, size))
            return QUANTAPDF_ERROR_FORMAT;
        out_table->offset = offset;
        out_table->length = length;
        out_table->present = 1;
        return QUANTAPDF_OK;
    }
    return QUANTAPDF_ERROR_FORMAT;
}

static int cmap_format4_valid(
    const unsigned char *subtable,
    size_t length)
{
    uint16_t declared;
    uint16_t seg_count_x2;
    size_t seg_count;
    size_t required;

    if (length < 16u || read_u16(subtable) != 4u)
        return 0;
    declared = read_u16(subtable + 2u);
    if ((size_t)declared > length || declared < 16u)
        return 0;
    seg_count_x2 = read_u16(subtable + 6u);
    if (seg_count_x2 == 0u || (seg_count_x2 & 1u) != 0u)
        return 0;
    seg_count = (size_t)seg_count_x2 / 2u;
    if (seg_count > (SIZE_MAX - 16u) / 8u)
        return 0;
    required = 16u + seg_count * 8u;
    return required <= (size_t)declared;
}

static int cmap_format12_valid(
    const unsigned char *subtable,
    size_t length)
{
    uint32_t declared;
    uint32_t groups;
    size_t required;

    if (length < 16u || read_u16(subtable) != 12u)
        return 0;
    declared = read_u32(subtable + 4u);
    if ((size_t)declared > length || declared < 16u)
        return 0;
    groups = read_u32(subtable + 12u);
    if ((size_t)groups > (SIZE_MAX - 16u) / 12u)
        return 0;
    required = 16u + (size_t)groups * 12u;
    return required <= (size_t)declared;
}

static int cmap_candidate_score(uint16_t platform, uint16_t encoding, uint16_t format)
{
    if (format == 12u && platform == 3u && encoding == 10u)
        return 60;
    if (format == 12u && platform == 0u)
        return 50;
    if (format == 4u && platform == 3u && encoding == 1u)
        return 40;
    if (format == 4u && platform == 0u)
        return 30;
    if (format == 4u && platform == 3u && encoding == 0u)
        return 20;
    return 0;
}

static quantapdf_status select_cmap(
    const unsigned char *data,
    size_t size,
    const quantapdf_sfnt_table *cmap,
    quantapdf_truetype_info *out_info)
{
    const unsigned char *table;
    uint16_t count;
    size_t i;
    int best_score = 0;

    if (cmap->length < 4u)
        return QUANTAPDF_ERROR_FORMAT;
    table = data + cmap->offset;
    if (read_u16(table) != 0u)
        return QUANTAPDF_ERROR_FORMAT;
    count = read_u16(table + 2u);
    if ((size_t)count > (cmap->length - 4u) / 8u)
        return QUANTAPDF_ERROR_FORMAT;

    for (i = 0u; i < (size_t)count; ++i) {
        const unsigned char *record = table + 4u + i * 8u;
        uint16_t platform = read_u16(record);
        uint16_t encoding = read_u16(record + 2u);
        uint32_t relative = read_u32(record + 4u);
        const unsigned char *subtable;
        size_t available;
        uint16_t format;
        size_t declared;
        int score;

        if ((size_t)relative >= cmap->length)
            continue;
        available = cmap->length - (size_t)relative;
        if (available < 2u)
            continue;
        subtable = table + relative;
        format = read_u16(subtable);
        score = cmap_candidate_score(platform, encoding, format);
        if (score <= best_score)
            continue;

        if (format == 4u) {
            if (!cmap_format4_valid(subtable, available))
                continue;
            declared = (size_t)read_u16(subtable + 2u);
        } else if (format == 12u) {
            if (!cmap_format12_valid(subtable, available))
                continue;
            declared = (size_t)read_u32(subtable + 4u);
        } else {
            continue;
        }
        out_info->cmap_format = format;
        out_info->cmap_offset = cmap->offset + (size_t)relative;
        out_info->cmap_length = declared;
        best_score = score;
    }
    return best_score == 0 ? QUANTAPDF_ERROR_UNSUPPORTED : QUANTAPDF_OK;
}

quantapdf_status quantapdf_truetype_inspect(
    const unsigned char *data,
    size_t size,
    quantapdf_truetype_info *out_info)
{
    quantapdf_sfnt_table head;
    quantapdf_sfnt_table hhea;
    quantapdf_sfnt_table maxp;
    quantapdf_sfnt_table hmtx;
    quantapdf_sfnt_table cmap;
    quantapdf_sfnt_table loca;
    quantapdf_sfnt_table glyf;
    quantapdf_status status;
    uint32_t signature;
    uint16_t index_to_loca;
    size_t required_loca;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_info, 0, sizeof(*out_info));
    if (data == NULL || size < 12u)
        return QUANTAPDF_ERROR_FORMAT;

    signature = read_u32(data);
    if (signature != UINT32_C(0x00010000) &&
        signature != UINT32_C(0x74727565))
        return QUANTAPDF_ERROR_UNSUPPORTED;

#define FIND_REQUIRED(name, literal)                                           \
    do {                                                                       \
        status = find_table(data, size, literal, &name);                       \
        if (status != QUANTAPDF_OK)                                            \
            return status;                                                     \
    } while (0)

    FIND_REQUIRED(head, "head");
    FIND_REQUIRED(hhea, "hhea");
    FIND_REQUIRED(maxp, "maxp");
    FIND_REQUIRED(hmtx, "hmtx");
    FIND_REQUIRED(cmap, "cmap");
    FIND_REQUIRED(loca, "loca");
    FIND_REQUIRED(glyf, "glyf");
#undef FIND_REQUIRED

    (void)glyf;
    if (head.length < 54u || hhea.length < 36u || maxp.length < 6u)
        return QUANTAPDF_ERROR_FORMAT;
    if (read_u32(data + head.offset + 12u) != UINT32_C(0x5f0f3cf5))
        return QUANTAPDF_ERROR_FORMAT;

    out_info->units_per_em = read_u16(data + head.offset + 18u);
    out_info->x_min = read_s16(data + head.offset + 36u);
    out_info->y_min = read_s16(data + head.offset + 38u);
    out_info->x_max = read_s16(data + head.offset + 40u);
    out_info->y_max = read_s16(data + head.offset + 42u);
    index_to_loca = read_u16(data + head.offset + 50u);
    out_info->ascender = read_s16(data + hhea.offset + 4u);
    out_info->descender = read_s16(data + hhea.offset + 6u);
    out_info->num_hmetrics = read_u16(data + hhea.offset + 34u);
    out_info->num_glyphs = read_u16(data + maxp.offset + 4u);

    if (out_info->units_per_em < 16u ||
        out_info->units_per_em > 16384u ||
        out_info->num_glyphs == 0u ||
        out_info->num_hmetrics == 0u ||
        out_info->num_hmetrics > out_info->num_glyphs ||
        out_info->x_max < out_info->x_min ||
        out_info->y_max < out_info->y_min)
        return QUANTAPDF_ERROR_FORMAT;

    if ((size_t)out_info->num_hmetrics >
        SIZE_MAX / 4u)
        return QUANTAPDF_ERROR_FORMAT;
    {
        size_t required_hmtx = (size_t)out_info->num_hmetrics * 4u;
        size_t trailing =
            (size_t)(out_info->num_glyphs - out_info->num_hmetrics) * 2u;
        if (required_hmtx > SIZE_MAX - trailing)
            return QUANTAPDF_ERROR_FORMAT;
        required_hmtx += trailing;
        if (hmtx.length < required_hmtx)
            return QUANTAPDF_ERROR_FORMAT;
    }

    if (index_to_loca == 0u) {
        required_loca = ((size_t)out_info->num_glyphs + 1u) * 2u;
    } else if (index_to_loca == 1u) {
        required_loca = ((size_t)out_info->num_glyphs + 1u) * 4u;
    } else {
        return QUANTAPDF_ERROR_FORMAT;
    }
    if (loca.length < required_loca)
        return QUANTAPDF_ERROR_FORMAT;

    out_info->hmtx_offset = hmtx.offset;
    out_info->hmtx_length = hmtx.length;
    return select_cmap(data, size, &cmap, out_info);
}

static quantapdf_status glyph_format12(
    const unsigned char *subtable,
    size_t length,
    uint32_t codepoint,
    uint16_t num_glyphs,
    uint16_t *out_glyph)
{
    uint32_t groups;
    size_t low = 0u;
    size_t high;

    if (!cmap_format12_valid(subtable, length))
        return QUANTAPDF_ERROR_FORMAT;
    groups = read_u32(subtable + 12u);
    high = (size_t)groups;
    while (low < high) {
        size_t mid = low + (high - low) / 2u;
        const unsigned char *group = subtable + 16u + mid * 12u;
        uint32_t start = read_u32(group);
        uint32_t end = read_u32(group + 4u);

        if (codepoint < start) {
            high = mid;
        } else if (codepoint > end) {
            low = mid + 1u;
        } else {
            uint32_t start_glyph = read_u32(group + 8u);
            uint64_t glyph =
                (uint64_t)start_glyph + (uint64_t)(codepoint - start);
            if (glyph == 0u || glyph >= num_glyphs || glyph > UINT16_MAX)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            *out_glyph = (uint16_t)glyph;
            return QUANTAPDF_OK;
        }
    }
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

static quantapdf_status glyph_format4(
    const unsigned char *subtable,
    size_t length,
    uint32_t codepoint,
    uint16_t num_glyphs,
    uint16_t *out_glyph)
{
    uint16_t seg_count_x2;
    size_t seg_count;
    const unsigned char *end_count;
    const unsigned char *start_count;
    const unsigned char *id_delta;
    const unsigned char *id_range_offset;
    size_t i;

    if (codepoint > UINT16_MAX ||
        !cmap_format4_valid(subtable, length))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    seg_count_x2 = read_u16(subtable + 6u);
    seg_count = (size_t)seg_count_x2 / 2u;
    end_count = subtable + 14u;
    start_count = end_count + seg_count * 2u + 2u;
    id_delta = start_count + seg_count * 2u;
    id_range_offset = id_delta + seg_count * 2u;

    for (i = 0u; i < seg_count; ++i) {
        uint16_t end = read_u16(end_count + i * 2u);
        uint16_t start;
        uint16_t range_offset;
        uint16_t glyph;

        if (codepoint > (uint32_t)end)
            continue;
        start = read_u16(start_count + i * 2u);
        if (codepoint < (uint32_t)start)
            return QUANTAPDF_ERROR_UNSUPPORTED;

        range_offset = read_u16(id_range_offset + i * 2u);
        if (range_offset == 0u) {
            int16_t delta = read_s16(id_delta + i * 2u);
            glyph = (uint16_t)((uint32_t)codepoint + (uint16_t)delta);
        } else {
            size_t word_offset =
                (size_t)(id_range_offset - subtable) + i * 2u;
            size_t glyph_offset =
                word_offset + (size_t)range_offset +
                ((size_t)codepoint - (size_t)start) * 2u;
            int16_t delta;

            if (!range_valid(glyph_offset, 2u, length))
                return QUANTAPDF_ERROR_FORMAT;
            glyph = read_u16(subtable + glyph_offset);
            if (glyph == 0u)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            delta = read_s16(id_delta + i * 2u);
            glyph = (uint16_t)((uint32_t)glyph + (uint16_t)delta);
        }
        if (glyph == 0u || glyph >= num_glyphs)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        *out_glyph = glyph;
        return QUANTAPDF_OK;
    }
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

quantapdf_status quantapdf_truetype_glyph(
    const unsigned char *data,
    size_t size,
    const quantapdf_truetype_info *info,
    uint32_t codepoint,
    uint16_t *out_glyph)
{
    const unsigned char *subtable;

    if (out_glyph == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_glyph = 0u;
    if (data == NULL || info == NULL ||
        !range_valid(info->cmap_offset, info->cmap_length, size))
        return QUANTAPDF_ERROR_ARGUMENT;
    subtable = data + info->cmap_offset;
    if (info->cmap_format == 12u)
        return glyph_format12(
            subtable, info->cmap_length, codepoint,
            info->num_glyphs, out_glyph);
    if (info->cmap_format == 4u)
        return glyph_format4(
            subtable, info->cmap_length, codepoint,
            info->num_glyphs, out_glyph);
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

quantapdf_status quantapdf_truetype_advance(
    const unsigned char *data,
    size_t size,
    const quantapdf_truetype_info *info,
    uint16_t glyph,
    uint16_t *out_advance)
{
    size_t metric_index;
    size_t offset;

    if (out_advance == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_advance = 0u;
    if (data == NULL || info == NULL ||
        glyph >= info->num_glyphs ||
        info->num_hmetrics == 0u ||
        !range_valid(info->hmtx_offset, info->hmtx_length, size))
        return QUANTAPDF_ERROR_ARGUMENT;

    metric_index = glyph < info->num_hmetrics
        ? (size_t)glyph
        : (size_t)info->num_hmetrics - 1u;
    if (metric_index > SIZE_MAX / 4u)
        return QUANTAPDF_ERROR_FORMAT;
    offset = info->hmtx_offset + metric_index * 4u;
    if (!range_valid(offset, 2u, size))
        return QUANTAPDF_ERROR_FORMAT;
    *out_advance = read_u16(data + offset);
    return QUANTAPDF_OK;
}
