#ifndef QUANTAPDF_COMPOSER_TEST_HELPERS_H
#define QUANTAPDF_COMPOSER_TEST_HELPERS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int quantapdf_test_make_jpeg(unsigned char **out_data, size_t *out_size);
int quantapdf_test_make_png(
    int alpha,
    unsigned char **out_data,
    size_t *out_size);
int quantapdf_test_make_oversized_png(
    unsigned char **out_data,
    size_t *out_size);
int quantapdf_test_make_truncated_jpeg(
    const unsigned char *data,
    size_t size,
    unsigned char **out_data,
    size_t *out_size);
int quantapdf_test_make_huge_progressive_jpeg(
    const unsigned char *data,
    size_t size,
    unsigned char **out_data,
    size_t *out_size);
int quantapdf_test_make_malformed_png(
    int variant,
    unsigned char **out_data,
    size_t *out_size);
int quantapdf_test_pdf_content_contains(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    const char *needle);
int quantapdf_test_pdf_content_order(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    const char *first,
    const char *second);
size_t quantapdf_test_pdf_content_count(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    const char *needle);
int quantapdf_test_pdf_page_size_positive(
    const unsigned char *data,
    size_t size,
    size_t page_index);
int quantapdf_test_pdf_image_xobject_info(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    size_t image_id,
    int *out_components,
    int *out_has_smask);
int quantapdf_test_pdf_extgstate_info(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    size_t graphics_state_id,
    double *out_fill_alpha,
    double *out_stroke_alpha,
    char *out_blend_mode,
    size_t blend_mode_capacity);
int quantapdf_test_pdf_pattern_info(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    size_t paint_id,
    int *out_shading_type,
    int *out_function_type,
    double out_matrix[6]);
int quantapdf_test_pdf_form_xobject_info(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    size_t form_id,
    double out_bbox[4],
    int *out_has_resources);
int quantapdf_test_pdf_form_group_info(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    size_t form_id,
    int *out_has_group,
    int *out_isolated,
    int *out_knockout);
void quantapdf_test_use_comma_locale(int enabled);

#ifdef __cplusplus
}
#endif

#endif
