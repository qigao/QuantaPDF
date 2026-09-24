#ifndef QUANTAPDF_QUANTAPDF_H
#define QUANTAPDF_QUANTAPDF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(QUANTAPDF_SHARED)
#  if defined(QUANTAPDF_BUILDING_LIBRARY)
#    define QUANTAPDF_API __declspec(dllexport)
#  else
#    define QUANTAPDF_API __declspec(dllimport)
#  endif
#elif defined(QUANTAPDF_SHARED) && \
    (defined(__GNUC__) || defined(__clang__))
#  define QUANTAPDF_API __attribute__((visibility("default")))
#else
#  define QUANTAPDF_API
#endif

#define QUANTAPDF_VERSION_MAJOR 2
#define QUANTAPDF_VERSION_MINOR 27
#define QUANTAPDF_VERSION_PATCH 0
#define QUANTAPDF_ABI_VERSION 2

typedef struct quantapdf_document quantapdf_document;
typedef struct quantapdf_page quantapdf_page;
typedef struct quantapdf_bitmap quantapdf_bitmap;
typedef struct quantapdf_text_page quantapdf_text_page;
typedef struct quantapdf_image_page quantapdf_image_page;
typedef struct quantapdf_link_page quantapdf_link_page;
typedef struct quantapdf_output quantapdf_output;
typedef struct quantapdf_outline quantapdf_outline;
typedef struct quantapdf_annotation_page quantapdf_annotation_page;
typedef struct quantapdf_pdf_edit quantapdf_pdf_edit;
typedef struct quantapdf_form quantapdf_form;
typedef struct quantapdf_composer quantapdf_composer;

typedef struct quantapdf_point {
    float x;
    float y;
} quantapdf_point;

typedef struct quantapdf_rect {
    float x0;
    float y0;
    float x1;
    float y1;
} quantapdf_rect;

typedef struct quantapdf_affine_transform {
    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
} quantapdf_affine_transform;

/*
 * size_t is intentional: its alignment matches the pre-existing size-tagged
 * option records' maximum alignment, so this V2 tail begins at the legacy V1
 * sizeof boundary on both 32-bit and 64-bit ABIs rather than occupying old
 * tail padding.
 */
typedef size_t quantapdf_composer_graphics_state_id;
typedef size_t quantapdf_composer_paint_id;
typedef size_t quantapdf_composer_clip_id;
typedef size_t quantapdf_composer_form_id;

typedef enum quantapdf_composer_blend_mode {
    QUANTAPDF_COMPOSER_BLEND_NORMAL = 0,
    QUANTAPDF_COMPOSER_BLEND_MULTIPLY = 1,
    QUANTAPDF_COMPOSER_BLEND_SCREEN = 2,
    QUANTAPDF_COMPOSER_BLEND_OVERLAY = 3,
    QUANTAPDF_COMPOSER_BLEND_DARKEN = 4,
    QUANTAPDF_COMPOSER_BLEND_LIGHTEN = 5
} quantapdf_composer_blend_mode;

typedef struct quantapdf_composer_graphics_state_options {
    size_t struct_size;
    float fill_alpha;
    float stroke_alpha;
    quantapdf_composer_blend_mode blend_mode;
    quantapdf_composer_clip_id clip_id;
} quantapdf_composer_graphics_state_options;

#define QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_graphics_state_options, blend_mode) + \
     sizeof(quantapdf_composer_blend_mode))
#define QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE \
    (offsetof(quantapdf_composer_graphics_state_options, clip_id))
#define QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_MIN_SIZE \
    (offsetof(quantapdf_composer_graphics_state_options, clip_id) + \
     sizeof(quantapdf_composer_clip_id))
#define QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE \
    (sizeof(quantapdf_composer_graphics_state_options))

#define QUANTAPDF_COMPOSER_DEFAULT_MAX_PAGES ((size_t)1024u)
#define QUANTAPDF_COMPOSER_DEFAULT_MAX_OPERATIONS ((size_t)1000000u)
#define QUANTAPDF_COMPOSER_DEFAULT_MAX_RESOURCE_BYTES \
    ((size_t)256u * (size_t)1024u * (size_t)1024u)
#define QUANTAPDF_COMPOSER_DEFAULT_MAX_NAVIGATION_ITEMS ((size_t)1000000u)

typedef struct quantapdf_composer_options {
    size_t struct_size;
    size_t max_pages;
    size_t max_operations;
    size_t max_resource_bytes;
    size_t max_navigation_items;
} quantapdf_composer_options;

#define QUANTAPDF_COMPOSER_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_options, max_resource_bytes) + sizeof(size_t))
#define QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE \
    (offsetof(quantapdf_composer_options, max_resource_bytes) + sizeof(size_t))
#define QUANTAPDF_COMPOSER_OPTIONS_V2_MIN_SIZE \
    (offsetof(quantapdf_composer_options, max_navigation_items) + sizeof(size_t))
#define QUANTAPDF_COMPOSER_OPTIONS_V2_SIZE \
    (sizeof(quantapdf_composer_options))

typedef struct quantapdf_composer_page_options {
    size_t struct_size;
    float width_points;
    float height_points;
    uint32_t background_argb;
} quantapdf_composer_page_options;

#define QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_page_options, background_argb) + \
     sizeof(uint32_t))
#define QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_page_options))

#define QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP UINT32_C(0x00000001)
#define QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED UINT32_C(0x00000002)
#define QUANTAPDF_COMPOSER_FORM_FLAG_KNOCKOUT UINT32_C(0x00000004)

typedef struct quantapdf_composer_form_options {
    size_t struct_size;
    float width_points;
    float height_points;
    uint32_t flags;
} quantapdf_composer_form_options;

#define QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_form_options, height_points) + sizeof(float))
#define QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE \
    (offsetof(quantapdf_composer_form_options, flags))
#define QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_MIN_SIZE \
    (offsetof(quantapdf_composer_form_options, flags) + sizeof(uint32_t))
#define QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE \
    (sizeof(quantapdf_composer_form_options))

typedef struct quantapdf_composer_form_draw_options {
    size_t struct_size;
    quantapdf_composer_graphics_state_id graphics_state_id;
} quantapdf_composer_form_draw_options;

#define QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_form_draw_options, graphics_state_id) + \
     sizeof(quantapdf_composer_graphics_state_id))
#define QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_form_draw_options))

typedef enum quantapdf_composer_font {
    QUANTAPDF_COMPOSER_FONT_HELVETICA = 0,
    QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD = 1,
    QUANTAPDF_COMPOSER_FONT_HELVETICA_OBLIQUE = 2,
    QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD_OBLIQUE = 3,
    QUANTAPDF_COMPOSER_FONT_TIMES_ROMAN = 4,
    QUANTAPDF_COMPOSER_FONT_TIMES_BOLD = 5,
    QUANTAPDF_COMPOSER_FONT_TIMES_ITALIC = 6,
    QUANTAPDF_COMPOSER_FONT_TIMES_BOLD_ITALIC = 7,
    QUANTAPDF_COMPOSER_FONT_COURIER = 8,
    QUANTAPDF_COMPOSER_FONT_COURIER_BOLD = 9,
    QUANTAPDF_COMPOSER_FONT_COURIER_OBLIQUE = 10,
    QUANTAPDF_COMPOSER_FONT_COURIER_BOLD_OBLIQUE = 11
} quantapdf_composer_font;

typedef enum quantapdf_composer_text_alignment {
    QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT = 0,
    QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER = 1,
    QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT = 2
} quantapdf_composer_text_alignment;

typedef struct quantapdf_composer_text_options {
    size_t struct_size;
    quantapdf_composer_font font;
    float font_size;
    uint32_t argb;
    float line_height_multiplier;
    quantapdf_composer_text_alignment alignment;
    int wrap;
    quantapdf_composer_graphics_state_id graphics_state_id;
} quantapdf_composer_text_options;

#define QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_text_options, wrap) + sizeof(int))
#define QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE \
    (offsetof(quantapdf_composer_text_options, graphics_state_id))
#define QUANTAPDF_COMPOSER_TEXT_OPTIONS_V2_MIN_SIZE \
    (offsetof(quantapdf_composer_text_options, graphics_state_id) + \
     sizeof(quantapdf_composer_graphics_state_id))
#define QUANTAPDF_COMPOSER_TEXT_OPTIONS_V2_SIZE \
    (sizeof(quantapdf_composer_text_options))

typedef uint32_t quantapdf_composer_font_id;

typedef struct quantapdf_composer_font_options {
    size_t struct_size;
} quantapdf_composer_font_options;

#define QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_MIN_SIZE \
    (sizeof(size_t))
#define QUANTAPDF_COMPOSER_FONT_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_font_options))

typedef struct quantapdf_composer_embedded_text_options {
    size_t struct_size;
    quantapdf_composer_font_id font_id;
    float font_size;
    uint32_t argb;
    float line_height_multiplier;
    quantapdf_composer_text_alignment alignment;
    int wrap;
    quantapdf_composer_graphics_state_id graphics_state_id;
} quantapdf_composer_embedded_text_options;

#define QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_embedded_text_options, wrap) + sizeof(int))
#define QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V1_SIZE \
    (offsetof(quantapdf_composer_embedded_text_options, graphics_state_id))
#define QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V2_MIN_SIZE \
    (offsetof(quantapdf_composer_embedded_text_options, graphics_state_id) + \
     sizeof(quantapdf_composer_graphics_state_id))
#define QUANTAPDF_COMPOSER_EMBEDDED_TEXT_OPTIONS_V2_SIZE \
    (sizeof(quantapdf_composer_embedded_text_options))

typedef struct quantapdf_composer_text_measurement {
    size_t struct_size;
    float width;
    float height;
    size_t line_count;
} quantapdf_composer_text_measurement;

#define QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_text_measurement, line_count) + sizeof(size_t))
#define QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_SIZE \
    (sizeof(quantapdf_composer_text_measurement))

typedef struct quantapdf_composer_glyph {
    uint32_t glyph_id;
    float x_advance;
    float y_advance;
    float x_offset;
    float y_offset;
    uint32_t unicode_offset;
    uint32_t unicode_length;
} quantapdf_composer_glyph;

#define QUANTAPDF_COMPOSER_GLYPH_V1_SIZE \
    (sizeof(quantapdf_composer_glyph))

typedef struct quantapdf_composer_glyph_run_options {
    size_t struct_size;
    quantapdf_composer_font_id font_id;
    float font_size;
    uint32_t argb;
    quantapdf_composer_graphics_state_id graphics_state_id;
} quantapdf_composer_glyph_run_options;

#define QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_glyph_run_options, argb) + sizeof(uint32_t))
#define QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V1_SIZE \
    (offsetof(quantapdf_composer_glyph_run_options, graphics_state_id))
#define QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V2_MIN_SIZE \
    (offsetof(quantapdf_composer_glyph_run_options, graphics_state_id) + \
     sizeof(quantapdf_composer_graphics_state_id))
#define QUANTAPDF_COMPOSER_GLYPH_RUN_OPTIONS_V2_SIZE \
    (sizeof(quantapdf_composer_glyph_run_options))

typedef uint32_t quantapdf_composer_image_id;

typedef enum quantapdf_composer_image_fit {
    QUANTAPDF_COMPOSER_IMAGE_FIT_CONTAIN = 0,
    QUANTAPDF_COMPOSER_IMAGE_FIT_COVER = 1,
    QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH = 2
} quantapdf_composer_image_fit;

typedef struct quantapdf_composer_image_options {
    size_t struct_size;
    quantapdf_composer_image_fit fit;
    quantapdf_composer_graphics_state_id graphics_state_id;
} quantapdf_composer_image_options;

#define QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_image_options, fit) + \
     sizeof(quantapdf_composer_image_fit))
#define QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE \
    (offsetof(quantapdf_composer_image_options, graphics_state_id))
#define QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE \
    (offsetof(quantapdf_composer_image_options, graphics_state_id) + \
     sizeof(quantapdf_composer_graphics_state_id))
#define QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE \
    (sizeof(quantapdf_composer_image_options))

typedef enum quantapdf_composer_raster_format {
    QUANTAPDF_COMPOSER_RASTER_GRAY8 = 1,
    QUANTAPDF_COMPOSER_RASTER_RGB24 = 2,
    QUANTAPDF_COMPOSER_RASTER_RGBA32 = 3
} quantapdf_composer_raster_format;

typedef struct quantapdf_composer_raster {
    size_t struct_size;
    quantapdf_composer_raster_format format;
    uint32_t width;
    uint32_t height;
    size_t stride;
    const unsigned char *pixels;
    size_t size;
} quantapdf_composer_raster;

#define QUANTAPDF_COMPOSER_RASTER_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_raster, size) + sizeof(size_t))
#define QUANTAPDF_COMPOSER_RASTER_V1_SIZE \
    (sizeof(quantapdf_composer_raster))

typedef enum quantapdf_composer_path_command_kind {
    QUANTAPDF_COMPOSER_PATH_MOVE_TO = 1,
    QUANTAPDF_COMPOSER_PATH_LINE_TO = 2,
    QUANTAPDF_COMPOSER_PATH_CUBIC_TO = 3,
    QUANTAPDF_COMPOSER_PATH_CLOSE = 4
} quantapdf_composer_path_command_kind;

typedef struct quantapdf_composer_path_command {
    quantapdf_composer_path_command_kind kind;
    quantapdf_point point1;
    quantapdf_point point2;
    quantapdf_point point3;
} quantapdf_composer_path_command;

typedef enum quantapdf_composer_fill_rule {
    QUANTAPDF_COMPOSER_FILL_NONZERO = 0,
    QUANTAPDF_COMPOSER_FILL_EVEN_ODD = 1
} quantapdf_composer_fill_rule;

typedef struct quantapdf_composer_clip_options {
    size_t struct_size;
    quantapdf_composer_fill_rule fill_rule;
    quantapdf_affine_transform transform;
} quantapdf_composer_clip_options;

#define QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_clip_options, transform) + \
     sizeof(quantapdf_affine_transform))
#define QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_clip_options))

typedef enum quantapdf_composer_line_cap {
    QUANTAPDF_COMPOSER_LINE_CAP_BUTT = 0,
    QUANTAPDF_COMPOSER_LINE_CAP_ROUND = 1,
    QUANTAPDF_COMPOSER_LINE_CAP_SQUARE = 2
} quantapdf_composer_line_cap;

typedef enum quantapdf_composer_line_join {
    QUANTAPDF_COMPOSER_LINE_JOIN_MITER = 0,
    QUANTAPDF_COMPOSER_LINE_JOIN_ROUND = 1,
    QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL = 2
} quantapdf_composer_line_join;

typedef struct quantapdf_composer_path_options {
    size_t struct_size;
    int stroke;
    int fill;
    uint32_t stroke_argb;
    uint32_t fill_argb;
    float stroke_width;
    quantapdf_composer_fill_rule fill_rule;
    quantapdf_composer_line_cap line_cap;
    quantapdf_composer_line_join line_join;
    float miter_limit;
    quantapdf_composer_graphics_state_id graphics_state_id;
    quantapdf_composer_paint_id fill_paint_id;
    quantapdf_composer_paint_id stroke_paint_id;
    quantapdf_affine_transform transform;
} quantapdf_composer_path_options;

#define QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_path_options, miter_limit) + sizeof(float))
#define QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE \
    (offsetof(quantapdf_composer_path_options, graphics_state_id))
#define QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE \
    (offsetof(quantapdf_composer_path_options, graphics_state_id) + \
     sizeof(quantapdf_composer_graphics_state_id))
#define QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_SIZE \
    (offsetof(quantapdf_composer_path_options, fill_paint_id))
#define QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE \
    (offsetof(quantapdf_composer_path_options, stroke_paint_id) + \
     sizeof(quantapdf_composer_paint_id))
#define QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE \
    (offsetof(quantapdf_composer_path_options, transform))
#define QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE \
    (offsetof(quantapdf_composer_path_options, transform) + \
     sizeof(quantapdf_affine_transform))
#define QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE \
    (sizeof(quantapdf_composer_path_options))

#define QUANTAPDF_COMPOSER_MAX_GRADIENT_STOPS ((size_t)64u)

typedef struct quantapdf_composer_gradient_stop {
    float offset;
    uint32_t argb;
} quantapdf_composer_gradient_stop;

#define QUANTAPDF_COMPOSER_GRADIENT_STOP_V1_SIZE \
    (sizeof(quantapdf_composer_gradient_stop))

typedef struct quantapdf_composer_linear_gradient_options {
    size_t struct_size;
    quantapdf_point start;
    quantapdf_point end;
    quantapdf_affine_transform transform;
    const quantapdf_composer_gradient_stop *stops;
    size_t stop_count;
} quantapdf_composer_linear_gradient_options;

#define QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_linear_gradient_options, stop_count) + \
     sizeof(size_t))
#define QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_linear_gradient_options))

typedef struct quantapdf_composer_radial_gradient_options {
    size_t struct_size;
    quantapdf_point start_center;
    float start_radius;
    quantapdf_point end_center;
    float end_radius;
    quantapdf_affine_transform transform;
    const quantapdf_composer_gradient_stop *stops;
    size_t stop_count;
} quantapdf_composer_radial_gradient_options;

#define QUANTAPDF_COMPOSER_RADIAL_GRADIENT_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_radial_gradient_options, stop_count) + \
     sizeof(size_t))
#define QUANTAPDF_COMPOSER_RADIAL_GRADIENT_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_radial_gradient_options))

typedef struct quantapdf_composer_tiling_pattern_options {
    size_t struct_size;
    float width_points;
    float height_points;
    float x_step;
    float y_step;
    quantapdf_affine_transform transform;
} quantapdf_composer_tiling_pattern_options;

#define QUANTAPDF_COMPOSER_TILING_PATTERN_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_tiling_pattern_options, transform) + \
     sizeof(quantapdf_affine_transform))
#define QUANTAPDF_COMPOSER_TILING_PATTERN_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_tiling_pattern_options))

#define QUANTAPDF_COMPOSER_MAX_DASH_COUNT ((size_t)64u)

typedef struct quantapdf_composer_dash_pattern {
    size_t struct_size;
    const float *lengths;
    size_t length_count;
    float phase;
} quantapdf_composer_dash_pattern;

#define QUANTAPDF_COMPOSER_DASH_PATTERN_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_dash_pattern, phase) + sizeof(float))
#define QUANTAPDF_COMPOSER_DASH_PATTERN_V1_SIZE \
    (sizeof(quantapdf_composer_dash_pattern))

typedef struct quantapdf_composer_svg_options {
    size_t struct_size;
} quantapdf_composer_svg_options;

#define QUANTAPDF_COMPOSER_SVG_OPTIONS_V1_MIN_SIZE \
    (sizeof(size_t))
#define QUANTAPDF_COMPOSER_SVG_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_svg_options))

typedef enum quantapdf_barcode_kind {
    QUANTAPDF_BARCODE_CODE_128B = 1,
    QUANTAPDF_BARCODE_CODE_39 = 2,
    QUANTAPDF_BARCODE_EAN_13 = 3,
    QUANTAPDF_BARCODE_UPC_A = 4,
    QUANTAPDF_BARCODE_EAN_8 = 5,
    QUANTAPDF_BARCODE_UPC_E = 6,
    QUANTAPDF_BARCODE_QR = 7
} quantapdf_barcode_kind;

typedef struct quantapdf_barcode_options {
    size_t struct_size;
    uint32_t argb;
} quantapdf_barcode_options;

#define QUANTAPDF_BARCODE_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_barcode_options, argb) + sizeof(uint32_t))
#define QUANTAPDF_BARCODE_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_barcode_options))

typedef uint32_t quantapdf_composer_outline_id;

typedef struct quantapdf_composer_outline_options {
    size_t struct_size;
    quantapdf_composer_outline_id parent_id;
    size_t target_page_index;
    quantapdf_point target;
    int is_open;
} quantapdf_composer_outline_options;

#define QUANTAPDF_COMPOSER_OUTLINE_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_composer_outline_options, is_open) + sizeof(int))
#define QUANTAPDF_COMPOSER_OUTLINE_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_composer_outline_options))

typedef struct quantapdf_page_crop {
    size_t struct_size;
    int page_index;
    quantapdf_rect bounds;
} quantapdf_page_crop;

typedef struct quantapdf_page_trim {
    size_t struct_size;
    int page_index;
    quantapdf_rect bounds;
} quantapdf_page_trim;

typedef struct quantapdf_page_poster_split {
    size_t struct_size;
    int page_index;
    size_t columns;
    size_t rows;
} quantapdf_page_poster_split;

#define QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES \
    ((size_t)64u * (size_t)1024u * (size_t)1024u)

typedef struct quantapdf_image_recompression_options {
    size_t struct_size;
    int jpeg_quality;
    size_t max_decoded_bytes_per_image;
} quantapdf_image_recompression_options;

#define QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_image_recompression_options, jpeg_quality) + \
     sizeof(int))
#define QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_image_recompression_options))

typedef enum quantapdf_flatten_flag {
    QUANTAPDF_FLATTEN_ANNOTATIONS = 1u << 0,
    QUANTAPDF_FLATTEN_WIDGETS = 1u << 1
} quantapdf_flatten_flag;

typedef enum quantapdf_audit_finding {
    QUANTAPDF_AUDIT_JAVASCRIPT_ACTION = 1u << 0,
    QUANTAPDF_AUDIT_LAUNCH_ACTION = 1u << 1,
    QUANTAPDF_AUDIT_EXTERNAL_ACTION = 1u << 2,
    QUANTAPDF_AUDIT_OTHER_ACTION = 1u << 3,
    QUANTAPDF_AUDIT_EMBEDDED_FILE = 1u << 4,
    QUANTAPDF_AUDIT_XFA = 1u << 5,
    QUANTAPDF_AUDIT_RICH_MEDIA = 1u << 6,
    QUANTAPDF_AUDIT_SIGNATURE = 1u << 7,
    QUANTAPDF_AUDIT_ENCRYPTION = 1u << 8
} quantapdf_audit_finding;

typedef enum quantapdf_sanitize_flag {
    QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS = 1u << 0,
    QUANTAPDF_SANITIZE_LAUNCH_ACTIONS = 1u << 1,
    QUANTAPDF_SANITIZE_EXTERNAL_ACTIONS = 1u << 2,
    QUANTAPDF_SANITIZE_OTHER_ACTIONS = 1u << 3,
    QUANTAPDF_SANITIZE_EMBEDDED_FILES = 1u << 4,
    QUANTAPDF_SANITIZE_XFA = 1u << 5,
    QUANTAPDF_SANITIZE_RICH_MEDIA = 1u << 6,
    QUANTAPDF_SANITIZE_ALL = (1u << 7) - 1u
} quantapdf_sanitize_flag;

typedef struct quantapdf_audit_result {
    size_t struct_size;
    uint32_t findings;
} quantapdf_audit_result;

#define QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE \
    (offsetof(quantapdf_audit_result, findings) + sizeof(uint32_t))
#define QUANTAPDF_AUDIT_RESULT_V1_SIZE (sizeof(quantapdf_audit_result))

typedef enum quantapdf_encryption_method {
    QUANTAPDF_ENCRYPTION_AES_256 = 1
} quantapdf_encryption_method;

typedef enum quantapdf_pdf_permission {
    QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION = 1u << 0,
    QUANTAPDF_PERMISSION_MODIFY_OTHER = 1u << 1,
    QUANTAPDF_PERMISSION_COPY = 1u << 2,
    QUANTAPDF_PERMISSION_ANNOTATE_AND_FILL_FORMS = 1u << 3,
    QUANTAPDF_PERMISSION_FILL_FORMS = 1u << 4,
    QUANTAPDF_PERMISSION_ASSEMBLE = 1u << 5,
    QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY = 1u << 6,
    QUANTAPDF_PERMISSION_ALL = (1u << 7) - 1u
} quantapdf_pdf_permission;

typedef struct quantapdf_encryption_options {
    size_t struct_size;
    quantapdf_encryption_method method;
    const char *user_password_utf8;
    const char *owner_password_utf8;
    uint32_t permissions;
    int encrypt_metadata;
} quantapdf_encryption_options;

#define QUANTAPDF_ENCRYPTION_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_encryption_options, encrypt_metadata) + sizeof(int))
#define QUANTAPDF_ENCRYPTION_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_encryption_options))

/*
 * These types are traversed as C arrays and therefore have fixed V1 layouts.
 * A future extension must use a new element type/API or an explicit stride.
 */
#define QUANTAPDF_PAGE_CROP_V1_MIN_SIZE \
    (offsetof(quantapdf_page_crop, bounds) + sizeof(quantapdf_rect))
#define QUANTAPDF_PAGE_CROP_V1_SIZE (sizeof(quantapdf_page_crop))
#define QUANTAPDF_PAGE_TRIM_V1_MIN_SIZE \
    (offsetof(quantapdf_page_trim, bounds) + sizeof(quantapdf_rect))
#define QUANTAPDF_PAGE_TRIM_V1_SIZE (sizeof(quantapdf_page_trim))
#define QUANTAPDF_PAGE_POSTER_SPLIT_V1_MIN_SIZE \
    (offsetof(quantapdf_page_poster_split, rows) + sizeof(size_t))
#define QUANTAPDF_PAGE_POSTER_SPLIT_V1_SIZE \
    (sizeof(quantapdf_page_poster_split))

typedef struct quantapdf_quad {
    quantapdf_point ul;
    quantapdf_point ur;
    quantapdf_point ll;
    quantapdf_point lr;
} quantapdf_quad;

typedef enum quantapdf_page_box {
    QUANTAPDF_PAGE_BOX_MEDIA = 0,
    QUANTAPDF_PAGE_BOX_CROP = 1
} quantapdf_page_box;

typedef struct quantapdf_render_options {
    size_t struct_size;
    float dpi;
    float rotation_degrees;
    int clip_enabled;
    quantapdf_rect clip;
    int alpha;
} quantapdf_render_options;

typedef struct quantapdf_text_block_info {
    size_t struct_size;
    quantapdf_rect bounds;
} quantapdf_text_block_info;

typedef struct quantapdf_text_line_info {
    size_t struct_size;
    quantapdf_rect bounds;
    float direction_x;
    float direction_y;
    int writing_mode;
} quantapdf_text_line_info;

typedef struct quantapdf_text_span_info {
    size_t struct_size;
    quantapdf_rect bounds;
    float font_size;
    uint32_t argb;
    uint32_t bidi_level;
} quantapdf_text_span_info;

typedef struct quantapdf_search_result {
    size_t struct_size;
    quantapdf_quad quad;
} quantapdf_search_result;

#define QUANTAPDF_SEARCH_RESULT_V1_MIN_SIZE \
    (offsetof(quantapdf_search_result, quad) + sizeof(quantapdf_quad))
#define QUANTAPDF_SEARCH_RESULT_V1_SIZE (sizeof(quantapdf_search_result))

typedef struct quantapdf_image_info {
    size_t struct_size;
    quantapdf_quad quad;
    int pixel_width;
    int pixel_height;
    int components;
    int bits_per_component;
    int has_alpha;
} quantapdf_image_info;

typedef enum quantapdf_link_kind {
    QUANTAPDF_LINK_URI = 1,
    QUANTAPDF_LINK_INTERNAL = 2
} quantapdf_link_kind;

typedef struct quantapdf_link_info {
    size_t struct_size;
    quantapdf_rect hotspot;
    quantapdf_link_kind kind;
    int target_page;
    quantapdf_point target;
} quantapdf_link_info;

typedef enum quantapdf_outline_destination_kind {
    QUANTAPDF_OUTLINE_DESTINATION_NONE = 0,
    QUANTAPDF_OUTLINE_DESTINATION_INTERNAL = 1,
    QUANTAPDF_OUTLINE_DESTINATION_URI = 2
} quantapdf_outline_destination_kind;

typedef struct quantapdf_outline_info {
    size_t struct_size;
    size_t parent_index;
    size_t first_child_index;
    size_t next_sibling_index;
    quantapdf_outline_destination_kind destination_kind;
    int target_page;
    quantapdf_point target;
    int is_open;
} quantapdf_outline_info;

typedef enum quantapdf_annotation_type {
    QUANTAPDF_ANNOTATION_UNKNOWN = 0,
    QUANTAPDF_ANNOTATION_TEXT = 1,
    QUANTAPDF_ANNOTATION_FREE_TEXT = 2,
    QUANTAPDF_ANNOTATION_LINE = 3,
    QUANTAPDF_ANNOTATION_SQUARE = 4,
    QUANTAPDF_ANNOTATION_CIRCLE = 5,
    QUANTAPDF_ANNOTATION_POLYGON = 6,
    QUANTAPDF_ANNOTATION_POLY_LINE = 7,
    QUANTAPDF_ANNOTATION_HIGHLIGHT = 8,
    QUANTAPDF_ANNOTATION_UNDERLINE = 9,
    QUANTAPDF_ANNOTATION_SQUIGGLY = 10,
    QUANTAPDF_ANNOTATION_STRIKE_OUT = 11,
    QUANTAPDF_ANNOTATION_REDACT = 12,
    QUANTAPDF_ANNOTATION_STAMP = 13,
    QUANTAPDF_ANNOTATION_CARET = 14,
    QUANTAPDF_ANNOTATION_INK = 15,
    QUANTAPDF_ANNOTATION_FILE_ATTACHMENT = 16,
    QUANTAPDF_ANNOTATION_SOUND = 17,
    QUANTAPDF_ANNOTATION_MOVIE = 18,
    QUANTAPDF_ANNOTATION_RICH_MEDIA = 19,
    QUANTAPDF_ANNOTATION_SCREEN = 20,
    QUANTAPDF_ANNOTATION_PRINTER_MARK = 21,
    QUANTAPDF_ANNOTATION_TRAP_NET = 22,
    QUANTAPDF_ANNOTATION_WATERMARK = 23,
    QUANTAPDF_ANNOTATION_3D = 24,
    QUANTAPDF_ANNOTATION_PROJECTION = 25
} quantapdf_annotation_type;

typedef struct quantapdf_annotation_info {
    size_t struct_size;
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
} quantapdf_annotation_info;

typedef struct quantapdf_annotation_ref {
    uint64_t opaque[2];
} quantapdf_annotation_ref;

typedef enum quantapdf_annotation_update_field {
    QUANTAPDF_ANNOTATION_UPDATE_BOUNDS = 1u << 0,
    QUANTAPDF_ANNOTATION_UPDATE_FLAGS = 1u << 1,
    QUANTAPDF_ANNOTATION_UPDATE_CONTENTS = 1u << 2
} quantapdf_annotation_update_field;

typedef struct quantapdf_annotation_create_options {
    size_t struct_size;
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} quantapdf_annotation_create_options;

typedef struct quantapdf_annotation_update {
    size_t struct_size;
    uint32_t fields;
    quantapdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} quantapdf_annotation_update;

typedef enum quantapdf_form_field_type {
    QUANTAPDF_FORM_FIELD_UNKNOWN = 0,
    QUANTAPDF_FORM_FIELD_PUSH_BUTTON = 1,
    QUANTAPDF_FORM_FIELD_CHECKBOX = 2,
    QUANTAPDF_FORM_FIELD_RADIO_BUTTON = 3,
    QUANTAPDF_FORM_FIELD_TEXT = 4,
    QUANTAPDF_FORM_FIELD_COMBO_BOX = 5,
    QUANTAPDF_FORM_FIELD_LIST_BOX = 6,
    QUANTAPDF_FORM_FIELD_SIGNATURE = 7
} quantapdf_form_field_type;

typedef enum quantapdf_form_value_presence {
    QUANTAPDF_FORM_VALUE_NOT_APPLICABLE = 0,
    QUANTAPDF_FORM_VALUE_MISSING = 1,
    QUANTAPDF_FORM_VALUE_PRESENT = 2
} quantapdf_form_value_presence;

typedef enum quantapdf_form_value_kind {
    QUANTAPDF_FORM_VALUE_UTF8 = 1,
    QUANTAPDF_FORM_VALUE_OPTION = 2
} quantapdf_form_value_kind;

typedef struct quantapdf_form_value_info {
    size_t struct_size;
    quantapdf_form_value_kind kind;
    size_t option_index;
} quantapdf_form_value_info;

typedef enum quantapdf_form_option_kind {
    QUANTAPDF_FORM_OPTION_BUTTON_STATE = 1,
    QUANTAPDF_FORM_OPTION_CHOICE = 2
} quantapdf_form_option_kind;

typedef struct quantapdf_form_option_info {
    size_t struct_size;
    quantapdf_form_option_kind kind;
} quantapdf_form_option_info;

typedef struct quantapdf_form_field_info {
    size_t struct_size;
    quantapdf_form_field_type type;
    uint32_t flags;
    quantapdf_form_value_presence value_presence;
    size_t value_count;
    size_t option_count;
    size_t widget_count;
    int is_multiselect;
    int is_signed;
} quantapdf_form_field_info;

typedef struct quantapdf_form_widget_info {
    size_t struct_size;
    size_t field_index;
    int page_index;
    quantapdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} quantapdf_form_widget_info;

typedef struct quantapdf_form_field_ref {
    uint64_t opaque[2];
} quantapdf_form_field_ref;

typedef struct quantapdf_form_value_input {
    size_t struct_size;
    quantapdf_form_value_kind kind;
    size_t option_index;
    const char *utf8;
    size_t utf8_size;
} quantapdf_form_value_input;

#define QUANTAPDF_FORM_VALUE_INPUT_V1_MIN_SIZE \
    (offsetof(quantapdf_form_value_input, utf8_size) + sizeof(size_t))
#define QUANTAPDF_FORM_VALUE_INPUT_V1_SIZE \
    (sizeof(quantapdf_form_value_input))

typedef struct quantapdf_form_value_update {
    size_t struct_size;
    quantapdf_form_value_presence presence;
    const quantapdf_form_value_input *values;
    size_t value_count;
} quantapdf_form_value_update;

typedef enum quantapdf_metadata_field {
    QUANTAPDF_METADATA_TITLE = 1,
    QUANTAPDF_METADATA_AUTHOR = 2,
    QUANTAPDF_METADATA_SUBJECT = 3,
    QUANTAPDF_METADATA_KEYWORDS = 4,
    QUANTAPDF_METADATA_CREATOR = 5,
    QUANTAPDF_METADATA_PRODUCER = 6,
    QUANTAPDF_METADATA_CREATION_DATE = 7,
    QUANTAPDF_METADATA_MODIFICATION_DATE = 8
} quantapdf_metadata_field;

typedef enum quantapdf_status {
    QUANTAPDF_OK = 0,
    QUANTAPDF_ERROR_ARGUMENT = 1,
    QUANTAPDF_ERROR_IO = 2,
    QUANTAPDF_ERROR_PASSWORD = 3,
    QUANTAPDF_ERROR_FORMAT = 4,
    QUANTAPDF_ERROR_UNSUPPORTED = 5,
    QUANTAPDF_ERROR_NOMEM = 6,
    QUANTAPDF_ERROR_BACKEND = 7,
    QUANTAPDF_ERROR_STATE = 8
} quantapdf_status;

typedef quantapdf_status (*quantapdf_composer_form_builder_fn)(
    quantapdf_composer *form_composer,
    size_t page_index,
    void *user_data);

typedef quantapdf_status (*quantapdf_composer_pattern_builder_fn)(
    quantapdf_composer *tile_composer,
    size_t page_index,
    void *user_data);

QUANTAPDF_API quantapdf_status quantapdf_composer_create(
    const quantapdf_composer_options *options,
    quantapdf_composer **out_composer);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_page(
    quantapdf_composer *composer,
    const quantapdf_composer_page_options *options,
    size_t *out_page_index);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_form(
    quantapdf_composer *composer,
    const quantapdf_composer_form_options *options,
    quantapdf_composer_form_builder_fn builder,
    void *user_data,
    quantapdf_composer_form_id *out_form_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_font(
    quantapdf_composer *composer,
    const unsigned char *font_data,
    size_t font_size,
    const quantapdf_composer_font_options *options,
    quantapdf_composer_font_id *out_font_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_image(
    quantapdf_composer *composer,
    const unsigned char *data,
    size_t size,
    quantapdf_composer_image_id *out_image_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_raster(
    quantapdf_composer *composer,
    const quantapdf_composer_raster *raster,
    quantapdf_composer_image_id *out_image_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_graphics_state(
    quantapdf_composer *composer,
    const quantapdf_composer_graphics_state_options *options,
    quantapdf_composer_graphics_state_id *out_graphics_state_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_clip_path(
    quantapdf_composer *composer,
    const quantapdf_composer_path_command *commands,
    size_t command_count,
    const quantapdf_composer_clip_options *options,
    quantapdf_composer_clip_id *out_clip_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_linear_gradient(
    quantapdf_composer *composer,
    const quantapdf_composer_linear_gradient_options *options,
    quantapdf_composer_paint_id *out_paint_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_radial_gradient(
    quantapdf_composer *composer,
    const quantapdf_composer_radial_gradient_options *options,
    quantapdf_composer_paint_id *out_paint_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_tiling_pattern(
    quantapdf_composer *composer,
    const quantapdf_composer_tiling_pattern_options *options,
    quantapdf_composer_pattern_builder_fn builder,
    void *user_data,
    quantapdf_composer_paint_id *out_paint_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_measure_text(
    const quantapdf_composer *composer,
    const char *text_utf8,
    float max_width,
    const quantapdf_composer_text_options *options,
    quantapdf_composer_text_measurement *out_measurement);

QUANTAPDF_API quantapdf_status quantapdf_composer_measure_embedded_text(
    const quantapdf_composer *composer,
    const char *text_utf8,
    float max_width,
    const quantapdf_composer_embedded_text_options *options,
    quantapdf_composer_text_measurement *out_measurement);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_form(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_composer_form_id form_id,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_form_draw_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_text(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_composer_text_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_text_transformed(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_text_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_embedded_text(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_composer_embedded_text_options *options);

QUANTAPDF_API quantapdf_status
quantapdf_composer_draw_embedded_text_transformed(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_embedded_text_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_glyph_run(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_point origin,
    const quantapdf_composer_glyph *glyphs,
    size_t glyph_count,
    const char *unicode_utf8,
    size_t unicode_size,
    const quantapdf_composer_glyph_run_options *options);

QUANTAPDF_API quantapdf_status
quantapdf_composer_draw_glyph_run_transformed(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_point origin,
    const quantapdf_composer_glyph *glyphs,
    size_t glyph_count,
    const char *unicode_utf8,
    size_t unicode_size,
    const quantapdf_affine_transform *transform,
    const quantapdf_composer_glyph_run_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_image(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_composer_image_id image_id,
    const quantapdf_rect *bounds,
    const quantapdf_composer_image_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_path(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_composer_path_command *commands,
    size_t command_count,
    const quantapdf_composer_path_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_path_dashed(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_composer_path_command *commands,
    size_t command_count,
    const quantapdf_composer_path_options *options,
    const quantapdf_composer_dash_pattern *dash_pattern);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_svg(
    quantapdf_composer *composer,
    size_t page_index,
    const unsigned char *svg_data,
    size_t svg_size,
    const quantapdf_rect *bounds,
    const quantapdf_composer_svg_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_draw_barcode(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_barcode_kind kind,
    const char *payload_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_barcode_options *options);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_uri_link(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_rect *hotspot,
    const char *uri_utf8);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_page_link(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_rect *hotspot,
    size_t target_page_index,
    quantapdf_point target);

QUANTAPDF_API quantapdf_status quantapdf_composer_add_outline(
    quantapdf_composer *composer,
    const char *title_utf8,
    const quantapdf_composer_outline_options *options,
    quantapdf_composer_outline_id *out_outline_id);

QUANTAPDF_API quantapdf_status quantapdf_composer_finish(
    const quantapdf_composer *composer,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_open(
    const char *filename,
    const char *password,
    quantapdf_document **out_document);

QUANTAPDF_API quantapdf_status quantapdf_page_count(
    quantapdf_document *document,
    int *out_page_count);

QUANTAPDF_API quantapdf_status quantapdf_document_form(
    quantapdf_document *document,
    quantapdf_form **out_form);

QUANTAPDF_API quantapdf_status quantapdf_form_field_count(
    const quantapdf_form *form,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_form_field_get_info(
    const quantapdf_form *form,
    size_t field_index,
    quantapdf_form_field_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_form_field_name(
    const quantapdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_field_label(
    const quantapdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_field_value_get_info(
    const quantapdf_form *form,
    size_t field_index,
    size_t value_index,
    quantapdf_form_value_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_form_field_value_utf8(
    const quantapdf_form *form,
    size_t field_index,
    size_t value_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_field_option_get_info(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    quantapdf_form_option_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_form_field_option_export(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_field_option_display(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_form_widget_count(
    const quantapdf_form *form,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_form_widget_get_info(
    const quantapdf_form *form,
    size_t widget_index,
    quantapdf_form_widget_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_document_metadata(
    quantapdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_document_audit(
    quantapdf_document *document,
    quantapdf_audit_result *out_result);

QUANTAPDF_API quantapdf_status quantapdf_document_outline(
    quantapdf_document *document,
    quantapdf_outline **out_outline);

QUANTAPDF_API quantapdf_status quantapdf_outline_count(
    const quantapdf_outline *outline,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_outline_get_info(
    const quantapdf_outline *outline,
    size_t index,
    quantapdf_outline_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_outline_title(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_outline_uri(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_export_pages(
    quantapdf_document *document,
    const int *page_indices,
    size_t page_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_export_page_range(
    quantapdf_document *document,
    int first_page,
    size_t page_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_merge_outputs(
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_crop_pages(
    quantapdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_trim_pages(
    quantapdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_poster_split_pages(
    quantapdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_rewrite_lossless(
    quantapdf_document *document,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_recompress_images(
    quantapdf_document *document,
    const quantapdf_image_recompression_options *options,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_flatten_interactive(
    quantapdf_document *document,
    uint32_t flags,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_sanitize(
    quantapdf_document *document,
    uint32_t flags,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_encrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_decrypt_pdf(
    quantapdf_document *document,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_reencrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_output_data(
    const quantapdf_output *output,
    const unsigned char **out_data,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_output_save_file(
    const quantapdf_output *output,
    const char *filename);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_begin(
    quantapdf_document *document,
    quantapdf_pdf_edit **out_edit);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_form_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_form **out_form);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_form_field_ref_at(
    quantapdf_pdf_edit *edit,
    size_t field_index,
    quantapdf_form_field_ref *out_ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_form_set_values(
    quantapdf_pdf_edit *edit,
    const quantapdf_form_field_ref *ref,
    const quantapdf_form_value_update *update);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_count(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_ref_at(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t index,
    quantapdf_annotation_ref *out_ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_get_info(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_contents(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_create(
    quantapdf_pdf_edit *edit,
    int page_index,
    const quantapdf_annotation_create_options *options,
    quantapdf_annotation_ref *out_ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_update(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    const quantapdf_annotation_update *update);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_delete(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_load_page(
    quantapdf_document *document,
    int page_index,
    quantapdf_page **out_page);

QUANTAPDF_API quantapdf_status quantapdf_page_bounds(
    quantapdf_page *page,
    quantapdf_rect *out_bounds);

QUANTAPDF_API quantapdf_status quantapdf_page_box_bounds(
    quantapdf_page *page,
    quantapdf_page_box box,
    quantapdf_rect *out_bounds);

QUANTAPDF_API quantapdf_status quantapdf_render_page(
    quantapdf_page *page,
    quantapdf_bitmap **out_bitmap);

QUANTAPDF_API quantapdf_status quantapdf_render_page_with_options(
    quantapdf_page *page,
    const quantapdf_render_options *options,
    quantapdf_bitmap **out_bitmap);

QUANTAPDF_API quantapdf_status quantapdf_render_thumbnail(
    quantapdf_page *page,
    int max_width,
    int max_height,
    quantapdf_bitmap **out_bitmap);

QUANTAPDF_API quantapdf_status quantapdf_bitmap_dimensions(
    quantapdf_bitmap *bitmap,
    int *out_width,
    int *out_height,
    int *out_stride,
    int *out_components);

QUANTAPDF_API quantapdf_status quantapdf_bitmap_data(
    quantapdf_bitmap *bitmap,
    const unsigned char **out_data,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_extract_text(
    quantapdf_page *page,
    char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_extract_structured_text(
    quantapdf_page *page,
    quantapdf_text_page **out_text);

QUANTAPDF_API quantapdf_status quantapdf_text_block_count(
    const quantapdf_text_page *text,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_text_get_block_info(
    const quantapdf_text_page *text,
    size_t block_index,
    quantapdf_text_block_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_text_line_count(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_text_get_line_info(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    quantapdf_text_line_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_text_span_count(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_text_get_span_info(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    quantapdf_text_span_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_text_span_text(
    const quantapdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_text_search(
    const quantapdf_text_page *text,
    const char *needle_utf8,
    quantapdf_search_result *results,
    size_t capacity,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_extract_images(
    quantapdf_page *page,
    quantapdf_image_page **out_images);

QUANTAPDF_API quantapdf_status quantapdf_image_count(
    const quantapdf_image_page *images,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_image_get_info(
    const quantapdf_image_page *images,
    size_t index,
    quantapdf_image_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_image_render(
    const quantapdf_image_page *images,
    size_t index,
    quantapdf_bitmap **out_bitmap);

QUANTAPDF_API quantapdf_status quantapdf_extract_links(
    quantapdf_page *page,
    quantapdf_link_page **out_links);

QUANTAPDF_API quantapdf_status quantapdf_link_count(
    const quantapdf_link_page *links,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_link_get_info(
    const quantapdf_link_page *links,
    size_t index,
    quantapdf_link_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_link_uri(
    const quantapdf_link_page *links,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_extract_annotations(
    quantapdf_page *page,
    quantapdf_annotation_page **out_annotations);

QUANTAPDF_API quantapdf_status quantapdf_annotation_count(
    const quantapdf_annotation_page *annotations,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_annotation_get_info(
    const quantapdf_annotation_page *annotations,
    size_t index,
    quantapdf_annotation_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_annotation_contents(
    const quantapdf_annotation_page *annotations,
    size_t index,
    const char **out_utf8,
    size_t *out_size);

QUANTAPDF_API const char *quantapdf_status_string(
    quantapdf_status status);

QUANTAPDF_API void quantapdf_free(
    void *memory);

QUANTAPDF_API void quantapdf_drop_output(
    quantapdf_output *output);

QUANTAPDF_API void quantapdf_drop_composer(
    quantapdf_composer *composer);

QUANTAPDF_API void quantapdf_drop_pdf_edit(
    quantapdf_pdf_edit *edit);

QUANTAPDF_API void quantapdf_drop_form(
    quantapdf_form *form);

QUANTAPDF_API void quantapdf_drop_text_page(
    quantapdf_text_page *text);

QUANTAPDF_API void quantapdf_drop_image_page(
    quantapdf_image_page *images);

QUANTAPDF_API void quantapdf_drop_link_page(
    quantapdf_link_page *links);

QUANTAPDF_API void quantapdf_drop_outline(
    quantapdf_outline *outline);

QUANTAPDF_API void quantapdf_drop_annotation_page(
    quantapdf_annotation_page *annotations);

QUANTAPDF_API void quantapdf_drop_bitmap(
    quantapdf_bitmap *bitmap);

QUANTAPDF_API void quantapdf_drop_page(
    quantapdf_page *page);

QUANTAPDF_API void quantapdf_close(
    quantapdf_document *document);

#ifdef __cplusplus
}
#endif

#endif
