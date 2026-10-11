#include "pdf_content_ir.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct operator_spec {
    const char *name;
    quantapdf_pdf_content_opcode opcode;
    unsigned int arity;
} operator_spec;

/* F and f are equivalent PDF operators. */
static const operator_spec vocabulary[] = {
    {"q", QUANTAPDF_PDF_OP_SAVE, 0}, {"Q", QUANTAPDF_PDF_OP_RESTORE, 0},
    {"cm", QUANTAPDF_PDF_OP_CONCAT, 6},
    {"m", QUANTAPDF_PDF_OP_MOVE, 2}, {"l", QUANTAPDF_PDF_OP_LINE, 2},
    {"c", QUANTAPDF_PDF_OP_CURVE, 6}, {"v", QUANTAPDF_PDF_OP_CURVE_V, 4},
    {"y", QUANTAPDF_PDF_OP_CURVE_Y, 4}, {"h", QUANTAPDF_PDF_OP_CLOSE, 0},
    {"re", QUANTAPDF_PDF_OP_RECT, 4},
    {"S", QUANTAPDF_PDF_OP_STROKE, 0},
    {"s", QUANTAPDF_PDF_OP_CLOSE_STROKE, 0},
    {"f", QUANTAPDF_PDF_OP_FILL, 0}, {"F", QUANTAPDF_PDF_OP_FILL, 0},
    {"f*", QUANTAPDF_PDF_OP_FILL_EVEN_ODD, 0},
    {"B", QUANTAPDF_PDF_OP_FILL_STROKE, 0},
    {"B*", QUANTAPDF_PDF_OP_FILL_STROKE_EVEN_ODD, 0},
    {"b", QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE, 0},
    {"b*", QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE_EVEN_ODD, 0},
    {"n", QUANTAPDF_PDF_OP_END_PATH, 0},
    {"W", QUANTAPDF_PDF_OP_CLIP, 0},
    {"W*", QUANTAPDF_PDF_OP_CLIP_EVEN_ODD, 0},
    {"w", QUANTAPDF_PDF_OP_LINE_WIDTH, 1},
    {"G", QUANTAPDF_PDF_OP_STROKE_GRAY, 1},
    {"g", QUANTAPDF_PDF_OP_FILL_GRAY, 1},
    {"RG", QUANTAPDF_PDF_OP_STROKE_RGB, 3},
    {"rg", QUANTAPDF_PDF_OP_FILL_RGB, 3}
};

static int is_white(unsigned char c)
{
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}

static int is_delimiter(unsigned char c)
{
    return c == '%' || c == '/' || c == '(' || c == ')' || c == '<' ||
           c == '>' || c == '[' || c == ']' || c == '{' || c == '}';
}

static int is_number_start(unsigned char c)
{
    return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

/* Decimal PDF grammar; avoids locale-dependent strtod behavior. */
static quantapdf_status read_number(const unsigned char *s, size_t n, double *out)
{
    size_t i = 0;
    double value = 0.0, fraction = 0.1;
    int negative = 0, point = 0, has_digit = 0;
    if (n > 64) return QUANTAPDF_ERROR_UNSUPPORTED;
    if (n && (s[0] == '+' || s[0] == '-')) {
        negative = s[0] == '-';
        ++i;
    }
    for (; i < n; ++i) {
        if (s[i] == '.' && !point) { point = 1; continue; }
        if (s[i] < '0' || s[i] > '9') return QUANTAPDF_ERROR_FORMAT;
        has_digit = 1;
        if (point) { value += (s[i] - '0') * fraction; fraction *= 0.1; }
        else value = value * 10.0 + (s[i] - '0');
        if (!isfinite(value)) return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (!has_digit) return QUANTAPDF_ERROR_FORMAT;
    *out = negative ? -value : value;
    return QUANTAPDF_OK;
}

static const operator_spec *find_operator(const unsigned char *s, size_t n)
{
    size_t i;
    for (i = 0; i < sizeof(vocabulary) / sizeof(vocabulary[0]); ++i) {
        const operator_spec *op = &vocabulary[i];
        size_t length = strlen(op->name);
        if (length == n && memcmp(op->name, s, n) == 0) return op;
    }
    return NULL;
}

static quantapdf_status append_operation(quantapdf_pdf_content_ir *ir, size_t *capacity,
    const quantapdf_pdf_content_limits *limits, const operator_spec *spec,
    const double *arguments, size_t offset)
{
    quantapdf_pdf_content_op *ops, *slot;
    size_t next;
    if (ir->count >= limits->max_operators) return QUANTAPDF_ERROR_UNSUPPORTED;
    if (ir->count == *capacity) {
        next = *capacity == 0 ? 16 : *capacity;
        if (*capacity != 0 && next <= SIZE_MAX / 2) next *= 2;
        else if (*capacity != 0) next = limits->max_operators;
        if (next > limits->max_operators) next = limits->max_operators;
        if (next <= *capacity || next > SIZE_MAX / sizeof(*ops))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        ops = (quantapdf_pdf_content_op *)realloc(ir->ops, next * sizeof(*ops));
        if (ops == NULL) return QUANTAPDF_ERROR_NOMEM;
        ir->ops = ops; *capacity = next;
    }
    slot = &ir->ops[ir->count++];
    memset(slot, 0, sizeof(*slot));
    slot->opcode = spec->opcode;
    slot->argument_count = spec->arity;
    if (spec->arity) memcpy(slot->arguments, arguments, spec->arity * sizeof(*arguments));
    slot->operator_byte_offset = offset;
    return QUANTAPDF_OK;
}

void quantapdf_pdf_content_dispose(quantapdf_pdf_content_ir *ir)
{
    if (ir == NULL) return;
    free(ir->ops);
    free(ir);
}

quantapdf_status quantapdf_pdf_content_parse(const unsigned char *data, size_t size,
    const quantapdf_pdf_content_limits *limits, quantapdf_pdf_content_ir **out_ir)
{
    quantapdf_pdf_content_ir *ir;
    size_t pos = 0, capacity = 0, state_depth = 0;
    double arguments[6] = {0};
    unsigned int argument_count = 0;
    int current_point = 0;
    quantapdf_status status = QUANTAPDF_OK;
    if (out_ir != NULL) *out_ir = NULL;
    if (out_ir == NULL || limits == NULL || (size && data == NULL) ||
        limits->max_input_bytes == 0 || limits->max_operators == 0 ||
        limits->max_graphics_depth == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (size > limits->max_input_bytes) return QUANTAPDF_ERROR_UNSUPPORTED;
    ir = (quantapdf_pdf_content_ir *)calloc(1, sizeof(*ir));
    if (ir == NULL) return QUANTAPDF_ERROR_NOMEM;
    while (pos < size) {
        size_t start;
        const operator_spec *spec;
        while (pos < size) {
            if (is_white(data[pos])) { ++pos; continue; }
            if (data[pos] == '%') {
                while (pos < size && data[pos] != '\n' && data[pos] != '\r') ++pos;
                continue;
            }
            break;
        }
        if (pos == size) break;
        if (is_delimiter(data[pos])) { status = QUANTAPDF_ERROR_UNSUPPORTED; break; }
        start = pos;
        while (pos < size && !is_white(data[pos]) && !is_delimiter(data[pos])) ++pos;
        if (is_number_start(data[start])) {
            if (argument_count == 6) { status = QUANTAPDF_ERROR_UNSUPPORTED; break; }
            status = read_number(data + start, pos - start, &arguments[argument_count]);
            if (status != QUANTAPDF_OK) break;
            ++argument_count;
            continue;
        }
        spec = find_operator(data + start, pos - start);
        if (spec == NULL) { status = QUANTAPDF_ERROR_UNSUPPORTED; break; }
        if (spec->arity != argument_count) { status = QUANTAPDF_ERROR_FORMAT; break; }
        if (spec->opcode == QUANTAPDF_PDF_OP_SAVE) {
            if (state_depth >= limits->max_graphics_depth) {
                status = QUANTAPDF_ERROR_UNSUPPORTED; break;
            }
            ++state_depth;
        } else if (spec->opcode == QUANTAPDF_PDF_OP_RESTORE) {
            if (state_depth == 0) { status = QUANTAPDF_ERROR_FORMAT; break; }
            --state_depth;
        }
        if (spec->opcode == QUANTAPDF_PDF_OP_MOVE ||
            spec->opcode == QUANTAPDF_PDF_OP_RECT) current_point = 1;
        if (spec->opcode == QUANTAPDF_PDF_OP_LINE ||
            spec->opcode == QUANTAPDF_PDF_OP_CURVE ||
            spec->opcode == QUANTAPDF_PDF_OP_CURVE_V ||
            spec->opcode == QUANTAPDF_PDF_OP_CURVE_Y ||
            spec->opcode == QUANTAPDF_PDF_OP_CLOSE) {
            if (!current_point) { status = QUANTAPDF_ERROR_FORMAT; break; }
        }
        if (spec->opcode == QUANTAPDF_PDF_OP_LINE_WIDTH && arguments[0] < 0.0) {
            status = QUANTAPDF_ERROR_FORMAT; break;
        }
        status = append_operation(ir, &capacity, limits, spec, arguments, start);
        if (status != QUANTAPDF_OK) break;
        argument_count = 0;
        if ((spec->opcode >= QUANTAPDF_PDF_OP_STROKE &&
             spec->opcode <= QUANTAPDF_PDF_OP_END_PATH)) current_point = 0;
    }
    if (status == QUANTAPDF_OK && (argument_count || state_depth))
        status = QUANTAPDF_ERROR_FORMAT;
    if (status != QUANTAPDF_OK) {
        quantapdf_pdf_content_dispose(ir);
        return status;
    }
    *out_ir = ir;
    return QUANTAPDF_OK;
}
