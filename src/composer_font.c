#include "internal.h"
#include "backend/ttf_font.h"
#include "backend/composer_text_layout.h"

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


static int quantapdf_embedded_measurement_prepare(
    quantapdf_composer_text_measurement *out_measurement)
{
    if (out_measurement == NULL ||
        out_measurement->struct_size <
            QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_MIN_SIZE)
        return 0;
    out_measurement->width = 0.0f;
    out_measurement->height = 0.0f;
    out_measurement->line_count = 0u;
    return 1;
}


static quantapdf_composer_graphics_state_id
quantapdf_embedded_graphics_state_id(
    const quantapdf_composer_embedded_text_options *options)
{
    if (options != NULL &&
        options->struct_size >=
            QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V2_MIN_SIZE)
        return options->graphics_state_id;
    return 0u;
}

static quantapdf_composer_graphics_state_id
quantapdf_glyph_graphics_state_id(
    const quantapdf_composer_glyph_run_options *options)
{
    if (options != NULL &&
        options->struct_size >=
            QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V2_MIN_SIZE)
        return options->graphics_state_id;
    return 0u;
}

static void quantapdf_copy_embedded_options(
    quantapdf_composer_embedded_text_options *destination,
    const quantapdf_composer_embedded_text_options *source)
{
    memset(destination, 0, sizeof(*destination));
    destination->struct_size =
        QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V2_SIZE;
    destination->font_id = source->font_id;
    destination->font_size = source->font_size;
    destination->argb = source->argb;
    destination->line_height_multiplier = source->line_height_multiplier;
    destination->alignment = source->alignment;
    destination->wrap = source->wrap;
    destination->graphics_state_id =
        quantapdf_embedded_graphics_state_id(source);
}

static void quantapdf_copy_glyph_options(
    quantapdf_composer_glyph_run_options *destination,
    const quantapdf_composer_glyph_run_options *source)
{
    memset(destination, 0, sizeof(*destination));
    destination->struct_size =
        QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V2_SIZE;
    destination->font_id = source->font_id;
    destination->font_size = source->font_size;
    destination->argb = source->argb;
    destination->graphics_state_id =
        quantapdf_glyph_graphics_state_id(source);
}


static int quantapdf_glyph_run_utf8_valid(
    const unsigned char *data,
    size_t size)
{
    size_t offset = 0u;

    if (size != 0u && data == NULL)
        return 0;
    while (offset < size) {
        uint32_t codepoint;
        size_t count;
        size_t i;
        unsigned char first = data[offset];

        if (first < 0x80u) {
            codepoint = first;
            count = 1u;
        } else if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = (uint32_t)(first & 0x1fu);
            count = 2u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = (uint32_t)(first & 0x0fu);
            count = 3u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = (uint32_t)(first & 0x07u);
            count = 4u;
        } else {
            return 0;
        }
        if (count > size - offset)
            return 0;
        for (i = 1u; i < count; ++i) {
            if ((data[offset + i] & 0xc0u) != 0x80u)
                return 0;
            codepoint =
                (codepoint << 6u) |
                (uint32_t)(data[offset + i] & 0x3fu);
        }
        if ((count == 2u && codepoint < 0x80u) ||
            (count == 3u && codepoint < 0x800u) ||
            (count == 4u && codepoint < 0x10000u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
            codepoint > 0x10ffffu)
            return 0;
        offset += count;
    }
    return 1;
}

static int quantapdf_glyph_run_utf8_boundary(
    const unsigned char *data,
    size_t size,
    size_t offset)
{
    if (offset == 0u || offset == size)
        return 1;
    if (data == NULL || offset > size)
        return 0;
    return (data[offset] & 0xc0u) != 0x80u;
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

quantapdf_status quantapdf_composer_measure_embedded_text(
    const quantapdf_composer *composer,
    const char *text_utf8,
    float max_width,
    const quantapdf_composer_embedded_text_options *options,
    quantapdf_composer_text_measurement *out_measurement)
{
    const quantapdf_composer_font_state *font;
    quantapdf_status status;

    if (!quantapdf_embedded_measurement_prepare(out_measurement))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (composer == NULL || text_utf8 == NULL || options == NULL ||
        options->struct_size <
            QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_MIN_SIZE ||
        options->font_id == 0u ||
        (size_t)options->font_id > composer->font_count ||
        !isfinite(max_width) || max_width <= 0.0f ||
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

    return quantapdf_measure_embedded_text_internal(
        font->data,
        font->size,
        text_utf8,
        max_width,
        options,
        out_measurement);
}

static quantapdf_status quantapdf_composer_draw_embedded_text_internal(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_affine_transform *transform,
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
        !quantapdf_affine_transform_valid_internal(transform) ||
        !isfinite(options->font_size) || options->font_size <= 0.0f ||
        (options->argb >> 24u) != 0xffu ||
        !isfinite(options->line_height_multiplier) ||
        options->line_height_multiplier <= 0.0f ||
        options->alignment < QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT ||
        options->alignment > QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT ||
        (options->wrap != 0 && options->wrap != 1) ||
        !quantapdf_graphics_state_id_valid_internal(
            composer, quantapdf_embedded_graphics_state_id(options)))
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
    operation.graphics_state_id =
        quantapdf_embedded_graphics_state_id(options);
    quantapdf_copy_embedded_options(
        &operation.value.embedded_text.options, options);
    operation.value.embedded_text.transform = *transform;
    composer->operations[composer->operation_count++] = operation;
    composer->resource_bytes += text_size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_draw_embedded_text(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_composer_embedded_text_options *options)
{
    quantapdf_affine_transform const transform =
        quantapdf_affine_identity_internal();
    return quantapdf_composer_draw_embedded_text_internal(
        composer, page_index, text_utf8, bounds, &transform, options);
}

quantapdf_status quantapdf_composer_draw_embedded_text_transformed(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_embedded_text_options *options)
{
    return quantapdf_composer_draw_embedded_text_internal(
        composer, page_index, text_utf8, bounds, transform, options);
}

static quantapdf_status quantapdf_composer_draw_glyph_run_internal(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_point origin,
    const quantapdf_composer_glyph *glyphs,
    size_t glyph_count,
    const char *unicode_utf8,
    size_t unicode_size,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_glyph_run_options *options)
{
    quantapdf_composer_operation operation;
    quantapdf_composer_glyph *glyph_copy = NULL;
    char *unicode_copy = NULL;
    const quantapdf_composer_font_state *font;
    const unsigned char *unicode_bytes =
        (const unsigned char *)unicode_utf8;
    quantapdf_status status;
    uint32_t font_glyph_count = 0u;
    size_t glyph_bytes;
    size_t resource_bytes;
    size_t i;

    if (composer == NULL || options == NULL ||
        options->struct_size <
            QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_MIN_SIZE ||
        page_index >= composer->page_count ||
        options->font_id == 0u ||
        (size_t)options->font_id > composer->font_count ||
        !isfinite(origin.x) || !isfinite(origin.y) ||
        !quantapdf_affine_transform_valid_internal(transform) ||
        !isfinite(options->font_size) || options->font_size <= 0.0f ||
        (options->argb >> 24u) != 0xffu ||
        !quantapdf_graphics_state_id_valid_internal(
            composer, quantapdf_glyph_graphics_state_id(options)) ||
        glyphs == NULL || glyph_count == 0u ||
        (unicode_size != 0u && unicode_utf8 == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (unicode_size > (size_t)UINT32_MAX ||
        glyph_count > SIZE_MAX / sizeof(*glyphs))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (!quantapdf_glyph_run_utf8_valid(unicode_bytes, unicode_size))
        return QUANTAPDF_ERROR_FORMAT;

    font = &composer->fonts[options->font_id - 1u];
    status = quantapdf_ttf_glyph_count(
        font->data, font->size, &font_glyph_count);
    if (status != QUANTAPDF_OK)
        return status;

    for (i = 0u; i < glyph_count; ++i) {
        size_t cluster_offset = glyphs[i].unicode_offset;
        size_t cluster_length = glyphs[i].unicode_length;

        if (glyphs[i].glyph_id >= font_glyph_count ||
            !isfinite(glyphs[i].x_advance) ||
            !isfinite(glyphs[i].y_advance) ||
            !isfinite(glyphs[i].x_offset) ||
            !isfinite(glyphs[i].y_offset))
            return QUANTAPDF_ERROR_ARGUMENT;
        if (cluster_offset > unicode_size ||
            cluster_length > unicode_size - cluster_offset ||
            !quantapdf_glyph_run_utf8_boundary(
                unicode_bytes, unicode_size, cluster_offset) ||
            !quantapdf_glyph_run_utf8_boundary(
                unicode_bytes,
                unicode_size,
                cluster_offset + cluster_length))
            return QUANTAPDF_ERROR_ARGUMENT;
    }

    glyph_bytes = glyph_count * sizeof(*glyphs);
    if (unicode_size > SIZE_MAX - glyph_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    resource_bytes = glyph_bytes + unicode_size;
    if (composer->resource_bytes > composer->max_resource_bytes ||
        resource_bytes >
            composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    glyph_copy = (quantapdf_composer_glyph *)malloc(glyph_bytes);
    if (glyph_copy == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(glyph_copy, glyphs, glyph_bytes);

    if (unicode_size != 0u) {
        unicode_copy = (char *)malloc(unicode_size);
        if (unicode_copy == NULL) {
            free(glyph_copy);
            return QUANTAPDF_ERROR_NOMEM;
        }
        memcpy(unicode_copy, unicode_utf8, unicode_size);
    }

    status = quantapdf_composer_reserve_operation_internal(composer);
    if (status != QUANTAPDF_OK) {
        free(unicode_copy);
        free(glyph_copy);
        return status;
    }

    memset(&operation, 0, sizeof(operation));
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_GLYPH_RUN;
    operation.page_index = page_index;
    operation.value.glyph_run.glyphs = glyph_copy;
    operation.value.glyph_run.glyph_count = glyph_count;
    operation.value.glyph_run.unicode_utf8 = unicode_copy;
    operation.value.glyph_run.unicode_size = unicode_size;
    operation.value.glyph_run.origin = origin;
    operation.graphics_state_id =
        quantapdf_glyph_graphics_state_id(options);
    quantapdf_copy_glyph_options(
        &operation.value.glyph_run.options, options);
    operation.value.glyph_run.transform = *transform;
    composer->operations[composer->operation_count++] = operation;
    composer->resource_bytes += resource_bytes;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_draw_glyph_run(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_point origin,
    const quantapdf_composer_glyph *glyphs,
    size_t glyph_count,
    const char *unicode_utf8,
    size_t unicode_size,
    const quantapdf_composer_glyph_run_options *options)
{
    quantapdf_affine_transform const transform =
        quantapdf_affine_identity_internal();
    return quantapdf_composer_draw_glyph_run_internal(
        composer,
        page_index,
        origin,
        glyphs,
        glyph_count,
        unicode_utf8,
        unicode_size,
        &transform,
        options);
}

quantapdf_status quantapdf_composer_draw_glyph_run_transformed(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_point origin,
    const quantapdf_composer_glyph *glyphs,
    size_t glyph_count,
    const char *unicode_utf8,
    size_t unicode_size,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_glyph_run_options *options)
{
    return quantapdf_composer_draw_glyph_run_internal(
        composer,
        page_index,
        origin,
        glyphs,
        glyph_count,
        unicode_utf8,
        unicode_size,
        transform,
        options);
}
