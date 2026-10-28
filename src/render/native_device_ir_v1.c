#include "render/native_device_ir_v1.h"
#include "render/native_device_plan.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define QP_DPI_MAX_OPS 4u
#define QP_DPI_MAX_PATH_COMMANDS 9u

static int qp_device_float(float source, double scale, float *out)
{
    const double value=(double)source*scale;
    if(!isfinite(source) || !isfinite(value) ||
       value< -1000000.0 || value>1000000.0)
        return 0;
    *out=(float)value;
    return isfinite(*out);
}

static int qp_device_point(quantapdf_point *point,double scale)
{
    float x,y;
    if(!qp_device_float(point->x,scale,&x) ||
       !qp_device_float(point->y,scale,&y))
        return 0;
    point->x=x;point->y=y;
    return 1;
}

static int qp_device_commands(
    quantapdf_composer_path_command *dst,
    const quantapdf_composer_path_command *src,
    size_t count,double scale)
{
    if(src==NULL || count==0u || count>QP_DPI_MAX_PATH_COMMANDS)
        return 0;
    for(size_t i=0u;i<count;++i) {
        dst[i]=src[i];
        switch(dst[i].kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            if(!qp_device_point(&dst[i].point1,scale))return 0;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            break;
        default:
            /* No unqualified CUBIC/stroke command can be translated
             * without explicitly qualified Source V10 semantics. */
            return 0;
        }
    }
    return 1;
}

static int qp_image_bounds(
    quantapdf_rect *rect,double scale,size_t width,size_t height)
{
    float x0,x1,y0,y1;
    if(!qp_device_float(rect->x0,scale,&x0) ||
       !qp_device_float(rect->x1,scale,&x1) ||
       !qp_device_float(rect->y0,scale,&y0) ||
       !qp_device_float(rect->y1,scale,&y1) ||
       x0<0.0f || y0<0.0f ||
       x1<=x0 || y1<=y0 ||
       (double)x1>(double)width || (double)y1>(double)height)
        return 0;
    /* The existing one-layer RAW Image raster requires integer
     * footprints. Refuse fractional 96-DPI image edges without
     * changing its established nearest-sample and Clip contracts. */
    if(x0!=(float)(size_t)x0 || x1!=(float)(size_t)x1 ||
       y0!=(float)(size_t)y0 || y1!=(float)(size_t)y1)
        return 0;
    rect->x0=x0;rect->y0=y0;rect->x1=x1;rect->y1=y1;
    return 1;
}

quantapdf_status quantapdf_native_device_ir_render_v1(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_native_device_plan plan={0};
    quantapdf_ir projected={0};
    quantapdf_composer_page_state page={0};
    quantapdf_composer_operation operations[QP_DPI_MAX_OPS]={{0}};
    quantapdf_composer_path_command paths[QP_DPI_MAX_OPS]
        [QP_DPI_MAX_PATH_COMMANDS]={{{0}}};
    quantapdf_composer_clip_state clip={0};
    quantapdf_composer_path_command
        clip_commands[QP_DPI_MAX_PATH_COMMANDS]={{0}};
    char box[80];
    size_t source_width,source_height;
    int printed;
    double scale;
    quantapdf_status status;

    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(source==NULL || source->pages==NULL || limits==NULL ||
       limits->max_pixels==0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if((dpi!=96u && dpi!=144u) || source->page_count!=1u ||
       source->operation_count>QP_DPI_MAX_OPS ||
       (source->operation_count!=0u && source->operations==NULL) ||
       source->resources.clip_count>1u ||
       !isfinite(source->pages[0].width_points) ||
       !isfinite(source->pages[0].height_points) ||
       source->pages[0].width_points<1.0f ||
       source->pages[0].height_points<1.0f ||
       source->pages[0].width_points>1000000.0f ||
       source->pages[0].height_points>1000000.0f)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    source_width=(size_t)source->pages[0].width_points;
    source_height=(size_t)source->pages[0].height_points;
    if((float)source_width!=source->pages[0].width_points ||
       (float)source_height!=source->pages[0].height_points)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    printed=snprintf(box,sizeof(box),"0 0 %zu %zu",
                     source_width,source_height);
    if(printed<0 || (size_t)printed>=sizeof(box))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status=quantapdf_native_device_plan_create(
        box,box,"1",0,dpi,limits->max_pixels,&plan);
    if(status!=QUANTAPDF_OK)return status;
    if(plan.width==0u || plan.height==0u ||
       plan.width>SIZE_MAX/plan.height ||
       plan.width*plan.height>limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* A shallow projected IR borrows image resources and every untouched
     * registry pointer. Stack-owned command projections cannot be freed
     * as an IR: the original Source owner alone disposes all payloads. */
    projected=*source;
    page=source->pages[0];
    page.width_points=(float)plan.width;
    page.height_points=(float)plan.height;
    projected.pages=&page;
    projected.operations=source->operation_count!=0u?operations:NULL;
    scale=(double)dpi/72.0;
    for(size_t i=0u;i<source->operation_count;++i) {
        const quantapdf_composer_operation *original=&source->operations[i];
        operations[i]=*original;
        if(original->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const quantapdf_composer_path_operation *path=&original->value.path;
            const quantapdf_affine_transform *t=&path->options.transform;
            const int zero=(t->a==0.0f && t->b==0.0f &&
                t->c==0.0f && t->d==0.0f &&
                t->e==0.0f && t->f==0.0f);
            if(!zero || !qp_device_commands(paths[i],path->commands,
                                             path->command_count,scale))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            operations[i].value.path.commands=paths[i];
        } else if(original->kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            if(!qp_image_bounds(&operations[i].bounds,scale,
                                plan.width,plan.height))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        } else {
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    if(source->resources.clip_count==1u) {
        const quantapdf_composer_clip_state *old=source->resources.clips;
        if(old==NULL ||
           old->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
           old->transform.a!=1.0f || old->transform.b!=0.0f ||
           old->transform.c!=0.0f || old->transform.d!=1.0f ||
           old->transform.e!=0.0f || old->transform.f!=0.0f ||
           !qp_device_commands(clip_commands,old->commands,
                               old->command_count,scale))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        clip=*old;
        clip.commands=clip_commands;
        projected.resources.clips=&clip;
    }
    /* Canonical preflight/raster owns all pixel allocation. PATH and
     * Clip edges are now at the target grid (including fractional 4/3
     * edges); IMAGE placement is checked to integer target bounds.
     * Never render a 72-DPI bitmap and upscale. */
    return quantapdf_ir_raster_scoped_mixed_v5(
        &projected,0u,limits,out_image);
}
