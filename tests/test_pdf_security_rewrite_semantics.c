#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void semantic_check(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: semantic check failed: %s\n",
                __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}
#define SCHECK(expression) semantic_check((expression), #expression, __LINE__)

int quantapdf_security_canonicalize_fixture(
    const char *source_path,
    const char *output_path);

static void compare_bytes(
    quantapdf_status left_status,
    const char *left,
    size_t left_size,
    quantapdf_status right_status,
    const char *right,
    size_t right_size)
{
    SCHECK(left_status == right_status);
    SCHECK((left == NULL) == (right == NULL));
    SCHECK(left_size == right_size);
    if (left_size != 0)
        SCHECK(memcmp(left, right, left_size) == 0);
}

static size_t compare_outline(
    quantapdf_document *left_document,
    quantapdf_document *right_document)
{
    quantapdf_outline *left = NULL;
    quantapdf_outline *right = NULL;
    size_t left_count = 0;
    size_t right_count = 0;
    size_t index;

    SCHECK(quantapdf_document_outline(left_document, &left) == QUANTAPDF_OK);
    SCHECK(quantapdf_document_outline(right_document, &right) == QUANTAPDF_OK);
    SCHECK(quantapdf_outline_count(left, &left_count) == QUANTAPDF_OK);
    SCHECK(quantapdf_outline_count(right, &right_count) == QUANTAPDF_OK);
    SCHECK(left_count == right_count);
    for (index = 0; index < left_count; ++index) {
        quantapdf_outline_info left_info = {sizeof(left_info)};
        quantapdf_outline_info right_info = {sizeof(right_info)};
        const char *left_text = NULL;
        const char *right_text = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        quantapdf_status left_status;
        quantapdf_status right_status;
        SCHECK(quantapdf_outline_get_info(left, index, &left_info) == QUANTAPDF_OK);
        SCHECK(quantapdf_outline_get_info(right, index, &right_info) == QUANTAPDF_OK);
        SCHECK(memcmp(&left_info, &right_info, sizeof(left_info)) == 0);
        left_status = quantapdf_outline_title(
            left, index, &left_text, &left_size);
        right_status = quantapdf_outline_title(
            right, index, &right_text, &right_size);
        compare_bytes(left_status, left_text, left_size,
                      right_status, right_text, right_size);
        left_text = NULL;
        right_text = NULL;
        left_size = 0;
        right_size = 0;
        left_status = quantapdf_outline_uri(
            left, index, &left_text, &left_size);
        right_status = quantapdf_outline_uri(
            right, index, &right_text, &right_size);
        compare_bytes(left_status, left_text, left_size,
                      right_status, right_text, right_size);
    }
    quantapdf_drop_outline(right);
    quantapdf_drop_outline(left);
    return left_count;
}

static size_t compare_links(
    quantapdf_page *left_page,
    quantapdf_page *right_page)
{
    quantapdf_link_page *left = NULL;
    quantapdf_link_page *right = NULL;
    size_t left_count = 0;
    size_t right_count = 0;
    size_t index;

    SCHECK(quantapdf_extract_links(left_page, &left) == QUANTAPDF_OK);
    SCHECK(quantapdf_extract_links(right_page, &right) == QUANTAPDF_OK);
    SCHECK(quantapdf_link_count(left, &left_count) == QUANTAPDF_OK);
    SCHECK(quantapdf_link_count(right, &right_count) == QUANTAPDF_OK);
    SCHECK(left_count == right_count);
    for (index = 0; index < left_count; ++index) {
        quantapdf_link_info left_info = {sizeof(left_info)};
        quantapdf_link_info right_info = {sizeof(right_info)};
        const char *left_uri = NULL;
        const char *right_uri = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        quantapdf_status left_status;
        quantapdf_status right_status;
        SCHECK(quantapdf_link_get_info(left, index, &left_info) == QUANTAPDF_OK);
        SCHECK(quantapdf_link_get_info(right, index, &right_info) == QUANTAPDF_OK);
        SCHECK(memcmp(&left_info, &right_info, sizeof(left_info)) == 0);
        left_status = quantapdf_link_uri(left, index, &left_uri, &left_size);
        right_status = quantapdf_link_uri(right, index, &right_uri, &right_size);
        compare_bytes(left_status, left_uri, left_size,
                      right_status, right_uri, right_size);
    }
    quantapdf_drop_link_page(right);
    quantapdf_drop_link_page(left);
    return left_count;
}

static size_t compare_annotations(
    quantapdf_page *left_page,
    quantapdf_page *right_page)
{
    quantapdf_annotation_page *left = NULL;
    quantapdf_annotation_page *right = NULL;
    size_t left_count = 0;
    size_t right_count = 0;
    size_t index;

    SCHECK(quantapdf_extract_annotations(left_page, &left) == QUANTAPDF_OK);
    SCHECK(quantapdf_extract_annotations(right_page, &right) == QUANTAPDF_OK);
    SCHECK(quantapdf_annotation_count(left, &left_count) == QUANTAPDF_OK);
    SCHECK(quantapdf_annotation_count(right, &right_count) == QUANTAPDF_OK);
    SCHECK(left_count == right_count);
    for (index = 0; index < left_count; ++index) {
        quantapdf_annotation_info left_info = {sizeof(left_info)};
        quantapdf_annotation_info right_info = {sizeof(right_info)};
        const char *left_contents = NULL;
        const char *right_contents = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        quantapdf_status left_status;
        quantapdf_status right_status;
        SCHECK(quantapdf_annotation_get_info(left, index, &left_info) == QUANTAPDF_OK);
        SCHECK(quantapdf_annotation_get_info(right, index, &right_info) == QUANTAPDF_OK);
        SCHECK(memcmp(&left_info, &right_info, sizeof(left_info)) == 0);
        left_status = quantapdf_annotation_contents(
            left, index, &left_contents, &left_size);
        right_status = quantapdf_annotation_contents(
            right, index, &right_contents, &right_size);
        compare_bytes(left_status, left_contents, left_size,
                      right_status, right_contents, right_size);
    }
    quantapdf_drop_annotation_page(right);
    quantapdf_drop_annotation_page(left);
    return left_count;
}

static size_t compare_images(
    quantapdf_page *left_page,
    quantapdf_page *right_page)
{
    quantapdf_image_page *left = NULL;
    quantapdf_image_page *right = NULL;
    size_t left_count = 0;
    size_t right_count = 0;
    size_t index;

    SCHECK(quantapdf_extract_images(left_page, &left) == QUANTAPDF_OK);
    SCHECK(quantapdf_extract_images(right_page, &right) == QUANTAPDF_OK);
    SCHECK(quantapdf_image_count(left, &left_count) == QUANTAPDF_OK);
    SCHECK(quantapdf_image_count(right, &right_count) == QUANTAPDF_OK);
    SCHECK(left_count == right_count);
    for (index = 0; index < left_count; ++index) {
        quantapdf_image_info left_info = {sizeof(left_info)};
        quantapdf_image_info right_info = {sizeof(right_info)};
        quantapdf_bitmap *left_bitmap = NULL;
        quantapdf_bitmap *right_bitmap = NULL;
        const unsigned char *left_pixels = NULL;
        const unsigned char *right_pixels = NULL;
        size_t left_pixel_size = 0;
        size_t right_pixel_size = 0;
        SCHECK(quantapdf_image_get_info(left, index, &left_info) == QUANTAPDF_OK);
        SCHECK(quantapdf_image_get_info(right, index, &right_info) == QUANTAPDF_OK);
        SCHECK(memcmp(&left_info, &right_info, sizeof(left_info)) == 0);
        SCHECK(quantapdf_image_render(left, index, &left_bitmap) == QUANTAPDF_OK);
        SCHECK(quantapdf_image_render(right, index, &right_bitmap) == QUANTAPDF_OK);
        SCHECK(quantapdf_bitmap_data(
            left_bitmap, &left_pixels, &left_pixel_size) == QUANTAPDF_OK);
        SCHECK(quantapdf_bitmap_data(
            right_bitmap, &right_pixels, &right_pixel_size) == QUANTAPDF_OK);
        SCHECK(left_pixel_size != 0 && left_pixel_size == right_pixel_size);
        SCHECK(memcmp(left_pixels, right_pixels, left_pixel_size) == 0);
        quantapdf_drop_bitmap(right_bitmap);
        quantapdf_drop_bitmap(left_bitmap);
    }
    quantapdf_drop_image_page(right);
    quantapdf_drop_image_page(left);
    return left_count;
}

static size_t compare_text_and_search(
    quantapdf_page *left_page,
    quantapdf_page *right_page)
{
    char *left_plain = NULL;
    char *right_plain = NULL;
    size_t left_plain_size = 0;
    size_t right_plain_size = 0;
    quantapdf_text_page *left = NULL;
    quantapdf_text_page *right = NULL;
    size_t left_count = 0;
    size_t right_count = 0;
    quantapdf_search_result *left_results = NULL;
    quantapdf_search_result *right_results = NULL;
    size_t index;

    SCHECK(quantapdf_extract_text(
        left_page, &left_plain, &left_plain_size) == QUANTAPDF_OK);
    SCHECK(quantapdf_extract_text(
        right_page, &right_plain, &right_plain_size) == QUANTAPDF_OK);
    compare_bytes(QUANTAPDF_OK, left_plain, left_plain_size,
                  QUANTAPDF_OK, right_plain, right_plain_size);
    quantapdf_free(right_plain);
    quantapdf_free(left_plain);

    SCHECK(quantapdf_extract_structured_text(left_page, &left) == QUANTAPDF_OK);
    SCHECK(quantapdf_extract_structured_text(right_page, &right) == QUANTAPDF_OK);
    SCHECK(quantapdf_text_search(left, "PDF", NULL, 0, &left_count) == QUANTAPDF_OK);
    SCHECK(quantapdf_text_search(right, "PDF", NULL, 0, &right_count) == QUANTAPDF_OK);
    SCHECK(left_count == right_count);
    if (left_count != 0) {
        left_results = (quantapdf_search_result *)calloc(
            left_count, sizeof(*left_results));
        right_results = (quantapdf_search_result *)calloc(
            right_count, sizeof(*right_results));
        SCHECK(left_results != NULL && right_results != NULL);
        for (index = 0; index < left_count; ++index) {

        }
        SCHECK(quantapdf_text_search(
            left, "PDF", left_results, left_count, &left_count) == QUANTAPDF_OK);
        SCHECK(quantapdf_text_search(
            right, "PDF", right_results, right_count, &right_count) == QUANTAPDF_OK);
        SCHECK(memcmp(
            left_results,
            right_results,
            left_count * sizeof(*left_results)) == 0);
    }
    free(right_results);
    free(left_results);
    quantapdf_drop_text_page(right);
    quantapdf_drop_text_page(left);
    return left_plain_size;
}

static size_t compare_form(
    quantapdf_document *left_document,
    quantapdf_document *right_document)
{
    quantapdf_form *left = NULL;
    quantapdf_form *right = NULL;
    size_t left_count = 0;
    size_t right_count = 0;
    size_t field_count;
    size_t field;

    SCHECK(quantapdf_document_form(left_document, &left) == QUANTAPDF_OK);
    SCHECK(quantapdf_document_form(right_document, &right) == QUANTAPDF_OK);
    SCHECK(quantapdf_form_field_count(left, &left_count) == QUANTAPDF_OK);
    SCHECK(quantapdf_form_field_count(right, &right_count) == QUANTAPDF_OK);
    SCHECK(left_count == right_count);
    field_count = left_count;
    for (field = 0; field < left_count; ++field) {
        quantapdf_form_field_info left_info = {sizeof(left_info)};
        quantapdf_form_field_info right_info = {sizeof(right_info)};
        const char *left_text = NULL;
        const char *right_text = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        size_t item;
        quantapdf_status left_status;
        quantapdf_status right_status;
        SCHECK(quantapdf_form_field_get_info(left, field, &left_info) == QUANTAPDF_OK);
        SCHECK(quantapdf_form_field_get_info(right, field, &right_info) == QUANTAPDF_OK);
        SCHECK(memcmp(&left_info, &right_info, sizeof(left_info)) == 0);
        left_status = quantapdf_form_field_name(
            left, field, &left_text, &left_size);
        right_status = quantapdf_form_field_name(
            right, field, &right_text, &right_size);
        compare_bytes(left_status, left_text, left_size,
                      right_status, right_text, right_size);
        left_text = NULL; right_text = NULL; left_size = 0; right_size = 0;
        left_status = quantapdf_form_field_label(
            left, field, &left_text, &left_size);
        right_status = quantapdf_form_field_label(
            right, field, &right_text, &right_size);
        compare_bytes(left_status, left_text, left_size,
                      right_status, right_text, right_size);
        for (item = 0; item < left_info.value_count; ++item) {
            quantapdf_form_value_info left_value = {sizeof(left_value)};
            quantapdf_form_value_info right_value = {sizeof(right_value)};
            SCHECK(quantapdf_form_field_value_get_info(
                left, field, item, &left_value) == QUANTAPDF_OK);
            SCHECK(quantapdf_form_field_value_get_info(
                right, field, item, &right_value) == QUANTAPDF_OK);
            SCHECK(memcmp(&left_value, &right_value, sizeof(left_value)) == 0);
            if (left_value.kind == QUANTAPDF_FORM_VALUE_UTF8) {
                left_text = NULL; right_text = NULL; left_size = 0; right_size = 0;
                left_status = quantapdf_form_field_value_utf8(
                    left, field, item, &left_text, &left_size);
                right_status = quantapdf_form_field_value_utf8(
                    right, field, item, &right_text, &right_size);
                compare_bytes(left_status, left_text, left_size,
                              right_status, right_text, right_size);
            }
        }
        for (item = 0; item < left_info.option_count; ++item) {
            quantapdf_form_option_info left_option = {sizeof(left_option)};
            quantapdf_form_option_info right_option = {sizeof(right_option)};
            SCHECK(quantapdf_form_field_option_get_info(
                left, field, item, &left_option) == QUANTAPDF_OK);
            SCHECK(quantapdf_form_field_option_get_info(
                right, field, item, &right_option) == QUANTAPDF_OK);
            SCHECK(memcmp(&left_option, &right_option, sizeof(left_option)) == 0);
            left_text = NULL; right_text = NULL; left_size = 0; right_size = 0;
            left_status = quantapdf_form_field_option_export(
                left, field, item, &left_text, &left_size);
            right_status = quantapdf_form_field_option_export(
                right, field, item, &right_text, &right_size);
            compare_bytes(left_status, left_text, left_size,
                          right_status, right_text, right_size);
            left_text = NULL; right_text = NULL; left_size = 0; right_size = 0;
            left_status = quantapdf_form_field_option_display(
                left, field, item, &left_text, &left_size);
            right_status = quantapdf_form_field_option_display(
                right, field, item, &right_text, &right_size);
            compare_bytes(left_status, left_text, left_size,
                          right_status, right_text, right_size);
        }
    }
    SCHECK(quantapdf_form_widget_count(left, &left_count) == QUANTAPDF_OK);
    SCHECK(quantapdf_form_widget_count(right, &right_count) == QUANTAPDF_OK);
    SCHECK(left_count == right_count);
    for (field = 0; field < left_count; ++field) {
        quantapdf_form_widget_info left_info = {sizeof(left_info)};
        quantapdf_form_widget_info right_info = {sizeof(right_info)};
        SCHECK(quantapdf_form_widget_get_info(left, field, &left_info) == QUANTAPDF_OK);
        SCHECK(quantapdf_form_widget_get_info(right, field, &right_info) == QUANTAPDF_OK);
        SCHECK(memcmp(&left_info, &right_info, sizeof(left_info)) == 0);
    }
    quantapdf_drop_form(right);
    quantapdf_drop_form(left);
    return field_count;
}

static void compare_metadata(
    quantapdf_document *left,
    quantapdf_document *right)
{
    int field;
    for (field = QUANTAPDF_METADATA_TITLE;
         field <= QUANTAPDF_METADATA_MODIFICATION_DATE;
         ++field) {
        char *left_text = NULL;
        char *right_text = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        quantapdf_status left_status = quantapdf_document_metadata(
            left, (quantapdf_metadata_field)field, &left_text, &left_size);
        quantapdf_status right_status = quantapdf_document_metadata(
            right, (quantapdf_metadata_field)field, &right_text, &right_size);
        compare_bytes(left_status, left_text, left_size,
                      right_status, right_text, right_size);
        quantapdf_free(right_text);
        quantapdf_free(left_text);
    }
}

typedef struct semantic_observations {
    size_t image_occurrences;
    size_t link_occurrences;
    size_t annotation_occurrences;
    size_t form_fields;
    size_t outline_nodes;
    size_t text_bytes;
} semantic_observations;

static semantic_observations compare_documents(
    const char *source_path,
    const char *fixture_path)
{
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "semantic-user",
        "semantic-owner",
        QUANTAPDF_PERMISSION_ALL,
        1};
    quantapdf_document *left = NULL;
    quantapdf_document *authenticated = NULL;
    quantapdf_document *right = NULL;
    quantapdf_output *encrypted = NULL;
    quantapdf_output *decrypted = NULL;
    int left_pages = 0;
    int right_pages = 0;
    int page_index;
    quantapdf_status status;
    semantic_observations observed = {0};

    SCHECK(quantapdf_open(test_context(), source_path, NULL, &left) == QUANTAPDF_OK);
    {
        quantapdf_audit_result audit = { 0};
        quantapdf_status audit_status = quantapdf_document_audit(left, &audit);
        if (audit_status != QUANTAPDF_OK)
            fprintf(stderr, "semantic audit failed for %s: %s\n",
                    fixture_path, quantapdf_status_string(audit_status));
        SCHECK(audit_status == QUANTAPDF_OK);
    }
    status = quantapdf_encrypt_pdf(left, &options, &encrypted);
    if (status != QUANTAPDF_OK)
        fprintf(stderr, "semantic transform failed for %s: %s\n",
                fixture_path, quantapdf_status_string(status));
    SCHECK(status == QUANTAPDF_OK);
    SCHECK(quantapdf_output_save_file(
        encrypted, SECURITY_SEMANTIC_ENCRYPTED) == QUANTAPDF_OK);
    quantapdf_drop_output(encrypted);
    SCHECK(quantapdf_open(test_context(),
        SECURITY_SEMANTIC_ENCRYPTED,
        "semantic-user",
        &authenticated) == QUANTAPDF_OK);
    SCHECK(quantapdf_decrypt_pdf(authenticated, &decrypted) == QUANTAPDF_OK);
    quantapdf_close(authenticated);
    SCHECK(quantapdf_output_save_file(
        decrypted, SECURITY_SEMANTIC_DECRYPTED) == QUANTAPDF_OK);
    quantapdf_drop_output(decrypted);
    SCHECK(quantapdf_open(test_context(),
        SECURITY_SEMANTIC_DECRYPTED, NULL, &right) == QUANTAPDF_OK);
    SCHECK(quantapdf_page_count(left, &left_pages) == QUANTAPDF_OK);
    SCHECK(quantapdf_page_count(right, &right_pages) == QUANTAPDF_OK);
    SCHECK(left_pages == right_pages);
    compare_metadata(left, right);
    observed.outline_nodes += compare_outline(left, right);
    observed.form_fields += compare_form(left, right);
    for (page_index = 0; page_index < left_pages; ++page_index) {
        quantapdf_page *left_page = NULL;
        quantapdf_page *right_page = NULL;
        quantapdf_rect left_bounds = {0};
        quantapdf_rect right_bounds = {0};
        int box;
        SCHECK(quantapdf_load_page(left, page_index, &left_page) == QUANTAPDF_OK);
        SCHECK(quantapdf_load_page(right, page_index, &right_page) == QUANTAPDF_OK);
        SCHECK(quantapdf_page_bounds(left_page, &left_bounds) == QUANTAPDF_OK);
        SCHECK(quantapdf_page_bounds(right_page, &right_bounds) == QUANTAPDF_OK);
        SCHECK(memcmp(&left_bounds, &right_bounds, sizeof(left_bounds)) == 0);
        for (box = QUANTAPDF_PAGE_BOX_MEDIA;
             box <= QUANTAPDF_PAGE_BOX_CROP;
             ++box) {
            SCHECK(quantapdf_page_box_bounds(
                left_page, (quantapdf_page_box)box, &left_bounds) == QUANTAPDF_OK);
            SCHECK(quantapdf_page_box_bounds(
                right_page, (quantapdf_page_box)box, &right_bounds) == QUANTAPDF_OK);
            SCHECK(memcmp(&left_bounds, &right_bounds, sizeof(left_bounds)) == 0);
        }
        observed.text_bytes += compare_text_and_search(left_page, right_page);
        observed.image_occurrences += compare_images(left_page, right_page);
        observed.link_occurrences += compare_links(left_page, right_page);
        observed.annotation_occurrences +=
            compare_annotations(left_page, right_page);
        quantapdf_drop_page(right_page);
        quantapdf_drop_page(left_page);
    }
    quantapdf_close(right);
    quantapdf_close(left);
    return observed;
}

void quantapdf_security_check_public_semantics(void)
{
    static const char *const fixtures[] = {
        SECURITY_SEMANTIC_TEXT_PDF,
        SECURITY_SEMANTIC_LINKS_PDF,
        SECURITY_SEMANTIC_IMAGES_PDF,
        SECURITY_SEMANTIC_FORM_PDF,
        SECURITY_SEMANTIC_OUTLINE_PDF,
        SECURITY_SEMANTIC_METADATA_PDF,
        SECURITY_SEMANTIC_ANNOTATIONS_PDF};
    size_t index;
    semantic_observations totals = {0};
    for (index = 0; index < sizeof(fixtures) / sizeof(fixtures[0]); ++index) {
        semantic_observations observed;
        SCHECK(quantapdf_security_canonicalize_fixture(
            fixtures[index], SECURITY_SEMANTIC_CANONICAL));
        observed = compare_documents(
            SECURITY_SEMANTIC_CANONICAL, fixtures[index]);
        totals.image_occurrences += observed.image_occurrences;
        totals.link_occurrences += observed.link_occurrences;
        totals.annotation_occurrences += observed.annotation_occurrences;
        totals.form_fields += observed.form_fields;
        totals.outline_nodes += observed.outline_nodes;
        totals.text_bytes += observed.text_bytes;
    }
    SCHECK(totals.image_occurrences >= 1);
    SCHECK(totals.link_occurrences >= 1);
    SCHECK(totals.annotation_occurrences >= 1);
    SCHECK(totals.form_fields >= 1);
    SCHECK(totals.outline_nodes >= 1);
    SCHECK(totals.text_bytes >= 1);
}
