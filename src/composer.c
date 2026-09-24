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

int quantapdf_graphics_state_id_valid_internal(
    const quantapdf_composer *composer,
    quantapdf_composer_graphics_state_id graphics_state_id)
{
    return composer != NULL &&
        (graphics_state_id == 0u ||
         (size_t)graphics_state_id <= composer->graphics_state_count);
}

static quantapdf_composer_graphics_state_id
quantapdf_text_graphics_state_id(
    const quantapdf_composer_text_options *options)
{
    if (options != NULL &&
        options->struct_size >= QUANTAPDF_COMPOSER_TEXT_OPTIONS_V2_MIN_SIZE)
        return options->graphics_state_id;
    return 0u;
}

static quantapdf_composer_graphics_state_id
quantapdf_image_graphics_state_id(
    const quantapdf_composer_image_options *options)
{
    if (options != NULL &&
        options->struct_size >= QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE)
        return options->graphics_state_id;
    return 0u;
}

static quantapdf_composer_graphics_state_id
quantapdf_path_graphics_state_id(
    const quantapdf_composer_path_options *options)
{
    if (options != NULL &&
        options->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE)
        return options->graphics_state_id;
    return 0u;
}

static quantapdf_composer_paint_id
quantapdf_path_fill_paint_id(
    const quantapdf_composer_path_options *options)
{
    if (options != NULL &&
        options->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE)
        return options->fill_paint_id;
    return 0u;
}

static quantapdf_composer_paint_id
quantapdf_path_stroke_paint_id(
    const quantapdf_composer_path_options *options)
{
    if (options != NULL &&
        options->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE)
        return options->stroke_paint_id;
    return 0u;
}

static int quantapdf_paint_id_valid(
    const quantapdf_composer *composer,
    quantapdf_composer_paint_id paint_id)
{
    return composer != NULL &&
        (paint_id == 0u || paint_id <= composer->paint_count);
}

static int quantapdf_form_id_valid(
    const quantapdf_composer *composer,
    quantapdf_composer_form_id form_id)
{
    return composer != NULL &&
        form_id != 0u && form_id <= composer->form_count;
}

static int quantapdf_clip_id_valid(
    const quantapdf_composer *composer,
    quantapdf_composer_clip_id clip_id)
{
    return composer != NULL &&
        (clip_id == 0u || clip_id <= composer->clip_count);
}

static quantapdf_composer_clip_id quantapdf_graphics_state_clip_id(
    const quantapdf_composer_graphics_state_options *options)
{
    if (options != NULL &&
        options->struct_size >=
            QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_MIN_SIZE)
        return options->clip_id;
    return 0u;
}

static float quantapdf_canonical_float(float value)
{
    return value == 0.0f ? 0.0f : value;
}

static int quantapdf_composer_resource_transform_normalize(
    const quantapdf_affine_transform *source,
    quantapdf_affine_transform *destination)
{
    double determinant;

    if (source == NULL || destination == NULL)
        return 0;
    if (source->a == 0.0f && source->b == 0.0f &&
        source->c == 0.0f && source->d == 0.0f &&
        source->e == 0.0f && source->f == 0.0f) {
        *destination = quantapdf_affine_identity_internal();
        return 1;
    }
    if (!quantapdf_affine_transform_valid_internal(source))
        return 0;
    determinant =
        (double)source->a * (double)source->d -
        (double)source->b * (double)source->c;
    if (!isfinite(determinant) || determinant == 0.0)
        return 0;
    destination->a = quantapdf_canonical_float(source->a);
    destination->b = quantapdf_canonical_float(source->b);
    destination->c = quantapdf_canonical_float(source->c);
    destination->d = quantapdf_canonical_float(source->d);
    destination->e = quantapdf_canonical_float(source->e);
    destination->f = quantapdf_canonical_float(source->f);
    return 1;
}

static int quantapdf_gradient_stops_valid(
    const quantapdf_composer_gradient_stop *stops,
    size_t stop_count)
{
    size_t i;

    if (stops == NULL || stop_count < 2u ||
        stop_count > QUANTAPDF_COMPOSER_MAX_GRADIENT_STOPS)
        return 0;
    if (!isfinite(stops[0].offset) || stops[0].offset != 0.0f ||
        !isfinite(stops[stop_count - 1u].offset) ||
        stops[stop_count - 1u].offset != 1.0f)
        return 0;
    for (i = 0u; i < stop_count; ++i) {
        if (!isfinite(stops[i].offset) ||
            stops[i].offset < 0.0f || stops[i].offset > 1.0f ||
            (stops[i].argb >> 24u) != 0xffu)
            return 0;
        if (i != 0u && stops[i].offset <= stops[i - 1u].offset)
            return 0;
    }
    return 1;
}

static void quantapdf_copy_text_options(
    quantapdf_composer_text_options *destination,
    const quantapdf_composer_text_options *source)
{
    memset(destination, 0, sizeof(*destination));
    destination->struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V2_SIZE;
    destination->font = source->font;
    destination->font_size = source->font_size;
    destination->argb = source->argb;
    destination->line_height_multiplier = source->line_height_multiplier;
    destination->alignment = source->alignment;
    destination->wrap = source->wrap;
    destination->graphics_state_id =
        quantapdf_text_graphics_state_id(source);
}

static void quantapdf_copy_image_options(
    quantapdf_composer_image_options *destination,
    const quantapdf_composer_image_options *source)
{
    memset(destination, 0, sizeof(*destination));
    destination->struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    destination->fit = source->fit;
    destination->graphics_state_id =
        quantapdf_image_graphics_state_id(source);
}

static void quantapdf_copy_path_options(
    quantapdf_composer_path_options *destination,
    const quantapdf_composer_path_options *source)
{
    memset(destination, 0, sizeof(*destination));
    destination->struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
    destination->stroke = source->stroke;
    destination->fill = source->fill;
    destination->stroke_argb = source->stroke_argb;
    destination->fill_argb = source->fill_argb;
    destination->stroke_width = source->stroke_width;
    destination->fill_rule = source->fill_rule;
    destination->line_cap = source->line_cap;
    destination->line_join = source->line_join;
    destination->miter_limit = source->miter_limit;
    destination->graphics_state_id =
        quantapdf_path_graphics_state_id(source);
    destination->fill_paint_id =
        quantapdf_path_fill_paint_id(source);
    destination->stroke_paint_id =
        quantapdf_path_stroke_paint_id(source);
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
         (quantapdf_path_stroke_paint_id(options) == 0u &&
          (options->stroke_argb >> 24u) != 0xffu)))
        return 0;
    if (options->fill &&
        quantapdf_path_fill_paint_id(options) == 0u &&
        (options->fill_argb >> 24u) != 0xffu)
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

static quantapdf_status quantapdf_composer_reserve_graphics_state(
    quantapdf_composer *composer)
{
    quantapdf_composer_graphics_state *grown;
    size_t new_capacity;

    if (composer->graphics_state_count == SIZE_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->graphics_state_count < composer->graphics_state_capacity)
        return QUANTAPDF_OK;
    new_capacity = composer->graphics_state_capacity == 0u
        ? 8u
        : composer->graphics_state_capacity * 2u;
    if (new_capacity < composer->graphics_state_capacity ||
        new_capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_graphics_state *)realloc(
        composer->graphics_states, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->graphics_states = grown;
    composer->graphics_state_capacity = new_capacity;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_composer_reserve_clip(
    quantapdf_composer *composer)
{
    quantapdf_composer_clip_state *grown;
    size_t new_capacity;

    if (composer->clip_count == SIZE_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->clip_count < composer->clip_capacity)
        return QUANTAPDF_OK;
    new_capacity = composer->clip_capacity == 0u
        ? 8u
        : composer->clip_capacity * 2u;
    if (new_capacity < composer->clip_capacity ||
        new_capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_clip_state *)realloc(
        composer->clips, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->clips = grown;
    composer->clip_capacity = new_capacity;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_composer_reserve_paint(
    quantapdf_composer *composer)
{
    quantapdf_composer_paint_state *grown;
    size_t new_capacity;

    if (composer->paint_count == SIZE_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->paint_count < composer->paint_capacity)
        return QUANTAPDF_OK;
    new_capacity = composer->paint_capacity == 0u
        ? 8u
        : composer->paint_capacity * 2u;
    if (new_capacity < composer->paint_capacity ||
        new_capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_paint_state *)realloc(
        composer->paints, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->paints = grown;
    composer->paint_capacity = new_capacity;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_composer_reserve_form(
    quantapdf_composer *composer)
{
    quantapdf_composer_form_state *grown;
    size_t new_capacity;

    if (composer->form_count == SIZE_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->form_count < composer->form_capacity)
        return QUANTAPDF_OK;
    new_capacity = composer->form_capacity == 0u
        ? 4u
        : composer->form_capacity * 2u;
    if (new_capacity < composer->form_capacity ||
        new_capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_form_state *)realloc(
        composer->forms, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->forms = grown;
    composer->form_capacity = new_capacity;
    return QUANTAPDF_OK;
}

static int quantapdf_paint_state_equal(
    const quantapdf_composer_paint_state *left,
    const quantapdf_composer_paint_state *right)
{
    size_t i;

    if (left->kind != right->kind ||
        left->start.x != right->start.x ||
        left->start.y != right->start.y ||
        left->end.x != right->end.x ||
        left->end.y != right->end.y ||
        left->start_radius != right->start_radius ||
        left->end_radius != right->end_radius ||
        left->transform.a != right->transform.a ||
        left->transform.b != right->transform.b ||
        left->transform.c != right->transform.c ||
        left->transform.d != right->transform.d ||
        left->transform.e != right->transform.e ||
        left->transform.f != right->transform.f ||
        left->stop_count != right->stop_count)
        return 0;
    for (i = 0u; i < left->stop_count; ++i) {
        if (left->stops[i].offset !=
                quantapdf_canonical_float(right->stops[i].offset) ||
            left->stops[i].argb != right->stops[i].argb)
            return 0;
    }
    return 1;
}

static quantapdf_status quantapdf_composer_publish_paint(
    quantapdf_composer *composer,
    quantapdf_composer_paint_state *state,
    const quantapdf_composer_gradient_stop *source_stops,
    quantapdf_composer_paint_id *out_paint_id)
{
    quantapdf_composer_gradient_stop *copied = NULL;
    quantapdf_status status;
    size_t stop_bytes;
    size_t i;

    if (state->stop_count > SIZE_MAX / sizeof(*copied))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    stop_bytes = state->stop_count * sizeof(*copied);

    for (i = 0u; i < composer->paint_count; ++i) {
        quantapdf_composer_paint_state candidate = *state;
        candidate.stops = (quantapdf_composer_gradient_stop *)source_stops;
        if (quantapdf_paint_state_equal(
                &composer->paints[i], &candidate)) {
            *out_paint_id = i + 1u;
            return QUANTAPDF_OK;
        }
    }

    if (composer->resource_bytes > composer->max_resource_bytes ||
        stop_bytes > composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    copied = (quantapdf_composer_gradient_stop *)malloc(stop_bytes);
    if (copied == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    for (i = 0u; i < state->stop_count; ++i) {
        copied[i] = source_stops[i];
        copied[i].offset = quantapdf_canonical_float(copied[i].offset);
    }
    state->stops = copied;

    status = quantapdf_composer_reserve_paint(composer);
    if (status != QUANTAPDF_OK) {
        free(copied);
        state->stops = NULL;
        return status;
    }

    composer->paints[composer->paint_count] = *state;
    ++composer->paint_count;
    composer->resource_bytes += stop_bytes;
    *out_paint_id = composer->paint_count;
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
    composer->pages[composer->page_count].suppress_background = 0;
    *out_page_index = composer->page_count;
    ++composer->page_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_form(
    quantapdf_composer *composer,
    const quantapdf_composer_form_options *options,
    quantapdf_composer_form_builder_fn builder,
    void *user_data,
    quantapdf_composer_form_id *out_form_id)
{
    quantapdf_composer *child = NULL;
    quantapdf_composer_options child_options;
    quantapdf_composer_page_options page_options;
    unsigned char *pdf_data = NULL;
    size_t pdf_size = 0u;
    size_t page_index = SIZE_MAX;
    size_t remaining;
    size_t i;
    quantapdf_status status;

    if (out_form_id != NULL)
        *out_form_id = 0u;
    if (composer == NULL || options == NULL || builder == NULL ||
        out_form_id == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_MIN_SIZE ||
        !isfinite(options->width_points) || options->width_points <= 0.0f ||
        !isfinite(options->height_points) || options->height_points <= 0.0f)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (composer->resource_bytes > composer->max_resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    remaining = composer->max_resource_bytes - composer->resource_bytes;
    if (remaining == 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    memset(&child_options, 0, sizeof(child_options));
    child_options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V2_SIZE;
    child_options.max_pages = 1u;
    child_options.max_operations = composer->max_operations;
    child_options.max_resource_bytes = remaining;
    child_options.max_navigation_items = 1u;
    status = quantapdf_composer_create(&child_options, &child);
    if (status != QUANTAPDF_OK)
        return status;
    child->max_navigation_items = 0u;

    memset(&page_options, 0, sizeof(page_options));
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = options->width_points;
    page_options.height_points = options->height_points;
    page_options.background_argb = UINT32_C(0xffffffff);
    status = quantapdf_composer_add_page(
        child, &page_options, &page_index);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_composer(child);
        return status;
    }
    child->pages[0].suppress_background = 1;

    status = builder(child, 0u, user_data);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_composer(child);
        return status;
    }
    if (child->page_count != 1u ||
        child->link_count != 0u ||
        child->outline_count != 0u) {
        quantapdf_drop_composer(child);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    status = quantapdf_qpdf_compose(child, &pdf_data, &pdf_size);
    quantapdf_drop_composer(child);
    child = NULL;
    if (status != QUANTAPDF_OK)
        return status;
    if (pdf_data == NULL || pdf_size == 0u) {
        free(pdf_data);
        return QUANTAPDF_ERROR_BACKEND;
    }

    for (i = 0u; i < composer->form_count; ++i) {
        const quantapdf_composer_form_state *existing =
            &composer->forms[i];
        if (existing->width_points == options->width_points &&
            existing->height_points == options->height_points &&
            existing->pdf_size == pdf_size &&
            memcmp(existing->pdf_data, pdf_data, pdf_size) == 0) {
            free(pdf_data);
            *out_form_id = i + 1u;
            return QUANTAPDF_OK;
        }
    }

    if (pdf_size > remaining) {
        free(pdf_data);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    status = quantapdf_composer_reserve_form(composer);
    if (status != QUANTAPDF_OK) {
        free(pdf_data);
        return status;
    }

    composer->forms[composer->form_count].pdf_data = pdf_data;
    composer->forms[composer->form_count].pdf_size = pdf_size;
    composer->forms[composer->form_count].width_points =
        options->width_points;
    composer->forms[composer->form_count].height_points =
        options->height_points;
    composer->forms[composer->form_count].requires_pdf_16 =
        pdf_size >= 8u &&
        memcmp(pdf_data, "%PDF-1.", 7u) == 0 &&
        pdf_data[7] >= '6';
    ++composer->form_count;
    composer->resource_bytes += pdf_size;
    *out_form_id = composer->form_count;
    return QUANTAPDF_OK;
}

static quantapdf_point quantapdf_canonical_point(quantapdf_point point)
{
    point.x = quantapdf_canonical_float(point.x);
    point.y = quantapdf_canonical_float(point.y);
    return point;
}

static int quantapdf_clip_command_equal(
    const quantapdf_composer_path_command *left,
    const quantapdf_composer_path_command *right)
{
    if (left->kind != right->kind)
        return 0;
    switch (left->kind) {
    case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
    case QUANTAPDF_COMPOSER_PATH_LINE_TO:
        return left->point1.x == quantapdf_canonical_float(right->point1.x) &&
            left->point1.y == quantapdf_canonical_float(right->point1.y);
    case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
        return left->point1.x == quantapdf_canonical_float(right->point1.x) &&
            left->point1.y == quantapdf_canonical_float(right->point1.y) &&
            left->point2.x == quantapdf_canonical_float(right->point2.x) &&
            left->point2.y == quantapdf_canonical_float(right->point2.y) &&
            left->point3.x == quantapdf_canonical_float(right->point3.x) &&
            left->point3.y == quantapdf_canonical_float(right->point3.y);
    case QUANTAPDF_COMPOSER_PATH_CLOSE:
        return 1;
    }
    return 0;
}

static int quantapdf_clip_state_equal(
    const quantapdf_composer_clip_state *existing,
    const quantapdf_composer_path_command *commands,
    size_t command_count,
    quantapdf_composer_fill_rule fill_rule,
    const quantapdf_affine_transform *transform)
{
    size_t i;

    if (existing->command_count != command_count ||
        existing->fill_rule != fill_rule ||
        existing->transform.a != transform->a ||
        existing->transform.b != transform->b ||
        existing->transform.c != transform->c ||
        existing->transform.d != transform->d ||
        existing->transform.e != transform->e ||
        existing->transform.f != transform->f)
        return 0;
    for (i = 0u; i < command_count; ++i) {
        if (!quantapdf_clip_command_equal(
                &existing->commands[i], &commands[i]))
            return 0;
    }
    return 1;
}

quantapdf_status quantapdf_composer_add_clip_path(
    quantapdf_composer *composer,
    const quantapdf_composer_path_command *commands,
    size_t command_count,
    const quantapdf_composer_clip_options *options,
    quantapdf_composer_clip_id *out_clip_id)
{
    quantapdf_affine_transform transform;
    quantapdf_composer_path_command *copied = NULL;
    quantapdf_composer_clip_state state;
    quantapdf_status status;
    size_t command_bytes;
    size_t i;

    if (out_clip_id != NULL)
        *out_clip_id = 0u;
    if (composer == NULL || commands == NULL || command_count == 0u ||
        options == NULL || out_clip_id == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_MIN_SIZE ||
        options->fill_rule < QUANTAPDF_COMPOSER_FILL_NONZERO ||
        options->fill_rule > QUANTAPDF_COMPOSER_FILL_EVEN_ODD ||
        !quantapdf_composer_path_commands_valid(commands, command_count) ||
        !quantapdf_composer_resource_transform_normalize(
            &options->transform, &transform))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (command_count > SIZE_MAX / sizeof(*copied))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    command_bytes = command_count * sizeof(*copied);

    for (i = 0u; i < composer->clip_count; ++i) {
        if (quantapdf_clip_state_equal(
                &composer->clips[i],
                commands,
                command_count,
                options->fill_rule,
                &transform)) {
            *out_clip_id = i + 1u;
            return QUANTAPDF_OK;
        }
    }

    if (composer->resource_bytes > composer->max_resource_bytes ||
        command_bytes >
            composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    copied = (quantapdf_composer_path_command *)calloc(
        command_count, sizeof(*copied));
    if (copied == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    for (i = 0u; i < command_count; ++i) {
        copied[i].kind = commands[i].kind;
        switch (commands[i].kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            copied[i].point1 =
                quantapdf_canonical_point(commands[i].point1);
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            copied[i].point1 =
                quantapdf_canonical_point(commands[i].point1);
            copied[i].point2 =
                quantapdf_canonical_point(commands[i].point2);
            copied[i].point3 =
                quantapdf_canonical_point(commands[i].point3);
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            break;
        }
    }

    status = quantapdf_composer_reserve_clip(composer);
    if (status != QUANTAPDF_OK) {
        free(copied);
        return status;
    }

    memset(&state, 0, sizeof(state));
    state.commands = copied;
    state.command_count = command_count;
    state.fill_rule = options->fill_rule;
    state.transform = transform;
    composer->clips[composer->clip_count] = state;
    ++composer->clip_count;
    composer->resource_bytes += command_bytes;
    *out_clip_id = composer->clip_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_graphics_state(
    quantapdf_composer *composer,
    const quantapdf_composer_graphics_state_options *options,
    quantapdf_composer_graphics_state_id *out_graphics_state_id)
{
    quantapdf_composer_graphics_state state;
    quantapdf_status status;
    size_t i;

    if (out_graphics_state_id != NULL)
        *out_graphics_state_id = 0u;
    if (composer == NULL || options == NULL ||
        out_graphics_state_id == NULL ||
        options->struct_size <
            QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_MIN_SIZE ||
        !isfinite(options->fill_alpha) ||
        options->fill_alpha < 0.0f || options->fill_alpha > 1.0f ||
        !isfinite(options->stroke_alpha) ||
        options->stroke_alpha < 0.0f || options->stroke_alpha > 1.0f ||
        options->blend_mode < QUANTAPDF_COMPOSER_BLEND_NORMAL ||
        options->blend_mode > QUANTAPDF_COMPOSER_BLEND_LIGHTEN ||
        !quantapdf_clip_id_valid(
            composer, quantapdf_graphics_state_clip_id(options)))
        return QUANTAPDF_ERROR_ARGUMENT;

    state.fill_alpha =
        options->fill_alpha == 0.0f ? 0.0f : options->fill_alpha;
    state.stroke_alpha =
        options->stroke_alpha == 0.0f ? 0.0f : options->stroke_alpha;
    state.blend_mode = options->blend_mode;
    state.clip_id = quantapdf_graphics_state_clip_id(options);

    for (i = 0u; i < composer->graphics_state_count; ++i) {
        const quantapdf_composer_graphics_state *existing =
            &composer->graphics_states[i];
        if (existing->fill_alpha == state.fill_alpha &&
            existing->stroke_alpha == state.stroke_alpha &&
            existing->blend_mode == state.blend_mode &&
            existing->clip_id == state.clip_id) {
            *out_graphics_state_id =
                (quantapdf_composer_graphics_state_id)(i + 1u);
            return QUANTAPDF_OK;
        }
    }

    status = quantapdf_composer_reserve_graphics_state(composer);
    if (status != QUANTAPDF_OK)
        return status;
    composer->graphics_states[composer->graphics_state_count] = state;
    ++composer->graphics_state_count;
    *out_graphics_state_id =
        (quantapdf_composer_graphics_state_id)composer->graphics_state_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_linear_gradient(
    quantapdf_composer *composer,
    const quantapdf_composer_linear_gradient_options *options,
    quantapdf_composer_paint_id *out_paint_id)
{
    quantapdf_composer_paint_state state;

    if (out_paint_id != NULL)
        *out_paint_id = 0u;
    if (composer == NULL || options == NULL || out_paint_id == NULL ||
        options->struct_size <
            QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_MIN_SIZE ||
        !isfinite(options->start.x) || !isfinite(options->start.y) ||
        !isfinite(options->end.x) || !isfinite(options->end.y) ||
        (options->start.x == options->end.x &&
         options->start.y == options->end.y) ||
        !quantapdf_gradient_stops_valid(
            options->stops, options->stop_count))
        return QUANTAPDF_ERROR_ARGUMENT;

    memset(&state, 0, sizeof(state));
    state.kind = QUANTAPDF_COMPOSER_PAINT_LINEAR_GRADIENT_INTERNAL;
    state.start.x = quantapdf_canonical_float(options->start.x);
    state.start.y = quantapdf_canonical_float(options->start.y);
    state.end.x = quantapdf_canonical_float(options->end.x);
    state.end.y = quantapdf_canonical_float(options->end.y);
    if (!quantapdf_composer_resource_transform_normalize(
            &options->transform, &state.transform))
        return QUANTAPDF_ERROR_ARGUMENT;
    state.stop_count = options->stop_count;

    return quantapdf_composer_publish_paint(
        composer, &state, options->stops, out_paint_id);
}

quantapdf_status quantapdf_composer_add_radial_gradient(
    quantapdf_composer *composer,
    const quantapdf_composer_radial_gradient_options *options,
    quantapdf_composer_paint_id *out_paint_id)
{
    quantapdf_composer_paint_state state;

    if (out_paint_id != NULL)
        *out_paint_id = 0u;
    if (composer == NULL || options == NULL || out_paint_id == NULL ||
        options->struct_size <
            QUANTAPDF_COMPOSER_RADIAL_GRADIENT_OPTIONS_V1_MIN_SIZE ||
        !isfinite(options->start_center.x) ||
        !isfinite(options->start_center.y) ||
        !isfinite(options->end_center.x) ||
        !isfinite(options->end_center.y) ||
        !isfinite(options->start_radius) || options->start_radius < 0.0f ||
        !isfinite(options->end_radius) || options->end_radius < 0.0f ||
        (options->start_center.x == options->end_center.x &&
         options->start_center.y == options->end_center.y &&
         options->start_radius == options->end_radius) ||
        !quantapdf_gradient_stops_valid(
            options->stops, options->stop_count))
        return QUANTAPDF_ERROR_ARGUMENT;

    memset(&state, 0, sizeof(state));
    state.kind = QUANTAPDF_COMPOSER_PAINT_RADIAL_GRADIENT_INTERNAL;
    state.start.x = quantapdf_canonical_float(options->start_center.x);
    state.start.y = quantapdf_canonical_float(options->start_center.y);
    state.end.x = quantapdf_canonical_float(options->end_center.x);
    state.end.y = quantapdf_canonical_float(options->end_center.y);
    state.start_radius =
        quantapdf_canonical_float(options->start_radius);
    state.end_radius =
        quantapdf_canonical_float(options->end_radius);
    if (!quantapdf_composer_resource_transform_normalize(
            &options->transform, &state.transform))
        return QUANTAPDF_ERROR_ARGUMENT;
    state.stop_count = options->stop_count;

    return quantapdf_composer_publish_paint(
        composer, &state, options->stops, out_paint_id);
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

quantapdf_status quantapdf_composer_add_raster(
    quantapdf_composer *composer,
    const quantapdf_composer_raster *raster,
    quantapdf_composer_image_id *out_image_id)
{
    quantapdf_composer_image_state image;
    quantapdf_status status;
    size_t source_components;
    size_t output_components;
    size_t source_row_bytes;
    size_t output_row_bytes;
    size_t pixel_count;
    size_t required_source_size;
    size_t main_size;
    size_t alpha_size = 0u;
    size_t total_size;
    size_t y;

    if (out_image_id != NULL)
        *out_image_id = 0u;
    if (composer == NULL || raster == NULL || out_image_id == NULL ||
        raster->struct_size < QUANTAPDF_COMPOSER_RASTER_V1_MIN_SIZE ||
        raster->pixels == NULL || raster->width == 0u ||
        raster->height == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    switch (raster->format) {
    case QUANTAPDF_COMPOSER_RASTER_GRAY8:
        source_components = 1u;
        output_components = 1u;
        break;
    case QUANTAPDF_COMPOSER_RASTER_RGB24:
        source_components = 3u;
        output_components = 3u;
        break;
    case QUANTAPDF_COMPOSER_RASTER_RGBA32:
        source_components = 4u;
        output_components = 3u;
        break;
    default:
        return QUANTAPDF_ERROR_ARGUMENT;
    }

    if ((size_t)raster->width > SIZE_MAX / source_components)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    source_row_bytes = (size_t)raster->width * source_components;
    if (raster->stride < source_row_bytes)
        return QUANTAPDF_ERROR_ARGUMENT;

    required_source_size = source_row_bytes;
    if (raster->height > 1u) {
        size_t rows_before_last = (size_t)raster->height - 1u;
        if (rows_before_last >
            (SIZE_MAX - source_row_bytes) / raster->stride)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        required_source_size =
            rows_before_last * raster->stride + source_row_bytes;
    }
    if (raster->size < required_source_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    if ((size_t)raster->height > SIZE_MAX / (size_t)raster->width)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixel_count = (size_t)raster->width * (size_t)raster->height;

    if ((size_t)raster->width > SIZE_MAX / output_components)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    output_row_bytes = (size_t)raster->width * output_components;
    if ((size_t)raster->height > SIZE_MAX / output_row_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    main_size = (size_t)raster->height * output_row_bytes;
    if (raster->format == QUANTAPDF_COMPOSER_RASTER_RGBA32)
        alpha_size = pixel_count;
    if (alpha_size > SIZE_MAX - main_size)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    total_size = main_size + alpha_size;

    if (composer->resource_bytes > composer->max_resource_bytes ||
        total_size > composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    memset(&image, 0, sizeof(image));
    image.data = (unsigned char *)malloc(main_size);
    if (image.data == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    if (alpha_size != 0u) {
        image.alpha_data = (unsigned char *)malloc(alpha_size);
        if (image.alpha_data == NULL) {
            free(image.data);
            return QUANTAPDF_ERROR_NOMEM;
        }
    }

    for (y = 0u; y < (size_t)raster->height; ++y) {
        const unsigned char *source =
            raster->pixels + y * raster->stride;
        unsigned char *destination =
            image.data + y * output_row_bytes;
        if (raster->format == QUANTAPDF_COMPOSER_RASTER_RGBA32) {
            size_t x;
            unsigned char *alpha =
                image.alpha_data + y * (size_t)raster->width;
            for (x = 0u; x < (size_t)raster->width; ++x) {
                destination[x * 3u] = source[x * 4u];
                destination[x * 3u + 1u] = source[x * 4u + 1u];
                destination[x * 3u + 2u] = source[x * 4u + 2u];
                alpha[x] = source[x * 4u + 3u];
            }
        } else {
            memcpy(destination, source, output_row_bytes);
        }
    }

    image.size = main_size;
    image.alpha_size = alpha_size;
    image.width = raster->width;
    image.height = raster->height;
    image.components = (int)output_components;
    image.has_alpha = alpha_size != 0u;
    image.format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;

    status = quantapdf_composer_reserve_image(composer);
    if (status != QUANTAPDF_OK) {
        free(image.alpha_data);
        free(image.data);
        return status;
    }

    composer->images[composer->image_count] = image;
    ++composer->image_count;
    composer->resource_bytes += total_size;
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
        (options->argb >> 24u) != 0xffu ||
        !quantapdf_graphics_state_id_valid_internal(
            composer, quantapdf_text_graphics_state_id(options)))
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
    operation.graphics_state_id = quantapdf_text_graphics_state_id(options);
    quantapdf_copy_text_options(&operation.value.text.options, options);
    operation.value.text.transform = *transform;
    composer->operations[composer->operation_count] = operation;
    ++composer->operation_count;
    composer->resource_bytes += text_size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_draw_form(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_composer_form_id form_id,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_form_draw_options *options)
{
    quantapdf_affine_transform normalized;
    quantapdf_composer_operation operation;
    quantapdf_status status;

    if (composer == NULL || page_index >= composer->page_count ||
        !quantapdf_form_id_valid(composer, form_id) ||
        options == NULL ||
        options->struct_size <
            QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_MIN_SIZE ||
        !quantapdf_graphics_state_id_valid_internal(
            composer, options->graphics_state_id) ||
        transform == NULL ||
        !quantapdf_composer_resource_transform_normalize(
            transform, &normalized))
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_composer_reserve_operation_internal(composer);
    if (status != QUANTAPDF_OK)
        return status;

    memset(&operation, 0, sizeof(operation));
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_FORM;
    operation.page_index = page_index;
    operation.graphics_state_id = options->graphics_state_id;
    operation.value.form.form_id = form_id;
    operation.value.form.transform = normalized;
    operation.value.form.options = *options;
    composer->operations[composer->operation_count++] = operation;
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
        options->fit > QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH ||
        !quantapdf_graphics_state_id_valid_internal(
            composer, quantapdf_image_graphics_state_id(options)))
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_composer_reserve_operation_internal(composer);
    if (status != QUANTAPDF_OK)
        return status;
    memset(&operation, 0, sizeof(operation));
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    operation.page_index = page_index;
    operation.bounds = *bounds;
    operation.graphics_state_id = quantapdf_image_graphics_state_id(options);
    operation.value.image.image_id = image_id;
    quantapdf_copy_image_options(&operation.value.image.options, options);
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
        !quantapdf_graphics_state_id_valid_internal(
            composer, quantapdf_path_graphics_state_id(options)) ||
        !quantapdf_paint_id_valid(
            composer, quantapdf_path_fill_paint_id(options)) ||
        !quantapdf_paint_id_valid(
            composer, quantapdf_path_stroke_paint_id(options)) ||
        (quantapdf_path_fill_paint_id(options) != 0u && !options->fill) ||
        (quantapdf_path_stroke_paint_id(options) != 0u && !options->stroke) ||
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
    operation.graphics_state_id = quantapdf_path_graphics_state_id(options);
    operation.value.path.commands = copied_commands;
    operation.value.path.command_count = command_count;
    quantapdf_copy_path_options(&operation.value.path.options, options);
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
    if (dash_pattern == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
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
    for (i = 0u; i < composer->paint_count; ++i)
        free(composer->paints[i].stops);
    for (i = 0u; i < composer->form_count; ++i)
        free(composer->forms[i].pdf_data);
    for (i = 0u; i < composer->clip_count; ++i)
        free(composer->clips[i].commands);
    free(composer->clips);
    free(composer->forms);
    free(composer->paints);
    free(composer->graphics_states);
    free(composer->fonts);
    free(composer->outlines);
    free(composer->links);
    free(composer->images);
    free(composer->operations);
    free(composer->pages);
    free(composer);
}
