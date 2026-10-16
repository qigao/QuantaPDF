#include "ir_visible_page_device_preview_v0.h"
#include "graph/graph_page_device_sample.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* Each selected source has exactly one 4-edge closed rectangle. Nothing
 * here authorizes the unbounded cubic tessellation or generic stroke path. */
enum { QP_VISIBLE_RECT_COMMANDS = 5, QP_VISIBLE_TOTAL_EDGES = 8 };

static int default_visible_transform(
    const quantapdf_affine_transform *t,
    const quantapdf_graph_page_display *display)
{
    const double *p=display->from_source;
    const double a=p[0], b=-p[1], c=-p[2], d=p[3];
    const double e=p[4], f=display->height_points-p[5];
    if (!isfinite(a) || !isfinite(b) || !isfinite(c) ||
        !isfinite(d) || !isfinite(e) || !isfinite(f))
        return 0;
    /* The original Graph->Composer mapped lowering produces exactly these
     * float coefficients for an identity content CTM. It never stores the
     * raw PDF source coordinates directly as a device transform. */
    return t->a==(float)a && t->b==(float)b &&
           t->c==(float)c && t->d==(float)d &&
           t->e==(float)e && t->f==(float)f;
}

static int valid_rectangle_commands(
    const quantapdf_composer_path_command *commands, size_t count)
{
    if (commands==NULL || count != QP_VISIBLE_RECT_COMMANDS ||
        commands[0].kind != QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
        commands[1].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        commands[2].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        commands[3].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
        commands[4].kind != QUANTAPDF_COMPOSER_PATH_CLOSE)
        return 0;
    for (size_t i=0u;i<4u;++i)
        if (!isfinite(commands[i].point1.x) ||
            !isfinite(commands[i].point1.y))
            return 0;
    return 1;
}

/* Independently bounded float narrowing. Check all four command points
 * against their double-precision projection, not only each coefficient:
 * a coefficient's tiny rounding can otherwise accumulate across a path. */
static int project_one_transform(
    const quantapdf_affine_transform *original,
    const quantapdf_composer_path_command *commands,
    double sx, double sy, uint32_t width, uint32_t height,
    quantapdf_affine_transform *out)
{
    quantapdf_affine_transform v;
    double linear[6] = {
        sx*(double)original->a, sy*(double)original->b,
        sx*(double)original->c, sy*(double)original->d,
        sx*(double)original->e, sy*(double)original->f
    };
    float stored[6];
    for (size_t i=0u;i<6u;++i) {
        if (!isfinite(linear[i]) || fabs(linear[i])>65536.0)
            return 0;
        stored[i]=(float)linear[i];
        if (!isfinite(stored[i]))
            return 0;
    }
    v.a=stored[0];v.b=stored[1];v.c=stored[2];
    v.d=stored[3];v.e=stored[4];v.f=stored[5];

    for (size_t i=0u;i<4u;++i) {
        double x=(double)commands[i].point1.x;
        double y=(double)commands[i].point1.y;
        double ex=linear[0]*x+linear[2]*y+linear[4];
        double ey=linear[1]*x+linear[3]*y+linear[5];
        double fx=(double)v.a*x+(double)v.c*y+(double)v.e;
        double fy=(double)v.b*x+(double)v.d*y+(double)v.f;
        if (!isfinite(ex) || !isfinite(ey) ||
            !isfinite(fx) || !isfinite(fy) ||
            fabs(ex-fx)>1.0/4096.0 || fabs(ey-fy)>1.0/4096.0 ||
            ex<0.0 || ex>(double)width ||
            ey<0.0 || ey>(double)height)
            return 0;
    }
    *out=v;
    return 1;
}

/* Single unchanged Path/W Clip admission and raster pipeline. The new
 * opt-in supplies its Graph-owner-qualified value grid; the old API still
 * constructs the historical floating grid at this exact call site. */
static quantapdf_status visible_page_fillclip_preview_impl(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_graph_page_geometry *source_geometry,
    const quantapdf_graph_device_grid *provided_grid,
    unsigned int dpi, const quantapdf_ir_raster_v1_limits *limits,
    size_t max_rgba_bytes, quantapdf_ir_raster_image *out_image)
{
    quantapdf_graph_device_grid grid;
    quantapdf_graph_page_display display;
    const quantapdf_composer_operation *op;
    const quantapdf_composer_clip_state *clip;
    const quantapdf_composer_graphics_state *state;
    quantapdf_composer_operation projected_op;
    quantapdf_composer_clip_state projected_clip;
    quantapdf_composer_page_state projected_page;
    quantapdf_ir projected;
    double sx,sy,dx,dy;
    size_t pixels;
    quantapdf_status status;

    if (out_image != NULL)
        memset(out_image,0,sizeof(*out_image));
    if (out_image==NULL || ir==NULL || source_geometry==NULL ||
        limits==NULL || max_rgba_bytes==0u ||
        limits->max_pixels==0u || limits->max_operations==0u ||
        limits->max_edges==0u || limits->max_sample_tests==0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (page_index!=0u || ir->page_count!=1u || ir->pages==NULL ||
        ir->operation_count!=1u || ir->operations==NULL ||
        ir->resources.graphics_state_count!=1u ||
        ir->resources.graphics_states==NULL ||
        ir->resources.clip_count!=1u || ir->resources.clips==NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if (dpi!=72u && dpi!=96u && dpi!=144u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (provided_grid==NULL) {
        /* Historical entrypoint: do not change its rounding/admission. */
        status=quantapdf_graph_page_device_grid(source_geometry,dpi,
            limits->max_pixels,max_rgba_bytes,&grid);
        if (status!=QUANTAPDF_OK) return status;
    } else {
        /* The caller already constructed the exact integer grid while the
         * sealed Graph owner existed. Only copied metadata survives here.
         * Never round display width again or open a PDFium fallback. */
        grid=*provided_grid;
    }
    status=quantapdf_graph_page_display_map(source_geometry,&display);
    if (status!=QUANTAPDF_OK)
        return status;

    /* A fabricated/corrupted value may not evade preallocation checks.
     * Check pixel product before RGB multiplication on 32/64-bit size_t. */
    if (grid.width==0u || grid.height==0u ||
        (size_t)grid.width>SIZE_MAX/(size_t)grid.height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels=(size_t)grid.width*(size_t)grid.height;
    if (pixels>SIZE_MAX/3u ||
        grid.stride_bytes!=(size_t)grid.width*3u ||
        grid.output_bytes!=pixels*3u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (provided_grid!=NULL) {
        const double sx_expected=(double)grid.width/display.width_points;
        const double sy_expected=(double)grid.height/display.height_points;
        const double expected[6]={
            sx_expected*display.from_source[0],
            -sy_expected*display.from_source[1],
            sx_expected*display.from_source[2],
            -sy_expected*display.from_source[3],
            sx_expected*display.from_source[4],
            sy_expected*(display.height_points-display.from_source[5])
        };
        if (!isfinite(sx_expected) || !isfinite(sy_expected) ||
            !(sx_expected>0.0) || !(sy_expected>0.0))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        for (size_t k=0u;k<6u;++k)
            if (!isfinite(expected[k]) ||
                memcmp(&expected[k],&grid.from_source[k],
                       sizeof(double))!=0)
                return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (pixels>max_rgba_bytes/4u ||
        pixels>limits->max_pixels ||
        (double)(float)grid.width!=(double)grid.width ||
        (double)(float)grid.height!=(double)grid.height ||
        quantapdf_graph_device_grid_admit_q4_work(
            &grid,QP_VISIBLE_TOTAL_EDGES,limits->max_edges,
            limits->max_sample_tests)!=QUANTAPDF_OK)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    sx=(double)grid.width/display.width_points;
    sy=(double)grid.height/display.height_points;
    dx=fabs((double)ir->pages[0].width_points-display.width_points)*sx;
    dy=fabs((double)ir->pages[0].height_points-display.height_points)*sy;
    if (!isfinite(dx) || !isfinite(dy) ||
        dx>1.0/4096.0 || dy>1.0/4096.0 ||
        ir->pages[0].suppress_background!=1)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    op=&ir->operations[0];
    clip=&ir->resources.clips[0];
    state=&ir->resources.graphics_states[0];
    if (op->page_index!=0u ||
        op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
        op->graphics_state_id!=1u ||
        op->value.path.options.graphics_state_id!=1u ||
        op->value.path.options.struct_size<
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE ||
        op->value.path.options.fill!=1 ||
        op->value.path.options.stroke!=0 ||
        op->value.path.options.fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO ||
        op->value.path.dash_count!=0u ||
        !valid_rectangle_commands(op->value.path.commands,
            op->value.path.command_count) ||
        clip->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
        clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO ||
        !valid_rectangle_commands(clip->commands,clip->command_count) ||
        state->clip_id!=1u ||
        state->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
        state->fill_alpha!=1.0f || state->stroke_alpha!=1.0f ||
        state->soft_mask_id!=0u ||
        !default_visible_transform(&op->value.path.options.transform,&display) ||
        !default_visible_transform(&clip->transform,&display))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    projected_op=*op;
    projected_clip=*clip;
    projected_page=ir->pages[0];
    projected=*ir; /* Borrow every owned command/resource; never mutate. */
    if (!project_one_transform(&op->value.path.options.transform,
            op->value.path.commands,sx,sy,grid.width,grid.height,
            &projected_op.value.path.options.transform) ||
        !project_one_transform(&clip->transform,
            clip->commands,sx,sy,grid.width,grid.height,
            &projected_clip.transform))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    projected_page.width_points=(float)grid.width;
    projected_page.height_points=(float)grid.height;
    projected.pages=&projected_page;
    projected.operations=&projected_op;
    projected.resources.clips=&projected_clip;

    /* Renderer enforces the rest of IR resource/admission semantics.
     * It publishes only a complete malloc-owned output on success. */
    status=quantapdf_ir_raster_scoped_operations_v1(
        &projected,0u,limits,out_image);
    if (status==QUANTAPDF_OK &&
        (out_image->pixels==NULL ||
         out_image->width!=(size_t)grid.width ||
         out_image->height!=(size_t)grid.height ||
         out_image->stride!=(size_t)grid.width*4u ||
         out_image->size!=pixels*4u)) {
        quantapdf_ir_raster_image_dispose(out_image);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return status;
}

/* Exact-decimal opt-in: no caller switches to this automatically.
 * The user-owned grid must come from private Graph-owner-time admission.
 * Strict Path+W Clip policy and actual C11 renderer remain shared. */
quantapdf_status quantapdf_ir_raster_visible_page_fillclip_exact_grid_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_graph_page_geometry *source_geometry,
    const quantapdf_graph_device_grid *exact_grid, unsigned int dpi,
    const quantapdf_ir_raster_v1_limits *limits,
    size_t max_rgba_bytes, quantapdf_ir_raster_image *out_image)
{
    if (exact_grid==NULL) {
        if (out_image!=NULL) memset(out_image,0,sizeof(*out_image));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return visible_page_fillclip_preview_impl(ir,page_index,
        source_geometry,exact_grid,dpi,limits,max_rgba_bytes,out_image);
}

/* Historical entrypoint preserved, including old float strict-ceil behavior.
 * It deliberately does NOT opt into the new exact private Grid. */
quantapdf_status quantapdf_ir_raster_visible_page_fillclip_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_graph_page_geometry *source_geometry,
    unsigned int dpi, const quantapdf_ir_raster_v1_limits *limits,
    size_t max_rgba_bytes, quantapdf_ir_raster_image *out_image)
{
    return visible_page_fillclip_preview_impl(ir,page_index,
        source_geometry,NULL,dpi,limits,max_rgba_bytes,out_image);
}
