#include "render/ir_raster_scoped_mixed_v5.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_V5_MAX_LAYERS 4u
#define QP_V5_MAX_PIXELS ((size_t)1048576u)
#define QP_V5_MAX_SOURCE_PIXELS ((size_t)4194304u)
#define QP_V5_MAX_DRAW_WORK ((size_t)4194304u)


/* Explicit profiles share two owned frozen Path leaves and one flat root.
 * Their exact GS count remains part of each independent admission. */
static int v5_two_clip_leaves(const quantapdf_resource_registry *r,
    const quantapdf_composer_clip_state *out[2],size_t graphics_states)
{
    const quantapdf_composer_clip_state *root;
    size_t i;
    if(r->graphics_states==NULL ||
       r->graphics_state_count!=graphics_states ||
       r->graphics_state_capacity!=graphics_states || r->clips==NULL ||
       r->clip_count!=3u || r->clip_capacity!=3u)return 0;
    root=&r->clips[2];
    if(root->kind!=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
       root->commands!=NULL || root->command_count!=0u ||
       root->members==NULL || root->member_count!=2u ||
       !((root->members[0]==1u && root->members[1]==2u) ||
         (root->members[0]==2u && root->members[1]==1u)) ||
       !((root->transform.a==0.0f && root->transform.b==0.0f &&
          root->transform.c==0.0f && root->transform.d==0.0f &&
          root->transform.e==0.0f && root->transform.f==0.0f) ||
         (root->transform.a==1.0f && root->transform.b==0.0f &&
          root->transform.c==0.0f && root->transform.d==1.0f &&
          root->transform.e==0.0f && root->transform.f==0.0f)))
        return 0;
    for(i=0u;i<2u;++i) {
        const quantapdf_composer_clip_state *leaf=&r->clips[i];
        const quantapdf_affine_transform *m=&leaf->transform;
        const double determinant=(double)m->a*(double)m->d-
                                 (double)m->b*(double)m->c;
        if(leaf->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
           leaf->commands==NULL || leaf->command_count==0u ||
           leaf->command_count>9u || leaf->members!=NULL ||
           leaf->member_count!=0u ||
           (leaf->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO &&
            leaf->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD) ||
           !isfinite(m->a) || !isfinite(m->b) ||
           !isfinite(m->c) || !isfinite(m->d) ||
           !isfinite(m->e) || !isfinite(m->f) ||
           fabs((double)m->a)>1000000.0 ||
           fabs((double)m->b)>1000000.0 ||
           fabs((double)m->c)>1000000.0 ||
           fabs((double)m->d)>1000000.0 ||
           fabs((double)m->e)>1000000.0 ||
           fabs((double)m->f)>1000000.0 ||
           !isfinite(determinant) || determinant==0.0)
            return 0;
        out[i]=leaf;
    }
    return 1;
}

/* Painted PATHs and the original two-GS profile keep one closed bounded
 * contour, including the canonical zero-edge Clip encoding. */
static int v5_closed_contour(
    const quantapdf_composer_path_command *commands,size_t count)
{
    size_t i;
    if(commands==NULL || count<4u || count>9u ||
       commands[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       commands[count-1u].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)
        return 0;
    for(i=1u;i+1u<count;++i)
        if(commands[i].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO &&
           commands[i].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO)
            return 0;
    return 1;
}

/* Explicit compound/two-GS and unclipped-prefix profiles share this
 * Clip grammar. Contours stay in one winding calculation and budget. */
static int v5_closed_clip_contours_limit(
    const quantapdf_composer_path_command *commands,size_t count,
    size_t max_count)
{
    size_t i,contours=0u,segments=0u;
    int active=0;
    if(commands==NULL || count<4u || count>max_count)return 0;
    for(i=0u;i<count;++i) {
        switch(commands[i].kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            if(active || contours==2u)return 0;
            active=1;segments=0u;
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            if(!active)return 0;
            ++segments;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            if(!active || segments<2u)return 0;
            active=0;++contours;
            break;
        default:return 0;
        }
    }
    return !active && contours!=0u;
}

static int v5_closed_clip_contours(
    const quantapdf_composer_path_command *commands,size_t count)
{
    return v5_closed_clip_contours_limit(commands,count,9u);
}


/* The *only* qualified ten-command compound W* Clip leaf: two
 * axis-aligned rectangles with strict inner containment. A return
 * to the contour's origin is represented by its CLOSE (same edge).
 * Legacy generic compound Clip validation retains max nine commands. */
static int v5_source_ten_rectangles(
    const quantapdf_composer_clip_state *clip)
{
    const quantapdf_composer_path_command *p;
    const quantapdf_point *a,*b,*c,*d,*e,*f,*g,*h;
    float x0,x1,y0,y1,u0,u1,v0,v1;
    if(clip==NULL || clip->commands==NULL ||
       clip->command_count!=10u ||
       clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD ||
       clip->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
       clip->members!=NULL || clip->member_count!=0u)
        return 0;
    p=clip->commands;
    if(p[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       p[1].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[2].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[3].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[4].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE ||
       p[5].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       p[6].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[7].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[8].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[9].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)return 0;
    a=&p[0].point1;b=&p[1].point1;c=&p[2].point1;d=&p[3].point1;
    e=&p[5].point1;f=&p[6].point1;g=&p[7].point1;h=&p[8].point1;
    if(a->x==b->x || a->y==d->y || e->x==f->x || e->y==h->y ||
       a->y!=b->y || b->x!=c->x || c->y!=d->y || d->x!=a->x ||
       e->y!=f->y || f->x!=g->x || g->y!=h->y || h->x!=e->x)
        return 0;
    x0=fminf(a->x,b->x);x1=fmaxf(a->x,b->x);
    y0=fminf(a->y,d->y);y1=fmaxf(a->y,d->y);
    u0=fminf(e->x,f->x);u1=fmaxf(e->x,f->x);
    v0=fminf(e->y,h->y);v1=fmaxf(e->y,h->y);
    return u0>x0 && u1<x1 && v0>y0 && v1<y1;
}

/* Strict private 7-node Graph: four Clip leaves, three roots. */
static int v5_three_gs_clips(const quantapdf_resource_registry *r,
    const quantapdf_composer_clip_state *out[4],
    int branches,int stroke_first)
{
    static const quantapdf_composer_clip_id cumulative_pairs[3][2]={
        {1u,2u},{3u,4u},{5u,6u}
    };
    static const quantapdf_composer_clip_id branch_pairs[3][2]={
        {1u,2u},{1u,3u},{1u,4u}
    };
    size_t i,j;
    if(r->clips==NULL || r->clip_count!=7u || r->clip_capacity!=7u ||
       r->graphics_states==NULL || r->graphics_state_count!=3u ||
       r->graphics_state_capacity!=3u)return 0;
    for(i=0u;i<4u;++i) {
        const quantapdf_composer_clip_state *p=&r->clips[i];
        const quantapdf_affine_transform *m=&p->transform;
        if(p->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
           p->members!=NULL || p->member_count!=0u ||
           (p->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD &&
            p->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO) ||
           m->a!=1.0f || m->b!=0.0f || m->c!=0.0f ||
           m->d!=1.0f || m->e!=0.0f || m->f!=0.0f ||
           !(stroke_first && i==1u ?
              (p->command_count==10u &&
               v5_closed_clip_contours_limit(p->commands,
                                              p->command_count,10u)) :
               v5_closed_clip_contours(p->commands,p->command_count)))
            return 0;
        out[i]=p;
    }
    for(i=0u;i<3u;++i) {
        const quantapdf_composer_clip_state *p=&r->clips[i+4u];
        const quantapdf_affine_transform *m=&p->transform;
        if(p->kind!=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
           p->commands!=NULL || p->command_count!=0u ||
           p->members==NULL || p->member_count!=2u ||
           !((m->a==0.0f && m->b==0.0f && m->c==0.0f &&
              m->d==0.0f && m->e==0.0f && m->f==0.0f) ||
             (m->a==1.0f && m->b==0.0f && m->c==0.0f &&
              m->d==1.0f && m->e==0.0f && m->f==0.0f)))return 0;
        for(j=0u;j<2u;++j)
            if(p->members[j]!=(branches?
                branch_pairs[i][j]:cumulative_pairs[i][j]))return 0;
    }
    return 1;
}

/* A separate owned shape, not a two-leaf intersection with its root
 * discarded. Prove counts/capacities before indexing either record. */
static int v5_sibling_clip_leaves(const quantapdf_resource_registry *r,
    const quantapdf_composer_clip_state *out[2])
{
    size_t i;
    if(r->graphics_states==NULL || r->graphics_state_count!=2u ||
       r->graphics_state_capacity!=2u || r->clips==NULL ||
       r->clip_count!=2u || r->clip_capacity!=2u)
        return 0;
    for(i=0u;i<2u;++i) {
        const quantapdf_composer_clip_state *leaf=&r->clips[i];
        if(leaf->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
           leaf->commands==NULL || leaf->command_count<4u ||
           leaf->command_count>9u || leaf->members!=NULL ||
           leaf->member_count!=0u ||
           (leaf->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO &&
            leaf->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD))
            return 0;
        out[i]=leaf;
    }
    return 1;
}

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
    const quantapdf_composer_page_state *page,const quantapdf_rect *bounds,
    const quantapdf_composer_image_state *source,
    quantapdf_native_geometry_v1 *geometry,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_ir_raster_image *out)
{
    quantapdf_ir_raster_image layer={0};quantapdf_status status;
    const double left=(double)bounds->x0,top=(double)bounds->y0;
    const double dx=(double)bounds->x1-left,dy=(double)bounds->y1-top;
    if(dx<=0.0 || dy<=0.0 || !isfinite(dx) || !isfinite(dy))return QUANTAPDF_ERROR_UNSUPPORTED;
    status=quantapdf_native_geometry_execute_v1(geometry,NULL,&layer);
    if(status!=QUANTAPDF_OK) {
        quantapdf_native_raster_image_dispose_v1(ledger,&layer);return status;
    }
    if(layer.pixels==NULL || layer.width!=(size_t)page->width_points ||
       layer.height!=(size_t)page->height_points || layer.stride!=layer.width*4u ||
       layer.size!=layer.stride*layer.height) {
        quantapdf_native_raster_image_dispose_v1(ledger,&layer);return QUANTAPDF_ERROR_UNSUPPORTED;
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
static quantapdf_status v5_prepare_blank(
    const quantapdf_ir *ir,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_scoped_page_v5 *out)
{
    const quantapdf_composer_page_state *page=&ir->pages[0];
    const quantapdf_resource_registry *r=&ir->resources;
    size_t width,height,pixels,bytes;
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
    out->ledger=ledger;out->page=*page;
    out->width=width;out->height=height;out->pixels=pixels;out->bytes=bytes;
    out->ready=1;
    return QUANTAPDF_OK;
}

/* The source operation is immutable. Project a single borrowed resource
 * and normalize only stack-local ID copies to the single-layer profiles. */
static quantapdf_status v5_prepare_impl(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    int fractional_images,
    double viewport_width,double viewport_height,int two_clip_optin,
    int clipped_paths_optin,int clip_states_optin,int compound_clips_optin,
    int unclipped_prefix_optin,int sibling_clips_optin,
    int visible_two_gs_optin,int visible_three_gs_optin,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_scoped_page_v5 *out)
{
    const int two_states=clip_states_optin || sibling_clips_optin;
    /* 1=previous cumulative proof; 2=independent q/Q branches.
     * All existing V5 wrappers still explicitly pass 0/1. */
    const int three_states=visible_three_gs_optin!=0;
    const int branch_three_states=visible_three_gs_optin>=2;
    const int stroke_three_states=visible_three_gs_optin==3;
    quantapdf_ir projected={0};
    quantapdf_composer_page_state transparent={0};
    quantapdf_composer_operation operation={0};
    quantapdf_native_geometry_v1 *geometry;
    quantapdf_composer_clip_state viewport={0};
    quantapdf_composer_path_command viewport_commands[5]={{0}};
    const quantapdf_composer_clip_state *viewport_ptr=NULL;
    quantapdf_ir_raster_v1_limits geometry_limits;
    quantapdf_ir_raster_image_v1_limits image_limits;
    size_t width,height,pixels,bytes,i;
    size_t edge_layers=0u,source_work=0u,draw_work=0u;
    unsigned image_used=0u,clip_used=0u,fractional_mask=0u;
    size_t path_count=0u,image_count=0u;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;
    const quantapdf_composer_clip_state *clip_leaves[4]={NULL,NULL,NULL,NULL};
    size_t clip_leaf_count=0u;
    int path_clip_restored=0;
    unsigned clip_state_phase=0u;
    const quantapdf_composer_page_state *page;

    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));geometry=out->geometry;
    if(ledger==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    if(ir==NULL || limits==NULL || ir->pages==NULL ||
       (ir->operations==NULL && ir->operation_count!=0u) ||
       ir->page_count!=1u || page_index!=0u ||
       limits->max_pixels==0u || limits->max_operations==0u ||
       limits->max_edges==0u || limits->max_sample_tests==0u ||
       limits->max_source_pixels==0u || limits->max_drawn_pixels==0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(ledger->failed)return QUANTAPDF_ERROR_UNSUPPORTED;

    if(!isfinite(viewport_width) || !isfinite(viewport_height) ||
       viewport_width<=0.0 || viewport_height<=0.0 ||
       viewport_width>1000000.0 || viewport_height>1000000.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if((two_clip_optin || clipped_paths_optin) && ir->operation_count==0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(two_states && ir->operation_count<2u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(unclipped_prefix_optin &&
       (ir->operation_count<2u ||
        ir->operations[0].graphics_state_id!=0u ||
        (clip_states_optin && !two_clip_optin)))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* PRIVATE exact 2-GS opt-in: no extra paints/resources can vanish
     * when the graph is projected one operation at a time. Keep old
     * wrappers completely separate from the region-work admission. */
    if(visible_two_gs_optin) {
        if(!two_clip_optin || !clip_states_optin ||
           !compound_clips_optin || clipped_paths_optin ||
           unclipped_prefix_optin || sibling_clips_optin ||
           ir->operation_count!=2u || ir->operations==NULL ||
           ir->resources.graphics_state_count!=2u ||
           ir->resources.clip_count!=3u ||
           ir->resources.image_count!=0u ||
           ir->resources.images!=NULL ||
           ir->resources.image_capacity!=0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        for(size_t admission_index=0u;admission_index<2u;
            ++admission_index) {
            const quantapdf_composer_operation *op=
                &ir->operations[admission_index];
            if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
               op->graphics_state_id!=admission_index+1u ||
               op->value.path.options.fill!=1 ||
               op->value.path.options.stroke!=0)
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }

    if(three_states) {
        const quantapdf_resource_registry *reg=&ir->resources;
        if(two_clip_optin || clipped_paths_optin || clip_states_optin ||
           compound_clips_optin || unclipped_prefix_optin ||
           sibling_clips_optin || visible_two_gs_optin ||
           ir->operation_count!=3u || ir->operations==NULL ||
           reg->graphics_state_count!=3u || reg->clip_count!=7u ||
           reg->images!=NULL || reg->image_count!=0u ||
           reg->image_capacity!=0u ||
           viewport_width!=(double)ir->pages[0].width_points ||
           viewport_height!=(double)ir->pages[0].height_points)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        for(size_t k=0u;k<3u;++k) {
            const quantapdf_composer_operation *op=&ir->operations[k];
            if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
               (size_t)op->graphics_state_id!=k+1u ||
               (stroke_three_states && k==0u ?
                 (op->value.path.options.fill!=0 ||
                  op->value.path.options.stroke!=1 ||
                  op->value.path.command_count!=2u) :
                 (op->value.path.options.fill!=1 ||
                  op->value.path.options.stroke!=0)))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    if(ir->operation_count==0u) {
        const double w=(double)ir->pages[0].width_points;
        const double h=(double)ir->pages[0].height_points;
        if(viewport_width>w || viewport_height>h ||
           w-viewport_width>=1.0 || h-viewport_height>=1.0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        return v5_prepare_blank(ir,limits,ledger,out);
    }

    if(ir->operation_count>QP_V5_MAX_LAYERS ||
       ir->operation_count>limits->max_operations ||
       (ir->resources.image_count!=0u &&
        (ir->resources.image_count>
            ((two_states || unclipped_prefix_optin)?2u:QP_V5_MAX_LAYERS) ||
         ir->resources.images==NULL)) ||
       ir->resources.graphics_state_count>(three_states?3u:two_states?2u:1u) ||
       ir->resources.clip_count!=
           (three_states?7u:sibling_clips_optin?2u:two_clip_optin?3u:
                                 ir->resources.graphics_state_count) ||
       (ir->resources.graphics_state_count!=0u &&
        (ir->resources.graphics_states==NULL ||
         ir->resources.clips==NULL)) ||
       ir->resources.font_count!=0u || ir->resources.form_count!=0u ||
       ir->resources.soft_mask_count!=0u ||
       ir->resources.paint_count!=0u ||
       ir->link_count!=0u || ir->outline_count!=0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(two_clip_optin || clipped_paths_optin || unclipped_prefix_optin ||
       sibling_clips_optin || three_states) {
        const quantapdf_resource_registry *r=&ir->resources;
        /* The opt-in borrows the complete owned graph. Per-layer
         * projection must not hide unregistered payloads or capacities
         * just because their resource counts happen to be zero. */
        if(r->image_capacity!=r->image_count ||
           r->fonts!=NULL || r->font_capacity!=0u ||
           r->forms!=NULL || r->form_capacity!=0u ||
           r->soft_masks!=NULL || r->soft_mask_capacity!=0u ||
           r->paints!=NULL || r->paint_capacity!=0u ||
           ir->links!=NULL || ir->outlines!=NULL)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(two_clip_optin) {
            if(!v5_two_clip_leaves(r,clip_leaves,clip_states_optin?2u:1u))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            clip_leaf_count=2u;
        }
        if(sibling_clips_optin) {
            if(!v5_sibling_clip_leaves(r,clip_leaves))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            clip_leaf_count=2u;
        }
        if(three_states) {
            if(!v5_three_gs_clips(r,clip_leaves,
                                  branch_three_states,stroke_three_states))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            clip_leaf_count=4u;
        }
        if(unclipped_prefix_optin &&
           (r->graphics_states==NULL ||
            r->graphics_state_count!=(clip_states_optin?2u:1u) ||
            r->graphics_state_capacity!=r->graphics_state_count ||
            r->clips==NULL || r->clip_count!=(two_clip_optin?3u:1u) ||
            r->clip_capacity!=r->clip_count))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if((two_states || unclipped_prefix_optin) &&
           r->image_count==0u && r->images!=NULL)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(clipped_paths_optin &&
           (r->images!=NULL || r->image_count!=0u ||
            r->image_capacity!=0u || r->graphics_states==NULL ||
            r->graphics_state_count!=1u ||
            r->graphics_state_capacity!=1u || r->clips==NULL ||
            r->clip_count!=(two_clip_optin?3u:1u) ||
            r->clip_capacity!=r->clip_count))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    /* Exact one..four PATHs with zero Images are a separately admitted
     * native-only profile, using the existing four-layer ceiling rather
     * than accepting unvalidated mixed/Clip resource state.
     * Refuse ALL resource pointers, capacities and counts so no hidden
     * Image/Clip/GS/Font can be discarded by the fast path. */
    if(ir->resources.image_count==0u &&
       !clipped_paths_optin && !two_states && !three_states &&
       !unclipped_prefix_optin) {
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

    if(three_states) {
        static const quantapdf_composer_clip_id cumulative_ids[3]={
            1u,5u,7u
        };
        static const quantapdf_composer_clip_id branch_ids[3]={
            5u,6u,7u
        };
        for(i=0u;i<3u;++i) {
            const quantapdf_composer_graphics_state *s=
                &ir->resources.graphics_states[i];
            if(s->fill_alpha!=1.0f || s->stroke_alpha!=1.0f ||
               s->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
               s->clip_id!=(branch_three_states?
                   branch_ids[i]:cumulative_ids[i]) ||
               s->soft_mask_id!=0u)
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    } else if(two_states) {
        for(i=0u;i<2u;++i) {
            const quantapdf_composer_graphics_state *gs=
                &ir->resources.graphics_states[i];
            const quantapdf_affine_transform *t=&clip_leaves[i]->transform;
            if(gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
               gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
               gs->clip_id!=(sibling_clips_optin?(i+1u):(i==0u?1u:3u)) ||
               gs->soft_mask_id!=0u ||
               t->a!=1.0f || t->b!=0.0f || t->c!=0.0f ||
               t->d!=1.0f || t->e!=0.0f || t->f!=0.0f ||
               !(compound_clips_optin ?
                   v5_closed_clip_contours(clip_leaves[i]->commands,
                                           clip_leaves[i]->command_count) :
                   v5_closed_contour(clip_leaves[i]->commands,
                                     clip_leaves[i]->command_count)))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    } else if(ir->resources.graphics_state_count==1u) {
        const quantapdf_composer_graphics_state *gs=
            &ir->resources.graphics_states[0];
        const quantapdf_composer_clip_state *clip=&ir->resources.clips[0];
        if(gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
           gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
           gs->clip_id!=(two_clip_optin?3u:1u) ||
           gs->soft_mask_id!=0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(!two_clip_optin) {
            if(clip->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
               clip->commands==NULL || clip->command_count==0u ||
               (clip->command_count>9u &&
                !(compound_clips_optin && clipped_paths_optin &&
                  v5_source_ten_rectangles(clip))) ||
               clip->member_count!=0u || clip->members!=NULL ||
               (clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO &&
                clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            /* One public V10 clipped-PATH compound leaf must preserve
             * a single even-odd/nonzero winding calculation across
             * exactly one or two closed contours. The test is done
             * BEFORE any output allocation; no fallback or alternate
             * clip sampling engine. */
            if(compound_clips_optin && clipped_paths_optin &&
               !((clip->command_count==10u &&
                  v5_source_ten_rectangles(clip)) ||
                 v5_closed_clip_contours(clip->commands,
                                         clip->command_count)))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            clip_leaves[0]=clip;
            clip_leaf_count=1u;
        }
    } else if(two_clip_optin)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(clipped_paths_optin)
        for(i=0u;i<clip_leaf_count;++i)
            if(clip_leaves[i]->command_count<4u)
                return QUANTAPDF_ERROR_UNSUPPORTED;
    if(unclipped_prefix_optin && !clip_states_optin)
        for(i=0u;i<clip_leaf_count;++i) {
            const quantapdf_affine_transform *t=&clip_leaves[i]->transform;
            if(t->a!=1.0f || t->b!=0.0f || t->c!=0.0f ||
               t->d!=1.0f || t->e!=0.0f || t->f!=0.0f ||
               !v5_closed_clip_contours(clip_leaves[i]->commands,
                                       clip_leaves[i]->command_count))
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
        if(clipped_paths_optin) {
            /* One frozen Clip scope precedes an optional Q-restored
             * suffix. A later return to GS=1 is a distinct scope that
             * this bounded Source profile has not admitted. */
            if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
               (i==0u && state!=1u) ||
               (state!=0u && path_clip_restored))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            if(state==0u)path_clip_restored=1;
        }
        if(sibling_clips_optin) {
            /* 0* 1+ 0* 2+ 0*: phase0 precedes A, phase1 paints A,
             * phase2 is the unclipped gap, phase3 paints B, and phase4
             * is the final suffix. A closed sibling cannot reenter. */
            if(state==0u) {
                if(clip_state_phase==1u)clip_state_phase=2u;
                else if(clip_state_phase==3u)clip_state_phase=4u;
            } else if(state==1u) {
                if(clip_state_phase>1u)return QUANTAPDF_ERROR_UNSUPPORTED;
                clip_state_phase=1u;
            } else {
                if(clip_state_phase==0u || clip_state_phase==4u)
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                clip_state_phase=3u;
            }
        }
        if(clip_states_optin || unclipped_prefix_optin) {
            /* Exact 1* 2+ 1* 0*: GS1 means the original first leaf,
             * GS2 its frozen intersection. After either restoration,
             * no later operation may reenter the discarded state.
             * The explicit prefix entry alone permits initial zeroes
             * before any nonzero identity has actually been painted.
             * With one GS this reduces to 0+ 1+ 0*. */
            if(state==0u) {
                if(!unclipped_prefix_optin || clip_used!=0u)
                    clip_state_phase=3u;
            }
            else if(state==1u) {
                if(clip_state_phase==3u)return QUANTAPDF_ERROR_UNSUPPORTED;
                if(clip_state_phase==1u)clip_state_phase=2u;
            } else {
                if(clip_state_phase>=2u)return QUANTAPDF_ERROR_UNSUPPORTED;
                clip_state_phase=1u;
            }
        }
        if(state!=0u)
            clip_used|=(two_states || three_states)?(1u<<(unsigned)(state-1u)):1u;
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const quantapdf_composer_path_operation *p=&op->value.path;
            const quantapdf_composer_path_options *opt=&p->options;
            /* ONLY the Source V10 explicit clipped-PATH entry can
             * carry one opaque, width-1, solid closed-triangle Stroke
             * to the existing C11 V1 clipped path sampler. All private
             * and unclipped V5 profiles remain fill-only. */
            const int clipped_triangle_stroke=
                clipped_paths_optin && ir->operation_count==1u &&
                state==1u && ir->resources.clip_count==1u &&
                p->command_count==4u &&
                p->commands!=NULL &&
                p->commands[0].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
                p->commands[1].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
                p->commands[2].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
                p->commands[3].kind==QUANTAPDF_COMPOSER_PATH_CLOSE &&
                opt->fill==0 && opt->stroke==1 &&
                opt->stroke_width==1.0f &&
                opt->line_cap==QUANTAPDF_COMPOSER_LINE_CAP_BUTT &&
                opt->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_MITER &&
                opt->miter_limit==10.0f;
            /* The only new source-like Stroke is the FIRST of exactly
             * three frozen branch paints, one open axis-aligned segment.
             * Isometry/width and actual work are still revalidated by
             * the SAME canonical V1 stroke sampler. */
            const int branch_open_stroke=
                stroke_three_states && i==0u && state==1u &&
                p->command_count==2u && p->commands!=NULL &&
                p->commands[0].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
                p->commands[1].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
                opt->fill==0 && opt->stroke==1 &&
                opt->stroke_width==1.0f &&
                opt->line_cap==QUANTAPDF_COMPOSER_LINE_CAP_BUTT &&
                opt->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_MITER &&
                opt->miter_limit==10.0f;
            if(opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
               !(clipped_triangle_stroke || branch_open_stroke ||
                 (opt->fill==1 && opt->stroke==0)) ||
               (((clipped_triangle_stroke || branch_open_stroke)?
                  opt->stroke_argb:opt->fill_argb)>>24u)!=255u ||
               p->commands==NULL || p->command_count==0u ||
               p->command_count>10240u ||
               ((clipped_paths_optin || two_states || three_states ||
                 unclipped_prefix_optin) &&
                ((!branch_open_stroke &&
                  (p->command_count<4u || p->command_count>9u)) ||
                 opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE)) ||
               ((two_states || three_states || unclipped_prefix_optin) &&
                !branch_open_stroke &&
                !v5_closed_contour(p->commands,p->command_count)) ||
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
               ((two_states || unclipped_prefix_optin) &&
                opt->struct_size<QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE) ||
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
    if(two_states || three_states || unclipped_prefix_optin) {
        /* The new scope model does not enlarge any mixed/Image layer
         * limit. Homogeneous PATHs alone may use all four layers. */
        if((path_count==0u && image_count!=2u) ||
           (image_count==0u && path_count<2u) ||
           (path_count!=0u && image_count!=0u &&
            (path_count>2u || image_count>2u)))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    } else if(path_count==0u) {
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
            r->clips!=NULL &&
            r->clip_count==(two_clip_optin?3u:1u) &&
            r->clip_capacity==(two_clip_optin?3u:1u);
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
       (three_states?clip_used!=7u:two_states?clip_used!=3u:
                    clip_used!=ir->resources.graphics_state_count) ||
       (edge_layers!=0u &&
        (limits->max_edges/edge_layers==0u ||
         limits->max_sample_tests/edge_layers==0u)))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Complete immutable inventory, charged once before any child allocates.
     * Local counters retain the established hard/shape admission; this commit
     * is the common page quota, shared even across accounted invocations. */
    if(!quantapdf_native_pixels_charge_v1(ledger,source_work,draw_work))goto cleanup;

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
        size_t active_leaves=state==0u?0u:
            (clip_states_optin && state==1u?1u:clip_leaf_count);
        const quantapdf_composer_clip_state *const *active_clips=clip_leaves;
        const quantapdf_composer_clip_state *branch_clips[2]={
            NULL,NULL
        };
        if(three_states && state!=0u)
            active_leaves=state==1u?1u:state==2u?2u:4u;
        if(branch_three_states && state!=0u) {
            branch_clips[0]=clip_leaves[0];
            branch_clips[1]=clip_leaves[state];
            active_clips=branch_clips;
            active_leaves=2u;
        }
        if(sibling_clips_optin && state!=0u) {
            active_clips=&clip_leaves[state-1u];
            active_leaves=1u;
        }
        operation=*op;memset(&projected.resources,0,sizeof(projected.resources));
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const quantapdf_composer_clip_state *all[5]={NULL,NULL,NULL,NULL,NULL};
            size_t n=0u;
            operation.graphics_state_id=0u;operation.value.path.options.graphics_state_id=0u;
            if(viewport_ptr!=NULL)all[n++]=viewport_ptr;
            for(size_t k=0u;k<active_leaves;++k)all[n++]=active_clips[k];
            const quantapdf_native_work_mode_v1 mode=n==0u?QP_NATIVE_WORK_FULL:
                stroke_three_states && i==0u?QP_NATIVE_WORK_VISIBLE_STROKE:
                ((compound_clips_optin && clipped_paths_optin) ||
                 visible_two_gs_optin || visible_three_gs_optin)?
                    QP_NATIVE_WORK_VISIBLE_FILL:QP_NATIVE_WORK_FULL;
            status=quantapdf_native_geometry_prepare_v1(&projected,0u,&geometry_limits,NULL,
                all,n,mode,ledger,&geometry[i]);
        } else if(state!=0u || (fractional_mask & (1u<<(unsigned)i))!=0u) {
            const quantapdf_composer_clip_state *all[3]={NULL,NULL,NULL};size_t n=0u;
            const int fractional=(fractional_mask & (1u<<(unsigned)i))!=0u ||
                ((two_clip_optin || sibling_clips_optin) && state!=0u);
            if(fractional) {
                if(viewport_ptr!=NULL)all[n++]=viewport_ptr;
                for(size_t k=0u;k<active_leaves;++k)all[n++]=active_clips[k];
            } else {
                /* The earlier integer one-leaf Image profile intentionally
                 * excludes a redundant viewport proved to contain bounds. */
                all[n++]=&ir->resources.clips[0];
            }
            status=quantapdf_native_image_mask_prepare_v1(page,&op->bounds,&geometry_limits,
                                                          all,n,ledger,&geometry[i]);
        } else status=QUANTAPDF_OK;
        if(status!=QUANTAPDF_OK)goto cleanup;
    }

    out->ledger=ledger;out->page=*page;out->geometry_limits=geometry_limits;
    out->image_limits=image_limits;out->width=width;out->height=height;
    out->pixels=pixels;out->bytes=bytes;out->count=ir->operation_count;
    out->drawn_pixels=draw_work;
    for(i=0u;i<out->count;++i) {
        const quantapdf_composer_operation *op=&ir->operations[i];
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            out->images[i]=*op;out->images[i].value.image.image_id=1u;
            out->resources[i]=ir->resources.images[(size_t)op->value.image.image_id-1u];
            out->modes[i]=(fractional_mask & (1u<<(unsigned)i))!=0u ||
                ((two_clip_optin || sibling_clips_optin) && op->graphics_state_id!=0u)?2u:
                op->graphics_state_id!=0u?3u:1u;
        }
    }
    out->ready=1;
    return QUANTAPDF_OK;
cleanup:
    ledger->failed=1;
    quantapdf_native_scoped_dispose_v5(out);
    return status;
}

void quantapdf_native_scoped_dispose_v5(quantapdf_native_scoped_page_v5 *page)
{
    if(page==NULL)return;
    for(size_t i=0u;i<QP_V5_MAX_LAYERS;++i)
        quantapdf_native_geometry_dispose_v1(&page->geometry[i]);
    memset(page,0,sizeof(*page));
}

/* Private M2a: compile a qualified original Text show and one decoded
 * RAW Image as TWO ordered layers under the SAME V5 owner/ledger/sampler.
 * No PDF parser, font decoder, pixel renderer or layer budget is duplicated.
 * The existing V5 full-RGBA compositor determines source-over order. */
/* A profile validates the COMPLETE page first. This private operation
 * compiler then consumes one frozen paint and stores its owned geometry in the
 * original source-order slot. It adds no admission route or pixel producer.
 * Preparation order remains Text, optional Fill, then Image to preserve the
 * qualified allocation/error/budget sequence independently of paint order. */
typedef struct qp_v5_operation_compile {
    const quantapdf_composer_operation *operation;
    const quantapdf_composer_page_state *page;
    const quantapdf_native_device_plan *physical;
    const quantapdf_composer_clip_state *clip;
    const quantapdf_composer_image_state *image;
    quantapdf_ir_raster_v1_limits limits;
    size_t slot;
    double scale;
    int image_mask;
} qp_v5_operation_compile;

static quantapdf_status qp_v5_compile_operation(
    const qp_v5_operation_compile *plan,quantapdf_native_scoped_page_v5 *out)
{
    quantapdf_ir view={0};
    quantapdf_composer_page_state page=*plan->page;
    quantapdf_composer_operation operation=*plan->operation;
    quantapdf_native_work_ledger_v1 *ledger=out->ledger;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;
    operation.graphics_state_id=0u;
    view.pages=&page;view.page_count=1u;
    view.operations=&operation;view.operation_count=1u;
    switch(operation.kind) {
    case QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES:
        status=plan->clip!=NULL?
            quantapdf_native_geometry_prepare_text_clipped_v1(
                &view,&plan->limits,plan->physical,plan->clip,
                ledger,&out->geometry[plan->slot]):
            quantapdf_native_geometry_prepare_text_v1(
                &view,&plan->limits,plan->physical,
                ledger,&out->geometry[plan->slot]);
        /* Preserve the qualified Text-on-white rounding; the SAME executor
         * converts this black grayscale observation to coverage alpha. */
        if(status==QUANTAPDF_OK)out->modes[plan->slot]=4u;
        return status;
    case QUANTAPDF_COMPOSER_OPERATION_PATH: {
        quantapdf_composer_path_command commands[5]={{0}};
        const quantapdf_composer_clip_state *clips[1]={plan->clip};
        operation.value.path.options.graphics_state_id=0u;
        for(size_t i=0u;i<5u;++i) {
            commands[i]=plan->operation->value.path.commands[i];
            if(i<4u) {
                const double x=(double)commands[i].point1.x*plan->scale;
                const double y=(double)commands[i].point1.y*plan->scale;
                if(!isfinite(x) || !isfinite(y) ||
                   x< -1000000.0 || x>1000000.0 ||
                   y< -1000000.0 || y>1000000.0)
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                commands[i].point1.x=(float)x;commands[i].point1.y=(float)y;
            }
        }
        operation.value.path.commands=commands;
        page.suppress_background=1;page.background_argb=0u;
        status=quantapdf_native_geometry_prepare_v1(
            &view,0u,&plan->limits,NULL,
            plan->clip!=NULL?clips:NULL,plan->clip!=NULL?1u:0u,
            plan->clip!=NULL?QP_NATIVE_WORK_VISIBLE_FILL:QP_NATIVE_WORK_FULL,
            ledger,&out->geometry[plan->slot]);
        if(status==QUANTAPDF_OK)out->modes[plan->slot]=0u;
        return status;
    }
    case QUANTAPDF_COMPOSER_OPERATION_IMAGE:
        out->images[plan->slot]=operation;
        out->images[plan->slot].value.image.image_id=1u;
        out->images[plan->slot].value.image.options.graphics_state_id=0u;
        out->resources[plan->slot]=*plan->image;
        out->modes[plan->slot]=plan->image_mask?2u:1u;
        if(!plan->image_mask)return QUANTAPDF_OK;
        if(plan->limits.max_edges==0u || plan->limits.max_sample_tests==0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        return plan->clip!=NULL?
            quantapdf_native_image_mask_prepare_visible_clip_v1(
                &page,&operation.bounds,&plan->limits,plan->clip,
                ledger,&out->geometry[plan->slot]):
            quantapdf_native_image_mask_prepare_v1(
                &page,&operation.bounds,&plan->limits,NULL,0u,
                ledger,&out->geometry[plan->slot]);
    default:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
}

static quantapdf_status qp_mixed_text_image_prepare(
    const quantapdf_ir *ir,const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_device_plan *physical,
    quantapdf_native_work_ledger_v1 *ledger,
    quantapdf_native_scoped_page_v5 *out,int with_fill,int with_clip)
{
    quantapdf_composer_page_state device_page={0};
    quantapdf_composer_operation image;
    quantapdf_composer_clip_state scaled_clip={0};
    quantapdf_composer_path_command scaled_clip_commands[5]={{0}};
    const quantapdf_composer_clip_state *clip_source=NULL;
    qp_v5_operation_compile operations[3]={{0}};
    size_t compile_count=0u;
    quantapdf_ir_raster_v1_limits text_limits={0},image_mask_limits={0};
    quantapdf_ir_raster_v1_limits fill_limits={0};
    size_t text_index=0u,image_index=0u,fill_index=0u,source_pixels,drawn;
    size_t image_mask_samples=0u,image_mask_edges=0u,fill_samples=0u;
    size_t x0,x1,y0,y1;
    float *coords[4];
    double scale;
    int fractional=0;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(ir==NULL || limits==NULL || physical==NULL || ledger==NULL ||
       ir->pages==NULL || ir->operations==NULL ||
       ir->page_count!=1u ||
       ir->operation_count!=(with_fill?3u:2u) ||
       ir->resources.images==NULL ||
       ir->resources.image_count!=1u ||
       ir->resources.image_capacity!=1u ||
       (with_clip && !with_fill) ||
       ir->resources.fonts!=NULL ||
       ir->resources.font_count!=0u ||
       ir->resources.font_capacity!=0u ||
       ir->resources.forms!=NULL || ir->resources.form_count!=0u ||
       ir->resources.soft_masks!=NULL || ir->resources.soft_mask_count!=0u ||
       ir->resources.paints!=NULL || ir->resources.paint_count!=0u ||
       ir->links!=NULL || ir->link_count!=0u ||
       ir->outlines!=NULL || ir->outline_count!=0u ||
       limits->max_operations<(with_fill?3u:2u) ||
       limits->max_edges==0u ||
       limits->max_sample_tests==0u || limits->max_pixels==0u ||
       !quantapdf_native_device_plan_matches_physical_page(
           physical,ir->pages[0].width_points,ir->pages[0].height_points,
           limits->max_pixels))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(with_clip) {
        const quantapdf_resource_registry *r=&ir->resources;
        if(r->graphics_states==NULL || r->graphics_state_count!=1u ||
           r->graphics_state_capacity!=1u ||
           r->clips==NULL || r->clip_count!=1u || r->clip_capacity!=1u ||
           r->graphics_states[0].clip_id!=1u ||
           r->graphics_states[0].soft_mask_id!=0u ||
           r->graphics_states[0].fill_alpha!=1.0f ||
           r->graphics_states[0].stroke_alpha!=1.0f ||
           r->graphics_states[0].blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        clip_source=&r->clips[0];
        const quantapdf_affine_transform *t=&clip_source->transform;
        if(clip_source->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
           (clip_source->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO &&
            clip_source->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(clip_source->members!=NULL || clip_source->member_count!=0u ||
           clip_source->commands==NULL || clip_source->command_count!=5u ||
           !v5_closed_contour(clip_source->commands,5u) ||
           t->a!=1.0f || t->b!=0.0f || t->c!=0.0f ||
           t->d!=1.0f || t->e!=0.0f || t->f!=0.0f)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    } else if(ir->resources.graphics_states!=NULL ||
              ir->resources.graphics_state_count!=0u ||
              ir->resources.graphics_state_capacity!=0u ||
              ir->resources.clips!=NULL ||
              ir->resources.clip_count!=0u ||
              ir->resources.clip_capacity!=0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(with_fill) {
        if(ir->operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES &&
           ir->operations[1].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
           ir->operations[2].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            text_index=0u;fill_index=1u;image_index=2u;
        } else if(ir->operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
                  ir->operations[1].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
                  ir->operations[2].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES) {
            image_index=0u;fill_index=1u;text_index=2u;
        } else return QUANTAPDF_ERROR_UNSUPPORTED;
    } else if(ir->operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES &&
              ir->operations[1].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
        text_index=0u;image_index=1u;
    } else if(ir->operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
              ir->operations[1].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES) {
        text_index=1u;image_index=0u;
    } else return QUANTAPDF_ERROR_UNSUPPORTED;
    const quantapdf_composer_operation *text_op=&ir->operations[text_index];
    const quantapdf_composer_operation *image_op=&ir->operations[image_index];
    const quantapdf_composer_image_state *resource=&ir->resources.images[0];
    if(text_op->page_index!=0u ||
       text_op->graphics_state_id!=(with_clip?1u:0u) ||
       text_op->value.text_outlines.argb!=UINT32_C(0xff000000) ||
       image_op->page_index!=0u ||
       image_op->graphics_state_id!=(with_clip?1u:0u) ||
       image_op->value.image.image_id!=1u ||
       image_op->value.image.options.struct_size<
           (with_clip?QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE:
                      QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE) ||
       image_op->value.image.options.graphics_state_id!=(with_clip?1u:0u) ||
       image_op->value.image.options.fit!=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH ||
       resource->width==0u || resource->height==0u ||
       resource->format!=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW ||
       (resource->components!=1 && resource->components!=3) ||
       resource->has_alpha!=0 ||
       resource->alpha_data!=NULL || resource->alpha_size!=0u ||
       resource->data==NULL ||
       (size_t)resource->width>SIZE_MAX/(size_t)resource->height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(with_fill) {
        const quantapdf_composer_operation *fill=&ir->operations[fill_index];
        const quantapdf_composer_path_operation *path=&fill->value.path;
        const quantapdf_affine_transform *t=&path->options.transform;
        if(fill->page_index!=0u ||
           fill->graphics_state_id!=(with_clip?1u:0u) ||
           path->options.graphics_state_id!=(with_clip?1u:0u) ||
           path->options.struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE ||
           path->options.fill!=1 || path->options.stroke!=0 ||
           (path->options.fill_argb>>24u)!=255u ||
           path->options.fill_paint_id!=0u ||
           path->options.stroke_paint_id!=0u ||
           path->dash_lengths!=NULL || path->dash_count!=0u ||
           path->commands==NULL || path->command_count!=5u ||
           t->a!=0.0f || t->b!=0.0f || t->c!=0.0f ||
           t->d!=0.0f || t->e!=0.0f || t->f!=0.0f ||
           path->commands[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
           path->commands[1].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           path->commands[2].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           path->commands[3].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           path->commands[4].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    source_pixels=(size_t)resource->width*(size_t)resource->height;
    if(source_pixels>SIZE_MAX/(size_t)resource->components ||
       resource->size!=source_pixels*(size_t)resource->components ||
       source_pixels>limits->max_source_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    device_page=*ir->pages;
    device_page.width_points=(float)physical->width;
    device_page.height_points=(float)physical->height;
    if(device_page.background_argb!=UINT32_C(0xffffffff) ||
       device_page.suppress_background!=0 ||
       physical->width>(size_t)SIZE_MAX/physical->height ||
       physical->size!=physical->width*physical->height*4u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    image=*image_op;
    scale=(double)physical->dpi/72.0;
    if(with_clip) {
        /* Source authoring coordinates are preserved for the exact
         * physical Text mask. Fill and Image each consume one separately
         * projected copy through the SAME existing V1 4x4 sampler.
         * The C11 compiled owners snapshot all these stack values. */
        scaled_clip=*clip_source;
        for(size_t i=0u;i<5u;++i) {
            scaled_clip_commands[i]=clip_source->commands[i];
            if(i<4u) {
                const double x=(double)scaled_clip_commands[i].point1.x*scale;
                const double y=(double)scaled_clip_commands[i].point1.y*scale;
                if(!isfinite(x) || !isfinite(y) ||
                   x< -1000000.0 || x>1000000.0 ||
                   y< -1000000.0 || y>1000000.0)
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                scaled_clip_commands[i].point1.x=(float)x;
                scaled_clip_commands[i].point1.y=(float)y;
            }
        }
        scaled_clip.commands=scaled_clip_commands;
    }
    coords[0]=&image.bounds.x0;coords[1]=&image.bounds.x1;
    coords[2]=&image.bounds.y0;coords[3]=&image.bounds.y1;
    for(size_t i=0u;i<4u;++i) {
        const double v=(double)(*coords[i])*scale;
        if(!isfinite(v) || v< -1000000.0 || v>1000000.0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        *coords[i]=(float)v;
    }
    if(!v5_image_bounds(&image.bounds,physical->width,physical->height,1,
                        &x0,&x1,&y0,&y1,&fractional))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(x1<x0 || y1<y0 || x1-x0>SIZE_MAX/(y1-y0==0u?1u:y1-y0))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    drawn=(x1-x0)*(y1-y0);
    if(drawn>limits->max_drawn_pixels ||
       !quantapdf_native_pixels_charge_v1(ledger,source_pixels,drawn))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    out->ledger=ledger;
    out->page=device_page;
    out->width=physical->width;out->height=physical->height;
    out->pixels=physical->width*physical->height;
    out->bytes=physical->size;
    out->count=ir->operation_count;out->drawn_pixels=drawn;
    out->image_limits.max_pixels=limits->max_pixels;
    out->image_limits.max_operations=1u;
    out->image_limits.max_source_pixels=limits->max_source_pixels;
    out->image_limits.max_drawn_pixels=limits->max_drawn_pixels;
    /* When the image is fractional, the existing 4x4 geometric mask
     * consumes a fixed 1/8 page share before sampling either layer.
     * Otherwise Text holds the full original 8M/512 budget. */
    image_mask_samples=(fractional || with_clip)?
        limits->max_sample_tests/8u:0u;
    image_mask_edges=with_clip?16u:
        (fractional?limits->max_edges/8u:0u);
    if(with_fill) {
        /* One fixed, conservative PATH share plus one Text share.
         * Every C11 geometry reserves against the SAME page ledger.
         * Budget the actual full device grid and five edges; never
         * reset the work cap per layer. Historical M2a is untouched. */
        if(with_clip) {
            /* Strict visited-area V1 Fill and Image masks are bounded
             * by their true Clip/intersection, not a full-page mask.
             * All three pre-reserved shares add to the single 8M cap. */
            fill_samples=limits->max_sample_tests/8u;
        } else {
            if(out->pixels>SIZE_MAX/80u)goto cleanup;
            fill_samples=out->pixels*80u;
            if(fill_samples>SIZE_MAX-fill_samples/8u-1u)goto cleanup;
            fill_samples+=fill_samples/8u+1u;
        }
        if(limits->max_sample_tests<=image_mask_samples ||
           fill_samples>=limits->max_sample_tests-image_mask_samples ||
           limits->max_edges<=image_mask_edges+(with_clip?16u:8u))
            goto cleanup;
        fill_limits.max_pixels=limits->max_pixels;
        fill_limits.max_operations=1u;
        fill_limits.max_edges=with_clip?16u:8u;
        fill_limits.max_sample_tests=fill_samples;
    }
    text_limits.max_pixels=limits->max_pixels;
    text_limits.max_operations=limits->max_operations;
    text_limits.max_edges=limits->max_edges-image_mask_edges-
        (with_fill?(with_clip?16u:8u):0u);
    text_limits.max_sample_tests=
        limits->max_sample_tests-image_mask_samples-fill_samples;
    if(text_limits.max_edges==0u || text_limits.max_sample_tests==0u)
        goto cleanup;
    operations[compile_count].operation=text_op;
    operations[compile_count].page=ir->pages;
    operations[compile_count].physical=physical;
    operations[compile_count].clip=clip_source;
    operations[compile_count].limits=text_limits;
    operations[compile_count].slot=text_index;
    ++compile_count;
    if(with_fill) {
        operations[compile_count].operation=&ir->operations[fill_index];
        operations[compile_count].page=&device_page;
        operations[compile_count].clip=with_clip?&scaled_clip:NULL;
        operations[compile_count].limits=fill_limits;
        operations[compile_count].scale=scale;
        operations[compile_count].slot=fill_index;
        ++compile_count;
    }
    if(fractional || with_clip) {
        image_mask_limits.max_pixels=limits->max_pixels;
        image_mask_limits.max_operations=1u;
        image_mask_limits.max_edges=image_mask_edges;
        image_mask_limits.max_sample_tests=image_mask_samples;
    }
    operations[compile_count].operation=&image;
    operations[compile_count].page=&device_page;
    operations[compile_count].clip=with_clip?&scaled_clip:NULL;
    operations[compile_count].image=resource;
    operations[compile_count].limits=image_mask_limits;
    operations[compile_count].slot=image_index;
    operations[compile_count].image_mask=fractional || with_clip;
    ++compile_count;
    for(size_t i=0u;i<compile_count;++i) {
        status=qp_v5_compile_operation(&operations[i],out);
        if(status!=QUANTAPDF_OK)goto cleanup;
    }
    out->geometry_limits=image_mask_limits;
    out->ready=1;
    return QUANTAPDF_OK;
cleanup:
    ledger->failed=1;
    quantapdf_native_scoped_dispose_v5(out);
    return status;
}

quantapdf_status quantapdf_native_scoped_prepare_m2a_text_image_v1(
    const quantapdf_ir *ir,const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_device_plan *physical,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_scoped_page_v5 *out)
{
    return qp_mixed_text_image_prepare(ir,limits,physical,ledger,out,0,0);
}

quantapdf_status quantapdf_native_scoped_prepare_m2b1_text_fill_image_v1(
    const quantapdf_ir *ir,const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_device_plan *physical,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_scoped_page_v5 *out)
{
    return qp_mixed_text_image_prepare(ir,limits,physical,ledger,out,1,0);
}

/* M2b.2c PRIVATE C11 compiled page: original one W/W* root is carried
 * by exactly three authored Text/Fill/Image paints. The same V5 owner
 * and sampler intersects each coverage at the same 4x4 device subpixels.
 * The public Source/Bitmap route remains closed until original whole-
 * image independent RGB and complete failure/receipt gates pass. */
quantapdf_status quantapdf_native_scoped_prepare_m2b2c_text_fill_image_v1(
    const quantapdf_ir *ir,const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_device_plan *physical,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_scoped_page_v5 *out)
{
    return qp_mixed_text_image_prepare(ir,limits,physical,ledger,out,1,1);
}

quantapdf_status quantapdf_native_scoped_execute_v5(
    quantapdf_native_scoped_page_v5 *page,quantapdf_ir_raster_image *out)
{
    quantapdf_ir_raster_image layers[QP_V5_MAX_LAYERS]={{0}};
    quantapdf_native_work_ledger_v1 *ledger;
    quantapdf_composer_page_state transparent;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;
    unsigned char *result=NULL;
    size_t i,j;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(page==NULL || !page->ready || page->ledger==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    ledger=page->ledger;
    if(ledger->failed)return QUANTAPDF_ERROR_UNSUPPORTED;
    /* All repeated placement and fixed sample shares are admitted before the
     * first pixel. Unique Source pixels and retained edges stay charged once. */
    if(page->executed && !quantapdf_native_pixels_charge_v1(ledger,0u,page->drawn_pixels))goto cleanup;
    for(i=0u;i<page->count;++i)
        if(page->geometry[i].data!=NULL &&
           quantapdf_native_geometry_reserve_v1(&page->geometry[i])!=QUANTAPDF_OK)goto cleanup;
    transparent=page->page;transparent.background_argb=0u;transparent.suppress_background=1;
    for(i=0u;i<page->count;++i) {
        if(page->modes[i]==0u) {
            status=quantapdf_native_geometry_execute_v1(&page->geometry[i],NULL,&layers[i]);
        } else if(page->modes[i]==4u) {
            /* M2a exact opaque black Text qualification: use the existing
             * TTF-on-white result as a per-pixel coverage observation.
             * Convert this owned buffer to black/alpha BEFORE existing
             * source-over. Do not quantize alpha during glyph sampling
             * then round it again when compositing over white. */
            status=quantapdf_native_geometry_execute_v1(&page->geometry[i],NULL,&layers[i]);
            if(status==QUANTAPDF_OK) {
                for(size_t p=0u;p<page->pixels;++p) {
                    unsigned char *pixel=layers[i].pixels+4u*p;
                    const unsigned char value=pixel[0];
                    if(pixel[1]!=value || pixel[2]!=value || pixel[3]!=255u) {
                        status=QUANTAPDF_ERROR_UNSUPPORTED;
                        break;
                    }
                    pixel[0]=pixel[1]=pixel[2]=0u;
                    pixel[3]=(unsigned char)(255u-value);
                }
            }
        } else if(page->modes[i]==2u) {
            status=v5_fractional_image_layer(&transparent,&page->images[i].bounds,&page->resources[i],
                                             &page->geometry[i],ledger,&layers[i]);
        } else if(page->modes[i]==3u) {
            status=quantapdf_native_clipped_raw_execute_v1(&transparent,&page->images[i],
                &page->resources[i],&page->image_limits,&page->geometry_limits,
                ledger,&page->geometry[i],&layers[i]);
        } else {
            quantapdf_ir projected={0};quantapdf_composer_operation operation=page->images[i];
            operation.graphics_state_id=0u;operation.value.image.options.graphics_state_id=0u;
            projected.pages=&transparent;projected.page_count=1u;
            projected.operations=&operation;projected.operation_count=1u;
            projected.resources.images=&page->resources[i];projected.resources.image_count=1u;
            status=quantapdf_ir_raster_raw_images_v1_borrowed(&projected,0u,&page->image_limits,ledger,&layers[i]);
        }
        if(status!=QUANTAPDF_OK)goto cleanup;
        if(layers[i].pixels==NULL || layers[i].width!=page->width || layers[i].height!=page->height ||
           layers[i].stride!=page->width*4u || layers[i].size!=page->bytes) {
            status=QUANTAPDF_ERROR_UNSUPPORTED;goto cleanup;
        }
    }
    result=(unsigned char *)quantapdf_native_raster_malloc_v1(ledger,page->bytes,&status);
    if(result==NULL)goto cleanup;
    for(j=0u;j<page->pixels;++j) {
        unsigned char *pixel=result+j*4u;
        const uint32_t bg=page->page.background_argb;
        pixel[0]=page->page.suppress_background?0u:(unsigned char)((bg>>16u)&255u);
        pixel[1]=page->page.suppress_background?0u:(unsigned char)((bg>>8u)&255u);
        pixel[2]=page->page.suppress_background?0u:(unsigned char)(bg&255u);
        pixel[3]=page->page.suppress_background?0u:255u;
        for(i=0u;i<page->count;++i)v5_over(pixel,layers[i].pixels+j*4u);
    }
    out->pixels=result;out->size=page->bytes;out->width=page->width;
    out->height=page->height;out->stride=page->width*4u;
    result=NULL;page->executed=1;status=QUANTAPDF_OK;
cleanup:
    if(status!=QUANTAPDF_OK)ledger->failed=1;
    quantapdf_native_raster_free_v1(ledger,result,page->bytes);
    for(i=0u;i<QP_V5_MAX_LAYERS;++i)
        quantapdf_native_raster_image_dispose_v1(ledger,&layers[i]);
    return status;
}

/* Historical one-shot wrappers use precisely the same compiler and painter. */
static quantapdf_status v5_render_impl(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    int fractional_images,double vw,double vh,int two_clip,int clipped_paths,
    int clip_states,int compound_clips,int prefix,int siblings,int visible_two,int visible_three,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_ir_raster_image *out)
{
    quantapdf_native_work_ledger_v1 local;
    quantapdf_native_scoped_page_v5 prepared={0};quantapdf_status status;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(limits==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    if(ledger==NULL) {
        quantapdf_native_work_init_v1(&local,limits->max_sample_tests,limits->max_edges);ledger=&local;
    }
    status=v5_prepare_impl(ir,page_index,limits,fractional_images,vw,vh,two_clip,clipped_paths,
        clip_states,compound_clips,prefix,siblings,visible_two,visible_three,ledger,&prepared);
    if(status==QUANTAPDF_OK)status=quantapdf_native_scoped_execute_v5(&prepared,out);
    quantapdf_native_scoped_dispose_v5(&prepared);
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
            (double)ir->pages[0].height_points:0.0,0,0,0,0,0,0,
        0,0,NULL,out_image);
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
            (double)ir->pages[0].height_points:0.0,0,0,0,0,0,0,
        0,0,NULL,out_image);
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
        viewport_width,viewport_height,0,0,0,0,0,0,0,0,NULL,out_image);
}

/* PRIVATE opt-in for independently owned two-leaf Intersection root.
 * Existing Source/Device and V5 single-Clip entrypoints NEVER call it. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,1,0,0,0,0,0,0,0,NULL,out_image);
}

/* Only this explicit profile admits PATH-only paint under frozen Clip.
 * Every older entry still requires a resource-free PATH-only registry. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_clipped_paths_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    const int two=ir!=NULL && ir->resources.clip_count==3u;
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,two,1,0,0,0,0,0,0,NULL,out_image);
}

/* Original two-GS entry retains strictly single-contour Clip leaves. */
/* Only public Source V10's ONE owned Clip/GS leaf containing two
 * closed authored contours. The original 1..4 clipped PATH fills and
 * invariant budgets are consumed by the SAME V5->V1 sampler and
 * physical viewport. Older one-contour and two-GS entrypoints stay
 * independently strict; no resource or ownership is synthesized. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_compound_clipped_paths_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    if(ir==NULL || ir->resources.clip_count!=1u) {
        if(out_image!=NULL)memset(out_image,0,sizeof(*out_image));
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,0,1,0,1,0,0,0,0,NULL,out_image);
}

quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_clip_states_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,1,0,1,0,0,0,0,0,NULL,out_image);
}

/* Compound leaves use the same V1 edge list and winding calculation,
 * not independently averaged contour masks or extra composition layers. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_compound_clip_states_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,1,0,1,1,0,0,0,0,NULL,out_image);
}

/* PRIVATE exact Source V10 two-GS/compound-Clip visible-work lease.
 * One opaque Fill under leaf1, then one under intersection {1,2}.
 * Shares the entire existing C11 V5->V1 sampler and conservative
 * edge_layers=2 work partition; never widens historical entrypoints. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_two_gs_visible_work_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,1,0,1,1,0,0,1,0,NULL,out_image);
}


/* Private exact 3-GS work proof; keep the one V5->V1 sampler. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_three_gs_visible_work_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,0,0,0,0,0,0,0,1,NULL,out_image);
}


/* Private three independent q/Q branch intersections. The unchanged
 * original yWorks first Stroke remains unsupported by this Fill-only
 * IR capability; no Source V10 profile selects this entrypoint yet. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_three_gs_branch_visible_work_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,0,0,0,0,0,0,0,2,NULL,out_image);
}


/* PRIVATE source-shaped 3GS subset: original one-pixel horizontal
 * butt Stroke in first frozen W/W* branch, followed by two opaque
 * PATH Fills. The source reader still rejects the authentic original
 * PDF until its actual q/Q, CTM and Clip ownership are qualified. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_three_gs_branch_stroke_visible_work_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,0,0,0,0,0,0,0,3,NULL,out_image);
}

/* Prefix is a distinct admission, never an implicit widening of any
 * older mixed, PATH-only or two-GS wrapper. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_unclipped_prefix_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    const int two=ir!=NULL && ir->resources.clip_count==3u;
    const int states=ir!=NULL && ir->resources.graphics_state_count==2u;
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,two,0,states,1,1,0,0,0,NULL,out_image);
}

quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_sibling_clips_device_viewport(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double viewport_width,double viewport_height,
    quantapdf_ir_raster_image *out_image)
{
    return v5_render_impl(ir,page_index,limits,1,
        viewport_width,viewport_height,0,0,0,1,0,1,0,0,NULL,out_image);
}

/* Accounted access to exactly the production profiles. Legacy wrappers
 * above use a local ledger; the prepared bridge supplies the page owner. */
quantapdf_status quantapdf_native_scoped_prepare_v5(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double vw,double vh,quantapdf_native_scoped_profile_v5 profile,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_native_scoped_page_v5 *out)
{
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(ledger==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    switch(profile) {
    case QP_SCOPED_LEGACY:
        return v5_prepare_impl(ir,page_index,limits,0,vw,vh,0,0,0,0,0,0,0,0,ledger,out);
    case QP_SCOPED_DEVICE:
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,0,0,0,0,0,0,0,0,ledger,out);
    case QP_SCOPED_TWO_CLIP:
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,1,0,0,0,0,0,0,0,ledger,out);
    case QP_SCOPED_CLIPPED_PATHS:
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,
            ir!=NULL && ir->resources.clip_count==3u,1,0,0,0,0,0,0,ledger,out);
    case QP_SCOPED_COMPOUND_PATHS:
        if(ir==NULL || ir->resources.clip_count!=1u)return QUANTAPDF_ERROR_UNSUPPORTED;
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,0,1,0,1,0,0,0,0,ledger,out);
    case QP_SCOPED_CLIP_STATES:
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,1,0,1,0,0,0,0,0,ledger,out);
    case QP_SCOPED_COMPOUND_STATES:
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,1,0,1,1,0,0,0,0,ledger,out);
    case QP_SCOPED_PREFIX:
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,
            ir!=NULL && ir->resources.clip_count==3u,0,
            ir!=NULL && ir->resources.graphics_state_count==2u,1,1,0,0,0,ledger,out);
    case QP_SCOPED_SIBLINGS:
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,0,0,0,1,0,1,0,0,ledger,out);
    case QP_SCOPED_THREE_GS:
        return v5_prepare_impl(ir,page_index,limits,1,vw,vh,0,0,0,0,0,0,0,3,ledger,out);
    default:return QUANTAPDF_ERROR_UNSUPPORTED;
    }
}

quantapdf_status quantapdf_ir_raster_scoped_mixed_v5_accounted(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    double vw,double vh,quantapdf_native_scoped_profile_v5 profile,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_ir_raster_image *out)
{
    quantapdf_native_scoped_page_v5 prepared={0};quantapdf_status status;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    status=quantapdf_native_scoped_prepare_v5(ir,page_index,limits,vw,vh,profile,ledger,&prepared);
    if(status==QUANTAPDF_OK)status=quantapdf_native_scoped_execute_v5(&prepared,out);
    quantapdf_native_scoped_dispose_v5(&prepared);return status;
}
