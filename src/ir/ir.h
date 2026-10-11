#ifndef QUANTAPDF_IR_H
#define QUANTAPDF_IR_H

#include "composer_operations.h"
#include "../resource/composer_resources.h"

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

/* Unique payload ownership; zero initialization creates an empty model.
 * Const consumers borrow all reachable storage for the duration of the call. */
typedef struct quantapdf_ir {
    quantapdf_composer_page_state *pages;
    size_t page_count;
    quantapdf_composer_operation *operations;
    size_t operation_count;
    quantapdf_composer_image_state *images;
    size_t image_count;
    quantapdf_composer_font_state *fonts;
    size_t font_count;
    quantapdf_composer_graphics_state *graphics_states;
    size_t graphics_state_count;
    quantapdf_composer_soft_mask_state *soft_masks;
    size_t soft_mask_count;
    quantapdf_composer_clip_state *clips;
    size_t clip_count;
    quantapdf_composer_paint_state *paints;
    size_t paint_count;
    quantapdf_composer_form_state *forms;
    size_t form_count;
    quantapdf_composer_link_state *links;
    size_t link_count;
    quantapdf_composer_outline_state *outlines;
    size_t outline_count;
} quantapdf_ir;

#ifdef __cplusplus
extern "C" {
#endif

/* Releases committed payloads and resets the model. Does not release a context. */
void quantapdf_ir_dispose(quantapdf_ir *ir);

#ifdef __cplusplus
}
#endif

#endif
