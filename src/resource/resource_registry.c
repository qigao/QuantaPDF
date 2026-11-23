#include "resource_registry.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int transform_equal(const quantapdf_affine_transform *a,
                           const quantapdf_affine_transform *b)
{
    return a->a == b->a && a->b == b->b && a->c == b->c &&
        a->d == b->d && a->e == b->e && a->f == b->f;
}

static int bytes_equal(const void *a, const void *b, size_t size)
{
    return size == 0 || memcmp(a, b, size) == 0;
}

static int image_equal(const quantapdf_composer_image_state *a,
                       const quantapdf_composer_image_state *b)
{
    return a->format == b->format && a->width == b->width &&
        a->height == b->height && a->components == b->components &&
        (a->format != QUANTAPDF_COMPOSER_IMAGE_FORMAT_JPEG ||
         a->jpeg_color_transform == b->jpeg_color_transform) &&
        a->has_alpha == b->has_alpha && a->interpolate == b->interpolate &&
        a->mask_width == b->mask_width && a->mask_height == b->mask_height &&
        a->mask_interpolate == b->mask_interpolate && a->mask_bits == b->mask_bits &&
        a->size == b->size &&
        a->alpha_size == b->alpha_size && bytes_equal(a->data, b->data, a->size) &&
        bytes_equal(a->alpha_data, b->alpha_data, a->alpha_size);
}

static int font_equal(const quantapdf_composer_font_state *a,
                      const quantapdf_composer_font_state *b)
{
    return a->size == b->size && bytes_equal(a->data, b->data, a->size);
}

static int graphics_state_equal(const quantapdf_composer_graphics_state *a,
                                const quantapdf_composer_graphics_state *b)
{
    return a->fill_alpha == b->fill_alpha && a->stroke_alpha == b->stroke_alpha &&
        a->blend_mode == b->blend_mode && a->clip_id == b->clip_id &&
        a->soft_mask_id == b->soft_mask_id && a->text_knockout == b->text_knockout;
}

static int soft_mask_equal(const quantapdf_composer_soft_mask_state *a,
                           const quantapdf_composer_soft_mask_state *b)
{
    return a->form_id == b->form_id && a->mode == b->mode &&
        transform_equal(&a->transform, &b->transform);
}

static int form_equal(const quantapdf_composer_form_state *a,
                      const quantapdf_composer_form_state *b)
{
    return a->width_points == b->width_points && a->height_points == b->height_points &&
        a->flags == b->flags && a->requires_pdf_16 == b->requires_pdf_16 &&
        a->pdf_size == b->pdf_size && bytes_equal(a->pdf_data, b->pdf_data, a->pdf_size);
}

static int command_equal(
    const quantapdf_composer_path_command *left,
    const quantapdf_composer_path_command *right)
{
    if (left->kind != right->kind)
        return 0;
    switch (left->kind) {
    case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
    case QUANTAPDF_COMPOSER_PATH_LINE_TO:
        return left->point1.x == right->point1.x &&
            left->point1.y == right->point1.y;
    case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
        return left->point1.x == right->point1.x &&
            left->point1.y == right->point1.y &&
            left->point2.x == right->point2.x &&
            left->point2.y == right->point2.y &&
            left->point3.x == right->point3.x &&
            left->point3.y == right->point3.y;
    case QUANTAPDF_COMPOSER_PATH_CLOSE:
        return 1;
    }
    return 0;
}

static int optional_clip_values_equal(const double *a, const double *b, size_t count)
{
    return a == b || (a != NULL && b != NULL && bytes_equal(a, b, count * sizeof(*a)));
}

static int mask_transfer_equal(const quantapdf_mask_transfer *a,
    const quantapdf_mask_transfer *b, size_t count)
{
    if (a == b) return 1;
    for (size_t i = 0u; i < count; ++i) {
        if (a[i].end != b[i].end || a[i].upper_bound != b[i].upper_bound ||
            a[i].encode[0] != b[i].encode[0] || a[i].encode[1] != b[i].encode[1] ||
            a[i].domain[0] != b[i].domain[0] || a[i].domain[1] != b[i].domain[1] ||
            a[i].has_range != b[i].has_range ||
            (a[i].has_range && (a[i].range[0] != b[i].range[0] || a[i].range[1] != b[i].range[1])) ||
            a[i].c0 != b[i].c0 || a[i].c1 != b[i].c1 || a[i].exponent != b[i].exponent ||
            a[i].sample_offset != b[i].sample_offset || a[i].sample_count != b[i].sample_count ||
            a[i].sample_bits != b[i].sample_bits || a[i].sample_order != b[i].sample_order ||
            a[i].program_offset != b[i].program_offset || a[i].program_size != b[i].program_size ||
            a[i].instruction_offset != b[i].instruction_offset || a[i].instruction_count != b[i].instruction_count ||
            !bytes_equal(a[i].sample_encode, b[i].sample_encode, sizeof(a[i].sample_encode)) ||
            !bytes_equal(a[i].sample_decode, b[i].sample_decode, sizeof(a[i].sample_decode)))
            return 0;
    }
    return 1;
}

static int clip_equal(const quantapdf_composer_clip_state *a,
                      const quantapdf_composer_clip_state *b)
{
    size_t i;
    if (a->kind != b->kind)
        return 0;
    if (a->kind == QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
        a->kind == QUANTAPDF_COMPOSER_CLIP_MASK_GROUP_INTERNAL)
        return a->member_count == b->member_count && a->knockout == b->knockout &&
            a->mask_mode == b->mask_mode && a->backdrop_luminosity == b->backdrop_luminosity &&
            a->transfer_count == b->transfer_count &&
            mask_transfer_equal(a->transfer, b->transfer, a->transfer_count) &&
            a->transfer_data_bytes == b->transfer_data_bytes &&
            bytes_equal(a->transfer_data, b->transfer_data, a->transfer_data_bytes) &&
            bytes_equal(a->members, b->members, a->member_count * sizeof(*a->members)) &&
            optional_clip_values_equal(a->member_opacities, b->member_opacities, a->member_count) &&
            optional_clip_values_equal(a->member_luminosities, b->member_luminosities, a->member_count);
    if (a->command_count != b->command_count || a->fill_rule != b->fill_rule ||
        !transform_equal(&a->transform, &b->transform))
        return 0;
    for (i = 0; i < a->command_count; ++i)
        if (!command_equal(&a->commands[i], &b->commands[i]))
            return 0;
    return 1;
}

static int paint_equal(
    const quantapdf_composer_paint_state *left,
    const quantapdf_composer_paint_state *right)
{
    size_t i;

    if (left->kind != right->kind ||
        left->transform.a != right->transform.a ||
        left->transform.b != right->transform.b ||
        left->transform.c != right->transform.c ||
        left->transform.d != right->transform.d ||
        left->transform.e != right->transform.e ||
        left->transform.f != right->transform.f)
        return 0;

    if (left->kind == QUANTAPDF_COMPOSER_PAINT_TILING_PATTERN_INTERNAL) {
        return left->tile_width == right->tile_width &&
            left->tile_height == right->tile_height &&
            left->x_step == right->x_step &&
            left->y_step == right->y_step &&
            left->pdf_size == right->pdf_size &&
            left->pdf_data != NULL && right->pdf_data != NULL &&
            memcmp(left->pdf_data, right->pdf_data, left->pdf_size) == 0;
    }

    if (left->start.x != right->start.x ||
        left->start.y != right->start.y ||
        left->end.x != right->end.x ||
        left->end.y != right->end.y ||
        left->start_radius != right->start_radius ||
        left->end_radius != right->end_radius ||
        left->stop_count != right->stop_count)
        return 0;
    for (i = 0u; i < left->stop_count; ++i) {
        if (left->stops[i].offset !=
                right->stops[i].offset ||
            left->stops[i].argb != right->stops[i].argb)
            return 0;
    }
    return 1;
}

static quantapdf_status reserve_slot(
    void *data, size_t count, size_t capacity, size_t element_size,
    size_t maximum_count, size_t initial_capacity,
    void **out_data, size_t *out_capacity)
{
    size_t next;
    void *grown;
    if (count >= maximum_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    *out_data = data;
    *out_capacity = capacity;
    if (count < capacity)
        return QUANTAPDF_OK;
    if (capacity > SIZE_MAX / 2)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    next = capacity == 0 ? initial_capacity : capacity * 2;
    if (next > maximum_count || next > SIZE_MAX / element_size)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = realloc(data, next * element_size);
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    *out_data = grown;
    *out_capacity = next;
    return QUANTAPDF_OK;
}

static void drop_image(quantapdf_composer_image_state *value)
{
    free(value->alpha_data);
    free(value->data);
    memset(value, 0, sizeof(*value));
}

quantapdf_composer_image_id quantapdf_resource_find_image(
    const quantapdf_resource_registry *registry, const quantapdf_composer_image_state *candidate)
{
    size_t i;
    if (registry == NULL || candidate == NULL)
        return 0;
    for (i = 0; i < registry->image_count; ++i)
        if (image_equal(&registry->images[i], candidate))
            return (quantapdf_composer_image_id)(i + 1);
    return 0;
}

quantapdf_status quantapdf_resource_intern_image(
    quantapdf_resource_registry *registry, quantapdf_composer_image_state *candidate,
    size_t available_bytes, quantapdf_composer_image_id *out_id, size_t *out_added_bytes)
{
    quantapdf_status status = QUANTAPDF_OK;
    quantapdf_composer_image_id existing = 0;
    size_t bytes = 0, capacity = 0;
    void *grown = NULL;
    if (out_id != NULL)
        *out_id = 0;
    if (out_added_bytes != NULL)
        *out_added_bytes = 0;
    if (registry == NULL || candidate == NULL || out_id == NULL || out_added_bytes == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (candidate->alpha_size > SIZE_MAX - candidate->size)
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    else
        bytes = candidate->size + candidate->alpha_size;
    if (status == QUANTAPDF_OK)
        existing = quantapdf_resource_find_image(registry, candidate);
    if (existing != 0) {
        *out_id = existing;
        drop_image(candidate);
        return QUANTAPDF_OK;
    }
    if (status == QUANTAPDF_OK && bytes > available_bytes)
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    if (status == QUANTAPDF_OK)
        status = reserve_slot(registry->images, registry->image_count,
            registry->image_capacity, sizeof(*registry->images), (size_t)UINT32_MAX, 8,
            &grown, &capacity);
    if (status != QUANTAPDF_OK) {
        drop_image(candidate);
        return status;
    }
    registry->images = (quantapdf_composer_image_state *)grown;
    registry->image_capacity = capacity;
    registry->images[registry->image_count++] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    *out_id = (quantapdf_composer_image_id)registry->image_count;
    *out_added_bytes = bytes;
    return QUANTAPDF_OK;
}

static void drop_font(quantapdf_composer_font_state *value)
{
    free(value->data);
    memset(value, 0, sizeof(*value));
}

quantapdf_composer_font_id quantapdf_resource_find_font(
    const quantapdf_resource_registry *registry, const quantapdf_composer_font_state *candidate)
{
    size_t i;
    if (registry == NULL || candidate == NULL)
        return 0;
    for (i = 0; i < registry->font_count; ++i)
        if (font_equal(&registry->fonts[i], candidate))
            return (quantapdf_composer_font_id)(i + 1);
    return 0;
}

quantapdf_status quantapdf_resource_intern_font(
    quantapdf_resource_registry *registry, quantapdf_composer_font_state *candidate,
    size_t available_bytes, quantapdf_composer_font_id *out_id, size_t *out_added_bytes)
{
    quantapdf_status status = QUANTAPDF_OK;
    quantapdf_composer_font_id existing = 0;
    size_t bytes = 0, capacity = 0;
    void *grown = NULL;
    if (out_id != NULL)
        *out_id = 0;
    if (out_added_bytes != NULL)
        *out_added_bytes = 0;
    if (registry == NULL || candidate == NULL || out_id == NULL || out_added_bytes == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    bytes = candidate->size;
    if (status == QUANTAPDF_OK)
        existing = quantapdf_resource_find_font(registry, candidate);
    if (existing != 0) {
        *out_id = existing;
        drop_font(candidate);
        return QUANTAPDF_OK;
    }
    if (status == QUANTAPDF_OK && bytes > available_bytes)
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    if (status == QUANTAPDF_OK)
        status = reserve_slot(registry->fonts, registry->font_count,
            registry->font_capacity, sizeof(*registry->fonts), (size_t)UINT32_MAX, 4,
            &grown, &capacity);
    if (status != QUANTAPDF_OK) {
        drop_font(candidate);
        return status;
    }
    registry->fonts = (quantapdf_composer_font_state *)grown;
    registry->font_capacity = capacity;
    registry->fonts[registry->font_count++] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    *out_id = (quantapdf_composer_font_id)registry->font_count;
    *out_added_bytes = bytes;
    return QUANTAPDF_OK;
}

static void drop_graphics_state(quantapdf_composer_graphics_state *value)
{
    memset(value, 0, sizeof(*value));
}

quantapdf_composer_graphics_state_id quantapdf_resource_find_graphics_state(
    const quantapdf_resource_registry *registry, const quantapdf_composer_graphics_state *candidate)
{
    size_t i;
    if (registry == NULL || candidate == NULL)
        return 0;
    for (i = 0; i < registry->graphics_state_count; ++i)
        if (graphics_state_equal(&registry->graphics_states[i], candidate))
            return (quantapdf_composer_graphics_state_id)(i + 1);
    return 0;
}

quantapdf_status quantapdf_resource_intern_graphics_state(
    quantapdf_resource_registry *registry, quantapdf_composer_graphics_state *candidate,
    size_t available_bytes, quantapdf_composer_graphics_state_id *out_id, size_t *out_added_bytes)
{
    quantapdf_status status = QUANTAPDF_OK;
    quantapdf_composer_graphics_state_id existing = 0;
    size_t bytes = 0, capacity = 0;
    void *grown = NULL;
    if (out_id != NULL)
        *out_id = 0;
    if (out_added_bytes != NULL)
        *out_added_bytes = 0;
    if (registry == NULL || candidate == NULL || out_id == NULL || out_added_bytes == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (status == QUANTAPDF_OK)
        existing = quantapdf_resource_find_graphics_state(registry, candidate);
    if (existing != 0) {
        *out_id = existing;
        drop_graphics_state(candidate);
        return QUANTAPDF_OK;
    }
    if (status == QUANTAPDF_OK && bytes > available_bytes)
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    if (status == QUANTAPDF_OK)
        status = reserve_slot(registry->graphics_states, registry->graphics_state_count,
            registry->graphics_state_capacity, sizeof(*registry->graphics_states), (size_t)SIZE_MAX, 8,
            &grown, &capacity);
    if (status != QUANTAPDF_OK) {
        drop_graphics_state(candidate);
        return status;
    }
    registry->graphics_states = (quantapdf_composer_graphics_state *)grown;
    registry->graphics_state_capacity = capacity;
    registry->graphics_states[registry->graphics_state_count++] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    *out_id = (quantapdf_composer_graphics_state_id)registry->graphics_state_count;
    *out_added_bytes = bytes;
    return QUANTAPDF_OK;
}

static void drop_soft_mask(quantapdf_composer_soft_mask_state *value)
{
    memset(value, 0, sizeof(*value));
}

quantapdf_composer_soft_mask_id quantapdf_resource_find_soft_mask(
    const quantapdf_resource_registry *registry, const quantapdf_composer_soft_mask_state *candidate)
{
    size_t i;
    if (registry == NULL || candidate == NULL)
        return 0;
    for (i = 0; i < registry->soft_mask_count; ++i)
        if (soft_mask_equal(&registry->soft_masks[i], candidate))
            return (quantapdf_composer_soft_mask_id)(i + 1);
    return 0;
}

quantapdf_status quantapdf_resource_intern_soft_mask(
    quantapdf_resource_registry *registry, quantapdf_composer_soft_mask_state *candidate,
    size_t available_bytes, quantapdf_composer_soft_mask_id *out_id, size_t *out_added_bytes)
{
    quantapdf_status status = QUANTAPDF_OK;
    quantapdf_composer_soft_mask_id existing = 0;
    size_t bytes = 0, capacity = 0;
    void *grown = NULL;
    if (out_id != NULL)
        *out_id = 0;
    if (out_added_bytes != NULL)
        *out_added_bytes = 0;
    if (registry == NULL || candidate == NULL || out_id == NULL || out_added_bytes == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (status == QUANTAPDF_OK)
        existing = quantapdf_resource_find_soft_mask(registry, candidate);
    if (existing != 0) {
        *out_id = existing;
        drop_soft_mask(candidate);
        return QUANTAPDF_OK;
    }
    if (status == QUANTAPDF_OK && bytes > available_bytes)
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    if (status == QUANTAPDF_OK)
        status = reserve_slot(registry->soft_masks, registry->soft_mask_count,
            registry->soft_mask_capacity, sizeof(*registry->soft_masks), (size_t)SIZE_MAX, 8,
            &grown, &capacity);
    if (status != QUANTAPDF_OK) {
        drop_soft_mask(candidate);
        return status;
    }
    registry->soft_masks = (quantapdf_composer_soft_mask_state *)grown;
    registry->soft_mask_capacity = capacity;
    registry->soft_masks[registry->soft_mask_count++] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    *out_id = (quantapdf_composer_soft_mask_id)registry->soft_mask_count;
    *out_added_bytes = bytes;
    return QUANTAPDF_OK;
}

static void drop_clip(quantapdf_composer_clip_state *value)
{
    free(value->commands);
    free(value->members);
    free(value->member_opacities);
    free(value->member_luminosities);
    free(value->transfer);
    free(value->transfer_data);
    memset(value, 0, sizeof(*value));
}

quantapdf_composer_clip_id quantapdf_resource_find_clip(
    const quantapdf_resource_registry *registry, const quantapdf_composer_clip_state *candidate)
{
    size_t i;
    if (registry == NULL || candidate == NULL)
        return 0;
    for (i = 0; i < registry->clip_count; ++i)
        if (clip_equal(&registry->clips[i], candidate))
            return (quantapdf_composer_clip_id)(i + 1);
    return 0;
}

quantapdf_status quantapdf_resource_intern_clip(
    quantapdf_resource_registry *registry, quantapdf_composer_clip_state *candidate,
    size_t available_bytes, quantapdf_composer_clip_id *out_id, size_t *out_added_bytes)
{
    quantapdf_status status = QUANTAPDF_OK;
    quantapdf_composer_clip_id existing = 0;
    size_t bytes = 0, capacity = 0;
    void *grown = NULL;
    if (out_id != NULL)
        *out_id = 0;
    if (out_added_bytes != NULL)
        *out_added_bytes = 0;
    if (registry == NULL || candidate == NULL || out_id == NULL || out_added_bytes == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (candidate->command_count > SIZE_MAX / sizeof(*candidate->commands) ||
        candidate->member_count > SIZE_MAX / sizeof(*candidate->members))
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    else {
        bytes = candidate->command_count * sizeof(*candidate->commands);
        if (candidate->member_count * sizeof(*candidate->members) > SIZE_MAX - bytes)
            status = QUANTAPDF_ERROR_UNSUPPORTED;
        else
            bytes += candidate->member_count * sizeof(*candidate->members);
    }
    const size_t sample_bytes = sizeof(double) * (size_t)(
        (candidate->member_opacities != NULL) + (candidate->member_luminosities != NULL));
    if (status == QUANTAPDF_OK && sample_bytes != 0u) {
        if (candidate->member_count > (SIZE_MAX - bytes) / sample_bytes)
            status = QUANTAPDF_ERROR_UNSUPPORTED;
        else
            bytes += candidate->member_count * sample_bytes;
    }
    if (status == QUANTAPDF_OK && candidate->transfer_count != 0u) {
        if (candidate->transfer_count > (SIZE_MAX - bytes) / sizeof(*candidate->transfer))
            status = QUANTAPDF_ERROR_UNSUPPORTED;
        else
            bytes += candidate->transfer_count * sizeof(*candidate->transfer);
    }
    if (status == QUANTAPDF_OK) {
        if (candidate->transfer_data_bytes > SIZE_MAX - bytes)
            status = QUANTAPDF_ERROR_UNSUPPORTED;
        else
            bytes += candidate->transfer_data_bytes;
    }
    if (status == QUANTAPDF_OK)
        existing = quantapdf_resource_find_clip(registry, candidate);
    if (existing != 0) {
        *out_id = existing;
        drop_clip(candidate);
        return QUANTAPDF_OK;
    }
    if (status == QUANTAPDF_OK && bytes > available_bytes)
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    if (status == QUANTAPDF_OK)
        status = reserve_slot(registry->clips, registry->clip_count,
            registry->clip_capacity, sizeof(*registry->clips), (size_t)SIZE_MAX, 8,
            &grown, &capacity);
    if (status != QUANTAPDF_OK) {
        drop_clip(candidate);
        return status;
    }
    registry->clips = (quantapdf_composer_clip_state *)grown;
    registry->clip_capacity = capacity;
    registry->clips[registry->clip_count++] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    *out_id = (quantapdf_composer_clip_id)registry->clip_count;
    *out_added_bytes = bytes;
    return QUANTAPDF_OK;
}

static void drop_paint(quantapdf_composer_paint_state *value)
{
    free(value->stops);
    free(value->pdf_data);
    memset(value, 0, sizeof(*value));
}

quantapdf_composer_paint_id quantapdf_resource_find_paint(
    const quantapdf_resource_registry *registry, const quantapdf_composer_paint_state *candidate)
{
    size_t i;
    if (registry == NULL || candidate == NULL)
        return 0;
    for (i = 0; i < registry->paint_count; ++i)
        if (paint_equal(&registry->paints[i], candidate))
            return (quantapdf_composer_paint_id)(i + 1);
    return 0;
}

quantapdf_status quantapdf_resource_intern_paint(
    quantapdf_resource_registry *registry, quantapdf_composer_paint_state *candidate,
    size_t available_bytes, quantapdf_composer_paint_id *out_id, size_t *out_added_bytes)
{
    quantapdf_status status = QUANTAPDF_OK;
    quantapdf_composer_paint_id existing = 0;
    size_t bytes = 0, capacity = 0;
    void *grown = NULL;
    if (out_id != NULL)
        *out_id = 0;
    if (out_added_bytes != NULL)
        *out_added_bytes = 0;
    if (registry == NULL || candidate == NULL || out_id == NULL || out_added_bytes == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (candidate->kind == QUANTAPDF_COMPOSER_PAINT_TILING_PATTERN_INTERNAL)
        bytes = candidate->pdf_size;
    else if (candidate->stop_count > SIZE_MAX / sizeof(*candidate->stops))
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    else
        bytes = candidate->stop_count * sizeof(*candidate->stops);
    if (status == QUANTAPDF_OK)
        existing = quantapdf_resource_find_paint(registry, candidate);
    if (existing != 0) {
        *out_id = existing;
        drop_paint(candidate);
        return QUANTAPDF_OK;
    }
    if (status == QUANTAPDF_OK && bytes > available_bytes)
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    if (status == QUANTAPDF_OK)
        status = reserve_slot(registry->paints, registry->paint_count,
            registry->paint_capacity, sizeof(*registry->paints), (size_t)SIZE_MAX, 8,
            &grown, &capacity);
    if (status != QUANTAPDF_OK) {
        drop_paint(candidate);
        return status;
    }
    registry->paints = (quantapdf_composer_paint_state *)grown;
    registry->paint_capacity = capacity;
    registry->paints[registry->paint_count++] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    *out_id = (quantapdf_composer_paint_id)registry->paint_count;
    *out_added_bytes = bytes;
    return QUANTAPDF_OK;
}

static void drop_form(quantapdf_composer_form_state *value)
{
    free(value->pdf_data);
    memset(value, 0, sizeof(*value));
}

quantapdf_composer_form_id quantapdf_resource_find_form(
    const quantapdf_resource_registry *registry, const quantapdf_composer_form_state *candidate)
{
    size_t i;
    if (registry == NULL || candidate == NULL)
        return 0;
    for (i = 0; i < registry->form_count; ++i)
        if (form_equal(&registry->forms[i], candidate))
            return (quantapdf_composer_form_id)(i + 1);
    return 0;
}

quantapdf_status quantapdf_resource_intern_form(
    quantapdf_resource_registry *registry, quantapdf_composer_form_state *candidate,
    size_t available_bytes, quantapdf_composer_form_id *out_id, size_t *out_added_bytes)
{
    quantapdf_status status = QUANTAPDF_OK;
    quantapdf_composer_form_id existing = 0;
    size_t bytes = 0, capacity = 0;
    void *grown = NULL;
    if (out_id != NULL)
        *out_id = 0;
    if (out_added_bytes != NULL)
        *out_added_bytes = 0;
    if (registry == NULL || candidate == NULL || out_id == NULL || out_added_bytes == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    bytes = candidate->pdf_size;
    if (status == QUANTAPDF_OK)
        existing = quantapdf_resource_find_form(registry, candidate);
    if (existing != 0) {
        *out_id = existing;
        drop_form(candidate);
        return QUANTAPDF_OK;
    }
    if (status == QUANTAPDF_OK && bytes > available_bytes)
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    if (status == QUANTAPDF_OK)
        status = reserve_slot(registry->forms, registry->form_count,
            registry->form_capacity, sizeof(*registry->forms), (size_t)SIZE_MAX, 4,
            &grown, &capacity);
    if (status != QUANTAPDF_OK) {
        drop_form(candidate);
        return status;
    }
    registry->forms = (quantapdf_composer_form_state *)grown;
    registry->form_capacity = capacity;
    registry->forms[registry->form_count++] = *candidate;
    memset(candidate, 0, sizeof(*candidate));
    *out_id = (quantapdf_composer_form_id)registry->form_count;
    *out_added_bytes = bytes;
    return QUANTAPDF_OK;
}

quantapdf_resource_mark quantapdf_resource_checkpoint(
    const quantapdf_resource_registry *registry)
{
    quantapdf_resource_mark mark = {0};
    assert(registry != NULL);
    mark.owner = registry;
    mark.image_count = registry->image_count;
    mark.font_count = registry->font_count;
    mark.graphics_state_count = registry->graphics_state_count;
    mark.soft_mask_count = registry->soft_mask_count;
    mark.clip_count = registry->clip_count;
    mark.paint_count = registry->paint_count;
    mark.form_count = registry->form_count;
    return mark;
}

void quantapdf_resource_rewind(
    quantapdf_resource_registry *registry, quantapdf_resource_mark mark)
{
    size_t i;
    assert(registry != NULL && mark.owner == registry);
    assert(mark.image_count <= registry->image_count);
    assert(mark.font_count <= registry->font_count);
    assert(mark.graphics_state_count <= registry->graphics_state_count);
    assert(mark.soft_mask_count <= registry->soft_mask_count);
    assert(mark.clip_count <= registry->clip_count);
    assert(mark.paint_count <= registry->paint_count);
    assert(mark.form_count <= registry->form_count);
    for (i = mark.form_count; i < registry->form_count; ++i)
        drop_form(&registry->forms[i]);
    registry->form_count = mark.form_count;
    for (i = mark.paint_count; i < registry->paint_count; ++i)
        drop_paint(&registry->paints[i]);
    registry->paint_count = mark.paint_count;
    for (i = mark.clip_count; i < registry->clip_count; ++i)
        drop_clip(&registry->clips[i]);
    registry->clip_count = mark.clip_count;
    for (i = mark.soft_mask_count; i < registry->soft_mask_count; ++i)
        drop_soft_mask(&registry->soft_masks[i]);
    registry->soft_mask_count = mark.soft_mask_count;
    for (i = mark.graphics_state_count; i < registry->graphics_state_count; ++i)
        drop_graphics_state(&registry->graphics_states[i]);
    registry->graphics_state_count = mark.graphics_state_count;
    for (i = mark.font_count; i < registry->font_count; ++i)
        drop_font(&registry->fonts[i]);
    registry->font_count = mark.font_count;
    for (i = mark.image_count; i < registry->image_count; ++i)
        drop_image(&registry->images[i]);
    registry->image_count = mark.image_count;
}

void quantapdf_resource_dispose(quantapdf_resource_registry *registry)
{
    quantapdf_resource_mark empty = {0};
    if (registry == NULL)
        return;
    empty.owner = registry;
    quantapdf_resource_rewind(registry, empty);
    free(registry->images);
    free(registry->fonts);
    free(registry->graphics_states);
    free(registry->soft_masks);
    free(registry->clips);
    free(registry->paints);
    free(registry->forms);
    memset(registry, 0, sizeof(*registry));
}
