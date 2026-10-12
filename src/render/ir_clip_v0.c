#include "render/ir_clip_v0.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct clip_rect {
    size_t left, top, right, bottom;
} clip_rect;

static int integer_coord(float f, size_t bound, size_t *out)
{
    size_t n;
    if (!isfinite(f) || f < 0.0f || (double)f > (double)bound)
        return 0;
    n = (size_t)f;
    if ((double)n != (double)f)
        return 0;
    *out = n;
    return 1;
}

static int is_identity(quantapdf_affine_transform const *t)
{
    return t->a == 1.0f && t->b == 0.0f &&
           t->c == 0.0f && t->d == 1.0f &&
           t->e == 0.0f && t->f == 0.0f;
}

static int admit_rectangle(
    const quantapdf_composer_clip_state *clip,
    size_t width, size_t height, clip_rect *out)
{
    size_t x[4], y[4], left, right, top, bottom, i;
    unsigned corners = 0u;

    if (clip->kind != QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
        clip->commands == NULL || clip->command_count != 5u ||
        clip->member_count != 0u || clip->members != NULL ||
        !is_identity(&clip->transform) ||
        (clip->fill_rule != QUANTAPDF_COMPOSER_FILL_NONZERO &&
         clip->fill_rule != QUANTAPDF_COMPOSER_FILL_EVEN_ODD))
        return 0;
    if (clip->commands[0].kind != QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
        clip->commands[1].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        clip->commands[2].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        clip->commands[3].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        clip->commands[4].kind != QUANTAPDF_COMPOSER_PATH_CLOSE)
        return 0;

    for (i = 0u; i < 4u; ++i) {
        if (!integer_coord(clip->commands[i].point1.x, width, &x[i]) ||
            !integer_coord(clip->commands[i].point1.y, height, &y[i]))
            return 0;
    }
    left = right = x[0];
    top = bottom = y[0];
    for (i = 1u; i < 4u; ++i) {
        if (x[i] < left) left = x[i];
        if (x[i] > right) right = x[i];
        if (y[i] < top) top = y[i];
        if (y[i] > bottom) bottom = y[i];
    }
    if (left == right || top == bottom)
        return 0;
    for (i = 0u; i < 4u; ++i) {
        size_t j = (i + 1u) % 4u;
        unsigned bit;
        if ((x[i] == x[j]) == (y[i] == y[j]))
            return 0;
        if ((x[i] != left && x[i] != right) ||
            (y[i] != top && y[i] != bottom))
            return 0;
        bit = 1u << ((x[i] == right ? 1u : 0u) +
                     (y[i] == bottom ? 2u : 0u));
        if ((corners & bit) != 0u)
            return 0;
        corners |= bit;
    }
    if (corners != 15u)
        return 0;

    out->left = left;
    out->top = top;
    out->right = right;
    out->bottom = bottom;
    return 1;
}

quantapdf_status quantapdf_ir_raster_clip_rect_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_ir projected = {0};
    quantapdf_composer_page_state page;
    quantapdf_composer_operation operation;
    const quantapdf_composer_graphics_state *state;
    clip_rect rect;
    quantapdf_status status;
    size_t x, y;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        page_index >= ir->page_count || ir->operations == NULL ||
        ir->operation_count != 1u ||
        ir->operations[0].page_index != page_index)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (ir->resources.graphics_state_count != 1u ||
        ir->resources.graphics_states == NULL ||
        ir->resources.clip_count != 1u ||
        ir->resources.clips == NULL ||
        ir->resources.image_count != 0u ||
        ir->resources.font_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.paint_count != 0u ||
        ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u ||
        ir->operations[0].kind != QUANTAPDF_COMPOSER_OPERATION_PATH ||
        ir->operations[0].graphics_state_id != 1u ||
        ir->operations[0].value.path.options.struct_size <
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE ||
        ir->operations[0].value.path.options.graphics_state_id != 1u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    state = &ir->resources.graphics_states[0];
    if (state->clip_id != 1u || state->soft_mask_id != 0u ||
        state->blend_mode != QUANTAPDF_COMPOSER_BLEND_NORMAL ||
        state->fill_alpha != 1.0f || state->stroke_alpha != 1.0f)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    page = ir->pages[page_index];
    /* Pixel size and allocation limits are revalidated by Raster V1.
     * This early bounded conversion prevents undefined float->size_t casts. */
    if (!isfinite(page.width_points) || !isfinite(page.height_points) ||
        page.width_points <= 0.0f || page.height_points <= 0.0f ||
        page.width_points > 1000000.0f || page.height_points > 1000000.0f ||
        (double)(size_t)page.width_points != (double)page.width_points ||
        (double)(size_t)page.height_points != (double)page.height_points)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if (!admit_rectangle(&ir->resources.clips[0],
            (size_t)page.width_points, (size_t)page.height_points, &rect))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Project only the canonical solid path. A missing/invalid shape or a
     * budget error is rejected by V1 before a single output pixel exists. */
    operation = ir->operations[0];
    operation.graphics_state_id = 0u;
    operation.value.path.options.graphics_state_id = 0u;
    projected.pages = &page;
    projected.page_count = 1u;
    projected.operations = &operation;
    projected.operation_count = 1u;
    status = quantapdf_ir_raster_render_v1(&projected, 0u, limits, out_image);
    if (status != QUANTAPDF_OK)
        return status;

    /* Integer-aligned rectangle clip has exact per-pixel coverage. Restore
     * the untouched page background outside the selected clipping region.
     * Only one painted operation is admitted, so no preclip paint is erased. */
    for (y = 0u; y < out_image->height; ++y) {
        for (x = 0u; x < out_image->width; ++x) {
            unsigned char *p;
            if (x >= rect.left && x < rect.right &&
                y >= rect.top && y < rect.bottom)
                continue;
            p = out_image->pixels + y * out_image->stride + 4u * x;
            if (page.suppress_background) {
                p[0] = p[1] = p[2] = p[3] = 0u;
            } else {
                p[0] = (unsigned char)(page.background_argb >> 16u);
                p[1] = (unsigned char)(page.background_argb >> 8u);
                p[2] = (unsigned char)page.background_argb;
                p[3] = 255u;
            }
        }
    }
    return QUANTAPDF_OK;
}
