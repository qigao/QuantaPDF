#ifndef QUANTAPDF_TEXT_SNAPSHOT_H
#define QUANTAPDF_TEXT_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#include <quantapdf/quantapdf.h>

typedef struct quantapdf_text_block_internal {
    quantapdf_rect bounds;
    size_t first_line;
    size_t line_count;
} quantapdf_text_block_internal;

typedef struct quantapdf_text_line_internal {
    quantapdf_rect bounds;
    float direction_x;
    float direction_y;
    int writing_mode;
    size_t first_span;
    size_t span_count;
} quantapdf_text_line_internal;

typedef struct quantapdf_text_span_internal {
    quantapdf_rect bounds;
    float font_size;
    uint32_t argb;
    uint32_t bidi_level;
    uint16_t flags;
    size_t first_char;
    size_t char_count;
    size_t text_offset;
    size_t text_size;
} quantapdf_text_span_internal;

typedef struct quantapdf_text_char_internal {
    uint32_t codepoint;
    uint16_t bidi;
    uint16_t flags;
    quantapdf_quad quad;
    size_t span_index;
} quantapdf_text_char_internal;

struct quantapdf_text_page {
    quantapdf_context *context;
    quantapdf_text_block_internal *blocks;
    quantapdf_text_line_internal *lines;
    quantapdf_text_span_internal *spans;
    quantapdf_text_char_internal *chars;
    char *strings;
    size_t block_count;
    size_t line_count;
    size_t span_count;
    size_t char_count;
    size_t string_size;
};

#endif
