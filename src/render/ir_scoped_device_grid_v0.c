#include "render/ir_scoped_device_grid_v0.h"
#include "render/ir_clip_dag_v3.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define QP_DEVICE_GRID_MAX_OPERATIONS QUANTAPDF_IR_SCOPED_OPERATIONS_V1_MAX_OPERATIONS

/* Restrict this first pixel-producing integration to already-normalized
 * display coordinates with identity Path/Clip transforms. General affine
 * transforms and stroke scaling have a separate correctness gate. */
static int qp_grid_identity_or_zero(const quantapdf_affine_transform *t)
{
    int zero = t->a == 0.0f && t->b == 0.0f && t->c == 0.0f &&
               t->d == 0.0f && t->e == 0.0f && t->f == 0.0f;
    int identity = t->a == 1.0f && t->b == 0.0f && t->c == 0.0f &&
                   t->d == 1.0f && t->e == 0.0f && t->f == 0.0f;
    return zero || identity;
}

/* Independently normalized source paths use (x,-y) plus a bounded Path or
 * W Clip translation. Affine scale/shear/rotation is still NOT admitted. */
static int qp_grid_translation_only(
    const quantapdf_affine_transform *t, double *tx, double *ty)
{
    int zero=t->a==0.0f && t->b==0.0f && t->c==0.0f &&
             t->d==0.0f && t->e==0.0f && t->f==0.0f;
    if (zero) {
        *tx=0.0; *ty=0.0;
        return 1;
    }
    if (t->a!=1.0f || t->b!=0.0f || t->c!=0.0f ||
        t->d!=1.0f || !isfinite(t->e) || !isfinite(t->f) ||
        fabs((double)t->e)>1000000.0 ||
        fabs((double)t->f)>1000000.0)
        return 0;
    *tx=(double)t->e;
    *ty=(double)t->f;
    return 1;
}

static int qp_grid_point_within_page(
    quantapdf_point p, double tx, double ty,
    const quantapdf_native_device_grid_geometry *geometry)
{
    double x,y;
    if (!isfinite(p.x) || !isfinite(p.y) ||
        fabs((double)p.x)>1000000.0 ||
        fabs((double)p.y)>1000000.0)
        return 0;
    x=(double)p.x+tx;
    y=(double)p.y+ty;
    return isfinite(x) && isfinite(y) &&
           x>=0.0 && y>=0.0 &&
           x<=geometry->page_width_points &&
           y<=geometry->page_height_points;
}

/* Command points are borrowed, never rewritten. Guard both raw/normalized
 * precision and the translation before assigning any projected matrices. */
static int qp_grid_commands_valid(
    const quantapdf_composer_path_command *commands, size_t count,
    double tx, double ty,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *limits)
{
    size_t i;
    if (commands == NULL || count == 0u ||
        count > limits->max_edges * 2u)
        return 0;
    for (i=0u;i<count;++i) {
        const quantapdf_composer_path_command *cmd=&commands[i];
        switch (cmd->kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            if (!qp_grid_point_within_page(cmd->point1,tx,ty,geometry))
                return 0;
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            if (!qp_grid_point_within_page(cmd->point1,tx,ty,geometry) ||
                !qp_grid_point_within_page(cmd->point2,tx,ty,geometry) ||
                !qp_grid_point_within_page(cmd->point3,tx,ty,geometry))
                return 0;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            break;
        default:
            return 0;
        }
    }
    return 1;
}

/* Only translation composed with the common anisotropic device grid.
 * Keep each float matrix coefficient within 1/4096 device-pixel drift. */
static int qp_grid_project_translation(
    double sx,double sy,double tx,double ty,
    quantapdf_affine_transform *out)
{
    double ex=sx*tx,fy=sy*ty;
    if (!isfinite(ex) || !isfinite(fy) ||
        fabs(ex)>1000000.0 || fabs(fy)>1000000.0 ||
        !isfinite((float)ex) || !isfinite((float)fy) ||
        fabs((double)(float)ex-ex)>(1.0/4096.0) ||
        fabs((double)(float)fy-fy)>(1.0/4096.0))
        return 0;
    *out=(quantapdf_affine_transform){
        (float)sx,0.0f,0.0f,(float)sy,(float)ex,(float)fy
    };
    return 1;
}

quantapdf_status quantapdf_ir_raster_scoped_device_grid_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_native_device_grid grid;
    quantapdf_native_device_grid_status grid_status;
    quantapdf_ir projected;
    quantapdf_composer_page_state projected_page;
    quantapdf_composer_operation
        projected_ops[QP_DEVICE_GRID_MAX_OPERATIONS];
    quantapdf_composer_clip_state
        projected_clips[QUANTAPDF_NATIVE_CLIP_DAG_MAX_NODES];
    quantapdf_ir_raster_v1_limits work;
    double sx, sy;
    size_t i;
    quantapdf_status status;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || geometry == NULL || grid_limits == NULL ||
        raster_limits == NULL || ir->pages == NULL ||
        ir->operations == NULL || ir->page_count != 1u || page_index != 0u ||
        ir->operation_count == 0u ||
        ir->operation_count > QP_DEVICE_GRID_MAX_OPERATIONS ||
        ir->resources.clip_count > QUANTAPDF_NATIVE_CLIP_DAG_MAX_NODES ||
        (ir->resources.clip_count != 0u && ir->resources.clips == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;

    grid_status = quantapdf_native_device_grid_create_v0(
        geometry, grid_limits, &grid);
    if (grid_status == QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (grid_status != QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
        quantapdf_native_device_grid_check_ir_v0(
            &grid, ir->pages[0].width_points,
            ir->pages[0].height_points) != QUANTAPDF_NATIVE_DEVICE_GRID_OK)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* The downstream V1 engine still stores pixel dimensions as float
     * page points. Never introduce an inexact integer->float conversion. */
    if ((double)(float)grid.width != (double)grid.width ||
        (double)(float)grid.height != (double)grid.height ||
        grid.pixel_count > raster_limits->max_pixels ||
        grid.sample_slots > raster_limits->max_sample_tests ||
        quantapdf_native_device_grid_admit_edges_v0(
            &grid, grid_limits, 1u) != QUANTAPDF_NATIVE_DEVICE_GRID_OK)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    sx = (double)grid.width / geometry->page_width_points;
    sy = (double)grid.height / geometry->page_height_points;
    if (!isfinite(sx) || !isfinite(sy) || sx <= 0.0 || sy <= 0.0 ||
        !isfinite((float)sx) || !isfinite((float)sy) ||
        (float)sx <= 0.0f || (float)sy <= 0.0f ||
        fabs((double)(float)sx - sx) * geometry->page_width_points >
            (1.0 / 4096.0) ||
        fabs((double)(float)sy - sy) * geometry->page_height_points >
            (1.0 / 4096.0))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* Project both Path and Clip resources into the SAME device space.
     * This synchronous borrowed projection is never retained or disposed.
     * Reject stroke early: width-one joins are not valid under nonunit,
     * generally nonisometric fractional-page scale. */
    for (i = 0u; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *original = &ir->operations[i];
        const quantapdf_composer_path_options *opt =
            &original->value.path.options;
        double tx=0.0,ty=0.0;
        if (original->page_index != 0u ||
            original->kind != QUANTAPDF_COMPOSER_OPERATION_PATH ||
            opt->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE ||
            opt->stroke != 0 || opt->fill != 1 ||
            !qp_grid_translation_only(&opt->transform,&tx,&ty) ||
            !qp_grid_commands_valid(
                original->value.path.commands,
                original->value.path.command_count,
                tx,ty,geometry,grid_limits))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        projected_ops[i] = *original;
        if (!qp_grid_project_translation(
                sx,sy,tx,ty,&projected_ops[i].value.path.options.transform))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    for (i = 0u; i < ir->resources.clip_count; ++i) {
        const quantapdf_composer_clip_state *clip =
            &ir->resources.clips[i];
        projected_clips[i] = *clip;
        if (clip->kind == QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL) {
            double tx=0.0,ty=0.0;
            /* The native DAG collector requires nonsingular leaf matrices. */
            if (!qp_grid_translation_only(&clip->transform,&tx,&ty) ||
                !qp_grid_commands_valid(
                    clip->commands, clip->command_count,
                    tx,ty,geometry,grid_limits) ||
                !qp_grid_project_translation(
                    sx,sy,tx,ty,&projected_clips[i].transform))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        } else if (clip->kind !=
                       QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
                   !qp_grid_identity_or_zero(&clip->transform)) {
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }

    projected = *ir;
    projected_page = ir->pages[0];
    projected_page.width_points = (float)grid.width;
    projected_page.height_points = (float)grid.height;
    projected.pages = &projected_page;
    projected.operations = projected_ops;
    if (ir->resources.clip_count != 0u)
        projected.resources.clips = projected_clips;
    work = *raster_limits;
    if (work.max_pixels > grid_limits->max_pixels)
        work.max_pixels = grid_limits->max_pixels;
    if (work.max_edges > grid_limits->max_edges)
        work.max_edges = grid_limits->max_edges;
    if (work.max_sample_tests > grid_limits->max_sample_tests)
        work.max_sample_tests = grid_limits->max_sample_tests;

    status = quantapdf_ir_raster_scoped_operations_v1(
        &projected, 0u, &work, out_image);
    if (status == QUANTAPDF_OK &&
        (out_image->pixels == NULL || out_image->width != grid.width ||
         out_image->height != grid.height ||
         out_image->stride != grid.stride ||
         out_image->size != grid.rgba_bytes)) {
        quantapdf_ir_raster_image_dispose(out_image);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return status;
}
