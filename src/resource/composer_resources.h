#ifndef QUANTAPDF_COMPOSER_RESOURCES_H
#define QUANTAPDF_COMPOSER_RESOURCES_H

#include <quantapdf/quantapdf.h>

typedef enum quantapdf_composer_image_format_internal {
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_JPEG = 1,
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_PNG = 2,
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW = 3
} quantapdf_composer_image_format_internal;

typedef struct quantapdf_composer_image_state {
    unsigned char *data;
    size_t size;
    unsigned char *alpha_data;
    size_t alpha_size;
    uint32_t width;
    uint32_t height;
    int components;
    int has_alpha;
    quantapdf_composer_image_format_internal format;
} quantapdf_composer_image_state;

typedef struct quantapdf_composer_font_state {
    unsigned char *data;
    size_t size;
} quantapdf_composer_font_state;

typedef struct quantapdf_composer_graphics_state {
    float fill_alpha;
    float stroke_alpha;
    quantapdf_composer_blend_mode blend_mode;
    quantapdf_composer_clip_id clip_id;
    quantapdf_composer_soft_mask_id soft_mask_id;
} quantapdf_composer_graphics_state;

typedef struct quantapdf_composer_soft_mask_state {
    quantapdf_composer_form_id form_id;
    quantapdf_composer_soft_mask_mode mode;
    quantapdf_affine_transform transform;
} quantapdf_composer_soft_mask_state;

typedef enum quantapdf_composer_clip_kind_internal {
    QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL = 1,
    QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL = 2
} quantapdf_composer_clip_kind_internal;

typedef struct quantapdf_composer_clip_state {
    quantapdf_composer_clip_kind_internal kind;
    quantapdf_composer_path_command *commands;
    size_t command_count;
    quantapdf_composer_fill_rule fill_rule;
    quantapdf_affine_transform transform;
    quantapdf_composer_clip_id *members;
    size_t member_count;
} quantapdf_composer_clip_state;

typedef enum quantapdf_composer_paint_kind_internal {
    QUANTAPDF_COMPOSER_PAINT_LINEAR_GRADIENT_INTERNAL = 1,
    QUANTAPDF_COMPOSER_PAINT_RADIAL_GRADIENT_INTERNAL = 2,
    QUANTAPDF_COMPOSER_PAINT_TILING_PATTERN_INTERNAL = 3
} quantapdf_composer_paint_kind_internal;

typedef struct quantapdf_composer_paint_state {
    quantapdf_composer_paint_kind_internal kind;
    quantapdf_point start;
    quantapdf_point end;
    float start_radius;
    float end_radius;
    quantapdf_affine_transform transform;
    quantapdf_composer_gradient_stop *stops;
    size_t stop_count;
    unsigned char *pdf_data;
    size_t pdf_size;
    float tile_width;
    float tile_height;
    float x_step;
    float y_step;
    int requires_pdf_16;
} quantapdf_composer_paint_state;

typedef struct quantapdf_composer_form_state {
    unsigned char *pdf_data;
    size_t pdf_size;
    float width_points;
    float height_points;
    uint32_t flags;
    int requires_pdf_16;
} quantapdf_composer_form_state;

#endif
