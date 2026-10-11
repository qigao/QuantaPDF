#include "internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void quantapdf_dispose_text_page(quantapdf_text_page *text)
{
    if (text == NULL)
        return;

    free(text->blocks);
    free(text->lines);
    free(text->spans);
    free(text->chars);
    free(text->strings);
    free(text);
}

quantapdf_status quantapdf_extract_structured_text(
    quantapdf_page *page,
    quantapdf_text_page **out_text)
{
    if (out_text == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_text = NULL;

    if (page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return page->document->ops->extract_structured_text(
        page->backend, out_text);
}

quantapdf_status quantapdf_text_block_count(
    const quantapdf_text_page *text,
    size_t *out_count)
{
    if (out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = 0;
    if (text == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_count = text->block_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_text_get_block_info(
    const quantapdf_text_page *text,
    size_t block_index,
    quantapdf_text_block_info *out_info)
{
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_text_block_info, bounds) +
        sizeof(out_info->bounds);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->bounds = (quantapdf_rect){ 0 };
    if (text == NULL || block_index >= text->block_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->bounds = text->blocks[block_index].bounds;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_text_line_count(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t *out_count)
{
    if (out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = 0;
    if (text == NULL || block_index >= text->block_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_count = text->blocks[block_index].line_count;
    return QUANTAPDF_OK;
}

static const quantapdf_text_line_internal *quantapdf_lookup_line(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index)
{
    const quantapdf_text_block_internal *block;

    if (text == NULL || block_index >= text->block_count)
        return NULL;
    block = &text->blocks[block_index];
    if (line_index >= block->line_count)
        return NULL;
    return &text->lines[block->first_line + line_index];
}

quantapdf_status quantapdf_text_get_line_info(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    quantapdf_text_line_info *out_info)
{
    const quantapdf_text_line_internal *line;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_text_line_info, writing_mode) +
        sizeof(out_info->writing_mode);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->bounds = (quantapdf_rect){ 0 };
    out_info->direction_x = 0.0f;
    out_info->direction_y = 0.0f;
    out_info->writing_mode = 0;

    line = quantapdf_lookup_line(text, block_index, line_index);
    if (line == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->bounds = line->bounds;
    out_info->direction_x = line->direction_x;
    out_info->direction_y = line->direction_y;
    out_info->writing_mode = line->writing_mode;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_text_span_count(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t *out_count)
{
    const quantapdf_text_line_internal *line;

    if (out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = 0;

    line = quantapdf_lookup_line(text, block_index, line_index);
    if (line == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_count = line->span_count;
    return QUANTAPDF_OK;
}

static const quantapdf_text_span_internal *quantapdf_lookup_span(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index)
{
    const quantapdf_text_line_internal *line;

    line = quantapdf_lookup_line(text, block_index, line_index);
    if (line == NULL || span_index >= line->span_count)
        return NULL;
    return &text->spans[line->first_span + span_index];
}

quantapdf_status quantapdf_text_get_span_info(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    quantapdf_text_span_info *out_info)
{
    const quantapdf_text_span_internal *span;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_text_span_info, bidi_level) +
        sizeof(out_info->bidi_level);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->bounds = (quantapdf_rect){ 0 };
    out_info->font_size = 0.0f;
    out_info->argb = 0;
    out_info->bidi_level = 0;

    span = quantapdf_lookup_span(text, block_index, line_index, span_index);
    if (span == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->bounds = span->bounds;
    out_info->font_size = span->font_size;
    out_info->argb = span->argb;
    out_info->bidi_level = span->bidi_level;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_text_span_text(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_text_span_internal *span;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (out_utf8 == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    span = quantapdf_lookup_span(text, block_index, line_index, span_index);
    if (span == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_utf8 = text->strings + span->text_offset;
    *out_size = span->text_size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_text_page(quantapdf_text_page *text)
{
    quantapdf_dispose_text_page(text);
}
