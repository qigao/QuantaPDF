#include "pdf_path_lowering.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The QPDF Composer serializes most geometry with four fractional digits,
 * after storing path coordinates and CTMs as floats. Enforce equality on
 * this decimal grid BEFORE publishing an operation. */
static int decimal4(double value, double writer_value)
{
    if (!isfinite(value) || !isfinite(writer_value) ||
        fabs(value) > 65536.0 || fabs(writer_value) > 65536.0)
        return 0;
    return fabs(value - round(value * 10000.0) / 10000.0) < 1e-9 &&
        fabs(value - round(writer_value * 10000.0) / 10000.0) < 1e-9;
}

static quantapdf_status point(double x, double y, float h, quantapdf_point *out)
{
    float cx, cy;
    if (!isfinite(x) || !isfinite(y)) return QUANTAPDF_ERROR_FORMAT;
    cx = (float)x;
    cy = (float)((double)h - y);
    if (!decimal4(x, (double)cx) ||
        !decimal4(y, (double)h - (double)cy))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    out->x = cx; out->y = cy;
    return QUANTAPDF_OK;
}

static quantapdf_status color(const double rgb[3], uint32_t *out)
{
    uint32_t packed = UINT32_C(0xff000000);
    for (size_t i = 0; i < 3; ++i) {
        double component = rgb[i], encoded;
        long channel;
        if (!isfinite(component) || component < 0.0 || component > 1.0)
            return QUANTAPDF_ERROR_FORMAT;
        channel = lround(component * 255.0);
        if (channel < 0 || channel > 255) return QUANTAPDF_ERROR_FORMAT;
        encoded = (double)channel / 255.0;
        if (!decimal4(component, encoded))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        packed |= (uint32_t)channel << (16u - 8u * (unsigned int)i);
    }
    *out = packed;
    return QUANTAPDF_OK;
}

static quantapdf_status transform(const double pdf[6], float height,
    quantapdf_affine_transform *out)
{
    double candidate[6], roundtrip[6], determinant;
    float stored[6];
    for (size_t i = 0; i < 6; ++i)
        if (!isfinite(pdf[i])) return QUANTAPDF_ERROR_FORMAT;
    determinant = pdf[0] * pdf[3] - pdf[1] * pdf[2];
    if (!isfinite(determinant) || fabs(determinant) < 1e-12)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Conjugate the PDF row-vector CTM by Composer's top-down page flip.
     * This exactly inverts qpdf_composer.cpp::append_path_content. */
    candidate[0] = pdf[0]; candidate[1] = -pdf[1];
    candidate[2] = -pdf[2]; candidate[3] = pdf[3];
    candidate[4] = pdf[4] + pdf[2] * (double)height;
    candidate[5] = (double)height * (1.0 - pdf[3]) - pdf[5];
    for (size_t i = 0; i < 6; ++i) {
        if (!isfinite(candidate[i]) || fabs(candidate[i]) > 65536.0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        stored[i] = (float)candidate[i];
    }
    roundtrip[0] = stored[0];
    roundtrip[1] = -(double)stored[1];
    roundtrip[2] = -(double)stored[2];
    roundtrip[3] = stored[3];
    roundtrip[4] = (double)stored[2] * height + stored[4];
    roundtrip[5] = (double)height * (1.0 - (double)stored[3]) - stored[5];
    for (size_t i = 0; i < 6; ++i)
        if (!decimal4(pdf[i], roundtrip[i]))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    out->a = stored[0]; out->b = stored[1]; out->c = stored[2];
    out->d = stored[3]; out->e = stored[4]; out->f = stored[5];
    return QUANTAPDF_OK;
}

static quantapdf_status paint_options(
    const quantapdf_pdf_path_record *path, float height,
    quantapdf_composer_path_options *options)
{
    quantapdf_pdf_content_opcode opcode = path->paint_opcode;
    quantapdf_status status;
    memset(options, 0, sizeof(*options));
    options->struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    options->fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    options->line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    options->line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    options->miter_limit = 10.0f;
    options->stroke_argb = UINT32_C(0xff000000);
    options->fill_argb = UINT32_C(0xff000000);
    switch (opcode) {
    case QUANTAPDF_PDF_OP_STROKE:
    case QUANTAPDF_PDF_OP_CLOSE_STROKE: options->stroke = 1; break;
    case QUANTAPDF_PDF_OP_FILL:
    case QUANTAPDF_PDF_OP_FILL_EVEN_ODD: options->fill = 1; break;
    case QUANTAPDF_PDF_OP_FILL_STROKE:
    case QUANTAPDF_PDF_OP_FILL_STROKE_EVEN_ODD:
    case QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE:
    case QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE_EVEN_ODD:
        options->stroke = options->fill = 1; break;
    default: return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (opcode == QUANTAPDF_PDF_OP_FILL_EVEN_ODD ||
        opcode == QUANTAPDF_PDF_OP_FILL_STROKE_EVEN_ODD ||
        opcode == QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE_EVEN_ODD)
        options->fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    status = transform(path->graphics.ctm, height, &options->transform);
    if (status != QUANTAPDF_OK) return status;
    if (options->stroke) {
        double width = path->graphics.line_width;
        if (!isfinite(width) || width < 0.0) return QUANTAPDF_ERROR_FORMAT;
        options->stroke_width = (float)width;
        if (!decimal4(width, options->stroke_width))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        status = color(path->graphics.stroke_rgb, &options->stroke_argb);
        if (status != QUANTAPDF_OK) return status;
    }
    if (options->fill) {
        status = color(path->graphics.fill_rgb, &options->fill_argb);
        if (status != QUANTAPDF_OK) return status;
    }
    return QUANTAPDF_OK;
}

static int closing_paint(quantapdf_pdf_content_opcode code)
{
    return code == QUANTAPDF_PDF_OP_CLOSE_STROKE ||
        code == QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE ||
        code == QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE_EVEN_ODD;
}

static quantapdf_status count_commands(
    const quantapdf_pdf_content_ir *content,
    const quantapdf_pdf_path_record *path, size_t available,
    size_t *out_count)
{
    size_t count = 0;
    int segment = 0;
    if (path->path_first_operator >= path->terminal_operator ||
        path->terminal_operator >= content->count ||
        content->ops[path->terminal_operator].opcode != path->paint_opcode)
        return QUANTAPDF_ERROR_FORMAT;
    for (size_t i = path->path_first_operator; i < path->terminal_operator; ++i) {
        quantapdf_pdf_content_opcode op = content->ops[i].opcode;
        size_t addition = op == QUANTAPDF_PDF_OP_RECT ? 5 : 1;
        if (op < QUANTAPDF_PDF_OP_MOVE || op > QUANTAPDF_PDF_OP_RECT)
            return QUANTAPDF_ERROR_FORMAT;
        if (count > available || addition > available - count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        count += addition;
        if (op == QUANTAPDF_PDF_OP_RECT || op == QUANTAPDF_PDF_OP_LINE ||
            op == QUANTAPDF_PDF_OP_CURVE ||
            op == QUANTAPDF_PDF_OP_CURVE_V ||
            op == QUANTAPDF_PDF_OP_CURVE_Y) segment = 1;
    }
    if (closing_paint(path->paint_opcode) &&
        content->ops[path->terminal_operator - 1].opcode != QUANTAPDF_PDF_OP_CLOSE) {
        if (count == available) return QUANTAPDF_ERROR_UNSUPPORTED;
        ++count;
    }
    if (!segment) return QUANTAPDF_ERROR_UNSUPPORTED;
    *out_count = count;
    return QUANTAPDF_OK;
}

static quantapdf_status commands_from_path(
    const quantapdf_pdf_content_ir *content,
    const quantapdf_pdf_path_record *path, float height,
    quantapdf_composer_path_command *commands, size_t expected)
{
    size_t count = 0;
    quantapdf_point current = {0}, start = {0}, target = {0};
    int have_current = 0;
    quantapdf_status status;
#define EMIT_MOVE(X,Y) do { \
    status = point((X), (Y), height, &target); \
    if (status != QUANTAPDF_OK) return status; \
    commands[count].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO; \
    commands[count++].point1 = target; current = start = target; have_current = 1; \
} while(0)
#define EMIT_LINE(X,Y) do { \
    status = point((X), (Y), height, &target); \
    if (status != QUANTAPDF_OK) return status; \
    if (!have_current) return QUANTAPDF_ERROR_FORMAT; \
    commands[count].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; \
    commands[count++].point1 = target; current = target; \
} while(0)
    for (size_t i = path->path_first_operator; i < path->terminal_operator; ++i) {
        const quantapdf_pdf_content_op *op = &content->ops[i];
        const double *a = op->arguments;
        quantapdf_composer_path_command *item = &commands[count];
        switch (op->opcode) {
        case QUANTAPDF_PDF_OP_MOVE:
            if (op->argument_count != 2) return QUANTAPDF_ERROR_FORMAT;
            EMIT_MOVE(a[0], a[1]);
            break;
        case QUANTAPDF_PDF_OP_LINE:
            if (op->argument_count != 2) return QUANTAPDF_ERROR_FORMAT;
            EMIT_LINE(a[0], a[1]);
            break;
        case QUANTAPDF_PDF_OP_CURVE:
        case QUANTAPDF_PDF_OP_CURVE_V:
        case QUANTAPDF_PDF_OP_CURVE_Y: {
            quantapdf_point p1, p2, p3;
            if (!have_current) return QUANTAPDF_ERROR_FORMAT;
            if (op->opcode == QUANTAPDF_PDF_OP_CURVE) {
                if (op->argument_count != 6) return QUANTAPDF_ERROR_FORMAT;
                status = point(a[0], a[1], height, &p1);
                if (status != QUANTAPDF_OK) return status;
                status = point(a[2], a[3], height, &p2);
                if (status != QUANTAPDF_OK) return status;
                status = point(a[4], a[5], height, &p3);
            } else if (op->opcode == QUANTAPDF_PDF_OP_CURVE_V) {
                if (op->argument_count != 4) return QUANTAPDF_ERROR_FORMAT;
                p1 = current;
                status = point(a[0], a[1], height, &p2);
                if (status != QUANTAPDF_OK) return status;
                status = point(a[2], a[3], height, &p3);
            } else {
                if (op->argument_count != 4) return QUANTAPDF_ERROR_FORMAT;
                status = point(a[0], a[1], height, &p1);
                if (status != QUANTAPDF_OK) return status;
                status = point(a[2], a[3], height, &p3);
                p2 = p3;
            }
            if (status != QUANTAPDF_OK) return status;
            item->kind = QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
            item->point1 = p1; item->point2 = p2; item->point3 = p3;
            current = p3; ++count;
            break;
        }
        case QUANTAPDF_PDF_OP_CLOSE:
            if (!have_current || op->argument_count) return QUANTAPDF_ERROR_FORMAT;
            item->kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
            ++count; current = start; break;
        case QUANTAPDF_PDF_OP_RECT: {
            double x = a[0], y = a[1], w = a[2], h = a[3];
            if (op->argument_count != 4 || !isfinite(x + w) || !isfinite(y + h))
                return QUANTAPDF_ERROR_FORMAT;
            EMIT_MOVE(x, y);
            EMIT_LINE(x + w, y);
            EMIT_LINE(x + w, y + h);
            EMIT_LINE(x, y + h);
            commands[count++].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
            current = start;
            break;
        }
        default: return QUANTAPDF_ERROR_FORMAT;
        }
    }
    if (closing_paint(path->paint_opcode) &&
        content->ops[path->terminal_operator - 1].opcode != QUANTAPDF_PDF_OP_CLOSE) {
        commands[count++].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    }
#undef EMIT_MOVE
#undef EMIT_LINE
    return count == expected ? QUANTAPDF_OK : QUANTAPDF_ERROR_BACKEND;
}

quantapdf_status quantapdf_pdf_path_lower_to_composer(
    const quantapdf_pdf_page_semantics *page,
    double canvas_width, double canvas_height,
    const quantapdf_pdf_path_lower_limits *limits, quantapdf_ir *out_ir)
{
    quantapdf_ir temporary = {0};
    const quantapdf_pdf_content_ir *content;
    const quantapdf_pdf_graphics_ir *graphics;
    quantapdf_status status = QUANTAPDF_OK;
    size_t command_total = 0;
    if (out_ir != NULL) memset(out_ir, 0, sizeof(*out_ir));
    if (out_ir == NULL || page == NULL || page->content == NULL ||
        page->graphics == NULL || limits == NULL ||
        limits->max_painted_paths == 0 || limits->max_path_commands == 0 ||
        !isfinite(canvas_width) || !isfinite(canvas_height) ||
        canvas_width <= 0.0 || canvas_height <= 0.0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (!decimal4(canvas_width, (double)(float)canvas_width) ||
        !decimal4(canvas_height, (double)(float)canvas_height))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    content = page->content;
    graphics = page->graphics;
    if ((content->count != 0 && content->ops == NULL) ||
        (graphics->path_count != 0 && graphics->paths == NULL) ||
        (graphics->clip_count != 0 && graphics->clips == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (graphics->clip_count != 0) return QUANTAPDF_ERROR_UNSUPPORTED;
    if (graphics->path_count > limits->max_painted_paths ||
        graphics->path_count > SIZE_MAX / sizeof(*temporary.operations))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    temporary.pages = (quantapdf_composer_page_state *)calloc(1, sizeof(*temporary.pages));
    if (temporary.pages == NULL) return QUANTAPDF_ERROR_NOMEM;
    temporary.page_count = 1;
    temporary.pages[0].width_points = (float)canvas_width;
    temporary.pages[0].height_points = (float)canvas_height;
    /* A PDF page has no implicit opaque white background paint. */
    temporary.pages[0].suppress_background = 1;
    if (graphics->path_count) {
        temporary.operations = (quantapdf_composer_operation *)calloc(
            graphics->path_count, sizeof(*temporary.operations));
        if (temporary.operations == NULL) { status = QUANTAPDF_ERROR_NOMEM; goto fail; }
    }
    for (size_t i = 0; i < graphics->path_count; ++i) {
        const quantapdf_pdf_path_record *path = &graphics->paths[i];
        quantapdf_composer_operation *op = &temporary.operations[i];
        size_t commands;
        if (path->graphics.clip_id != 0 || path->clip_after != 0) {
            status = QUANTAPDF_ERROR_UNSUPPORTED; goto fail;
        }
        status = count_commands(content, path,
            limits->max_path_commands - command_total, &commands);
        if (status != QUANTAPDF_OK) goto fail;
        op->kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        op->page_index = 0;
        op->bounds = (quantapdf_rect){0, 0, (float)canvas_width, (float)canvas_height};
        status = paint_options(path, (float)canvas_height, &op->value.path.options);
        if (status != QUANTAPDF_OK) goto fail;
        op->value.path.commands = (quantapdf_composer_path_command *)calloc(
            commands, sizeof(*op->value.path.commands));
        if (op->value.path.commands == NULL) { status = QUANTAPDF_ERROR_NOMEM; goto fail; }
        op->value.path.command_count = commands;
        ++temporary.operation_count;
        status = commands_from_path(content, path, (float)canvas_height,
            op->value.path.commands, commands);
        if (status != QUANTAPDF_OK) goto fail;
        command_total += commands;
    }
    *out_ir = temporary;
    return QUANTAPDF_OK;
fail:
    quantapdf_ir_dispose(&temporary);
    return status;
}
