#ifndef QUANTAPDF_RESOURCE_REGISTRY_H
#define QUANTAPDF_RESOURCE_REGISTRY_H

#include "composer_resources.h"

/* Unique ownership. Published payloads are immutable; append may move records. */
typedef struct quantapdf_resource_registry {
    quantapdf_composer_image_state *images;
    size_t image_count;
    size_t image_capacity;
    quantapdf_composer_font_state *fonts;
    size_t font_count;
    size_t font_capacity;
    quantapdf_composer_graphics_state *graphics_states;
    size_t graphics_state_count;
    size_t graphics_state_capacity;
    quantapdf_composer_soft_mask_state *soft_masks;
    size_t soft_mask_count;
    size_t soft_mask_capacity;
    quantapdf_composer_clip_state *clips;
    size_t clip_count;
    size_t clip_capacity;
    quantapdf_composer_paint_state *paints;
    size_t paint_count;
    size_t paint_capacity;
    quantapdf_composer_form_state *forms;
    size_t form_count;
    size_t form_capacity;
} quantapdf_resource_registry;

/* Lexical append checkpoint; invalid after moving or disposing its owner. */
typedef struct quantapdf_resource_mark {
    const quantapdf_resource_registry *owner;
    size_t image_count;
    size_t font_count;
    size_t graphics_state_count;
    size_t soft_mask_count;
    size_t clip_count;
    size_t paint_count;
    size_t form_count;
} quantapdf_resource_mark;

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_resource_mark quantapdf_resource_checkpoint(
    const quantapdf_resource_registry *registry);
/* mark must belong to registry and its current append lineage. */
void quantapdf_resource_rewind(
    quantapdf_resource_registry *registry, quantapdf_resource_mark mark);
void quantapdf_resource_dispose(quantapdf_resource_registry *registry);

/* Find borrows a validated candidate. Zero means absent.
 * Intern consumes and clears a uniquely owned candidate on every valid call,
 * including duplicate, budget and allocation outcomes. Invalid arguments retain
 * caller ownership. Success returns a one-based ID and newly charged bytes. */
quantapdf_composer_image_id quantapdf_resource_find_image(
    const quantapdf_resource_registry *registry, const quantapdf_composer_image_state *candidate);
quantapdf_status quantapdf_resource_intern_image(
    quantapdf_resource_registry *registry, quantapdf_composer_image_state *candidate,
    size_t available_bytes, quantapdf_composer_image_id *out_id, size_t *out_added_bytes);

quantapdf_composer_font_id quantapdf_resource_find_font(
    const quantapdf_resource_registry *registry, const quantapdf_composer_font_state *candidate);
quantapdf_status quantapdf_resource_intern_font(
    quantapdf_resource_registry *registry, quantapdf_composer_font_state *candidate,
    size_t available_bytes, quantapdf_composer_font_id *out_id, size_t *out_added_bytes);

quantapdf_composer_graphics_state_id quantapdf_resource_find_graphics_state(
    const quantapdf_resource_registry *registry, const quantapdf_composer_graphics_state *candidate);
quantapdf_status quantapdf_resource_intern_graphics_state(
    quantapdf_resource_registry *registry, quantapdf_composer_graphics_state *candidate,
    size_t available_bytes, quantapdf_composer_graphics_state_id *out_id, size_t *out_added_bytes);

quantapdf_composer_soft_mask_id quantapdf_resource_find_soft_mask(
    const quantapdf_resource_registry *registry, const quantapdf_composer_soft_mask_state *candidate);
quantapdf_status quantapdf_resource_intern_soft_mask(
    quantapdf_resource_registry *registry, quantapdf_composer_soft_mask_state *candidate,
    size_t available_bytes, quantapdf_composer_soft_mask_id *out_id, size_t *out_added_bytes);

quantapdf_composer_clip_id quantapdf_resource_find_clip(
    const quantapdf_resource_registry *registry, const quantapdf_composer_clip_state *candidate);
quantapdf_status quantapdf_resource_intern_clip(
    quantapdf_resource_registry *registry, quantapdf_composer_clip_state *candidate,
    size_t available_bytes, quantapdf_composer_clip_id *out_id, size_t *out_added_bytes);

quantapdf_composer_paint_id quantapdf_resource_find_paint(
    const quantapdf_resource_registry *registry, const quantapdf_composer_paint_state *candidate);
quantapdf_status quantapdf_resource_intern_paint(
    quantapdf_resource_registry *registry, quantapdf_composer_paint_state *candidate,
    size_t available_bytes, quantapdf_composer_paint_id *out_id, size_t *out_added_bytes);

quantapdf_composer_form_id quantapdf_resource_find_form(
    const quantapdf_resource_registry *registry, const quantapdf_composer_form_state *candidate);
quantapdf_status quantapdf_resource_intern_form(
    quantapdf_resource_registry *registry, quantapdf_composer_form_state *candidate,
    size_t available_bytes, quantapdf_composer_form_id *out_id, size_t *out_added_bytes);

#ifdef __cplusplus
}
#endif

#endif
