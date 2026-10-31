#ifndef QUANTAPDF_COMPOSER_OPERATIONS_H
#define QUANTAPDF_COMPOSER_OPERATIONS_H

#include <quantapdf/quantapdf.h>

typedef struct quantapdf_composer_page_state {
    float width_points;
    float height_points;
    uint32_t background_argb;
    int suppress_background;
} quantapdf_composer_page_state;

typedef enum quantapdf_composer_operation_kind {
    QUANTAPDF_COMPOSER_OPERATION_TEXT = 1,
    QUANTAPDF_COMPOSER_OPERATION_IMAGE = 2,
    QUANTAPDF_COMPOSER_OPERATION_PATH = 3,
    QUANTAPDF_COMPOSER_OPERATION_EMBEDDED_TEXT = 4,
    QUANTAPDF_COMPOSER_OPERATION_GLYPH_RUN = 5,
    QUANTAPDF_COMPOSER_OPERATION_FORM = 6,
    QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES = 7
} quantapdf_composer_operation_kind;

typedef struct quantapdf_composer_text_operation {
    char *text_utf8;
    quantapdf_composer_text_options options;
    quantapdf_affine_transform transform;
} quantapdf_composer_text_operation;

typedef struct quantapdf_composer_image_operation {
    quantapdf_composer_image_id image_id;
    quantapdf_composer_image_options options;
} quantapdf_composer_image_operation;

typedef struct quantapdf_composer_path_operation {
    quantapdf_composer_path_command *commands;
    size_t command_count;
    quantapdf_composer_path_options options;
    float *dash_lengths;
    size_t dash_count;
    float dash_phase;
} quantapdf_composer_path_operation;

typedef struct quantapdf_composer_embedded_text_operation {
    char *text_utf8;
    quantapdf_composer_embedded_text_options options;
    quantapdf_affine_transform transform;
} quantapdf_composer_embedded_text_operation;

typedef struct quantapdf_composer_glyph_run_operation {
    quantapdf_composer_glyph *glyphs;
    size_t glyph_count;
    char *unicode_utf8;
    size_t unicode_size;
    quantapdf_point origin;
    quantapdf_composer_glyph_run_options options;
    quantapdf_affine_transform transform;
} quantapdf_composer_glyph_run_operation;

typedef struct quantapdf_composer_form_operation {
    quantapdf_composer_form_id form_id;
    quantapdf_affine_transform transform;
    quantapdf_composer_form_draw_options options;
} quantapdf_composer_form_operation;

typedef struct quantapdf_outline_glyph {
    quantapdf_composer_path_command *commands;
    size_t command_count;
} quantapdf_outline_glyph;
typedef struct quantapdf_text_outline_operation {
    quantapdf_outline_glyph *glyphs;
    size_t glyph_count;
    uint32_t argb;
} quantapdf_text_outline_operation;

typedef struct quantapdf_composer_operation {
    quantapdf_composer_operation_kind kind;
    size_t page_index;
    quantapdf_rect bounds;
    quantapdf_composer_graphics_state_id graphics_state_id;
    union {
        quantapdf_composer_text_operation text;
        quantapdf_composer_image_operation image;
        quantapdf_composer_path_operation path;
        quantapdf_composer_embedded_text_operation embedded_text;
        quantapdf_composer_glyph_run_operation glyph_run;
        quantapdf_composer_form_operation form;
        quantapdf_text_outline_operation text_outlines;
    } value;
} quantapdf_composer_operation;

#endif
