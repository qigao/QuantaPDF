#ifndef QUANTAPDF_COMPOSER_INTERNAL_H
#define QUANTAPDF_COMPOSER_INTERNAL_H

#include "../resource/composer_resources.h"
#include "../ir/composer_operations.h"

typedef enum quantapdf_composer_link_kind_internal {
    QUANTAPDF_COMPOSER_LINK_URI_INTERNAL = 1,
    QUANTAPDF_COMPOSER_LINK_PAGE_INTERNAL = 2
} quantapdf_composer_link_kind_internal;

typedef struct quantapdf_composer_link_state {
    size_t page_index;
    quantapdf_rect hotspot;
    quantapdf_composer_link_kind_internal kind;
    char *uri_utf8;
    size_t target_page_index;
    quantapdf_point target;
} quantapdf_composer_link_state;

typedef struct quantapdf_composer_outline_state {
    char *title_utf8;
    quantapdf_composer_outline_id parent_id;
    size_t target_page_index;
    quantapdf_point target;
    int is_open;
} quantapdf_composer_outline_state;

struct quantapdf_composer {
    quantapdf_context *context;
    size_t max_pages;
    size_t max_operations;
    size_t max_resource_bytes;
    size_t max_navigation_items;
    quantapdf_composer_page_state *pages;
    size_t page_count;
    size_t page_capacity;
    quantapdf_composer_operation *operations;
    size_t operation_count;
    size_t operation_capacity;
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
    quantapdf_composer_link_state *links;
    size_t link_count;
    size_t link_capacity;
    quantapdf_composer_outline_state *outlines;
    size_t outline_count;
    size_t outline_capacity;
    size_t resource_bytes;
};

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_composer_reserve_operation_internal(
    quantapdf_composer *composer);

quantapdf_status quantapdf_composer_reserve_operations_internal(
    quantapdf_composer *composer,
    size_t extra_operations);

int quantapdf_affine_transform_valid_internal(
    const quantapdf_affine_transform *transform);

quantapdf_affine_transform quantapdf_affine_identity_internal(void);

int quantapdf_graphics_state_id_valid_internal(
    const quantapdf_composer *composer,
    quantapdf_composer_graphics_state_id graphics_state_id);

#ifdef __cplusplus
}
#endif

#endif
