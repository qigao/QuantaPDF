#ifndef QUANTAPDF_FORM_SNAPSHOT_H
#define QUANTAPDF_FORM_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#include <quantapdf/quantapdf.h>

typedef struct quantapdf_pdf_form_string {
    size_t offset;
    size_t size;
    int present;
} quantapdf_pdf_form_string;

typedef struct quantapdf_pdf_form_value_internal {
    quantapdf_form_value_kind kind;
    size_t option_index;
    quantapdf_pdf_form_string utf8;
} quantapdf_pdf_form_value_internal;

typedef struct quantapdf_pdf_form_option_internal {
    quantapdf_form_option_kind kind;
    quantapdf_pdf_form_string export_text;
    quantapdf_pdf_form_string display_text;
    char *button_state;
} quantapdf_pdf_form_option_internal;

typedef struct quantapdf_pdf_form_field_internal {
    quantapdf_form_field_type type;
    uint32_t flags;
    quantapdf_form_value_presence value_presence;
    size_t first_value;
    size_t value_count;
    size_t first_option;
    size_t option_count;
    size_t widget_count;
    int is_multiselect;
    int is_signed;
    quantapdf_pdf_form_string name;
    quantapdf_pdf_form_string label;
} quantapdf_pdf_form_field_internal;

typedef struct quantapdf_pdf_form_widget_internal {
    size_t field_index;
    int page_index;
    quantapdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} quantapdf_pdf_form_widget_internal;

typedef struct quantapdf_pdf_form_model {
    quantapdf_pdf_form_field_internal *fields;
    size_t field_count;
    quantapdf_pdf_form_value_internal *values;
    size_t value_count;
    quantapdf_pdf_form_option_internal *options;
    size_t option_count;
    quantapdf_pdf_form_widget_internal *widgets;
    size_t widget_count;
    char *strings;
    size_t string_size;
    size_t string_capacity;
} quantapdf_pdf_form_model;

struct quantapdf_form {
    quantapdf_context *context;
    quantapdf_pdf_form_model *model;
};

void quantapdf_pdf_form_drop_model(quantapdf_pdf_form_model *model);

#endif
