#include "render/ir_stroke_v0.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int finite_point(quantapdf_point p)
{
    return isfinite(p.x) && isfinite(p.y) &&
           fabs((double)p.x) <= 1000000.0 &&
           fabs((double)p.y) <= 1000000.0;
}

static int identity_or_inherited(
    const quantapdf_composer_path_options *options)
{
    const quantapdf_affine_transform *t = &options->transform;
    if (options->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE)
        return 1;
    /* An explicit V4 transform must be canonical, not a zero matrix.
     * The normal Composer producer normalizes default input to identity. */
    return t->a == 1.0f && t->b == 0.0f &&
           t->c == 0.0f && t->d == 1.0f &&
           t->e == 0.0f && t->f == 0.0f;
}

static void fill_point(quantapdf_composer_path_command *cmd,
                       quantapdf_composer_path_command_kind kind,
                       double x, double y)
{
    cmd->kind = kind;
    cmd->point1.x = (float)x;
    cmd->point1.y = (float)y;
}

quantapdf_status quantapdf_ir_raster_stroke_butt_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_ir projected = {0};
    quantapdf_composer_page_state page;
    quantapdf_composer_operation operation = {0};
    quantapdf_composer_path_command commands[5] = {{0}};
    const quantapdf_composer_path_operation *path;
    const quantapdf_composer_path_options *opt;
    quantapdf_point from, to;
    double dx, dy, length, nx, ny, hw;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        page_index >= ir->page_count || ir->operations == NULL ||
        ir->operation_count != 1u ||
        ir->operations[0].page_index != page_index)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (ir->resources.image_count != 0u ||
        ir->resources.font_count != 0u ||
        ir->resources.graphics_state_count != 0u ||
        ir->resources.clip_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.paint_count != 0u ||
        ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u ||
        ir->operations[0].kind != QUANTAPDF_COMPOSER_OPERATION_PATH ||
        ir->operations[0].graphics_state_id != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    path = &ir->operations[0].value.path;
    opt = &path->options;
    if (opt->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
        opt->stroke != 1 || opt->fill != 0 ||
        (opt->stroke_argb >> 24u) != 255u ||
        !isfinite(opt->stroke_width) || opt->stroke_width <= 0.0f ||
        opt->stroke_width > 2048.0f ||
        opt->line_cap != QUANTAPDF_COMPOSER_LINE_CAP_BUTT ||
        opt->line_join != QUANTAPDF_COMPOSER_LINE_JOIN_MITER ||
        (opt->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
         opt->graphics_state_id != 0u) ||
        (opt->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE &&
         (opt->fill_paint_id != 0u || opt->stroke_paint_id != 0u)) ||
        !identity_or_inherited(opt) ||
        path->dash_count != 0u || path->dash_phase != 0.0f ||
        path->command_count != 2u || path->commands == NULL ||
        path->commands[0].kind != QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
        path->commands[1].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    from = path->commands[0].point1;
    to = path->commands[1].point1;
    if (!finite_point(from) || !finite_point(to))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    dx = (double)to.x - (double)from.x;
    dy = (double)to.y - (double)from.y;
    length = sqrt(dx * dx + dy * dy);
    if (!isfinite(length) || length <= 0.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    hw = (double)opt->stroke_width * 0.5;
    nx = -dy * hw / length;
    ny = dx * hw / length;

    fill_point(&commands[0], QUANTAPDF_COMPOSER_PATH_MOVE_TO,
               (double)from.x + nx, (double)from.y + ny);
    fill_point(&commands[1], QUANTAPDF_COMPOSER_PATH_LINE_TO,
               (double)to.x + nx, (double)to.y + ny);
    fill_point(&commands[2], QUANTAPDF_COMPOSER_PATH_LINE_TO,
               (double)to.x - nx, (double)to.y - ny);
    fill_point(&commands[3], QUANTAPDF_COMPOSER_PATH_LINE_TO,
               (double)from.x - nx, (double)from.y - ny);
    commands[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    if (!finite_point(commands[0].point1) ||
        !finite_point(commands[1].point1) ||
        !finite_point(commands[2].point1) ||
        !finite_point(commands[3].point1))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    page = ir->pages[page_index];
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    operation.value.path.commands = commands;
    operation.value.path.command_count = 5u;
    operation.value.path.options.struct_size =
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    operation.value.path.options.fill = 1;
    operation.value.path.options.fill_argb = opt->stroke_argb;
    operation.value.path.options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    operation.value.path.options.transform =
        (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    projected.pages = &page;
    projected.page_count = 1u;
    projected.operations = &operation;
    projected.operation_count = 1u;

    /* V1 preflights transformed geometry/work and owns the final allocation.
     * The temporary operation/commands never escape this synchronous call. */
    return quantapdf_ir_raster_render_v1(
        &projected, 0u, limits, out_image);
}
