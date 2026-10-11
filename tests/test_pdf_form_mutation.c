#include <quantapdf/quantapdf.h>

#include "pdf_edit_test_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORM_MUTATION_ROUNDTRIP_PDF "acroform-mutation-roundtrip-output.pdf"

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static void compile_surface(void)
{
    quantapdf_pdf_edit *edit = NULL;
    quantapdf_form *form = NULL;
    quantapdf_form_field_ref ref = {{0, 0}};
    quantapdf_form_value_input value = {0};
    quantapdf_form_value_update update = {0};

    value.struct_size = sizeof(value);
    value.kind = QUANTAPDF_FORM_VALUE_UTF8;
    value.option_index = SIZE_MAX;
    value.utf8 = "x";
    value.utf8_size = 1;
    update.struct_size = sizeof(update);
    update.presence = QUANTAPDF_FORM_VALUE_PRESENT;
    update.values = &value;
    update.value_count = 1;

    if (0) {
        (void)quantapdf_pdf_edit_form_snapshot(edit, &form);
        (void)quantapdf_pdf_edit_form_field_ref_at(edit, 0, &ref);
        (void)quantapdf_pdf_edit_form_set_values(edit, &ref, &update);
    }
}

static void expect_form_count(const quantapdf_form *form, size_t expected)
{
    size_t count = SIZE_MAX;
    CHECK(quantapdf_form_field_count(form, &count) == QUANTAPDF_OK);
    CHECK(count == expected);
}

static void copy_output(const quantapdf_output *output,
    unsigned char **out_data, size_t *out_size)
{
    const unsigned char *data = NULL;
    size_t size = 0;

    *out_data = NULL;
    *out_size = 0;
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL && size != 0);
    *out_data = (unsigned char *)malloc(size);
    CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
}

static void copy_editor_output(quantapdf_pdf_edit *edit,
    unsigned char **out_data, size_t *out_size)
{
    quantapdf_output *output = NULL;
    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    copy_output(output, out_data, out_size);
    quantapdf_drop_output(output);
}

static size_t count_bytes(const unsigned char *data, size_t size, const char *needle)
{
    size_t needle_size = strlen(needle);
    size_t i;
    size_t count = 0;

    if (needle_size == 0 || needle_size > size)
        return 0;
    for (i = 0; i + needle_size <= size; ++i)
        if (memcmp(data + i, needle, needle_size) == 0)
            ++count;
    return count;
}

static quantapdf_output *snapshot_source(const char *path, int observe_document_form)
{
    quantapdf_document *document = NULL;
    quantapdf_pdf_edit *edit = NULL;
    quantapdf_output *output = NULL;
    quantapdf_form *form = NULL;

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    if (observe_document_form) {
        CHECK(quantapdf_document_form(document, &form) == QUANTAPDF_OK);
        CHECK(form != NULL);
        quantapdf_drop_form(form);
    }
    CHECK(quantapdf_pdf_edit_begin(document, &edit) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    quantapdf_drop_pdf_edit(edit);
    quantapdf_close(document);
    return output;
}

static quantapdf_form_field_ref field_ref_by_name(quantapdf_pdf_edit *edit,
    const char *wanted)
{
    quantapdf_form *form = NULL;
    quantapdf_form_field_ref ref = {{0, 0}};
    size_t count = 0;
    size_t index;

    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    CHECK(quantapdf_form_field_count(form, &count) == QUANTAPDF_OK);
    for (index = 0; index < count; ++index) {
        const char *name = NULL;
        size_t size = 0;
        CHECK(quantapdf_form_field_name(form, index, &name, &size) == QUANTAPDF_OK);
        if (name != NULL && size == strlen(wanted) && memcmp(name, wanted, size) == 0) {
            CHECK(quantapdf_pdf_edit_form_field_ref_at(edit, index, &ref) == QUANTAPDF_OK);
            quantapdf_drop_form(form);
            return ref;
        }
    }
    quantapdf_drop_form(form);
    CHECK(0);
    return ref;
}

static size_t form_field_index_by_name(const quantapdf_form *form, const char *wanted)
{
    size_t count = 0;
    size_t index;
    CHECK(quantapdf_form_field_count(form, &count) == QUANTAPDF_OK);
    for (index = 0; index < count; ++index) {
        const char *name = NULL;
        size_t size = 0;
        CHECK(quantapdf_form_field_name(form, index, &name, &size) == QUANTAPDF_OK);
        if (name != NULL && size == strlen(wanted) && memcmp(name, wanted, size) == 0)
            return index;
    }
    CHECK(0);
    return SIZE_MAX;
}

static void expect_text_field(const quantapdf_form *form, const char *name,
    quantapdf_form_value_presence presence, const char *expected)
{
    size_t index = form_field_index_by_name(form, name);
    quantapdf_form_field_info info = {0};

    info.struct_size = sizeof(info);
    CHECK(quantapdf_form_field_get_info(form, index, &info) == QUANTAPDF_OK);
    CHECK(info.type == QUANTAPDF_FORM_FIELD_TEXT);
    CHECK(info.value_presence == presence);
    if (presence == QUANTAPDF_FORM_VALUE_MISSING) {
        CHECK(info.value_count == 0);
    } else {
        quantapdf_form_value_info value_info = {0};
        const char *text = NULL;
        size_t size = 0;
        CHECK(presence == QUANTAPDF_FORM_VALUE_PRESENT && info.value_count == 1);
        value_info.struct_size = sizeof(value_info);
        CHECK(quantapdf_form_field_value_get_info(form, index, 0, &value_info) == QUANTAPDF_OK);
        CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_UTF8);
        CHECK(quantapdf_form_field_value_utf8(form, index, 0, &text, &size) == QUANTAPDF_OK);
        CHECK(text != NULL && size == strlen(expected));
        CHECK(memcmp(text, expected, size) == 0);
    }
}

static void expect_button_field(const quantapdf_form *form, const char *name,
    quantapdf_form_field_type type, quantapdf_form_value_presence presence,
    size_t value_count, size_t option_index)
{
    size_t index = form_field_index_by_name(form, name);
    quantapdf_form_field_info info = {0};

    info.struct_size = sizeof(info);
    CHECK(quantapdf_form_field_get_info(form, index, &info) == QUANTAPDF_OK);
    CHECK(info.type == type);
    CHECK(info.value_presence == presence);
    CHECK(info.value_count == value_count);
    if (value_count != 0) {
        quantapdf_form_value_info value_info = {0};
        CHECK(value_count == 1);
        value_info.struct_size = sizeof(value_info);
        CHECK(quantapdf_form_field_value_get_info(form, index, 0, &value_info) == QUANTAPDF_OK);
        CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_OPTION);
        CHECK(value_info.option_index == option_index);
    }
}

static void expect_choice_options(const quantapdf_form *form, const char *name,
    quantapdf_form_field_type type, quantapdf_form_value_presence presence,
    const size_t *expected_options, size_t expected_count)
{
    size_t index = form_field_index_by_name(form, name);
    quantapdf_form_field_info info = {0};
    size_t i;

    info.struct_size = sizeof(info);
    CHECK(quantapdf_form_field_get_info(form, index, &info) == QUANTAPDF_OK);
    CHECK(info.type == type);
    CHECK(info.value_presence == presence);
    CHECK(info.value_count == expected_count);
    for (i = 0; i < expected_count; ++i) {
        quantapdf_form_value_info value_info = {0};
        value_info.struct_size = sizeof(value_info);
        CHECK(quantapdf_form_field_value_get_info(form, index, i, &value_info) == QUANTAPDF_OK);
        CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_OPTION);
        CHECK(value_info.option_index == expected_options[i]);
    }
}

static void expect_choice_utf8(const quantapdf_form *form, const char *name,
    quantapdf_form_field_type type, const char *expected, size_t expected_size)
{
    size_t index = form_field_index_by_name(form, name);
    quantapdf_form_field_info info = {0};
    quantapdf_form_value_info value_info = {0};
    const char *text = NULL;
    size_t size = SIZE_MAX;

    info.struct_size = sizeof(info);
    CHECK(quantapdf_form_field_get_info(form, index, &info) == QUANTAPDF_OK);
    CHECK(info.type == type);
    CHECK(info.value_presence == QUANTAPDF_FORM_VALUE_PRESENT);
    CHECK(info.value_count == 1);
    value_info.struct_size = sizeof(value_info);
    CHECK(quantapdf_form_field_value_get_info(form, index, 0, &value_info) == QUANTAPDF_OK);
    CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_UTF8);
    CHECK(quantapdf_form_field_value_utf8(form, index, 0, &text, &size) == QUANTAPDF_OK);
    CHECK(text != NULL && size == expected_size);
    CHECK(memcmp(text, expected, expected_size) == 0);
}

static void make_text_update(quantapdf_form_value_input *value,
    quantapdf_form_value_update *update, const char *text, size_t size)
{
    memset(value, 0, sizeof(*value));
    memset(update, 0, sizeof(*update));
    value->struct_size = sizeof(*value);
    value->kind = QUANTAPDF_FORM_VALUE_UTF8;
    value->option_index = SIZE_MAX;
    value->utf8 = text;
    value->utf8_size = size;
    update->struct_size = sizeof(*update);
    update->presence = QUANTAPDF_FORM_VALUE_PRESENT;
    update->values = value;
    update->value_count = 1;
}

static void make_option_update(quantapdf_form_value_input *value,
    quantapdf_form_value_update *update, size_t option_index)
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

static void make_options_update(quantapdf_form_value_input *values,
    quantapdf_form_value_update *update, const size_t *option_indices, size_t count)
{
    size_t i;

    memset(update, 0, sizeof(*update));
    for (i = 0; i < count; ++i) {
        memset(&values[i], 0, sizeof(values[i]));
        values[i].struct_size = sizeof(values[i]);
        values[i].kind = QUANTAPDF_FORM_VALUE_OPTION;
        values[i].option_index = option_indices[i];
    }
    update->struct_size = sizeof(*update);
    update->presence = QUANTAPDF_FORM_VALUE_PRESENT;
    update->values = count != 0 ? values : NULL;
    update->value_count = count;
}

static void make_empty_update(quantapdf_form_value_update *update,
    quantapdf_form_value_presence presence)
{
    memset(update, 0, sizeof(*update));
    update->struct_size = sizeof(*update);
    update->presence = presence;
    update->values = NULL;
    update->value_count = 0;
}

static quantapdf_pdf_edit *open_edit(const char *path)
{
    quantapdf_document *document = NULL;
    quantapdf_pdf_edit *edit = NULL;
    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_begin(document, &edit) == QUANTAPDF_OK);
    quantapdf_close(document);
    return edit;
}

static void test_editor_snapshot_and_refs(void)
{
    quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    quantapdf_form *first = NULL;
    quantapdf_form *second = NULL;
    quantapdf_form_field_ref ref0 = {{9, 9}}, ref0_again = {{8, 8}}, ref1 = {{7, 7}}, bad = {{6, 6}};

    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &first) == QUANTAPDF_OK);
    expect_form_count(first, 4);
    CHECK(quantapdf_pdf_edit_form_field_ref_at(edit, 0, &ref0) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_field_ref_at(edit, 0, &ref0_again) == QUANTAPDF_OK);
    CHECK(memcmp(&ref0, &ref0_again, sizeof(ref0)) == 0);
    CHECK(quantapdf_pdf_edit_form_field_ref_at(edit, 1, &ref1) == QUANTAPDF_OK);
    CHECK(memcmp(&ref0, &ref1, sizeof(ref0)) != 0);
    CHECK(quantapdf_pdf_edit_form_field_ref_at(edit, SIZE_MAX, &bad) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bad.opaque[0] == 0 && bad.opaque[1] == 0);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &second) == QUANTAPDF_OK);
    quantapdf_drop_pdf_edit(edit);
    expect_form_count(first, 4);
    expect_form_count(second, 4);
    quantapdf_drop_form(first);
    quantapdf_drop_form(second);
}

static void test_snapshot_borrows_survive_source_mutation(void)
{
    quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    quantapdf_form_field_ref ref = field_ref_by_name(edit, "zero");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;
    quantapdf_form *first = NULL, *second = NULL;
    const char *borrowed = NULL, *again = NULL;
    size_t size = 0, again_size = 0, field_index;

    make_text_update(&value, &update, "alpha", 5);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &first) == QUANTAPDF_OK);
    field_index = form_field_index_by_name(first, "zero");
    CHECK(quantapdf_form_field_value_utf8(first, field_index, 0,
        &borrowed, &size) == QUANTAPDF_OK);
    CHECK(size == 5 && memcmp(borrowed, "alpha", size) == 0);

    make_text_update(&value, &update, "beta", 4);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &second) == QUANTAPDF_OK);
    quantapdf_drop_pdf_edit(edit);
    expect_text_field(first, "zero", QUANTAPDF_FORM_VALUE_PRESENT, "alpha");
    expect_text_field(second, "zero", QUANTAPDF_FORM_VALUE_PRESENT, "beta");
    quantapdf_drop_form(second);

    CHECK(quantapdf_form_field_value_utf8(first, field_index, 0,
        &again, &again_size) == QUANTAPDF_OK);
    CHECK(again == borrowed && again_size == size);
    CHECK(memcmp(borrowed, "alpha", size) == 0);
    quantapdf_drop_form(first);
}

static void test_need_appearances_observation_is_byte_preserving(void)
{
    quantapdf_output *plain = snapshot_source(FORM_MUTATION_NEED_APPEARANCES_PDF, 0);
    quantapdf_output *observed = snapshot_source(FORM_MUTATION_NEED_APPEARANCES_PDF, 1);
    unsigned char *plain_data = NULL, *observed_data = NULL;
    size_t plain_size = 0, observed_size = 0;
    quantapdf_pdf_edit *edit;
    quantapdf_output *before = NULL, *after = NULL;
    quantapdf_form *form = NULL;
    quantapdf_form_field_ref ref = {{0, 0}};
    unsigned char *before_data = NULL, *after_data = NULL;
    size_t before_size = 0, after_size = 0;

    copy_output(plain, &plain_data, &plain_size);
    copy_output(observed, &observed_data, &observed_size);
    CHECK(plain_size == observed_size && memcmp(plain_data, observed_data, plain_size) == 0);
    free(plain_data); free(observed_data);
    quantapdf_drop_output(plain); quantapdf_drop_output(observed);

    edit = open_edit(FORM_MUTATION_NEED_APPEARANCES_PDF);
    CHECK(quantapdf_pdf_edit_snapshot(edit, &before) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_form_count(form, 1);
    CHECK(quantapdf_pdf_edit_form_field_ref_at(edit, 0, &ref) == QUANTAPDF_OK);
    quantapdf_drop_form(form);
    CHECK(quantapdf_pdf_edit_snapshot(edit, &after) == QUANTAPDF_OK);
    copy_output(before, &before_data, &before_size);
    copy_output(after, &after_data, &after_size);
    CHECK(before_size == after_size && memcmp(before_data, after_data, before_size) == 0);
    free(before_data); free(after_data);
    quantapdf_drop_output(before); quantapdf_drop_output(after);
    quantapdf_drop_pdf_edit(edit);
}

static void test_widget_prepare_is_byte_preserving(void)
{
    quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    quantapdf_form_field_ref ref = field_ref_by_name(edit, "textWidget");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;
    unsigned char *before = NULL, *after = NULL;
    size_t before_size = 0, after_size = 0;

    make_text_update(&value, &update, "new-text", 8);
    copy_editor_output(edit, &before, &before_size);
    quantapdf_test_pdf_edit_set_fault(edit, QUANTAPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_ERROR_BACKEND);
    copy_editor_output(edit, &after, &after_size);
    CHECK(before_size == after_size && memcmp(before, after, before_size) == 0);
    free(before); free(after);
    quantapdf_drop_pdf_edit(edit);
}

static void test_zero_widget_text_roundtrip(void)
{
    quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    quantapdf_form_field_ref ref = field_ref_by_name(edit, "zero");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;
    quantapdf_form *form = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *reopened = NULL;

    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_text_field(form, "zero", QUANTAPDF_FORM_VALUE_MISSING, "");
    quantapdf_drop_form(form); form = NULL;

    make_text_update(&value, &update, "alpha", 5);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_text_field(form, "zero", QUANTAPDF_FORM_VALUE_PRESENT, "alpha");
    quantapdf_drop_form(form); form = NULL;

    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(output, FORM_MUTATION_ROUNDTRIP_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(FORM_MUTATION_ROUNDTRIP_PDF, NULL, &reopened) == QUANTAPDF_OK);
    CHECK(quantapdf_document_form(reopened, &form) == QUANTAPDF_OK);
    expect_text_field(form, "zero", QUANTAPDF_FORM_VALUE_PRESENT, "alpha");
    quantapdf_drop_form(form); quantapdf_close(reopened); quantapdf_drop_output(output);
    remove(FORM_MUTATION_ROUNDTRIP_PDF);

    update.presence = QUANTAPDF_FORM_VALUE_MISSING;
    update.values = NULL;
    update.value_count = 0;
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_text_field(form, "zero", QUANTAPDF_FORM_VALUE_MISSING, "");
    quantapdf_drop_form(form);
    quantapdf_drop_pdf_edit(edit);
}

static void test_text_validation_and_noop(void)
{
    quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    quantapdf_form_field_ref ref = field_ref_by_name(edit, "zero");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;
    quantapdf_form *form = NULL;
    unsigned char invalid[] = {0xC0, 0x80};
    char embedded[] = {'a', '\0', 'b'};
    unsigned char *before = NULL, *after = NULL;
    size_t before_size = 0, after_size = 0;
    static const char empty[] = "";

    make_text_update(&value, &update, (const char *)invalid, sizeof(invalid));
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_ERROR_ARGUMENT);
    make_text_update(&value, &update, embedded, sizeof(embedded));
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_ERROR_ARGUMENT);
    make_text_update(&value, &update, empty, 0);
    value.struct_size = sizeof(value) + sizeof(uint64_t);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) ==
          QUANTAPDF_ERROR_ARGUMENT);
    make_text_update(&value, &update, empty, 0);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_text_field(form, "zero", QUANTAPDF_FORM_VALUE_PRESENT, "");
    quantapdf_drop_form(form);

    copy_editor_output(edit, &before, &before_size);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_OK);
    copy_editor_output(edit, &after, &after_size);
    CHECK(before_size == after_size && memcmp(before, after, before_size) == 0);
    free(before); free(after);
    quantapdf_drop_pdf_edit(edit);
}

static void test_text_modes_and_preflight(void)
{
    static const struct {
        const char *name;
        quantapdf_status expected;
    } cases[] = {
        {"readonly", QUANTAPDF_ERROR_STATE},
        {"required", QUANTAPDF_OK},
        {"noexport", QUANTAPDF_OK},
        {"rich", QUANTAPDF_ERROR_UNSUPPORTED},
        {"file", QUANTAPDF_ERROR_UNSUPPORTED},
        {"push", QUANTAPDF_ERROR_UNSUPPORTED},
        {"sig", QUANTAPDF_ERROR_UNSUPPORTED},
        {"unknown", QUANTAPDF_ERROR_UNSUPPORTED}
    };
    size_t i;
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;

    make_text_update(&value, &update, "changed", 7);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_MODES_PDF);
        quantapdf_form_field_ref ref = field_ref_by_name(edit, cases[i].name);
        CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == cases[i].expected);
        quantapdf_drop_pdf_edit(edit);
    }
    {
        quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_NEED_APPEARANCES_PDF);
        quantapdf_form_field_ref ref = field_ref_by_name(edit, "field");
        CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_drop_pdf_edit(edit);
    }
    {
        quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_XFA_PDF);
        quantapdf_form_field_ref ref = field_ref_by_name(edit, "field");
        CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_drop_pdf_edit(edit);
    }
    {
        quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_BAD_NEED_APPEARANCES_PDF);
        quantapdf_form_field_ref ref = field_ref_by_name(edit, "field");
        CHECK(quantapdf_pdf_edit_form_set_values(edit, &ref, &update) == QUANTAPDF_ERROR_FORMAT);
        quantapdf_drop_pdf_edit(edit);
    }
}

static void test_text_group_and_inheritance(void)
{
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;
    quantapdf_form *form = NULL;
    quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_GROUPS_PDF);
    quantapdf_form_field_ref group = field_ref_by_name(edit, "g");
    quantapdf_form_field_ref target = field_ref_by_name(edit, "target");

    make_text_update(&value, &update, "new-group", 9);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &group, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_text_field(form, "g", QUANTAPDF_FORM_VALUE_PRESENT, "new-group");
    expect_text_field(form, "target", QUANTAPDF_FORM_VALUE_PRESENT, "shared");
    expect_text_field(form, "sibling", QUANTAPDF_FORM_VALUE_PRESENT, "shared");
    quantapdf_drop_form(form); form = NULL;

    update.presence = QUANTAPDF_FORM_VALUE_MISSING;
    update.values = NULL;
    update.value_count = 0;
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &target, &update) == QUANTAPDF_ERROR_UNSUPPORTED);

    make_text_update(&value, &update, "local", 5);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &target, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_text_field(form, "target", QUANTAPDF_FORM_VALUE_PRESENT, "local");
    expect_text_field(form, "sibling", QUANTAPDF_FORM_VALUE_PRESENT, "shared");
    quantapdf_drop_form(form);
    quantapdf_drop_pdf_edit(edit);
}

static void test_button_mutation_and_ap_preservation(void)
{
    quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    quantapdf_form_field_ref check = field_ref_by_name(edit, "check");
    quantapdf_form_field_ref radio = field_ref_by_name(edit, "radio");
    quantapdf_form_value_input value;
    quantapdf_form_value_update update;
    quantapdf_form *form = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *reopened = NULL;
    unsigned char *bytes = NULL;
    size_t size = 0;

    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_button_field(form, "check", QUANTAPDF_FORM_FIELD_CHECKBOX,
        QUANTAPDF_FORM_VALUE_PRESENT, 0, SIZE_MAX);
    expect_button_field(form, "radio", QUANTAPDF_FORM_FIELD_RADIO_BUTTON,
        QUANTAPDF_FORM_VALUE_PRESENT, 1, 0);
    quantapdf_drop_form(form); form = NULL;

    make_option_update(&value, &update, 0);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &check, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_button_field(form, "check", QUANTAPDF_FORM_FIELD_CHECKBOX,
        QUANTAPDF_FORM_VALUE_PRESENT, 1, 0);
    quantapdf_drop_form(form); form = NULL;

    make_empty_update(&update, QUANTAPDF_FORM_VALUE_PRESENT);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &check, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_button_field(form, "check", QUANTAPDF_FORM_FIELD_CHECKBOX,
        QUANTAPDF_FORM_VALUE_PRESENT, 0, SIZE_MAX);
    quantapdf_drop_form(form); form = NULL;

    make_empty_update(&update, QUANTAPDF_FORM_VALUE_MISSING);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &check, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_button_field(form, "check", QUANTAPDF_FORM_FIELD_CHECKBOX,
        QUANTAPDF_FORM_VALUE_MISSING, 0, SIZE_MAX);
    quantapdf_drop_form(form); form = NULL;

    make_option_update(&value, &update, 1);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &radio, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_button_field(form, "radio", QUANTAPDF_FORM_FIELD_RADIO_BUTTON,
        QUANTAPDF_FORM_VALUE_PRESENT, 1, 1);
    quantapdf_drop_form(form); form = NULL;

    make_option_update(&value, &update, 0);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &radio, &update) == QUANTAPDF_OK);
    make_empty_update(&update, QUANTAPDF_FORM_VALUE_PRESENT);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &radio, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_button_field(form, "radio", QUANTAPDF_FORM_FIELD_RADIO_BUTTON,
        QUANTAPDF_FORM_VALUE_PRESENT, 0, SIZE_MAX);
    quantapdf_drop_form(form); form = NULL;

    make_empty_update(&update, QUANTAPDF_FORM_VALUE_MISSING);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &radio, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_button_field(form, "radio", QUANTAPDF_FORM_FIELD_RADIO_BUTTON,
        QUANTAPDF_FORM_VALUE_MISSING, 0, SIZE_MAX);
    quantapdf_drop_form(form); form = NULL;

    make_option_update(&value, &update, 99);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &radio, &update) == QUANTAPDF_ERROR_ARGUMENT);
    make_text_update(&value, &update, "bad", 3);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &radio, &update) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    copy_output(output, &bytes, &size);
    CHECK(count_bytes(bytes, size, "CHECK-AP-KEEP-OFF") == 1);
    CHECK(count_bytes(bytes, size, "CHECK-AP-KEEP-YES") == 1);
    CHECK(count_bytes(bytes, size, "RADIO-AP-KEEP-OFF") == 1);
    CHECK(count_bytes(bytes, size, "RADIO-AP-KEEP-ON") == 1);
    free(bytes);

    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    CHECK(quantapdf_output_save_file(output, FORM_MUTATION_ROUNDTRIP_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(FORM_MUTATION_ROUNDTRIP_PDF, NULL, &reopened) == QUANTAPDF_OK);
    CHECK(quantapdf_document_form(reopened, &form) == QUANTAPDF_OK);
    expect_button_field(form, "check", QUANTAPDF_FORM_FIELD_CHECKBOX,
        QUANTAPDF_FORM_VALUE_MISSING, 0, SIZE_MAX);
    expect_button_field(form, "radio", QUANTAPDF_FORM_FIELD_RADIO_BUTTON,
        QUANTAPDF_FORM_VALUE_MISSING, 0, SIZE_MAX);
    quantapdf_drop_form(form);
    quantapdf_close(reopened);
    quantapdf_drop_output(output);
    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    quantapdf_drop_pdf_edit(edit);
}

static void test_choice_mutation(void)
{
    quantapdf_pdf_edit *edit = open_edit(FORM_MUTATION_CHOICE_PDF);
    quantapdf_form_field_ref combo = field_ref_by_name(edit, "combo");
    quantapdf_form_field_ref editable = field_ref_by_name(edit, "editable");
    quantapdf_form_field_ref single = field_ref_by_name(edit, "single");
    quantapdf_form_field_ref multi = field_ref_by_name(edit, "multi");
    quantapdf_form_field_ref dup = field_ref_by_name(edit, "dup");
    quantapdf_form_value_input values[2];
    quantapdf_form_value_update update;
    quantapdf_form *form = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *reopened = NULL;
    unsigned char *bytes = NULL;
    size_t size = 0;
    const size_t option0[] = {0};
    const size_t option1[] = {1};
    const size_t option2[] = {2};
    const size_t multi10[] = {1};
    const size_t multi02[] = {0, 2};
    const size_t multi20[] = {2, 0};
    const size_t duplicate[] = {1, 1};
    static const char custom[] = "Nagoya";
    static const char empty[] = "";

    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "combo", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option1, 1);
    expect_choice_utf8(form, "editable", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        "Kyoto", 5);
    expect_choice_options(form, "single", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option1, 1);
    expect_choice_options(form, "multi", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, multi02, 2);
    expect_choice_options(form, "dup", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option1, 1);
    quantapdf_drop_form(form); form = NULL;

    make_option_update(&values[0], &update, 0);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &combo, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "combo", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option0, 1);
    quantapdf_drop_form(form); form = NULL;

    make_text_update(&values[0], &update, "bad", 3);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &combo, &update) == QUANTAPDF_ERROR_ARGUMENT);

    make_option_update(&values[0], &update, 1);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &editable, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "editable", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option1, 1);
    quantapdf_drop_form(form); form = NULL;

    make_text_update(&values[0], &update, custom, sizeof(custom) - 1);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &editable, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_utf8(form, "editable", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        custom, sizeof(custom) - 1);
    quantapdf_drop_form(form); form = NULL;

    make_text_update(&values[0], &update, empty, 0);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &editable, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_utf8(form, "editable", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        empty, 0);
    quantapdf_drop_form(form); form = NULL;

    make_option_update(&values[0], &update, 0);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &dup, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "dup", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option0, 1);
    quantapdf_drop_form(form); form = NULL;
    make_option_update(&values[0], &update, 1);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &dup, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "dup", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option1, 1);
    quantapdf_drop_form(form); form = NULL;

    make_option_update(&values[0], &update, 2);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &single, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "single", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option2, 1);
    quantapdf_drop_form(form); form = NULL;
    make_empty_update(&update, QUANTAPDF_FORM_VALUE_PRESENT);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &single, &update) == QUANTAPDF_ERROR_ARGUMENT);
    make_empty_update(&update, QUANTAPDF_FORM_VALUE_MISSING);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &single, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "single", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_MISSING, NULL, 0);
    quantapdf_drop_form(form); form = NULL;

    make_empty_update(&update, QUANTAPDF_FORM_VALUE_PRESENT);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &multi, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "multi", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, NULL, 0);
    quantapdf_drop_form(form); form = NULL;

    make_options_update(values, &update, multi10, 1);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &multi, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "multi", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, multi10, 1);
    quantapdf_drop_form(form); form = NULL;

    make_options_update(values, &update, duplicate, 2);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &multi, &update) == QUANTAPDF_ERROR_ARGUMENT);

    make_options_update(values, &update, multi20, 2);
    CHECK(quantapdf_pdf_edit_form_set_values(edit, &multi, &update) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "multi", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, multi20, 2);
    quantapdf_drop_form(form); form = NULL;

    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    copy_output(output, &bytes, &size);
    CHECK(count_bytes(bytes, size, "CHOICE-AP-0") == 1);
    CHECK(count_bytes(bytes, size, "CHOICE-AP-1") == 1);
    CHECK(count_bytes(bytes, size, "CHOICE-AP-2") == 1);
    CHECK(count_bytes(bytes, size, "CHOICE-AP-3") == 1);
    CHECK(count_bytes(bytes, size, "CHOICE-AP-4") == 1);
    free(bytes);

    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    CHECK(quantapdf_output_save_file(output, FORM_MUTATION_ROUNDTRIP_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(FORM_MUTATION_ROUNDTRIP_PDF, NULL, &reopened) == QUANTAPDF_OK);
    CHECK(quantapdf_document_form(reopened, &form) == QUANTAPDF_OK);
    expect_choice_options(form, "combo", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option0, 1);
    expect_choice_utf8(form, "editable", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        empty, 0);
    expect_choice_options(form, "single", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_MISSING, NULL, 0);
    expect_choice_options(form, "multi", QUANTAPDF_FORM_FIELD_LIST_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, multi20, 2);
    expect_choice_options(form, "dup", QUANTAPDF_FORM_FIELD_COMBO_BOX,
        QUANTAPDF_FORM_VALUE_PRESENT, option1, 1);
    quantapdf_drop_form(form);
    quantapdf_close(reopened);
    quantapdf_drop_output(output);
    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    quantapdf_drop_pdf_edit(edit);
}

static void test_api_reset(void)
{
    quantapdf_form *form = (quantapdf_form *)(uintptr_t)1;
    quantapdf_form_field_ref ref = {{UINT64_MAX, UINT64_MAX}};
    CHECK(quantapdf_pdf_edit_form_snapshot(NULL, &form) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(form == NULL);
    CHECK(quantapdf_pdf_edit_form_snapshot(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_pdf_edit_form_field_ref_at(NULL, 0, &ref) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref.opaque[0] == 0 && ref.opaque[1] == 0);
    CHECK(quantapdf_pdf_edit_form_field_ref_at(NULL, 0, NULL) == QUANTAPDF_ERROR_ARGUMENT);
}

int main(void)
{
    compile_surface();
    test_api_reset();
    test_editor_snapshot_and_refs();
    test_snapshot_borrows_survive_source_mutation();
    test_need_appearances_observation_is_byte_preserving();
    test_widget_prepare_is_byte_preserving();
    test_zero_widget_text_roundtrip();
    test_text_validation_and_noop();
    test_text_modes_and_preflight();
    test_text_group_and_inheritance();
    test_button_mutation_and_ap_preservation();
    test_choice_mutation();
    return EXIT_SUCCESS;
}
