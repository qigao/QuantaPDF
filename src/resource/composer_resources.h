#ifndef QUANTAPDF_COMPOSER_RESOURCES_H
#define QUANTAPDF_COMPOSER_RESOURCES_H

#include <quantapdf/quantapdf.h>

typedef enum quantapdf_composer_image_format_internal {
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_JPEG = 1,
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_PNG = 2,
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW = 3,
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW16 = 4 /* Big-endian unsigned samples. */
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
    int jpeg_color_transform; /* PDF DCT ColorTransform for three-component JPEGs. */
    int interpolate;
    uint32_t mask_width; /* Independent image mask grid, zero for associated alpha. */
    uint32_t mask_height;
    int mask_interpolate;
    unsigned mask_bits; /* Stencil expanded to bytes, RAW8, or big-endian RAW16. */
} quantapdf_composer_image_state;

static inline int quantapdf_image_alpha_layout(const quantapdf_composer_image_state *image,
                                               size_t image_pixels, size_t *mask_pixels)
{
    *mask_pixels = 0u;
    if (image->mask_width != 0u) {
        if (image->mask_height == 0u || image->mask_width > SIZE_MAX / image->mask_height ||
            (image->mask_interpolate != 0 && image->mask_interpolate != 1) ||
            (image->mask_bits != 1u && image->mask_bits != 8u && image->mask_bits != 16u))
            return 0;
        *mask_pixels = (size_t)image->mask_width * image->mask_height;
        const size_t stride = image->mask_bits == 16u ? 2u : 1u;
        return *mask_pixels <= SIZE_MAX / stride && image->has_alpha == 1 &&
            image->alpha_data != NULL && image->alpha_size == *mask_pixels * stride;
    }
    if (image->mask_height != 0u || image->mask_interpolate != 0 || image->mask_bits != 0u)
        return 0;
    return image->has_alpha == 1 ? image->alpha_data != NULL && image->alpha_size == image_pixels :
        image->has_alpha == 0 && image->alpha_data == NULL && image->alpha_size == 0u;
}

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
