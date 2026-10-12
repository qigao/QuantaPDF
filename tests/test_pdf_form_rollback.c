#include <quantapdf/quantapdf.h>

#include "pdf_edit_test_api.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rollback_check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define ROLLBACK_CHECK(x) rollback_check_impl((x), #x, __LINE__)

static quantapdf_pdf_edit *rollback_open_edit(const char *path)
{
    quantapdf_document *document = NULL;
    quantapdf_pdf_edit *edit = NULL;

    ROLLBACK_CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    ROLLBACK_CHECK(quantapdf_pdf_edit_begin(document, &edit) == QUANTAPDF_OK);
    quantapdf_close(document);
    return edit;
}

static size_t rollback_field_index(
    const quantapdf_form *form,
    const char *wanted)
{
    size_t count = 0;
    size_t i;

    ROLLBACK_CHECK(quantapdf_form_field_count(form, &count) == QUANTAPDF_OK);
    for (i = 0; i < count; ++i) {
        const char *name = NULL;
        size_t size = 0;
        ROLLBACK_CHECK(quantapdf_form_field_name(form, i, &name, &size) ==
            QUANTAPDF_OK);
        if (name != NULL && size == strlen(wanted) &&
            memcmp(name, wanted, size) == 0)
            return i;
    }
    ROLLBACK_CHECK(0);
    return SIZE_MAX;
}

static quantapdf_form_field_ref rollback_field_ref(
    quantapdf_pdf_edit *edit,
    const char *name)
{
    quantapdf_form *form = NULL;
    quantapdf_form_field_ref ref = {{0, 0}};
    size_t index;

    ROLLBACK_CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    index = rollback_field_index(form, name);
    ROLLBACK_CHECK(quantapdf_pdf_edit_form_field_ref_at(edit, index, &ref) ==
        QUANTAPDF_OK);
    quantapdf_drop_form(form);
    return ref;
}

static void rollback_expect_same_ref(
    const quantapdf_form_field_ref *left,
    const quantapdf_form_field_ref *right)
{
    ROLLBACK_CHECK(left != NULL);
    ROLLBACK_CHECK(right != NULL);
    ROLLBACK_CHECK(memcmp(left, right, sizeof(*left)) == 0);
}

static void rollback_make_text(
    quantapdf_form_value_input *value,
    quantapdf_form_value_update *update,
    const char *text)
{
    memset(value, 0, sizeof(*value));
    memset(update, 0, sizeof(*update));
    value->struct_size = sizeof(*value);
    value->kind = QUANTAPDF_FORM_VALUE_UTF8;
    value->option_index = SIZE_MAX;
    value->utf8 = text;
    value->utf8_size = strlen(text);
    update->struct_size = sizeof(*update);
    update->presence = QUANTAPDF_FORM_VALUE_PRESENT;
    update->values = value;
    update->value_count = 1;
}

static void rollback_make_option(
    quantapdf_form_value_input *value,
    quantapdf_form_value_update *update,
    size_t option_index)
{
    memset(value, 0, sizeof(*value));
    memset(update, 0, sizeof(*update));
    value->struct_size = sizeof(*value);
    value->kind = QUANTAPDF_FORM_VALUE_OPTION;
    value->option_index = option_index;
    update->struct_size = sizeof(*update);
    update->presence = QUANTAPDF_FORM_VALUE_PRESENT;
    update->values = value;
    update->value_count = 1;
}

static void rollback_copy_editor_output(
    quantapdf_pdf_edit *edit,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;

    *out_data = NULL;
    *out_size = 0;
    ROLLBACK_CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    ROLLBACK_CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    ROLLBACK_CHECK(data != NULL && size != 0);
    *out_data = (unsigned char *)malloc(size);
    ROLLBACK_CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
    quantapdf_drop_output(output);
}

static void rollback_expect_failed_atomic(
    quantapdf_pdf_edit *edit,
    const char *field_name,
    const quantapdf_form_field_ref *ref,
    const quantapdf_form_value_update *update,
    quantapdf_test_pdf_edit_fault fault)
{
    unsigned char *before = NULL;
    unsigned char *after = NULL;
    size_t before_size = 0;
    size_t after_size = 0;
    quantapdf_form *form = NULL;
    quantapdf_form_field_ref rediscovered;

    rollback_copy_editor_output(edit, &before, &before_size);
    quantapdf_test_pdf_edit_set_fault(edit, fault);
    ROLLBACK_CHECK(quantapdf_pdf_edit_form_set_values(edit, ref, update) ==
        QUANTAPDF_ERROR_BACKEND);
    rollback_copy_editor_output(edit, &after, &after_size);
    ROLLBACK_CHECK(before_size == after_size);
    ROLLBACK_CHECK(memcmp(before, after, before_size) == 0);
    ROLLBACK_CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    quantapdf_drop_form(form);
    rediscovered = rollback_field_ref(edit, field_name);
    rollback_expect_same_ref(ref, &rediscovered);
    free(before);
    free(after);
}

static void rollback_expect_text(
    quantapdf_pdf_edit *edit,
    const char *name,
    const char *expected)
{
    quantapdf_form *form = NULL;
    size_t index;
    quantapdf_form_field_info info = {0};
    quantapdf_form_value_info value_info = {0};
    const char *text = NULL;
    size_t size = 0;

    ROLLBACK_CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    index = rollback_field_index(form, name);
    info.struct_size = sizeof(info);
    ROLLBACK_CHECK(quantapdf_form_field_get_info(form, index, &info) == QUANTAPDF_OK);
    ROLLBACK_CHECK(info.type == QUANTAPDF_FORM_FIELD_TEXT);
    ROLLBACK_CHECK(info.value_presence == QUANTAPDF_FORM_VALUE_PRESENT);
    ROLLBACK_CHECK(info.value_count == 1);
    value_info.struct_size = sizeof(value_info);
    ROLLBACK_CHECK(quantapdf_form_field_value_get_info(form, index, 0,
        &value_info) == QUANTAPDF_OK);
    ROLLBACK_CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_UTF8);
    ROLLBACK_CHECK(quantapdf_form_field_value_utf8(form, index, 0,
        &text, &size) == QUANTAPDF_OK);
    ROLLBACK_CHECK(text != NULL && size == strlen(expected));
    ROLLBACK_CHECK(memcmp(text, expected, size) == 0);
    quantapdf_drop_form(form);
}

static void rollback_expect_button_option(
    quantapdf_pdf_edit *edit,
    const char *name,
    size_t expected_option)
{
    quantapdf_form *form = NULL;
    size_t index;
    quantapdf_form_field_info info = {0};
    quantapdf_form_value_info value_info = {0};

    ROLLBACK_CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    index = rollback_field_index(form, name);
    info.struct_size = sizeof(info);
    ROLLBACK_CHECK(quantapdf_form_field_get_info(form, index, &info) == QUANTAPDF_OK);
    ROLLBACK_CHECK(info.type == QUANTAPDF_FORM_FIELD_CHECKBOX);
    ROLLBACK_CHECK(info.value_presence == QUANTAPDF_FORM_VALUE_PRESENT);
    ROLLBACK_CHECK(info.value_count == 1);
    value_info.struct_size = sizeof(value_info);
    ROLLBACK_CHECK(quantapdf_form_field_value_get_info(form, index, 0,
        &value_info) == QUANTAPDF_OK);
    ROLLBACK_CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_OPTION);
    ROLLBACK_CHECK(value_info.option_index == expected_option);
    quantapdf_drop_form(form);
}

static void test_direct_field_ref_survives_semantic_rollback(void)
{
    quantapdf_pdf_edit *edit = rollback_open_edit(FORM_MUTATION_DIRECT_FIELD_PDF);
    quantapdf_form_field_ref ref = rollback_field_ref(edit, "direct");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;

    rollback_make_text(&value, &update, "failed");
    rollback_expect_failed_atomic(edit, "direct", &ref, &update,
        QUANTAPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_SEMANTIC_WRITE);
    rollback_expect_text(edit, "direct", "before");

    rollback_make_text(&value, &update, "after");
    ROLLBACK_CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        QUANTAPDF_OK);
    rollback_expect_text(edit, "direct", "after");
    quantapdf_drop_pdf_edit(edit);
}

static void test_button_first_state_rollback_and_reuse(void)
{
    quantapdf_pdf_edit *edit = rollback_open_edit(FORM_MUTATION_BASIC_PDF);
    quantapdf_form_field_ref ref = rollback_field_ref(edit, "check");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;

    rollback_make_option(&value, &update, 0);
    rollback_expect_failed_atomic(edit, "check", &ref, &update,
        QUANTAPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_WIDGET_STATE);
    ROLLBACK_CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        QUANTAPDF_OK);
    rollback_expect_button_option(edit, "check", 0);
    quantapdf_drop_pdf_edit(edit);
}

static void test_first_ap_refresh_rollback_and_reuse(void)
{
    quantapdf_pdf_edit *edit = rollback_open_edit(FORM_MUTATION_EVENTS_PDF);
    quantapdf_form_field_ref ref = rollback_field_ref(edit, "target");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;

    rollback_make_text(&value, &update, "AFTER");
    rollback_expect_failed_atomic(edit, "target", &ref, &update,
        QUANTAPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_AP_REFRESH);
    rollback_expect_text(edit, "target", "BEFORE");
    rollback_expect_text(edit, "unrelated", "UNCHANGED");
    rollback_expect_text(edit, "calc", "CALC-SENTINEL");

    ROLLBACK_CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        QUANTAPDF_OK);
    rollback_expect_text(edit, "target", "AFTER");
    rollback_expect_text(edit, "unrelated", "UNCHANGED");
    rollback_expect_text(edit, "calc", "CALC-SENTINEL");
    quantapdf_drop_pdf_edit(edit);
}

static void test_failed_rollback_disables_form_session(void)
{
    quantapdf_pdf_edit *edit = rollback_open_edit(FORM_MUTATION_DIRECT_FIELD_PDF);
    quantapdf_form_field_ref ref = rollback_field_ref(edit, "direct");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;
    quantapdf_form *before = NULL;
    quantapdf_form *failed = (quantapdf_form *)(uintptr_t)1;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
    size_t before_count = 0;
    size_t after_count = 0;

    ROLLBACK_CHECK(quantapdf_pdf_edit_form_snapshot(edit, &before) == QUANTAPDF_OK);
    ROLLBACK_CHECK(quantapdf_form_field_count(before, &before_count) == QUANTAPDF_OK);
    ROLLBACK_CHECK(before_count != 0);
    rollback_make_text(&value, &update, "unpublished");
    quantapdf_test_pdf_edit_set_fault(edit, QUANTAPDF_TEST_PDF_EDIT_FAULT_ROLLBACK);
    ROLLBACK_CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        QUANTAPDF_ERROR_BACKEND);
    ROLLBACK_CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        QUANTAPDF_ERROR_STATE);
    ROLLBACK_CHECK(quantapdf_pdf_edit_form_snapshot(edit, &failed) == QUANTAPDF_ERROR_STATE);
    ROLLBACK_CHECK(failed == NULL);
    ROLLBACK_CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_ERROR_STATE);
    ROLLBACK_CHECK(output == NULL);
    quantapdf_drop_pdf_edit(edit);
    ROLLBACK_CHECK(quantapdf_form_field_count(before, &after_count) == QUANTAPDF_OK);
    ROLLBACK_CHECK(before_count == after_count);
    ROLLBACK_CHECK(rollback_field_index(before, "direct") < after_count);
    quantapdf_drop_form(before);
}

int quantapdf_pdf_form_rollback_main(void)
{
    test_direct_field_ref_survives_semantic_rollback();
    test_button_first_state_rollback_and_reuse();
    test_first_ap_refresh_rollback_and_reuse();
    test_failed_rollback_disables_form_session();
    return EXIT_SUCCESS;
}
