#include "internal.h"
#include "backend/qpdf_composer.h"
#include "backend/composer_text_layout.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int quantapdf_composer_rect_valid(const quantapdf_rect *rect)
{
    return rect != NULL &&
        isfinite(rect->x0) && isfinite(rect->y0) &&
        isfinite(rect->x1) && isfinite(rect->y1) &&
        rect->x1 > rect->x0 && rect->y1 > rect->y0;
}


static int quantapdf_composer_measurement_prepare(
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


static int quantapdf_composer_point_valid(const quantapdf_point *point)
{
    return point != NULL && isfinite(point->x) && isfinite(point->y);
}

int quantapdf_affine_transform_valid_internal(
    const quantapdf_affine_transform *transform)
{
    return transform != NULL &&
        isfinite(transform->a) && isfinite(transform->b) &&
        isfinite(transform->c) && isfinite(transform->d) &&
        isfinite(transform->e) && isfinite(transform->f);
}

quantapdf_affine_transform quantapdf_affine_identity_internal(void)
{
    quantapdf_affine_transform transform = {
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    return transform;
}

static int quantapdf_composer_path_options_valid(
    const quantapdf_composer_path_options *options)
{
    if (options == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
        (options->stroke != 0 && options->stroke != 1) ||
        (options->fill != 0 && options->fill != 1) ||
        (!options->stroke && !options->fill) ||
        options->fill_rule < QUANTAPDF_COMPOSER_FILL_NONZERO ||
        options->fill_rule > QUANTAPDF_COMPOSER_FILL_EVEN_ODD)
        return 0;
    if (options->stroke &&
        (!isfinite(options->stroke_width) || options->stroke_width < 0.0f ||
         options->line_cap < QUANTAPDF_COMPOSER_LINE_CAP_BUTT ||
         options->line_cap > QUANTAPDF_COMPOSER_LINE_CAP_SQUARE ||
         options->line_join < QUANTAPDF_COMPOSER_LINE_JOIN_MITER ||
         options->line_join > QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL ||
         !isfinite(options->miter_limit) || options->miter_limit < 1.0f ||
         (options->stroke_argb >> 24u) != 0xffu))
        return 0;
    if (options->fill && (options->fill_argb >> 24u) != 0xffu)
        return 0;
    return 1;
}

static int quantapdf_composer_dash_pattern_valid(
    const quantapdf_composer_dash_pattern *pattern)
{
    size_t i;
    int have_positive = 0;

    if (pattern == NULL ||
        pattern->struct_size < QUANTAPDF_COMPOSER_DASH_PATTERN_V1_MIN_SIZE ||
        pattern->lengths == NULL ||
        pattern->length_count == 0u ||
        pattern->length_count > QUANTAPDF_COMPOSER_MAX_DASH_COUNT ||
        !isfinite(pattern->phase) || pattern->phase < 0.0f)
        return 0;
    for (i = 0u; i < pattern->length_count; ++i) {
        if (!isfinite(pattern->lengths[i]) || pattern->lengths[i] < 0.0f)
            return 0;
        if (pattern->lengths[i] > 0.0f)
            have_positive = 1;
    }
    return have_positive;
}

static int quantapdf_composer_path_commands_valid(
    const quantapdf_composer_path_command *commands,
    size_t command_count)
{
    size_t i;
    int have_current = 0;
    int have_segment = 0;

    if (commands == NULL || command_count == 0u)
        return 0;
    for (i = 0u; i < command_count; ++i) {
        const quantapdf_composer_path_command *command = &commands[i];
        switch (command->kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            if (!quantapdf_composer_point_valid(&command->point1))
                return 0;
            have_current = 1;
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            if (!have_current ||
                !quantapdf_composer_point_valid(&command->point1))
                return 0;
            have_segment = 1;
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            if (!have_current ||
                !quantapdf_composer_point_valid(&command->point1) ||
                !quantapdf_composer_point_valid(&command->point2) ||
                !quantapdf_composer_point_valid(&command->point3))
                return 0;
            have_segment = 1;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            if (!have_current)
                return 0;
            break;
        default:
            return 0;
        }
    }
    return have_segment;
}

static int quantapdf_composer_codepoint_is_winansi(uint32_t codepoint)
{
    static const uint32_t special[] = {
        UINT32_C(0x20ac), UINT32_C(0x201a), UINT32_C(0x0192),
        UINT32_C(0x201e), UINT32_C(0x2026), UINT32_C(0x2020),
        UINT32_C(0x2021), UINT32_C(0x02c6), UINT32_C(0x2030),
        UINT32_C(0x0160), UINT32_C(0x2039), UINT32_C(0x0152),
        UINT32_C(0x017d), UINT32_C(0x2018), UINT32_C(0x2019),
        UINT32_C(0x201c), UINT32_C(0x201d), UINT32_C(0x2022),
        UINT32_C(0x2013), UINT32_C(0x2014), UINT32_C(0x02dc),
        UINT32_C(0x2122), UINT32_C(0x0161), UINT32_C(0x203a),
        UINT32_C(0x0153), UINT32_C(0x017e), UINT32_C(0x0178)};
    size_t i;

    if ((codepoint >= 0x20u && codepoint <= 0x7eu) ||
        (codepoint >= 0xa0u && codepoint <= 0xffu) ||
        codepoint == '\n' || codepoint == '\r' || codepoint == '\t')
        return 1;
    for (i = 0u; i < sizeof(special) / sizeof(special[0]); ++i) {
        if (special[i] == codepoint)
            return 1;
    }
    return 0;
}

static int quantapdf_composer_utf8_is_winansi(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    while (*cursor != 0u) {
        uint32_t codepoint;
        size_t count;
        size_t i;

        if (*cursor < 0x80u) {
            codepoint = *cursor;
            count = 1u;
        } else if (*cursor >= 0xc2u && *cursor <= 0xdfu) {
            codepoint = (uint32_t)(*cursor & 0x1fu);
            count = 2u;
        } else if (*cursor >= 0xe0u && *cursor <= 0xefu) {
            codepoint = (uint32_t)(*cursor & 0x0fu);
            count = 3u;
        } else {
            return 0;
        }
        for (i = 1u; i < count; ++i) {
            if (cursor[i] == 0u || (cursor[i] & 0xc0u) != 0x80u)
                return 0;
            codepoint = (codepoint << 6u) | (uint32_t)(cursor[i] & 0x3fu);
        }
        if ((count == 3u && codepoint < 0x800u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu))
            return 0;
        if (!quantapdf_composer_codepoint_is_winansi(codepoint))
            return 0;
        cursor += count;
    }
    return 1;
}

quantapdf_status quantapdf_composer_reserve_operations_internal(
    quantapdf_composer *composer,
    size_t extra_operations)
{
    quantapdf_composer_operation *grown;
    size_t needed;
    size_t new_capacity;

    if (composer == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (extra_operations >
        composer->max_operations - composer->operation_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    needed = composer->operation_count + extra_operations;
    if (needed <= composer->operation_capacity)
        return QUANTAPDF_OK;

    new_capacity = composer->operation_capacity == 0u
        ? (composer->max_operations < 16u ? composer->max_operations : 16u)
        : composer->operation_capacity;
    while (new_capacity < needed) {
        size_t doubled;
        if (new_capacity > SIZE_MAX / 2u)
            doubled = composer->max_operations;
        else
            doubled = new_capacity * 2u;
        if (doubled <= new_capacity ||
            doubled > composer->max_operations)
            doubled = composer->max_operations;
        new_capacity = doubled;
        if (new_capacity < needed &&
            new_capacity == composer->max_operations)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (new_capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_operation *)realloc(
        composer->operations, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->operations = grown;
    composer->operation_capacity = new_capacity;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_reserve_operation_internal(
    quantapdf_composer *composer)
{
    return quantapdf_composer_reserve_operations_internal(composer, 1u);
}

static quantapdf_status quantapdf_composer_reserve_image(
    quantapdf_composer *composer)
{
    quantapdf_composer_image_state *grown;
    size_t new_capacity;

    if (composer->image_count >= (size_t)UINT32_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->image_count < composer->image_capacity)
        return QUANTAPDF_OK;
    new_capacity = composer->image_capacity == 0u
        ? 8u
        : composer->image_capacity * 2u;
    if (new_capacity < composer->image_capacity ||
        new_capacity > (size_t)UINT32_MAX ||
        new_capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_image_state *)realloc(
        composer->images, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->images = grown;
    composer->image_capacity = new_capacity;
    return QUANTAPDF_OK;
}

static size_t quantapdf_composer_limit_or_default(
    size_t configured,
    size_t default_value)
{
    return configured == 0u ? default_value : configured;
}

quantapdf_status quantapdf_composer_create(
    const quantapdf_composer_options *options,
    quantapdf_composer **out_composer)
{
    quantapdf_composer *composer;
    size_t max_pages = QUANTAPDF_COMPOSER_DEFAULT_MAX_PAGES;
    size_t max_operations = QUANTAPDF_COMPOSER_DEFAULT_MAX_OPERATIONS;
    size_t max_resource_bytes =
        QUANTAPDF_COMPOSER_DEFAULT_MAX_RESOURCE_BYTES;
    size_t max_navigation_items =
        QUANTAPDF_COMPOSER_DEFAULT_MAX_NAVIGATION_ITEMS;

    if (out_composer == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_composer = NULL;

    if (options != NULL) {
        if (options->struct_size < QUANTAPDF_COMPOSER_OPTIONS_V1_MIN_SIZE)
            return QUANTAPDF_ERROR_ARGUMENT;
        max_pages = quantapdf_composer_limit_or_default(
            options->max_pages, max_pages);
        max_operations = quantapdf_composer_limit_or_default(
            options->max_operations, max_operations);
        max_resource_bytes = quantapdf_composer_limit_or_default(
            options->max_resource_bytes, max_resource_bytes);
        if (options->struct_size >= QUANTAPDF_COMPOSER_OPTIONS_V2_MIN_SIZE) {
            max_navigation_items = quantapdf_composer_limit_or_default(
                options->max_navigation_items, max_navigation_items);
        }
    }

    if (max_pages > SIZE_MAX / sizeof(quantapdf_composer_page_state))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    composer = (quantapdf_composer *)calloc(1u, sizeof(*composer));
    if (composer == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->max_pages = max_pages;
    composer->max_operations = max_operations;
    composer->max_resource_bytes = max_resource_bytes;
    composer->max_navigation_items = max_navigation_items;
    *out_composer = composer;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_page(
    quantapdf_composer *composer,
    const quantapdf_composer_page_options *options,
    size_t *out_page_index)
{
    quantapdf_composer_page_state *grown;
    size_t new_capacity;

    if (out_page_index != NULL)
        *out_page_index = SIZE_MAX;
    if (composer == NULL || options == NULL || out_page_index == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_MIN_SIZE ||
        !isfinite(options->width_points) ||
        !isfinite(options->height_points) ||
        options->width_points <= 0.0f || options->height_points <= 0.0f)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (composer->page_count >= composer->max_pages)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if (composer->page_count == composer->page_capacity) {
        new_capacity = composer->page_capacity == 0u
            ? (composer->max_pages < 8u ? composer->max_pages : 8u)
            : composer->page_capacity * 2u;
        if (new_capacity < composer->page_capacity ||
            new_capacity > composer->max_pages)
            new_capacity = composer->max_pages;
        grown = (quantapdf_composer_page_state *)realloc(
            composer->pages, new_capacity * sizeof(*grown));
        if (grown == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        composer->pages = grown;
        composer->page_capacity = new_capacity;
    }

    composer->pages[composer->page_count].width_points =
        options->width_points;
    composer->pages[composer->page_count].height_points =
        options->height_points;
    composer->pages[composer->page_count].background_argb =
        options->background_argb;
    *out_page_index = composer->page_count;
    ++composer->page_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_image(
    quantapdf_composer *composer,
    const unsigned char *data,
    size_t size,
    quantapdf_composer_image_id *out_image_id)
{
    quantapdf_composer_image_state image;
    quantapdf_status status;
    int is_png;

    if (out_image_id != NULL)
        *out_image_id = 0u;
    if (composer == NULL || data == NULL || size == 0u ||
        out_image_id == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(&image, 0, sizeof(image));
    if (composer->resource_bytes > composer->max_resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    is_png = size >= 8u && data[0] == 0x89u && data[1] == 'P' &&
        data[2] == 'N' && data[3] == 'G' && data[4] == 0x0du &&
        data[5] == 0x0au && data[6] == 0x1au && data[7] == 0x0au;
    if (is_png) {
        status = quantapdf_png_decode(
            data,
            size,
            composer->max_resource_bytes - composer->resource_bytes,
            &image.data,
            &image.size,
            &image.alpha_data,
            &image.alpha_size,
            &image.width,
            &image.height);
        if (status != QUANTAPDF_OK)
            return status;
        image.format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_PNG;
        image.components = 3;
        image.has_alpha = image.alpha_data != NULL;
    } else {
        status = quantapdf_jpeg_validate(
            data,
            size,
            composer->max_resource_bytes - composer->resource_bytes,
            &image.width,
            &image.height,
            &image.components);
        if (status != QUANTAPDF_OK)
            return status;
        image.data = (unsigned char *)malloc(size);
        if (image.data == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        memcpy(image.data, data, size);
        image.size = size;
        image.format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_JPEG;
    }
    status = quantapdf_composer_reserve_image(composer);
    if (status != QUANTAPDF_OK) {
        free(image.alpha_data);
        free(image.data);
        return status;
    }
    composer->images[composer->image_count] = image;
    ++composer->image_count;
    composer->resource_bytes += image.size + image.alpha_size;
    *out_image_id = (quantapdf_composer_image_id)composer->image_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_measure_text(
    const quantapdf_composer *composer,
    const char *text_utf8,
    float max_width,
    const quantapdf_composer_text_options *options,
    quantapdf_composer_text_measurement *out_measurement)
{
    if (!quantapdf_composer_measurement_prepare(out_measurement))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (composer == NULL || text_utf8 == NULL || options == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_MIN_SIZE ||
        !isfinite(max_width) || max_width <= 0.0f ||
        options->font < QUANTAPDF_COMPOSER_FONT_HELVETICA ||
        options->font > QUANTAPDF_COMPOSER_FONT_COURIER_BOLD_OBLIQUE ||
        !isfinite(options->font_size) || options->font_size <= 0.0f ||
        !isfinite(options->line_height_multiplier) ||
        options->line_height_multiplier <= 0.0f ||
        options->alignment < QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT ||
        options->alignment > QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT ||
        (options->argb >> 24u) != 0xffu)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (!quantapdf_composer_utf8_is_winansi(text_utf8))
        return QUANTAPDF_ERROR_FORMAT;

    return quantapdf_measure_base14_text_internal(
        text_utf8, max_width, options, out_measurement);
}

static quantapdf_status quantapdf_composer_draw_text_internal(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_text_options *options)
{
    quantapdf_composer_operation operation;
    quantapdf_status status;
    size_t text_size;

    if (composer == NULL || text_utf8 == NULL || options == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_MIN_SIZE ||
        page_index >= composer->page_count ||
        !quantapdf_composer_rect_valid(bounds) ||
        !quantapdf_affine_transform_valid_internal(transform) ||
        options->font < QUANTAPDF_COMPOSER_FONT_HELVETICA ||
        options->font > QUANTAPDF_COMPOSER_FONT_COURIER_BOLD_OBLIQUE ||
        !isfinite(options->font_size) || options->font_size <= 0.0f ||
        !isfinite(options->line_height_multiplier) ||
        options->line_height_multiplier <= 0.0f ||
        options->alignment < QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT ||
        options->alignment > QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT ||
        (options->argb >> 24u) != 0xffu)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (!quantapdf_composer_utf8_is_winansi(text_utf8))
        return QUANTAPDF_ERROR_FORMAT;

    text_size = strlen(text_utf8) + 1u;
    if (text_size > composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    memset(&operation, 0, sizeof(operation));
    operation.value.text.text_utf8 = (char *)malloc(text_size);
    if (operation.value.text.text_utf8 == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(operation.value.text.text_utf8, text_utf8, text_size);
    status = quantapdf_composer_reserve_operation_internal(composer);
    if (status != QUANTAPDF_OK) {
        free(operation.value.text.text_utf8);
        return status;
    }
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_TEXT;
    operation.page_index = page_index;
    operation.bounds = *bounds;
    operation.value.text.options = *options;
    operation.value.text.transform = *transform;
    composer->operations[composer->operation_count] = operation;
    ++composer->operation_count;
    composer->resource_bytes += text_size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_draw_text(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_composer_text_options *options)
{
    quantapdf_affine_transform const transform =
        quantapdf_affine_identity_internal();
    return quantapdf_composer_draw_text_internal(
        composer, page_index, text_utf8, bounds, &transform, options);
}

quantapdf_status quantapdf_composer_draw_text_transformed(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_text_options *options)
{
    return quantapdf_composer_draw_text_internal(
        composer, page_index, text_utf8, bounds, transform, options);
}

quantapdf_status quantapdf_composer_draw_image(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_composer_image_id image_id,
    const quantapdf_rect *bounds,
    const quantapdf_composer_image_options *options)
{
    quantapdf_composer_operation operation;
    quantapdf_status status;

    if (composer == NULL || page_index >= composer->page_count ||
        image_id == 0u || (size_t)image_id > composer->image_count ||
        !quantapdf_composer_rect_valid(bounds) || options == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE ||
        options->fit < QUANTAPDF_COMPOSER_IMAGE_FIT_CONTAIN ||
        options->fit > QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_composer_reserve_operation_internal(composer);
    if (status != QUANTAPDF_OK)
        return status;
    memset(&operation, 0, sizeof(operation));
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    operation.page_index = page_index;
    operation.bounds = *bounds;
    operation.value.image.image_id = image_id;
    operation.value.image.options = *options;
    composer->operations[composer->operation_count] = operation;
    ++composer->operation_count;
    return QUANTAPDF_OK;
}


static quantapdf_status quantapdf_composer_draw_path_internal(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_composer_path_command *commands,
    size_t command_count,
    const quantapdf_composer_path_options *options,
    const quantapdf_composer_dash_pattern *dash_pattern)
{
    quantapdf_composer_operation operation;
    quantapdf_composer_path_command *copied_commands = NULL;
    float *copied_dash = NULL;
    quantapdf_status status;
    size_t path_bytes;
    size_t dash_bytes = 0u;
    size_t resource_bytes;

    if (composer == NULL || page_index >= composer->page_count ||
        !quantapdf_composer_path_options_valid(options) ||
        commands == NULL || command_count == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (dash_pattern != NULL &&
        (!options->stroke ||
         !quantapdf_composer_dash_pattern_valid(dash_pattern)))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (command_count > SIZE_MAX / sizeof(*copied_commands))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    path_bytes = command_count * sizeof(*copied_commands);
    if (dash_pattern != NULL) {
        if (dash_pattern->length_count > SIZE_MAX / sizeof(*copied_dash))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        dash_bytes = dash_pattern->length_count * sizeof(*copied_dash);
    }
    if (dash_bytes > SIZE_MAX - path_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    resource_bytes = path_bytes + dash_bytes;
    if (composer->resource_bytes > composer->max_resource_bytes ||
        resource_bytes >
            composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (!quantapdf_composer_path_commands_valid(commands, command_count))
        return QUANTAPDF_ERROR_ARGUMENT;

    copied_commands =
        (quantapdf_composer_path_command *)malloc(path_bytes);
    if (copied_commands == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(copied_commands, commands, path_bytes);

    if (dash_pattern != NULL) {
        copied_dash = (float *)malloc(dash_bytes);
        if (copied_dash == NULL) {
            free(copied_commands);
            return QUANTAPDF_ERROR_NOMEM;
        }
        memcpy(copied_dash, dash_pattern->lengths, dash_bytes);
    }

    status = quantapdf_composer_reserve_operation_internal(composer);
    if (status != QUANTAPDF_OK) {
        free(copied_dash);
        free(copied_commands);
        return status;
    }

    memset(&operation, 0, sizeof(operation));
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    operation.page_index = page_index;
    operation.value.path.commands = copied_commands;
    operation.value.path.command_count = command_count;
    operation.value.path.options = *options;
    operation.value.path.dash_lengths = copied_dash;
    operation.value.path.dash_count =
        dash_pattern == NULL ? 0u : dash_pattern->length_count;
    operation.value.path.dash_phase =
        dash_pattern == NULL ? 0.0f : dash_pattern->phase;
    composer->operations[composer->operation_count] = operation;
    ++composer->operation_count;
    composer->resource_bytes += resource_bytes;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_draw_path(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_composer_path_command *commands,
    size_t command_count,
    const quantapdf_composer_path_options *options)
{
    return quantapdf_composer_draw_path_internal(
        composer, page_index, commands, command_count, options, NULL);
}

quantapdf_status quantapdf_composer_draw_path_dashed(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_composer_path_command *commands,
    size_t command_count,
    const quantapdf_composer_path_options *options,
    const quantapdf_composer_dash_pattern *dash_pattern)
{
    return quantapdf_composer_draw_path_internal(
        composer,
        page_index,
        commands,
        command_count,
        options,
        dash_pattern);
}

quantapdf_status quantapdf_composer_finish(
    const quantapdf_composer *composer,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (composer == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (composer->page_count == 0u)
        return QUANTAPDF_ERROR_STATE;

    output = (quantapdf_output *)calloc(1u, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    status = quantapdf_qpdf_compose(
        composer, &output->data, &output->size);
    if (status != QUANTAPDF_OK) {
        free(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}

void quantapdf_drop_composer(quantapdf_composer *composer)
{
    size_t i;

    if (composer == NULL)
        return;
    for (i = 0u; i < composer->operation_count; ++i) {
        if (composer->operations[i].kind == QUANTAPDF_COMPOSER_OPERATION_TEXT)
            free(composer->operations[i].value.text.text_utf8);
        else if (composer->operations[i].kind ==
                 QUANTAPDF_COMPOSER_OPERATION_PATH) {
            free(composer->operations[i].value.path.dash_lengths);
            free(composer->operations[i].value.path.commands);
        }
        else if (composer->operations[i].kind ==
                 QUANTAPDF_COMPOSER_OPERATION_EMBEDDED_TEXT)
            free(composer->operations[i].value.embedded_text.text_utf8);
        else if (composer->operations[i].kind ==
                 QUANTAPDF_COMPOSER_OPERATION_GLYPH_RUN) {
            free(composer->operations[i].value.glyph_run.glyphs);
            free(composer->operations[i].value.glyph_run.unicode_utf8);
        }
    }
    for (i = 0u; i < composer->image_count; ++i)
        free(composer->images[i].alpha_data);
    for (i = 0u; i < composer->image_count; ++i)
        free(composer->images[i].data);
    for (i = 0u; i < composer->link_count; ++i)
        free(composer->links[i].uri_utf8);
    for (i = 0u; i < composer->outline_count; ++i)
        free(composer->outlines[i].title_utf8);
    for (i = 0u; i < composer->font_count; ++i)
        free(composer->fonts[i].data);
    free(composer->fonts);
    free(composer->outlines);
    free(composer->links);
    free(composer->images);
    free(composer->operations);
    free(composer->pages);
    free(composer);
}
