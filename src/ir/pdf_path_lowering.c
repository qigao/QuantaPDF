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

static quantapdf_status point(double x, double y, quantapdf_point *out)
{
    float cx, cy;
    if (!isfinite(x) || !isfinite(y)) return QUANTAPDF_ERROR_FORMAT;
    cx = (float)x;
    cy = (float)(-y);
    if (!decimal4(x, (double)cx) ||
        !decimal4(y, -(double)cy))
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

    /* Canonical, uniformly rebased source path basis (x, -y).
     * The same transform is frozen at W/W* and used at each paint.
     * Composer's Writer applies its normal page-height conversion; do not
     * embed c*H/d*H in this private float transform as well. Rebase both
     * local points and transforms together; there is no fallback basis. */
    candidate[0] = pdf[0]; candidate[1] = -pdf[1];
    candidate[2] = -pdf[2]; candidate[3] = pdf[3];
    candidate[4] = pdf[4];
    candidate[5] = (double)height - pdf[5];
    for (size_t i = 0; i < 6; ++i) {
        if (!isfinite(candidate[i]) || fabs(candidate[i]) > 65536.0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        stored[i] = (float)candidate[i];
    }
    roundtrip[0] = stored[0];
    roundtrip[1] = -(double)stored[1];
    roundtrip[2] = -(double)stored[2];
    roundtrip[3] = stored[3];
    roundtrip[4] = stored[4];
    roundtrip[5] = (double)height - stored[5];
    for (size_t i = 0; i < 6; ++i)
        if (!decimal4(pdf[i], roundtrip[i]))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    out->a = stored[0]; out->b = stored[1]; out->c = stored[2];
    out->d = stored[3]; out->e = stored[4]; out->f = stored[5];
    return QUANTAPDF_OK;
}

/* The same conjugated source CTM must be used for both drawing and clipping.
 * PDF clipping freezes the geometry under the CTM active at W/W*, while
 * later cm operators can change the CTM of subsequent painted paths. */
static quantapdf_status mapped_transform(
    const double source[6], const double page[6], float height,
    quantapdf_affine_transform *out)
{
    double mapped[6];
    mapped[0] = source[0] * page[0] + source[1] * page[2];
    mapped[1] = source[0] * page[1] + source[1] * page[3];
    mapped[2] = source[2] * page[0] + source[3] * page[2];
    mapped[3] = source[2] * page[1] + source[3] * page[3];
    mapped[4] = source[4] * page[0] + source[5] * page[2] + page[4];
    mapped[5] = source[4] * page[1] + source[5] * page[3] + page[5];
    return transform(mapped, height, out);
}

static quantapdf_status paint_options(
    const quantapdf_pdf_path_record *path, float height,
    const double page_transform[6],
    quantapdf_composer_path_options *options)
{
    quantapdf_pdf_content_opcode opcode = path->paint_opcode;
    quantapdf_status status;
    memset(options, 0, sizeof(*options));
    options->struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    options->fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    if (path->graphics.line_cap < 0 || path->graphics.line_cap > 2 ||
        path->graphics.line_join < 0 || path->graphics.line_join > 2 ||
        !isfinite(path->graphics.miter_limit) ||
        path->graphics.miter_limit < 1.0)
        return QUANTAPDF_ERROR_FORMAT;
    options->line_cap = (quantapdf_composer_line_cap)path->graphics.line_cap;
    options->line_join = (quantapdf_composer_line_join)path->graphics.line_join;
    options->miter_limit = (float)path->graphics.miter_limit;
    if (!decimal4(path->graphics.miter_limit, options->miter_limit))
        return QUANTAPDF_ERROR_UNSUPPORTED;
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
    status = mapped_transform(path->graphics.ctm, page_transform,
        height, &options->transform);
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

/* Optional stroke dash is owned by the Composer operation, independently of
 * the Content IR's detached numeric array pool. Empty d array means solid,
 * and a fill-only paint never emits a stroke dash. */
static quantapdf_status lower_dash(const quantapdf_pdf_content_ir *content,
    const quantapdf_pdf_path_record *source,
    quantapdf_composer_path_operation *operation)
{
    const quantapdf_pdf_graphics_state *graphics = &source->graphics;
    float *lengths;
    size_t n = graphics->dash_count;
    int any_positive = 0;
    if (!operation->options.stroke || n == 0) return QUANTAPDF_OK;
    if (n > QUANTAPDF_PDF_MAX_DASH_ITEMS ||
        graphics->dash_offset > content->dash_value_count ||
        n > content->dash_value_count - graphics->dash_offset ||
        content->dash_values == NULL)
        return QUANTAPDF_ERROR_FORMAT;
    if (!isfinite(graphics->dash_phase) || graphics->dash_phase < 0.0)
        return QUANTAPDF_ERROR_FORMAT;
    operation->dash_phase = (float)graphics->dash_phase;
    if (!decimal4(graphics->dash_phase, operation->dash_phase))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (n > SIZE_MAX / sizeof(float)) return QUANTAPDF_ERROR_UNSUPPORTED;
    lengths = (float *)malloc(n * sizeof(*lengths));
    if (lengths == NULL) return QUANTAPDF_ERROR_NOMEM;
    operation->dash_lengths = lengths;
    operation->dash_count = n;
    for (size_t i = 0; i < n; ++i) {
        double v = content->dash_values[graphics->dash_offset + i];
        if (!isfinite(v) || v < 0.0) return QUANTAPDF_ERROR_FORMAT;
        if (v > 0.0) any_positive = 1;
        lengths[i] = (float)v;
        if (!decimal4(v, lengths[i])) return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return any_positive ? QUANTAPDF_OK : QUANTAPDF_ERROR_FORMAT;
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
    if (!segment) {
        /* A stroked path may contain only MOVE and CLOSE geometry.
         * PDF 32000-1 §8.5.3.2 gives a closed single point a Round-cap
         * disk, and an open single MOVE no painted coverage. Do not extend
         * this admission to Fill or Clip: their degenerates have separate,
         * device-dependent semantics not qualified by native Stroke. */
        if ((path->paint_opcode != QUANTAPDF_PDF_OP_STROKE &&
             path->paint_opcode != QUANTAPDF_PDF_OP_CLOSE_STROKE) ||
            path->path_first_operator >= path->terminal_operator ||
            content->ops[path->path_first_operator].opcode !=
                QUANTAPDF_PDF_OP_MOVE)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    *out_count = count;
    return QUANTAPDF_OK;
}

static quantapdf_status commands_from_path(
    const quantapdf_pdf_content_ir *content,
    const quantapdf_pdf_path_record *path,
    quantapdf_composer_path_command *commands, size_t expected)
{
    size_t count = 0;
    quantapdf_point current = {0}, start = {0}, target = {0};
    int have_current = 0;
    quantapdf_status status;
#define EMIT_MOVE(X,Y) do { \
    status = point((X), (Y), &target); \
    if (status != QUANTAPDF_OK) return status; \
    commands[count].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO; \
    commands[count++].point1 = target; current = start = target; have_current = 1; \
} while(0)
#define EMIT_LINE(X,Y) do { \
    status = point((X), (Y), &target); \
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
                status = point(a[0], a[1], &p1);
                if (status != QUANTAPDF_OK) return status;
                status = point(a[2], a[3], &p2);
                if (status != QUANTAPDF_OK) return status;
                status = point(a[4], a[5], &p3);
            } else if (op->opcode == QUANTAPDF_PDF_OP_CURVE_V) {
                if (op->argument_count != 4) return QUANTAPDF_ERROR_FORMAT;
                p1 = current;
                status = point(a[0], a[1], &p2);
                if (status != QUANTAPDF_OK) return status;
                status = point(a[2], a[3], &p3);
            } else {
                if (op->argument_count != 4) return QUANTAPDF_ERROR_FORMAT;
                status = point(a[0], a[1], &p1);
                if (status != QUANTAPDF_OK) return status;
                status = point(a[2], a[3], &p3);
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

/* A path with W/W* has an operator between geometry and the paint/n
 * terminator. Reuse the strictly admitted geometry encoder by making a
 * local view ending at that clipping marker, then append a paint-specific
 * implicit h for s/b/b* if the source has not already closed the path.
 * Clip geometry itself uses W's own implicit subpath closing, not paint h. */
static quantapdf_status lower_commands(
    const quantapdf_pdf_content_ir *content,
    const quantapdf_pdf_path_record *path,
    const quantapdf_pdf_clip_record *clip,
    size_t available, int for_clip,
    quantapdf_composer_path_command **out_commands, size_t *out_count)
{
    quantapdf_pdf_path_record geometry = *path;
    quantapdf_composer_path_command *commands;
    size_t base_count, count;
    int add_close = 0;
    quantapdf_status status;
    *out_commands = NULL;
    *out_count = 0;
    if (path->terminal_operator >= content->count ||
        content->ops[path->terminal_operator].opcode != path->paint_opcode)
        return QUANTAPDF_ERROR_FORMAT;
    if (clip != NULL) {
        if (clip->clip_operator >= content->count ||
            clip->clip_operator <= path->path_first_operator ||
            clip->clip_operator >= path->terminal_operator ||
            clip->clip_operator + 1 != path->terminal_operator)
            return QUANTAPDF_ERROR_FORMAT;
        geometry.terminal_operator = clip->clip_operator;
        geometry.paint_opcode = content->ops[clip->clip_operator].opcode;
        if (geometry.paint_opcode != QUANTAPDF_PDF_OP_CLIP &&
            geometry.paint_opcode != QUANTAPDF_PDF_OP_CLIP_EVEN_ODD)
            return QUANTAPDF_ERROR_FORMAT;
    } else if (for_clip) return QUANTAPDF_ERROR_ARGUMENT;
    status = count_commands(content, &geometry, available, &base_count);
    if (status != QUANTAPDF_OK) return status;
    if (clip != NULL && !for_clip && closing_paint(path->paint_opcode) &&
        content->ops[geometry.terminal_operator - 1].opcode !=
            QUANTAPDF_PDF_OP_CLOSE) {
        if (base_count >= available) return QUANTAPDF_ERROR_UNSUPPORTED;
        add_close = 1;
    }
    count = base_count + (size_t)add_close;
    if (count > SIZE_MAX / sizeof(*commands))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    commands = (quantapdf_composer_path_command *)calloc(count,
        sizeof(*commands));
    if (commands == NULL) return QUANTAPDF_ERROR_NOMEM;
    status = commands_from_path(content, &geometry,
        commands, base_count);
    if (status != QUANTAPDF_OK) {
        free(commands);
        return status;
    }
    if (add_close)
        commands[base_count].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    *out_commands = commands;
    *out_count = count;
    return QUANTAPDF_OK;
}

/* Clip and opacity/blend must share one compositor graphics-state resource;
 * two separate gs calls in PDF could otherwise reset one another. */
static quantapdf_status intern_graphics_state(
    quantapdf_resource_registry *resources,
    const quantapdf_pdf_graphics_state *graphics,
    quantapdf_composer_clip_id clip,
    quantapdf_composer_graphics_state_id *out_id)
{
    quantapdf_composer_graphics_state candidate = {0};
    size_t added_bytes = 0;
    if (out_id == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    *out_id = 0;
    if (!isfinite(graphics->fill_alpha) || !isfinite(graphics->stroke_alpha) ||
        graphics->fill_alpha < 0.0 || graphics->fill_alpha > 1.0 ||
        graphics->stroke_alpha < 0.0 || graphics->stroke_alpha > 1.0)
        return QUANTAPDF_ERROR_FORMAT;
    if (graphics->blend_mode < QUANTAPDF_COMPOSER_BLEND_NORMAL ||
        graphics->blend_mode > QUANTAPDF_COMPOSER_BLEND_LIGHTEN)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (clip == 0 && graphics->fill_alpha == 1.0 &&
        graphics->stroke_alpha == 1.0 &&
        graphics->blend_mode == QUANTAPDF_COMPOSER_BLEND_NORMAL)
        return QUANTAPDF_OK;
    candidate.fill_alpha = (float)graphics->fill_alpha;
    candidate.stroke_alpha = (float)graphics->stroke_alpha;
    if (!decimal4(graphics->fill_alpha, candidate.fill_alpha) ||
        !decimal4(graphics->stroke_alpha, candidate.stroke_alpha))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    candidate.blend_mode = graphics->blend_mode;
    candidate.clip_id = clip;
    return quantapdf_resource_intern_graphics_state(resources,
        &candidate, SIZE_MAX, out_id, &added_bytes);
}

/* Every source clip record extends its parent and is given a 1-based ID.
 * Flat leaf intersections are equivalent to the persistent clip chain, but
 * fit the existing Composer Writer's CLIP_INTERSECTION representation.
 * There is no synthetic rasterization, and q/Q only changes the active ID. */
static quantapdf_status intern_clip_chain(
    quantapdf_resource_registry *resources,
    const quantapdf_pdf_graphics_ir *graphics,
    size_t new_clip_index,
    const quantapdf_composer_clip_id
        leaves[QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS],
    quantapdf_composer_clip_id *out_effective)
{
    quantapdf_composer_clip_id members[QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS];
    quantapdf_composer_clip_state candidate = {0};
    size_t n = 0, current = new_clip_index + 1, bytes, added_bytes = 0;
    while (current != 0) {
        if (current > new_clip_index + 1 ||
            n >= QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS ||
            leaves[current - 1] == 0)
            return QUANTAPDF_ERROR_FORMAT;
        members[n++] = leaves[current - 1];
        current = graphics->clips[current - 1].parent_clip_id;
        if (current > new_clip_index) return QUANTAPDF_ERROR_FORMAT;
    }
    if (n == 1) {
        *out_effective = members[0];
        return QUANTAPDF_OK;
    }
    bytes = n * sizeof(*members);
    candidate.kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    candidate.members = (quantapdf_composer_clip_id *)malloc(bytes);
    if (candidate.members == NULL) return QUANTAPDF_ERROR_NOMEM;
    memcpy(candidate.members, members, bytes);
    candidate.member_count = n;
    /* Intern consumes all owned candidate payload, even on failure. */
    return quantapdf_resource_intern_clip(resources, &candidate, SIZE_MAX,
        out_effective, &added_bytes);
}

quantapdf_status quantapdf_pdf_path_lower_to_composer_mapped(
    const quantapdf_pdf_page_semantics *page,
    double canvas_width, double canvas_height,
    const double page_transform[6],
    const quantapdf_pdf_path_lower_limits *limits, quantapdf_ir *out_ir)
{
    quantapdf_ir temporary = {0};
    const quantapdf_pdf_content_ir *content;
    const quantapdf_pdf_graphics_ir *graphics;
    quantapdf_status status = QUANTAPDF_OK;
    size_t command_total = 0, painted_count = 0, next_clip = 0;
    quantapdf_composer_clip_id
        leaves[QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS] = {0};
    quantapdf_composer_clip_id
        effective[QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS] = {0};
    if (out_ir != NULL) memset(out_ir, 0, sizeof(*out_ir));
    if (out_ir == NULL || page == NULL || page->content == NULL ||
        page->graphics == NULL || page_transform == NULL || limits == NULL ||
        limits->max_painted_paths == 0 || limits->max_path_commands == 0 ||
        !isfinite(canvas_width) || !isfinite(canvas_height) ||
        canvas_width <= 0.0 || canvas_height <= 0.0)
        return QUANTAPDF_ERROR_ARGUMENT;
    for (size_t component = 0; component < 6; ++component)
        if (!isfinite(page_transform[component])) return QUANTAPDF_ERROR_ARGUMENT;
    if (!decimal4(canvas_width, (double)(float)canvas_width) ||
        !decimal4(canvas_height, (double)(float)canvas_height))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    content = page->content;
    graphics = page->graphics;
    if ((content->count != 0 && content->ops == NULL) ||
        (graphics->path_count != 0 && graphics->paths == NULL) ||
        (graphics->clip_count != 0 && graphics->clips == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (graphics->clip_count > QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    for (size_t i = 0; i < graphics->path_count; ++i) {
        if (graphics->paths[i].paint_opcode == QUANTAPDF_PDF_OP_END_PATH)
            continue;
        if (painted_count == limits->max_painted_paths)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        ++painted_count;
    }
    if (painted_count > SIZE_MAX / sizeof(*temporary.operations))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    temporary.pages = (quantapdf_composer_page_state *)calloc(1, sizeof(*temporary.pages));
    if (temporary.pages == NULL) return QUANTAPDF_ERROR_NOMEM;
    temporary.page_count = 1;
    temporary.pages[0].width_points = (float)canvas_width;
    temporary.pages[0].height_points = (float)canvas_height;
    /* A PDF page has no implicit opaque white background paint. */
    temporary.pages[0].suppress_background = 1;
    if (painted_count) {
        temporary.operations = (quantapdf_composer_operation *)calloc(
            painted_count, sizeof(*temporary.operations));
        if (temporary.operations == NULL) { status = QUANTAPDF_ERROR_NOMEM; goto fail; }
    }
    for (size_t i = 0; i < graphics->path_count; ++i) {
        const quantapdf_pdf_path_record *path = &graphics->paths[i];
        const quantapdf_pdf_clip_record *clip = NULL;
        quantapdf_composer_clip_id prior_clip = 0;
        size_t count;
        if (path->graphics.clip_id > next_clip ||
            path->clip_after > graphics->clip_count)
        { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
        if (path->graphics.clip_id != 0) {
            prior_clip = effective[path->graphics.clip_id - 1];
            if (prior_clip == 0) { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
        }
        if (path->clip_after != path->graphics.clip_id) {
            if (next_clip >= graphics->clip_count ||
                path->clip_after != next_clip + 1)
            { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
            clip = &graphics->clips[next_clip];
            if (clip->clip_operator >= content->count ||
                clip->parent_clip_id != path->graphics.clip_id ||
                clip->path_first_operator != path->path_first_operator ||
                clip->terminal_operator != path->terminal_operator ||
                clip->even_odd != (content->ops[clip->clip_operator].opcode ==
                    QUANTAPDF_PDF_OP_CLIP_EVEN_ODD))
            { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
        }
        if (path->paint_opcode != QUANTAPDF_PDF_OP_END_PATH) {
            quantapdf_composer_operation *op =
                &temporary.operations[temporary.operation_count];
            op->kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
            op->page_index = 0;
            op->bounds = (quantapdf_rect){0, 0,
                (float)canvas_width, (float)canvas_height};
            status = paint_options(path, (float)canvas_height,
                page_transform, &op->value.path.options);
            if (status != QUANTAPDF_OK) goto fail;
            status = lower_commands(content, path, clip,
                limits->max_path_commands - command_total, 0,
                &op->value.path.commands, &op->value.path.command_count);
            if (status != QUANTAPDF_OK) goto fail;
            command_total += op->value.path.command_count;
            ++temporary.operation_count; /* No partially-owned record escapes. */
            status = lower_dash(content, path, &op->value.path);
            if (status != QUANTAPDF_OK) goto fail;
            status = intern_graphics_state(&temporary.resources,
                &path->graphics, prior_clip, &op->graphics_state_id);
            if (status != QUANTAPDF_OK) goto fail;
            op->value.path.options.graphics_state_id = op->graphics_state_id;
        } else if (clip == NULL) {
            /* n without a clipping operation is harmless to PDF but cannot
             * be represented as a painted Composer operation. */
            status = QUANTAPDF_ERROR_UNSUPPORTED; goto fail;
        }
        if (clip != NULL) {
            quantapdf_composer_clip_state candidate = {0};
            size_t added_bytes = 0;
            count = 0;
            candidate.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
            candidate.fill_rule = clip->even_odd ?
                QUANTAPDF_COMPOSER_FILL_EVEN_ODD :
                QUANTAPDF_COMPOSER_FILL_NONZERO;
            status = mapped_transform(path->graphics.ctm, page_transform,
                (float)canvas_height, &candidate.transform);
            if (status != QUANTAPDF_OK) goto fail;
            status = lower_commands(content, path, clip,
                limits->max_path_commands - command_total, 1,
                &candidate.commands, &count);
            if (status != QUANTAPDF_OK) goto fail;
            candidate.command_count = count;
            command_total += count;
            status = quantapdf_resource_intern_clip(&temporary.resources,
                &candidate, SIZE_MAX, &leaves[next_clip], &added_bytes);
            if (status != QUANTAPDF_OK) goto fail;
            status = intern_clip_chain(&temporary.resources,
                graphics, next_clip, leaves, &effective[next_clip]);
            if (status != QUANTAPDF_OK) goto fail;
            ++next_clip;
        }
    }
    if (next_clip != graphics->clip_count)
    { status = QUANTAPDF_ERROR_FORMAT; goto fail; }
    *out_ir = temporary;
    return QUANTAPDF_OK;
fail:
    quantapdf_ir_dispose(&temporary);
    return status;
}


quantapdf_status quantapdf_pdf_path_lower_to_composer(
    const quantapdf_pdf_page_semantics *page,
    double canvas_width, double canvas_height,
    const quantapdf_pdf_path_lower_limits *limits, quantapdf_ir *out_ir)
{
    static const double identity[6] = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    return quantapdf_pdf_path_lower_to_composer_mapped(page,
        canvas_width, canvas_height, identity, limits, out_ir);
}
