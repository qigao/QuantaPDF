#include "render/ir_clip_intersection_v1.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct native_rectangle {
    size_t left, top, right, bottom;
} native_rectangle;

static int integer_coordinate(float value, size_t bound, size_t *out)
{
    size_t i;
    if (!isfinite(value) || value < 0.0f || (double)value > (double)bound)
        return 0;
    i = (size_t)value;
    if ((double)i != (double)value)
        return 0;
    *out = i;
    return 1;
}

static int identity_or_empty(const quantapdf_affine_transform *t)
{
    int zero = t->a == 0.0f && t->b == 0.0f &&
               t->c == 0.0f && t->d == 0.0f &&
               t->e == 0.0f && t->f == 0.0f;
    int identity = t->a == 1.0f && t->b == 0.0f &&
                   t->c == 0.0f && t->d == 1.0f &&
                   t->e == 0.0f && t->f == 0.0f;
    return zero || identity;
}

static int exact_identity(const quantapdf_affine_transform *t)
{
    return t->a == 1.0f && t->b == 0.0f &&
           t->c == 0.0f && t->d == 1.0f &&
           t->e == 0.0f && t->f == 0.0f;
}

static int read_leaf(const quantapdf_composer_clip_state *clip,
                     size_t width, size_t height, native_rectangle *out)
{
    size_t x[4], y[4], i;
    native_rectangle rect;
    unsigned corners = 0u;
    if (clip->kind != QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
        clip->commands == NULL || clip->command_count != 5u ||
        clip->members != NULL || clip->member_count != 0u ||
        !exact_identity(&clip->transform) ||
        (clip->fill_rule != QUANTAPDF_COMPOSER_FILL_NONZERO &&
         clip->fill_rule != QUANTAPDF_COMPOSER_FILL_EVEN_ODD) ||
        clip->commands[0].kind != QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
        clip->commands[1].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        clip->commands[2].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        clip->commands[3].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        clip->commands[4].kind != QUANTAPDF_COMPOSER_PATH_CLOSE)
        return 0;
    for (i = 0u; i < 4u; ++i) {
        if (!integer_coordinate(clip->commands[i].point1.x, width, &x[i]) ||
            !integer_coordinate(clip->commands[i].point1.y, height, &y[i]))
            return 0;
    }
    rect.left = rect.right = x[0];
    rect.top = rect.bottom = y[0];
    for (i = 1u; i < 4u; ++i) {
        if (x[i] < rect.left) rect.left = x[i];
        if (x[i] > rect.right) rect.right = x[i];
        if (y[i] < rect.top) rect.top = y[i];
        if (y[i] > rect.bottom) rect.bottom = y[i];
    }
    if (rect.left == rect.right || rect.top == rect.bottom)
        return 0;
    for (i = 0u; i < 4u; ++i) {
        size_t next = (i + 1u) % 4u;
        unsigned bit;
        if ((x[i] == x[next]) == (y[i] == y[next]) ||
            (x[i] != rect.left && x[i] != rect.right) ||
            (y[i] != rect.top && y[i] != rect.bottom))
            return 0;
        bit = 1u << ((x[i] == rect.right ? 1u : 0u) +
                     (y[i] == rect.bottom ? 2u : 0u));
        if ((corners & bit) != 0u)
            return 0;
        corners |= bit;
    }
    if (corners != 15u)
        return 0;
    *out = rect;
    return 1;
}

static void set_command(quantapdf_composer_path_command *cmd,
                        quantapdf_composer_path_command_kind kind,
                        float x, float y)
{
    memset(cmd, 0, sizeof(*cmd));
    cmd->kind = kind;
    cmd->point1.x = x;
    cmd->point1.y = y;
}

quantapdf_status quantapdf_ir_raster_clip_intersection_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_ir synthetic;
    quantapdf_composer_graphics_state state;
    quantapdf_composer_clip_state leaf = {0};
    quantapdf_composer_path_command commands[5];
    const quantapdf_composer_clip_state *root;
    native_rectangle result = {0};
    size_t i, root_index, width, height;
    unsigned seen;
    int have_rectangle = 0;

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
        ir->resources.clips == NULL ||
        ir->operations[0].kind != QUANTAPDF_COMPOSER_OPERATION_PATH ||
        ir->operations[0].graphics_state_id != 1u ||
        ir->operations[0].value.path.options.struct_size <
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE ||
        ir->operations[0].value.path.options.graphics_state_id != 1u ||
        ir->resources.image_count != 0u || ir->resources.font_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.paint_count != 0u || ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    state = ir->resources.graphics_states[0];
    if (state.clip_id == 0u ||
        state.clip_id > ir->resources.clip_count ||
        state.soft_mask_id != 0u ||
        state.blend_mode != QUANTAPDF_COMPOSER_BLEND_NORMAL ||
        state.fill_alpha != 1.0f || state.stroke_alpha != 1.0f)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    root_index = (size_t)state.clip_id - 1u;
    root = &ir->resources.clips[root_index];
    if (root->kind != QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
        root->commands != NULL || root->command_count != 0u ||
        root->members == NULL ||
        root->member_count < 2u ||
        root->member_count > QUANTAPDF_IR_CLIP_INTERSECTION_V1_MAX_MEMBERS ||
        ir->resources.clip_count != root->member_count + 1u ||
        !identity_or_empty(&root->transform))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if (!isfinite(ir->pages[page_index].width_points) ||
        !isfinite(ir->pages[page_index].height_points) ||
        ir->pages[page_index].width_points <= 0.0f ||
        ir->pages[page_index].height_points <= 0.0f ||
        ir->pages[page_index].width_points > 1000000.0f ||
        ir->pages[page_index].height_points > 1000000.0f)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    width = (size_t)ir->pages[page_index].width_points;
    height = (size_t)ir->pages[page_index].height_points;
    if (width == 0u || height == 0u ||
        (double)width != (double)ir->pages[page_index].width_points ||
        (double)height != (double)ir->pages[page_index].height_points)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    seen = 1u << (unsigned)root_index;
    for (i = 0u; i < root->member_count; ++i) {
        size_t index;
        native_rectangle rect;
        quantapdf_composer_clip_id id = root->members[i];
        if (id == 0u || id > ir->resources.clip_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        index = (size_t)id - 1u;
        if ((seen & (1u << (unsigned)index)) != 0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        seen |= 1u << (unsigned)index;
        if (!read_leaf(&ir->resources.clips[index], width, height, &rect))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (!have_rectangle) {
            result = rect;
            have_rectangle = 1;
        } else {
            if (rect.left > result.left) result.left = rect.left;
            if (rect.top > result.top) result.top = rect.top;
            if (rect.right < result.right) result.right = rect.right;
            if (rect.bottom < result.bottom) result.bottom = rect.bottom;
        }
    }
    /* Every declared clip must belong to this exactly-one-level graph. */
    if (seen != (1u << (unsigned)ir->resources.clip_count) - 1u ||
        result.left >= result.right || result.top >= result.bottom)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Use a local canonical leaf + local graphics state. No IR pointers are
     * retained or modified. V0 performs the final path/budget preflight and
     * publishes pixels only after successfully rendering the selected path. */
    set_command(&commands[0], QUANTAPDF_COMPOSER_PATH_MOVE_TO,
                (float)result.left, (float)result.top);
    set_command(&commands[1], QUANTAPDF_COMPOSER_PATH_LINE_TO,
                (float)result.right, (float)result.top);
    set_command(&commands[2], QUANTAPDF_COMPOSER_PATH_LINE_TO,
                (float)result.right, (float)result.bottom);
    set_command(&commands[3], QUANTAPDF_COMPOSER_PATH_LINE_TO,
                (float)result.left, (float)result.bottom);
    set_command(&commands[4], QUANTAPDF_COMPOSER_PATH_CLOSE, 0.0f, 0.0f);
    leaf.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    leaf.commands = commands;
    leaf.command_count = 5u;
    leaf.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    leaf.transform = (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    state.clip_id = 1u;
    synthetic = *ir;
    synthetic.resources.clips = &leaf;
    synthetic.resources.clip_count = 1u;
    synthetic.resources.graphics_states = &state;
    return quantapdf_ir_raster_clip_rect_v0(
        &synthetic, page_index, limits, out_image);
}
