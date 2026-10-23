#include "render/ir_scoped_operations_v1.h"
#include "render/ir_clip_dag_v3.h"
#include "render/ir_stroke_polyline_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t scoped_component_weight(size_t count)
{
    const uint64_t cap = UINT64_C(65536);
    uint64_t value = (uint64_t)count;
    if (value > cap)
        value = cap;
    return value == 0u ? 1u : value;
}

static uint64_t scoped_operation_weight(
    const quantapdf_composer_operation *op)
{
    const quantapdf_composer_path_operation *path = &op->value.path;
    const quantapdf_composer_path_options *opt = &path->options;
    const quantapdf_composer_path_command *c = path->commands;
    if (opt->stroke == 1 && opt->fill == 0 && c != NULL &&
        ((path->command_count == 5u &&
          c[0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
          c[1].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
          c[2].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
          c[3].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
          c[4].kind == QUANTAPDF_COMPOSER_PATH_CLOSE) ||
         (path->command_count == 7u &&
          c[0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
          c[1].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
          c[2].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
          c[3].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
          c[4].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
          c[5].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
          c[6].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO))) {
        /* Rectangular Stroke expands to the outer/inner eight-edge ring. */
        return scoped_component_weight(8u);
    }
    return scoped_component_weight(path->command_count);
}

static size_t scoped_proportional_budget(
    size_t total, uint64_t weight, uint64_t total_weight)
{
    const uint64_t value = (uint64_t)total;
    const uint64_t quotient = value / total_weight;
    const uint64_t remainder = value % total_weight;
    const uint64_t share = quotient * weight +
        (remainder * weight) / total_weight;
    return share > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)share;
}

/* Straight-alpha source-over for a detached native V1 RGBA pixel.
 * Multiplications and numerator fit uint32 on every platform. */
static void over(unsigned char *dst, const unsigned char *src)
{
    unsigned sa = src[3], da = dst[3], keep = 255u - sa;
    unsigned denominator = 255u * sa + keep * da;
    size_t channel;
    if (sa == 0u)
        return;
    for (channel = 0u; channel < 3u; ++channel) {
        unsigned numerator = 255u * sa * (unsigned)src[channel] +
                             keep * da * (unsigned)dst[channel];
        dst[channel] =
            (unsigned char)((numerator + denominator / 2u) / denominator);
    }
    dst[3] = (unsigned char)((denominator + 127u) / 255u);
}

/* Source lowering interns Clip intersection prefixes as it reads W.
 * A later operation may reference only the flattened root, leaving earlier
 * intersections unreferenced. Permit only ordered suffix prefixes; every
 * other orphan, malformed node or unrelated resource still rejects. */
static int qp_shadowed_clip_prefixes_valid(const quantapdf_ir *ir,
                                           unsigned used)
{
    for(size_t i=0u;i<ir->resources.clip_count;++i) {
        const quantapdf_composer_clip_state *prefix;
        const quantapdf_affine_transform *t;
        int identity,empty,valid=0;
        if(used & (1u << (unsigned)i)) continue;
        prefix=&ir->resources.clips[i];
        if(prefix->kind!=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
           prefix->commands!=NULL || prefix->command_count!=0u ||
           prefix->members==NULL || prefix->member_count<2u ||
           prefix->member_count>=QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES)
            return 0;
        t=&prefix->transform;
        empty=t->a==0.0f && t->b==0.0f && t->c==0.0f &&
              t->d==0.0f && t->e==0.0f && t->f==0.0f;
        identity=t->a==1.0f && t->b==0.0f && t->c==0.0f &&
                 t->d==1.0f && t->e==0.0f && t->f==0.0f;
        if(!empty && !identity) return 0;
        for(size_t j=i+1u;j<ir->resources.clip_count && !valid;++j) {
            const quantapdf_composer_clip_state *root=&ir->resources.clips[j];
            size_t offset;
            if(!(used & (1u << (unsigned)j)) ||
               root->kind!=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
               root->members==NULL || root->member_count<=prefix->member_count)
                continue;
            offset=root->member_count-prefix->member_count;
            valid=1;
            for(size_t k=0u;k<prefix->member_count;++k)
                if(prefix->members[k]!=root->members[offset+k]) {
                    valid=0; break;
                }
        }
        if(!valid) return 0;
    }
    return 1;
}

static quantapdf_status scoped_operations_v1_impl(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan, int polyline_strokes,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_page_state *page;
    quantapdf_ir_raster_v1_limits per_operation;
    size_t width, height, count, bytes, i, k;
    uint64_t operation_weights[
        QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS] = {0};
    uint64_t total_weight = 0u;
    size_t operation_sample_budgets[
        QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS] = {0};
    size_t operation_edge_budgets[
        QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS] = {0};
    unsigned states_used = 0u, clips_used = 0u;
    const quantapdf_composer_clip_state *active_clips
        [QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS]
        [QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES] = {{0}};
    size_t active_counts[QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS] = {0};
    quantapdf_composer_path_operation outlines[
        QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS] = {{0}};
    unsigned char *final_pixels = NULL;
    quantapdf_status status = QUANTAPDF_ERROR_ARGUMENT;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        ir->operations == NULL || ir->page_count != 1u || page_index != 0u ||
        ir->operation_count == 0u ||
        ir->operation_count > QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS ||
        ir->operation_count > limits->max_operations ||
        limits->max_pixels == 0u || limits->max_pixels > SIZE_MAX / 4u ||
        limits->max_edges == 0u ||
        limits->max_sample_tests / ir->operation_count == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (ir->resources.graphics_state_count >
            QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS ||
        ir->resources.clip_count > QUANTAPDF_NATIVE_CLIP_DAG_MAX_NODES ||
        (ir->resources.graphics_state_count != 0u &&
         ir->resources.graphics_states == NULL) ||
        (ir->resources.clip_count != 0u &&
         ir->resources.clips == NULL) ||
        ir->resources.image_count != 0u || ir->resources.font_count != 0u ||
        ir->resources.soft_mask_count != 0u ||
        ir->resources.paint_count != 0u || ir->resources.form_count != 0u ||
        ir->link_count != 0u || ir->outline_count != 0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    page = &ir->pages[0];
    if (!isfinite(page->width_points) || !isfinite(page->height_points) ||
        page->width_points <= 0.0f || page->height_points <= 0.0f ||
        (double)page->width_points > (double)limits->max_pixels ||
        (double)page->height_points > (double)limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (plan != NULL) {
        if (!quantapdf_native_device_plan_matches_page(plan,
                page->width_points, page->height_points, limits->max_pixels))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        width = plan->width;
        height = plan->height;
    } else {
        width = (size_t)page->width_points;
        height = (size_t)page->height_points;
        if (width == 0u || height == 0u ||
            (double)width != (double)page->width_points ||
            (double)height != (double)page->height_points ||
            width > limits->max_pixels / height)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if ((page->suppress_background != 0 && page->suppress_background != 1) ||
        (!page->suppress_background && (page->background_argb >> 24u) != 255u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    count = width * height;
    bytes = count * 4u;

    /* Admit all state/resource associations before raster allocation. Each
     * operation's immutable state is scoped to that operation only. */
    for (i = 0u; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *op = &ir->operations[i];
        const quantapdf_composer_graphics_state *gs;
        const quantapdf_composer_path_options *opt;
        size_t id, clip_id;
        if (op->page_index != 0u ||
            op->kind != QUANTAPDF_COMPOSER_OPERATION_PATH)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        opt = &op->value.path.options;
        id = (size_t)op->graphics_state_id;
        if (id != 0u &&
            (opt->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE ||
             opt->graphics_state_id != op->graphics_state_id))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (id == 0u &&
            opt->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
            opt->graphics_state_id != 0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (id == 0u)
            continue;
        if (id > ir->resources.graphics_state_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        states_used |= 1u << (unsigned)(id - 1u);
        gs = &ir->resources.graphics_states[id - 1u];
        if (gs->blend_mode != QUANTAPDF_COMPOSER_BLEND_NORMAL ||
            gs->fill_alpha != 1.0f || gs->stroke_alpha != 1.0f ||
            gs->soft_mask_id != 0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        clip_id = (size_t)gs->clip_id;
        if (clip_id == 0u)
            continue;
        if (clip_id > ir->resources.clip_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        {
            unsigned visited = 0u;
            if (quantapdf_ir_clip_dag_collect_v3(
                    ir, (quantapdf_composer_clip_id)clip_id,
                    active_clips[i], &active_counts[i], &visited) != QUANTAPDF_OK)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            clips_used |= visited;
        }
    }
    if (states_used !=
            ((1u << (unsigned)ir->resources.graphics_state_count) - 1u) ||
        !qp_shadowed_clip_prefixes_valid(ir, clips_used))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Clip membership comes from each operation's immutable graphics state.
     * An earlier Stroke has no later W Clip in that state. Never infer this
     * relationship merely from the number or kinds of operations: an authored
     * Stroke inside an active Clip must retain that Clip. */

    final_pixels = (unsigned char *)malloc(bytes);
    if (final_pixels == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    for (k = 0u; k < count; ++k) {
        unsigned char *dst = final_pixels + k * 4u;
        if (page->suppress_background) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0u;
        } else {
            dst[0] = (unsigned char)(page->background_argb >> 16u);
            dst[1] = (unsigned char)(page->background_argb >> 8u);
            dst[2] = (unsigned char)page->background_argb;
            dst[3] = 255u;
        }
    }

    /* Allocate the global work and edge budgets in proportion to each
     * layer's authored path plus the clips actually rasterized with it.
     * Equal division is incorrect when one layer also evaluates a Clip:
     * that duplicates edge/sample work inside the layer. Component weights
     * are capped, so their sum and proportional arithmetic remain bounded.
     * Each per-layer share is still a partition of the original global cap. */
    for (i = 0u; i < ir->operation_count; ++i) {
        uint64_t weight = scoped_operation_weight(&ir->operations[i]);
        if (polyline_strokes && ir->operations[i].value.path.options.stroke==1 &&
            ir->operations[i].value.path.options.fill==0) {
            quantapdf_composer_path_operation projected=ir->operations[i].value.path;
            size_t edges=0u;
            projected.options.graphics_state_id=0u;
            status=plan ? quantapdf_ir_stroke_polyline_outline_v1_planned(&projected,
                limits->max_edges,plan,&outlines[i]) :
                quantapdf_ir_stroke_polyline_outline_v1(&projected,limits->max_edges,&outlines[i]);
            if(status!=QUANTAPDF_OK) goto failure;
            /* Dash expansion can dwarf its authored path. Partition from
             * actual outline edges rather than guessing from source size. */
            for(size_t ci=0u;ci<outlines[i].command_count;++ci)
                if(outlines[i].commands[ci].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE) ++edges;
            weight=scoped_component_weight(edges);
        }
        for (k = 0u; k < active_counts[i]; ++k) {
            const quantapdf_composer_clip_state *clip = active_clips[i][k];
            if (clip == NULL || clip->commands == NULL ||
                clip->command_count == 0u) {
                status = QUANTAPDF_ERROR_UNSUPPORTED;
                goto failure;
            }
            weight += scoped_component_weight(clip->command_count);
        }
        operation_weights[i] = weight;
        total_weight += weight;
    }
    if (total_weight == 0u) {
        status = QUANTAPDF_ERROR_UNSUPPORTED;
        goto failure;
    }
    for (i = 0u; i < ir->operation_count; ++i) {
        operation_sample_budgets[i] = scoped_proportional_budget(
            limits->max_sample_tests, operation_weights[i], total_weight);
        operation_edge_budgets[i] = scoped_proportional_budget(
            limits->max_edges, operation_weights[i], total_weight);
        if (operation_sample_budgets[i] == 0u ||
            operation_edge_budgets[i] == 0u) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto failure;
        }
    }

    per_operation = *limits;
    per_operation.max_operations = 1u;

    for (i = 0u; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *source = &ir->operations[i];
        quantapdf_composer_operation operation = *source;
        quantapdf_composer_page_state transparent_page = *page;
        quantapdf_ir local = {0};
        quantapdf_ir_raster_image layer = {0};
        operation.graphics_state_id = 0u;
        operation.value.path.options.graphics_state_id = 0u;
        transparent_page.suppress_background = 1;
        local.pages = &transparent_page;
        local.page_count = 1u;
        local.operations = &operation;
        local.operation_count = 1u;
        per_operation.max_sample_tests = operation_sample_budgets[i];
        per_operation.max_edges = operation_edge_budgets[i];
        if(outlines[i].commands!=NULL) operation.value.path=outlines[i];
        if (plan != NULL) {
            status = quantapdf_ir_raster_render_v1_planned(
                &local, 0u, &per_operation, plan, active_clips[i],
                active_counts[i], &layer);
        } else if (active_counts[i] == 0u) {
            status = quantapdf_ir_raster_render_v1(
                &local, 0u, &per_operation, &layer);
        } else {
            status = quantapdf_ir_raster_render_v1_multi_clipped(
                &local, 0u, &per_operation, active_clips[i],
                active_counts[i], &layer);
        }
        if (status != QUANTAPDF_OK) {
            goto failure;
        }
        if (layer.width != width || layer.height != height ||
            layer.stride != width * 4u || layer.size != bytes ||
            layer.pixels == NULL) {
            quantapdf_ir_raster_image_dispose(&layer);
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto failure;
        }
        for (k = 0u; k < count; ++k)
            over(final_pixels + 4u * k, layer.pixels + 4u * k);
        quantapdf_ir_raster_image_dispose(&layer);
    }

    for(i=0u;i<ir->operation_count;++i)
        quantapdf_ir_stroke_polyline_dispose_v1(&outlines[i]);
    out_image->pixels = final_pixels;
    out_image->width = width;
    out_image->height = height;
    out_image->stride = width * 4u;
    out_image->size = bytes;
    return QUANTAPDF_OK;

failure:
    for(i=0u;i<ir->operation_count;++i)
        quantapdf_ir_stroke_polyline_dispose_v1(&outlines[i]);
    free(final_pixels);
    return status;
}

quantapdf_status quantapdf_ir_raster_scoped_operations_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return scoped_operations_v1_impl(ir, page_index, limits, NULL, 0, out_image);
}

quantapdf_status quantapdf_ir_raster_scoped_operations_v1_planned(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    quantapdf_ir_raster_image *out_image)
{
    if (plan == NULL) {
        if (out_image != NULL)
            memset(out_image, 0, sizeof(*out_image));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return scoped_operations_v1_impl(ir, page_index, limits, plan, 0, out_image);
}

quantapdf_status quantapdf_ir_raster_scoped_operations_v2(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,quantapdf_ir_raster_image *out_image)
{
    return scoped_operations_v1_impl(ir,page_index,limits,NULL,1,out_image);
}
quantapdf_status quantapdf_ir_raster_scoped_operations_v2_planned(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,const quantapdf_native_device_plan *plan,
    quantapdf_ir_raster_image *out_image)
{
    if(!plan) {
        if(out_image) memset(out_image,0,sizeof(*out_image));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return scoped_operations_v1_impl(ir,page_index,limits,plan,1,out_image);
}
