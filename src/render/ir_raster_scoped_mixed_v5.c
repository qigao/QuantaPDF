#include "render/ir_raster_scoped_mixed_v5.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_V5_MAX_LAYERS 4u
#define QP_V5_MAX_PIXELS ((size_t)1048576u)
#define QP_V5_MAX_SOURCE_PIXELS ((size_t)4194304u)
#define QP_V5_MAX_DRAW_WORK ((size_t)4194304u)

static int v5_integer(float v,size_t max,size_t *out)
{
    size_t integral;
    if(!isfinite(v) || v<0.0f || (double)v>(double)max)return 0;
    integral=(size_t)v;
    if((float)integral!=v)return 0;
    *out=integral;
    return 1;
}


/* Native device IMAGE bounds are the full authorial source placement.
 * The V10 opt-in permits a rectangle partially or fully outside the
 * CropBox viewport, and bills only its enclosing visible pixel grid;
 * the SOURCE parser already billed full XObject/placement work.
 * Same 4x4 C11 mask/Clip intersection, no new image renderer. */
static int v5_image_bounds(const quantapdf_rect *b,size_t width,size_t height,
                           int fractional_allowed,size_t *x0,size_t *x1,
                           size_t *y0,size_t *y1,int *fractional)
{
    if(!isfinite(b->x0) || !isfinite(b->x1) ||
       !isfinite(b->y0) || !isfinite(b->y1) ||
       b->x1<=b->x0 || b->y1<=b->y0)
        return 0;
    if(!fractional_allowed) {
        if(!v5_integer(b->x0,width,x0) ||
           !v5_integer(b->x1,width,x1) ||
           !v5_integer(b->y0,height,y0) ||
           !v5_integer(b->y1,height,y1) ||
           *x1<=*x0 || *y1<=*y0)
            return 0;
        *fractional=0;
    } else {
        /* Clamp only the *work/accounting* rectangle into the finite
         * output viewport. The actual unclipped original bounds remain
         * immutable in borrowed IR, consumed by the SAME 4x4 C11
         * geometry/Clip mask. A wholly outside IMAGE is valid zero ink. */
        if(b->x0< -1000000.0f || b->x1>1000000.0f ||
           b->y0< -1000000.0f || b->y1>1000000.0f)
            return 0;
        *x0=(size_t)fmin((double)width,
             fmax(0.0,floor((double)b->x0)));
        *y0=(size_t)fmin((double)height,
             fmax(0.0,floor((double)b->y0)));
        *x1=(size_t)fmin((double)width,
             fmax(0.0,ceil((double)b->x1)));
        *y1=(size_t)fmin((double)height,
             fmax(0.0,ceil((double)b->y1)));
        if(*x1<*x0)*x1=*x0;
        if(*y1<*y0)*y1=*y0;
        *fractional=
            b->x0!=(float)(*x0) || b->x1!=(float)(*x1) ||
            b->y0!=(float)(*y0) || b->y1!=(float)(*y1);
    }
    return *x1<=width && *y1<=height;
}

static void v5_point(quantapdf_composer_path_command *p,
                      quantapdf_composer_path_command_kind kind,
                      float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;
    p->point1.x=x;
    p->point1.y=y;
}

/* Fractional IMAGE paint belongs to the SAME canonical C11 V5 operation
 * compositor. Its rectangle and optional frozen W/W* Clip are sampled
 * together by the existing C11 4x4 PATH sampler: one set of 16 points,
 * NOT two independently averaged alpha masks multiplied together.
 * Source color retains RAW nearest-neighbor sampling at the clamped
 * device pixel center, including partially covered edge pixels.
 * The pixel mask becomes the detached layer; no second provider, QPDF
 * rendering, superscaled 72-DPI bitmap, or mutable IR is involved. */
static quantapdf_status v5_fractional_image_layer(
    const quantapdf_composer_page_state *page,
    const quantapdf_rect *bounds,
    const quantapdf_composer_image_state *source,
    const quantapdf_composer_clip_state *clip,
    const quantapdf_composer_clip_state *viewport_clip,
    const quantapdf_ir_raster_v1_limits *geometry_limits,
    quantapdf_ir_raster_image *out)
{
    quantapdf_ir geometry={0};
    quantapdf_composer_page_state transparent=*page;
    quantapdf_composer_operation paint={0};
    quantapdf_composer_path_command rectangle[5]={{0}};
    quantapdf_ir_raster_image layer={0};
    quantapdf_status status;
    const double left=(double)bounds->x0;
    const double top=(double)bounds->y0;
    const double dx=(double)bounds->x1-left;
    const double dy=(double)bounds->y1-top;
    if(dx<=0.0 || dy<=0.0 || !isfinite(dx) || !isfinite(dy))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    transparent.background_argb=0u;
    transparent.suppress_background=1;
    geometry.pages=&transparent;
    geometry.page_count=1u;
    geometry.operations=&paint;
    geometry.operation_count=1u;
    paint.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    paint.value.path.commands=rectangle;
    paint.value.path.command_count=5u;
    paint.value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    paint.value.path.options.fill=1;
    paint.value.path.options.fill_argb=UINT32_C(0xffffffff);
    paint.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    v5_point(&rectangle[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,
             bounds->x0,bounds->y0);
    v5_point(&rectangle[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,
             bounds->x1,bounds->y0);
    v5_point(&rectangle[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,
             bounds->x1,bounds->y1);
    v5_point(&rectangle[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,
             bounds->x0,bounds->y1);
    v5_point(&rectangle[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
    if(viewport_clip!=NULL) {
        const quantapdf_composer_clip_state *all[2]={
            viewport_clip,clip
        };
        /* One 4x4 sample intersection: original Image rectangle,
         * frozen W/W* Clip and actual partial-page viewport. */
        status=quantapdf_ir_raster_render_v1_multi_clipped(
            &geometry,0u,geometry_limits,all,
            clip!=NULL?2u:1u,&layer);
    } else
        status=clip==NULL ?
            quantapdf_ir_raster_render_v1(
                &geometry,0u,geometry_limits,&layer) :
            quantapdf_ir_raster_render_v1_clipped(
                &geometry,0u,geometry_limits,clip,&layer);
    if(status!=QUANTAPDF_OK) {
        quantapdf_ir_raster_image_dispose(&layer);
        return status;
    }
    if(layer.pixels==NULL ||
       layer.width!=(size_t)page->width_points ||
       layer.height!=(size_t)page->height_points ||
       layer.stride!=layer.width*4u ||
       layer.size!=layer.stride*layer.height) {
        quantapdf_ir_raster_image_dispose(&layer);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    for(size_t y=0u;y<layer.height;++y) {
        const double src_y=(((double)y+0.5)-top)*
                           (double)source->height/dy;
        size_t sy=src_y<=0.0?0u:
                  src_y>=(double)source->height?
                  (size_t)source->height-1u:(size_t)src_y;
        for(size_t x=0u;x<layer.width;++x) {
            unsigned char *pixel=layer.pixels+(y*layer.width+x)*4u;
            if(pixel[3]==0u)continue;
            const double src_x=(((double)x+0.5)-left)*
                               (double)source->width/dx;
            size_t sx=src_x<=0.0?0u:
                      src_x>=(double)source->width?
                      (size_t)source->width-1u:(size_t)src_x;
            const size_t index=sy*(size_t)source->width+sx;
            const unsigned char *rgb=
                source->data+index*(size_t)source->components;
            pixel[0]=rgb[0];
            pixel[1]=rgb[source->components==1?0u:1u];
            pixel[2]=rgb[source->components==1?0u:2u];
            if(source->has_alpha)
                pixel[3]=(unsigned char)(
                    ((unsigned)pixel[3]*
                     (unsigned)source->alpha_data[index]+127u)/255u);
        }
    }
    *out=layer;
    return QUANTAPDF_OK;
}

static void v5_over(unsigned char *dst,const unsigned char *src)
{
    const unsigned sa=src[3],da=dst[3];
    const uint64_t alpha=(uint64_t)sa*255u+(uint64_t)da*(255u-sa);
    size_t c;
    if(alpha==0u){memset(dst,0,4u);return;}
    for(c=0u;c<3u;++c){
        const uint64_t value=(uint64_t)src[c]*sa*255u+
            (uint64_t)dst[c]*da*(255u-sa);
        dst[c]=(unsigned char)((value+alpha/2u)/alpha);
    }
    dst[3]=(unsigned char)((alpha+127u)/255u);
}

/* Zero-operation is a legitimate opaque blank page, not a partially
 * published or failed Image/Path source. Admit only an exact empty
 * registry and integer bounded geometry; all other IR profiles retain
 * the existing strict V5 painted operation contract. */
static quantapdf_status v5_render_blank(
    const quantapdf_ir *ir,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_page_state *page=&ir->pages[0];
    const quantapdf_resource_registry *r=&ir->resources;
    size_t width,height,pixels,bytes;
    unsigned char *white;
    if(ir->page_count!=1u || ir->operations!=NULL ||
       ir->operation_count!=0u || ir->links!=NULL ||
       ir->link_count!=0u || ir->outlines!=NULL ||
       ir->outline_count!=0u || r->images!=NULL ||
       r->image_count!=0u || r->image_capacity!=0u ||
       r->fonts!=NULL || r->font_count!=0u ||
       r->font_capacity!=0u || r->graphics_states!=NULL ||
       r->graphics_state_count!=0u ||
       r->graphics_state_capacity!=0u || r->soft_masks!=NULL ||
       r->soft_mask_count!=0u || r->soft_mask_capacity!=0u ||
       r->clips!=NULL || r->clip_count!=0u ||
       r->clip_capacity!=0u || r->paints!=NULL ||
       r->paint_count!=0u || r->paint_capacity!=0u ||
       r->forms!=NULL || r->form_count!=0u ||
       r->form_capacity!=0u ||
       page->background_argb!=UINT32_C(0xffffffff) ||
       page->suppress_background!=0 ||
       !v5_integer(page->width_points,QP_V5_MAX_PIXELS,&width) ||
       !v5_integer(page->height_points,QP_V5_MAX_PIXELS,&height) ||
       width==0u || height==0u ||
       width>QP_V5_MAX_PIXELS/height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels=width*height;
    if(pixels>limits->max_pixels ||
       pixels>SIZE_MAX/4u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    bytes=pixels*4u;
    white=(unsigned char *)malloc(bytes);
    if(white==NULL)return QUANTAPDF_ERROR_NOMEM;
    memset(white,255,bytes);
    out_image->pixels=white;
    out_image->size=bytes;
    out_image->width=width;
    out_image->height=height;
    out_image->stride=width*4u;
    return QUANTAPDF_OK;
}

/* The source operation is immutable. Project a single borrowed resource
 * and normalize only stack-local ID copies to the single-layer profiles. */
static quantapdf_status v5_render_impl(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    int fractional_images,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_ir projected={0};
    quantapdf_composer_page_state transparent={0};
    quantapdf_composer_operation operation={0};
    quantapdf_ir_raster_image layers[QP_V5_MAX_LAYERS]={{0}};
    quantapdf_composer_clip_state viewport={0};
    quantapdf_composer_path_command viewport_commands[5]={{0}};
    const quantapdf_composer_clip_state *viewport_ptr=NULL;
    quantapdf_ir_raster_v1_limits geometry_limits;
    quantapdf_ir_raster_image_v1_limits image_limits;
    unsigned char *result=NULL;
    size_t width,height,pixels,bytes,i,j;
    size_t edge_layers=0u,source_work=0u,draw_work=0u;
    unsigned image_used=0u,clip_used=0u,fractional_mask=0u;
    size_t path_count=0u,image_count=0u;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;
    const quantapdf_composer_page_state *page;

    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || limits==NULL || ir->pages==NULL ||
       (ir->operations==NULL && ir->operation_count!=0u) ||
       ir->page_count!=1u || page_index!=0u ||
       limits->max_pixels==0u || limits->max_operations==0u ||
       limits->max_edges==0u || limits->max_sample_tests==0u ||
       limits->max_source_pixels==0u || limits->max_drawn_pixels==0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    if(!isfinite(viewport_width) || !isfinite(viewport_height) ||
       viewport_width<=0.0 || viewport_height<=0.0 ||
       viewport_width>1000000.0 || viewport_height>1000000.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(ir->operation_count==0u) {
        const double w=(double)ir->pages[0].width_points;
        const double h=(double)ir->pages[0].height_points;
        if(viewport_width>w || viewport_height>h ||
           w-viewport_width>=1.0 || h-viewport_height>=1.0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        return v5_render_blank(ir,limits,out_image);
    }

    if(ir->operation_count>QP_V5_MAX_LAYERS ||
       ir->operation_count>limits->max_operations ||
       (ir->resources.image_count!=0u &&
        (ir->resources.image_count>QP_V5_MAX_LAYERS ||
         ir->resources.images==NULL)) ||
       ir->resources.graphics_state_count>1u ||
       ir->resources.clip_count!=ir->resources.graphics_state_count ||
       (ir->resources.graphics_state_count!=0u &&
        (ir->resources.graphics_states==NULL ||
         ir->resources.clips==NULL)) ||
       ir->resources.font_count!=0u || ir->resources.form_count!=0u ||
       ir->resources.soft_mask_count!=0u ||
       ir->resources.paint_count!=0u ||
       ir->link_count!=0u || ir->outline_count!=0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* Exact one..four PATHs with zero Images are a separately admitted
     * native-only profile, using the existing four-layer ceiling rather
     * than accepting unvalidated mixed/Clip resource state.
     * Refuse ALL resource pointers, capacities and counts so no hidden
     * Image/Clip/GS/Font can be discarded by the fast path. */
    if(ir->resources.image_count==0u) {
        const quantapdf_resource_registry *r=&ir->resources;
        if(ir->operation_count==0u ||
           ir->operation_count>QP_V5_MAX_LAYERS ||
           r->images!=NULL || r->image_capacity!=0u ||
           r->graphics_states!=NULL || r->graphics_state_count!=0u ||
           r->graphics_state_capacity!=0u ||
           r->clips!=NULL || r->clip_count!=0u ||
           r->clip_capacity!=0u ||
           r->fonts!=NULL || r->font_count!=0u ||
           r->font_capacity!=0u ||
           r->forms!=NULL || r->form_count!=0u ||
           r->form_capacity!=0u ||
           r->soft_masks!=NULL || r->soft_mask_count!=0u ||
           r->soft_mask_capacity!=0u ||
           r->paints!=NULL || r->paint_count!=0u ||
           r->paint_capacity!=0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    page=&ir->pages[0];
    if(!isfinite(page->width_points) || !isfinite(page->height_points) ||
       page->width_points<=0.0f || page->height_points<=0.0f ||
       page->width_points>(float)QP_V5_MAX_PIXELS ||
       page->height_points>(float)QP_V5_MAX_PIXELS ||
       (page->suppress_background!=0 && page->suppress_background!=1) ||
       (!page->suppress_background &&
        (page->background_argb>>24u)!=255u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    width=(size_t)page->width_points;
    height=(size_t)page->height_points;
    if(width==0u || height==0u ||
       (float)width!=page->width_points ||
       (float)height!=page->height_points ||
       width>QP_V5_MAX_PIXELS/height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels=width*height;
    if(pixels>limits->max_pixels ||
       viewport_width>(double)width ||
       viewport_height>(double)height ||
       (double)width-viewport_width>=1.0 ||
       (double)height-viewport_height>=1.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    bytes=pixels*4u;
    if(viewport_width!=(double)width ||
       viewport_height!=(double)height) {
        /* Original page physical bounds are a borrowed standard Clip,
         * not a mask averaged apart from PATH/Image/W Clip coverage. */
        viewport.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        viewport.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        viewport.transform=(quantapdf_affine_transform){
            1.0f,0.0f,0.0f,1.0f,0.0f,0.0f
        };
        viewport.commands=viewport_commands;
        viewport.command_count=5u;
        v5_point(&viewport_commands[0],
                 QUANTAPDF_COMPOSER_PATH_MOVE_TO,0.0f,0.0f);
        v5_point(&viewport_commands[1],
                 QUANTAPDF_COMPOSER_PATH_LINE_TO,
                 (float)viewport_width,0.0f);
        v5_point(&viewport_commands[2],
                 QUANTAPDF_COMPOSER_PATH_LINE_TO,
                 (float)viewport_width,(float)viewport_height);
        v5_point(&viewport_commands[3],
                 QUANTAPDF_COMPOSER_PATH_LINE_TO,
                 0.0f,(float)viewport_height);
        v5_point(&viewport_commands[4],
                 QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
        viewport_ptr=&viewport;
    }

    if(ir->resources.graphics_state_count==1u) {
        const quantapdf_composer_graphics_state *gs=
            &ir->resources.graphics_states[0];
        const quantapdf_composer_clip_state *clip=&ir->resources.clips[0];
        if(gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
           gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
           gs->clip_id!=1u || gs->soft_mask_id!=0u ||
           clip->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
           clip->commands==NULL || clip->command_count==0u ||
           clip->command_count>9u ||
           clip->member_count!=0u || clip->members!=NULL ||
           (clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO &&
            clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    /* Charge unique registered RAW inputs once. Repeated Do may reuse the
     * same owned resource, but its destination work is charged each time. */
    for(i=0u;i<ir->resources.image_count;++i) {
        const quantapdf_composer_image_state *source=
            &ir->resources.images[i];
        size_t count,bytes_needed;
        if(source->format!=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW ||
           source->data==NULL || source->width==0u || source->height==0u ||
           (source->components!=1 && source->components!=3) ||
           (source->has_alpha!=0 && source->has_alpha!=1) ||
           (size_t)source->width>QP_V5_MAX_SOURCE_PIXELS/
               (size_t)source->height)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        count=(size_t)source->width*(size_t)source->height;
        if(count>limits->max_source_pixels ||
           count>QP_V5_MAX_SOURCE_PIXELS-source_work ||
           source_work>limits->max_source_pixels-count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        bytes_needed=count*(size_t)source->components;
        if(source->size!=bytes_needed ||
           (source->has_alpha ?
             (source->alpha_data==NULL || source->alpha_size!=count) :
             (source->alpha_data!=NULL || source->alpha_size!=0u)))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        source_work+=count;
    }

    /* Reject *every* unknown or late operation and unused resource BEFORE
     * allocating output. Frozen Clip membership is per operation, never
     * implicitly inherited from the preceding layer. */
    for(i=0u;i<ir->operation_count;++i) {
        const quantapdf_composer_operation *op=&ir->operations[i];
        const size_t state=(size_t)op->graphics_state_id;
        if(op->page_index!=0u ||
           state>ir->resources.graphics_state_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(state!=0u)clip_used=1u;
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const quantapdf_composer_path_operation *p=&op->value.path;
            const quantapdf_composer_path_options *opt=&p->options;
            if(opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
               opt->fill!=1 || opt->stroke!=0 ||
               (opt->fill_argb>>24u)!=255u ||
               p->commands==NULL || p->command_count==0u ||
               p->command_count>10240u ||
               p->dash_count!=0u || p->dash_lengths!=NULL ||
               (state!=0u &&
                opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE) ||
               (opt->struct_size>=QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
                (size_t)opt->graphics_state_id!=state) ||
               (opt->struct_size>=QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE &&
                (opt->fill_paint_id!=0u || opt->stroke_paint_id!=0u)))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            ++path_count;
            ++edge_layers;
        } else if(op->kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            const quantapdf_composer_image_options *opt=&op->value.image.options;
            size_t x0,x1,y0,y1,id,area;
            int has_fraction=0;
            if(opt->struct_size<QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE ||
               opt->fit!=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH ||
               (state!=0u &&
                opt->struct_size<QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE) ||
               (opt->struct_size>=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE &&
                (size_t)opt->graphics_state_id!=state))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            id=(size_t)op->value.image.image_id;
            if(id==0u || id>ir->resources.image_count ||
               !v5_image_bounds(&op->bounds,width,height,
                                fractional_images,&x0,&x1,&y0,&y1,
                                &has_fraction))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            area=(x1-x0)*(y1-y0);
            if(area>limits->max_drawn_pixels ||
               area>QP_V5_MAX_DRAW_WORK-draw_work ||
               draw_work>limits->max_drawn_pixels-area)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            draw_work+=area;
            image_used|=1u<<(unsigned)(id-1u);
            ++image_count;
            if(viewport_ptr!=NULL &&
               ((double)op->bounds.x1>viewport_width ||
                (double)op->bounds.y1>viewport_height))
                has_fraction=1;
            if(has_fraction)fractional_mask|=1u<<(unsigned)i;
            /* A fractional IMAGE rectangle requires the same bounded
             * geometry-mask sampling as an existing clipped IMAGE. */
            if(state!=0u || has_fraction)++edge_layers;
        } else {
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    /* IMAGE-only admission: 1..4 unclipped images using the already
     * bounded four-layer C11 compositor, or exactly 1..2 images under
     * the SAME owned GS/Clip leaf. The new 3/4-image profile requires
     * exact resource capacity. Mixed and clipped profiles retain their
     * independently narrower admission; no hidden state is inferred. */
    if(path_count==0u) {
        const quantapdf_resource_registry *r=&ir->resources;
        const int unclipped=
            r->graphics_states==NULL &&
            r->graphics_state_count==0u &&
            r->graphics_state_capacity==0u &&
            r->clips==NULL && r->clip_count==0u &&
            r->clip_capacity==0u;
        int bounded_clipped=
            image_count>=1u && image_count<=2u &&
            ir->operation_count==image_count &&
            r->image_capacity==image_count &&
            r->graphics_states!=NULL &&
            r->graphics_state_count==1u &&
            r->graphics_state_capacity==1u &&
            r->clips!=NULL && r->clip_count==1u &&
            r->clip_capacity==1u;
        if(bounded_clipped)
            for(size_t k=0u;k<ir->operation_count;++k) {
                const quantapdf_composer_operation *paint=&ir->operations[k];
                if(paint->kind!=QUANTAPDF_COMPOSER_OPERATION_IMAGE ||
                   paint->graphics_state_id!=1u ||
                   paint->value.image.options.struct_size<
                       QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE ||
                   paint->value.image.options.graphics_state_id!=1u) {
                    bounded_clipped=0;
                    break;
                }
            }
        if(image_count==0u ||
           image_count>(unclipped ? QP_V5_MAX_LAYERS : 2u) ||
           ir->operation_count!=image_count ||
           (!unclipped && !bounded_clipped) ||
           (unclipped && image_count>2u &&
            r->image_capacity!=image_count) ||
           r->fonts!=NULL || r->font_count!=0u ||
           r->font_capacity!=0u ||
           r->forms!=NULL || r->form_count!=0u ||
           r->form_capacity!=0u ||
           r->soft_masks!=NULL || r->soft_mask_count!=0u ||
           r->soft_mask_capacity!=0u ||
           r->paints!=NULL || r->paint_count!=0u ||
           r->paint_capacity!=0u ||
           ir->links!=NULL || ir->outlines!=NULL)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    /* Unclipped IMAGE + PATH uses the existing layered painter, but
     * MUST reject hidden unused graphics/clip/font/paint resources
     * before allocating any output. A zero count with a non-NULL
     * pointer is NOT an empty resource registry. */
    if(path_count!=0u && image_count!=0u &&
       ir->resources.graphics_state_count==0u) {
        const quantapdf_resource_registry *reg=&ir->resources;
        if(reg->graphics_states!=NULL ||
           reg->graphics_state_capacity!=0u ||
           reg->clips!=NULL || reg->clip_capacity!=0u ||
           reg->fonts!=NULL || reg->font_count!=0u ||
           reg->font_capacity!=0u ||
           reg->forms!=NULL || reg->form_count!=0u ||
           reg->form_capacity!=0u ||
           reg->soft_masks!=NULL || reg->soft_mask_count!=0u ||
           reg->soft_mask_capacity!=0u ||
           reg->paints!=NULL || reg->paint_count!=0u ||
           reg->paint_capacity!=0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if((path_count==0u && image_count==0u) ||
       (ir->resources.image_count!=0u && image_count==0u) ||
       (ir->resources.image_count==0u && image_count!=0u) ||
       image_used!=((1u<<(unsigned)ir->resources.image_count)-1u) ||
       clip_used!=ir->resources.clip_count ||
       (edge_layers!=0u &&
        (limits->max_edges/edge_layers==0u ||
         limits->max_sample_tests/edge_layers==0u)))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    geometry_limits.max_pixels=limits->max_pixels;
    geometry_limits.max_operations=1u;
    geometry_limits.max_edges=limits->max_edges/
        (edge_layers==0u?1u:edge_layers);
    geometry_limits.max_sample_tests=limits->max_sample_tests/
        (edge_layers==0u?1u:edge_layers);
    image_limits.max_pixels=limits->max_pixels;
    image_limits.max_operations=1u;
    image_limits.max_source_pixels=limits->max_source_pixels;
    image_limits.max_drawn_pixels=limits->max_drawn_pixels;
    transparent=*page;
    transparent.suppress_background=1;
    transparent.background_argb=0u;
    projected.pages=&transparent;
    projected.page_count=1u;
    projected.operations=&operation;
    projected.operation_count=1u;

    for(i=0u;i<ir->operation_count;++i) {
        const quantapdf_composer_operation *op=&ir->operations[i];
        const size_t state=(size_t)op->graphics_state_id;
        operation=*op;
        memset(&projected.resources,0,sizeof(projected.resources));
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            operation.graphics_state_id=0u;
            operation.value.path.options.graphics_state_id=0u;
            if(viewport_ptr!=NULL) {
                const quantapdf_composer_clip_state *both[2]={
                    viewport_ptr,
                    state!=0u?&ir->resources.clips[0]:NULL
                };
                status=quantapdf_ir_raster_render_v1_multi_clipped(
                    &projected,0u,&geometry_limits,both,
                    state!=0u?2u:1u,&layers[i]);
            } else
                status=state==0u ?
                    quantapdf_ir_raster_render_v1(&projected,0u,
                        &geometry_limits,&layers[i]) :
                    quantapdf_ir_raster_render_v1_clipped(&projected,0u,
                        &geometry_limits,&ir->resources.clips[0],&layers[i]);
        } else {
            const size_t id=(size_t)op->value.image.image_id;
            projected.resources.images=&ir->resources.images[id-1u];
            projected.resources.image_count=1u;
            operation.value.image.image_id=1u;
            if((fractional_mask & (1u<<(unsigned)i))!=0u) {
                status=v5_fractional_image_layer(
                    page,&op->bounds,&ir->resources.images[id-1u],
                    state!=0u?&ir->resources.clips[0]:NULL,
                    viewport_ptr,&geometry_limits,&layers[i]);
            } else if(state!=0u) {
                projected.resources.graphics_states=
                    ir->resources.graphics_states;
                projected.resources.graphics_state_count=1u;
                projected.resources.clips=ir->resources.clips;
                projected.resources.clip_count=1u;
                status=quantapdf_ir_raster_clipped_raw_image_v1(
                    &projected,0u,&image_limits,
                    &geometry_limits,&layers[i]);
            } else {
                status=quantapdf_ir_raster_raw_images_v1(
                    &projected,0u,&image_limits,&layers[i]);
            }
        }
        if(status!=QUANTAPDF_OK)goto cleanup;
        if(layers[i].pixels==NULL ||
           layers[i].width!=width || layers[i].height!=height ||
           layers[i].stride!=width*4u || layers[i].size!=bytes) {
            status=QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
    }

    result=(unsigned char *)malloc(bytes);
    if(result==NULL){status=QUANTAPDF_ERROR_NOMEM;goto cleanup;}
    for(j=0u;j<pixels;++j) {
        unsigned char *p=result+j*4u;
        const uint32_t bg=page->background_argb;
        p[0]=page->suppress_background?0u:
            (unsigned char)((bg>>16u)&255u);
        p[1]=page->suppress_background?0u:
            (unsigned char)((bg>>8u)&255u);
        p[2]=page->suppress_background?0u:
            (unsigned char)(bg&255u);
        p[3]=page->suppress_background?0u:255u;
        for(i=0u;i<ir->operation_count;++i)
            v5_over(p,layers[i].pixels+j*4u);
    }
    out_image->pixels=result;
    out_image->size=bytes;
    out_image->width=width;
    out_image->height=height;
    out_image->stride=width*4u;
    result=NULL;
    status=QUANTAPDF_OK;
cleanup:
    free(result);
    for(i=0u;i<QP_V5_MAX_LAYERS;++i)
        quantapdf_ir_raster_image_dispose(&layers[i]);
    return status;
}

/* Legacy 72-DPI and integer-image callers never acquire the extended
 * fractional-device admission simply because the code is linked. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,0,
        ir!=NULL && ir->pages!=NULL?
            (double)ir->pages[0].width_points:0.0,
        ir!=NULL && ir->pages!=NULL?
            (double)ir->pages[0].height_points:0.0,
        out_image);
}

quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_device(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        ir!=NULL && ir->pages!=NULL?
            (double)ir->pages[0].width_points:0.0,
        ir!=NULL && ir->pages!=NULL?
            (double)ir->pages[0].height_points:0.0,
        out_image);
}

/* V10 ONLY: original unrounded physical viewport on the same C11 V5
 * sample grid, not post-raster clipping nor a parallel pixel painter. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,out_image);
}
