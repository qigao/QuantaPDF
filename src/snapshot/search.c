#include "snapshot/text_snapshot.h"

#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int quantapdf_is_continuation(unsigned char byte)
{
    return (byte & 0xc0u) == 0x80u;
}

static int quantapdf_decode_one_utf8(
    const unsigned char *data,
    size_t remaining,
    uint32_t *out_codepoint,
    size_t *out_size)
{
    unsigned char b0;

    if (remaining == 0)
        return 0;

    b0 = data[0];
    if (b0 <= 0x7fu) {
        *out_codepoint = b0;
        *out_size = 1;
        return 1;
    }

    if (b0 >= 0xc2u && b0 <= 0xdfu) {
        if (remaining < 2 || !quantapdf_is_continuation(data[1]))
            return 0;
        *out_codepoint = ((uint32_t)(b0 & 0x1fu) << 6) |
            (uint32_t)(data[1] & 0x3fu);
        *out_size = 2;
        return 1;
    }

    if (b0 >= 0xe0u && b0 <= 0xefu) {
        unsigned char b1;
        unsigned char b2;

        if (remaining < 3)
            return 0;
        b1 = data[1];
        b2 = data[2];
        if (!quantapdf_is_continuation(b1) ||
            !quantapdf_is_continuation(b2))
            return 0;
        if (b0 == 0xe0u && b1 < 0xa0u)
            return 0;
        if (b0 == 0xedu && b1 >= 0xa0u)
            return 0;

        *out_codepoint = ((uint32_t)(b0 & 0x0fu) << 12) |
            ((uint32_t)(b1 & 0x3fu) << 6) |
            (uint32_t)(b2 & 0x3fu);
        *out_size = 3;
        return 1;
    }

    if (b0 >= 0xf0u && b0 <= 0xf4u) {
        unsigned char b1;
        unsigned char b2;
        unsigned char b3;

        if (remaining < 4)
            return 0;
        b1 = data[1];
        b2 = data[2];
        b3 = data[3];
        if (!quantapdf_is_continuation(b1) ||
            !quantapdf_is_continuation(b2) ||
            !quantapdf_is_continuation(b3))
            return 0;
        if (b0 == 0xf0u && b1 < 0x90u)
            return 0;
        if (b0 == 0xf4u && b1 >= 0x90u)
            return 0;

        *out_codepoint = ((uint32_t)(b0 & 0x07u) << 18) |
            ((uint32_t)(b1 & 0x3fu) << 12) |
            ((uint32_t)(b2 & 0x3fu) << 6) |
            (uint32_t)(b3 & 0x3fu);
        *out_size = 4;
        return 1;
    }

    return 0;
}

static quantapdf_status quantapdf_decode_search_needle(
    const char *needle_utf8,
    uint32_t **out_codepoints,
    size_t *out_count)
{
    const unsigned char *bytes;
    uint32_t *codepoints;
    size_t byte_count;
    size_t byte_index = 0;
    size_t codepoint_count = 0;

    *out_codepoints = NULL;
    *out_count = 0;

    if (needle_utf8 == NULL || needle_utf8[0] == '\0')
        return QUANTAPDF_ERROR_ARGUMENT;

    byte_count = strlen(needle_utf8);
    if (byte_count > SIZE_MAX / sizeof(*codepoints))
        return QUANTAPDF_ERROR_NOMEM;

    codepoints = (uint32_t *)malloc(byte_count * sizeof(*codepoints));
    if (codepoints == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    bytes = (const unsigned char *)needle_utf8;
    while (byte_index < byte_count) {
        uint32_t codepoint;
        size_t encoded_size;

        if (!quantapdf_decode_one_utf8(
                bytes + byte_index,
                byte_count - byte_index,
                &codepoint,
                &encoded_size)) {
            free(codepoints);
            return QUANTAPDF_ERROR_ARGUMENT;
        }

        codepoints[codepoint_count++] = codepoint;
        byte_index += encoded_size;
    }

    *out_codepoints = codepoints;
    *out_count = codepoint_count;
    return QUANTAPDF_OK;
}

static void quantapdf_line_char_range(
    const quantapdf_text_page *text,
    const quantapdf_text_line_internal *line,
    size_t *out_first_char,
    size_t *out_char_count)
{
    const quantapdf_text_span_internal *first_span;
    const quantapdf_text_span_internal *last_span;
    size_t end_char;

    if (line->span_count == 0) {
        *out_first_char = 0;
        *out_char_count = 0;
        return;
    }

    first_span = &text->spans[line->first_span];
    last_span = &text->spans[line->first_span + line->span_count - 1];
    end_char = last_span->first_char + last_span->char_count;

    *out_first_char = first_span->first_char;
    *out_char_count = end_char - first_span->first_char;
}

static int quantapdf_chars_match(
    const quantapdf_text_page *text,
    size_t first_char,
    const uint32_t *needle,
    size_t needle_count)
{
    size_t i;

    for (i = 0; i < needle_count; ++i) {
        if (text->chars[first_char + i].codepoint != needle[i])
            return 0;
    }
    return 1;
}

static quantapdf_status quantapdf_count_search_results(
    const quantapdf_text_page *text,
    const uint32_t *needle,
    size_t needle_count,
    size_t *out_required)
{
    size_t required = 0;
    size_t line_index;

    for (line_index = 0; line_index < text->line_count; ++line_index) {
        const quantapdf_text_line_internal *line = &text->lines[line_index];
        size_t first_char;
        size_t char_count;
        size_t offset;

        quantapdf_line_char_range(text, line, &first_char, &char_count);
        if (needle_count > char_count)
            continue;

        for (offset = 0; offset <= char_count - needle_count; ++offset) {
            if (!quantapdf_chars_match(
                    text, first_char + offset, needle, needle_count))
                continue;
            if (required == SIZE_MAX)
                return QUANTAPDF_ERROR_NOMEM;
            ++required;
        }
    }

    *out_required = required;
    return QUANTAPDF_OK;
}

static float quantapdf_project_point(quantapdf_point point, float x, float y)
{
    return point.x * x + point.y * y;
}

static void quantapdf_include_quad_projection(
    quantapdf_quad quad,
    float dx,
    float dy,
    float nx,
    float ny,
    float *min_u,
    float *max_u,
    float *min_v,
    float *max_v)
{
    quantapdf_point points[4];
    size_t i;

    points[0] = quad.ul;
    points[1] = quad.ur;
    points[2] = quad.ll;
    points[3] = quad.lr;

    for (i = 0; i < 4; ++i) {
        float u = quantapdf_project_point(points[i], dx, dy);
        float v = quantapdf_project_point(points[i], nx, ny);

        if (u < *min_u)
            *min_u = u;
        if (u > *max_u)
            *max_u = u;
        if (v < *min_v)
            *min_v = v;
        if (v > *max_v)
            *max_v = v;
    }
}

static quantapdf_point quantapdf_point_from_basis(
    float u,
    float v,
    float dx,
    float dy,
    float nx,
    float ny)
{
    quantapdf_point point;

    point.x = dx * u + nx * v;
    point.y = dy * u + ny * v;
    return point;
}

static void quantapdf_make_search_quad(
    const quantapdf_text_page *text,
    const quantapdf_text_line_internal *line,
    size_t first_char,
    size_t char_count,
    quantapdf_quad *out_quad)
{
    float dx = line->direction_x;
    float dy = line->direction_y;
    float nx;
    float ny;
    float min_u = FLT_MAX;
    float max_u = -FLT_MAX;
    float min_v = FLT_MAX;
    float max_v = -FLT_MAX;
    size_t i;

    if (dx == 0.0f && dy == 0.0f) {
        dx = 1.0f;
        dy = 0.0f;
    }
    nx = -dy;
    ny = dx;

    for (i = 0; i < char_count; ++i) {
        quantapdf_include_quad_projection(
            text->chars[first_char + i].quad,
            dx,
            dy,
            nx,
            ny,
            &min_u,
            &max_u,
            &min_v,
            &max_v);
    }

    out_quad->ul = quantapdf_point_from_basis(
        min_u, min_v, dx, dy, nx, ny);
    out_quad->ur = quantapdf_point_from_basis(
        max_u, min_v, dx, dy, nx, ny);
    out_quad->ll = quantapdf_point_from_basis(
        min_u, max_v, dx, dy, nx, ny);
    out_quad->lr = quantapdf_point_from_basis(
        max_u, max_v, dx, dy, nx, ny);
}

static void quantapdf_fill_search_results(
    const quantapdf_text_page *text,
    const uint32_t *needle,
    size_t needle_count,
    quantapdf_search_result *results)
{
    size_t result_index = 0;
    size_t line_index;

    for (line_index = 0; line_index < text->line_count; ++line_index) {
        const quantapdf_text_line_internal *line = &text->lines[line_index];
        size_t first_char;
        size_t char_count;
        size_t offset;

        quantapdf_line_char_range(text, line, &first_char, &char_count);
        if (needle_count > char_count)
            continue;

        for (offset = 0; offset <= char_count - needle_count; ++offset) {
            if (!quantapdf_chars_match(
                    text, first_char + offset, needle, needle_count))
                continue;

            quantapdf_make_search_quad(
                text,
                line,
                first_char + offset,
                needle_count,
                &results[result_index].quad);
            ++result_index;
        }
    }
}

quantapdf_status quantapdf_text_search(
    const quantapdf_text_page *text,
    const char *needle_utf8,
    quantapdf_search_result *results,
    size_t capacity,
    size_t *out_count)
{
    uint32_t *needle = NULL;
    size_t needle_count = 0;
    size_t required = 0;
    const size_t minimum_size = QUANTAPDF_SEARCH_RESULT_V1_MIN_SIZE;
    const size_t element_size = QUANTAPDF_SEARCH_RESULT_V1_SIZE;
    size_t i;
    quantapdf_status status;

    if (out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = 0;

    if (text == NULL || needle_utf8 == NULL || needle_utf8[0] == '\0')
        return QUANTAPDF_ERROR_ARGUMENT;
    if (results == NULL && capacity != 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_decode_search_needle(
        needle_utf8, &needle, &needle_count);
    if (status != QUANTAPDF_OK)
        return status;

    status = quantapdf_count_search_results(
        text, needle, needle_count, &required);
    if (status != QUANTAPDF_OK) {
        free(needle);
        return status;
    }

    *out_count = required;
    if (results == NULL) {
        free(needle);
        return QUANTAPDF_OK;
    }

    if (capacity < required) {
        free(needle);
        return QUANTAPDF_ERROR_ARGUMENT;
    }

    for (i = 0; i < required; ++i) {
        if (results[i].struct_size < minimum_size ||
            results[i].struct_size > element_size) {
            free(needle);
            return QUANTAPDF_ERROR_ARGUMENT;
        }
    }

    quantapdf_fill_search_results(text, needle, needle_count, results);
    free(needle);
    return QUANTAPDF_OK;
}
