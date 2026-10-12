#include "pdf_graphics_state.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static quantapdf_pdf_graphics_state initial_state(void)
{
    quantapdf_pdf_graphics_state value = {0};
    value.ctm[0] = value.ctm[3] = 1.0;
    value.line_width = 1.0;
    value.fill_alpha = value.stroke_alpha = 1.0;
    value.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    value.miter_limit = 10.0;
    return value;
}

/* PDF row-vector convention; "cm M" premultiplies: CTM = M * CTM. */
static quantapdf_status concatenate(double ctm[6], const double m[6])
{
    double result[6];
    result[0] = m[0] * ctm[0] + m[1] * ctm[2];
    result[1] = m[0] * ctm[1] + m[1] * ctm[3];
    result[2] = m[2] * ctm[0] + m[3] * ctm[2];
    result[3] = m[2] * ctm[1] + m[3] * ctm[3];
    result[4] = m[4] * ctm[0] + m[5] * ctm[2] + ctm[4];
    result[5] = m[4] * ctm[1] + m[5] * ctm[3] + ctm[5];
    for (size_t i = 0; i < 6; ++i)
        if (!isfinite(result[i])) return QUANTAPDF_ERROR_UNSUPPORTED;
    memcpy(ctm, result, sizeof(result));
    return QUANTAPDF_OK;
}

static quantapdf_status grow(void **data, size_t *capacity, size_t count,
    size_t limit, size_t element)
{
    size_t next;
    void *buffer;
    if (count < *capacity) return QUANTAPDF_OK;
    if (count >= limit) return QUANTAPDF_ERROR_UNSUPPORTED;
    next = *capacity == 0 ? 8 : *capacity;
    if (*capacity != 0 && next <= SIZE_MAX / 2) next *= 2;
    else if (*capacity != 0) next = limit;
    if (next > limit) next = limit;
    if (next <= *capacity || next > SIZE_MAX / element)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    buffer = realloc(*data, next * element);
    if (buffer == NULL) return QUANTAPDF_ERROR_NOMEM;
    *data = buffer;
    *capacity = next;
    return QUANTAPDF_OK;
}

void quantapdf_pdf_graphics_dispose(quantapdf_pdf_graphics_ir *graphics)
{
    if (graphics == NULL) return;
    free(graphics->paths);
    free(graphics->clips);
    free(graphics);
}

static int is_terminator(quantapdf_pdf_content_opcode opcode)
{
    return opcode >= QUANTAPDF_PDF_OP_STROKE &&
        opcode <= QUANTAPDF_PDF_OP_END_PATH;
}

static int is_constructor(quantapdf_pdf_content_opcode opcode)
{
    return opcode >= QUANTAPDF_PDF_OP_MOVE &&
        opcode <= QUANTAPDF_PDF_OP_RECT;
}

/* Do not accept partial graphics objects as successful semantic imports. */
quantapdf_status quantapdf_pdf_graphics_evaluate_resolved(
    const quantapdf_pdf_content_ir *source,
    const quantapdf_pdf_content_limits *limits,
    const quantapdf_pdf_extgstate_delta *deltas, size_t deltas_count,
    quantapdf_pdf_graphics_ir **out_graphics)
{
    quantapdf_pdf_graphics_ir *ir = NULL;
    quantapdf_pdf_graphics_state current, *stack = NULL;
    quantapdf_status status = QUANTAPDF_OK;
    size_t depth = 0, path_capacity = 0, clip_capacity = 0;
    size_t path_begin = 0, clip_operator = 0;
    size_t slots;
    int in_path = 0, pending_clip = 0;
    if (out_graphics != NULL) *out_graphics = NULL;
    if (out_graphics == NULL || source == NULL || limits == NULL ||
        (source->count != 0 && source->ops == NULL) ||
        (source->dash_value_count != 0 && source->dash_values == NULL) ||
        (source->name_byte_count != 0 && source->name_bytes == NULL) ||
        ((deltas == NULL && deltas_count != 0) ||
         (deltas != NULL && deltas_count != source->count)) ||
        limits->max_graphics_depth == 0 || limits->max_operators == 0 ||
        source->count > limits->max_operators) return QUANTAPDF_ERROR_ARGUMENT;
    ir = (quantapdf_pdf_graphics_ir *)calloc(1, sizeof(*ir));
    if (ir == NULL) return QUANTAPDF_ERROR_NOMEM;
    /* Only operations can push state, regardless of a huge depth budget. */
    slots = source->count < limits->max_graphics_depth ?
        source->count : limits->max_graphics_depth;
    if (slots > SIZE_MAX / sizeof(*stack)) { status = QUANTAPDF_ERROR_UNSUPPORTED; goto fail; }
    if (slots != 0) {
        stack = (quantapdf_pdf_graphics_state *)malloc(slots * sizeof(*stack));
        if (stack == NULL) { status = QUANTAPDF_ERROR_NOMEM; goto fail; }
    }
    current = initial_state();
    for (size_t i = 0; i < source->count; ++i) {
        const quantapdf_pdf_content_op *op = &source->ops[i];
        const double *a = op->arguments;
        if (op->argument_count > 6) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
        for (size_t j = 0; j < op->argument_count; ++j)
            if (!isfinite(a[j])) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
        if (is_constructor(op->opcode)) {
            if (pending_clip) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
            if (!in_path) { in_path = 1; path_begin = i; }
            continue;
        }
        if (op->opcode == QUANTAPDF_PDF_OP_CLIP ||
            op->opcode == QUANTAPDF_PDF_OP_CLIP_EVEN_ODD) {
            if (pending_clip) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
            if (!in_path) { in_path = 1; path_begin = i; }
            pending_clip = op->opcode == QUANTAPDF_PDF_OP_CLIP ? 1 : 2;
            clip_operator = i;
            continue;
        }
        if (is_terminator(op->opcode)) {
            quantapdf_pdf_path_record *record;
            if (!in_path) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
            /* Source-PDF compatibility: PDFium leaves the current clipping
             * region unchanged for W/W* on a path with no constructors.
             * Consume the empty W n pair without publishing a Clip node. */
            if (pending_clip && path_begin == clip_operator) {
                if (op->opcode != QUANTAPDF_PDF_OP_END_PATH) {
                    status = QUANTAPDF_ERROR_UNSUPPORTED; goto fail;
                }
                in_path = pending_clip = 0;
                continue;
            }
            status = grow((void **)&ir->paths, &path_capacity, ir->path_count,
                limits->max_operators, sizeof(*ir->paths));
            if (status != QUANTAPDF_OK) goto fail;
            record = &ir->paths[ir->path_count++];
            record->path_first_operator = path_begin;
            record->terminal_operator = i;
            record->paint_opcode = op->opcode;
            record->graphics = current;
            record->clip_after = current.clip_id;
            if (pending_clip) {
                quantapdf_pdf_clip_record *clip;
                status = grow((void **)&ir->clips, &clip_capacity, ir->clip_count,
                    limits->max_operators, sizeof(*ir->clips));
                if (status != QUANTAPDF_OK) goto fail;
                clip = &ir->clips[ir->clip_count++];
                clip->parent_clip_id = current.clip_id;
                clip->path_first_operator = path_begin;
                clip->clip_operator = clip_operator;
                clip->terminal_operator = i;
                clip->even_odd = pending_clip == 2;
                current.clip_id = ir->clip_count;
                record->clip_after = current.clip_id;
            }
            in_path = pending_clip = 0;
            continue;
        }
        if (in_path) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
        switch (op->opcode) {
        case QUANTAPDF_PDF_OP_SAVE:
            if (depth >= slots) { status = QUANTAPDF_ERROR_UNSUPPORTED; goto fail; }
            stack[depth++] = current;
            break;
        case QUANTAPDF_PDF_OP_RESTORE:
            if (depth == 0) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
            current = stack[--depth];
            break;
        case QUANTAPDF_PDF_OP_CONCAT:
            status = concatenate(current.ctm, a);
            if (status != QUANTAPDF_OK) goto fail;
            break;
        case QUANTAPDF_PDF_OP_LINE_WIDTH:
            if (a[0] < 0.0) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
            current.line_width = a[0];
            break;
        case QUANTAPDF_PDF_OP_LINE_CAP:
        case QUANTAPDF_PDF_OP_LINE_JOIN:
            if (op->argument_count != 1 ||
                (a[0] != 0.0 && a[0] != 1.0 && a[0] != 2.0)) {
                status = QUANTAPDF_ERROR_FORMAT; goto fail;
            }
            if (op->opcode == QUANTAPDF_PDF_OP_LINE_CAP)
                current.line_cap = (int)a[0];
            else current.line_join = (int)a[0];
            break;
        case QUANTAPDF_PDF_OP_MITER_LIMIT:
            if (op->argument_count != 1 || a[0] < 1.0) {
                status = QUANTAPDF_ERROR_FORMAT; goto fail;
            }
            current.miter_limit = a[0];
            break;
        case QUANTAPDF_PDF_OP_DASH:
            if (op->argument_count != 1 || a[0] < 0.0 ||
                op->dash_count > QUANTAPDF_PDF_MAX_DASH_ITEMS ||
                op->dash_offset > source->dash_value_count ||
                op->dash_count > source->dash_value_count - op->dash_offset) {
                status = QUANTAPDF_ERROR_FORMAT; goto fail;
            }
            if (op->dash_count != 0) {
                int any_positive = 0;
                for (size_t j = 0; j < op->dash_count; ++j) {
                    double length = source->dash_values[op->dash_offset + j];
                    if (!isfinite(length) || length < 0.0) {
                        status = QUANTAPDF_ERROR_FORMAT; goto fail;
                    }
                    if (length > 0.0) any_positive = 1;
                }
                if (!any_positive) {
                    status = QUANTAPDF_ERROR_FORMAT; goto fail;
                }
            }
            current.dash_offset = op->dash_offset;
            current.dash_count = op->dash_count;
            current.dash_phase = op->dash_count != 0 ? a[0] : 0.0;
            break;
        case QUANTAPDF_PDF_OP_EXT_GSTATE: {
            const quantapdf_pdf_extgstate_delta *delta;
            if (deltas == NULL || i >= deltas_count || !deltas[i].resolved) {
                status = QUANTAPDF_ERROR_UNSUPPORTED; goto fail;
            }
            delta = &deltas[i];
            if ((delta->presence & ~7u) != 0) {
                status = QUANTAPDF_ERROR_FORMAT; goto fail;
            }
            if ((delta->presence & QUANTAPDF_PDF_GS_FILL_ALPHA) != 0) {
                if (!isfinite(delta->fill_alpha) || delta->fill_alpha < 0.0 ||
                    delta->fill_alpha > 1.0) {
                    status = QUANTAPDF_ERROR_FORMAT; goto fail;
                }
                current.fill_alpha = delta->fill_alpha;
            }
            if ((delta->presence & QUANTAPDF_PDF_GS_STROKE_ALPHA) != 0) {
                if (!isfinite(delta->stroke_alpha) || delta->stroke_alpha < 0.0 ||
                    delta->stroke_alpha > 1.0) {
                    status = QUANTAPDF_ERROR_FORMAT; goto fail;
                }
                current.stroke_alpha = delta->stroke_alpha;
            }
            if ((delta->presence & QUANTAPDF_PDF_GS_BLEND_MODE) != 0) {
                if (delta->blend_mode < QUANTAPDF_COMPOSER_BLEND_NORMAL ||
                    delta->blend_mode > QUANTAPDF_COMPOSER_BLEND_LIGHTEN) {
                    status = QUANTAPDF_ERROR_UNSUPPORTED; goto fail;
                }
                current.blend_mode = delta->blend_mode;
            }
            break;
        }
        case QUANTAPDF_PDF_OP_STROKE_GRAY:
        case QUANTAPDF_PDF_OP_FILL_GRAY: {
            double *dest = op->opcode == QUANTAPDF_PDF_OP_FILL_GRAY ?
                current.fill_rgb : current.stroke_rgb;
            if (a[0] < 0. || a[0] > 1.) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
            dest[0] = dest[1] = dest[2] = a[0];
            break;
        }
        case QUANTAPDF_PDF_OP_STROKE_RGB:
        case QUANTAPDF_PDF_OP_FILL_RGB: {
            double *dest = op->opcode == QUANTAPDF_PDF_OP_FILL_RGB ?
                current.fill_rgb : current.stroke_rgb;
            for (size_t k = 0; k < 3; ++k) {
                if (a[k] < 0. || a[k] > 1.) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
                dest[k] = a[k];
            }
            break;
        }
        default:
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto fail;
        }
    }
    if (in_path || pending_clip || depth) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
    free(stack);
    *out_graphics = ir;
    return QUANTAPDF_OK;
fail:
    free(stack);
    quantapdf_pdf_graphics_dispose(ir);
    return status;
}


quantapdf_status quantapdf_pdf_graphics_evaluate(
    const quantapdf_pdf_content_ir *source,
    const quantapdf_pdf_content_limits *limits,
    quantapdf_pdf_graphics_ir **out_graphics)
{
    return quantapdf_pdf_graphics_evaluate_resolved(source, limits,
        NULL, 0, out_graphics);
}
