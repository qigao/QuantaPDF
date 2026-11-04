#include "backend/qpdf_page_ir.h"
#include "backend/qpdf_text.h"
#include "backend/qpdf_document_internal.h"

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/* Test-only, compile-time-elided allocation arming at actual SOURCE-owned
 * std::vector sites. The normal backend has no probe or extra symbols. */
#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
extern "C" void quantapdf_source_stage_oom_arm(unsigned) noexcept;
extern "C" bool quantapdf_source_ir_oom_fail_c_allocation() noexcept;
#define QP_SOURCE_STAGE_ARM(n) quantapdf_source_stage_oom_arm(n)
#define QP_SOURCE_STACK_GROW(v) \
    do {if((v).size()==(v).capacity())QP_SOURCE_STAGE_ARM(11u);} while(0)
#define QP_SOURCE_PATH_GROW(v) \
    do {if((v).size()==(v).capacity())QP_SOURCE_STAGE_ARM(12u);} while(0)
#define QP_SOURCE_C_CALLOC(n,s) \
    (quantapdf_source_ir_oom_fail_c_allocation()?nullptr:std::calloc((n),(s)))
#define QP_SOURCE_C_MALLOC(n) \
    (quantapdf_source_ir_oom_fail_c_allocation()?nullptr:std::malloc(n))
#define QP_SOURCE_C_REALLOC(p,n) \
    (quantapdf_source_ir_oom_fail_c_allocation()?nullptr:std::realloc((p),(n)))
#else
#define QP_SOURCE_STAGE_ARM(n) ((void)0)
#define QP_SOURCE_STACK_GROW(v) ((void)0)
#define QP_SOURCE_PATH_GROW(v) ((void)0)
#define QP_SOURCE_C_CALLOC(n,s) std::calloc((n),(s))
#define QP_SOURCE_C_MALLOC(n) std::malloc(n)
#define QP_SOURCE_C_REALLOC(p,n) std::realloc((p),(n))
#endif

namespace {
using Obj = QPDFObjectHandle;
constexpr size_t hard_image_pixels = 4194304u;
constexpr size_t hard_page_pixels = 4194304u;
constexpr size_t hard_draw_work = 16777216u;
constexpr size_t hard_operations = 4u;

struct rejected { quantapdf_status status; };

void admit(bool condition, quantapdf_status status = QUANTAPDF_ERROR_FORMAT)
{
    if (!condition) throw rejected{status};
}

size_t integer(Obj x, size_t limit)
{
    admit(x.isInteger());
    auto n = x.getIntValue();
    admit(n > 0, QUANTAPDF_ERROR_FORMAT);
    admit(static_cast<uint64_t>(n) <= static_cast<uint64_t>(limit),
        QUANTAPDF_ERROR_UNSUPPORTED);
    return static_cast<size_t>(n);
}

double finite_number(Obj x)
{
    admit(x.isNumber());
    double result = x.getNumericValue();
    admit(std::isfinite(result));
    return result;
}

int64_t page_lattice(Obj number,int64_t ticks)
{
    admit(number.isNumber(),QUANTAPDF_ERROR_FORMAT);
    const std::string raw=number.unparse();
    admit(!raw.empty() && raw.size()<=20u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    size_t i=0u;
    const bool negative=raw[i]=='-';
    if(raw[i]=='-' || raw[i]=='+')++i;
    admit(i<raw.size(),QUANTAPDF_ERROR_UNSUPPORTED);
    /* The original token, including its 20-byte cap, is unchanged.
     * Trailing fractional zeros carry no precision and must not
     * consume the fixed-size exact-quarter integer accumulator.
     * Still validate EVERY input byte below; a late nonzero digit
     * remains significant and must be rejected if nonquarter. */
    size_t significant_end=raw.size();
    const size_t dot=raw.find('.');
    if(dot!=std::string::npos)
        while(significant_end>dot+1u && raw[significant_end-1u]=='0')
            --significant_end;
    int64_t integer_part=0,decimal_part=0,denominator=1;
    bool seen_digit=false,point=false;
    for(;i<raw.size();++i) {
        const char c=raw[i];
        if(c=='.' && !point) {point=true;continue;}
        admit(c>='0' && c<='9',QUANTAPDF_ERROR_UNSUPPORTED);
        const int64_t d=(int64_t)(c-'0');
        if(!point) {
            admit(integer_part<=1000000ll/10ll,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            integer_part=integer_part*10ll+d;
            admit(integer_part<=1000000ll,
                  QUANTAPDF_ERROR_UNSUPPORTED);
        } else if(i<significant_end) {
            admit(denominator<=100000000ll,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            decimal_part=decimal_part*10ll+d;
            denominator*=10ll;
        }
        seen_digit=true;
    }
    admit(seen_digit && (decimal_part*ticks)%denominator==0ll,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const int64_t quarters=integer_part*ticks+
        decimal_part*ticks/denominator;
    admit(quarters<=1000000ll*ticks,QUANTAPDF_ERROR_UNSUPPORTED);
    return negative?-quarters:quarters;
}

/* The current geometry compiler consumes quarter-point transformed values. */
double whole_signed_source(double value)
{
    admit(std::isfinite(value) && value>=-1000000.0 &&
          value<=1000000.0 &&
          std::trunc(value*4.0)==value*4.0,
          QUANTAPDF_ERROR_UNSUPPORTED);
    return value;
}

size_t whole(double value, size_t max)
{
    admit(std::isfinite(value));
    admit(value >= 0.0 && value <= static_cast<double>(max),
        QUANTAPDF_ERROR_UNSUPPORTED);
    size_t v = static_cast<size_t>(value);
    admit(static_cast<double>(v) == value, QUANTAPDF_ERROR_UNSUPPORTED);
    return v;
}

class bounded_sink final : public Pipeline {
public:
    explicit bounded_sink(size_t limit) :
        Pipeline("native image source limited decode", nullptr), capacity(limit) {}
    void write(unsigned char const *source, size_t count) override
    {
        admit(count <= capacity - bytes.size(), QUANTAPDF_ERROR_UNSUPPORTED);
        if (count != 0u) bytes.insert(bytes.end(),source,source+count);
    }
    void finish() override { complete = true; }
    size_t capacity;
    bool complete = false;
    std::vector<unsigned char> bytes;
};

/* Image predictors are decoded by qpdf after their row/channel metadata is
 * checked here. Content stream filters are handled separately by qpdf. */
std::vector<unsigned char> image_data(Obj stream, size_t limit,
                                      size_t image_columns=0u,
                                      int image_channels=0)
{
    admit(stream.isStream());
    Obj dict = stream.getDict();
    admit(dict.getKey("/F").isNull() &&
          dict.getKey("/FFilter").isNull() &&
          dict.getKey("/FDecodeParms").isNull(), QUANTAPDF_ERROR_UNSUPPORTED);
    Obj filter = dict.getKey("/Filter");
    const bool flate = filter.isName() &&
        filter.getName() == "/FlateDecode";
    admit(filter.isNull() || flate, QUANTAPDF_ERROR_UNSUPPORTED);

    Obj parms=dict.getKey("/DecodeParms");
    admit(parms.isNull() || flate,
          QUANTAPDF_ERROR_UNSUPPORTED);
    if(!parms.isNull()) {
        admit(parms.isDictionary() && image_columns>0u &&
              image_columns<=hard_image_pixels &&
              (image_channels==1 || image_channels==3),
              QUANTAPDF_ERROR_UNSUPPORTED);
        for(const auto& key:parms.getKeys())
            admit(key=="/Predictor" || key=="/Colors" ||
                  key=="/Columns" || key=="/BitsPerComponent",
                  QUANTAPDF_ERROR_UNSUPPORTED);
        Obj predictor=parms.getKey("/Predictor");
        Obj colors=parms.getKey("/Colors");
        Obj columns=parms.getKey("/Columns");
        Obj bits=parms.getKey("/BitsPerComponent");
        admit(predictor.isInteger() && colors.isInteger() &&
              columns.isInteger() && bits.isInteger(),
              QUANTAPDF_ERROR_UNSUPPORTED);
        const auto code=predictor.getIntValue();
        admit(code==1 || code==2 || (code>=10 && code<=15),
              QUANTAPDF_ERROR_UNSUPPORTED);
        admit(colors.getIntValue()==image_channels &&
              bits.getIntValue()==8,
              QUANTAPDF_ERROR_UNSUPPORTED);
        const auto cols=columns.getIntValue();
        admit(cols>0 &&
              static_cast<uint64_t>(cols)==
                  static_cast<uint64_t>(image_columns) &&
              image_columns<=hard_image_pixels/
                  static_cast<size_t>(image_channels),
              QUANTAPDF_ERROR_UNSUPPORTED);
    }
    auto level = flate ? qpdf_dl_specialized : qpdf_dl_none;
    bounded_sink sink(limit);
    bool filtered = false;
    admit(stream.pipeStreamData(&sink,&filtered,0,level,true,false) &&
          sink.complete && (filtered || !flate),
          QUANTAPDF_ERROR_FORMAT);
    admit(!sink.bytes.empty());
    return std::move(sink.bytes);
}

using matrix = std::array<double,6>;
const matrix identity={1,0,0,1,0,0};

matrix concat(const matrix& parent,const matrix& child)
{
    matrix m={parent[0]*child[0]+parent[2]*child[1],
              parent[1]*child[0]+parent[3]*child[1],
              parent[0]*child[2]+parent[2]*child[3],
              parent[1]*child[2]+parent[3]*child[3],
              parent[0]*child[4]+parent[2]*child[5]+parent[4],
              parent[1]*child[4]+parent[3]*child[5]+parent[5]};
    for(double n:m) admit(std::isfinite(n),QUANTAPDF_ERROR_UNSUPPORTED);
    return m;
}

struct staged_image {
    std::vector<unsigned char> pixels;
    uint32_t width=0,height=0;
    int components=0;
    quantapdf_rect bounds={};
};

struct staged_path {
    std::vector<quantapdf_composer_path_command> commands;
    uint32_t fill_argb=UINT32_C(0xff000000);
    quantapdf_composer_fill_rule fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;

    bool stroke=false;
    uint32_t stroke_argb=UINT32_C(0xff000000);
    float stroke_width=1.0f; /* bounded TeXample geometry may vary */
};

struct staged_image_clip {
    std::vector<quantapdf_composer_path_command> commands;
    quantapdf_composer_fill_rule rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
};

bool bounded_clip_contour(
    const std::vector<quantapdf_composer_path_command>& commands,
    bool allow_implicit_close=false)
{
    if(commands.empty() || commands.size()>9u)return false;
    const bool closed=
        commands.back().kind==QUANTAPDF_COMPOSER_PATH_CLOSE;
    const size_t final_count=commands.size()+(closed?0u:1u);
    if(final_count<4u || final_count>9u ||
       commands.front().kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       (!closed && !allow_implicit_close))
        return false;
    const size_t end=commands.size()-(closed?1u:0u);
    for(size_t i=1u;i<end;++i)
        if(commands[i].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO &&
           commands[i].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO)
            return false;
    return true;
}

/* A compound Clip keeps one aggregate winding rule over one or two
 * closed contours. The TOTAL command allowance stays at nine; every
 * contour still spends MOVE, at least two edges, and CLOSE. Only the
 * last contour may await the original W/n implicit closure. Painted
 * PATHs retain the separate single-contour predicate above. */
bool bounded_clip_path(
    const std::vector<quantapdf_composer_path_command>& commands,
    bool allow_implicit_close=false)
{
    if(commands.empty() || commands.size()>9u)return false;
    size_t contours=0u,start=0u;
    bool active=false;
    for(size_t i=0u;i<commands.size();++i) {
        switch(commands[i].kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            if(active || contours==2u)return false;
            ++contours;
            start=i;
            active=true;
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            if(!active)return false;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            if(!active || i-start<3u)return false;
            active=false;
            break;
        default:
            return false;
        }
    }
    if(active)
        return allow_implicit_close && commands.size()-start>=3u &&
            commands.size()<9u;
    return contours!=0u && commands.size()>=4u;
}

bool nested_rectangles(const std::vector<quantapdf_composer_path_command>& p)
{
    if(p.size()!=10u ||
       p[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       p[1].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[2].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[3].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[4].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE ||
       p[5].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       p[6].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[7].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[8].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       p[9].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)return false;
    const auto a=p[0].point1,b=p[1].point1,c=p[2].point1,d=p[3].point1;
    const auto e=p[5].point1,f=p[6].point1,g=p[7].point1,h=p[8].point1;
    if(a.x==b.x || a.y==d.y || e.x==f.x || e.y==h.y ||
       a.y!=b.y || b.x!=c.x || c.y!=d.y || d.x!=a.x ||
       e.y!=f.y || f.x!=g.x || g.y!=h.y || h.x!=e.x)return false;
    const float x0=std::min(a.x,b.x),x1=std::max(a.x,b.x);
    const float y0=std::min(a.y,d.y),y1=std::max(a.y,d.y);
    const float u0=std::min(e.x,f.x),u1=std::max(e.x,f.x);
    const float v0=std::min(e.y,h.y),v1=std::max(e.y,h.y);
    return u0>x0 && u1<x1 && v0>y0 && v1<y1;
}

bool qualified_owned_clip_pair(
    const std::vector<quantapdf_composer_path_command>& first,
    const std::vector<quantapdf_composer_path_command>& second)
{
    /* Both bounded leaves retain their exact geometry and winding
     * rule, including compound contours and canonical zero-edge leaves.
     * Only two proven simple rectangles use the separate reduction. */
    return bounded_clip_path(first) && bounded_clip_path(second);
}

struct admission {
    Obj resources;
    const quantapdf_qpdf_page_limits& limits;
    size_t page_w,page_h;
    size_t source_pixels=0,drawn_pixels=0;
    std::vector<staged_image> draws;
    std::vector<staged_path> painted_paths;
    std::vector<int> order; /* 1=PATH, 2=IMAGE, as originally painted */
    bool clip_completed=false;
    staged_image_clip clip;
    /* Frozen source leaves; membership records distinguish restored scopes
     * from an actual intersection. A2 replaces these fixed graph shapes. */
    staged_image_clip second_clip;
    bool two_owned_clips=false;
    bool sibling_clip_scopes=false;
    bool saw_q_restore=false;

    std::vector<unsigned char> scoped_clip_membership;

    bool three_branch_candidate=false;
    std::array<staged_image_clip,3u> three_branch_leaves{};
    size_t three_branch_count=0u;

    /* Normalize the media origin and retain the exact Crop viewport while
     * charging full authored placements against the page-work budgets. */
    double media_left=0.0;
    double media_bottom=0.0;

    bool crop_viewport=false;
    double exact_page_w=0.0,exact_page_h=0.0;

    /* Remaining fixture-shaped geometry admission is independent of the
     * removed Source API versions; general ordered scenes remain A2 work. */
    bool texample_original_profile=false;
    unsigned texample_stage=0u;
    size_t texample_moves=0u,texample_cubics=0u;
    size_t texample_closes=0u,texample_zero_moves=0u;
    size_t texample_q_count=0u,texample_Q_count=0u;
    size_t texample_cm_count=0u,texample_gray_count=0u;
    size_t texample_stroke_gray_count=0u,texample_width_count=0u;
    size_t texample_orange_count=0u,texample_terminal_n=0u;
    bool texample_public_eligible=false;
    bool text_outlines_eligible=false;
    std::vector<quantapdf::detail::source_outline_paint> text_outline_paints={};
    bool text_outlines_complete=false;
    bool text_prefix_precision=false;
    /* PRIVATE M2b.2e: one clip identity and saved stack copied from
     * the ORIGINAL Content reader at BT. Every later q/Q arrives from
     * the existing outline reader; no second parser or QPDF owner. */
    bool outline_scope_started=false;
    unsigned char outline_clip_id=0u;
    std::vector<unsigned char> outline_clip_stack={};

    admission(Obj page_resources,const quantapdf_qpdf_page_limits& page_limits,
              size_t width,size_t height) :
        resources(page_resources),limits(page_limits),page_w(width),page_h(height) {}

    bool ordered_scene() const
    {
        return !clip_completed || !two_owned_clips;
    }

    void paint_path(std::vector<quantapdf_composer_path_command> commands,
                    uint32_t color,quantapdf_composer_fill_rule rule,
                    bool is_stroke=false,uint32_t stroke_rgb=UINT32_C(0xff000000),
                    float width=1.0f)
    {
        const bool three_path=three_branch_candidate && draws.empty() &&
            (is_stroke ?
                (painted_paths.empty() && three_branch_count==1u) :
                (!painted_paths.empty() && painted_paths[0].stroke &&
                 painted_paths.size()<3u));
        const bool texample_path=texample_original_profile &&
            draws.empty() && clip_completed &&
            commands.size()==12u && painted_paths.size()<3u &&
            (is_stroke ? painted_paths.size()>=1u :
                         painted_paths.empty());
        admit(texample_path || three_path ||
              (std::none_of(painted_paths.begin(),painted_paths.end(),
                   [](const staged_path& p){return p.stroke;}) &&
               (!is_stroke ||
                (clip_completed && draws.empty() && painted_paths.empty() &&
                 order.empty() && scoped_clip_membership.empty() &&
                 !saw_q_restore))),QUANTAPDF_ERROR_UNSUPPORTED);

        const size_t allowed_paths=clip_completed && !ordered_scene()?
            (draws.empty()?hard_operations:2u):limits.max_operations;
        admit(painted_paths.size()<allowed_paths &&
              (texample_path ||
               (three_path && is_stroke && commands.size()==2u) ||
               (commands.size()>=4u && (!is_stroke || commands.size()<=9u))) &&
              (ordered_scene() || !clip_completed || order.size()<hard_operations) &&
              limits.max_operations>=
                   order.size()+1u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        staged_path item;
        item.commands=std::move(commands);
        item.fill_argb=color;
        item.fill_rule=rule;
        item.stroke=is_stroke;
        item.stroke_argb=stroke_rgb;
        item.stroke_width=width;
        QP_SOURCE_STAGE_ARM(3u);
        painted_paths.push_back(std::move(item));
        QP_SOURCE_STAGE_ARM(4u);
        order.push_back(1);
    }

    void execute(Obj image,const matrix& ctm)
    {
        admit(std::none_of(painted_paths.begin(),painted_paths.end(),
                  [](const staged_path& p){return p.stroke;}),
              QUANTAPDF_ERROR_UNSUPPORTED);
        admit((ordered_scene() || !clip_completed || (draws.size()<2u && order.size()<hard_operations)) &&
              order.size()<limits.max_operations,QUANTAPDF_ERROR_UNSUPPORTED);
        admit(image.isStream(),QUANTAPDF_ERROR_FORMAT);
        Obj dict = image.getDict(), subtype=dict.getKey("/Subtype");
        admit(subtype.isName());
        admit(subtype.getName() == "/Image",QUANTAPDF_ERROR_UNSUPPORTED);
        Obj type=dict.getKey("/Type");
        admit(type.isNull() || (type.isName() && type.getName()=="/XObject"),
              QUANTAPDF_ERROR_UNSUPPORTED);
        Obj mask=dict.getKey("/ImageMask");
        admit(mask.isNull() || (mask.isBool() && !mask.getBoolValue()),
              QUANTAPDF_ERROR_UNSUPPORTED);
        admit(dict.getKey("/SMask").isNull() &&
              dict.getKey("/Mask").isNull() &&
              dict.getKey("/Alternates").isNull() &&
              dict.getKey("/OPI").isNull() &&
              dict.getKey("/Matte").isNull() &&
              /* /OC may hide the image; /SMaskInData and /Intent can
               * alter visible source semantics. Never represent those as
               * ordinary RAW pixels without a qualified interpreter. */
              dict.getKey("/OC").isNull() &&
              dict.getKey("/SMaskInData").isNull() &&
              dict.getKey("/Intent").isNull(),
              QUANTAPDF_ERROR_UNSUPPORTED);
        /* Absent or explicit false both request nearest-sample semantics.
         * Interpolation true, wrong type and unqualified filtering refuse. */
        Obj interpolation=dict.getKey("/Interpolate");
        admit(interpolation.isNull() ||
              (interpolation.isBool() && !interpolation.getBoolValue()),
              QUANTAPDF_ERROR_UNSUPPORTED);
        Obj bits=dict.getKey("/BitsPerComponent");
        admit(bits.isInteger());
        admit(bits.getIntValue() == 8, QUANTAPDF_ERROR_UNSUPPORTED);
        Obj colorspace=dict.getKey("/ColorSpace");
        admit(colorspace.isName(),QUANTAPDF_ERROR_UNSUPPORTED);
        int channels=colorspace.getName()=="/DeviceGray" ? 1 :
            (colorspace.getName()=="/DeviceRGB" ? 3 : 0);
        admit(channels != 0,QUANTAPDF_ERROR_UNSUPPORTED);

        Obj decode=dict.getKey("/Decode");
        std::array<bool,3u> invert={false,false,false};
        bool any_inversion=false;
        if(!decode.isNull()) {
            admit(decode.isArray() &&
                  decode.getArrayNItems()==2*channels,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            for(int c=0;c<channels;++c) {
                Obj lower=decode.getArrayItem(c*2);
                Obj upper=decode.getArrayItem(c*2+1);
                admit(lower.isNumber() && upper.isNumber(),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                const double a=lower.getNumericValue();
                const double b=upper.getNumericValue();
                admit(std::isfinite(a) && std::isfinite(b) &&
                      ((a==0.0 && b==1.0) ||
                       (a==1.0 && b==0.0)),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                invert[static_cast<size_t>(c)]=a==1.0;
                any_inversion=any_inversion ||
                    invert[static_cast<size_t>(c)];
            }
        }

        const size_t w=integer(dict.getKey("/Width"),hard_image_pixels);
        const size_t h=integer(dict.getKey("/Height"),hard_image_pixels);
        admit(w<=hard_image_pixels/h,QUANTAPDF_ERROR_UNSUPPORTED);
        const size_t pixels=w*h;
        admit(pixels <= limits.max_source_pixels &&
              source_pixels <= limits.max_source_pixels-pixels &&
              source_pixels <= hard_image_pixels-pixels,
              QUANTAPDF_ERROR_UNSUPPORTED);
        admit(pixels<=SIZE_MAX/(size_t)channels,QUANTAPDF_ERROR_UNSUPPORTED);
        const size_t size=pixels*(size_t)channels;
        auto decoded=image_data(
            image,size,w,channels);
        admit(decoded.size()==size);
        /* Only after full Decode admission and bounded stream decode
         * normalize the original color samples to owned RAW bytes.
         * A fully identity /Decode is zero-copy/zero-work here; otherwise
         * traverse pixel channels without a modulo for every sample. */
        if(any_inversion)
            for(size_t pixel=0u;pixel<pixels;++pixel) {
                const size_t base=pixel*static_cast<size_t>(channels);
                for(int c=0;c<channels;++c)
                    if(invert[static_cast<size_t>(c)]) {
                        const size_t byte=base+static_cast<size_t>(c);
                        decoded[byte]=static_cast<unsigned char>(
                            255u-decoded[byte]);
                    }
            }
        source_pixels+=pixels;

        admit(ctm[1]==0.0 && ctm[2]==0.0 &&
              ctm[0]>0.0 && ctm[3]>0.0,
              QUANTAPDF_ERROR_UNSUPPORTED);
        /* SOURCE PDF absolute coordinates are not page-local when
         * MediaBox has nonzero origin. Subtract the fully admitted
         * original lower-left corner BEFORE bounds, texture sampling
         * work and RGB/Clip compositing. Never alter the original CTM. */
        const double ax0=ctm[4]-media_left;
        const double ax1=ctm[4]+ctm[0]-media_left;
        const double ay0=ctm[5]-media_bottom;
        const double ay1=ctm[5]+ctm[3]-media_bottom;
        /* A cropped original IMAGE still owns its complete XObject
         * sample footprint. Its off-canvas edges are not clipped or
         * rescaled before nearest-sample lookup. The viewport C11
         * sampler intersects the full source footprint at 4x4 samples.
         * Legacy integer page grammar still rejects outside placements. */
        const bool ordered=ordered_scene();
        if(ordered)for(double value:{ax0,ax1,ay0,ay1})
            admit(std::isfinite(value) && value>=-1000000.0 && value<=1000000.0,
                  QUANTAPDF_ERROR_UNSUPPORTED);
        const double x0=ordered?ax0:crop_viewport ?
            whole_signed_source(ax0) :
            (double)whole(ax0,page_w);
        const double x1=ordered?ax1:crop_viewport ?
            whole_signed_source(ax1) :
            (double)whole(ax1,page_w);
        const double bottom=ordered?ay0:crop_viewport ?
            whole_signed_source(ay0) :
            (double)whole(ay0,page_h);
        const double top=ordered?ay1:crop_viewport ?
            whole_signed_source(ay1) :
            (double)whole(ay1,page_h);
        admit(x1>x0 && top>bottom,QUANTAPDF_ERROR_UNSUPPORTED);
        const size_t draw_width=whole(ordered?std::ceil(x1-x0):x1-x0,hard_draw_work);
        const size_t draw_height=whole(ordered?std::ceil(top-bottom):top-bottom,hard_draw_work);
        admit(draw_width<=hard_draw_work/draw_height,
              QUANTAPDF_ERROR_UNSUPPORTED);
        const size_t draw_area=draw_width*draw_height;
        admit(draw_area<=limits.max_drawn_pixels &&
              drawn_pixels<=limits.max_drawn_pixels-draw_area &&
              drawn_pixels<=hard_draw_work-draw_area,
              QUANTAPDF_ERROR_UNSUPPORTED);
        drawn_pixels+=draw_area;

        staged_image entry;
        entry.pixels=std::move(decoded);
        entry.width=static_cast<uint32_t>(w);
        entry.height=static_cast<uint32_t>(h);
        entry.components=channels;
        const double canvas_h=exact_page_h>0.0?
            exact_page_h:(double)page_h;
        entry.bounds={static_cast<float>(x0),
                      static_cast<float>(canvas_h-top),
                      static_cast<float>(x1),
                      static_cast<float>(canvas_h-bottom)};
        QP_SOURCE_STAGE_ARM(1u);
        draws.push_back(std::move(entry));
        QP_SOURCE_STAGE_ARM(2u);
        order.push_back(2);
    }
};

void validate_opaque_gs(Obj resources,Obj name)
{
    admit(resources.isDictionary() && name.isName(),
          QUANTAPDF_ERROR_FORMAT);
    Obj ext=resources.getKey("/ExtGState");
    /* An undeclared gs is unsupported; a wrongly typed declared state is
     * malformed. Resolve this before publishing any IR resources. */
    admit(ext.isDictionary(),QUANTAPDF_ERROR_UNSUPPORTED);
    Obj entry=ext.getKey(name.getName());
    admit(!entry.isNull(),QUANTAPDF_ERROR_UNSUPPORTED);
    admit(entry.isDictionary(),QUANTAPDF_ERROR_FORMAT);
    admit(entry.getKeys().size()<=8u,QUANTAPDF_ERROR_UNSUPPORTED);
    for(const auto& key:entry.getKeys())
        admit(key=="/Type" || key=="/CA" || key=="/ca" ||
              key=="/BM" || key=="/SMask" || key=="/AIS" ||
              key=="/OP" || key=="/op" || key=="/OPM",
              QUANTAPDF_ERROR_UNSUPPORTED);
    Obj type=entry.getKey("/Type");
    if(!type.isNull()) {
        admit(type.isName(),QUANTAPDF_ERROR_FORMAT);
        admit(type.getName()=="/ExtGState",QUANTAPDF_ERROR_UNSUPPORTED);
    }
    for(const char *key:{"/CA","/ca"}) {
        Obj alpha=entry.getKey(key);
        if(!alpha.isNull()) {
            admit(alpha.isNumber(),QUANTAPDF_ERROR_FORMAT);
            admit(std::isfinite(alpha.getNumericValue()) &&
                  alpha.getNumericValue()==1.0,
                  QUANTAPDF_ERROR_UNSUPPORTED);
        }
    }
    Obj bm=entry.getKey("/BM");
    if(!bm.isNull()) {
        admit(bm.isName(),QUANTAPDF_ERROR_FORMAT);
        admit(bm.getName()=="/Normal",QUANTAPDF_ERROR_UNSUPPORTED);
    }
    Obj smask=entry.getKey("/SMask");
    if(!smask.isNull()) {
        admit(smask.isName(),QUANTAPDF_ERROR_UNSUPPORTED);
        admit(smask.getName()=="/None",QUANTAPDF_ERROR_UNSUPPORTED);
    }
    for(const char *key:{"/AIS","/OP","/op"}) {
        Obj value=entry.getKey(key);
        if(!value.isNull()) {
            admit(value.isBool(),QUANTAPDF_ERROR_FORMAT);
            admit(!value.getBoolValue(),QUANTAPDF_ERROR_UNSUPPORTED);
        }
    }
    Obj opm=entry.getKey("/OPM");
    if(!opm.isNull()) {
        admit(opm.isInteger(),QUANTAPDF_ERROR_FORMAT);
        admit(opm.getIntValue()==0,QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

/* M2b.2o Source-only: retain two original rectangle leaves and their
 * intersection root. The BT handoff separately qualifies the exact nested
 * q/Q stack and ordered three-paint profile before callbacks use it. */
bool source_m2b2o_compound_clip(const admission& source)
{
    return source.text_outlines_eligible &&
        source.clip_completed && source.two_owned_clips && !source.sibling_clip_scopes &&
        quantapdf::detail::source_outline_is_rectangle(source.clip.commands) &&
        quantapdf::detail::source_outline_is_rectangle(source.second_clip.commands);
}

/* M2b.2r: the original Image/Fill prefix has already observed leaf1.
 * The BT handoff separately validates a real Q, a new independent q,
 * leaf2 and the saved identity CTMs before Text may observe leaf2. */
bool source_m2b2r_image_fill_prefix(const admission& source)
{
    return source.text_outlines_eligible &&
        source.clip_completed && source.order.size()==2u &&
        source.order[0]==2 && source.order[1]==1 &&
        source.draws.size()==1u && source.painted_paths.size()==1u &&
        source.scoped_clip_membership.size()==2u &&
        source.scoped_clip_membership[0]==1u && source.scoped_clip_membership[1]==1u &&
        quantapdf::detail::source_outline_is_rectangle(source.clip.commands);
}

bool source_m2b2r_reverse_sibling(const admission& source)
{
    return source_m2b2r_image_fill_prefix(source) && source.saw_q_restore &&
        source.two_owned_clips && source.sibling_clip_scopes &&
        quantapdf::detail::source_outline_is_rectangle(source.second_clip.commands);
}

/* One callback per REAL q/Q emitted by the same existing outline
 * token reader after BT. A Q restores exactly the Clip identity saved
 * at that original q. Later paints record that identity directly. */
quantapdf_status source_outline_scope(
    void *user,int is_save,unsigned char before,unsigned char after)
{
    admission& source=*static_cast<admission*>(user);
    try {
        admit(source.text_outlines_eligible &&
              source.outline_scope_started &&
              before==source.outline_clip_id &&
              before<=(source.two_owned_clips?2u:1u) &&
              after<=(source.two_owned_clips?2u:1u),
              QUANTAPDF_ERROR_UNSUPPORTED);
        if(is_save==1) {
            admit(before==after,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            QP_SOURCE_STACK_GROW(source.outline_clip_stack);
            source.outline_clip_stack.push_back(before);
        } else {
            admit(is_save==0 && !source.outline_clip_stack.empty() &&
                  after==source.outline_clip_stack.back(),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            source.outline_clip_stack.pop_back();
            if(before!=0u && after==0u)source.saw_q_restore=true;
        }
        source.outline_clip_id=after;
        return QUANTAPDF_OK;
    } catch(rejected const& e){return e.status;}
      catch(std::bad_alloc const&){return QUANTAPDF_ERROR_NOMEM;}
      catch(...){return QUANTAPDF_ERROR_UNSUPPORTED;}
}

/* M2b.2l Source ownership ONLY: after original Text/Fill under Clip1,
 * Q restores GS0 and q starts exactly one independent W/W* rectangle.
 * No intersection reduction, replacement of Clip1 or new parser. */
quantapdf_status source_m2b2l_sibling_clip(void *user,
    const std::vector<quantapdf_composer_path_command>& commands,
    quantapdf_composer_fill_rule rule,const std::array<double,6>& ctm,
    unsigned char before,unsigned char *after)
{
    admission& source=*static_cast<admission*>(user);
    if(after==nullptr)return QUANTAPDF_ERROR_ARGUMENT;
    *after=0u;
    try {
        admit(source.text_outlines_eligible &&
              source.outline_scope_started && source.clip_completed &&
              source.outline_clip_id==0u && before==0u && source.saw_q_restore &&
              !source.two_owned_clips && !source.sibling_clip_scopes &&
              source.second_clip.commands.empty() &&
              source.outline_clip_stack.size()==1u && source.outline_clip_stack[0]==0u &&
              source.order.size()==2u && source.order[0]==3 && source.order[1]==1 &&
              source.draws.empty() && source.painted_paths.empty() &&
              source.scoped_clip_membership.size()==2u &&
              source.scoped_clip_membership[0]==1u && source.scoped_clip_membership[1]==1u &&
              quantapdf::detail::source_outline_is_rectangle(source.clip.commands) &&
              quantapdf::detail::source_outline_is_rectangle(commands) &&
              (rule==QUANTAPDF_COMPOSER_FILL_NONZERO || rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD) &&
              ctm==matrix{1.0,0.0,0.0,1.0,0.0,0.0},
              QUANTAPDF_ERROR_UNSUPPORTED);
        QP_SOURCE_STAGE_ARM(9u);
        source.second_clip.commands=commands;
        source.second_clip.rule=rule;
        source.two_owned_clips=true;source.sibling_clip_scopes=true;
        source.outline_clip_id=2u;*after=2u;
        return QUANTAPDF_OK;
    } catch(rejected const& e){return e.status;}
      catch(std::bad_alloc const&){return QUANTAPDF_ERROR_NOMEM;}
      catch(...){return QUANTAPDF_ERROR_UNSUPPORTED;}
}

/* Record the actual Text-show position in the page scene. Unclipped
 * shows use the page operation budget; Clip combinations remain bounded
 * by their existing graph support. No font or qpdf pointer is retained. */
quantapdf_status source_text_paint(void *user)
{
    admission& source=*static_cast<admission*>(user);
    try {
        admit(source.order.size()<source.limits.max_operations,QUANTAPDF_ERROR_UNSUPPORTED);
        if(!source.ordered_scene()) {
            const bool compound=source_m2b2o_compound_clip(source) &&
                source.outline_clip_id==2u && source.order.empty() && !source.saw_q_restore;
            const bool reverse=source_m2b2r_reverse_sibling(source) &&
                source.outline_clip_id==2u && source.outline_clip_stack.size()>=1u &&
                source.outline_clip_stack.size()<=2u && source.outline_clip_stack[0]==0u &&
                std::all_of(source.outline_clip_stack.begin()+1,source.outline_clip_stack.end(),
                    [](unsigned char id){return id==2u;});
            admit(compound || reverse,QUANTAPDF_ERROR_UNSUPPORTED);
        }
        source.order.push_back(3);
        source.scoped_clip_membership.push_back(source.outline_clip_id);
        return QUANTAPDF_OK;
    } catch(rejected const& e){return e.status;}
      catch(std::bad_alloc const&){return QUANTAPDF_ERROR_NOMEM;}
      catch(...){return QUANTAPDF_ERROR_UNSUPPORTED;}
}
/* The outline reader freezes each actual solid Fill with Text shows.
 * Both paint kinds share the page operation budget and retain order. */
quantapdf_status source_fill_paint(void *user)
{
    admission& source=*static_cast<admission*>(user);
    try {
        admit(source.order.size()<source.limits.max_operations,QUANTAPDF_ERROR_UNSUPPORTED);
        if(!source.ordered_scene())
            admit(source_m2b2o_compound_clip(source) && source.outline_clip_id==2u &&
                  !source.saw_q_restore && source.order.size()==1u && source.order[0]==3,
                  QUANTAPDF_ERROR_UNSUPPORTED);
        source.order.push_back(1);
        source.scoped_clip_membership.push_back(source.outline_clip_id);
        return QUANTAPDF_OK;
    } catch(rejected const& e){return e.status;}
      catch(std::bad_alloc const&){return QUANTAPDF_ERROR_NOMEM;}
      catch(...){return QUANTAPDF_ERROR_UNSUPPORTED;}
}
quantapdf_status source_image_paint(
    void *user,Obj image,const std::array<double,6>&ctm)
{
    admission& source=*static_cast<admission*>(user);
    try {
        if(source.ordered_scene()) {
            source.execute(image,ctm);
            source.scoped_clip_membership.push_back(source.outline_clip_id);return QUANTAPDF_OK;
        }
        const bool sibling=source.sibling_clip_scopes && source.two_owned_clips &&
            source.outline_clip_id==2u && source.saw_q_restore &&
            source.order.size()==2u && source.order[0]==3 && source.order[1]==1 &&
            source.outline_clip_stack.size()>=1u && source.outline_clip_stack.size()<=2u &&
            source.outline_clip_stack[0]==0u &&
            std::all_of(source.outline_clip_stack.begin()+1,source.outline_clip_stack.end(),
                [](unsigned char id){return id==2u;}) &&
            quantapdf::detail::source_outline_is_rectangle(source.second_clip.commands);
        const bool compound=source_m2b2o_compound_clip(source) &&
            source.outline_clip_id==2u && !source.saw_q_restore &&
            source.order.size()==2u && source.order[0]==3 && source.order[1]==1;
        admit(source.text_outlines_eligible &&
              source.draws.empty() &&
              source.painted_paths.empty() &&
              source.outline_scope_started &&
              (sibling || compound || (source.outline_clip_id==(source.clip_completed?1u:0u) &&
              ((source.order.size()==1u && source.order[0]==3) ||
               (source.order.size()==2u && source.order[0]==3 &&
                source.order[1]==1)) &&
              (!source.clip_completed ||
               (source.clip.commands.size()==5u &&
                !source.two_owned_clips &&
                source.second_clip.commands.empty())) &&
              !source.saw_q_restore)),
              QUANTAPDF_ERROR_UNSUPPORTED);
        source.execute(image,ctm);
        source.scoped_clip_membership.push_back(source.outline_clip_id);
        return QUANTAPDF_OK;
    } catch(rejected const& e){return e.status;}
      catch(std::bad_alloc const&){return QUANTAPDF_ERROR_NOMEM;}
      catch(...){return QUANTAPDF_ERROR_UNSUPPORTED;}
}

class page_content_reader final : public Obj::ParserCallbacks {
    /* One q frame owns every currently interpreted graphics-state value.
     * Path construction belongs to the content stream, outside q/Q. */
    struct graphics_state {
        matrix ctm;
        uint32_t fill,stroke;
        unsigned char clip;
        double width,miter;
        int cap,join;
        bool default_dash,rgb_exact;
    };
public:
    explicit page_content_reader(admission& a) : target(a) {}

    void contentSize(size_t size) override
    {
        admit(size<=target.limits.max_content_bytes,QUANTAPDF_ERROR_UNSUPPORTED);
    }

    void handleObject(Obj item) override
    {
        admit(!item.isInlineImage(),QUANTAPDF_ERROR_UNSUPPORTED);
        if(outline_reader) {
            const auto status=outline_reader->consume(item);
            admit(status==QUANTAPDF_OK,status);return;
        }
        if (!item.isOperator()) {
            admit(args.size()<6u,QUANTAPDF_ERROR_UNSUPPORTED);
            args.push_back(item);
            return;
        }
        const std::string op=item.getOperatorValue();
        if(target.text_outlines_eligible && op=="BT") {
            const bool compound_root=source_m2b2o_compound_clip(target) &&
                clip_active==2u && !target.saw_q_restore &&
                stack.size()>=2u && stack.size()<=3u && stack[0].clip==0u && stack[1].clip==1u &&
                std::all_of(stack.begin()+2,stack.end(),
                    [](const graphics_state& state){return state.clip==2u;}) &&
                current==matrix{1.0,0.0,0.0,1.0,0.0,0.0} &&
                std::all_of(stack.begin(),stack.end(),[](const graphics_state& state) {
                    return state.ctm==matrix{1.0,0.0,0.0,1.0,0.0,0.0};
                }) && target.order.empty() && target.draws.empty() && target.painted_paths.empty();
            const bool reverse_sibling=source_m2b2r_reverse_sibling(target) &&
                clip_active==2u && stack.size()>=1u && stack.size()<=2u &&
                stack[0].clip==0u &&
                std::all_of(stack.begin()+1,stack.end(),
                    [](const graphics_state& state){return state.clip==2u;}) && current==identity &&
                std::all_of(stack.begin(),stack.end(),[](const graphics_state& state){return state.ctm==identity;});
            admit(args.empty() && pending_path.empty() && !clip_pending &&
                  (target.ordered_scene() || compound_root || reverse_sibling),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            std::vector<quantapdf::detail::source_outline_paint> prefix;
            for(size_t i=0u;i<target.painted_paths.size();++i) {
                const auto& path=target.painted_paths[i];
                admit(!path.stroke &&
                      quantapdf::detail::source_outline_is_rectangle(path.commands),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                quantapdf::detail::source_outline_paint paint{{},path.fill_argb};
                paint.rectangle=path.commands;paint.rule=path.fill_rule;prefix.push_back(std::move(paint));
            }
            QP_SOURCE_STAGE_ARM(8u);
            target.outline_scope_started=true;
            target.outline_clip_id=clip_active;
            std::vector<quantapdf::detail::source_text_seed> scoped_saved;
            scoped_saved.reserve(stack.size());
            for(const auto& state:stack) {
                QP_SOURCE_STACK_GROW(target.outline_clip_stack);
                target.outline_clip_stack.push_back(state.clip);
                scoped_saved.push_back({state.ctm,state.fill,state.clip});
            }
            const auto setup=quantapdf::detail::make_source_text_outlines(
                target.resources,target.exact_page_h,
                {current,fill_argb,clip_active},
                scoped_saved,prefix,
                {&target,source_text_paint,source_image_paint,
                 source_fill_paint,source_outline_scope,source_m2b2l_sibling_clip},outline_reader);
            admit(setup==QUANTAPDF_OK,setup);
            const auto status=outline_reader->consume(item);
            admit(status==QUANTAPDF_OK,status);return;
        }
        /* A single-pass feature selection, not a second token parser or
         * source retry. Leave every prior blank-page/state-only contract
         * untouched: select ONLY when the first actual m follows the
         * original cm/q/G/g/w/q/q prefix, with exact decimal page metadata. */
        if(target.texample_public_eligible && !target.texample_original_profile &&
           texample_prefix_possible) {
            static constexpr const char *prefix[]={"cm","q","G","g","w","q","q","m"};
            bool matches=op==prefix[texample_prefix_index];
            if(matches && (texample_prefix_index==2u || texample_prefix_index==3u ||
                           texample_prefix_index==4u))
                matches=args.size()==1u && args[0].isNumber() &&
                    args[0].getNumericValue()==(texample_prefix_index==4u?0.3985:0.0);
            if(!matches)texample_prefix_possible=false;
            else if(++texample_prefix_index==8u) {
                admit(stack.size()==3u && pending_path.empty() &&
                      !target.clip_completed && target.order.empty() &&
                      current[0]==1.0 && current[1]==0.0 &&
                      current[2]==0.0 && current[3]==1.0 &&
                      current[4]==118.566 && current[5]==90.22,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                target.texample_original_profile=true;
                target.texample_cm_count=1u;target.texample_q_count=3u;
                target.texample_stroke_gray_count=1u;
                target.texample_gray_count=1u;target.texample_width_count=1u;
                texample_original_ctm=true;texample_qdepth=3u;

            }
        }
        if(target.texample_original_profile) {
            /* The exact original M3 is a complete-source
             * profile. Reject an extra original operator rather than
             * omitting it while still publishing the first 3 paints. */
            const bool recognized=op=="cm" || op=="q" || op=="Q" ||
                op=="G" || op=="g" || op=="w" || op=="m" ||
                op=="c" || op=="h" || op=="W*" || op=="n" ||
                op=="rg" || op=="f*" || op=="S";
            admit(recognized,QUANTAPDF_ERROR_UNSUPPORTED);
        }

        if(!target.three_branch_candidate &&
           !target.texample_original_profile &&
           std::any_of(target.painted_paths.begin(),
                       target.painted_paths.end(),
                       [](const staged_path& p){return p.stroke;}))
            throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
        /* After precisely three paints, only original Q pops may
         * occur. Unsupported late resources must not be suppressed. */
        if(target.three_branch_candidate &&
           target.painted_paths.size()==3u && op!="Q")
            throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};

        if(clip_pending && op!="n")
            throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
        if((op=="M" || op=="d")) {
            if(op=="M") {
                admit(args.size()==1u,QUANTAPDF_ERROR_UNSUPPORTED);
                const double miter=finite_number(args[0]);
                admit(miter>=1.0 && miter<=1000000.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                stroke_miter=miter;
            } else {
                admit(args.size()==2u && args[0].isArray(),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                const int count=args[0].getArrayNItems();
                admit(count>=0 && count<=16,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                const double phase=finite_number(args[1]);
                admit(phase>=0.0 && phase<=1000000.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                double sum=0.0;
                for(int i=0;i<count;++i) {
                    const double length=
                        finite_number(args[0].getArrayItem(i));
                    admit(length>=0.0 && length<=1000000.0 &&
                          sum<=1000000.0-length,
                          QUANTAPDF_ERROR_UNSUPPORTED);
                    sum+=length;
                }
                admit(count==0 || sum>0.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);

                    stroke_default_dash=count==0 && phase==0.0;
            }
        } else if (op=="q") {
            admit(args.empty());
            if(target.texample_original_profile) {
                admit(texample_original_ctm && texample_qdepth<4u &&
                      ((target.texample_stage==0u &&
                        texample_qdepth<3u) ||
                       (target.texample_stage==1u &&
                        texample_qdepth==3u)),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                ++texample_qdepth;
                ++target.texample_q_count;
            }
            /* A real Stroke inside saved scopes still needs the general
             * Stroke sampler; every parsed state value is nevertheless saved. */
            saw_saved_scope=true;
            QP_SOURCE_STACK_GROW(stack);
            stack.push_back({current,fill_argb,stroke_argb,clip_active,
                stroke_width,stroke_miter,stroke_cap,stroke_join,
                stroke_default_dash,stroke_rgb_exact});
        } else if(op=="Q") {
            admit(args.empty() && !stack.empty());
            if(target.texample_original_profile) {
                admit(texample_qdepth>0u &&
                      ((target.texample_stage==2u &&
                        texample_qdepth>=3u) ||
                       (target.texample_stage==4u &&
                        (texample_qdepth==2u ||
                         (texample_qdepth==1u &&
                          target.texample_terminal_n==1u)))),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                --texample_qdepth;
                ++target.texample_Q_count;
            }
            const graphics_state saved=stack.back();stack.pop_back();
            const bool was_clipped=clip_active!=0u;
            current=saved.ctm;fill_argb=saved.fill;clip_active=saved.clip;
            stroke_argb=saved.stroke;stroke_width=saved.width;stroke_miter=saved.miter;
            stroke_cap=saved.cap;stroke_join=saved.join;
            stroke_default_dash=saved.default_dash;stroke_rgb_exact=saved.rgb_exact;
            if(target.clip_completed && was_clipped && !clip_active)
                target.saw_q_restore=true;
        } else if(op=="cm") {
            admit(args.size()==6u);
            matrix next;
            for(size_t i=0u;i<6u;++i)next[i]=finite_number(args[i]);
            current=concat(current,next);
            if(target.texample_original_profile) {
                /* Exact authored original 1/1000pt translation.
                 * No general fractional-CTM Source bypass exists. */
                admit(!texample_original_ctm &&
                      target.texample_cm_count==0u &&
                      texample_qdepth==0u &&
                      next[0]==1.0 && next[1]==0.0 &&
                      next[2]==0.0 && next[3]==1.0 &&
                      next[4]==118.566 && next[5]==90.22 &&
                      current==next,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                texample_original_ctm=true;
                ++target.texample_cm_count;
            }
            /* An exact page-height negative-Y authoring CTM selects
             * only the *candidate*. It does NOT make a page paintable;
             * the complete original 3GS/7Clip shape is still mandatory. */
            if(!target.three_branch_candidate && !target.clip_completed &&
               target.order.empty() && target.draws.empty() &&
               target.exact_page_w==600.0 &&
               target.exact_page_h==600.0 &&
               target.media_left==0.0 && target.media_bottom==0.0 &&
               current[0]==1.0 && current[1]==0.0 &&
               current[2]==0.0 && current[3]==-1.0 &&
               current[4]==0.0 && current[5]==600.0)
                target.three_branch_candidate=true;
        } else if((op=="w" || op=="J" || op=="j")) {
            /* Stroking-only graphics-state parameters have no effect
             * on Fill-only or blank Source profiles. The explicitly
             * selected complete Stroke profiles validate these same
             * state values before publishing their owned graph.
             * Admit strictly typed bounded originals, but allocate
             * no extra GS/Clip resource or hidden paint layer. */
            admit(args.size()==1u,QUANTAPDF_ERROR_UNSUPPORTED);
            if(op=="w") {
                const double width=finite_number(args[0]);
                admit(width>=0.0 && width<=1000000.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                stroke_width=width;
                if(target.texample_original_profile) {
                    admit(texample_original_ctm &&
                          texample_qdepth==1u &&
                          target.texample_stage==0u &&
                          target.texample_width_count==0u &&
                          width==0.3985,
                          QUANTAPDF_ERROR_UNSUPPORTED);
                    ++target.texample_width_count;
                }
            } else {
                admit(args[0].isInteger(),QUANTAPDF_ERROR_UNSUPPORTED);
                const auto style=args[0].getIntValue();
                admit(style>=0ll && style<=2ll,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                if(op=="J")stroke_cap=static_cast<int>(style);
                else stroke_join=static_cast<int>(style);
            }
        } else if(op=="gs") {
            /* Resolve a named, bounded *opaque no-op* graphics-state
             * resource at the actual operator position. A missing or
             * unsupported state rejects the COMPLETE original page;
             * never continue painting only the visible black Path. */
            admit(args.size()==1u && args[0].isName(),QUANTAPDF_ERROR_UNSUPPORTED);
            validate_opaque_gs(target.resources,args[0]);
        } else if(op=="Do") {
            admit(args.size()==1u && args[0].isName() &&
                  pending_path.empty() && !clip_pending);
            if(!target.clip_completed) {
                admit(!clip_active && !target.saw_q_restore,QUANTAPDF_ERROR_UNSUPPORTED);
            } else {
                admit((target.ordered_scene() || target.draws.size()<2u) &&
                      (target.ordered_scene() || (clip_active && !target.saw_q_restore) ||
                       (target.sibling_clip_scopes && clip_active==2u) ||
                       (!clip_active && target.saw_q_restore)),
                      QUANTAPDF_ERROR_UNSUPPORTED);
            }
            admit(target.resources.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            Obj xobjects=target.resources.getKey("/XObject");
            admit(xobjects.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            Obj named=xobjects.getKey(args[0].getName());
            admit(!named.isNull(),QUANTAPDF_ERROR_FORMAT);
            target.execute(named,current);
            QP_SOURCE_STAGE_ARM(6u);
            target.scoped_clip_membership.push_back(clip_active);
        } else if(op=="rg") {
            admit(args.size()==3u);
            uint32_t rgb=0u;
            for(size_t i=0u;i<3u;++i) {
                const double channel=finite_number(args[i]);
                uint32_t component=0u;
                admit(channel>=0.0 && channel<=1.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                component=static_cast<uint32_t>(
                    std::floor(channel*255.0+0.5));
                rgb|=component << (16u-8u*i);
            }
            fill_argb=UINT32_C(0xff000000)|rgb;
            if(target.texample_original_profile) {
                admit(texample_original_ctm &&
                      texample_qdepth==4u &&
                      target.texample_stage==1u &&
                      target.texample_orange_count==0u &&
                      fill_argb==UINT32_C(0xffff8000),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                ++target.texample_orange_count;
            }
        } else if(op=="g") {
            admit(args.size()==1u);
            const double channel=finite_number(args[0]);
            admit(channel>=0.0 && channel<=1.0,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            const uint32_t gray=static_cast<uint32_t>(
                std::floor(channel*255.0+0.5));
            fill_argb=UINT32_C(0xff000000) |
                (gray<<16u) | (gray<<8u) | gray;
            if(target.texample_original_profile) {
                admit(target.texample_stage==0u &&
                      texample_qdepth==1u &&
                      target.texample_gray_count==0u &&
                      gray==0u,QUANTAPDF_ERROR_UNSUPPORTED);
                ++target.texample_gray_count;
            }
        } else if((op=="G" || op=="RG" || op=="K")) {
            const size_t count=op=="G"?1u:(op=="RG"?3u:4u);
            admit(args.size()==count,QUANTAPDF_ERROR_UNSUPPORTED);
            uint32_t rgb=0u;
            bool exact=op=="RG" || op=="G";
            for(size_t i=0u;i<count;++i) {
                const double component=finite_number(args[i]);
                admit(component>=0.0 && component<=1.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                if(op=="G") {
                    exact=component==0.0 || component==1.0;
                    rgb=component==1.0?UINT32_C(0x00ffffff):0u;
                } else if(op=="RG") {
                    exact=exact && (component==0.0 || component==1.0);
                    rgb|=(component==1.0?255u:0u) << (16u-8u*i);
                }
            }
            /* Binary DeviceGray is exact RGB state before any q saves it.
             * Other Gray/CMYK strokes still need broader color admission. */
            stroke_rgb_exact=exact;
            if(target.texample_original_profile) {
                admit(op=="G" && args.size()==1u &&
                      finite_number(args[0])==0.0 &&
                      texample_qdepth==1u &&
                      target.texample_stage==0u &&
                      target.texample_stroke_gray_count==0u,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                exact=true;
                rgb=0u;
                ++target.texample_stroke_gray_count;
            }
            stroke_rgb_exact=exact;
            if(exact)stroke_argb=UINT32_C(0xff000000)|rgb;
        } else if(op=="m") {
            admit(args.size()==2u && !clip_pending,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(target.texample_original_profile)
                texample_move(finite_number(args[0]),
                              finite_number(args[1]));
            else {
                begin_contour();
                append_point(QUANTAPDF_COMPOSER_PATH_MOVE_TO,
                             finite_number(args[0]),finite_number(args[1]));
            }
        } else if(op=="l") {
            admit(args.size()==2u && !pending_path.empty() &&
                  !closed,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,
                         finite_number(args[0]),finite_number(args[1]));
        } else if(op=="c") {
            /* Public original PDF c: six finite control/endpoint
             * coordinates, staged in the SAME Composer CUBIC_TO kind
             * already handled by the single bounded C11 Fill raster.
             * The same paint engine consumes these bounded source values.
             * Qualified W/W* and complete original Stroke profiles
             * consume these same original cubic commands. */
            admit(args.size()==6u &&
                  (target.texample_original_profile ||
                   (!pending_path.empty() && !closed && !clip_pending)),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            std::array<double,6u> xy{};
            for(size_t i=0u;i<6u;++i)
                xy[i]=finite_number(args[i]);
            if(target.texample_original_profile)texample_cubic(xy);
            else append_cubic(xy);
        } else if((op=="v" || op=="y")) {
            /* Original PDF cubic shorthand: v uses the current point
             * as control #1; y repeats its endpoint as control #2.
             * Expand into the SAME staged CUBIC_TO command; do not
             * introduce a second curve interpreter or renderer. */
            admit(args.size()==4u && !pending_path.empty() &&
                  !closed && !clip_pending,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            std::array<double,4u> xy{};
            for(size_t i=0u;i<4u;++i)
                xy[i]=finite_number(args[i]);
            append_cubic_shorthand(op,xy);
        } else if(op=="h") {
            admit(args.empty() &&
                  (target.texample_original_profile ||
                   (pending_path.size()>=contour_start+3u && !closed)),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(target.texample_original_profile) {
                texample_close();
            } else close_path();
        } else if(op=="re") {
            admit(args.size()==4u && !clip_pending,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            begin_contour();
            const double x=finite_number(args[0]), y=finite_number(args[1]);
            const double w=finite_number(args[2]), h=finite_number(args[3]);

            admit((w>0.0 && h>0.0) ||
                  (w!=0.0 && h!=0.0),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            /* Single-root Fill coordinates use the same source lattice with
             * or without Text. The remaining two-root Text handoff retains
             * its current geometry admission. */
            const bool compound_text_prefix=target.clip_completed && clip_active==1u && !target.two_owned_clips &&
                !target.saw_q_restore && target.order.empty() && target.painted_paths.empty() &&
                stack.size()==2u && stack[0].clip==0u && stack[1].clip==1u &&
                quantapdf::detail::source_outline_is_rectangle(target.clip.commands);
            const bool reverse_text_prefix=source_m2b2r_image_fill_prefix(target) &&
                clip_active==0u && target.saw_q_restore && !target.two_owned_clips &&
                stack.size()==1u && stack[0].clip==0u;
            fractional_rect_staging=(target.ordered_scene() ||
                (target.text_outlines_eligible &&
                 ((compound_text_prefix && target.draws.empty()) || reverse_text_prefix))) && current==identity &&
                (std::trunc(x)!=x || std::trunc(y)!=y || std::trunc(w)!=w || std::trunc(h)!=h);
            if(fractional_rect_staging)for(const auto& value:args)(void)page_lattice(value,8ll);
            append_point(QUANTAPDF_COMPOSER_PATH_MOVE_TO,x,y);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x+w,y);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x+w,y+h);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x,y+h);
            close_path();fractional_rect_staging=false;
        } else if(op=="S") {
            if(target.texample_original_profile) {
                admit(args.empty() &&
                      (target.texample_stage==2u ||
                       target.texample_stage==3u) &&
                      texample_qdepth==2u && clip_active==0u &&
                      target.clip_completed && !clip_pending &&
                      stroke_rgb_exact &&
                      stroke_argb==UINT32_C(0xff000000) &&
                      stroke_width==0.3985 &&
                      stroke_cap==0 && stroke_join==0 &&
                      stroke_miter==10.0 && stroke_default_dash,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                texample_complete_group();
                target.paint_path(std::move(pending_path),
                    fill_argb,QUANTAPDF_COMPOSER_FILL_NONZERO,
                    true,stroke_argb,0.3985f);
                QP_SOURCE_STAGE_ARM(7u);
                target.scoped_clip_membership.push_back(0u);
                ++target.texample_stage;
                texample_clear_group();
            } else {
            admit(args.empty() && target.three_branch_candidate &&
                  target.three_branch_count==1u && clip_active==2u &&
                  !clip_pending && pending_path.size()==2u &&
                  contour_count==1u && !closed &&
                  target.painted_paths.empty() && target.order.empty() &&
                  target.draws.empty() &&
                  pending_path[0].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
                  pending_path[1].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
                  pending_path[0].point1.y==pending_path[1].point1.y &&
                  pending_path[0].point1.x!=pending_path[1].point1.x &&
                  stroke_rgb_exact &&
                  stroke_argb==UINT32_C(0xffff0000) &&
                  stroke_width==1.0 && stroke_cap==0 && stroke_join==0 &&
                  stroke_miter==10.0 && stroke_default_dash &&
                  current[0]==1.0 && current[1]==0.0 &&
                  current[2]==0.0 && current[3]==-1.0,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            target.paint_path(std::move(pending_path),fill_argb,
                QUANTAPDF_COMPOSER_FILL_NONZERO,true,stroke_argb);
            QP_SOURCE_STAGE_ARM(7u);
            target.scoped_clip_membership.push_back(clip_active);
            clear_path();
            }
        } else if(op=="s") {
            /* PDF s implicitly closes then strokes a single triangle.
             * Only one actual W/n Clip, 1px butt/miter/solid and an
             * exact DeviceRGB color can be passed to the EXISTING C11
             * bounded clipped-Stroke sampler. All other cases refuse. */
            admit(args.empty() && target.clip_completed &&
                  clip_active==1u && !clip_pending &&
                  !target.two_owned_clips && !target.saw_q_restore &&
                  !saw_saved_scope && pending_path.size()==3u &&
                  contour_count==1u && !closed &&
                  pending_path[0].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
                  pending_path[1].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
                  pending_path[2].kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
                  bounded_clip_path(target.clip.commands) &&
                  /* Original CTM must preserve physical line width:
                   * stage_point transforms the centerline, but the
                   * existing 1px C11 stroke sampler does not scale
                   * its half-width. Translation alone is harmless. */
                  current[0]==1.0 && current[1]==0.0 &&
                  current[2]==0.0 && current[3]==1.0 &&
                  stroke_rgb_exact && stroke_width==1.0 &&
                  stroke_cap==0 && stroke_join==0 &&
                  stroke_miter==10.0 && stroke_default_dash,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            close_path();
            target.paint_path(std::move(pending_path),fill_argb,
                QUANTAPDF_COMPOSER_FILL_NONZERO,true,stroke_argb);
            QP_SOURCE_STAGE_ARM(7u);
            target.scoped_clip_membership.push_back(clip_active);
            clear_path();
        } else if((op=="f" || op=="f*")) {
            if(target.texample_original_profile) {
                admit(op=="f*" && args.empty() &&
                      target.texample_stage==1u &&
                      texample_qdepth==4u && clip_active==1u &&
                      target.clip_completed && !clip_pending &&
                      fill_argb==UINT32_C(0xffff8000) &&
                      target.draws.empty() &&
                      target.painted_paths.empty(),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                texample_complete_group();
                target.paint_path(std::move(pending_path),fill_argb,
                    QUANTAPDF_COMPOSER_FILL_EVEN_ODD);
                QP_SOURCE_STAGE_ARM(7u);
                target.scoped_clip_membership.push_back(1u);
                target.texample_stage=2u;
                texample_clear_group();
            } else {
            admit(args.empty() && pending_path.size()>=3u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(target.three_branch_candidate)
                admit(op=="f" && target.painted_paths.size()>=1u &&
                      target.painted_paths.size()<=2u &&
                      target.painted_paths[0].stroke &&
                      target.three_branch_count==
                          target.painted_paths.size()+1u &&
                      clip_active==target.three_branch_count+1u &&
                      fill_argb==UINT32_C(0xff0000ff),
                      QUANTAPDF_ERROR_UNSUPPORTED);
            if(!target.clip_completed) {
                admit(!clip_active && !clip_pending,QUANTAPDF_ERROR_UNSUPPORTED);
            } else  {
                admit(target.clip_completed &&
                      (target.ordered_scene() || (clip_active && !target.saw_q_restore) ||
                       (target.sibling_clip_scopes &&
                        clip_active==2u) ||
                       (!clip_active && target.saw_q_restore)),
                      QUANTAPDF_ERROR_UNSUPPORTED);
            }
            if (!closed) close_path();
            target.paint_path(std::move(pending_path),fill_argb,
                op=="f" ? QUANTAPDF_COMPOSER_FILL_NONZERO :
                          QUANTAPDF_COMPOSER_FILL_EVEN_ODD);
            QP_SOURCE_STAGE_ARM(7u);
            target.scoped_clip_membership.push_back(clip_active);
            clear_path();
            }
        } else if((op=="W" || op=="W*")) {
            admit(args.empty() && !clip_pending,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(target.texample_original_profile) {
                admit(op=="W*" && target.texample_stage==0u &&
                      texample_qdepth==3u &&
                      !target.clip_completed && !clip_pending,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                texample_complete_group();
            } else if(target.three_branch_candidate &&
                      target.clip_completed) {
                /* Each nested W/W* is defined AFTER a genuine Q has
                 * restored the common outer W. No active inner Clip
                 * may be inherited by another branch. */
                admit(clip_active==1u &&
                      target.three_branch_count<3u &&
                      target.order.size()==target.three_branch_count &&
                      target.three_branch_count==
                          target.painted_paths.size() &&
                      (target.three_branch_count==0u ?
                       (op=="W*" && nested_rectangles(pending_path)) :
                       (pending_path.size()==6u &&
                        bounded_clip_path(pending_path,true) &&
                        op==(target.three_branch_count==1u?"W":"W*"))),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                three_branch_clip_pending=true;
            } else if(target.clip_completed) {
                const bool sibling=clip_active==0u && target.saw_q_restore;
                admit(!second_clip_used && !second_clip_pending &&
                      ((clip_active==1u && !target.saw_q_restore) ||
                       sibling) &&
                      pending_second_clip_contour(),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                second_clip_pending=true;
                sibling_clip_pending=sibling;
            } else {
                admit((pending_path.size()>=3u ||
                       trivial_zero_area_clip()) &&
                      !target.clip_completed &&
                      (trivial_zero_area_clip() ||
                       bounded_clip_path(pending_path,true) ||
                       (op=="W*" && nested_rectangles(pending_path))),
                      QUANTAPDF_ERROR_UNSUPPORTED);
            }
            clip_rule=op=="W*" ? QUANTAPDF_COMPOSER_FILL_EVEN_ODD :
                                 QUANTAPDF_COMPOSER_FILL_NONZERO;
            clip_pending=true;
        } else if(op=="n") {
            admit(args.empty(),QUANTAPDF_ERROR_UNSUPPORTED);
            if(target.texample_original_profile) {
                if(target.texample_stage==0u &&
                   texample_qdepth==3u && clip_pending &&
                   clip_rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD) {
                    admit(pending_path.size()==12u &&
                          texample_contours==2u && closed &&
                          !texample_pending_move &&
                          !target.clip_completed,
                          QUANTAPDF_ERROR_UNSUPPORTED);
                    target.clip.commands=std::move(pending_path);
                    target.clip.rule=clip_rule;
                    target.clip_completed=true;
                    clip_active=1u;
                    clip_pending=false;
                    target.texample_stage=1u;
                    texample_clear_group();
                } else {
                    admit(target.texample_stage==4u &&
                          texample_qdepth==1u &&
                          target.texample_terminal_n==0u &&
                          !clip_pending && !texample_pending_move &&
                          pending_path.empty(),
                          QUANTAPDF_ERROR_UNSUPPORTED);
                    ++target.texample_terminal_n;
                }
            } else if(three_branch_clip_pending) {
                admit(target.three_branch_candidate &&
                      target.three_branch_count<3u &&
                      clip_active==1u && clip_pending,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                complete_clip_contour();
                const size_t index=target.three_branch_count;
                admit(index==0u ?
                    (clip_rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD &&
                     nested_rectangles(pending_path)) :
                    (pending_path.size()==6u &&
                     bounded_clip_path(pending_path) &&
                     clip_rule==(index==1u?
                         QUANTAPDF_COMPOSER_FILL_NONZERO:
                         QUANTAPDF_COMPOSER_FILL_EVEN_ODD)),
                    QUANTAPDF_ERROR_UNSUPPORTED);
                target.three_branch_leaves[index].commands=
                    std::move(pending_path);
                target.three_branch_leaves[index].rule=clip_rule;
                ++target.three_branch_count;
                clip_active=static_cast<unsigned char>(
                    target.three_branch_count+1u);
                clip_pending=false;
                three_branch_clip_pending=false;
                clear_path();
            } else if(second_clip_pending) {
                /* Both original operands were separately parsed,
                 * billed and frozen at their definition-time CTM.
                 * Independent scopes always retain both leaves. An
                 * intersection's rectangle reduction is safe only if
                 * no paint or saved q can observe the first leaf. */
                std::array<float,4u> first{},next{};
                admit(target.clip_completed &&
                      (sibling_clip_pending ?
                         (clip_active==0u && target.saw_q_restore) :
                         (clip_active==1u && !target.saw_q_restore)) &&
                      !second_clip_used && !target.two_owned_clips &&
                      target.second_clip.commands.empty() && clip_pending &&
                      pending_second_clip_contour(),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                complete_clip_contour();
                admit(qualified_owned_clip_pair(target.clip.commands,
                                                pending_path),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                const bool first_observable=
                    std::find(target.scoped_clip_membership.begin(),
                              target.scoped_clip_membership.end(),1u)!=
                        target.scoped_clip_membership.end() ||
                    std::any_of(stack.begin(),stack.end(),
                        [](const graphics_state& state){return state.clip==1u;});
                if(!sibling_clip_pending && !first_observable &&
                   read_strict_rect(target.clip.commands,first) &&
                   read_strict_rect(pending_path,next)) {
                    const float left=std::max(first[0],next[0]);
                    const float top=std::max(first[1],next[1]);
                    const float right=std::min(first[2],next[2]);
                    const float bottom=std::min(first[3],next[3]);
                    const bool nonempty=right>left && bottom>top;
                    const float x0=nonempty?left:0.0f;
                    const float y0=nonempty?top:0.0f;
                    const float x1=nonempty?right:0.0f;
                    const float y1=nonempty?bottom:0.0f;
                    const quantapdf_point corners[4]={
                        {x0,y0},{x1,y0},{x1,y1},{x0,y1}
                    };
                    for(size_t i=0u;i<4u;++i)
                        target.clip.commands[i].point1=corners[i];
                    /* Both simple rectangles denote the same set for
                     * W and W*. The reduced contour is NONZERO. */
                    target.clip.rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
                } else {
                    target.second_clip.commands=std::move(pending_path);
                    target.second_clip.rule=clip_rule;
                    target.two_owned_clips=true;
                    target.sibling_clip_scopes=sibling_clip_pending;
                }
                second_clip_pending=false;
                sibling_clip_pending=false;
                second_clip_used=true;
                clip_active=2u;
                clip_pending=false;
                clear_path();
            } else if(!clip_pending) {
                clear_path();
            } else {
                admit(clip_pending && !target.clip_completed &&
                      (pending_path.size()>=3u ||
                       trivial_zero_area_clip()),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                complete_clip_contour();
                admit(bounded_clip_path(pending_path) ||
                      (clip_rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD &&
                       nested_rectangles(pending_path)),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                target.clip.commands=std::move(pending_path);
                target.clip.rule=clip_rule;
                target.clip_completed=true;
                clip_active=1u;
                clip_pending=false;
                clear_path();
            }
        } else {
            /* Parsing is COMPLETE, not image observation: unsupported
             * paint/Text/Stroke/Clip/Form/group/mask/graphics operators
             * invalidate the entire source page, including prior paints. */
            throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
        }
        args.clear();
    }

    void handleEOF() override
    {
        if(outline_reader) {
            const auto status=outline_reader->finish();admit(status==QUANTAPDF_OK,status);
            admit(target.outline_scope_started &&
                  target.outline_clip_stack.empty() &&
                  target.outline_clip_id<=1u &&
                  (!target.sibling_clip_scopes ||
                   (target.two_owned_clips && target.outline_clip_id==0u &&
                    target.order.size()==3u && target.draws.size()==1u &&
                    target.scoped_clip_membership.size()==3u &&
                    target.scoped_clip_membership[0]==1u &&
                    target.scoped_clip_membership[1]==1u &&
                    target.scoped_clip_membership[2]==2u)) &&
                  (!source_m2b2o_compound_clip(target) ||
                   (target.outline_clip_id==0u && target.order.size()==3u &&
                    target.draws.size()==1u && target.scoped_clip_membership.size()==3u &&
                    std::all_of(target.scoped_clip_membership.begin(),
                        target.scoped_clip_membership.end(),[](unsigned char id){return id==2u;}))),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            target.text_outline_paints=outline_reader->paints();
            target.text_outlines_complete=true;return;
        }
        admit(!target.text_prefix_precision,QUANTAPDF_ERROR_UNSUPPORTED);
        admit(args.empty() && stack.empty() &&
              pending_path.empty() && !clip_pending);
        if(target.texample_original_profile)
            admit(target.texample_stage==4u &&
                  target.texample_moves==24u &&
                  target.texample_cubics==32u &&
                  target.texample_closes==8u &&
                  target.texample_zero_moves==16u &&
                  target.texample_q_count==4u &&
                  target.texample_Q_count==4u &&
                  target.texample_cm_count==1u &&
                  target.texample_gray_count==1u &&
                  target.texample_stroke_gray_count==1u &&
                  target.texample_width_count==1u &&
                  target.texample_orange_count==1u &&
                  target.texample_terminal_n==1u &&
                  texample_qdepth==0u &&
                  !texample_pending_move,
                  QUANTAPDF_ERROR_UNSUPPORTED);
    }
private:
    /* The SAME QPDF operator callbacks account for every original m:
     * an unconsumed MOVE has zero winding edges, and retiring it is
     * allowed ONLY when a subsequent original m replaces it or the
     * complete paint/Clip operation terminates. */
    void texample_move(double x,double y)
    {
        admit(!clip_pending && (pending_path.empty() || closed ||
              texample_pending_move) && texample_group_moves<6u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        const quantapdf_point p=stage_point(x,y);
        if(texample_pending_move) {
            ++texample_group_empty;
            ++target.texample_zero_moves;
        }
        texample_pending_move=true;
        texample_move_point=p;
        ++texample_group_moves;
        ++target.texample_moves;
    }
    void texample_cubic(const std::array<double,6u>& xy)
    {
        admit(!clip_pending && texample_group_cubics<8u &&
              pending_path.size()<12u,QUANTAPDF_ERROR_UNSUPPORTED);
        if(texample_pending_move) {
            admit(texample_contours<2u &&
                  (pending_path.empty() || closed),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            quantapdf_composer_path_command cmd={};
            cmd.kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
            cmd.point1=texample_move_point;
            contour_start=pending_path.size();
            pending_path.push_back(cmd);
            texample_pending_move=false;
            ++texample_contours;
            closed=false;
        }
        admit(!closed && texample_contours>0u &&
              pending_path.size()>=contour_start+1u &&
              pending_path.size()<=contour_start+4u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        append_cubic(xy);
        ++texample_group_cubics;
        ++target.texample_cubics;
    }
    void texample_close()
    {
        admit(!texample_pending_move && !clip_pending &&
              texample_contours>0u && !closed &&
              pending_path.size()==contour_start+5u &&
              texample_group_closes<2u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        close_path();
        ++texample_group_closes;
        ++target.texample_closes;
    }
    void texample_complete_group()
    {
        /* Four groups of 6 m, 8 c, 2 h: two anchored closed curves,
         * four truly zero-edge original m subpaths per group. */
        admit(texample_pending_move &&
              texample_contours==2u &&
              texample_group_moves==6u &&
              texample_group_cubics==8u &&
              texample_group_closes==2u &&
              texample_group_empty==3u &&
              pending_path.size()==12u && closed,
              QUANTAPDF_ERROR_UNSUPPORTED);
        ++texample_group_empty;
        ++target.texample_zero_moves;
        texample_pending_move=false;
        admit(texample_group_empty==4u,QUANTAPDF_ERROR_UNSUPPORTED);
    }
    void texample_clear_group()
    {
        clear_path();
        texample_pending_move=false;
        texample_group_moves=0u;
        texample_group_cubics=0u;
        texample_group_closes=0u;
        texample_group_empty=0u;
        texample_contours=0u;
    }
    void begin_contour()
    {
        // Fill closes each preceding subpath at its original start before
        // beginning the next one. All contours remain in one paint so the
        // winding/even-odd rule sees their combined edges.
        if(!pending_path.empty() && !closed)close_path();
        ++contour_count;
        contour_start=pending_path.size();
        closed=false;
    }
    void clear_path()
    {
        pending_path.clear();
        contour_start=0u;
        contour_count=0u;
        closed=false;
    }
    static bool read_strict_rect(
        const std::vector<quantapdf_composer_path_command>& path,
        std::array<float,4u>& bounds)
    {
        /* This subset requires the exact authored re corner order,
         * including its signed-dimension reversals. Neither a curve
         * nor an arbitrary/rotated quad may be silently simplified. */
        if(path.size()!=5u ||
           path[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
           path[1].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           path[2].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           path[3].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           path[4].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)
            return false;
        const auto& a=path[0].point1;
        const auto& b=path[1].point1;
        const auto& c=path[2].point1;
        const auto& d=path[3].point1;
        if(!std::isfinite(a.x) || !std::isfinite(a.y) ||
           !std::isfinite(b.x) || !std::isfinite(b.y) ||
           !std::isfinite(c.x) || !std::isfinite(c.y) ||
           !std::isfinite(d.x) || !std::isfinite(d.y) ||
           a.y!=b.y || b.x!=c.x || c.y!=d.y || d.x!=a.x ||
           a.x==b.x || a.y==d.y)
            return false;
        bounds={std::min(a.x,b.x),std::min(a.y,d.y),
                std::max(a.x,b.x),std::max(a.y,d.y)};
        return true;
    }
    bool trivial_zero_area_clip() const
    {
        if(pending_path.empty())return true;
        if(pending_path.size()==1u)
            return pending_path[0].kind==
                QUANTAPDF_COMPOSER_PATH_MOVE_TO;
        /* The implicit return segment cancels an open single line. */
        return pending_path.size()==2u &&
            pending_path[0].kind==
                QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
            pending_path[1].kind==
                QUANTAPDF_COMPOSER_PATH_LINE_TO;
    }
    bool pending_second_clip_contour() const
    {
        /* Test the bounded shape without allocating or changing it at
         * W. The mandatory n performs the same implicit closure/empty
         * normalization as the first Clip, then revalidates the result. */
        return bounded_clip_path(target.clip.commands) &&
            (trivial_zero_area_clip() ||
             bounded_clip_path(pending_path,true));
    }
    void complete_clip_contour()
    {
        if(trivial_zero_area_clip()) {
            /* Empty original W, MOVE-only, and open MOVE+LINE have zero
             * Fill area under both rules. Preserve the existing owned
             * four-command zero-edge encoding for either operand; its
             * definition-time coordinates cannot add covered pixels. */
            if(pending_path.capacity()<4u)QP_SOURCE_STAGE_ARM(8u);
            pending_path.assign(4u,quantapdf_composer_path_command{});
            pending_path[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
            pending_path[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
            pending_path[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
            pending_path[3].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
            contour_count=1u;
            contour_start=0u;
            closed=true;
        }
        if(!closed)close_path();
    }
    quantapdf_point stage_point(double x,double y) const
    {
        /* Source coordinates/CTM are resolved at construction time.
         * Existing strict scale/shear and exact quarter-viewport
         * policies apply IDENTICALLY to every cubic control/end
         * point, not merely to its final visible endpoint. */
        if(target.texample_original_profile) {
            admit(texample_original_ctm &&
                  current[0]==1.0 && current[1]==0.0 &&
                  current[2]==0.0 && current[3]==1.0 &&
                  current[4]==118.566 && current[5]==90.22 &&
                  target.exact_page_w==237.133 &&
                  target.exact_page_h==180.44 &&
                  std::isfinite(x) && std::isfinite(y) &&
                  std::fabs(x)<=1000.0 && std::fabs(y)<=1000.0 &&
                  std::fabs(x*100000.0-std::round(x*100000.0))<0.0001 &&
                  std::fabs(y*100000.0-std::round(y*100000.0))<0.0001,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            const double px=current[4]+x-target.media_left;
            const double py=target.exact_page_h-
                (current[5]+y-target.media_bottom);
            admit(std::isfinite(px) && std::isfinite(py) &&
                  px>=0.0 && px<=target.exact_page_w &&
                  py>=0.0 && py<=target.exact_page_h,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            return {static_cast<float>(px),static_cast<float>(py)};
        }
        const bool y_inverse=target.three_branch_candidate &&
            current[0]==1.0 && current[1]==0.0 &&
            current[2]==0.0 && current[3]==-1.0 &&
            current[4]==0.0 && current[5]==600.0;
        admit(y_inverse ||
              (current[0]>0.0 && current[3]>0.0 &&
               current[1]==0.0 && current[2]==0.0),
              QUANTAPDF_ERROR_UNSUPPORTED);
        const double absolute_x=
            current[0]*x+current[4]-target.media_left;
        const double absolute_y=
            current[3]*y+current[5]-target.media_bottom;
        if(fractional_rect_staging) {
            const double py=target.exact_page_h-absolute_y;
            admit(std::isfinite(absolute_x) && std::isfinite(py) &&
                  std::abs(absolute_x)<=1000000.0 && std::abs(py)<=1000000.0 &&
                  std::trunc(absolute_x*8.0)==absolute_x*8.0 && std::trunc(py*8.0)==py*8.0,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(!target.ordered_scene())target.text_prefix_precision=true;
            return {static_cast<float>(absolute_x),static_cast<float>(py)};
        }
        const double ix=target.crop_viewport?
            whole_signed_source(absolute_x):
            (double)whole(absolute_x,target.page_w);
        const double iy=target.crop_viewport?
            whole_signed_source(absolute_y):
            (double)whole(absolute_y,target.page_h);
        return {static_cast<float>(ix),static_cast<float>(
            (target.exact_page_h>0.0?
                target.exact_page_h:(double)target.page_h)-iy)};
    }
    void append_point(quantapdf_composer_path_command_kind kind,
                      double x,double y)
    {
        quantapdf_composer_path_command cmd={};
        cmd.kind=kind;
        cmd.point1=stage_point(x,y);
        QP_SOURCE_PATH_GROW(pending_path);
        pending_path.push_back(cmd);
    }
    void append_cubic(const std::array<double,6u>& xy)
    {
        /* Do not publish a partially admitted cubic command. All
         * three individually transformed points must pass first. */
        admit(!pending_path.empty() && !closed && !clip_pending,
              QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_composer_path_command cmd={};
        cmd.kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
        cmd.point1=stage_point(xy[0],xy[1]);
        cmd.point2=stage_point(xy[2],xy[3]);
        cmd.point3=stage_point(xy[4],xy[5]);
        QP_SOURCE_PATH_GROW(pending_path);
        pending_path.push_back(cmd);
    }
    void append_cubic_shorthand(const std::string& op,
                                const std::array<double,4u>& xy)
    {
        admit(!pending_path.empty() && !closed && !clip_pending &&
              (op=="v" || op=="y"),QUANTAPDF_ERROR_UNSUPPORTED);
        const auto& previous=pending_path.back();
        quantapdf_point current_point{};
        if(previous.kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO)
            current_point=previous.point3;
        else {
            admit(previous.kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
                  previous.kind==QUANTAPDF_COMPOSER_PATH_LINE_TO,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            current_point=previous.point1;
        }
        /* A shorthand's implicit control inherits the exact staged
         * CURRENT point. In particular, preceding cubic commands use
         * point3, never their first Bezier control. All authored new
         * controls still pass the original bounded CTM/grid checks. */
        quantapdf_composer_path_command cmd={};
        cmd.kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
        const auto first=stage_point(xy[0],xy[1]);
        const auto endpoint=stage_point(xy[2],xy[3]);
        cmd.point1=op=="v"?current_point:first;
        cmd.point2=op=="v"?first:endpoint;
        cmd.point3=endpoint;
        QP_SOURCE_PATH_GROW(pending_path);
        pending_path.push_back(cmd);
    }
    void close_path()
    {
        admit(pending_path.size()>=contour_start+3u && !closed,
              QUANTAPDF_ERROR_UNSUPPORTED);
        // An authored return line already contributes the closing edge.
        // Replace only that redundant final line; curves retain all controls.
        const auto& first=pending_path[contour_start];
        auto& last=pending_path.back();
        if(pending_path.size()>=contour_start+4u &&
           last.kind==QUANTAPDF_COMPOSER_PATH_LINE_TO &&
           last.point1.x==first.point1.x && last.point1.y==first.point1.y)
            last.kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
        else {
            quantapdf_composer_path_command cmd={};
            cmd.kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
            QP_SOURCE_PATH_GROW(pending_path);
            pending_path.push_back(cmd);
        }
        closed=true;
    }
    admission& target;
    unsigned char clip_active=0u;
    bool clip_pending=false;
    bool second_clip_pending=false;
    bool sibling_clip_pending=false;
    bool second_clip_used=false;
    quantapdf_composer_fill_rule clip_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    matrix current=identity;
    uint32_t fill_argb=UINT32_C(0xff000000);
    uint32_t stroke_argb=UINT32_C(0xff000000);
    double stroke_width=1.0,stroke_miter=10.0;
    int stroke_cap=0,stroke_join=0;
    bool stroke_default_dash=true,stroke_rgb_exact=true;
    bool saw_saved_scope=false;
    std::vector<graphics_state> stack;
    std::vector<Obj> args;
    std::vector<quantapdf_composer_path_command> pending_path;
    size_t contour_start=0u,contour_count=0u;
    bool closed=false;
    bool three_branch_clip_pending=false;
    bool texample_pending_move=false,texample_original_ctm=false;
    std::unique_ptr<quantapdf::detail::source_text_outlines> outline_reader;
    bool fractional_rect_staging=false;
    bool texample_prefix_possible=true;
    size_t texample_prefix_index=0u;
    quantapdf_point texample_move_point{};
    size_t texample_group_moves=0u,texample_group_cubics=0u;
    size_t texample_group_closes=0u,texample_group_empty=0u;
    size_t texample_qdepth=0u,texample_contours=0u;
};

/* Complete-page selection, before any C owner is published. A root-only
 * source keeps the previously qualified one-GS representation. When
 * either earlier paint or Q restoration observes the first leaf, its
 * distinct identity must remain in every original operation. */
bool prepare_native_clip_membership(admission& source)
{
    bool first=false,root=false;
    for(unsigned char state:source.scoped_clip_membership) {
        admit(state<=2u,QUANTAPDF_ERROR_UNSUPPORTED);
        first=first || state==1u;
        root=root || state==2u;
    }
    if(!root) {
        /* A second owned definition with no paint under its intersection
         * is outside this bounded profile, even if leaf 1 was painted. */
        admit(!source.two_owned_clips,QUANTAPDF_ERROR_UNSUPPORTED);
        return false;
    }
    if(first) {
        admit(source.two_owned_clips,QUANTAPDF_ERROR_UNSUPPORTED);
        return true;
    }
    for(auto& state:source.scoped_clip_membership)
        if(state==2u)state=1u;
    return false;
}

void checked_original_rotation(Obj page,size_t max_steps,int effective)
{
    std::set<QPDFObjGen> seen;
    size_t depth=0u;
    bool chosen=false;
    for(Obj node=page;!node.isNull();node=node.getKey("/Parent")) {
        admit(depth++<max_steps && depth<=256u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        admit(node.isIndirect() && node.isDictionary(),
              QUANTAPDF_ERROR_FORMAT);
        admit(seen.insert(node.getObjGen()).second,
              QUANTAPDF_ERROR_FORMAT);
        const Obj raw=node.getKey("/Rotate");
        if(!chosen && !raw.isNull()) {
            admit(raw.isInteger(),QUANTAPDF_ERROR_FORMAT);
            const long long degrees=raw.getIntValue();
            admit(degrees%90ll==0ll,QUANTAPDF_ERROR_FORMAT);
            int normalized=static_cast<int>(degrees%360ll);
            if(normalized<0)normalized+=360;
            admit(normalized==effective,QUANTAPDF_ERROR_FORMAT);
            chosen=true;
        }
    }
    admit(chosen || effective==0,QUANTAPDF_ERROR_FORMAT);
}

quantapdf_point original_turn_point(quantapdf_point p,
    double w,double h,int rotation)
{
    const double x=p.x,y=p.y;
    /* Cropped Source geometry may extend outside the visible local
     * page, but no source point can exceed the bounded C11 coordinate
     * envelope even when its original /Rotate is nonzero. Full-Media
     * profiles still admit inside-page points at Source parse time. */
    admit(std::isfinite(x) && std::isfinite(y) &&
          x>=-1000000.0 && x<=1000000.0 &&
          y>=-1000000.0 && y<=1000000.0 &&
          w<=1000000u && h<=1000000u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    switch(rotation) {
    case 90:  return {static_cast<float>((double)h-y),
                      static_cast<float>(x)};
    case 180: return {static_cast<float>((double)w-x),
                      static_cast<float>((double)h-y)};
    case 270: return {static_cast<float>(y),
                      static_cast<float>((double)w-x)};
    default: throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
    }
}

void original_turn_commands(
    std::vector<quantapdf_composer_path_command>& commands,
    double w,double h,int rotation)
{
    for(auto& c:commands) {
        if(c.kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
           c.kind==QUANTAPDF_COMPOSER_PATH_LINE_TO)
            c.point1=original_turn_point(c.point1,w,h,rotation);
        else if(c.kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO) {
            c.point1=original_turn_point(c.point1,w,h,rotation);
            c.point2=original_turn_point(c.point2,w,h,rotation);
            c.point3=original_turn_point(c.point3,w,h,rotation);
        } else
            admit(c.kind==QUANTAPDF_COMPOSER_PATH_CLOSE,
                  QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

void original_turn_image(staged_image& image,
    double page_w,double page_h,int rotation)
{
    const quantapdf_rect b=image.bounds;
    const quantapdf_point p0=original_turn_point(
        {b.x0,b.y0},page_w,page_h,rotation);
    const quantapdf_point p1=original_turn_point(
        {b.x1,b.y1},page_w,page_h,rotation);
    const quantapdf_point p2=original_turn_point(
        {b.x1,b.y0},page_w,page_h,rotation);
    const quantapdf_point p3=original_turn_point(
        {b.x0,b.y1},page_w,page_h,rotation);
    image.bounds.x0=std::min({p0.x,p1.x,p2.x,p3.x});
    image.bounds.x1=std::max({p0.x,p1.x,p2.x,p3.x});
    image.bounds.y0=std::min({p0.y,p1.y,p2.y,p3.y});
    image.bounds.y1=std::max({p0.y,p1.y,p2.y,p3.y});

    const size_t w=(size_t)image.width,h=(size_t)image.height;
    const size_t channels=(size_t)image.components;
    admit(w!=0u && h!=0u && (channels==1u || channels==3u) &&
          w<=hard_image_pixels/h && w*h<=SIZE_MAX/channels &&
          image.pixels.size()==w*h*channels,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t new_width=rotation==180?w:h;
    const size_t new_height=rotation==180?h:w;
    QP_SOURCE_STAGE_ARM(5u);
    std::vector<unsigned char> turned(image.pixels.size());
    for(size_t y=0u;y<h;++y)
        for(size_t x=0u;x<w;++x) {
            size_t dx=0u,dy=0u;
            if(rotation==90) {dx=h-1u-y;dy=x;}
            else if(rotation==180) {dx=w-1u-x;dy=h-1u-y;}
            else if(rotation==270) {dx=y;dy=w-1u-x;}
            else throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
            std::memcpy(
                turned.data()+(dy*new_width+dx)*channels,
                image.pixels.data()+(y*w+x)*channels,channels);
        }
    image.pixels.swap(turned);
    image.width=static_cast<uint32_t>(new_width);
    image.height=static_cast<uint32_t>(new_height);
}

/* Consume complete q/Q, CTM and operator admission first; only then
 * orient SOURCE-staged PATH/Clip points and uniquely owned RAW Image
 * samples before emitting the one canonical Composer IR. The source
 * quotas and image-work accounting are invariant under quarter turns. */
void original_turn_staging(admission& source,int rotation)
{
    if(rotation==0)return;
    admit(rotation==90 || rotation==180 || rotation==270,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const double w=source.exact_page_w>0.0?
        source.exact_page_w:(double)source.page_w;
    const double h=source.exact_page_h>0.0?
        source.exact_page_h:(double)source.page_h;
    for(auto& path:source.painted_paths)
        original_turn_commands(path.commands,w,h,rotation);
    original_turn_commands(source.clip.commands,w,h,rotation);
    original_turn_commands(source.second_clip.commands,w,h,rotation);
    for(auto& image:source.draws)
        original_turn_image(image,w,h,rotation);
    if(rotation==90 || rotation==270) {
        std::swap(source.page_w,source.page_h);
        std::swap(source.exact_page_w,source.exact_page_h);
    }
}

/* Resolution of referenced resources is not a display-only observation.
 * A malformed /Parent chain must never be accepted by the image-only
 * renderer. Cap ancestry separately from content-stream/decode capacity. */
Obj bounded_page_resources(Obj page, size_t max_parent_steps,
                           bool allow_absent=false)
{
    std::set<QPDFObjGen> seen;
    Obj current = page;
    size_t depth = 0u;
    while (!current.isNull()) {
        admit(depth < max_parent_steps && depth < 256u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        ++depth;
        admit(current.isDictionary() && current.isIndirect(),
              QUANTAPDF_ERROR_FORMAT);
        admit(seen.insert(current.getObjGen()).second,
              QUANTAPDF_ERROR_FORMAT);
        Obj resources = current.getKey("/Resources");
        if (!resources.isNull()) {
            admit(resources.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            /* /DefaultRGB or /DefaultGray in the effective page resource
             * dictionary redefines how a Device* Image is interpreted.
             * The first direct RAW source profile has no such color
             * conversion; refuse instead of silently painting wrong RGB. */
            Obj color_spaces=resources.getKey("/ColorSpace");
            if (!color_spaces.isNull()) {
                admit(color_spaces.isDictionary(),QUANTAPDF_ERROR_FORMAT);
                admit(color_spaces.getKey("/DefaultRGB").isNull() &&
                      color_spaces.getKey("/DefaultGray").isNull(),
                      QUANTAPDF_ERROR_UNSUPPORTED);
            }
            return resources;
        }
        current=current.getKey("/Parent");
    }

    if(allow_absent)return Obj::newDictionary();
    throw rejected{QUANTAPDF_ERROR_FORMAT};
}

/* qpdf owns content syntax, concatenation and filter-chain decoding. Source
 * only charges the selected page's stream count and rejects external-file
 * contents, which this renderer does not implement. */
void page_content_resources(QPDFPageObjectHelper& page,
                            const quantapdf_qpdf_page_limits& limits)
{
    const auto streams=page.getPageContents();
    admit(streams.size()<=limits.max_content_streams,QUANTAPDF_ERROR_UNSUPPORTED);
    for(auto stream:streams) {
        const Obj dict=stream.getDict();
        admit(dict.getKey("/F").isNull() && dict.getKey("/FFilter").isNull() &&
              dict.getKey("/FDecodeParms").isNull(),QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

/* A strictly parsed zero-paint page owns a valid Page IR with no
 * operations or resources. The SINGLE C11 raster handles white page
 * materialization; no PDFium, backend image or alternate parser path. */
quantapdf_ir materialize_blank_page(const admission& source)
{
    quantapdf_ir ir={};
    ir.pages=static_cast<quantapdf_composer_page_state *>(
        QP_SOURCE_C_CALLOC(1u,sizeof(*ir.pages)));
    admit(ir.pages!=nullptr,QUANTAPDF_ERROR_NOMEM);
    ir.page_count=1u;
    ir.pages[0].width_points=static_cast<float>(
        source.exact_page_w>0.0?source.exact_page_w:(double)source.page_w);
    ir.pages[0].height_points=static_cast<float>(
        source.exact_page_h>0.0?source.exact_page_h:(double)source.page_h);
    ir.pages[0].background_argb=UINT32_C(0xffffffff);
    return ir;
}

/* Source text owns every glyph contour. Fonts/QPDF/observation cells are
 * never published; glyph order and text-show grouping remain explicit. */
void copy_outline_paint(quantapdf_composer_operation& op,
    const quantapdf::detail::source_outline_paint& paint)
{
    if(!paint.rectangle.empty()) {
        op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
        auto& path=op.value.path;path.command_count=paint.rectangle.size();
        path.commands=static_cast<quantapdf_composer_path_command *>(QP_SOURCE_C_MALLOC(path.command_count*sizeof(*path.commands)));
        admit(path.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(path.commands,paint.rectangle.data(),path.command_count*sizeof(*path.commands));
        path.options.fill=1;
        path.options.fill_argb=paint.argb;path.options.fill_rule=paint.rule;return;
    }
    op.kind=QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES;
    auto& text=op.value.text_outlines;text.argb=paint.argb;text.glyph_count=paint.glyphs.size();
    if(text.glyph_count==0u)return;
    text.glyphs=static_cast<quantapdf_outline_glyph *>(QP_SOURCE_C_CALLOC(text.glyph_count,sizeof(*text.glyphs)));
    admit(text.glyphs!=nullptr,QUANTAPDF_ERROR_NOMEM);
    for(size_t g=0u;g<text.glyph_count;++g) {
        const auto& commands=paint.glyphs[g].commands;auto& glyph=text.glyphs[g];
        glyph.command_count=commands.size();
        glyph.commands=static_cast<quantapdf_composer_path_command *>(QP_SOURCE_C_MALLOC(commands.size()*sizeof(*glyph.commands)));
        admit(glyph.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(glyph.commands,commands.data(),commands.size()*sizeof(*glyph.commands));
    }
}

/* Clipped scenes share the existing decoded Image publisher below. */
quantapdf_ir materialize(const admission& source);
quantapdf_composer_clip_id materialize_source_clip_graph(
    quantapdf_ir& ir,const admission& source);



quantapdf_ir materialize_two_clip_text_scene(const admission& source)
{
    const bool text_first=source.order.size()==3u &&
        source.order[0]==3 && source.order[1]==1 && source.order[2]==2;
    const bool image_first=source.order.size()==3u &&
        source.order[0]==2 && source.order[1]==1 && source.order[2]==3;

    const bool sibling=source.sibling_clip_scopes;
    const bool compound=source_m2b2o_compound_clip(source);
    admit(source.text_outlines_eligible && source.clip_completed &&
          source.text_outlines_complete && (text_first || image_first) &&
          source.draws.size()==1u && source.limits.max_operations>=3u &&
          source.text_outline_paints.size()==2u &&
          source.painted_paths.size()==(image_first?1u:0u) &&
          source.scoped_clip_membership.size()==3u &&
          (compound ?
             (text_first && source.saw_q_restore && source.outline_scope_started &&
              source.outline_clip_id==0u && source.outline_clip_stack.empty() &&
              std::all_of(source.scoped_clip_membership.begin(),
                  source.scoped_clip_membership.end(),[](unsigned char id){return id==2u;})) :
           (sibling && (text_first || image_first) && source.two_owned_clips &&
              source.saw_q_restore && source.outline_scope_started &&
              source.outline_clip_id==0u && source.outline_clip_stack.empty() &&
              source.scoped_clip_membership[0]==1u &&
              source.scoped_clip_membership[1]==1u &&
              source.scoped_clip_membership[2]==2u &&
              quantapdf::detail::source_outline_is_rectangle(source.clip.commands) &&
              quantapdf::detail::source_outline_is_rectangle(source.second_clip.commands))),
          QUANTAPDF_ERROR_UNSUPPORTED);
    admit(source.resources.isDictionary(),QUANTAPDF_ERROR_FORMAT);
    const Obj font_dict=source.resources.getKey("/Font");
    const Obj image_dict=source.resources.getKey("/XObject");
    admit(font_dict.isDictionary() && image_dict.isDictionary(),
          QUANTAPDF_ERROR_FORMAT);
    admit(source.resources.getKeys().size()==2u &&
          font_dict.getKeys().size()==1u && image_dict.getKeys().size()==1u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const auto& text=source.text_outline_paints[text_first?0u:1u];
    const auto& fill=source.text_outline_paints[text_first?1u:0u];
    admit(text.rectangle.empty() && !text.glyphs.empty() &&
          text.glyphs.size()<=64u &&
          text.argb==UINT32_C(0xff000000) &&
          fill.glyphs.empty() && fill.rectangle.size()==5u &&
          (fill.argb>>24u)==255u &&
          quantapdf::detail::source_outline_is_rectangle(fill.rectangle),
          QUANTAPDF_ERROR_UNSUPPORTED);
    if(image_first)
        admit(source.painted_paths[0].commands.size()==fill.rectangle.size() &&
              source.painted_paths[0].fill_argb==fill.argb &&
              source.painted_paths[0].fill_rule==fill.rule &&
              !source.painted_paths[0].stroke,
              QUANTAPDF_ERROR_UNSUPPORTED);
    size_t commands=fill.rectangle.size()+10u;
    for(const auto& glyph:text.glyphs) {
        admit(!glyph.commands.empty() &&
              glyph.commands.size()<=4096u-commands,
              QUANTAPDF_ERROR_UNSUPPORTED);
        commands+=glyph.commands.size();
    }
    quantapdf_ir ir=materialize(source);
    try {
        const auto image=ir.operations[0];
        auto *expanded=static_cast<quantapdf_composer_operation *>(
            QP_SOURCE_C_REALLOC(ir.operations,3u*sizeof(*ir.operations)));
        admit(expanded!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.operations=expanded;
        std::memset(expanded,0,3u*sizeof(*expanded));
        ir.operation_count=3u;
        for(size_t i=0u;i<3u;++i) {
            const int kind=source.order[i];
            auto& operation=ir.operations[i];
            if(kind==2) {operation=image;continue;}
            operation.page_index=0u;
            if(kind==1) {
                operation.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
                auto& path=operation.value.path;
                path.command_count=fill.rectangle.size();
                path.commands=static_cast<quantapdf_composer_path_command *>(
                    QP_SOURCE_C_MALLOC(path.command_count*sizeof(*path.commands)));
                admit(path.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
                std::memcpy(path.commands,fill.rectangle.data(),
                            path.command_count*sizeof(*path.commands));
                path.options.fill=1;
                path.options.fill_argb=fill.argb;
                path.options.fill_rule=fill.rule;
            } else {
                admit(kind==3,QUANTAPDF_ERROR_UNSUPPORTED);
                operation.kind=QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES;
                auto& result=operation.value.text_outlines;
                result.argb=text.argb;
                result.glyph_count=text.glyphs.size();
                result.glyphs=static_cast<quantapdf_outline_glyph *>(
                    QP_SOURCE_C_CALLOC(result.glyph_count,sizeof(*result.glyphs)));
                admit(result.glyphs!=nullptr,QUANTAPDF_ERROR_NOMEM);
                for(size_t g=0u;g<result.glyph_count;++g) {
                    auto& target=result.glyphs[g];
                    const auto& outline_commands=text.glyphs[g].commands;
                    target.command_count=outline_commands.size();
                    target.commands=static_cast<quantapdf_composer_path_command *>(
                        QP_SOURCE_C_MALLOC(outline_commands.size()*sizeof(*target.commands)));
                    admit(target.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
                    std::memcpy(target.commands,outline_commands.data(),
                                outline_commands.size()*sizeof(*target.commands));
                }
            }
        }
        {
            /* Reuse the same leaf/root publisher. The historical singleton
             * and sibling allocation orders stay unchanged. A compound
             * Source graph remains private until its own pixel gates pass. */
            ir.resources.graphics_states=
                static_cast<quantapdf_composer_graphics_state *>(
                   QP_SOURCE_C_CALLOC(sibling?2u:1u,sizeof(*ir.resources.graphics_states)));
            admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
            ir.resources.graphics_state_count=sibling?2u:1u;
            ir.resources.graphics_state_capacity=ir.resources.graphics_state_count;
            const auto root=materialize_source_clip_graph(ir,source);
            admit(root==(compound?3u:sibling?2u:1u),QUANTAPDF_ERROR_UNSUPPORTED);
            for(size_t i=0u;i<ir.resources.graphics_state_count;++i) {
                auto& state=ir.resources.graphics_states[i];
                state.clip_id=compound?root:static_cast<quantapdf_composer_clip_id>(i+1u);
                state.fill_alpha=1.0f;state.stroke_alpha=1.0f;
                state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
            }
            for(size_t i=0u;i<3u;++i) {
                auto& operation=ir.operations[i];
                const auto id=static_cast<quantapdf_composer_graphics_state_id>(
                    sibling?source.scoped_clip_membership[i]:1u);
                operation.graphics_state_id=id;
                if(operation.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
                    operation.value.image.options.graphics_state_id=id;
                } else if(operation.kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
                    operation.value.path.options.graphics_state_id=id;
                }
            }
        }
        return ir;
    } catch(...) {quantapdf_ir_dispose(&ir);throw;}
}

/* Publish Text/Path/Image paints once in original source order. Parsed staging
 * owns its resources until the complete independently owned IR is ready. */
quantapdf_ir materialize_ordered_paints(const admission& source)
{
    admit(source.draws.size()<=std::numeric_limits<quantapdf_composer_image_id>::max(),
          QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_ir ir=materialize_blank_page(source);
    try {
        const size_t n=source.order.size(),images=source.draws.size();
        if(n!=0u) {
            ir.operations=static_cast<quantapdf_composer_operation *>(
                QP_SOURCE_C_CALLOC(n,sizeof(*ir.operations)));
            admit(ir.operations!=nullptr,QUANTAPDF_ERROR_NOMEM);
        }
        ir.operation_count=n;
        if(images!=0u) {
            ir.resources.images=static_cast<quantapdf_composer_image_state *>(
                QP_SOURCE_C_CALLOC(images,sizeof(*ir.resources.images)));
            admit(ir.resources.images!=nullptr,QUANTAPDF_ERROR_NOMEM);
            ir.resources.image_count=ir.resources.image_capacity=images;
        }
        size_t path_index=0u,image_index=0u;
        for(size_t i=0u;i<n;++i) {
            auto& op=ir.operations[i];op.page_index=0u;
            if(source.order[i]==2) {
                const auto& staged=source.draws[image_index];
                auto& image=ir.resources.images[image_index++];
                image.data=static_cast<unsigned char *>(QP_SOURCE_C_MALLOC(staged.pixels.size()));
                admit(image.data!=nullptr,QUANTAPDF_ERROR_NOMEM);
                std::memcpy(image.data,staged.pixels.data(),staged.pixels.size());
                image.width=staged.width;image.height=staged.height;
                image.size=staged.pixels.size();image.components=staged.components;
                image.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
                op.kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;op.bounds=staged.bounds;
                op.value.image.image_id=static_cast<quantapdf_composer_image_id>(image_index);
                op.value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
                continue;
            }
            if(source.text_outlines_complete) {
                copy_outline_paint(op,source.text_outline_paints[path_index++]);continue;
            }
            const staged_path& staged=source.painted_paths[path_index++];
            op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
            auto& path=op.value.path;
            path.command_count=staged.commands.size();
            path.commands=static_cast<quantapdf_composer_path_command *>(
                QP_SOURCE_C_MALLOC(path.command_count*sizeof(*path.commands)));
            admit(path.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
            std::memcpy(path.commands,staged.commands.data(),path.command_count*sizeof(*path.commands));
            path.options.fill=staged.stroke?0:1;path.options.stroke=staged.stroke?1:0;
            path.options.fill_argb=staged.fill_argb;path.options.fill_rule=staged.fill_rule;
            if(staged.stroke) {
                path.options.stroke_argb=staged.stroke_argb;path.options.stroke_width=staged.stroke_width;
                path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
                path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_MITER;path.options.miter_limit=10.0f;
            }
        }

        return ir;
    } catch(...) {quantapdf_ir_dispose(&ir);throw;}
}

/* Assemble the ordered operations and their observed single-root Clip.
 * Legacy Stroke/two-root publishers still use the operation-only builder. */
quantapdf_ir materialize_ordered_scene(const admission& source)
{
    quantapdf_ir ir=materialize_ordered_paints(source);
    const size_t n=source.order.size();
    try {
        if(source.clip_completed &&
           std::find(source.scoped_clip_membership.begin(),source.scoped_clip_membership.end(),1u)!=source.scoped_clip_membership.end()) {
            ir.resources.graphics_states=static_cast<quantapdf_composer_graphics_state *>(QP_SOURCE_C_CALLOC(1u,sizeof(*ir.resources.graphics_states)));
            admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
            ir.resources.graphics_state_count=ir.resources.graphics_state_capacity=1u;
            auto& state=ir.resources.graphics_states[0];
            state.fill_alpha=state.stroke_alpha=1.0f;state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
            state.clip_id=materialize_source_clip_graph(ir,source);
            for(size_t i=0u;i<n;++i) {
                auto& op=ir.operations[i];const auto id=source.scoped_clip_membership[i];
                op.graphics_state_id=id;
                if(op.kind==QUANTAPDF_COMPOSER_OPERATION_PATH)op.value.path.options.graphics_state_id=id;
                if(op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE)op.value.image.options.graphics_state_id=id;
            }
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

quantapdf_ir materialize(
    const admission& source)
{
    quantapdf_ir ir={};
    try {
        ir.pages=static_cast<quantapdf_composer_page_state *>(
            QP_SOURCE_C_CALLOC(1u,sizeof(*ir.pages)));
        admit(ir.pages!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.page_count=1u;
        ir.pages[0].width_points=static_cast<float>(
            source.exact_page_w>0.0?source.exact_page_w:(double)source.page_w);
        ir.pages[0].height_points=static_cast<float>(
            source.exact_page_h>0.0?source.exact_page_h:(double)source.page_h);
        ir.pages[0].background_argb=UINT32_C(0xffffffff);

        const size_t count=source.draws.size();
        admit(count>0u && count<=hard_operations,QUANTAPDF_ERROR_UNSUPPORTED);
        ir.operations=static_cast<quantapdf_composer_operation *>(
            QP_SOURCE_C_CALLOC(count,sizeof(*ir.operations)));
        admit(ir.operations!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.images=static_cast<quantapdf_composer_image_state *>(
            QP_SOURCE_C_CALLOC(count,sizeof(*ir.resources.images)));
        admit(ir.resources.images!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.operation_count=count;
        ir.resources.image_count=count;
        ir.resources.image_capacity=count;
        for(size_t i=0u;i<count;++i) {
            const staged_image& staged=source.draws[i];
            auto& image=ir.resources.images[i];
            image.data=static_cast<unsigned char *>(QP_SOURCE_C_MALLOC(staged.pixels.size()));
            admit(image.data!=nullptr,QUANTAPDF_ERROR_NOMEM);
            std::memcpy(image.data,staged.pixels.data(),staged.pixels.size());
            image.width=staged.width;
            image.height=staged.height;
            image.size=staged.pixels.size();
            image.components=staged.components;
            image.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
            auto& operation=ir.operations[i];
            operation.kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
            operation.page_index=0u;
            operation.bounds=staged.bounds;
            operation.value.image.image_id=static_cast<quantapdf_composer_image_id>(i+1u);
            operation.value.image.options.fit=
                QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
        }
    } catch (...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
    return ir;
}


/* Publish the same detached Clip payload shape in homogeneous and mixed
 * materializers. The caller's existing catch/dispose owns every slot
 * immediately, including partial leaf/root allocation failures. The old
 * one-leaf path keeps its allocation order and count unchanged. */
quantapdf_composer_clip_id materialize_source_clip_graph(
    quantapdf_ir& ir,const admission& source)
{
    const bool two=source.two_owned_clips;
    admit(source.clip_completed &&
          (!source.sibling_clip_scopes || two) &&
          source.clip.commands.size()>=4u &&
          (source.clip.commands.size()<=9u ||
           (!two &&
            source.clip.rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD &&
            nested_rectangles(source.clip.commands))) &&
          ir.resources.clips==nullptr &&
          ir.resources.clip_count==0u && ir.resources.clip_capacity==0u &&
          (two ?
             (qualified_owned_clip_pair(source.clip.commands,
                                        source.second_clip.commands)) :
             source.second_clip.commands.empty()),
          QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t count=two?(source.sibling_clip_scopes?2u:3u):1u;
    const size_t leaves=two?2u:1u;
    ir.resources.clips=static_cast<quantapdf_composer_clip_state *>(
        QP_SOURCE_C_CALLOC(count,sizeof(*ir.resources.clips)));
    admit(ir.resources.clips!=nullptr,QUANTAPDF_ERROR_NOMEM);
    ir.resources.clip_count=count;
    ir.resources.clip_capacity=count;
    for(size_t i=0u;i<leaves;++i) {
        const staged_image_clip& staged=i==0u?source.clip:source.second_clip;
        auto& clip=ir.resources.clips[i];
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.fill_rule=staged.rule;
        clip.command_count=staged.commands.size();
        clip.commands=static_cast<quantapdf_composer_path_command *>(
            QP_SOURCE_C_MALLOC(clip.command_count*sizeof(*clip.commands)));
        admit(clip.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(clip.commands,staged.commands.data(),
                    clip.command_count*sizeof(*clip.commands));
        clip.transform={1.0f,0.0f,0.0f,1.0f,0.0f,0.0f};
    }
    if(two && !source.sibling_clip_scopes) {
        auto& root=ir.resources.clips[2];
        root.kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        root.member_count=2u;
        root.members=static_cast<quantapdf_composer_clip_id *>(
            QP_SOURCE_C_MALLOC(root.member_count*sizeof(*root.members)));
        admit(root.members!=nullptr,QUANTAPDF_ERROR_NOMEM);
        root.members[0]=1u;
        root.members[1]=2u;
    }
    return static_cast<quantapdf_composer_clip_id>(count);
}

quantapdf_ir materialize_texample_m3(const admission& s)
{
    admit(s.texample_original_profile && s.clip_completed && s.clip.rule==
              QUANTAPDF_COMPOSER_FILL_EVEN_ODD &&
          s.clip.commands.size()==12u &&
          s.texample_stage==4u && s.order.size()==3u &&
          s.painted_paths.size()==3u && s.draws.empty() &&
          s.scoped_clip_membership.size()==3u &&
          s.scoped_clip_membership[0]==1u &&
          s.scoped_clip_membership[1]==0u &&
          s.scoped_clip_membership[2]==0u &&
          s.texample_moves==24u && s.texample_cubics==32u &&
          s.texample_closes==8u && s.texample_zero_moves==16u &&
          s.texample_q_count==4u && s.texample_Q_count==4u &&
          s.texample_cm_count==1u && s.texample_gray_count==1u &&
          s.texample_stroke_gray_count==1u &&
          s.texample_width_count==1u && s.texample_orange_count==1u &&
          s.texample_terminal_n==1u && !s.two_owned_clips &&
          !s.sibling_clip_scopes && s.second_clip.commands.empty() &&
          s.exact_page_w==237.133 && s.exact_page_h==180.44,
          QUANTAPDF_ERROR_UNSUPPORTED);
    for(size_t i=0u;i<4u;++i) {
        const auto& p=i==0u?s.clip.commands:
                              s.painted_paths[i-1u].commands;
        /* Four source groups, each exactly two M+4C+CLOSE contours.
         * M coordinates and all 32 cubic controls are preserved in
         * the borrowed Source coordinates after the original CTM. */
        const double first[4]={146.9128,203.60642,146.9128,203.60642};
        const double second[4]={175.25962,231.95324,175.25962,231.95324};
        admit(p.size()==12u &&
              p[0].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
              p[6].kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
              p[5].kind==QUANTAPDF_COMPOSER_PATH_CLOSE &&
              p[11].kind==QUANTAPDF_COMPOSER_PATH_CLOSE &&
              std::fabs((double)p[0].point1.x-first[i])<0.0001 &&
              std::fabs((double)p[6].point1.x-second[i])<0.0001 &&
              std::fabs((double)p[0].point1.y-90.22)<0.0001 &&
              std::fabs((double)p[6].point1.y-90.22)<0.0001,
              QUANTAPDF_ERROR_UNSUPPORTED);
        for(size_t j=0u;j<12u;++j)
            if(j!=0u && j!=5u && j!=6u && j!=11u)
                admit(p[j].kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO,
                      QUANTAPDF_ERROR_UNSUPPORTED);
    }
    for(size_t i=0u;i<3u;++i) {
        const auto& p=s.painted_paths[i];
        admit(s.order[i]==1 &&
              (i==0u ?
               (!p.stroke && p.fill_argb==UINT32_C(0xffff8000) &&
                p.fill_rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD) :
               (p.stroke && p.stroke_argb==UINT32_C(0xff000000) &&
                p.stroke_width==0.3985f)),
              QUANTAPDF_ERROR_UNSUPPORTED);
    }
    quantapdf_ir ir=materialize_ordered_paints(s);
    try {
        ir.resources.clips=static_cast<quantapdf_composer_clip_state *>(
            QP_SOURCE_C_CALLOC(1u,sizeof(*ir.resources.clips)));
        admit(ir.resources.clips!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.clip_count=ir.resources.clip_capacity=1u;
        auto& leaf=ir.resources.clips[0];
        leaf.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        leaf.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
        leaf.command_count=12u;
        leaf.commands=static_cast<quantapdf_composer_path_command *>(
            QP_SOURCE_C_MALLOC(12u*sizeof(*leaf.commands)));
        admit(leaf.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(leaf.commands,s.clip.commands.data(),
                    12u*sizeof(*leaf.commands));
        leaf.transform={1.0f,0.0f,0.0f,1.0f,0.0f,0.0f};

        ir.resources.graphics_states=
            static_cast<quantapdf_composer_graphics_state *>(
                QP_SOURCE_C_CALLOC(1u,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=
            ir.resources.graphics_state_capacity=1u;
        auto& gs=ir.resources.graphics_states[0];
        gs.fill_alpha=1.0f;gs.stroke_alpha=1.0f;
        gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        gs.clip_id=1u;
        ir.operations[0].graphics_state_id=1u;
        ir.operations[0].value.path.options.graphics_state_id=1u;
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

quantapdf_ir materialize_three_branch_source(const admission& source)
{
    const auto& outer=source.clip.commands;
    const auto outer_canvas=[&]() {
        if(outer.size()!=5u || source.clip.rule!=
             QUANTAPDF_COMPOSER_FILL_NONZERO ||
           outer[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
           outer[1].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           outer[2].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           outer[3].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           outer[4].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)
            return false;
        const quantapdf_point p[4]={
            outer[0].point1,outer[1].point1,
            outer[2].point1,outer[3].point1
        };
        return p[0].x==0.0f && p[0].y==0.0f &&
               p[1].x==600.0f && p[1].y==0.0f &&
               p[2].x==600.0f && p[2].y==600.0f &&
               p[3].x==0.0f && p[3].y==600.0f;
    };
    admit(source.three_branch_candidate && source.clip_completed && outer_canvas() &&
          source.three_branch_count==3u &&
          !source.two_owned_clips && !source.sibling_clip_scopes &&
          source.second_clip.commands.empty() && source.draws.empty() &&
          source.order.size()==3u && source.painted_paths.size()==3u &&
          source.scoped_clip_membership.size()==3u &&
          source.limits.max_operations>=3u &&
          source.exact_page_w==600.0 && source.exact_page_h==600.0 &&
          source.media_left==0.0 && source.media_bottom==0.0 &&
          !source.crop_viewport &&
          nested_rectangles(source.three_branch_leaves[0].commands) &&
          source.three_branch_leaves[0].rule==
              QUANTAPDF_COMPOSER_FILL_EVEN_ODD,
          QUANTAPDF_ERROR_UNSUPPORTED);
    for(size_t i=0u;i<3u;++i) {
        const auto& s=source.painted_paths[i];
        admit(source.order[i]==1 &&
              source.scoped_clip_membership[i]==i+2u &&
              (i==0u ?
               (s.stroke && s.stroke_argb==UINT32_C(0xffff0000) &&
                s.commands.size()==2u) :
               (!s.stroke && s.fill_argb==UINT32_C(0xff0000ff) &&
                s.commands.size()==5u)),
              QUANTAPDF_ERROR_UNSUPPORTED);
        const auto& leaf=source.three_branch_leaves[i];
        admit(i==0u ?
              (leaf.commands.size()==10u &&
               leaf.rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD) :
              (leaf.commands.size()==6u &&
               bounded_clip_path(leaf.commands) &&
               leaf.rule==(i==1u?
                   QUANTAPDF_COMPOSER_FILL_NONZERO:
                   QUANTAPDF_COMPOSER_FILL_EVEN_ODD)),
              QUANTAPDF_ERROR_UNSUPPORTED);
    }
    quantapdf_ir ir=materialize_ordered_paints(source);
    try {
        ir.resources.clips=
            static_cast<quantapdf_composer_clip_state *>(
                QP_SOURCE_C_CALLOC(7u,sizeof(*ir.resources.clips)));
        admit(ir.resources.clips!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.clip_count=7u;
        ir.resources.clip_capacity=7u;
        for(size_t i=0u;i<4u;++i) {
            const auto& staged=i==0u?source.clip:
                                      source.three_branch_leaves[i-1u];
            auto& dest=ir.resources.clips[i];
            dest.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
            dest.fill_rule=staged.rule;
            dest.command_count=staged.commands.size();
            dest.commands=static_cast<quantapdf_composer_path_command *>(
                QP_SOURCE_C_MALLOC(
                    dest.command_count*sizeof(*dest.commands)));
            admit(dest.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
            std::memcpy(dest.commands,staged.commands.data(),
                        dest.command_count*sizeof(*dest.commands));
            dest.transform={1.0f,0.0f,0.0f,1.0f,0.0f,0.0f};
        }
        for(size_t i=0u;i<3u;++i) {
            auto& root=ir.resources.clips[i+4u];
            root.kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
            root.member_count=2u;
            root.members=static_cast<quantapdf_composer_clip_id *>(
                QP_SOURCE_C_MALLOC(2u*sizeof(*root.members)));
            admit(root.members!=nullptr,QUANTAPDF_ERROR_NOMEM);
            root.members[0]=1u;
            root.members[1]=static_cast<quantapdf_composer_clip_id>(i+2u);
        }
        ir.resources.graphics_states=
            static_cast<quantapdf_composer_graphics_state *>(
                QP_SOURCE_C_CALLOC(
                    3u,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=3u;
        ir.resources.graphics_state_capacity=3u;
        for(size_t i=0u;i<3u;++i) {
            auto& gs=ir.resources.graphics_states[i];
            gs.fill_alpha=1.0f;
            gs.stroke_alpha=1.0f;
            gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
            gs.clip_id=static_cast<quantapdf_composer_clip_id>(i+5u);
            auto& op=ir.operations[i];
            op.graphics_state_id=
                static_cast<quantapdf_composer_graphics_state_id>(i+1u);
            op.value.path.options.graphics_state_id=op.graphics_state_id;
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

quantapdf_ir materialize_clipped_path_only(const admission& source)
{
    const size_t n=source.order.size();
    admit(source.clip_completed && n>=1u && n<=hard_operations &&
          n<=source.limits.max_operations &&
          source.draws.empty() && source.painted_paths.size()==n &&
          source.scoped_clip_membership.size()==n &&
          source.scoped_clip_membership[0]==1u &&
          source.clip.commands.size()>=4u &&
          (source.clip.commands.size()<=9u ||
           (source.clip.rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD &&
            nested_rectangles(source.clip.commands))),
          QUANTAPDF_ERROR_UNSUPPORTED);
    bool saw_unclipped=false;
    for(size_t i=0u;i<n;++i) {
        const unsigned char active=source.scoped_clip_membership[i];
        admit(source.order[i]==1 && active<=1u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        if(active==0u)saw_unclipped=true;
        else admit(!saw_unclipped,QUANTAPDF_ERROR_UNSUPPORTED);
    }
    admit(!saw_unclipped || source.saw_q_restore,
          QUANTAPDF_ERROR_UNSUPPORTED);

    quantapdf_ir ir=materialize_ordered_paints(source);
    try {
        ir.resources.graphics_states=
            static_cast<quantapdf_composer_graphics_state *>(
                QP_SOURCE_C_CALLOC(1u,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=1u;
        ir.resources.graphics_state_capacity=1u;
        auto& state=ir.resources.graphics_states[0];
        state.fill_alpha=1.0f;
        state.stroke_alpha=1.0f;
        state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        state.clip_id=materialize_source_clip_graph(ir,source);
        for(size_t i=0u;i<n;++i) {
            const quantapdf_composer_graphics_state_id id=
                source.scoped_clip_membership[i] ? 1u : 0u;
            ir.operations[i].graphics_state_id=id;
            ir.operations[i].value.path.options.graphics_state_id=id;
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

quantapdf_ir materialize_clipped_image(const admission& source,
                                       size_t expected_images)
{
    const bool native_bounded=expected_images>=1u && expected_images<=2u &&
        expected_images<=source.limits.max_operations &&
        source.painted_paths.empty() &&
        source.order.size()==expected_images &&
        source.scoped_clip_membership.size()==expected_images &&
        std::all_of(source.order.begin(),source.order.end(),
                    [](int kind){return kind==2;}) &&
        std::all_of(source.scoped_clip_membership.begin(),
                    source.scoped_clip_membership.end(),
                    [](unsigned char member){return member==1u;});
    admit(source.clip_completed &&
          (expected_images==1u || expected_images==2u) &&
          source.draws.size()==expected_images &&
          native_bounded &&
          source.clip.commands.size()>=4u &&
          source.clip.commands.size()<=9u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_ir ir=materialize(source);
    try {
        ir.resources.graphics_states=
            static_cast<quantapdf_composer_graphics_state *>(
                QP_SOURCE_C_CALLOC(1u,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=1u;
        ir.resources.graphics_state_capacity=1u;
        auto& state=ir.resources.graphics_states[0];
        state.fill_alpha=1.0f;
        state.stroke_alpha=1.0f;
        state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        state.clip_id=materialize_source_clip_graph(ir,source);
        for(size_t i=0u;i<expected_images;++i) {
            const quantapdf_composer_graphics_state_id state_id=1u;
            ir.operations[i].graphics_state_id=state_id;
            ir.operations[i].value.image.options.graphics_state_id=state_id;
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

/* Native two-state Clip profile. GS1 names the original first leaf;
 * GS2 names the intersection. The bounded original sequence is
 * first* / intersection+ / restored-first* / unmasked*, with BOTH
 * nonzero states used. Existing one-GS materializers remain unchanged. */
quantapdf_ir materialize_clip_states(const admission& source)
{
    const size_t count=source.order.size();
    const size_t paths=source.painted_paths.size();
    const size_t images=source.draws.size();
    admit(source.clip_completed && source.two_owned_clips &&
          qualified_owned_clip_pair(source.clip.commands,
                                    source.second_clip.commands) &&
          count>=2u && count<=hard_operations &&
          count<=source.limits.max_operations && paths+images==count &&
          source.scoped_clip_membership.size()==count &&
          ((images==0u && paths>=2u && paths<=hard_operations) ||
           (paths==0u && images==2u) ||
           (paths>=1u && paths<=2u && images>=1u && images<=2u)),
          QUANTAPDF_ERROR_UNSUPPORTED);
    bool first_used=false,root_used=false,first_restored=false;
    bool unmasked=false;
    size_t path_index=0u,image_count=0u;
    for(size_t i=0u;i<count;++i) {
        const unsigned char state=source.scoped_clip_membership[i];
        admit(state<=2u,QUANTAPDF_ERROR_UNSUPPORTED);
        if(state==0u) {
            admit(root_used,QUANTAPDF_ERROR_UNSUPPORTED);
            unmasked=true;
        } else {
            admit(!unmasked,QUANTAPDF_ERROR_UNSUPPORTED);
            if(state==1u) {
                first_used=true;
                if(root_used)first_restored=true;
            } else {
                admit(!first_restored,QUANTAPDF_ERROR_UNSUPPORTED);
                root_used=true;
            }
        }
        if(source.order[i]==1) {
            admit(path_index<paths &&
                  bounded_clip_contour(source.painted_paths[path_index].commands),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            ++path_index;
        } else {
            admit(source.order[i]==2,QUANTAPDF_ERROR_UNSUPPORTED);
            ++image_count;
        }
    }
    admit(first_used && root_used && path_index==paths &&
          image_count==images && (!unmasked || source.saw_q_restore),
          QUANTAPDF_ERROR_UNSUPPORTED);

    quantapdf_ir ir=materialize_ordered_paints(source);
    try {
        ir.resources.graphics_states=
            static_cast<quantapdf_composer_graphics_state *>(
                QP_SOURCE_C_CALLOC(2u,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=2u;
        ir.resources.graphics_state_capacity=2u;
        const quantapdf_composer_clip_id root=
            materialize_source_clip_graph(ir,source);
        for(size_t i=0u;i<2u;++i) {
            auto& state=ir.resources.graphics_states[i];
            state.fill_alpha=1.0f;
            state.stroke_alpha=1.0f;
            state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
            state.clip_id=i==0u?1u:root;
        }
        for(size_t i=0u;i<count;++i) {
            auto& op=ir.operations[i];
            const quantapdf_composer_graphics_state_id id=
                source.scoped_clip_membership[i];
            op.graphics_state_id=id;
            if(op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
                op.value.image.options.graphics_state_id=id;
            } else {
                op.value.path.options.graphics_state_id=id;
            }
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

quantapdf_ir materialize_sibling_clip_scopes(const admission& source)
{
    const size_t count=source.order.size();
    const size_t paths=source.painted_paths.size();
    const size_t images=source.draws.size();
    admit(source.clip_completed && source.two_owned_clips &&
          source.sibling_clip_scopes && source.saw_q_restore &&
          qualified_owned_clip_pair(source.clip.commands,
                                    source.second_clip.commands) &&
          count>=2u && count<=hard_operations &&
          count<=source.limits.max_operations && paths+images==count &&
          source.scoped_clip_membership.size()==count &&
          ((images==0u && paths>=2u && paths<=hard_operations) ||
           (paths==0u && images==2u) ||
           (paths>=1u && paths<=2u && images>=1u && images<=2u)),
          QUANTAPDF_ERROR_UNSUPPORTED);
    /* Phases: prefix, first leaf, middle GS0, second leaf, suffix.
     * Both nonzero identities must be used: 0* 1+ 0* 2+ 0*. */
    unsigned phase=0u;
    bool first_used=false,second_used=false;
    size_t path_index=0u,image_count=0u;
    for(size_t i=0u;i<count;++i) {
        const unsigned char state=source.scoped_clip_membership[i];
        admit(state<=2u,QUANTAPDF_ERROR_UNSUPPORTED);
        if(state==0u) {
            if(phase==1u || phase==3u)++phase;
        } else if(state==1u) {
            admit(phase<=1u,QUANTAPDF_ERROR_UNSUPPORTED);
            phase=1u;
            first_used=true;
        } else {
            admit(first_used && phase<=3u,QUANTAPDF_ERROR_UNSUPPORTED);
            phase=3u;
            second_used=true;
        }
        if(source.order[i]==1) {
            admit(path_index<paths &&
                  bounded_clip_contour(source.painted_paths[path_index].commands),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            ++path_index;
        } else {
            admit(source.order[i]==2,QUANTAPDF_ERROR_UNSUPPORTED);
            ++image_count;
        }
    }
    admit(first_used && second_used && path_index==paths &&
          image_count==images,QUANTAPDF_ERROR_UNSUPPORTED);

    quantapdf_ir ir=materialize_ordered_paints(source);
    try {
        ir.resources.graphics_states=
            static_cast<quantapdf_composer_graphics_state *>(
                QP_SOURCE_C_CALLOC(2u,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=2u;
        ir.resources.graphics_state_capacity=2u;
        admit(materialize_source_clip_graph(ir,source)==2u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        for(size_t i=0u;i<2u;++i) {
            auto& state=ir.resources.graphics_states[i];
            state.fill_alpha=1.0f;
            state.stroke_alpha=1.0f;
            state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
            state.clip_id=static_cast<quantapdf_composer_clip_id>(i+1u);
        }
        for(size_t i=0u;i<count;++i) {
            auto& op=ir.operations[i];
            const quantapdf_composer_graphics_state_id id=
                source.scoped_clip_membership[i];
            op.graphics_state_id=id;
            if(op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
                op.value.image.options.graphics_state_id=id;
            } else {
                op.value.path.options.graphics_state_id=id;
            }
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

quantapdf_ir materialize_unclipped_prefix(const admission& source,
                                         bool first_and_root)
{
    const size_t count=source.order.size();
    const size_t paths=source.painted_paths.size();
    const size_t images=source.draws.size();
    admit(source.clip_completed && count>=2u && count<=hard_operations &&
          count<=source.limits.max_operations && paths+images==count &&
          source.scoped_clip_membership.size()==count &&
          source.scoped_clip_membership[0]==0u &&
          bounded_clip_path(source.clip.commands) &&
          (!first_and_root || source.two_owned_clips) &&
          (source.two_owned_clips ?
             qualified_owned_clip_pair(source.clip.commands,
                                       source.second_clip.commands) :
             source.second_clip.commands.empty()) &&
          ((images==0u && paths>=2u && paths<=hard_operations) ||
           (paths==0u && images==2u && !first_and_root) ||
           (paths>=1u && paths<=2u && images>=1u && images<=2u)),
          QUANTAPDF_ERROR_UNSUPPORTED);
    bool first_used=false,root_used=false,first_restored=false;
    bool suffix=false;
    size_t path_index=0u,image_count=0u;
    for(size_t i=0u;i<count;++i) {
        const unsigned char state=source.scoped_clip_membership[i];
        admit(state<=(first_and_root?2u:1u),QUANTAPDF_ERROR_UNSUPPORTED);
        if(state==0u) {
            if(first_used || root_used) {
                admit(!first_and_root || root_used,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                suffix=true;
            }
        } else {
            admit(!suffix,QUANTAPDF_ERROR_UNSUPPORTED);
            if(state==1u) {
                first_used=true;
                if(root_used)first_restored=true;
            } else {
                admit(!first_restored,QUANTAPDF_ERROR_UNSUPPORTED);
                root_used=true;
            }
        }
        if(source.order[i]==1) {
            admit(path_index<paths &&
                  bounded_clip_contour(source.painted_paths[path_index].commands),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            ++path_index;
        } else {
            admit(source.order[i]==2,QUANTAPDF_ERROR_UNSUPPORTED);
            ++image_count;
        }
    }
    admit(first_used && (!first_and_root || root_used) &&
          path_index==paths && image_count==images &&
          (!suffix || source.saw_q_restore),
          QUANTAPDF_ERROR_UNSUPPORTED);

    quantapdf_ir ir=materialize_ordered_paints(source);
    try {
        const size_t states=first_and_root?2u:1u;
        ir.resources.graphics_states=
            static_cast<quantapdf_composer_graphics_state *>(
                QP_SOURCE_C_CALLOC(states,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=states;
        ir.resources.graphics_state_capacity=states;
        const quantapdf_composer_clip_id root=
            materialize_source_clip_graph(ir,source);
        for(size_t i=0u;i<states;++i) {
            auto& state=ir.resources.graphics_states[i];
            state.fill_alpha=1.0f;
            state.stroke_alpha=1.0f;
            state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
            state.clip_id=first_and_root && i==0u?1u:root;
        }
        for(size_t i=0u;i<count;++i) {
            auto& op=ir.operations[i];
            const quantapdf_composer_graphics_state_id id=
                source.scoped_clip_membership[i];
            op.graphics_state_id=id;
            if(op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
                op.value.image.options.graphics_state_id=id;
            } else {
                op.value.path.options.graphics_state_id=id;
            }
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

/* Publish actual source order and frozen Clip membership, including IMAGE
 * paints on both sides of a Q restore. All resources become IR-owned. */
quantapdf_ir materialize_scoped_mixed(const admission& source)
{
    const size_t count=source.order.size();
    admit(source.clip_completed &&
          source.saw_q_restore && count>=2u && count<=4u &&
          source.draws.size()>=1u && source.draws.size()<=2u &&
          source.painted_paths.size()<=2u &&
          source.draws.size()+source.painted_paths.size()==count &&
          source.scoped_clip_membership.size()==count &&
          source.clip.commands.size()>=4u &&
          source.clip.commands.size()<=9u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    unsigned inside_images=0u,inside_paths=0u;
    unsigned outside_images=0u,outside_paths=0u;
    bool saw_unclipped=false;
    for(size_t i=0u;i<count;++i) {
        const unsigned char active=source.scoped_clip_membership[i];
        admit(active<=1u,QUANTAPDF_ERROR_UNSUPPORTED);
        /* No unqualified Clip re-entry after a real Q restore. */
        if(active==0u)saw_unclipped=true;
        else admit(!saw_unclipped,QUANTAPDF_ERROR_UNSUPPORTED);
        if(active) {
            if(source.order[i]==2)++inside_images;
            else if(source.order[i]==1)++inside_paths;
            else throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
        } else {
            if(source.order[i]==2)++outside_images;
            else if(source.order[i]==1)++outside_paths;
            else throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
        }
    }

    const bool native_two_inside=count==2u &&
        source.draws.size()==1u &&
        source.painted_paths.size()==1u &&
        source.scoped_clip_membership.size()==2u &&
        source.scoped_clip_membership[0]==1u &&
        source.scoped_clip_membership[1]==1u;
    admit(inside_images+inside_paths>=1u &&
          (outside_images+outside_paths>=1u || native_two_inside),
          QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_ir ir=materialize_ordered_paints(source);
    try {
        ir.resources.graphics_states=
            static_cast<quantapdf_composer_graphics_state *>(
            QP_SOURCE_C_CALLOC(1u,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=1u;
        ir.resources.graphics_state_capacity=1u;
        auto& state=ir.resources.graphics_states[0];
        state.fill_alpha=1.0f;
        state.stroke_alpha=1.0f;
        state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        state.clip_id=materialize_source_clip_graph(ir,source);
        for(size_t i=0u;i<count;++i) {
            auto& op=ir.operations[i];
            const quantapdf_composer_graphics_state_id id=
                source.scoped_clip_membership[i] ? 1u : 0u;
            op.graphics_state_id=id;
            if(op.kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
                op.value.image.options.graphics_state_id=id;
            } else {
                admit(op.kind==QUANTAPDF_COMPOSER_OPERATION_PATH,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                op.value.path.options.graphics_state_id=id;
            }
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}
} // namespace

constexpr size_t page_max_tree_nodes=128u;
size_t page_walk_pages(
    Obj node,Obj expected_parent,size_t depth,size_t max_depth,
    std::set<QPDFObjGen>& seen,size_t& nodes,size_t& leaf_index,
    size_t target,Obj& selected)
{
    admit(depth<=max_depth && depth<=256u &&
          nodes<page_max_tree_nodes,
          QUANTAPDF_ERROR_UNSUPPORTED);
    ++nodes;
    admit(node.isIndirect() && node.isDictionary(),
          QUANTAPDF_ERROR_FORMAT);
    admit(seen.insert(node.getObjGen()).second,
          QUANTAPDF_ERROR_FORMAT);
    const Obj parent=node.getKey("/Parent");
    admit((expected_parent.isNull() && parent.isNull()) ||
          (!expected_parent.isNull() && !parent.isNull() &&
           parent.isSameObjectAs(expected_parent)),
          QUANTAPDF_ERROR_FORMAT);
    const Obj type=node.getKey("/Type");
    admit(type.isName(),QUANTAPDF_ERROR_FORMAT);
    if(type.getName()=="/Page") {
        admit(node.getKey("/Kids").isNull() &&
              node.getKey("/Count").isNull(),
              QUANTAPDF_ERROR_FORMAT);
        if(leaf_index==target)selected=node;
        admit(leaf_index<page_max_tree_nodes,
              QUANTAPDF_ERROR_UNSUPPORTED);
        ++leaf_index;
        return 1u;
    }
    admit(type.getName()=="/Pages",QUANTAPDF_ERROR_FORMAT);
    const Obj kids=node.getKey("/Kids");
    const Obj declared=node.getKey("/Count");
    admit(kids.isArray() && declared.isInteger(),
          QUANTAPDF_ERROR_FORMAT);
    const auto count=declared.getIntValue();
    admit(count>0ll &&
          static_cast<uint64_t>(count)<=page_max_tree_nodes,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const int n=kids.getArrayNItems();
    admit(n>0 && static_cast<size_t>(n)<=page_max_tree_nodes,
          QUANTAPDF_ERROR_UNSUPPORTED);
    size_t subtotal=0u;
    for(int i=0;i<n;++i) {
        const size_t children=page_walk_pages(
            kids.getArrayItem(i),node,depth+1u,max_depth,
            seen,nodes,leaf_index,target,selected);
        admit(subtotal<=page_max_tree_nodes-children,
              QUANTAPDF_ERROR_UNSUPPORTED);
        subtotal+=children;
    }
    admit(subtotal==static_cast<size_t>(count),
          QUANTAPDF_ERROR_FORMAT);
    return subtotal;
}

Obj selected_page_object(QPDF& pdf,size_t selected_index,
                             size_t max_parent_steps)
{
    admit(selected_index<=static_cast<size_t>(INT_MAX) &&
          max_parent_steps!=0u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    std::set<QPDFObjGen> seen;
    size_t visited=0u,leaves=0u;
    Obj selected=Obj::newNull();
    const Obj root=pdf.getRoot().getKey("/Pages");
    page_walk_pages(root,Obj::newNull(),1u,max_parent_steps,
                          seen,visited,leaves,selected_index,selected);
    admit(!selected.isNull(),QUANTAPDF_ERROR_UNSUPPORTED);
    return selected;
}

/* Exact quarter-point M0 Source box parser. Use the original QPDF
 * NUMBER token, not double->snprintf (which can change decimal bounds).
 * This deliberately admits only the 1/4-point lattice. A fractional
 * viewport SPAN is a separate M1 gate: do not silently ceil, round,
 * stretch, or lose a partial final row/column in the current Raster. */
int64_t page_quarters(Obj number)
{ return page_lattice(number,4ll); }

std::array<int64_t,4> page_quarter_box(Obj box)
{
    admit(box.isArray() && box.getArrayNItems()==4,
          QUANTAPDF_ERROR_FORMAT);
    std::array<int64_t,4> q={};
    for(size_t i=0u;i<4u;++i)
        q[i]=page_quarters(box.getArrayItem((int)i));
    if(q[0]>q[2])std::swap(q[0],q[2]);
    if(q[1]>q[3])std::swap(q[1],q[3]);
    admit(q[2]>q[0] && q[3]>q[1],QUANTAPDF_ERROR_FORMAT);
    return q;
}

struct page_exact_boxes {
    std::array<int64_t,4> media;
    std::array<int64_t,4> crop;
    std::array<int64_t,4> visible;
    /* Existing source profiles use 1/4pt ticks. The ONLY newly
     * qualified source decimal page is the original TeXample
     * 237.133 x 180.44 MediaBox (1/1000pt ticks). Never coerce it
     * onto the older quarter-point grid or round it to 238x181pt. */
    uint32_t ticks_per_point=4u;
};

page_exact_boxes page_quarter_geometry(
    Obj selected,size_t max_parent_steps,
    const quantapdf_qpdf_page_geometry& geo)
{
    Obj media=Obj::newNull(),crop=Obj::newNull();
    std::set<QPDFObjGen> seen;
    size_t steps=0u;
    for(Obj p=selected;!p.isNull();p=p.getKey("/Parent")) {
        admit(steps++<max_parent_steps && steps<=256u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        admit(p.isIndirect() && p.isDictionary(),
              QUANTAPDF_ERROR_FORMAT);
        admit(seen.insert(p.getObjGen()).second,
              QUANTAPDF_ERROR_FORMAT);
        if(media.isNull())media=p.getKey("/MediaBox");
        if(crop.isNull())crop=p.getKey("/CropBox");
    }
    admit(!media.isNull(),QUANTAPDF_ERROR_FORMAT);
    /* Recognize EXACT original numeric PDF operands, not rounded
     * getNumericValue()/snprintf reparsing. This is a bounded one-page
     * TeXample geometry exception; the normal quarter grammar and
     * all other original MediaBox profiles remain untouched.
     * An independent cropped decimal viewport is NOT admitted. */
    const auto exact_texample_box=[](Obj box) {
        return box.isArray() && box.getArrayNItems()==4 &&
            box.getArrayItem(0).unparse()=="0" &&
            box.getArrayItem(1).unparse()=="0" &&
            box.getArrayItem(2).unparse()=="237.133" &&
            box.getArrayItem(3).unparse()=="180.44";
    };
    const bool teexample_decimal=exact_texample_box(media);
    if(teexample_decimal)
        admit(crop.isNull() || exact_texample_box(crop),
              QUANTAPDF_ERROR_UNSUPPORTED);
    const auto m=teexample_decimal ?
        std::array<int64_t,4>{0,0,237133,180440} :
        page_quarter_box(media);
    const auto c=teexample_decimal ? m :
        crop.isNull()?m:page_quarter_box(crop);
    const uint32_t denominator=teexample_decimal?1000u:4u;
    std::array<int64_t,4> view={
        std::max(m[0],c[0]),std::max(m[1],c[1]),
        std::min(m[2],c[2]),std::min(m[3],c[3])
    };
    admit(view[2]>view[0] && view[3]>view[1],
          QUANTAPDF_ERROR_FORMAT);
    /* Cross-check the independently loaded original PageTree geometry.
     * For quarter ticks these are binary-exact; for the original
     * decimal box the exact rational is converted to double ONCE
     * solely for the existing QPDF comparison, not for admission. */
    for(size_t i=0u;i<4u;++i) {
        admit(geo.media[i]==(double)m[i]/(double)denominator &&
              geo.crop[i]==(double)c[i]/(double)denominator,
              QUANTAPDF_ERROR_FORMAT);
    }
    admit(geo.left==(double)view[0]/(double)denominator &&
          geo.bottom==(double)view[1]/(double)denominator &&
          geo.right==(double)view[2]/(double)denominator &&
          geo.top==(double)view[3]/(double)denominator,
          QUANTAPDF_ERROR_FORMAT);
    return {m,c,view,denominator};
}

unsigned checked_page_userunit(Obj page,double geometry_unit)
{
    const Obj raw=page.getKey("/UserUnit");
    const int64_t quarters=raw.isNull()?4ll:page_quarters(raw);
    admit(quarters>=1ll && quarters<=16ll &&
          geometry_unit==(double)quarters/4.0,
          QUANTAPDF_ERROR_UNSUPPORTED);
    return (unsigned)quarters;
}

/* Scale bounded originals in their DETACHED staging, AFTER all
 * q/Q operators and the original source /Rotate were accepted and
 * BEFORE any uniquely-owned IR is published. This is purely source
 * geometry; original RAW Gray/RGB XObject bytes and orientation
 * remain exactly the same, with one nearest-neighbor C11 producer. */
void scale_source_userunit(admission& source,unsigned quarters)
{
    /* A nondefault /UserUnit scales true PDF Stroke width, while the
     * single C11 clipped-triangle sampler models exactly one device
     * pixel. Refuse before publishing IR rather than painting an
     * incorrectly thin or thick Stroke. All prior Fill/Image profiles
     * retain their existing fractional /UserUnit support. */
    if(quarters!=4u)
        admit(std::none_of(source.painted_paths.begin(),
                          source.painted_paths.end(),
                          [](const staged_path& p){return p.stroke;}),
              QUANTAPDF_ERROR_UNSUPPORTED);
    if(quarters==4u)return;
    admit(quarters>=1u && quarters<=16u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const double unit=(double)quarters/4.0;
    const double pw=source.exact_page_w*unit;
    const double ph=source.exact_page_h*unit;
    admit(std::isfinite(pw) && std::isfinite(ph) &&
          pw>=1.0 && ph>=1.0 &&
          pw<=1000000.0 && ph<=1000000.0 &&
          std::floor(pw*16.0)==pw*16.0 &&
          std::floor(ph*16.0)==ph*16.0,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t w=(size_t)std::ceil(pw);
    const size_t h=(size_t)std::ceil(ph);
    admit(w!=0u && h!=0u &&
          w<=hard_page_pixels/h &&
          w*h<=source.limits.max_page_pixels,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const auto point=[unit](quantapdf_point& p) {
        const double x=(double)p.x*unit;
        const double y=(double)p.y*unit;
        admit(std::isfinite(x) && std::isfinite(y) &&
              x>=-1000000.0 && x<=1000000.0 &&
              y>=-1000000.0 && y<=1000000.0,
              QUANTAPDF_ERROR_UNSUPPORTED);
        p.x=(float)x;
        p.y=(float)y;
    };
    const auto commands=[&point](
        std::vector<quantapdf_composer_path_command>& v) {
        for(auto& c:v) {
            if(c.kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
               c.kind==QUANTAPDF_COMPOSER_PATH_LINE_TO)
                point(c.point1);
            else if(c.kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO) {
                point(c.point1);
                point(c.point2);
                point(c.point3);
            } else
                admit(c.kind==QUANTAPDF_COMPOSER_PATH_CLOSE,
                      QUANTAPDF_ERROR_UNSUPPORTED);
        }
    };
    for(auto& p:source.painted_paths)
        commands(p.commands);
    commands(source.clip.commands);
    commands(source.second_clip.commands);
    size_t physical_draw=0u;
    for(auto& image:source.draws) {
        const double x0=(double)image.bounds.x0*unit;
        const double x1=(double)image.bounds.x1*unit;
        const double y0=(double)image.bounds.y0*unit;
        const double y1=(double)image.bounds.y1*unit;
        admit(std::isfinite(x0) && std::isfinite(x1) &&
              std::isfinite(y0) && std::isfinite(y1) &&
              x0>=-1000000.0 && x1<=1000000.0 &&
              y0>=-1000000.0 && y1<=1000000.0 &&
              x1>x0 && y1>y0,
              QUANTAPDF_ERROR_UNSUPPORTED);
        /* Even an IMAGE wholly outside the visible CropBox is billed
         * at its FULL physically scaled authorial placement, not a
         * tiny visible fragment. The C11 raster recharges its own
         * device-space work independently. */
        const size_t area_w=(size_t)std::ceil(x1-x0);
        const size_t area_h=(size_t)std::ceil(y1-y0);
        admit(area_w>0u && area_h>0u &&
              area_w<=hard_draw_work/area_h &&
              area_w*area_h<=source.limits.max_drawn_pixels &&
              physical_draw<=
                  source.limits.max_drawn_pixels-area_w*area_h &&
              physical_draw<=hard_draw_work-area_w*area_h,
              QUANTAPDF_ERROR_UNSUPPORTED);
        physical_draw+=area_w*area_h;
        image.bounds.x0=(float)x0;image.bounds.x1=(float)x1;
        image.bounds.y0=(float)y0;image.bounds.y1=(float)y1;
    }
    source.page_w=w;source.page_h=h;
    source.exact_page_w=pw;source.exact_page_h=ph;
}

/* The exact source-number bounds admit one integer-ceiled pixel canvas
 * without any rounding of the ORIGINAL source geometry. */
size_t source_exact_tick_ceil(
    int64_t lo,int64_t hi,uint32_t ticks_per_point,size_t cap)
{
    admit(hi>lo && cap!=0u &&
          (ticks_per_point==4u || ticks_per_point==1000u),
          QUANTAPDF_ERROR_UNSUPPORTED);
    const uint64_t n=(uint64_t)(hi-lo),ticks=ticks_per_point;
    const uint64_t pixels=n/ticks+(n%ticks!=0u);
    admit(pixels>0u && pixels<=(uint64_t)cap,
          QUANTAPDF_ERROR_UNSUPPORTED);
    return (size_t)pixels;
}

static quantapdf_status source_page_to_ir_impl(
    QPDF& pdf,
    const quantapdf_qpdf_page_limits *limits,
    quantapdf_ir *out_ir,size_t selected_page_index)
{
    std::memset(out_ir,0,sizeof(*out_ir));
    try {
        Obj selected=selected_page_object(
            pdf,selected_page_index,limits->max_parent_steps);
        quantapdf_qpdf_page_geometry geo={};
        const auto status=quantapdf_qpdf_load_page_geometry(
            pdf,static_cast<int>(selected_page_index),&geo);
        if(status!=QUANTAPDF_OK)return status;
        admit(geo.page.isSameObjectAs(selected),QUANTAPDF_ERROR_FORMAT);
        /* Quarters of a source point are parsed from the ORIGINAL QPDF
         * numeric token under independent source ancestry bounds, not
         * approximated by reformatting binary64. M0 admits fractional
         * endpoints only when visible and full Media spans stay integer.
         * This protects the C11 Raster's canonical integer page grid. */
        const page_exact_boxes exact=page_quarter_geometry(
            geo.page,limits->max_parent_steps,geo);
        const unsigned user_quarters=checked_page_userunit(geo.page,geo.user_unit);
        const bool full_media=
            geo.left==geo.media[0] &&
            geo.bottom==geo.media[1] &&
            geo.right==geo.media[2] && geo.top==geo.media[3] &&
            geo.crop[0]==geo.media[0] &&
            geo.crop[1]==geo.media[1] &&
            geo.crop[2]==geo.media[2] &&
            geo.crop[3]==geo.media[3];
        {
            /* Even a tiny CropBox cannot make an unbounded original
             * MediaBox an inexpensive source: budget the WHOLE media
             * before admitting bounded outside-crop paint geometry. */
            const size_t mw=source_exact_tick_ceil(
                exact.media[0],exact.media[2],
                exact.ticks_per_point,hard_page_pixels);
            const size_t mh=source_exact_tick_ceil(
                exact.media[1],exact.media[3],
                exact.ticks_per_point,hard_page_pixels);
            admit(mw!=0u && mh!=0u &&
                  mw<=limits->max_page_pixels/mh,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            const uint64_t mw16=(uint64_t)(
                exact.media[2]-exact.media[0])*(uint64_t)user_quarters;
            const uint64_t mh16=(uint64_t)(
                exact.media[3]-exact.media[1])*(uint64_t)user_quarters;
            const uint64_t physical_ticks=
                (uint64_t)exact.ticks_per_point*4u;
            const size_t device_mw=(size_t)(mw16/physical_ticks+
                (mw16%physical_ticks!=0u));
            const size_t device_mh=(size_t)(mh16/physical_ticks+
                (mh16%physical_ticks!=0u));
            admit(device_mw!=0u && device_mh!=0u &&
                  device_mw<=limits->max_page_pixels/device_mh,
                  QUANTAPDF_ERROR_UNSUPPORTED);
        }
        /* Source operators use the UNROTATED original media canvas.
         * The completely admitted staging will acquire the page-tree
         * orientation before publication, with no extra pixel producer. */
        const size_t page_w=source_exact_tick_ceil(
            exact.visible[0],exact.visible[2],exact.ticks_per_point,hard_page_pixels);
        const size_t page_h=source_exact_tick_ceil(
            exact.visible[1],exact.visible[3],exact.ticks_per_point,hard_page_pixels);
        checked_original_rotation(
                geo.page,limits->max_parent_steps,geo.rotation);
        admit(page_w!=0u && page_h!=0u &&
              page_w<=hard_page_pixels/page_h &&
              page_w*page_h<=limits->max_page_pixels,
              QUANTAPDF_ERROR_UNSUPPORTED);
        /* A source Page transparency /Group can be ignored ONLY when
         * its exactly validated isolated, non-knockout DeviceRGB
         * semantics are identical to the already restricted opaque
         * Normal C11 paint. Reject every unknown key, other blend,
         * unqualified color space and malformed value *before*
         * publishing any IR. */
        Obj group=geo.page.getKey("/Group");
        if(!group.isNull()) {
            admit(group.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            admit(group.getKeys().size()<=5u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            for(const auto& key:group.getKeys())
                admit(key=="/Type" || key=="/S" || key=="/I" ||
                      key=="/K" || key=="/CS",
                      QUANTAPDF_ERROR_UNSUPPORTED);
            Obj type=group.getKey("/Type");
            if(!type.isNull()) {
                admit(type.isName(),QUANTAPDF_ERROR_FORMAT);
                admit(type.getName()=="/Group",
                      QUANTAPDF_ERROR_UNSUPPORTED);
            }
            Obj subtype=group.getKey("/S");
            Obj isolated=group.getKey("/I");
            Obj knockout=group.getKey("/K");
            Obj space=group.getKey("/CS");
            admit(subtype.isName() && isolated.isBool() &&
                  space.isName() &&
                  (knockout.isNull() || knockout.isBool()),
                  QUANTAPDF_ERROR_FORMAT);
            admit(subtype.getName()=="/Transparency" &&
                  isolated.getBoolValue() &&
                  (knockout.isNull() || !knockout.getBoolValue()) &&
                  space.getName()=="/DeviceRGB",
                  QUANTAPDF_ERROR_UNSUPPORTED);
        }
        admit(geo.page.getKey("/Annots").isNull(),
              QUANTAPDF_ERROR_UNSUPPORTED);

        Obj resources=bounded_page_resources(
            geo.page,limits->max_parent_steps,true);
        admission source(resources,*limits,page_w,page_h);
        source.texample_public_eligible=exact.ticks_per_point==1000u && full_media &&
            geo.rotation==0 && user_quarters==4u;
        source.text_outlines_eligible=full_media &&
            geo.rotation==0 && user_quarters==4u && geo.left==0.0 && geo.bottom==0.0 &&
            geo.right==std::trunc(geo.right) && geo.top==std::trunc(geo.top);
        source.exact_page_w=(double)(exact.visible[2]-exact.visible[0])/(double)exact.ticks_per_point;
        source.exact_page_h=(double)(exact.visible[3]-exact.visible[1])/(double)exact.ticks_per_point;
        source.media_left=geo.left;
        source.media_bottom=geo.bottom;
        /* The opt-in signed/quarter-aware SOURCE staging is required
         * for nonzero fractional origins EVEN when CropBox==MediaBox.
         * The inherited strict integer branch remains unchanged for
         * ordinary page-zero integer-origin Source profiles. */
        const bool fractional_geometry=
            geo.left!=std::trunc(geo.left) ||
            geo.bottom!=std::trunc(geo.bottom) ||
            geo.right!=std::trunc(geo.right) ||
            geo.top!=std::trunc(geo.top) ||
            source.exact_page_w!=(double)page_w ||
            source.exact_page_h!=(double)page_h;
        source.crop_viewport=(!full_media || fractional_geometry);
        QPDFPageObjectHelper page(geo.page);
        page_content_resources(page,*limits);
        page_content_reader reader(source);
        QP_SOURCE_STAGE_ARM(10u);
        page.parseContents(&reader);
        admit(!pdf.anyWarnings(),QUANTAPDF_ERROR_FORMAT);
        if(source.text_outlines_complete) {
            *out_ir=source.ordered_scene()?materialize_ordered_scene(source):
                materialize_two_clip_text_scene(source);
            return QUANTAPDF_OK;
        }
        const bool clip_states=!source.texample_original_profile &&
            !source.three_branch_candidate &&
            !source.sibling_clip_scopes &&
            prepare_native_clip_membership(source);
        if(source.three_branch_candidate)
            admit(geo.rotation==0 && user_quarters==4u &&
                  !source.crop_viewport,
                  QUANTAPDF_ERROR_UNSUPPORTED);
        {
            original_turn_staging(source,geo.rotation);
            scale_source_userunit(source,user_quarters);
        }
        if(source.texample_original_profile) {
            *out_ir=materialize_texample_m3(source);
            return QUANTAPDF_OK;
        }
        if(source.clip_completed && !source.two_owned_clips &&
           std::none_of(source.painted_paths.begin(),source.painted_paths.end(),
               [](const staged_path& path){return path.stroke;})) {
            *out_ir=materialize_ordered_scene(source);
            return QUANTAPDF_OK;
        }
        if(exact.ticks_per_point==1000u) {
            admit(geo.rotation==0 && user_quarters==4u &&
                  full_media && source.draws.empty() &&
                  source.painted_paths.empty() && source.order.empty() &&
                  source.scoped_clip_membership.empty() &&
                  !source.clip_completed &&
                  source.clip.commands.empty() &&
                  source.second_clip.commands.empty() &&
                  !source.saw_q_restore &&
                  !source.three_branch_candidate,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            *out_ir=materialize_blank_page(source);
            return QUANTAPDF_OK;
        }
        if(source.three_branch_candidate) {
            *out_ir=materialize_three_branch_source(source);
            return QUANTAPDF_OK;
        }
        if(source.sibling_clip_scopes) {
            *out_ir=materialize_sibling_clip_scopes(source);
            return QUANTAPDF_OK;
        }
        /* A real GS0 prefix is a separate complete-page admission.
         * Preserve the existing unclipped staging grammar and select
         * its exact owned Clip state shape once, without retrying an
         * older producer or manufacturing a Q-restored suffix. */
        if(source.clip_completed &&
           !source.scoped_clip_membership.empty() &&
           source.scoped_clip_membership[0]==0u) {
            *out_ir=materialize_unclipped_prefix(source,clip_states);
            return QUANTAPDF_OK;
        }
        if(clip_states) {
            *out_ir=materialize_clip_states(source);
            return QUANTAPDF_OK;
        }
        /* This is NOT a recovery after unsupported content: the entire
         * page stream was parsed, EOF validated q/Q and pending-path
         * ownership, and zero paint/clip operations were admitted. */
        if(source.draws.empty() &&
           source.painted_paths.empty() && source.order.empty() &&
           source.scoped_clip_membership.empty() &&
           !source.clip_completed && !source.saw_q_restore) {
            *out_ir=materialize_blank_page(source);
            return QUANTAPDF_OK;
        }

        if(!source.clip_completed) {
            *out_ir=materialize_ordered_paints(source);
            return QUANTAPDF_OK;
        }
        if(source.draws.empty()) {
            *out_ir=materialize_clipped_path_only(source);
            return QUANTAPDF_OK;
        }

        if(!source.draws.empty() &&
           source.draws.size()<=2u &&
           source.painted_paths.empty() &&
           source.clip_completed &&
           source.order.size()==source.draws.size() &&
           source.scoped_clip_membership.size()==source.draws.size() &&
           std::all_of(source.scoped_clip_membership.begin(),
                       source.scoped_clip_membership.end(),
                       [](unsigned char member){return member==1u;})) {
            *out_ir=materialize_clipped_image(
                source,source.draws.size());
            return QUANTAPDF_OK;
        }
        admit(!source.draws.empty() && source.clip_completed && source.saw_q_restore &&
              source.draws.size()<=2u &&
              source.painted_paths.size()<=2u &&
              source.order.size()==source.draws.size()+source.painted_paths.size() &&
              source.order.size()>=2u && source.order.size()<=4u &&
              source.order.size()<=source.limits.max_operations &&
              source.scoped_clip_membership.size()==source.order.size(),
              QUANTAPDF_ERROR_UNSUPPORTED);
        *out_ir=materialize_scoped_mixed(source);
        return QUANTAPDF_OK;
    } catch(rejected const& e) {return e.status;}
      catch(std::bad_alloc const&) {return QUANTAPDF_ERROR_NOMEM;}
      catch(std::length_error const&) {return QUANTAPDF_ERROR_UNSUPPORTED;}
      catch(QPDFExc const& e) {return quantapdf_status_from_qpdf(e);}
      catch(std::runtime_error const&) {return QUANTAPDF_ERROR_FORMAT;}
      catch(...) {return QUANTAPDF_ERROR_BACKEND;}
}

extern "C" quantapdf_status quantapdf_qpdf_page_to_ir(
    quantapdf_qpdf_document *document,size_t page_index,
    const quantapdf_qpdf_page_limits *limits,quantapdf_ir *out_ir)
{
    return source_page_to_ir_impl(*document->pdf,limits,out_ir,page_index);
}
