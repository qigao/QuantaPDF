#include "render/ir_clip_path_v2.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int clip_matrix_valid(const quantapdf_affine_transform *t)
{
    double a = (double)t->a, b = (double)t->b;
    double c = (double)t->c, d = (double)t->d;
    double e = (double)t->e, f = (double)t->f;
    double determinant;
    if (!isfinite(a) || !isfinite(b) || !isfinite(c) ||
        !isfinite(d) || !isfinite(e) || !isfinite(f) ||
        fabs(a) > 1000000.0 || fabs(b) > 1000000.0 ||
        fabs(c) > 1000000.0 || fabs(d) > 1000000.0 ||
        fabs(e) > 1000000.0 || fabs(f) > 1000000.0)
        return 0;
    determinant = a * d - b * c;
    /* Unlike a public Composer builder's zero transform default, committed
     * IR must contain a finite nonsingular transform. */
    return isfinite(determinant) && determinant != 0.0;
}

quantapdf_status quantapdf_ir_raster_clip_path_v2(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_graphics_state *state;
    const quantapdf_composer_clip_state *clip;
    quantapdf_ir projected = {0};
    quantapdf_composer_operation operation = {0};

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        ir->operations == NULL || page_index >= ir->page_count ||
        ir->operation_count != 1u ||
        ir->operations[0].page_index != page_index)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (ir->operations[0].kind != QUANTAPDF_COMPOSER_OPERATION_PATH ||
        ir->operations[0].graphics_state_id != 1u ||
        ir->operations[0].value.path.options.struct_size <
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE ||
        ir->operations[0].value.path.options.graphics_state_id != 1u ||
        ir->resources.graphics_state_count != 1u ||
        ir->resources.graphics_states == NULL ||
        ir->resources.clip_count != 1u ||
        ir->resources.clips == NULL ||
        ir->resources.image_count != 0u ||
        ir->resources.font_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.paint_count != 0u ||
        ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    state = &ir->resources.graphics_states[0];
    clip = &ir->resources.clips[0];
    if (state->clip_id != 1u || state->soft_mask_id != 0u ||
        state->blend_mode != QUANTAPDF_COMPOSER_BLEND_NORMAL ||
        state->fill_alpha != 1.0f || state->stroke_alpha != 1.0f ||
        clip->kind != QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
        clip->commands == NULL || clip->command_count == 0u ||
        clip->members != NULL || clip->member_count != 0u ||
        !clip_matrix_valid(&clip->transform))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Resource stripping is confined to a borrowed synchronous view.
     * The original IR remains unchanged; no references survive the call.
     * The V1 engine flattens and bills both shapes before allocating pixels,
     * then tests their winding together at EACH subpixel sample. */
    projected = *ir;
    operation = ir->operations[0];
    operation.graphics_state_id = 0u;
    operation.value.path.options.graphics_state_id = 0u;
    projected.operations = &operation;
    memset(&projected.resources, 0, sizeof(projected.resources));
    projected.links = NULL;
    projected.outlines = NULL;
    return quantapdf_ir_raster_render_v1_clipped(
        &projected, page_index, limits, clip, out_image);
}
