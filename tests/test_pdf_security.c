#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pdf_security_create_fixture(
    const char *source_path,
    const char *output_path,
    const char *scenario);

int pdf_security_inspect_output(
    const unsigned char *data,
    size_t size,
    uint32_t *out_markers,
    int *out_has_object_stream);

int pdf_security_inspect_empty_containers(
    const unsigned char *data,
    size_t size);

int pdf_security_inspect_shared_container_owners(
    const unsigned char *data,
    size_t size,
    int inspect_next);

enum {
    PDF_SECURITY_SAFE_GOTO_MARKER = 1u << 7
};

static int failures;
static const char *current_case = "startup";

static void begin_case(const char *name)
{
    current_case = name;
    printf("CASE %s\n", name);
}

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(
            stderr, "%s:%d: [%s] check failed: %s\n",
            __FILE__, line, current_case, expression);
        ++failures;
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static int fixture_path(
    const char *scenario,
    char *path,
    size_t size)
{
    int written = snprintf(
        path, size, "%s/pdf-security-%s.pdf",
        SECURITY_FIXTURE_DIR, scenario);
    CHECK(written > 0 && (size_t)written < size);
    return written > 0 && (size_t)written < size;
}

static int create_fixture(
    const char *scenario,
    char *path,
    size_t size)
{
    int created;
    if (!fixture_path(scenario, path, size))
        return 0;
    created = pdf_security_create_fixture(SECURITY_PDF, path, scenario);
    CHECK(created);
    return created;
}

static void expect_document_audit(
    const char *path,
    const char *password,
    quantapdf_status expected_status,
    uint32_t expected_findings)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_status open_status = quantapdf_open(test_context(), path, password, &document);
    quantapdf_status status;

    CHECK(open_status == QUANTAPDF_OK);
    if (open_status != QUANTAPDF_OK)
        return;

    audit.findings = UINT32_MAX;
    status = quantapdf_document_audit(document, &audit);
    if (status != expected_status) {
        fprintf(
            stderr,
            "[%s] audit status: expected %d, got %d\n",
            current_case, (int)expected_status, (int)status);
    }
    if (audit.findings != expected_findings) {
        fprintf(
            stderr,
            "[%s] audit findings: expected 0x%08x, got 0x%08x\n",
            current_case, expected_findings, audit.findings);
    }
    CHECK(status == expected_status);
    CHECK(audit.findings == expected_findings);
    quantapdf_close(document);
}

static void expect_fixture(
    const char *scenario,
    quantapdf_status expected_status,
    uint32_t expected_findings)
{
    char path[512];
    begin_case(scenario);
    if (!create_fixture(scenario, path, sizeof(path)))
        return;
    expect_document_audit(
        path, NULL, expected_status, expected_findings);
}

static uint32_t inspect_sanitized_output(
    quantapdf_output *output,
    int *out_has_object_stream)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    uint32_t markers = UINT32_MAX;

    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL);
    CHECK(size != 0);
    if (data == NULL || size == 0)
        return UINT32_MAX;
    CHECK(pdf_security_inspect_output(
        data, size, &markers, out_has_object_stream));
    return markers;
}

static int inspect_sanitized_empty_containers(quantapdf_output *output)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    int mask;

    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL);
    CHECK(size != 0);
    if (data == NULL || size == 0)
        return -1;
    mask = pdf_security_inspect_empty_containers(data, size);
    CHECK(mask >= 0);
    return mask;
}

static int inspect_sanitized_shared_container_owners(
    quantapdf_output *output,
    int inspect_next)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    int mask;

    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL);
    CHECK(size != 0);
    if (data == NULL || size == 0)
        return -1;
    mask = pdf_security_inspect_shared_container_owners(
        data, size, inspect_next);
    CHECK(mask >= 0);
    return mask;
}

static int save_sanitized_output(
    quantapdf_output *output,
    const char *scenario,
    char *path,
    size_t path_size)
{
    if (!fixture_path(scenario, path, path_size))
        return 0;
    CHECK(quantapdf_output_save_file(output, path) == QUANTAPDF_OK);
    return 1;
}

static void expect_document_observations_equal(
    const char *left_path,
    const char *right_path)
{
    quantapdf_document *left = NULL;
    quantapdf_document *right = NULL;
    quantapdf_page *left_page = NULL;
    quantapdf_page *right_page = NULL;
    quantapdf_bitmap *left_bitmap = NULL;
    quantapdf_bitmap *right_bitmap = NULL;
    quantapdf_rect left_bounds = {0};
    quantapdf_rect right_bounds = {0};
    char *left_text = NULL;
    char *right_text = NULL;
    const unsigned char *left_pixels = NULL;
    const unsigned char *right_pixels = NULL;
    size_t left_text_size = 0;
    size_t right_text_size = 0;
    size_t left_pixel_size = 0;
    size_t right_pixel_size = 0;
    int left_pages = 0;
    int right_pages = 0;
    int left_width = 0;
    int left_height = 0;
    int left_stride = 0;
    int left_components = 0;
    int right_width = 0;
    int right_height = 0;
    int right_stride = 0;
    int right_components = 0;

    CHECK(quantapdf_open(test_context(), left_path, NULL, &left) == QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(), right_path, NULL, &right) == QUANTAPDF_OK);
    if (left == NULL || right == NULL)
        goto cleanup;
    CHECK(quantapdf_page_count(left, &left_pages) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(right, &right_pages) == QUANTAPDF_OK);
    CHECK(left_pages == right_pages);
    if (left_pages <= 0 || right_pages <= 0)
        goto cleanup;
    CHECK(quantapdf_load_page(left, 0, &left_page) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(right, 0, &right_page) == QUANTAPDF_OK);
    if (left_page == NULL || right_page == NULL)
        goto cleanup;
    CHECK(quantapdf_page_bounds(left_page, &left_bounds) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(right_page, &right_bounds) == QUANTAPDF_OK);
    CHECK(memcmp(&left_bounds, &right_bounds, sizeof(left_bounds)) == 0);
    CHECK(quantapdf_extract_text(
              left_page, &left_text, &left_text_size) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(
              right_page, &right_text, &right_text_size) == QUANTAPDF_OK);
    CHECK(left_text_size == right_text_size);
    if (left_text_size == right_text_size && left_text_size != 0)
        CHECK(memcmp(left_text, right_text, left_text_size) == 0);
    CHECK(quantapdf_render_page(left_page, &left_bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page(right_page, &right_bitmap) == QUANTAPDF_OK);
    if (left_bitmap == NULL || right_bitmap == NULL)
        goto cleanup;
    CHECK(quantapdf_bitmap_dimensions(
              left_bitmap, &left_width, &left_height, &left_stride,
              &left_components) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              right_bitmap, &right_width, &right_height, &right_stride,
              &right_components) == QUANTAPDF_OK);
    CHECK(left_width == right_width);
    CHECK(left_height == right_height);
    CHECK(left_stride == right_stride);
    CHECK(left_components == right_components);
    CHECK(quantapdf_bitmap_data(
              left_bitmap, &left_pixels, &left_pixel_size) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_data(
              right_bitmap, &right_pixels, &right_pixel_size) == QUANTAPDF_OK);
    CHECK(left_pixel_size == right_pixel_size);
    if (left_pixel_size == right_pixel_size && left_pixel_size != 0)
        CHECK(memcmp(left_pixels, right_pixels, left_pixel_size) == 0);

cleanup:
    quantapdf_drop_bitmap(right_bitmap);
    quantapdf_drop_bitmap(left_bitmap);
    quantapdf_free(right_text);
    quantapdf_free(left_text);
    quantapdf_drop_page(right_page);
    quantapdf_drop_page(left_page);
    quantapdf_close(right);
    quantapdf_close(left);
}

static void test_public_contract(void)
{
    quantapdf_audit_result audit = {0};
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;

    begin_case("public_contract");
    CHECK(QUANTAPDF_AUDIT_JAVASCRIPT_ACTION == (1u << 0));
    CHECK(QUANTAPDF_AUDIT_LAUNCH_ACTION == (1u << 1));
    CHECK(QUANTAPDF_AUDIT_EXTERNAL_ACTION == (1u << 2));
    CHECK(QUANTAPDF_AUDIT_OTHER_ACTION == (1u << 3));
    CHECK(QUANTAPDF_AUDIT_EMBEDDED_FILE == (1u << 4));
    CHECK(QUANTAPDF_AUDIT_XFA == (1u << 5));
    CHECK(QUANTAPDF_AUDIT_RICH_MEDIA == (1u << 6));
    CHECK(QUANTAPDF_AUDIT_SIGNATURE == (1u << 7));
    CHECK(QUANTAPDF_AUDIT_ENCRYPTION == (1u << 8));
    CHECK(QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS == (1u << 0));
    CHECK(QUANTAPDF_SANITIZE_LAUNCH_ACTIONS == (1u << 1));
    CHECK(QUANTAPDF_SANITIZE_EXTERNAL_ACTIONS == (1u << 2));
    CHECK(QUANTAPDF_SANITIZE_OTHER_ACTIONS == (1u << 3));
    CHECK(QUANTAPDF_SANITIZE_EMBEDDED_FILES == (1u << 4));
    CHECK(QUANTAPDF_SANITIZE_XFA == (1u << 5));
    CHECK(QUANTAPDF_SANITIZE_RICH_MEDIA == (1u << 6));
    CHECK(QUANTAPDF_SANITIZE_ALL == ((1u << 7) - 1u));

    audit.findings = UINT32_MAX;
    CHECK(quantapdf_document_audit(NULL, &audit) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(audit.findings == 0);
    CHECK(quantapdf_document_audit(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);

}

static void test_valid_audit_matrix(void)
{
    static const struct audit_case {
        const char *scenario;
        uint32_t findings;
    } cases[] = {
        {"internal_goto", 0},
        {"open_destination_array", 0},
        {"open_destination_name", 0},
        {"open_destination_string", 0},
        {"action_javascript", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION},
        {"names_javascript", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION},
        {"name_tree_safe", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION},
        {"name_tree_head_launch", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                                  QUANTAPDF_AUDIT_LAUNCH_ACTION},
        {"name_tree_next_javascript", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                                      QUANTAPDF_AUDIT_LAUNCH_ACTION},
        {"name_tree_next_launch", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                                  QUANTAPDF_AUDIT_LAUNCH_ACTION},
        {"name_tree_next_external", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                                    QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"name_tree_next_other", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                                 QUANTAPDF_AUDIT_OTHER_ACTION},
        {"name_tree_valid_multilevel", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION},
        {"name_tree_limited_head_launch",
                                  QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                                  QUANTAPDF_AUDIT_LAUNCH_ACTION},
        {"action_launch", QUANTAPDF_AUDIT_LAUNCH_ACTION},
        {"external_uri", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"external_gotor", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"external_gotoe", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"external_submitform", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"external_importdata", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"other_unknown", QUANTAPDF_AUDIT_OTHER_ACTION},
        {"goto_next_other", QUANTAPDF_AUDIT_OTHER_ACTION},
        {"names_embedded", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"af_embedded", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"ef_embedded", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"embedded_stream", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"file_attachment", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"xfa", QUANTAPDF_AUDIT_XFA},
        {"rich_richmedia", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"rich_3d", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"rich_movie", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"rich_sound", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"rich_screen", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"inherited_signature", QUANTAPDF_AUDIT_SIGNATURE},
        {"document_timestamp_field", QUANTAPDF_AUDIT_SIGNATURE},
        {"perms_docmdp", QUANTAPDF_AUDIT_SIGNATURE},
        {"perms_ur", QUANTAPDF_AUDIT_SIGNATURE},
        {"perms_ur3", QUANTAPDF_AUDIT_SIGNATURE},
        {"unreachable_javascript", 0},
        {"nonroot_names", 0},
        {"nonroot_acroform", 0},
        {"action_cycle", 0}
    };
    size_t index;

    begin_case("clean");
    expect_document_audit(SECURITY_PDF, NULL, QUANTAPDF_OK, 0);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        expect_fixture(cases[index].scenario, QUANTAPDF_OK,
                       cases[index].findings);

    begin_case("signed_fixture");
    expect_document_audit(
        SECURITY_SIGNED_PDF, NULL, QUANTAPDF_OK,
        QUANTAPDF_AUDIT_SIGNATURE);
    begin_case("encrypted_fixture");
    expect_document_audit(
        SECURITY_ENCRYPTED_PDF, "user-pass", QUANTAPDF_OK,
        QUANTAPDF_AUDIT_ENCRYPTION);
}

static void test_malformed_and_budget_matrix(void)
{
    static const char *malformed[] = {
        "malformed_open_action",
        "malformed_a",
        "malformed_aa_container",
        "malformed_aa_entry",
        "malformed_next",
        "malformed_names",
        "malformed_js_tree_node",
        "malformed_js_tree_kids",
        "malformed_js_tree_kid",
        "malformed_js_tree_names",
        "malformed_js_tree_odd_names",
        "malformed_js_tree_key",
        "malformed_js_tree_value",
        "malformed_js_tree_action",
        "malformed_js_tree_both",
        "malformed_js_tree_limits",
        "name_tree_cycle",
        "name_tree_duplicate_child",
        "name_tree_unordered_keys",
        "name_tree_duplicate_keys",
        "name_tree_reversed_limits",
        "name_tree_inconsistent_limits",
        "name_tree_overlapping_kids",
        "name_tree_out_of_order_kids",
        "malformed_annots",
        "malformed_annot_entry",
        "malformed_acroform",
        "malformed_fields",
        "malformed_field",
        "malformed_signature_value",
        "malformed_signature_type",
        "parent_mismatch",
        "malformed_perms",
        "malformed_perms_signature",
        "malformed_perms_type"
    };
    static const char *budget[] = {
        "budget_shared_next",
        "budget_shared_aa",
        "budget_shared_annots",
        "budget_js_name_tree"
    };
    size_t index;

    for (index = 0; index < sizeof(malformed) / sizeof(malformed[0]); ++index)
        expect_fixture(malformed[index], QUANTAPDF_ERROR_FORMAT, 0);
    for (index = 0; index < sizeof(budget) / sizeof(budget[0]); ++index)
        expect_fixture(budget[index], QUANTAPDF_ERROR_UNSUPPORTED, 0);
}

static void test_extended_result_and_repeatability(void)
{
    struct extended_audit_result {
        quantapdf_audit_result v1;
        unsigned char suffix[16];
    } extended;
    unsigned char expected_suffix[sizeof(extended.suffix)];
    quantapdf_audit_result first = {0};
    quantapdf_audit_result second = {0};
    quantapdf_document *document = NULL;
    quantapdf_status status;
    char path[512];

    begin_case("extended_result");
    memset(&extended, 0xa5, sizeof(extended));
    memcpy(expected_suffix, extended.suffix, sizeof(expected_suffix));

    extended.v1.findings = UINT32_MAX;
    status = quantapdf_open(test_context(), SECURITY_PDF, NULL, &document);
    CHECK(status == QUANTAPDF_OK);
    if (status == QUANTAPDF_OK) {
        CHECK(quantapdf_document_audit(document, &extended.v1) ==
              QUANTAPDF_OK);

        CHECK(extended.v1.findings == 0);
        CHECK(memcmp(
                  extended.suffix, expected_suffix,
                  sizeof(extended.suffix)) == 0);
        quantapdf_close(document);
    }

    begin_case("repeatability");
    document = NULL;
    if (!create_fixture("action_javascript", path, sizeof(path)))
        return;
    status = quantapdf_open(test_context(), path, NULL, &document);
    CHECK(status == QUANTAPDF_OK);
    if (status != QUANTAPDF_OK)
        return;

    CHECK(quantapdf_document_audit(document, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_document_audit(document, &second) == QUANTAPDF_OK);
    CHECK(first.findings == QUANTAPDF_AUDIT_JAVASCRIPT_ACTION);
    CHECK(second.findings == first.findings);
    quantapdf_close(document);
}

static void test_sanitize_policy_isolation_and_all(void)
{
    static const struct sanitize_case {
        const char *name;
        uint32_t flag;
    } cases[] = {
        {"sanitize_javascript", QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS},
        {"sanitize_launch", QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"sanitize_external", QUANTAPDF_SANITIZE_EXTERNAL_ACTIONS},
        {"sanitize_other", QUANTAPDF_SANITIZE_OTHER_ACTIONS},
        {"sanitize_embedded", QUANTAPDF_SANITIZE_EMBEDDED_FILES},
        {"sanitize_xfa", QUANTAPDF_SANITIZE_XFA},
        {"sanitize_rich_media", QUANTAPDF_SANITIZE_RICH_MEDIA}
    };
    quantapdf_audit_result source_audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    quantapdf_status status;
    char source_path[512];
    char output_path[512];
    size_t index;

    begin_case("sanitize_combined_fixture");
    if (!create_fixture(
            "sanitize_combined", source_path, sizeof(source_path)))
        return;
    status = quantapdf_open(test_context(), source_path, NULL, &document);
    CHECK(status == QUANTAPDF_OK);
    if (status != QUANTAPDF_OK)
        return;

    CHECK(quantapdf_document_audit(document, &source_audit) == QUANTAPDF_OK);
    CHECK(source_audit.findings == QUANTAPDF_SANITIZE_ALL);

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        uint32_t expected_findings = QUANTAPDF_SANITIZE_ALL & ~cases[index].flag;
        uint32_t expected_markers = expected_findings |
            PDF_SECURITY_SAFE_GOTO_MARKER;
        uint32_t markers;
        int has_object_stream = -1;

        begin_case(cases[index].name);
        output = (quantapdf_output *)(uintptr_t)1;
        CHECK(quantapdf_sanitize(document, cases[index].flag, &output) ==
              QUANTAPDF_OK);
        CHECK(output != NULL);
        if (output == NULL || output == (quantapdf_output *)(uintptr_t)1)
            continue;
        markers = inspect_sanitized_output(output, &has_object_stream);
        CHECK(markers == expected_markers);
        CHECK(has_object_stream == 0);
        if (save_sanitized_output(
                output, "sanitize-policy-output", output_path,
                sizeof(output_path))) {
            expect_document_audit(
                output_path, NULL, QUANTAPDF_OK, expected_findings);
        }
        quantapdf_drop_output(output);
        output = NULL;
    }

    begin_case("sanitize_all");
    CHECK(quantapdf_sanitize(document, QUANTAPDF_SANITIZE_ALL, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    if (output != NULL) {
        uint32_t markers;
        int has_object_stream = -1;
        markers = inspect_sanitized_output(output, &has_object_stream);
        printf(
            "RAW sanitize_all markers=0x%08x object_stream=%d\n",
            markers, has_object_stream);
        CHECK(markers == PDF_SECURITY_SAFE_GOTO_MARKER);
        CHECK(has_object_stream == 0);
        if (save_sanitized_output(
                output, "sanitize-all-output", output_path,
                sizeof(output_path))) {
            expect_document_audit(output_path, NULL, QUANTAPDF_OK, 0);
            expect_document_observations_equal(source_path, output_path);
        }
        quantapdf_drop_output(output);
    }

    source_audit.findings = 0;
    CHECK(quantapdf_document_audit(document, &source_audit) == QUANTAPDF_OK);
    CHECK(source_audit.findings == QUANTAPDF_SANITIZE_ALL);
    quantapdf_close(document);
}

static void test_sanitize_ownership_and_canonical_output(void)
{
    quantapdf_document *source = NULL;
    quantapdf_document *reopened = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_output *resanitized = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const unsigned char *resanitized_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    size_t resanitized_size = 0;
    char source_path[512];
    char output_path[512];
    uint32_t markers;
    int has_object_stream = -1;

    begin_case("sanitize_ownership_and_canonical_output");
    if (!create_fixture(
            "sanitize_combined", source_path, sizeof(source_path)))
        return;
    CHECK(quantapdf_open(test_context(), source_path, NULL, &source) == QUANTAPDF_OK);
    if (source == NULL)
        return;
    CHECK(quantapdf_sanitize(source, QUANTAPDF_SANITIZE_ALL, &first) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_sanitize(source, QUANTAPDF_SANITIZE_ALL, &second) ==
          QUANTAPDF_OK);
    quantapdf_close(source);
    source = NULL;
    CHECK(first != NULL);
    CHECK(second != NULL);
    if (first == NULL || second == NULL)
        goto cleanup;
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == second_size);
    if (first_size == second_size)
        CHECK(memcmp(first_data, second_data, first_size) == 0);
    markers = inspect_sanitized_output(first, &has_object_stream);
    CHECK(markers == PDF_SECURITY_SAFE_GOTO_MARKER);
    CHECK(has_object_stream == 0);

    if (!save_sanitized_output(
            first, "sanitize-canonical-output", output_path,
            sizeof(output_path)))
        goto cleanup;
    CHECK(quantapdf_open(test_context(), output_path, NULL, &reopened) == QUANTAPDF_OK);
    if (reopened == NULL)
        goto cleanup;
    CHECK(quantapdf_sanitize(
              reopened, QUANTAPDF_SANITIZE_ALL, &resanitized) ==
          QUANTAPDF_OK);
    CHECK(resanitized != NULL);
    if (resanitized == NULL)
        goto cleanup;
    CHECK(quantapdf_output_data(
              resanitized, &resanitized_data, &resanitized_size) ==
          QUANTAPDF_OK);
    CHECK(resanitized_size == first_size);
    if (resanitized_size == first_size)
        CHECK(memcmp(resanitized_data, first_data, first_size) == 0);

cleanup:
    quantapdf_drop_output(resanitized);
    quantapdf_close(reopened);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_close(source);
}

static void test_sanitize_empty_container_policy_isolation(void)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    char path[512];

    begin_case("sanitize_preserves_empty_unselected_containers");
    if (!create_fixture(
            "sanitize_empty_unselected", path, sizeof(path)))
        return;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;

    CHECK(quantapdf_document_audit(document, &audit) == QUANTAPDF_OK);
    CHECK(audit.findings == 0);
    CHECK(quantapdf_sanitize(document, QUANTAPDF_SANITIZE_XFA, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    if (output != NULL)
        CHECK(inspect_sanitized_empty_containers(output) == 7);
    quantapdf_drop_output(output);
    quantapdf_close(document);

    begin_case("sanitize_removes_containers_emptied_by_selected_content");
    document = NULL;
    output = NULL;
    if (!create_fixture(
            "sanitize_selected_to_empty", path, sizeof(path)))
        return;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;
    audit.findings = 0;
    CHECK(quantapdf_document_audit(document, &audit) == QUANTAPDF_OK);
    CHECK(audit.findings == QUANTAPDF_AUDIT_JAVASCRIPT_ACTION);
    CHECK(quantapdf_sanitize(
              document, QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    if (output != NULL)
        CHECK(inspect_sanitized_empty_containers(output) == 0);
    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void test_sanitize_mutation_budget_exhaustion(void)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
    char path[512];

    begin_case("sanitize_mutation_budget_exhaustion");
    if (!create_fixture(
            "mutation_budget_shared_next", path, sizeof(path)))
        return;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;

    CHECK(quantapdf_document_audit(document, &audit) == QUANTAPDF_OK);
    CHECK(audit.findings == 0);
    CHECK(quantapdf_sanitize(document, QUANTAPDF_SANITIZE_XFA, &output) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);
    quantapdf_close(document);
}

static void expect_sanitize_shared_container_cleanup(
    const char *scenario,
    int inspect_next)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    char path[512];

    if (!create_fixture(scenario, path, sizeof(path)))
        return;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;

    CHECK(quantapdf_document_audit(document, &audit) == QUANTAPDF_OK);
    CHECK(audit.findings == QUANTAPDF_AUDIT_JAVASCRIPT_ACTION);
    CHECK(quantapdf_sanitize(
              document, QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    if (output != NULL) {
        CHECK(inspect_sanitized_shared_container_owners(
                  output, inspect_next) == 0);
    }
    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void test_sanitize_shared_selected_containers(void)
{
    begin_case("sanitize_cleans_all_shared_selected_aa_owners");
    expect_sanitize_shared_container_cleanup(
        "sanitize_shared_selected_aa", 0);

    begin_case("sanitize_cleans_all_shared_selected_next_owners");
    expect_sanitize_shared_container_cleanup(
        "sanitize_shared_selected_next", 1);
}

static void expect_sanitize_failure(
    const char *path,
    const char *password,
    uint32_t flags,
    quantapdf_status expected_status)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;

    CHECK(quantapdf_open(test_context(), path, password, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;
    CHECK(quantapdf_sanitize(document, flags, &output) == expected_status);
    CHECK(output == NULL);
    quantapdf_close(document);
}

static void test_sanitize_javascript_name_tree_actions(void)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    char path[512];
    char output_path[512];
    uint32_t markers;
    int has_object_stream = -1;

    begin_case("sanitize_name_tree_launch_continuation");
    if (!create_fixture("name_tree_next_launch", path, sizeof(path)))
        return;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;

    CHECK(quantapdf_document_audit(document, &audit) == QUANTAPDF_OK);
    CHECK(audit.findings == (QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                             QUANTAPDF_AUDIT_LAUNCH_ACTION));
    CHECK(quantapdf_sanitize(
              document, QUANTAPDF_SANITIZE_LAUNCH_ACTIONS, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    if (output != NULL) {
        markers = inspect_sanitized_output(output, &has_object_stream);
        CHECK(markers == PDF_SECURITY_SAFE_GOTO_MARKER);
        if (save_sanitized_output(
                output, "name-tree-launch-output", output_path,
                sizeof(output_path))) {
            expect_document_audit(
                output_path, NULL, QUANTAPDF_OK,
                QUANTAPDF_AUDIT_JAVASCRIPT_ACTION);
        }
    }
    quantapdf_drop_output(output);
    quantapdf_close(document);

    begin_case("sanitize_name_tree_selected_head");
    document = NULL;
    output = NULL;
    if (!create_fixture("name_tree_head_launch", path, sizeof(path)))
        return;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;
    CHECK(quantapdf_sanitize(
              document, QUANTAPDF_SANITIZE_LAUNCH_ACTIONS, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    if (output != NULL && save_sanitized_output(
            output, "name-tree-head-output", output_path,
            sizeof(output_path))) {
        expect_document_audit(
            output_path, NULL, QUANTAPDF_OK,
            QUANTAPDF_AUDIT_JAVASCRIPT_ACTION);
    }
    quantapdf_drop_output(output);
    quantapdf_close(document);

    begin_case("sanitize_name_tree_updates_limits");
    document = NULL;
    output = NULL;
    if (!create_fixture("name_tree_limited_head_launch", path, sizeof(path)))
        return;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;
    CHECK(quantapdf_sanitize(
              document, QUANTAPDF_SANITIZE_LAUNCH_ACTIONS, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    if (output != NULL && save_sanitized_output(
            output, "name-tree-limits-output", output_path,
            sizeof(output_path))) {
        expect_document_audit(
            output_path, NULL, QUANTAPDF_OK,
            QUANTAPDF_AUDIT_JAVASCRIPT_ACTION);
    }
    quantapdf_drop_output(output);
    quantapdf_close(document);

    begin_case("sanitize_name_tree_whole_javascript_policy");
    document = NULL;
    output = NULL;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document != NULL) {
        CHECK(quantapdf_sanitize(
                  document, QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS,
                  &output) == QUANTAPDF_OK);
        CHECK(output != NULL);
        if (output != NULL && save_sanitized_output(
                output, "name-tree-javascript-output", output_path,
                sizeof(output_path))) {
            expect_document_audit(output_path, NULL, QUANTAPDF_OK, 0);
        }
        quantapdf_drop_output(output);
        quantapdf_close(document);
    }
}

static void test_sanitize_ambiguous_aliases(void)
{
    static const struct alias_case {
        const char *scenario;
        uint32_t findings;
        uint32_t flag;
    } cases[] = {
        {"alias_annots_next", QUANTAPDF_AUDIT_LAUNCH_ACTION |
                              QUANTAPDF_AUDIT_RICH_MEDIA,
                              QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_annots_next", QUANTAPDF_AUDIT_LAUNCH_ACTION |
                              QUANTAPDF_AUDIT_RICH_MEDIA,
                              QUANTAPDF_SANITIZE_RICH_MEDIA},
        {"alias_names_custom", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION,
                               QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS},
        {"alias_js_names_array_custom",
                               QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                               QUANTAPDF_AUDIT_LAUNCH_ACTION,
                               QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_acroform_custom", QUANTAPDF_AUDIT_XFA,
                                  QUANTAPDF_SANITIZE_XFA},
        {"alias_aa_custom", QUANTAPDF_AUDIT_LAUNCH_ACTION,
                            QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_next_custom", QUANTAPDF_AUDIT_LAUNCH_ACTION,
                              QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_openaction_next_custom", QUANTAPDF_AUDIT_LAUNCH_ACTION,
                                         QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_direct_next_custom", QUANTAPDF_AUDIT_LAUNCH_ACTION,
                                     QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_direct_aa_custom", QUANTAPDF_AUDIT_LAUNCH_ACTION,
                                   QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_direct_annots_custom", QUANTAPDF_AUDIT_RICH_MEDIA,
                                       QUANTAPDF_SANITIZE_RICH_MEDIA},
        {"alias_direct_page_annots_custom", QUANTAPDF_AUDIT_RICH_MEDIA,
                                            QUANTAPDF_SANITIZE_RICH_MEDIA},
        {"annotation_custom_alias", QUANTAPDF_AUDIT_LAUNCH_ACTION,
                                    QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_direct_js_names_custom",
                               QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                               QUANTAPDF_AUDIT_LAUNCH_ACTION,
                               QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_direct_js_limits_custom",
                               QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
                               QUANTAPDF_AUDIT_LAUNCH_ACTION,
                               QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_direct_catalog_names_custom",
                               QUANTAPDF_AUDIT_JAVASCRIPT_ACTION,
                               QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS},
        {"alias_direct_acroform_custom", QUANTAPDF_AUDIT_XFA,
                                         QUANTAPDF_SANITIZE_XFA},
        {"alias_direct_af_ef_custom", QUANTAPDF_AUDIT_EMBEDDED_FILE,
                                       QUANTAPDF_SANITIZE_EMBEDDED_FILES},
        {"alias_direct_action_owner_custom",
                               QUANTAPDF_AUDIT_LAUNCH_ACTION,
                               QUANTAPDF_SANITIZE_LAUNCH_ACTIONS},
        {"alias_annots_custom", QUANTAPDF_AUDIT_RICH_MEDIA,
                                QUANTAPDF_SANITIZE_RICH_MEDIA},
        {"alias_af_custom", QUANTAPDF_AUDIT_EMBEDDED_FILE,
                            QUANTAPDF_SANITIZE_EMBEDDED_FILES},
        {"alias_ef_custom", QUANTAPDF_AUDIT_EMBEDDED_FILE,
                            QUANTAPDF_SANITIZE_EMBEDDED_FILES}
    };
    char path[512];
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        begin_case(cases[index].scenario);
        if (!create_fixture(cases[index].scenario, path, sizeof(path)))
            continue;
        expect_document_audit(
            path, NULL, QUANTAPDF_OK, cases[index].findings);
        expect_sanitize_failure(
            path, NULL, cases[index].flag,
            QUANTAPDF_ERROR_UNSUPPORTED);
    }

    begin_case("alias_unselected_policy_is_allowed");
    if (create_fixture("alias_annots_next", path, sizeof(path))) {
        quantapdf_document *document = NULL;
        quantapdf_output *output = NULL;
        CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
        if (document != NULL) {
            CHECK(quantapdf_sanitize(
                      document, QUANTAPDF_SANITIZE_XFA, &output) ==
                  QUANTAPDF_OK);
            CHECK(output != NULL);
            quantapdf_drop_output(output);
            quantapdf_close(document);
        }
    }
}

static void test_sanitize_annotation_backlinks(void)
{
    static const struct backlink_case {
        uint32_t flag;
        uint32_t remaining;
        const char *output_name;
    } page_cases[] = {
        {QUANTAPDF_SANITIZE_RICH_MEDIA,
         QUANTAPDF_AUDIT_EMBEDDED_FILE,
         "annotation-page-rich-output"},
        {QUANTAPDF_SANITIZE_EMBEDDED_FILES,
         QUANTAPDF_AUDIT_RICH_MEDIA,
         "annotation-page-attachment-output"}
    };
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    char path[512];
    char output_path[512];
    size_t index;

    for (index = 0;
         index < sizeof(page_cases) / sizeof(page_cases[0]);
         ++index) {
        begin_case(index == 0
            ? "sanitize_rich_annotation_with_page_backlink"
            : "sanitize_attachment_with_page_backlink");
        if (!create_fixture("annotation_page_backlinks", path, sizeof(path)))
            continue;
        expect_document_audit(
            path, NULL, QUANTAPDF_OK,
            QUANTAPDF_AUDIT_EMBEDDED_FILE |
                QUANTAPDF_AUDIT_RICH_MEDIA);
        CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
        if (document == NULL)
            continue;
        CHECK(quantapdf_sanitize(
                  document, page_cases[index].flag, &output) ==
              QUANTAPDF_OK);
        CHECK(output != NULL);
        if (output != NULL && save_sanitized_output(
                output, page_cases[index].output_name, output_path,
                sizeof(output_path))) {
            expect_document_audit(
                output_path, NULL, QUANTAPDF_OK,
                page_cases[index].remaining);
        }
        quantapdf_drop_output(output);
        quantapdf_close(document);
        output = NULL;
        document = NULL;
    }

    begin_case("sanitize_actions_with_annotation_graph_backlinks");
    if (!create_fixture("annotation_reply_popup_links", path, sizeof(path)))
        return;
    expect_document_audit(
        path, NULL, QUANTAPDF_OK, QUANTAPDF_AUDIT_LAUNCH_ACTION);
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    if (document == NULL)
        return;
    CHECK(quantapdf_sanitize(
              document, QUANTAPDF_SANITIZE_LAUNCH_ACTIONS, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    if (output != NULL && save_sanitized_output(
            output, "annotation-links-output", output_path,
            sizeof(output_path))) {
        expect_document_audit(output_path, NULL, QUANTAPDF_OK, 0);
    }
    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void test_sanitize_strict_failures_and_arguments(void)
{
    static const char *malformed[] = {
        "malformed_open_action",
        "malformed_a",
        "malformed_aa_container",
        "malformed_aa_entry",
        "malformed_next",
        "malformed_names",
        "malformed_js_tree_node",
        "malformed_js_tree_kids",
        "malformed_js_tree_kid",
        "malformed_js_tree_names",
        "malformed_js_tree_odd_names",
        "malformed_js_tree_key",
        "malformed_js_tree_value",
        "malformed_js_tree_action",
        "malformed_js_tree_both",
        "malformed_js_tree_limits",
        "name_tree_cycle",
        "name_tree_duplicate_child",
        "name_tree_unordered_keys",
        "name_tree_duplicate_keys",
        "name_tree_reversed_limits",
        "name_tree_inconsistent_limits",
        "name_tree_overlapping_kids",
        "name_tree_out_of_order_kids",
        "malformed_annots",
        "malformed_annot_entry",
        "malformed_acroform",
        "malformed_fields",
        "malformed_field",
        "malformed_signature_value",
        "malformed_signature_type",
        "parent_mismatch",
        "malformed_perms",
        "malformed_perms_signature",
        "malformed_perms_type"
    };
    static const char *budget[] = {
        "budget_shared_next",
        "budget_shared_aa",
        "budget_shared_annots",
        "budget_js_name_tree"
    };
    quantapdf_document *document = NULL;
    quantapdf_output *output;
    char path[512];
    size_t index;

    begin_case("sanitize_arguments");
    CHECK(quantapdf_open(test_context(), SECURITY_PDF, NULL, &document) == QUANTAPDF_OK);
    if (document != NULL) {
        output = (quantapdf_output *)(uintptr_t)1;
        CHECK(quantapdf_sanitize(document, 0, &output) ==
              QUANTAPDF_ERROR_ARGUMENT);
        CHECK(output == NULL);
        output = (quantapdf_output *)(uintptr_t)1;
        CHECK(quantapdf_sanitize(document, UINT32_C(1) << 31, &output) ==
              QUANTAPDF_ERROR_ARGUMENT);
        CHECK(output == NULL);
        quantapdf_close(document);
    }

    begin_case("sanitize_signed");
    expect_sanitize_failure(
        SECURITY_SIGNED_PDF, NULL, QUANTAPDF_SANITIZE_ALL,
        QUANTAPDF_ERROR_UNSUPPORTED);
    begin_case("sanitize_encrypted");
    expect_sanitize_failure(
        SECURITY_ENCRYPTED_PDF, "user-pass", QUANTAPDF_SANITIZE_ALL,
        QUANTAPDF_ERROR_UNSUPPORTED);

    begin_case("sanitize_direct_embedded_stream");
    if (create_fixture("embedded_stream", path, sizeof(path))) {
        expect_sanitize_failure(
            path, NULL, QUANTAPDF_SANITIZE_EMBEDDED_FILES,
            QUANTAPDF_ERROR_UNSUPPORTED);
    }

    for (index = 0; index < sizeof(malformed) / sizeof(malformed[0]); ++index) {
        begin_case(malformed[index]);
        if (create_fixture(malformed[index], path, sizeof(path))) {
            expect_sanitize_failure(
                path, NULL, QUANTAPDF_SANITIZE_ALL,
                QUANTAPDF_ERROR_FORMAT);
        }
    }
    for (index = 0; index < sizeof(budget) / sizeof(budget[0]); ++index) {
        begin_case(budget[index]);
        if (create_fixture(budget[index], path, sizeof(path))) {
            expect_sanitize_failure(
                path, NULL, QUANTAPDF_SANITIZE_ALL,
                QUANTAPDF_ERROR_UNSUPPORTED);
        }
    }

    begin_case("sanitize_action_cycle");
    if (create_fixture("action_cycle", path, sizeof(path))) {
        CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
        if (document != NULL) {
            output = NULL;
            CHECK(quantapdf_sanitize(
                      document, QUANTAPDF_SANITIZE_ALL, &output) ==
                  QUANTAPDF_OK);
            CHECK(output != NULL);
            quantapdf_drop_output(output);
            quantapdf_close(document);
        }
    }
}

int main(void)
{
    test_public_contract();
    test_valid_audit_matrix();
    test_malformed_and_budget_matrix();
    test_extended_result_and_repeatability();
    test_sanitize_policy_isolation_and_all();
    test_sanitize_ownership_and_canonical_output();
    test_sanitize_empty_container_policy_isolation();
    test_sanitize_mutation_budget_exhaustion();
    test_sanitize_shared_selected_containers();
    test_sanitize_javascript_name_tree_actions();
    test_sanitize_ambiguous_aliases();
    test_sanitize_annotation_backlinks();
    test_sanitize_strict_failures_and_arguments();
    if (failures != 0)
        fprintf(stderr, "pdf_security: %d checks failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
