#ifndef QUANTAPDF_COMPOSER_INTERNAL_H
#define QUANTAPDF_COMPOSER_INTERNAL_H

#include "../ir/ir.h"

struct quantapdf_composer {
    quantapdf_context *context;
    size_t max_pages;
    size_t max_operations;
    size_t max_resource_bytes;
    size_t max_navigation_items;
    quantapdf_ir ir;
    size_t page_capacity;
    size_t operation_capacity;
    size_t link_capacity;
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
