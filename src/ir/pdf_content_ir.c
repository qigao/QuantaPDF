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
    {"rg", QUANTAPDF_PDF_OP_FILL_RGB, 3},
    {"J", QUANTAPDF_PDF_OP_LINE_CAP, 1},
    {"j", QUANTAPDF_PDF_OP_LINE_JOIN, 1},
    {"M", QUANTAPDF_PDF_OP_MITER_LIMIT, 1},
    {"d", QUANTAPDF_PDF_OP_DASH, 1},
    {"gs", QUANTAPDF_PDF_OP_EXT_GSTATE, 0}
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

/* Only a one-dimensional numeric dash array is admitted. Other PDF arrays
 * are not silently interpreted as unknown operands: fail closed. Comments
 * and compact lexical boundaries have the same rules as top-level tokens. */
static quantapdf_status read_dash_array(const unsigned char *data, size_t size,
    size_t *cursor, double values[QUANTAPDF_PDF_MAX_DASH_ITEMS], size_t *count)
{
    size_t pos = *cursor + 1;
    int has_positive = 0;
    *count = 0;
    for (;;) {
        size_t start;
        quantapdf_status status;
        while (pos < size) {
            if (is_white(data[pos])) { ++pos; continue; }
            if (data[pos] == '%') {
                while (pos < size && data[pos] != '\n' && data[pos] != '\r') ++pos;
                continue;
            }
            break;
        }
        if (pos == size) return QUANTAPDF_ERROR_FORMAT;
        if (data[pos] == ']') {
            if (*count != 0 && !has_positive) return QUANTAPDF_ERROR_FORMAT;
            *cursor = pos + 1;
            return QUANTAPDF_OK;
        }
        if (!is_number_start(data[pos]))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (*count == QUANTAPDF_PDF_MAX_DASH_ITEMS)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        start = pos;
        while (pos < size && !is_white(data[pos]) && !is_delimiter(data[pos]))
            ++pos;
        status = read_number(data + start, pos - start, &values[*count]);
        if (status != QUANTAPDF_OK) return status;
        if (values[*count] < 0.0) return QUANTAPDF_ERROR_FORMAT;
        if (values[*count] > 0.0) has_positive = 1;
        ++*count;
    }
}

static quantapdf_status append_dash_values(quantapdf_pdf_content_ir *ir,
    size_t *capacity, const quantapdf_pdf_content_limits *limits,
    const double *values, size_t count)
{
    size_t needed, next;
    double *grown;
    if (count > QUANTAPDF_PDF_MAX_DASH_ITEMS ||
        ir->dash_value_count > limits->max_input_bytes ||
        count > limits->max_input_bytes - ir->dash_value_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    needed = ir->dash_value_count + count;
    if (needed > SIZE_MAX / sizeof(double))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (needed > *capacity) {
        next = *capacity == 0 ? 16 : *capacity;
        while (next < needed) {
            if (next > limits->max_input_bytes / 2) {
                next = limits->max_input_bytes;
                break;
            }
            next *= 2;
        }
        if (next < needed || next > SIZE_MAX / sizeof(double))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        grown = (double *)realloc(ir->dash_values, next * sizeof(*grown));
        if (grown == NULL) return QUANTAPDF_ERROR_NOMEM;
        ir->dash_values = grown;
        *capacity = next;
    }
    if (count != 0)
        memcpy(ir->dash_values + ir->dash_value_count,
            values, count * sizeof(*values));
    ir->dash_value_count = needed;
    return QUANTAPDF_OK;
}

/* Byte-opaque PDF names need #xx decoding in general. Admit only a small
 * canonical 7-bit subset with no escapes, never guess a resource identity.
 * Resource dictionary keys use the same slash-prefixed spelling. */
static quantapdf_status read_extgstate_name(const unsigned char *data,
    size_t size, size_t *cursor, unsigned char pending[128], size_t *length)
{
    size_t start = *cursor, pos = start + 1;
    while (pos < size && !is_white(data[pos]) && !is_delimiter(data[pos])) {
        if (data[pos] == '#' || data[pos] >= 0x7f || data[pos] < 0x21)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        ++pos;
    }
    if (pos == start + 1) return QUANTAPDF_ERROR_FORMAT;
    if (pos - start > 127) return QUANTAPDF_ERROR_UNSUPPORTED;
    memcpy(pending, data + start, pos - start);
    *length = pos - start;
    *cursor = pos;
    return QUANTAPDF_OK;
}

static quantapdf_status append_name(quantapdf_pdf_content_ir *ir,
    size_t *capacity, const quantapdf_pdf_content_limits *limits,
    const unsigned char *data, size_t size, size_t *out_offset)
{
    size_t needed, next;
    unsigned char *next_bytes;
    if (size == 0 || ir->name_byte_count > limits->max_input_bytes ||
        size > limits->max_input_bytes - ir->name_byte_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    needed = ir->name_byte_count + size;
    if (needed > *capacity) {
        next = *capacity == 0 ? 16 : *capacity;
        while (next < needed) {
            if (next > limits->max_input_bytes / 2) {
                next = limits->max_input_bytes;
                break;
            }
            next *= 2;
        }
        if (next < needed) return QUANTAPDF_ERROR_UNSUPPORTED;
        next_bytes = (unsigned char *)realloc(ir->name_bytes, next);
        if (next_bytes == NULL) return QUANTAPDF_ERROR_NOMEM;
        ir->name_bytes = next_bytes;
        *capacity = next;
    }
    *out_offset = ir->name_byte_count;
    memcpy(ir->name_bytes + ir->name_byte_count, data, size);
    ir->name_byte_count = needed;
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
    free(ir->dash_values);
    free(ir->name_bytes);
    free(ir);
}

quantapdf_status quantapdf_pdf_content_parse(const unsigned char *data, size_t size,
    const quantapdf_pdf_content_limits *limits, quantapdf_pdf_content_ir **out_ir)
{
    quantapdf_pdf_content_ir *ir;
    size_t pos = 0, capacity = 0, dash_capacity = 0, name_capacity = 0, state_depth = 0;
    double arguments[6] = {0};
    double dash[QUANTAPDF_PDF_MAX_DASH_ITEMS] = {0};
    unsigned char pending_name[128] = {0};
    size_t dash_count = 0, name_length = 0;
    unsigned int argument_count = 0;
    int current_point = 0, dash_ready = 0, name_ready = 0;
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
        if (data[pos] == '/') {
            if (argument_count != 0 || dash_ready || name_ready) {
                status = QUANTAPDF_ERROR_FORMAT; break;
            }
            status = read_extgstate_name(data, size, &pos,
                pending_name, &name_length);
            if (status != QUANTAPDF_OK) break;
            name_ready = 1;
            continue;
        }
        if (data[pos] == '[') {
            if (argument_count != 0 || dash_ready || name_ready) {
                status = QUANTAPDF_ERROR_FORMAT; break;
            }
            status = read_dash_array(data, size, &pos, dash, &dash_count);
            if (status != QUANTAPDF_OK) break;
            dash_ready = 1;
            continue;
        }
        if (is_delimiter(data[pos])) { status = QUANTAPDF_ERROR_UNSUPPORTED; break; }
        start = pos;
        while (pos < size && !is_white(data[pos]) && !is_delimiter(data[pos])) ++pos;
        if (is_number_start(data[start])) {
            if (name_ready) { status = QUANTAPDF_ERROR_FORMAT; break; }
            if (argument_count == 6) { status = QUANTAPDF_ERROR_UNSUPPORTED; break; }
            status = read_number(data + start, pos - start, &arguments[argument_count]);
            if (status != QUANTAPDF_OK) break;
            ++argument_count;
            continue;
        }
        spec = find_operator(data + start, pos - start);
        if (spec == NULL) { status = QUANTAPDF_ERROR_UNSUPPORTED; break; }
        if (spec->arity != argument_count ||
            dash_ready != (spec->opcode == QUANTAPDF_PDF_OP_DASH) ||
            name_ready != (spec->opcode == QUANTAPDF_PDF_OP_EXT_GSTATE)) {
            status = QUANTAPDF_ERROR_FORMAT; break;
        }
        if ((spec->opcode == QUANTAPDF_PDF_OP_LINE_CAP ||
             spec->opcode == QUANTAPDF_PDF_OP_LINE_JOIN) &&
            (arguments[0] != 0.0 && arguments[0] != 1.0 &&
             arguments[0] != 2.0)) {
            status = QUANTAPDF_ERROR_FORMAT; break;
        }
        if (spec->opcode == QUANTAPDF_PDF_OP_MITER_LIMIT &&
            arguments[0] < 1.0) {
            status = QUANTAPDF_ERROR_FORMAT; break;
        }
        if (spec->opcode == QUANTAPDF_PDF_OP_DASH &&
            arguments[0] < 0.0) {
            status = QUANTAPDF_ERROR_FORMAT; break;
        }
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
        if (spec->opcode == QUANTAPDF_PDF_OP_DASH) {
            quantapdf_pdf_content_op *added = &ir->ops[ir->count - 1];
            added->dash_offset = ir->dash_value_count;
            added->dash_count = dash_count;
            status = append_dash_values(ir, &dash_capacity, limits,
                dash, dash_count);
            if (status != QUANTAPDF_OK) break;
            dash_ready = 0;
            dash_count = 0;
        }
        if (spec->opcode == QUANTAPDF_PDF_OP_EXT_GSTATE) {
            quantapdf_pdf_content_op *added = &ir->ops[ir->count - 1];
            added->name_size = name_length;
            status = append_name(ir, &name_capacity, limits, pending_name,
                name_length, &added->name_offset);
            if (status != QUANTAPDF_OK) break;
            name_ready = 0;
            name_length = 0;
        }
        argument_count = 0;
        if ((spec->opcode >= QUANTAPDF_PDF_OP_STROKE &&
             spec->opcode <= QUANTAPDF_PDF_OP_END_PATH)) current_point = 0;
    }
    if (status == QUANTAPDF_OK && (argument_count || state_depth || dash_ready || name_ready))
        status = QUANTAPDF_ERROR_FORMAT;
    if (status != QUANTAPDF_OK) {
        quantapdf_pdf_content_dispose(ir);
        return status;
    }
    *out_ir = ir;
    return QUANTAPDF_OK;
}
