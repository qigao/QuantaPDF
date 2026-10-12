#include "render/ir_clip_dag_v3.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct qp_dag_admission {
    const quantapdf_ir *ir;
    const quantapdf_composer_clip_state *
        leaves[QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES];
    unsigned char marks[QUANTAPDF_NATIVE_CLIP_DAG_MAX_NODES];
    size_t leaf_count;
    size_t seen_count;
    int nested;
} qp_dag_admission;

static int matrix_is_identity_or_zero(const quantapdf_affine_transform *t)
{
    int empty = t->a == 0.0f && t->b == 0.0f &&
                t->c == 0.0f && t->d == 0.0f &&
                t->e == 0.0f && t->f == 0.0f;
    int identity = t->a == 1.0f && t->b == 0.0f &&
                   t->c == 0.0f && t->d == 1.0f &&
                   t->e == 0.0f && t->f == 0.0f;
    return empty || identity;
}

static int matrix_is_nonsingular(const quantapdf_affine_transform *t)
{
    double a = t->a, b = t->b, c = t->c, d = t->d, e = t->e, f = t->f;
    double det;
    if (!isfinite(a) || !isfinite(b) || !isfinite(c) ||
        !isfinite(d) || !isfinite(e) || !isfinite(f) ||
        fabs(a) > 1000000.0 || fabs(b) > 1000000.0 ||
        fabs(c) > 1000000.0 || fabs(d) > 1000000.0 ||
        fabs(e) > 1000000.0 || fabs(f) > 1000000.0)
        return 0;
    det = a * d - b * c;
    return isfinite(det) && det != 0.0;
}

/* Tri-color DFS: repeated leaves/shared subgraphs are allowed, active
 * back-edges (cycles), extra nodes, too much depth or non-path leaves fail.
 * Every recorded leaf is unique and retains the borrowed source pointer. */
static int visit(qp_dag_admission *a, size_t id, size_t depth)
{
    const quantapdf_composer_clip_state *node;
    size_t i, j;
    if (id == 0u || id > a->ir->resources.clip_count ||
        depth > QUANTAPDF_NATIVE_CLIP_DAG_MAX_DEPTH)
        return 0;
    --id;
    if (a->marks[id] == 1u)
        return 0;
    if (a->marks[id] == 2u)
        return 1;
    a->marks[id] = 1u;
    node = &a->ir->resources.clips[id];
    if (node->kind == QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL) {
        if (node->commands == NULL || node->command_count == 0u ||
            node->members != NULL || node->member_count != 0u ||
            !matrix_is_nonsingular(&node->transform) ||
            a->leaf_count >= QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES)
            return 0;
        a->leaves[a->leaf_count++] = node;
    } else if (node->kind == QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL) {
        if (node->commands != NULL || node->command_count != 0u ||
            node->members == NULL || node->member_count < 2u ||
            node->member_count > QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES ||
            !matrix_is_identity_or_zero(&node->transform))
            return 0;
        if (depth > 1u)
            a->nested = 1;
        for (i = 0u; i < node->member_count; ++i) {
            for (j = 0u; j < i; ++j) {
                if (node->members[j] == node->members[i])
                    return 0;
            }
            if (!visit(a, (size_t)node->members[i], depth + 1u))
                return 0;
        }
    } else {
        return 0;
    }
    a->marks[id] = 2u;
    ++a->seen_count;
    return 1;
}

quantapdf_status quantapdf_ir_raster_clip_dag_v3(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    qp_dag_admission admission = {0};
    const quantapdf_composer_operation *original;
    const quantapdf_composer_graphics_state *state;
    quantapdf_composer_operation projected_op;
    quantapdf_ir projected;
    size_t root;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        ir->operations == NULL || page_index >= ir->page_count ||
        ir->page_count != 1u || page_index != 0u ||
        ir->operation_count != 1u || ir->operations[0].page_index != 0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (ir->resources.graphics_state_count != 1u ||
        ir->resources.graphics_states == NULL ||
        ir->resources.clip_count < 3u ||
        ir->resources.clip_count > QUANTAPDF_NATIVE_CLIP_DAG_MAX_NODES ||
        ir->resources.clips == NULL ||
        ir->resources.image_count != 0u ||
        ir->resources.font_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.paint_count != 0u ||
        ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    original = &ir->operations[0];
    if (original->kind != QUANTAPDF_COMPOSER_OPERATION_PATH ||
        original->graphics_state_id != 1u ||
        original->value.path.options.struct_size <
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE ||
        original->value.path.options.graphics_state_id != 1u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    state = &ir->resources.graphics_states[0];
    if (state->blend_mode != QUANTAPDF_COMPOSER_BLEND_NORMAL ||
        state->fill_alpha != 1.0f || state->stroke_alpha != 1.0f ||
        state->soft_mask_id != 0u ||
        state->clip_id == 0u ||
        state->clip_id > ir->resources.clip_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    root = (size_t)state->clip_id;
    if (ir->resources.clips[root - 1u].kind !=
        QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    admission.ir = ir;
    if (!visit(&admission, root, 1u) ||
        admission.seen_count != ir->resources.clip_count ||
        admission.leaf_count < 2u ||
        !admission.nested)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Flatten all unique Clip leaves under one combined geometric work
     * budget; evaluate AND of their winding at each sample in the native V1
     * renderer. No raster-mask multiplication or PDF writer is involved. */
    projected = *ir;
    projected_op = *original;
    projected_op.graphics_state_id = 0u;
    projected_op.value.path.options.graphics_state_id = 0u;
    projected.operations = &projected_op;
    memset(&projected.resources, 0, sizeof(projected.resources));
    projected.links = NULL;
    projected.outlines = NULL;
    return quantapdf_ir_raster_render_v1_multi_clipped(
        &projected, 0u, limits, admission.leaves, admission.leaf_count,
        out_image);
}
