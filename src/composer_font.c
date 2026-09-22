#include "internal.h"
#include "backend/ttf_font.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int quantapdf_embedded_text_rect_valid(const quantapdf_rect *rect)
{
    return rect != NULL &&
        isfinite(rect->x0) && isfinite(rect->y0) &&
        isfinite(rect->x1) && isfinite(rect->y1) &&
        rect->x1 > rect->x0 && rect->y1 > rect->y0;
}

static quantapdf_status quantapdf_composer_reserve_font(
    quantapdf_composer *composer)
{
    quantapdf_composer_font_state *grown;
    size_t capacity;

    if (composer->font_count >= (size_t)UINT32_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->font_count < composer->font_capacity)
        return QUANTAPDF_OK;
    capacity = composer->font_capacity == 0u
        ? 4u
        : composer->font_capacity * 2u;
    if (capacity < composer->font_capacity ||
        capacity > (size_t)UINT32_MAX ||
        capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_font_state *)realloc(
        composer->fonts, capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->fonts = grown;
    composer->font_capacity = capacity;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_font(
    quantapdf_composer *composer,
    const unsigned char *font_data,
    size_t font_size,
    const quantapdf_composer_font_options *options,
    quantapdf_composer_font_id *out_font_id)
{
    quantapdf_composer_font_state font = {0};
    quantapdf_status status;
    size_t i;

    if (out_font_id != NULL)
        *out_font_id = 0u;
    if (composer == NULL || font_data == NULL || font_size == 0u ||
        options == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_MIN_SIZE ||
        out_font_id == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_ttf_validate(font_data, font_size);
    if (status != QUANTAPDF_OK)
        return status;

    for (i = 0u; i < composer->font_count; ++i) {
        if (composer->fonts[i].size == font_size &&
            memcmp(composer->fonts[i].data, font_data, font_size) == 0) {
            *out_font_id = (quantapdf_composer_font_id)(i + 1u);
            return QUANTAPDF_OK;
        }
    }

    if (composer->resource_bytes > composer->max_resource_bytes ||
        font_size > composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status = quantapdf_composer_reserve_font(composer);
    if (status != QUANTAPDF_OK)
        return status;

    font.data = (unsigned char *)malloc(font_size);
    if (font.data == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(font.data, font_data, font_size);
    font.size = font_size;

    composer->fonts[composer->font_count] = font;
    ++composer->font_count;
    composer->resource_bytes += font_size;
    *out_font_id = (quantapdf_composer_font_id)composer->font_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_draw_embedded_text(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_composer_embedded_text_options *options)
{
    quantapdf_composer_operation operation;
    const quantapdf_composer_font_state *font;
    quantapdf_status status;
    size_t text_size;

    if (composer == NULL || text_utf8 == NULL || options == NULL ||
        options->struct_size <
            QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_MIN_SIZE ||
        page_index >= composer->page_count ||
        options->font_id == 0u ||
        (size_t)options->font_id > composer->font_count ||
        !quantapdf_embedded_text_rect_valid(bounds) ||
        !isfinite(options->font_size) || options->font_size <= 0.0f ||
        (options->argb >> 24u) != 0xffu ||
        !isfinite(options->line_height_multiplier) ||
        options->line_height_multiplier <= 0.0f ||
        options->alignment < QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT ||
        options->alignment > QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT ||
        (options->wrap != 0 && options->wrap != 1))
        return QUANTAPDF_ERROR_ARGUMENT;

    font = &composer->fonts[options->font_id - 1u];
    status = quantapdf_ttf_validate_text(
        font->data, font->size, text_utf8);
    if (status != QUANTAPDF_OK)
        return status;

    text_size = strlen(text_utf8) + 1u;
    if (composer->resource_bytes > composer->max_resource_bytes ||
        text_size > composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    memset(&operation, 0, sizeof(operation));
    operation.value.embedded_text.text_utf8 = (char *)malloc(text_size);
    if (operation.value.embedded_text.text_utf8 == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(operation.value.embedded_text.text_utf8, text_utf8, text_size);

    status = quantapdf_composer_reserve_operation_internal(composer);
    if (status != QUANTAPDF_OK) {
        free(operation.value.embedded_text.text_utf8);
        return status;
    }
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_EMBEDDED_TEXT;
    operation.page_index = page_index;
    operation.bounds = *bounds;
    operation.value.embedded_text.options = *options;
    composer->operations[composer->operation_count++] = operation;
    composer->resource_bytes += text_size;
    return QUANTAPDF_OK;
}
