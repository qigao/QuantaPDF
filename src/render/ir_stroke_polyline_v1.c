#include "render/ir_stroke_polyline_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_STROKE_POINTS 1024u
#define QP_STROKE_COMMANDS (QP_STROKE_POINTS * 10u)
#define QP_STROKE_COORD 1000000.0
#define QP_DASH_ITEMS 64u
#define QP_DASH_WORK 65536u
#define QP_ARC_SEGMENTS 512u
#define QP_ARC_CHORD_ERROR (1.0/64.0)
#define QP_ARC_FLOAT_ERROR (1.0/128.0)
#define QP_CURVE_DEPTH 16u
#define QP_CURVE_WORK 65536u
#define QP_CURVE_ERROR (1.0/32.0)
#define QP_PI 3.14159265358979323846

typedef struct stroke_point { double x, y; } stroke_point;
typedef struct stroke_builder {
    quantapdf_composer_path_command *commands;
    size_t count, edges, max_edges, dash_work, curve_work, curve_segments;
    int curve_mode;
    const quantapdf_composer_path_operation *source;
    double dash_period, arc_scale;
    const quantapdf_composer_path_options *options;
} stroke_builder;

static int valid_point(stroke_point p)
{
    return isfinite(p.x) && isfinite(p.y) &&
        fabs(p.x) <= QP_STROKE_COORD && fabs(p.y) <= QP_STROKE_COORD;
}
static int same_point(stroke_point a, stroke_point b)
{ return a.x == b.x && a.y == b.y; }

/* Equal orientation makes nonzero winding a union of stroke pieces, even
 * at self intersections. A reflected CTM reverses every piece together. */
static quantapdf_status polygon(stroke_builder *b, stroke_point *p, size_t n)
{
    double area = 0.0;
    if (n > b->max_edges - b->edges ||
        n + 1u > QP_STROKE_COMMANDS - b->count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    for (size_t i = 0; i < n; ++i) {
        if (!valid_point(p[i]) || (b->curve_mode &&
            hypot((double)(float)p[i].x-p[i].x,(double)(float)p[i].y-p[i].y)*b->arc_scale>QP_ARC_FLOAT_ERROR))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        /* The Composer path boundary stores floats. Check that the emitted
         * polygon still has an area at that precision, not just in double. */
        p[i].x = (float)p[i].x; p[i].y = (float)p[i].y;
    }
    for (size_t i = 1; i + 1u < n; ++i)
        area += (p[i].x-p[0].x)*(p[i+1u].y-p[0].y) -
                (p[i].y-p[0].y)*(p[i+1u].x-p[0].x);
    if (!isfinite(area) || area == 0.0) return QUANTAPDF_ERROR_UNSUPPORTED;
    for (size_t i = 0; i < n; ++i) {
        size_t j = area > 0.0 ? i : n-1u-i;
        quantapdf_composer_path_command *c = &b->commands[b->count++];
        c->kind = i == 0 ? QUANTAPDF_COMPOSER_PATH_MOVE_TO : QUANTAPDF_COMPOSER_PATH_LINE_TO;
        c->point1 = (quantapdf_point){(float)p[j].x,(float)p[j].y};
    }
    b->commands[b->count++].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    b->edges += n;
    return QUANTAPDF_OK;
}
/* Frobenius norm bounds all linear magnification, including shear and the
 * final FIT grid. Translation does not amplify chord/float error. */
static double arc_scale(const quantapdf_composer_path_options *o,const double *device)
{
    double a=1,b=0,c=0,d=1;
    if(o->struct_size>=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE) {
        const quantapdf_affine_transform *t=&o->transform;
        int zero=t->a==0 && t->b==0 && t->c==0 && t->d==0 && t->e==0 && t->f==0;
        if(!zero) {
            if(!isfinite(t->e) || !isfinite(t->f) || fabs(t->e)>QP_STROKE_COORD || fabs(t->f)>QP_STROKE_COORD) return 0;
            a=t->a; b=t->b; c=t->c; d=t->d;
        }
    }
    if(device) {
        const double aa=a,bb=b,cc=c,dd=d;
        a=aa*device[0]+bb*device[2]; b=aa*device[1]+bb*device[3];
        c=cc*device[0]+dd*device[2]; d=cc*device[1]+dd*device[3];
    }
    if(!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d) ||
        fabs(a)>QP_STROKE_COORD || fabs(b)>QP_STROKE_COORD ||
        fabs(c)>QP_STROKE_COORD || fabs(d)>QP_STROKE_COORD || a*d-b*c==0.0) return 0;
    return hypot(hypot(a,b),hypot(c,d));
}
static quantapdf_status arc_sector(stroke_builder *b,stroke_point at,
    stroke_point start,stroke_point end,double angle)
{
    stroke_point p[QP_ARC_SEGMENTS+2u];
    size_t steps=2u;
    double r=(double)b->options->stroke_width/2.0;
    /* 1-cos(t/2) <= t*t/8 bounds sagitta without inverse trig cancellation. */
    double bound=r*b->arc_scale*angle*angle/8.0;
    while(steps<QP_ARC_SEGMENTS && bound/((double)steps*(double)steps)>QP_ARC_CHORD_ERROR) steps*=2u;
    if(bound/((double)steps*(double)steps)>QP_ARC_CHORD_ERROR) return QUANTAPDF_ERROR_UNSUPPORTED;
    p[0]=at;
    for(size_t i=0;i<=steps;++i) {
        double theta=angle*(double)i/(double)steps;
        stroke_point unit=i==0u ? start : i==steps ? end :
            (stroke_point){start.x*cos(theta)-start.y*sin(theta),start.x*sin(theta)+start.y*cos(theta)};
        p[i+1u]=(stroke_point){at.x+r*unit.x,at.y+r*unit.y};
    }
    for(size_t i=0;i<steps+2u;++i) {
        if(!valid_point(p[i]) || hypot((double)(float)p[i].x-p[i].x,
            (double)(float)p[i].y-p[i].y)*b->arc_scale>QP_ARC_FLOAT_ERROR)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return polygon(b,p,steps+2u);
}
static quantapdf_status round_cap(stroke_builder *b,stroke_point at,stroke_point u,int start)
{
    stroke_point normal={-u.y,u.x},opposite={u.y,-u.x};
    return arc_sector(b,at,normal,opposite,start ? QP_PI : -QP_PI);
}
static int direction(stroke_point a, stroke_point b, stroke_point *u)
{
    double dx=b.x-a.x, dy=b.y-a.y, length=hypot(dx,dy);
    if (!isfinite(length) || length == 0.0) return 0;
    *u=(stroke_point){dx/length,dy/length}; return 1;
}
static quantapdf_status segment(stroke_builder *b,stroke_point a,
    stroke_point z,int start_cap,int end_cap)
{
    stroke_point u,p[4];
    double hw=(double)b->options->stroke_width/2.0;
    if (!direction(a,z,&u)) return QUANTAPDF_ERROR_UNSUPPORTED;
    if (b->options->line_cap == QUANTAPDF_COMPOSER_LINE_CAP_SQUARE) {
        if (start_cap) { a.x-=u.x*hw; a.y-=u.y*hw; }
        if (end_cap) { z.x+=u.x*hw; z.y+=u.y*hw; }
    }
    p[0]=(stroke_point){a.x-u.y*hw,a.y+u.x*hw};
    p[1]=(stroke_point){z.x-u.y*hw,z.y+u.x*hw};
    p[2]=(stroke_point){z.x+u.y*hw,z.y-u.x*hw};
    p[3]=(stroke_point){a.x+u.y*hw,a.y-u.x*hw};
    quantapdf_status status=polygon(b,p,4u);
    if(status!=QUANTAPDF_OK) return status;
    if(b->options->line_cap==QUANTAPDF_COMPOSER_LINE_CAP_ROUND) {
        if(start_cap) { status=round_cap(b,a,u,1); if(status!=QUANTAPDF_OK) return status; }
        if(end_cap) return round_cap(b,z,u,0);
    }
    return QUANTAPDF_OK;
}
static quantapdf_status join_directions(stroke_builder *b,stroke_point at,
    stroke_point u,stroke_point v)
{
    stroke_point p[4];
    double cross,dot,side,hw=(double)b->options->stroke_width/2.0;
    cross=u.x*v.y-u.y*v.x; dot=u.x*v.x+u.y*v.y;
    if (cross == 0.0) {
        /* Collinear continuation needs no wedge. Exact reversal/cusps retain
         * an explicit refusal until their degenerate semantics are qualified. */
        return dot > 0.0 ? QUANTAPDF_OK : QUANTAPDF_ERROR_UNSUPPORTED;
    }
    side=cross > 0.0 ? -1.0 : 1.0;
    if(b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_ROUND) {
        stroke_point first={-u.y*side,u.x*side},last={-v.y*side,v.x*side};
        return arc_sector(b,at,first,last,atan2(cross,dot));
    }
    p[0]=at;
    p[1]=(stroke_point){at.x-u.y*hw*side,at.y+u.x*hw*side};
    p[2]=(stroke_point){at.x-v.y*hw*side,at.y+v.x*hw*side};
    if (b->options->line_join == QUANTAPDF_COMPOSER_LINE_JOIN_MITER) {
        double t=((p[2].x-p[1].x)*v.y-(p[2].y-p[1].y)*v.x)/cross;
        stroke_point m={p[1].x+t*u.x,p[1].y+t*u.y};
        double ratio=hypot(m.x-at.x,m.y-at.y)/hw;
        if (!isfinite(ratio)) return QUANTAPDF_ERROR_UNSUPPORTED;
        if (ratio <= b->options->miter_limit) {
            p[3]=p[2]; p[2]=m; return polygon(b,p,4u);
        }
    }
    return polygon(b,p,3u);
}
static quantapdf_status join(stroke_builder *b,stroke_point a,
    stroke_point at,stroke_point z)
{
    stroke_point u,v;
    if(!direction(a,at,&u) || !direction(at,z,&v)) return QUANTAPDF_ERROR_UNSUPPORTED;
    return join_directions(b,at,u,v);
}
/* Caps are separate equal-orientation pieces. Each dash remains an open
 * fragment, including coincident endpoints at an authored closed seam. */
static quantapdf_status dash_cap(stroke_builder *b,stroke_point at,
    stroke_point u,int start)
{
    stroke_point p[4];
    double hw=(double)b->options->stroke_width/2.0;
    double sign=start ? -1.0 : 1.0;
    if(b->options->line_cap==QUANTAPDF_COMPOSER_LINE_CAP_BUTT) return QUANTAPDF_OK;
    if(b->options->line_cap==QUANTAPDF_COMPOSER_LINE_CAP_ROUND) return round_cap(b,at,u,start);
    p[0]=(stroke_point){at.x-u.y*hw,at.y+u.x*hw};
    p[1]=(stroke_point){p[0].x+sign*u.x*hw,p[0].y+sign*u.y*hw};
    p[2]=(stroke_point){at.x+u.y*hw+sign*u.x*hw,at.y-u.x*hw+sign*u.y*hw};
    p[3]=(stroke_point){at.x+u.y*hw,at.y-u.x*hw};
    return polygon(b,p,4u);
}
/* Zero on entries are real dash fragments. Their two caps use the underlying
 * segment direction; zero gaps still separate adjacent painted fragments.
 * Charge every transition even when butt caps emit no geometry. */
static quantapdf_status dash_zeros(stroke_builder *b,stroke_point at,
    stroke_point u,size_t logical,size_t *index,double *remain)
{
    const quantapdf_composer_path_operation *src=b->source;
    while(*remain==0.0) {
        quantapdf_status status;
        if(++b->dash_work>QP_DASH_WORK) return QUANTAPDF_ERROR_UNSUPPORTED;
        if(*index%2u==0u) {
            status=dash_cap(b,at,u,1); if(status!=QUANTAPDF_OK) return status;
            status=dash_cap(b,at,u,0); if(status!=QUANTAPDF_OK) return status;
        }
        *index=(*index+1u)%logical;
        *remain=src->dash_lengths[*index%src->dash_count];
    }
    return QUANTAPDF_OK;
}
static quantapdf_status dashed_subpath(stroke_builder *b,stroke_point *p,
    size_t n,int closed)
{
    const quantapdf_composer_path_operation *src=b->source;
    size_t logical=src->dash_count*(src->dash_count%2u ? 2u : 1u), index=0u;
    double phase=fmod((double)src->dash_phase,b->dash_period), remain;
    int active=0,tail_on=0;
    stroke_point last_u={0,0};
    quantapdf_status status;
    size_t segments=closed ? n : n-1u;
    /* Odd arrays repeat twice before their on/off state repeats. */
    while(phase>0.0 && phase >= src->dash_lengths[index%src->dash_count]) {
        if(++b->dash_work>QP_DASH_WORK) return QUANTAPDF_ERROR_UNSUPPORTED;
        phase-=src->dash_lengths[index%src->dash_count]; index=(index+1u)%logical;
    }
    remain=(double)src->dash_lengths[index%src->dash_count]-phase;
    for(size_t i=0;i<segments;++i) {
        stroke_point a=p[i],z=p[(i+1u)%n],u;
        double length=hypot(z.x-a.x,z.y-a.y), distance=0.0;
        if(!direction(a,z,&u)) return QUANTAPDF_ERROR_UNSUPPORTED;
        last_u=u;
        if(active) {
            status=join(b,p[i-1u],a,z); if(status!=QUANTAPDF_OK) return status;
        }
        while(distance < length) {
            stroke_point begin=distance==0.0 ? a : (stroke_point){a.x+u.x*distance,a.y+u.y*distance};
            status=dash_zeros(b,begin,u,logical,&index,&remain);
            if(status!=QUANTAPDF_OK) return status;
            double available=length-distance;
            int boundary=remain<=available, end=remain>=available;
            double take=boundary ? remain : available;
            double next=end ? length : distance+take;
            stroke_point finish=end ? z : (stroke_point){a.x+u.x*next,a.y+u.y*next};
            int on=index%2u==0u, final=end && i+1u==segments;
            if(++b->dash_work>QP_DASH_WORK || !(next>distance)) return QUANTAPDF_ERROR_UNSUPPORTED;
            if(on) {
                if(!active) {
                    status=dash_cap(b,begin,u,1); if(status!=QUANTAPDF_OK) return status;
                }
                status=segment(b,begin,finish,0,0); if(status!=QUANTAPDF_OK) return status;
                active=1;
                if(boundary && !final) {
                    status=dash_cap(b,finish,u,0); if(status!=QUANTAPDF_OK) return status;
                    active=0;
                }
            }
            if(final) tail_on=on;
            distance=next;
            if(boundary) {
                index=(index+1u)%logical;
                remain=src->dash_lengths[index%src->dash_count];
            } else remain-=take;
        }
    }
    /* Do not join the first and last dash merely because their endpoint
     * coordinates coincide. The dash generator produces open fragments. */
    stroke_point end=closed ? p[0] : p[n-1u];
    if(tail_on) { status=dash_cap(b,end,last_u,0); if(status!=QUANTAPDF_OK) return status; }
    /* A new zero dash at the terminal distance is outside the traversed
     * half-open path interval. Do not synthesize another endpoint fragment. */
    return QUANTAPDF_OK;
}
static quantapdf_status zero_length_subpath(stroke_builder *b,stroke_point at)
{
    quantapdf_status status;
    if(b->source->dash_count!=0u) return QUANTAPDF_ERROR_UNSUPPORTED;
    /* ISO 32000-1 §8.5.3.2: a degenerate stroked subpath is painted only
     * for round caps. Butt/projecting-square caps have no determinate
     * orientation and therefore produce no output. */
    if(b->options->line_cap!=QUANTAPDF_COMPOSER_LINE_CAP_ROUND)
        return QUANTAPDF_OK;
    status=arc_sector(b,at,(stroke_point){1,0},(stroke_point){0,1},QP_PI/2.0);
    if(status!=QUANTAPDF_OK) return status;
    status=arc_sector(b,at,(stroke_point){0,1},(stroke_point){-1,0},QP_PI/2.0);
    if(status!=QUANTAPDF_OK) return status;
    status=arc_sector(b,at,(stroke_point){-1,0},(stroke_point){0,-1},QP_PI/2.0);
    if(status!=QUANTAPDF_OK) return status;
    return arc_sector(b,at,(stroke_point){0,-1},(stroke_point){1,0},QP_PI/2.0);
}

static quantapdf_status subpath(stroke_builder *b,stroke_point *p,size_t n,int closed)
{
    quantapdf_status status;
    /* Only a final exact A→B→A→CLOSE is the same centerline trace as
     * A→B→CLOSE: the reverse is already the implicit closing edge.
     * Keep miter cusp, dashed reversal and all other authored returns out. */
    if(closed && n==3u && same_point(p[0],p[2]) &&
        !same_point(p[0],p[1])) {
        if(b->source->dash_count!=0u ||
            (b->options->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL &&
             b->options->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        n=2u;
    }
    if (closed && n > 1u && same_point(p[0],p[n-1u])) --n;
    /* Closing a single-point linear subpath still counts as a degenerate
     * stroke, including authored coincident LINEs collapsed to that point.
     * Dashed degenerate paths remain outside the qualified profile. */
    if (closed && n==1u) return zero_length_subpath(b,p[0]);
    /* An open A→B→A return is a distinct authored cusp. Qualify only
     * the last exact return under Butt caps and Bevel/Round joins:
     * the first segment already covers both traversals, and a Round
     * join at B is an outward semicircle. An open cap at A is Butt;
     * do not treat the cusp as a new end cap at B. */
    if(!closed && n==3u && same_point(p[0],p[2]) &&
        !same_point(p[0],p[1])) {
        stroke_point u;
        if(b->source->dash_count!=0u ||
            b->options->line_cap!=QUANTAPDF_COMPOSER_LINE_CAP_BUTT ||
            (b->options->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL &&
             b->options->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND) ||
            !direction(p[0],p[1],&u))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        status=segment(b,p[0],p[1],0,0);
        if(status!=QUANTAPDF_OK) return status;
        return b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_ROUND ?
            round_cap(b,p[1],u,0) : QUANTAPDF_OK;
    }
    /* A two-distinct-point closed path traces one segment in both
     * directions. With bevel joins the return adds no coverage beyond the
     * butt-ended centerline strip; with round joins each end is a bounded
     * semicircle. Caps are irrelevant because the subpath is closed.
     * Unknown cusp/miter and dashed semantics remain fail-closed. */
    if(closed && n==2u) {
        stroke_point u;
        if(b->source->dash_count!=0u ||
            (b->options->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL &&
             b->options->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND) ||
            !direction(p[0],p[1],&u))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        status=segment(b,p[0],p[1],0,0);
        if(status!=QUANTAPDF_OK) return status;
        if(b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_ROUND) {
            status=round_cap(b,p[0],u,1);
            if(status!=QUANTAPDF_OK) return status;
            return round_cap(b,p[1],u,0);
        }
        return QUANTAPDF_OK;
    }
    if (n < (closed ? 3u : 2u)) return QUANTAPDF_ERROR_UNSUPPORTED;
    if(b->source->dash_count) {
        /* Geometry admission is independent of which intervals happen to be
         * off: an unqualified cusp must not disappear behind a dash gap. */
        for(size_t i=closed ? 0u : 1u;i<(closed ? n : n-1u);++i) {
            stroke_point u,v;
            if(!direction(p[(i+n-1u)%n],p[i],&u) ||
                !direction(p[i],p[(i+1u)%n],&v) ||
                (u.x*v.y-u.y*v.x==0.0 && u.x*v.x+u.y*v.y<=0.0))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        return dashed_subpath(b,p,n,closed);
    }
    size_t segments=closed ? n : n-1u;
    for (size_t i=0;i<segments;++i) {
        status=segment(b,p[i],p[(i+1u)%n],!closed && i==0,!closed && i+1u==segments);
        if (status != QUANTAPDF_OK) return status;
    }
    for (size_t i=closed ? 0u : 1u;i<(closed ? n : n-1u);++i) {
        status=join(b,p[(i+n-1u)%n],p[i],p[(i+1u)%n]);
        if (status != QUANTAPDF_OK) return status;
    }
    return QUANTAPDF_OK;
}

static stroke_point midpoint(stroke_point a,stroke_point z)
{ return (stroke_point){(a.x+z.x)*.5,(a.y+z.y)*.5}; }
static int convex_quad(const stroke_point *p)
{
    double sign=0;
    for(size_t i=0;i<4u;++i) {
        stroke_point a=p[i],z=p[(i+1u)%4u],q=p[(i+2u)%4u];
        double cross=(z.x-a.x)*(q.y-z.y)-(z.y-a.y)*(q.x-z.x);
        if(!isfinite(cross) || cross==0.0 || (sign!=0.0 && (cross>0.0)!=(sign>0.0))) return 0;
        sign=cross;
    }
    return 1;
}
/* Certify the entire normal strip, not just its centerline. Derivative
 * controls share a forward cone, so the tangent cannot vanish in the open
 * interval. The same-parameter center/normal bounds are documented in the
 * conformance contract. Artificial vertices share exact tangent cross-sections. */
static int curve_leaf(stroke_builder *b,const stroke_point *p,
    stroke_point first,stroke_point last,stroke_point *quad)
{
    stroke_point u,d[3]; double m[3],slope=0.0,center,normal;
    const double hw=(double)b->options->stroke_width/2.0;
    if(!direction(p[0],p[3],&u)) return 0;
    for(size_t i=0;i<3u;++i) {
        d[i]=(stroke_point){p[i+1u].x-p[i].x,p[i+1u].y-p[i].y};
        m[i]=hypot(d[i].x,d[i].y);
        if(m[i]>0.0) {
            double along=d[i].x*u.x+d[i].y*u.y;
            if(along<=0.0) return 0;
            slope=fmax(slope,fabs(d[i].x*u.y-d[i].y*u.x)/along);
        }
    }
    stroke_point ends[2]={first,last};
    for(size_t i=0;i<2u;++i) {
        double along=ends[i].x*u.x+ends[i].y*u.y;
        if(along<=0.0) return 0;
        slope=fmax(slope,fabs(ends[i].x*u.y-ends[i].y*u.x)/along);
    }
    center=fmax(hypot(p[1].x-(2.0*p[0].x+p[3].x)/3.0,p[1].y-(2.0*p[0].y+p[3].y)/3.0),
                hypot(p[2].x-(p[0].x+2.0*p[3].x)/3.0,p[2].y-(p[0].y+2.0*p[3].y)/3.0));
    if(slope==0.0) center=0.0; /* A monotone collinear curve is exactly a line. */
    normal=2.0*slope;
    if(m[0]>0.0 && m[2]>0.0) {
        stroke_point a={d[0].x/m[0],d[0].y/m[0]},z={d[2].x/m[2],d[2].y/m[2]};
        double delta=hypot(a.x-z.x,a.y-z.y);
        double k=hypot(d[1].x-(m[0]*z.x+m[2]*a.x)/2.0,
                       d[1].y-(m[0]*z.y+m[2]*a.y)/2.0);
        double endpoint=fmax(hypot(a.x-first.x,a.y-first.y),hypot(z.x-last.x,z.y-last.y));
        normal=fmin(normal,delta*delta/4.0+k/fmin(m[0],m[2])+endpoint);
    }
    if(!isfinite(center+hw*normal) || (center+hw*normal)*b->arc_scale>QP_CURVE_ERROR) return 0;
    quad[0]=(stroke_point){p[0].x-hw*first.y,p[0].y+hw*first.x};
    quad[1]=(stroke_point){p[3].x-hw*last.y,p[3].y+hw*last.x};
    quad[2]=(stroke_point){p[3].x+hw*last.y,p[3].y-hw*last.x};
    quad[3]=(stroke_point){p[0].x+hw*first.y,p[0].y-hw*first.x};
    if(!convex_quad(quad)) return 0;
    stroke_point stored[4];
    for(size_t i=0;i<4u;++i) stored[i]=(stroke_point){(float)quad[i].x,(float)quad[i].y};
    return convex_quad(stored);
}
static quantapdf_status cubic_strip(stroke_builder *b,const stroke_point *p,
    stroke_point first,stroke_point last,unsigned depth)
{
    stroke_point quad[4];
    if(++b->curve_work>QP_CURVE_WORK) return QUANTAPDF_ERROR_UNSUPPORTED;
    if(curve_leaf(b,p,first,last,quad)) {
        if(++b->curve_segments>QP_STROKE_POINTS) return QUANTAPDF_ERROR_UNSUPPORTED;
        return polygon(b,quad,4u);
    }
    if(depth>=QP_CURVE_DEPTH) return QUANTAPDF_ERROR_UNSUPPORTED;
    stroke_point q0=midpoint(p[0],p[1]),q1=midpoint(p[1],p[2]),q2=midpoint(p[2],p[3]);
    stroke_point r0=midpoint(q0,q1),r1=midpoint(q1,q2),at=midpoint(r0,r1),tangent;
    if(!direction(r0,r1,&tangent)) return QUANTAPDF_ERROR_UNSUPPORTED;
    stroke_point left[4]={p[0],q0,r0,at},right[4]={at,r1,q2,p[3]};
    quantapdf_status status=cubic_strip(b,left,first,tangent,depth+1u);
    if(status!=QUANTAPDF_OK) return status;
    return cubic_strip(b,right,tangent,last,depth+1u);
}
typedef struct solid_subpath {
    stroke_point start,current,first,last;
    size_t segments;
    int active,curves,had_line_command,open_terminal_return;
} solid_subpath;
static quantapdf_status solid_line(stroke_builder *b,solid_subpath *s,stroke_point z)
{
    stroke_point u; quantapdf_status status;
    if(same_point(s->current,z)) return QUANTAPDF_OK;
    if(!direction(s->current,z,&u) || ++b->curve_segments>QP_STROKE_POINTS) return QUANTAPDF_ERROR_UNSUPPORTED;
    if(s->segments) { status=join_directions(b,s->current,s->last,u); if(status!=QUANTAPDF_OK) return status; }
    else s->first=u;
    status=segment(b,s->current,z,0,0); if(status!=QUANTAPDF_OK) return status;
    s->current=z; s->last=u; ++s->segments; return QUANTAPDF_OK;
}
static quantapdf_status solid_finish(stroke_builder *b,solid_subpath *s,int closed)
{
    quantapdf_status status;
    if(!s->segments) {
        /* A closed single-point subpath is a zero-length stroke: round caps
         * emit a bounded disk, butt/square paint nothing. This also applies
         * after one or more authored coincident LINEs. The open MOVE-only
         * subpath remains inert regardless of cap style. */
        if(closed) {
            status=zero_length_subpath(b,s->start);
            if(status==QUANTAPDF_OK) s->active=0;
            return status;
        }
        if(!s->had_line_command) {
            s->active=0;
            return QUANTAPDF_OK;
        }
        if(!closed && s->had_line_command) {
            status=zero_length_subpath(b,s->start);
            if(status==QUANTAPDF_OK) s->active=0;
            return status;
        }
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if(closed) {
        /* The same one-segment A→B→CLOSE rule applies when another
         * subpath makes the overall operation use the cubic compiler.
         * The first strip is already owned and validated by solid_line. */
        if(!s->curves && s->segments==1u && !same_point(s->current,s->start)) {
            if(b->source->dash_count!=0u ||
                (b->options->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL &&
                 b->options->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            if(b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_ROUND) {
                status=round_cap(b,s->start,s->first,1);
                if(status!=QUANTAPDF_OK) return status;
                status=round_cap(b,s->current,s->first,0);
                if(status!=QUANTAPDF_OK) return status;
            }
            s->active=0; return QUANTAPDF_OK;
        }
        status=solid_line(b,s,s->start); if(status!=QUANTAPDF_OK) return status;
        if(!s->curves && s->segments<3u) return QUANTAPDF_ERROR_UNSUPPORTED;
        status=join_directions(b,s->start,s->last,s->first);
    } else if(s->open_terminal_return) {
        /* The return was normalized without a duplicate reversing strip.
         * Butt caps at the coincident A endpoints emit nothing; B gets
         * only the qualified Bevel or bounded Round cusp join. */
        status=b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_ROUND ?
            round_cap(b,s->current,s->last,0) : QUANTAPDF_OK;
    } else {
        status=dash_cap(b,s->start,s->first,1); if(status!=QUANTAPDF_OK) return status;
        status=dash_cap(b,s->current,s->last,0);
    }
    s->active=0; return status;
}
static quantapdf_status curved_path(stroke_builder *b)
{
    solid_subpath s={0};
    for(size_t i=0;i<b->source->command_count;++i) {
        const quantapdf_composer_path_command *c=&b->source->commands[i];
        stroke_point p={0,0};
        quantapdf_status status;
        if(c->kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
           c->kind==QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           c->kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO)
            p=(stroke_point){(double)c->point1.x,(double)c->point1.y};
        if(c->kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO) {
            if(s.active) { status=solid_finish(b,&s,0); if(status!=QUANTAPDF_OK) return status; }
            if(!valid_point(p)) return QUANTAPDF_ERROR_UNSUPPORTED;
            memset(&s,0,sizeof(s)); s.active=1; s.start=s.current=p;
        } else if(c->kind==QUANTAPDF_COMPOSER_PATH_LINE_TO) {
            if(!s.active || !valid_point(p)) return QUANTAPDF_ERROR_UNSUPPORTED;
            s.had_line_command=1;
            /* Open exact A→B→A requires no duplicate strip. Accept only
             * when no additional segment follows before the next MOVE or
             * the end of the operation. A separate cubic may have selected
             * this private compiler, but cannot change the cusp policy. */
            if((i+1u==b->source->command_count ||
                b->source->commands[i+1u].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO) &&
               s.segments==1u && !s.curves &&
               same_point(p,s.start) && !same_point(s.current,s.start) &&
               b->source->dash_count==0u &&
               b->options->line_cap==QUANTAPDF_COMPOSER_LINE_CAP_BUTT &&
               (b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL ||
                b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_ROUND)) {
                s.open_terminal_return=1;
                continue;
            }
            /* Normalize only the terminal exact explicit return before
             * CLOSE. In a mixed cubic operation the first strip has already
             * been emitted, and solid_finish will add its Bevel/Round joins. */
            if(i+1u<b->source->command_count &&
                b->source->commands[i+1u].kind==QUANTAPDF_COMPOSER_PATH_CLOSE &&
                s.segments==1u && !s.curves &&
                same_point(p,s.start) && !same_point(s.current,s.start) &&
                b->source->dash_count==0u &&
                (b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL ||
                 b->options->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_ROUND))
                continue;
            status=solid_line(b,&s,p); if(status!=QUANTAPDF_OK) return status;
        } else if(c->kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO) {
            stroke_point controls[4]={s.current,p,{c->point2.x,c->point2.y},{c->point3.x,c->point3.y}},first,last;
            if(!s.active || !valid_point(p) || !valid_point(controls[2]) || !valid_point(controls[3])) return QUANTAPDF_ERROR_UNSUPPORTED;
            if(!(direction(controls[0],controls[1],&first) || direction(controls[0],controls[2],&first) || direction(controls[0],controls[3],&first)) ||
               !(direction(controls[2],controls[3],&last) || direction(controls[1],controls[3],&last) || direction(controls[0],controls[3],&last)))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            if(s.segments) { status=join_directions(b,s.current,s.last,first); if(status!=QUANTAPDF_OK) return status; }
            else s.first=first;
            status=cubic_strip(b,controls,first,last,0u); if(status!=QUANTAPDF_OK) return status;
            s.current=controls[3]; s.last=last; ++s.segments; s.curves=1;
        } else if(c->kind==QUANTAPDF_COMPOSER_PATH_CLOSE) {
            if(!s.active) return QUANTAPDF_ERROR_UNSUPPORTED;
            status=solid_finish(b,&s,1); if(status!=QUANTAPDF_OK) return status;
        } else return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return s.active ? solid_finish(b,&s,0) : QUANTAPDF_OK;
}

/* A monotone collinear cubic has exactly the same geometric trace and
 * arc length as its endpoint segment, even when its parameter speed varies.
 * This admits a narrow, mathematically exact subset of dashed cubics without
 * approximating curve length or silently flattening a genuinely curved path. */
static int cubic_is_monotone_collinear(stroke_point p0,stroke_point p1,
    stroke_point p2,stroke_point p3)
{
    const double dx=p3.x-p0.x,dy=p3.y-p0.y;
    if(!valid_point(p0) || !valid_point(p1) || !valid_point(p2) ||
        !valid_point(p3) || (dx==0.0 && dy==0.0))
        return 0;
    if(dx*(p1.y-p0.y)-dy*(p1.x-p0.x)!=0.0 ||
        dx*(p2.y-p0.y)-dy*(p2.x-p0.x)!=0.0)
        return 0;
    if(fabs(dx)>=fabs(dy)) {
        return dx>0.0 ?
            p0.x<=p1.x && p1.x<=p2.x && p2.x<=p3.x :
            p0.x>=p1.x && p1.x>=p2.x && p2.x>=p3.x;
    }
    return dy>0.0 ?
        p0.y<=p1.y && p1.y<=p2.y && p2.y<=p3.y :
        p0.y>=p1.y && p1.y>=p2.y && p2.y>=p3.y;
}

/* Normalize only exact monotone collinear cubic segments. Dash distances are
 * then measured on their exact line traces; every other cubic+dash pair
 * remains an atomic UNSUPPORTED result until curved arc-length is qualified. */
static quantapdf_status normalize_dashed_collinear_cubics(
    const quantapdf_composer_path_operation *source,
    quantapdf_composer_path_operation *normalized,
    quantapdf_composer_path_command **owned_commands)
{
    stroke_point current={0,0},start={0,0};
    int active=0;
    quantapdf_composer_path_command *commands;
    *owned_commands=NULL;
    commands=(quantapdf_composer_path_command *)calloc(
        source->command_count,sizeof(*commands));
    if(!commands) return QUANTAPDF_ERROR_NOMEM;
    memcpy(commands,source->commands,source->command_count*sizeof(*commands));
    for(size_t i=0;i<source->command_count;++i) {
        quantapdf_composer_path_command *c=&commands[i];
        if(c->kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO) {
            current=start=(stroke_point){c->point1.x,c->point1.y};
            if(!valid_point(current)) goto unsupported;
            active=1;
        } else if(c->kind==QUANTAPDF_COMPOSER_PATH_LINE_TO) {
            if(!active) goto unsupported;
            current=(stroke_point){c->point1.x,c->point1.y};
            if(!valid_point(current)) goto unsupported;
        } else if(c->kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO) {
            stroke_point p1={c->point1.x,c->point1.y};
            stroke_point p2={c->point2.x,c->point2.y};
            stroke_point end={c->point3.x,c->point3.y};
            if(!active || !cubic_is_monotone_collinear(current,p1,p2,end))
                goto unsupported;
            c->kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
            c->point1=c->point3;
            current=end;
        } else if(c->kind==QUANTAPDF_COMPOSER_PATH_CLOSE) {
            if(!active) goto unsupported;
            current=start;
            active=0;
        } else {
            goto unsupported;
        }
    }
    *normalized=*source;
    normalized->commands=commands;
    *owned_commands=commands;
    return QUANTAPDF_OK;
unsupported:
    free(commands);
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

void quantapdf_ir_stroke_polyline_dispose_v1(quantapdf_composer_path_operation *fill)
{
    if (!fill) return;
    free(fill->commands); memset(fill,0,sizeof(*fill));
}
static quantapdf_status outline_impl(
    const quantapdf_composer_path_operation *source,size_t max_edges,const double *device,
    quantapdf_composer_path_operation *out_fill)
{
    stroke_builder b={0};
    stroke_point points[QP_STROKE_POINTS];
    quantapdf_composer_path_operation normalized={0};
    quantapdf_composer_path_command *normalized_commands=NULL;
    size_t n=0;
    int zero_length_segment=0;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;
    const quantapdf_composer_path_options *o;
    if (!out_fill) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_fill,0,sizeof(*out_fill));
    if (!source || max_edges==0u) return QUANTAPDF_ERROR_ARGUMENT;
    o=&source->options;
    if (!source->commands || source->command_count==0u ||
        (source->command_count==1u &&
         source->commands[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO) ||
        source->command_count>QP_STROKE_POINTS ||
        o->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
        o->stroke!=1 || o->fill!=0 || (o->stroke_argb>>24)!=255u ||
        !isfinite(o->stroke_width) || o->stroke_width<=0 || o->stroke_width>2048 ||
        !isfinite(o->miter_limit) || o->miter_limit<1 || o->miter_limit>1000 ||
        (o->line_cap!=QUANTAPDF_COMPOSER_LINE_CAP_BUTT &&
         o->line_cap!=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE &&
         o->line_cap!=QUANTAPDF_COMPOSER_LINE_CAP_ROUND) ||
        (o->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_MITER &&
         o->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL &&
         o->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND) ||
        source->dash_count>QP_DASH_ITEMS || !isfinite(source->dash_phase) ||
        source->dash_phase<0 || source->dash_phase>1000000000.0f ||
        (source->dash_count==0u && source->dash_phase!=0) ||
        (source->dash_count!=0u && source->dash_lengths==NULL) ||
        (o->struct_size>=QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE && o->graphics_state_id!=0) ||
        (o->struct_size>=QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE &&
         (o->fill_paint_id!=0 || o->stroke_paint_id!=0)))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(o->line_cap==QUANTAPDF_COMPOSER_LINE_CAP_ROUND || o->line_join==QUANTAPDF_COMPOSER_LINE_JOIN_ROUND) {
        b.arc_scale=arc_scale(o,device);
        if(b.arc_scale<=0.0) return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    for(size_t i=0;i<source->dash_count;++i) {
        double v=source->dash_lengths[i];
        if(!isfinite(v) || v<0.0 || v>QP_STROKE_COORD) return QUANTAPDF_ERROR_UNSUPPORTED;
        b.dash_period+=v;
    }
    if(source->dash_count && b.dash_period==0.0) return QUANTAPDF_ERROR_UNSUPPORTED;
    if(source->dash_count%2u) b.dash_period*=2.0;
    b.source=source;
    b.commands=calloc(QP_STROKE_COMMANDS,sizeof(*b.commands));
    if (!b.commands) return QUANTAPDF_ERROR_NOMEM;
    b.max_edges=max_edges; b.options=o;
    for(size_t i=0;i<source->command_count;++i)
        if(source->commands[i].kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO) b.curve_mode=1;
    if(b.curve_mode && source->dash_count) {
        status=normalize_dashed_collinear_cubics(
            source,&normalized,&normalized_commands);
        if(status!=QUANTAPDF_OK) goto fail;
        source=&normalized;
        b.source=source;
        o=&source->options;
        b.options=o;
        b.curve_mode=0;
    } else if(b.curve_mode) {
        b.arc_scale=arc_scale(o,device);
        if(b.arc_scale<=0.0) goto fail;
        status=curved_path(&b); if(status!=QUANTAPDF_OK) goto fail;
        goto publish;
    }
    for (size_t i=0;i<source->command_count;++i) {
        const quantapdf_composer_path_command *c=&source->commands[i];
        stroke_point p={0,0};
        if(c->kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO || c->kind==QUANTAPDF_COMPOSER_PATH_LINE_TO)
            p=(stroke_point){(double)c->point1.x,(double)c->point1.y};
        if (c->kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO) {
            if (n) {
                /* An unpainted MOVE is valid, including before another
                 * subpath; a repeated LINE_TO uses the degenerate cap path. */
                if(n>1u || zero_length_segment) {
                    status=n==1u ? zero_length_subpath(&b,points[0]) :
                        subpath(&b,points,n,0);
                    if(status!=QUANTAPDF_OK) goto fail;
                }
            }
            if (!valid_point(p)) goto fail;
            n=1u; points[0]=p; zero_length_segment=0;
        } else if (c->kind==QUANTAPDF_COMPOSER_PATH_LINE_TO) {
            if (!n || !valid_point(p)) goto fail;
            if (!same_point(points[n-1u],p)) points[n++]=p;
            else zero_length_segment=1;
        } else if (c->kind==QUANTAPDF_COMPOSER_PATH_CLOSE) {
            status=subpath(&b,points,n,1); if(status!=QUANTAPDF_OK) goto fail;
            n=0; zero_length_segment=0;
        } else goto fail;
    }
    if(n) {
        if(n>1u || zero_length_segment) {
            status=n==1u ? zero_length_subpath(&b,points[0]) :
                subpath(&b,points,n,0);
            if(status!=QUANTAPDF_OK) goto fail;
        }
    }
publish:
    if (b.count==0) {
        /* A validated path entirely inside an off interval has exactly empty
         * coverage. Opposite directed edges encode that zero-winding fill,
         * while still passing normal transform/Clip/budget validation. */
        b.commands[0]=source->commands[0];
        int found_distinct=0;
        for(size_t i=1;i<source->command_count;++i) {
            if(source->commands[i].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
                (source->commands[i].point1.x!=b.commands[0].point1.x ||
                 source->commands[i].point1.y!=b.commands[0].point1.y)) {
                b.commands[1]=source->commands[i];
                found_distinct=1;
                break;
            }
        }
        if(!found_distinct) {
            const quantapdf_point at=b.commands[0].point1;
            b.commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
            b.commands[1].point1=(quantapdf_point){
                at.x<=QP_STROKE_COORD-1.0f ? at.x+1.0f : at.x-1.0f,at.y};
        }
        if(b.commands[1].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO || max_edges<2u) goto fail;
        b.commands[2].kind=QUANTAPDF_COMPOSER_PATH_CLOSE; b.count=3u;
    }
    out_fill->commands=b.commands; out_fill->command_count=b.count;
    out_fill->options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    out_fill->options.fill=1; out_fill->options.fill_argb=o->stroke_argb;
    out_fill->options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    out_fill->options.transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    if(o->struct_size>=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE)
        out_fill->options.transform=o->transform;
    free(normalized_commands);
    return QUANTAPDF_OK;
fail:
    free(normalized_commands);
    free(b.commands);
    return status==QUANTAPDF_OK ? QUANTAPDF_ERROR_UNSUPPORTED : status;
}

quantapdf_status quantapdf_ir_stroke_polyline_outline_v1(
    const quantapdf_composer_path_operation *source,size_t max_edges,
    quantapdf_composer_path_operation *out_fill)
{
    return outline_impl(source,max_edges,NULL,out_fill);
}
quantapdf_status quantapdf_ir_stroke_polyline_outline_v1_planned(
    const quantapdf_composer_path_operation *source,size_t max_edges,
    const quantapdf_native_device_plan *plan,quantapdf_composer_path_operation *out_fill)
{
    if(!out_fill) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_fill,0,sizeof(*out_fill));
    if(!plan) return QUANTAPDF_ERROR_ARGUMENT;
    if(!quantapdf_native_device_plan_matches_page(plan,(float)plan->width_points,
        (float)plan->height_points,QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS)) return QUANTAPDF_ERROR_UNSUPPORTED;
    return outline_impl(source,max_edges,plan->from_composer,out_fill);
}
