#include "backend/qpdf_native_image_ir.h"
#include "backend/qpdf_document_internal.h"

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>

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
#define QP_SOURCE_C_CALLOC(n,s) \
    (quantapdf_source_ir_oom_fail_c_allocation()?nullptr:std::calloc((n),(s)))
#define QP_SOURCE_C_MALLOC(n) \
    (quantapdf_source_ir_oom_fail_c_allocation()?nullptr:std::malloc(n))
#define QP_SOURCE_C_REALLOC(p,n) \
    (quantapdf_source_ir_oom_fail_c_allocation()?nullptr:std::realloc((p),(n)))
#else
#define QP_SOURCE_STAGE_ARM(n) ((void)0)
#define QP_SOURCE_C_CALLOC(n,s) std::calloc((n),(s))
#define QP_SOURCE_C_MALLOC(n) std::malloc(n)
#define QP_SOURCE_C_REALLOC(p,n) std::realloc((p),(n))
#endif

namespace {
using Obj = QPDFObjectHandle;
constexpr size_t hard_pdf_bytes = 2u * 1024u * 1024u;
constexpr size_t hard_content_bytes = 65536u;
constexpr size_t hard_image_pixels = 4194304u;
constexpr size_t hard_page_pixels = 4194304u;
constexpr size_t hard_draw_work = 16777216u;
constexpr size_t hard_streams = 4u;
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

double whole_signed_source(double value)
{
    /* Source V10 accepts quarter-point coordinates from a proven
     * exact-lattice page viewport. For the legacy integer boxes this
     * also admits a *fractional source CTM* only when its transformed
     * geometry is representable on the same 4x4 sample grid.
     * Work and absolute source range remain independently bounded. */
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

std::vector<unsigned char> stream_data(Obj stream, size_t limit,
                                      bool image,
                                      bool allow_predictor=false,
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
    /* V1–V9 must still refuse *all* DecodeParms before decompression.
     * The separately opted-in Source V10 performs strict preflight of
     * every TIFF/PNG predictor parameter before invoking QPDF's existing
     * Flate+predictor pipeline; do not duplicate those codecs here.
     *
     * Require explicit matching rows/channels/8-bit samples. This bounds
     * QPDF's predictor row allocation and forbids arrays, extra keys,
     * filter chains, guessed defaults and arbitrary predictor ranges.
     * The downstream sink caps the fully decoded byte count. */
    Obj parms=dict.getKey("/DecodeParms");
    admit(parms.isNull() || (image && allow_predictor && flate),
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
    admit(stream.pipeStreamData(&sink,&filtered,0,level,true,true) &&
          sink.complete && (filtered || !flate),
          QUANTAPDF_ERROR_UNSUPPORTED);
    if (image) admit(!sink.bytes.empty());
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

bool qualified_owned_clip_pair(
    const std::vector<quantapdf_composer_path_command>& first,
    const std::vector<quantapdf_composer_path_command>& second)
{
    /* Both bounded leaves retain their exact geometry and winding
     * rule, including compound contours and canonical zero-edge leaves.
     * Only two proven simple rectangles use the separate reduction. */
    return bounded_clip_path(first) && bounded_clip_path(second);
}

/* Canonical PRIVATE source-admission signatures. The enumerated IDs are
 * stable only within this translation unit; they are not a public ABI.
 * Each existing original-source entry point names exactly one contract.
 * No parser branch or materializer should infer capabilities from the
 * relative order of the legacy V1..V8 entry points. */
enum class source_profile_id : unsigned char {
    raw_images,
    mixed_path_image_v1,
    mixed_path_two_images_v2,
    mixed_two_paths_two_images_v3,
    clipped_image_v1,
    two_clipped_images_v2,
    restored_clipped_images_v3,
    clipped_image_then_path_v4,
    scoped_mixed_v5,
    scoped_mixed_v6,
    scoped_mixed_v7_rgb,
    scoped_mixed_v8_gray,
    scoped_mixed_v9_image_decode,
    scoped_mixed_v10_flate_predictor,
    count
};

enum class source_materialization : unsigned char {
    raw_images,
    mixed,
    clipped_images,
    clipped_image_then_path,
    scoped_exact,
    scoped_flexible
};

struct source_profile_contract {
    bool mixed;
    bool clipped;
    size_t clip_image_limit;
    bool restore_second_image;
    size_t mixed_operation_limit;
    bool frozen_scoped_membership;
    bool variable_operations;
    bool normalized_rgb;
    bool normalized_gray;
    /* Image /Decode endpoint admission is independently opt-in. */
    bool explicit_image_decode;
    /* Source V10 only: strict 8-bit TIFF/PNG Flate DecodeParms. */
    bool explicit_image_predictor;
    source_materialization output;
};

/* One immutable row for each historical source entry, in enum order.
 * The limits below are EXACT admission conditions, not advisory maximums;
 * source count and q/Q checks additionally validate every parsed operator. */
constexpr std::array<source_profile_contract,
                     static_cast<size_t>(source_profile_id::count)>
    source_profiles = {{
        /*  0: Image only */
        {false,false,1u,false,2u,false,false,false,false,false,false,
         source_materialization::raw_images},
        /*  1: mixed V1 */
        {true,false,1u,false,2u,false,false,false,false,false,false,
         source_materialization::mixed},
        /*  2: mixed V2 */
        {true,false,1u,false,3u,false,false,false,false,false,false,
         source_materialization::mixed},
        /*  3: mixed V3 */
        {true,false,1u,false,4u,false,false,false,false,false,false,
         source_materialization::mixed},
        /*  4: Image Clip V1 */
        {false,true,1u,false,2u,false,false,false,false,false,false,
         source_materialization::clipped_images},
        /*  5: Image Clip V2 */
        {false,true,2u,false,2u,false,false,false,false,false,false,
         source_materialization::clipped_images},
        /*  6: Image Clip V3 */
        {false,true,2u,true,2u,false,false,false,false,false,false,
         source_materialization::clipped_images},
        /*  7: Image/Path V4 */
        {true,true,1u,true,2u,false,false,false,false,false,false,
         source_materialization::clipped_image_then_path},
        /*  8: exact scoped V5 */
        {true,true,2u,false,4u,true,false,false,false,false,false,
         source_materialization::scoped_exact},
        /*  9: variable scoped V6 */
        {true,true,2u,false,4u,true,true,false,false,false,false,
         source_materialization::scoped_flexible},
        /* 10: fractional RGB V7 */
        {true,true,2u,false,4u,true,true,true,false,false,false,
         source_materialization::scoped_flexible},
        /* 11: DeviceGray V8 */
        {true,true,2u,false,4u,true,true,true,true,false,false,
         source_materialization::scoped_flexible},
        /* 12: explicit per-channel /Decode identity/inversion V9 */
        {true,true,2u,false,4u,true,true,true,true,true,false,
         source_materialization::scoped_flexible},
        /* 13: V9 plus strictly validated Flate PNG/TIFF DecodeParms */
        {true,true,2u,false,4u,true,true,true,true,true,true,
         source_materialization::scoped_flexible}
    }};

constexpr bool source_profile_table_valid()
{
    for(const auto& profile:source_profiles) {
        if(profile.mixed_operation_limit<2u ||
           profile.mixed_operation_limit>hard_operations ||
           profile.clip_image_limit<1u ||
           profile.clip_image_limit>2u ||
           (profile.normalized_gray && !profile.normalized_rgb) ||
           (profile.explicit_image_decode &&
            (!profile.normalized_gray ||
             !profile.variable_operations ||
             profile.output!=source_materialization::scoped_flexible)) ||
           (profile.explicit_image_predictor &&
            (!profile.explicit_image_decode ||
             !profile.normalized_gray ||
             !profile.variable_operations ||
             profile.output!=source_materialization::scoped_flexible)) ||
           (profile.variable_operations &&
            (!profile.frozen_scoped_membership ||
             profile.output!=source_materialization::scoped_flexible)) ||
           (profile.frozen_scoped_membership && !profile.clipped))
            return false;
    }
    return true;
}
static_assert(source_profile_table_valid(),
              "The private original-source profile matrix is inconsistent");

const source_profile_contract& source_profile_for(source_profile_id id)
{
    const size_t index=static_cast<size_t>(id);
    admit(index<source_profiles.size(),QUANTAPDF_ERROR_UNSUPPORTED);
    return source_profiles[index];
}

struct admission {
    Obj resources;
    const quantapdf_qpdf_source_raw_image_limits& limits;
    size_t page_w,page_h;
    size_t source_pixels=0,drawn_pixels=0,operators=0;
    std::vector<staged_image> draws;
    bool mixed_mode=false;
    std::vector<staged_path> painted_paths;
    std::vector<int> order; /* 1=PATH, 2=IMAGE, as originally painted */
    size_t mixed_operation_limit=2u; /* exact 1+1, 1+2 or V3 2+2 */
    bool clipped_image_mode=false;
    bool clip_completed=false;
    staged_image_clip clip;
    /* Native V10 only: two independently frozen leaves, kept until
     * owned IR publication as Clip IDs 1/2 plus Intersection root 3.
     * Native staging retains 1=first leaf and 2=intersection. Only a
     * complete page with no first-leaf paint may remap 2 to old GS1. */
    staged_image_clip second_clip;
    bool two_owned_clips=false;
    size_t clipped_image_limit=1u;
    bool restore_second_image=false;
    bool saw_q_restore=false;
    /* Source V5 freezes Clip/GS membership per paint: 1=inside, 0=Q.
     * Native V10 additionally preserves the second intersection as 2. */
    bool mixed_scoped_mode=false;
    std::vector<unsigned char> scoped_clip_membership;
    /* Source V6 charges the actual 2–4 layers against max_operations,
     * rather than the full hard capacity of four operations. */
    bool variable_scoped_mode=false;
    /* Source V7 independently admits finite normalized DeviceRGB rg
     * channels. Earlier source signatures remain exact-binary only. */
    bool normalized_rgb_mode=false;
    /* V8 adds a private nonstroking DeviceGray g admission, without
     * changing earlier source color profiles or the C11 renderer. */
    bool normalized_gray_mode=false;
    /* V9 onward: explicit Image /Decode [0 1]/[1 0] per channel. */
    bool explicit_image_decode_mode=false;
    /* V10 only: strict Flate TIFF/PNG predictor DecodeParms. */
    bool explicit_image_predictor_mode=false;
    /* V10 public-native only: fully parsed homogeneous opaque PATH
     * or RAW/Flate IMAGE paints and independently qualified Clip
     * profiles. Older mixed W/Q Path+Image modes stay narrower. */
    bool native_unclipped_mode=false;
    /* Public Source V10 alone normalizes the original PDF MediaBox
     * origin before deterministic Y-down Stage/IR projection. Older
     * private source signatures always use zero offsets. */
    double media_left=0.0;
    double media_bottom=0.0;
    /* Source V10 only: true effective Media∩Crop viewport. Stage all
     * original operations including ones extending beyond the viewport,
     * but bill full authored IMAGE placement and clamp only in the
     * ONE C11 viewport sample consumer, never by clipping source bytes. */
    bool crop_viewport=false;
    double exact_page_w=0.0,exact_page_h=0.0; /* V10 1/4-point spans */

    void paint_path(std::vector<quantapdf_composer_path_command> commands,
                    uint32_t color,quantapdf_composer_fill_rule rule)
    {
        /* Public V10 PATH-only pages, with or without a frozen Clip,
         * may consume the EXISTING four canonical C11 raster layers.
         * A later Image still faces the complete mixed preflight's
         * independent one/two PATH limit before any IR is published. */
        const size_t allowed_paths=
            native_unclipped_mode && draws.empty() ?
                hard_operations :
                (mixed_operation_limit==4u ? 2u : 1u);
        admit(mixed_mode && painted_paths.size()<allowed_paths &&
              commands.size()>=4u && commands.size()<=9u &&
              order.size()<mixed_operation_limit &&
              limits.max_operations>=
                   (variable_scoped_mode ? order.size()+1u :
                    mixed_operation_limit),
              QUANTAPDF_ERROR_UNSUPPORTED);
        staged_path item;
        item.commands=std::move(commands);
        item.fill_argb=color;
        item.fill_rule=rule;
        QP_SOURCE_STAGE_ARM(3u);
        painted_paths.push_back(std::move(item));
        QP_SOURCE_STAGE_ARM(4u);
        order.push_back(1);
    }

    void execute(Obj image,const matrix& ctm)
    {
        /* Only the public Source V10 IMAGE-only, zero-Clip/zero-PATH
         * profile may consume all four EXISTING C11 Raster layers.
         * Mixed PATH/IMAGE and all W/W* Clip profiles keep their
         * independently qualified lower IMAGE cardinalities. */
        const bool public_image_only =
            native_unclipped_mode && !clip_completed &&
            clip.commands.empty() && painted_paths.empty() &&
            !saw_q_restore;
        const size_t admitted_image_layers=public_image_only ?
            hard_operations :
            (mixed_operation_limit==4u ? 2u : mixed_operation_limit-1u);
        admit(draws.size() < hard_operations &&
              draws.size() < limits.max_operations &&
              (!mixed_mode ||
               (draws.size()<admitted_image_layers &&
                order.size()<mixed_operation_limit &&
                limits.max_operations>=
                   (variable_scoped_mode ? order.size()+1u :
                    mixed_operation_limit))),
              QUANTAPDF_ERROR_UNSUPPORTED);
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

        /* V9 image Decode is a SOURCE admission and must be checked
         * BEFORE potentially expensive decompression. In V1–V8 this
         * preserves the old immediate fail-closed /Decode refusal. */
        Obj decode=dict.getKey("/Decode");
        admit(decode.isNull() || explicit_image_decode_mode,
              QUANTAPDF_ERROR_UNSUPPORTED);
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
        auto decoded=stream_data(
            image,size,true,explicit_image_predictor_mode,w,channels);
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
        const double x0=crop_viewport ?
            whole_signed_source(ax0) :
            (double)whole(ax0,page_w);
        const double x1=crop_viewport ?
            whole_signed_source(ax1) :
            (double)whole(ax1,page_w);
        const double bottom=crop_viewport ?
            whole_signed_source(ay0) :
            (double)whole(ay0,page_h);
        const double top=crop_viewport ?
            whole_signed_source(ay1) :
            (double)whole(ay1,page_h);
        admit(x1>x0 && top>bottom,QUANTAPDF_ERROR_UNSUPPORTED);
        const size_t draw_width=whole(x1-x0,hard_draw_work);
        const size_t draw_height=whole(top-bottom,hard_draw_work);
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
        if (mixed_mode) {
            QP_SOURCE_STAGE_ARM(2u);
            order.push_back(2);
        }
    }
};

class image_content_reader final : public Obj::ParserCallbacks {
public:
    explicit image_content_reader(admission& a,bool allow_path=false,
                                  bool image_clip=false) :
        target(a),mixed(allow_path),clipped(image_clip) {}

    void handleObject(Obj item) override
    {
        admit(++target.operators<=512u,QUANTAPDF_ERROR_UNSUPPORTED);
        admit(!item.isInlineImage(),QUANTAPDF_ERROR_UNSUPPORTED);
        if (!item.isOperator()) {
            admit(args.size()<6u,QUANTAPDF_ERROR_UNSUPPORTED);
            args.push_back(item);
            return;
        }
        const std::string op=item.getOperatorValue();
        /* Each Source V10 W/W* must be immediately followed by
         * path-ending n. An empty original path cannot silently grow
         * new operators while its Clip is pending. Older private
         * profiles retain their earlier source admission contracts. */
        if(target.native_unclipped_mode && clip_pending && op!="n")
            throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
        if(target.native_unclipped_mode &&
                  (op=="M" || op=="d")) {
            /* Public V10 has no admitted Stroke paint. Its M/d state
             * cannot affect opaque Fill/RAW Image output. Validate
             * ORIGINAL typed PDF operands and bounded pattern metadata;
             * do not introduce a dash engine, state owner or IR layer.
             * The historical private Source profiles stay strict. */
            admit(pending_path.empty() && !clip_pending,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(op=="M") {
                admit(args.size()==1u,QUANTAPDF_ERROR_UNSUPPORTED);
                const double miter=finite_number(args[0]);
                admit(miter>=1.0 && miter<=1000000.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);
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
            }
        } else if (op=="q") {
            admit(args.empty() && pending_path.empty());
            admit(stack.size()<16u,QUANTAPDF_ERROR_UNSUPPORTED);
            stack.push_back(current);
            fill_stack.push_back(fill_argb);
            if(clipped)clip_stack.push_back(clip_active);
        } else if(op=="Q") {
            admit(args.empty() && pending_path.empty() && !stack.empty());
            current=stack.back();stack.pop_back();
            fill_argb=fill_stack.back();fill_stack.pop_back();
            if(clipped) {
                admit(!clip_stack.empty());
                const bool was_clipped=clip_active!=0u;
                clip_active=clip_stack.back();clip_stack.pop_back();
                if((target.restore_second_image && target.draws.size()==1u &&
                    was_clipped && !clip_active) ||
                   (target.mixed_scoped_mode && target.clip_completed &&
                    was_clipped && !clip_active))
                    target.saw_q_restore=true;
            }
        } else if(op=="cm") {
            admit(args.size()==6u && pending_path.empty());
            matrix next;
            for(size_t i=0u;i<6u;++i)next[i]=finite_number(args[i]);
            current=concat(current,next);
        } else if(target.native_unclipped_mode &&
                  (op=="w" || op=="J" || op=="j")) {
            /* Stroking-only graphics-state parameters have no effect
             * on any painting supported by public Source V10: all
             * Stroke/Text operators STILL reject the complete page.
             * Admit strictly typed bounded originals, but allocate
             * no GS/Clip resource and no hidden paint layer. A q/Q
             * scope needs no stroke-state retainer until real Stroke
             * is implemented. Old private Source profiles refuse. */
            admit(args.size()==1u && pending_path.empty() &&
                  !clip_pending,QUANTAPDF_ERROR_UNSUPPORTED);
            if(op=="w") {
                const double width=finite_number(args[0]);
                admit(width>=0.0 && width<=1000000.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);
            } else {
                admit(args[0].isInteger(),QUANTAPDF_ERROR_UNSUPPORTED);
                const auto style=args[0].getIntValue();
                admit(style>=0ll && style<=2ll,
                      QUANTAPDF_ERROR_UNSUPPORTED);
            }
        } else if(op=="Do") {
            admit(args.size()==1u && args[0].isName() &&
                  pending_path.empty() && !clip_pending);
            if(clipped) {
                if(target.native_unclipped_mode &&
                   !target.clip_completed) {
                    /* Public Source V10: up to four homogeneous IMAGE
                     * layers without W/GS/Clip, or exactly one PATH
                     * followed by one IMAGE. A fifth IMAGE, a late PATH,
                     * Text or any unqualified resource fails atomically.
                     * Older clipped Source versions stay unchanged. */
                    const bool image_only=
                        target.painted_paths.empty() &&
                        target.draws.size()<hard_operations &&
                        target.order.size()==target.draws.size();
                    const bool path_then_image=
                        target.painted_paths.size()==1u &&
                        target.draws.empty() && target.order.size()==1u &&
                        target.order[0]==1;
                    admit(!clip_active && !target.saw_q_restore &&
                          (image_only || path_then_image),
                          QUANTAPDF_ERROR_UNSUPPORTED);
                } else {
                    admit(target.clip_completed &&
                          target.draws.size()<target.clipped_image_limit,
                          QUANTAPDF_ERROR_UNSUPPORTED);
                    if(target.mixed_scoped_mode) {
                        /* Frozen original W/Q Clip membership per paint. */
                        admit((clip_active && !target.saw_q_restore) ||
                              (!clip_active && target.saw_q_restore),
                              QUANTAPDF_ERROR_UNSUPPORTED);
                    } else if(target.restore_second_image) {
                        if(target.draws.empty()) {
                            admit(clip_active!=0u,QUANTAPDF_ERROR_UNSUPPORTED);
                        } else {
                            admit(!clip_active && target.saw_q_restore,
                                  QUANTAPDF_ERROR_UNSUPPORTED);
                        }
                    } else {
                        admit(clip_active!=0u,QUANTAPDF_ERROR_UNSUPPORTED);
                    }
                }
            }
            admit(target.resources.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            Obj xobjects=target.resources.getKey("/XObject");
            admit(xobjects.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            Obj named=xobjects.getKey(args[0].getName());
            admit(!named.isNull(),QUANTAPDF_ERROR_FORMAT);
            target.execute(named,current);
            if(target.mixed_scoped_mode) {
                QP_SOURCE_STAGE_ARM(6u);
                target.scoped_clip_membership.push_back(clip_active);
            }
        } else if(mixed && op=="rg") {
            /* The V7-only normalized RGB profile converts finite channels
             * to opaque 8-bit RGB with a deterministic nearest-integer
             * rule. It does not widen the older exact 0/1 signatures or
             * accept implicit clamping of out-of-range source colors. */
            admit(args.size()==3u && pending_path.empty());
            uint32_t rgb=0u;
            for(size_t i=0u;i<3u;++i) {
                const double channel=finite_number(args[i]);
                uint32_t component=0u;
                if(target.normalized_rgb_mode) {
                    admit(channel>=0.0 && channel<=1.0,
                          QUANTAPDF_ERROR_UNSUPPORTED);
                    component=static_cast<uint32_t>(
                        std::floor(channel*255.0+0.5));
                } else {
                    admit(channel==0.0 || channel==1.0,
                          QUANTAPDF_ERROR_UNSUPPORTED);
                    component=channel==1.0 ? 255u : 0u;
                }
                rgb|=component << (16u-8u*i);
            }
            fill_argb=UINT32_C(0xff000000)|rgb;
        } else if(mixed && target.normalized_gray_mode && op=="g") {
            /* V8-only single-channel DeviceGray fill: bound first, then
             * quantize exactly like V7's normalized RGB components.
             * Painting still uses the existing opaque 8-bit RGB IR. */
            admit(args.size()==1u && pending_path.empty());
            const double channel=finite_number(args[0]);
            admit(channel>=0.0 && channel<=1.0,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            const uint32_t gray=static_cast<uint32_t>(
                std::floor(channel*255.0+0.5));
            fill_argb=UINT32_C(0xff000000) |
                (gray<<16u) | (gray<<8u) | gray;
        } else if(target.native_unclipped_mode &&
                  (op=="G" || op=="RG" || op=="K")) {
            /* Stroking-only DeviceGray/RGB/CMYK. Public Source V10
             * admits no actual Stroke. Validate the original typed
             * operands and drop the unobservable stroke colour, not
             * paint, IR, owner, renderer or resource budget. */
            admit(pending_path.empty() && !clip_pending,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            const size_t count=op=="G"?1u:(op=="RG"?3u:4u);
            admit(args.size()==count,QUANTAPDF_ERROR_UNSUPPORTED);
            for(size_t i=0u;i<count;++i) {
                const double component=finite_number(args[i]);
                admit(component>=0.0 && component<=1.0,
                      QUANTAPDF_ERROR_UNSUPPORTED);
            }
        } else if((mixed || clipped) && op=="m") {
            admit(args.size()==2u && !clip_pending,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            begin_contour(4u);
            append_point(QUANTAPDF_COMPOSER_PATH_MOVE_TO,
                         finite_number(args[0]),finite_number(args[1]));
        } else if((mixed || clipped) && op=="l") {
            admit(args.size()==2u && !pending_path.empty() &&
                  !closed && pending_path.size()<8u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,
                         finite_number(args[0]),finite_number(args[1]));
        } else if(target.native_unclipped_mode && op=="c") {
            /* Public original PDF c: six finite control/endpoint
             * coordinates, staged in the SAME Composer CUBIC_TO kind
             * already handled by the single bounded C11 Fill raster.
             * No fallback, new paint engine or private-profile change.
             * The qualified W/W* profiles use these same commands;
             * Stroke remains explicitly unsupported. */
            admit(args.size()==6u && !pending_path.empty() &&
                  !closed && !clip_pending && pending_path.size()<8u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            std::array<double,6u> xy{};
            for(size_t i=0u;i<6u;++i)
                xy[i]=finite_number(args[i]);
            append_cubic(xy);
        } else if(target.native_unclipped_mode &&
                  (op=="v" || op=="y")) {
            /* Original PDF cubic shorthand: v uses the current point
             * as control #1; y repeats its endpoint as control #2.
             * Expand into the SAME staged CUBIC_TO command; do not
             * introduce a second curve interpreter or renderer. */
            admit(args.size()==4u && !pending_path.empty() &&
                  !closed && !clip_pending && pending_path.size()<8u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            std::array<double,4u> xy{};
            for(size_t i=0u;i<4u;++i)
                xy[i]=finite_number(args[i]);
            append_cubic_shorthand(op,xy);
        } else if((mixed || clipped) && op=="h") {
            admit(args.empty() && pending_path.size()>=contour_start+3u &&
                  !closed,QUANTAPDF_ERROR_UNSUPPORTED);
            close_path();
        } else if((mixed || clipped) && op=="re") {
            admit(args.size()==4u && !clip_pending,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            begin_contour(5u);
            const double x=finite_number(args[0]), y=finite_number(args[1]);
            const double w=finite_number(args[2]), h=finite_number(args[3]);
            /* PDF re defines an AUTHORED corner sequence, not an
             * automatically normalized positive rectangle. Only
             * public Source V10 opts into negative nonzero dimensions;
             * old private signatures keep their narrower admission.
             * Every actual transformed corner is subsequently checked
             * by append_point against the original source viewport,
             * precision lattice and bounded coordinate envelope. */
            admit((w>0.0 && h>0.0) ||
                  (target.native_unclipped_mode &&
                   w!=0.0 && h!=0.0),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            append_point(QUANTAPDF_COMPOSER_PATH_MOVE_TO,x,y);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x+w,y);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x+w,y+h);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x,y+h);
            close_path();
        } else if(mixed && (op=="f" || op=="f*")) {
            admit(args.empty() && pending_path.size()>=3u &&
                  contour_count==1u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(clipped) {
                if(target.native_unclipped_mode &&
                   !target.clip_completed) {
                    /* V10 permits PATH-only or precisely IMAGE then
                     * one opaque PATH without W/W*; a second PATH after
                     * an IMAGE, or any hidden Clip, still fails closed. */
                    const bool path_only=target.draws.empty();
                    const bool image_then_path=
                        target.draws.size()==1u &&
                        target.painted_paths.empty() &&
                        target.order.size()==1u && target.order[0]==2;
                    admit(!clip_active && !clip_pending &&
                          !target.saw_q_restore &&
                          (path_only || image_then_path),
                          QUANTAPDF_ERROR_UNSUPPORTED);
                } else if(target.mixed_scoped_mode) {
                    /* V5 permits one Path/one Do in either order inside
                     * the immutable Clip, then another Path/Do after Q. */
                    admit(target.clip_completed &&
                          ((clip_active && !target.saw_q_restore) ||
                           (!clip_active && target.saw_q_restore)),
                          QUANTAPDF_ERROR_UNSUPPORTED);
                } else {
                    /* V4 admits a source-order Image Do, real Q restoration,
                     * then one opaque PATH paint outside the frozen Clip. */
                    admit(target.restore_second_image &&
                          target.clip_completed &&
                          target.draws.size()==1u &&
                          target.saw_q_restore && !clip_active &&
                          target.order.size()==1u && target.order[0]==2,
                          QUANTAPDF_ERROR_UNSUPPORTED);
                }
            }
            if (!closed) close_path();
            target.paint_path(std::move(pending_path),fill_argb,
                op=="f" ? QUANTAPDF_COMPOSER_FILL_NONZERO :
                          QUANTAPDF_COMPOSER_FILL_EVEN_ODD);
            if(target.mixed_scoped_mode) {
                QP_SOURCE_STAGE_ARM(7u);
                target.scoped_clip_membership.push_back(clip_active);
            }
            clear_path();
        } else if(clipped && (op=="W" || op=="W*")) {
            /* Native V10 keeps the first leaf's identity across paint
             * and nested q/Q. A second definition intersects that leaf;
             * it cannot redefine a restored/unclipped scope or add a
             * third leaf. Older Source signatures stay single-Clip. */
            admit(args.empty() && !clip_pending &&
                  (!target.mixed_scoped_mode || target.order.empty() ||
                   (target.native_unclipped_mode && target.clip_completed)),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(target.native_unclipped_mode && target.clip_completed) {
                admit(!second_clip_used && !second_clip_pending &&
                      clip_active==1u && !target.saw_q_restore &&
                      pending_second_clip_contour(),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                second_clip_pending=true;
            } else {
                admit((pending_path.size()>=3u ||
                       trivial_zero_area_clip()) &&
                      !target.clip_completed &&
                      (!target.native_unclipped_mode ||
                       trivial_zero_area_clip() ||
                       bounded_clip_path(pending_path,true)),
                      QUANTAPDF_ERROR_UNSUPPORTED);
            }
            clip_rule=op=="W*" ? QUANTAPDF_COMPOSER_FILL_EVEN_ODD :
                                 QUANTAPDF_COMPOSER_FILL_NONZERO;
            clip_pending=true;
        } else if(clipped && op=="n") {
            admit(args.empty(),QUANTAPDF_ERROR_UNSUPPORTED);
            if(second_clip_pending) {
                /* Both original operands were separately parsed,
                 * billed and frozen at their definition-time CTM.
                 * A rectangle reduction is safe only if no earlier
                 * paint or saved q can observe the original first leaf. */
                std::array<float,4u> first{},next{};
                admit(target.native_unclipped_mode &&
                      target.clip_completed && clip_active==1u &&
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
                    std::find(clip_stack.begin(),clip_stack.end(),1u)!=
                        clip_stack.end();
                if(!first_observable &&
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
                }
                second_clip_pending=false;
                second_clip_used=true;
                clip_active=2u;
                clip_pending=false;
                clear_path();
            } else if(target.native_unclipped_mode && !clip_pending) {
                /* PDF n ends a path WITHOUT paint or Clip. Native V10
                 * discards only the already bounded source commands;
                 * it does not spend a C11 layer, create IR resources,
                 * or change the current graphics state. In particular
                 * W/W* followed by n must use the existing branch below.
                 * Older private Source profiles remain unchanged. */
                admit(pending_path.empty() || contour_count==1u,
                      QUANTAPDF_ERROR_UNSUPPORTED);
                clear_path();
            } else {
                admit(clip_pending && !target.clip_completed &&
                      (pending_path.size()>=3u ||
                       trivial_zero_area_clip()),
                      QUANTAPDF_ERROR_UNSUPPORTED);
                complete_clip_contour();
                admit(!target.native_unclipped_mode ||
                      bounded_clip_path(pending_path),
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
        admit(args.empty() && stack.empty() && fill_stack.empty() &&
              pending_path.empty() && !clip_pending &&
              (!clipped || clip_stack.empty()));
    }
private:
    void begin_contour(size_t minimum_commands)
    {
        if(pending_path.empty()) {
            contour_count=1u;
            contour_start=0u;
        } else {
            /* V10 Clip construction alone may start a second contour.
             * The earlier contour must already be explicitly closed;
             * enough of the unchanged nine-command lease must remain.
             * f/f* and plain n separately retain one-contour admission. */
            admit(target.native_unclipped_mode && !clip_pending &&
                  closed && contour_count==1u &&
                  bounded_clip_contour(pending_path) &&
                  pending_path.size()<=9u-minimum_commands,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            contour_count=2u;
            contour_start=pending_path.size();
        }
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
        if(!target.native_unclipped_mode)return false;
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
        admit(current[0]>0.0 && current[3]>0.0 &&
              current[1]==0.0 && current[2]==0.0,
              QUANTAPDF_ERROR_UNSUPPORTED);
        const double absolute_x=
            current[0]*x+current[4]-target.media_left;
        const double absolute_y=
            current[3]*y+current[5]-target.media_bottom;
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
        pending_path.push_back(cmd);
        admit(pending_path.size()<=8u,QUANTAPDF_ERROR_UNSUPPORTED);
    }
    void append_cubic(const std::array<double,6u>& xy)
    {
        /* Do not publish a partially admitted cubic command. All
         * three individually transformed points must pass first. */
        admit(!pending_path.empty() && !closed && !clip_pending &&
              pending_path.size()<8u,QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_composer_path_command cmd={};
        cmd.kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
        cmd.point1=stage_point(xy[0],xy[1]);
        cmd.point2=stage_point(xy[2],xy[3]);
        cmd.point3=stage_point(xy[4],xy[5]);
        pending_path.push_back(cmd);
    }
    void append_cubic_shorthand(const std::string& op,
                                const std::array<double,4u>& xy)
    {
        admit(!pending_path.empty() && !closed && !clip_pending &&
              pending_path.size()<8u &&
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
        pending_path.push_back(cmd);
    }
    void close_path()
    {
        admit(pending_path.size()>=contour_start+3u && !closed &&
              pending_path.size()<9u,QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_composer_path_command cmd={};
        cmd.kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
        pending_path.push_back(cmd);
        closed=true;
    }
    admission& target;
    bool mixed=false;
    bool clipped=false;
    unsigned char clip_active=0u;
    bool clip_pending=false;
    bool second_clip_pending=false;
    bool second_clip_used=false;
    quantapdf_composer_fill_rule clip_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    matrix current=identity;
    uint32_t fill_argb=UINT32_C(0xff000000);
    std::vector<matrix> stack;
    std::vector<uint32_t> fill_stack;
    std::vector<unsigned char> clip_stack;
    std::vector<Obj> args;
    std::vector<quantapdf_composer_path_command> pending_path;
    size_t contour_start=0u,contour_count=0u;
    bool closed=false;
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

/* Resolve Source V10's actual page-tree /Rotate under its own bounded
 * ancestry lease, independently of any early /Resources lookup. QPDF's
 * geometry reader already checked the inherited numeric value; verify
 * the nearest entry against it and reject malformed/cyclic parents. */
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
    /* A valid blank PDF page does not need a Resources dictionary.
     * Only the V10 native-only profile opts in; if any resource is
     * present, its type and inherited parent chain were still checked. */
    if(allow_absent)return Obj::newDictionary();
    throw rejected{QUANTAPDF_ERROR_FORMAT};
}

std::vector<unsigned char> page_streams(
    Obj streams,const quantapdf_qpdf_source_raw_image_limits& limits)
{
    std::vector<unsigned char> bytes;
    auto append=[&](Obj stream) {
        admit(stream.isStream());
        admit(bytes.size()<limits.max_content_bytes,
              QUANTAPDF_ERROR_UNSUPPORTED);
        size_t left=limits.max_content_bytes-bytes.size()-1u;
        auto decoded=stream_data(stream,left,false);
        admit(decoded.size()<=left,QUANTAPDF_ERROR_UNSUPPORTED);
        bytes.insert(bytes.end(),decoded.begin(),decoded.end());
        bytes.push_back('\n');
    };
    if (streams.isArray()) {
        const int n=streams.getArrayNItems();
        admit(n>0 && static_cast<size_t>(n)<=hard_streams &&
              static_cast<size_t>(n)<=limits.max_content_streams,
              QUANTAPDF_ERROR_UNSUPPORTED);
        for(int i=0;i<n;++i)append(streams.getArrayItem(i));
    } else {
        admit(!streams.isNull());
        append(streams);
    }
    return bytes;
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

/* One owned PATH producer for the independently qualified native
 * homogeneous profiles. Scope/Clip admission belongs to their wrappers;
 * source order, commands and the four-layer budget are shared here.
 * The unclipped profile retains its exact C allocation sequence. */
quantapdf_ir materialize_owned_paths(const admission& source)
{
    const size_t n=source.order.size();
    admit(n>=1u && n<=hard_operations &&
          n<=source.limits.max_operations &&
          source.draws.empty() && source.painted_paths.size()==n,
          QUANTAPDF_ERROR_UNSUPPORTED);
    for(size_t i=0u;i<n;++i)
        admit(source.order[i]==1 &&
              source.painted_paths[i].commands.size()>=4u &&
              source.painted_paths[i].commands.size()<=9u,
              QUANTAPDF_ERROR_UNSUPPORTED);

    quantapdf_ir ir=materialize_blank_page(source);
    try {
        ir.operations=static_cast<quantapdf_composer_operation *>(
            QP_SOURCE_C_CALLOC(n,sizeof(*ir.operations)));
        admit(ir.operations!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.operation_count=n;
        for(size_t i=0u;i<n;++i) {
            const staged_path& staged=source.painted_paths[i];
            auto& op=ir.operations[i];
            op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
            op.page_index=0u;
            auto& path=op.value.path;
            path.command_count=staged.commands.size();
            path.commands=static_cast<quantapdf_composer_path_command *>(
                QP_SOURCE_C_MALLOC(path.command_count*sizeof(*path.commands)));
            admit(path.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
            std::memcpy(path.commands,staged.commands.data(),
                path.command_count*sizeof(*path.commands));
            path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
            path.options.fill=1;
            path.options.fill_argb=staged.fill_argb;
            path.options.fill_rule=staged.fill_rule;
        }
        return ir;
    } catch (...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

/* Public-native Source V10 bounded 1..4 unclipped PATH-only fills.
 * No Image, Clip or GS is manufactured for legacy mixed cardinality. */
quantapdf_ir materialize_path_only(const admission& source)
{
    const size_t n=source.order.size();
    admit(source.native_unclipped_mode &&
          source.mixed_mode && source.mixed_scoped_mode &&
          n>=1u && n<=hard_operations &&
          n<=source.limits.max_operations &&
          source.draws.empty() && source.painted_paths.size()==n &&
          source.scoped_clip_membership.size()==n &&
          !source.clip_completed && source.clip.commands.empty() &&
          !source.saw_q_restore,
          QUANTAPDF_ERROR_UNSUPPORTED);
    for(size_t i=0u;i<n;++i)
        admit(source.order[i]==1 &&
              source.scoped_clip_membership[i]==0u,
              QUANTAPDF_ERROR_UNSUPPORTED);
    return materialize_owned_paths(source);
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
            operation.value.image.options.struct_size=
                QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
            operation.value.image.options.fit=
                QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
        }
    } catch (...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
    return ir;
}
/* Public-native V10 1–4 Image-only original paints are already
 * decoded by the existing bounded Source reader. This branch only
 * confirms complete source order/ownership and reuses the canonical
 * image IR materializer and four-layer C11 Raster; no provider or
 * renderer is added. Old mixed and clipped profiles stay narrower. */
quantapdf_ir materialize_image_only(const admission& source)
{
    const size_t count=source.order.size();
    admit(source.native_unclipped_mode &&
          source.mixed_mode && source.mixed_scoped_mode &&
          count>=1u && count<=hard_operations &&
          count<=source.limits.max_operations &&
          source.draws.size()==count &&
          source.painted_paths.empty() &&
          source.scoped_clip_membership.size()==count &&
          !source.clip_completed && source.clip.commands.empty() &&
          !source.saw_q_restore,
          QUANTAPDF_ERROR_UNSUPPORTED);
    for(size_t i=0u;i<count;++i)
        admit(source.order[i]==2 &&
              source.scoped_clip_membership[i]==0u,
              QUANTAPDF_ERROR_UNSUPPORTED);
    return materialize(source);
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
          source.clip.commands.size()>=4u &&
          source.clip.commands.size()<=9u &&
          ir.resources.clips==nullptr &&
          ir.resources.clip_count==0u && ir.resources.clip_capacity==0u &&
          (two ?
             (source.native_unclipped_mode &&
              qualified_owned_clip_pair(source.clip.commands,
                                        source.second_clip.commands)) :
             source.second_clip.commands.empty()),
          QUANTAPDF_ERROR_UNSUPPORTED);
    const size_t count=two?3u:1u;
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
    if(two) {
        auto& root=ir.resources.clips[2];
        root.kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        root.member_count=2u;
        root.members=static_cast<quantapdf_composer_clip_id *>(
            QP_SOURCE_C_MALLOC(root.member_count*sizeof(*root.members)));
        admit(root.members!=nullptr,QUANTAPDF_ERROR_NOMEM);
        root.members[0]=1u;
        root.members[1]=2u;
    }
    return two?3u:1u;
}

/* Native V10 alone: one to four opaque PATH fills under the final
 * frozen Clip, optionally followed by PATH fills after a real Q
 * restoration. Every paint owns only its original PATH; the existing
 * Clip publisher supplies one leaf or the qualified two-leaf root. */
quantapdf_ir materialize_clipped_path_only(const admission& source)
{
    const size_t n=source.order.size();
    admit(source.native_unclipped_mode && source.mixed_mode &&
          source.mixed_scoped_mode && source.clipped_image_mode &&
          source.clip_completed && n>=1u && n<=hard_operations &&
          n<=source.limits.max_operations &&
          source.draws.empty() && source.painted_paths.size()==n &&
          source.scoped_clip_membership.size()==n &&
          source.scoped_clip_membership[0]==1u &&
          source.clip.commands.size()>=4u &&
          source.clip.commands.size()<=9u,
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

    quantapdf_ir ir=materialize_owned_paths(source);
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

/* Image Do with frozen source W/W* Clip geometry. The original profiles
 * keep one leaf. Native V10 alone may retain two qualified leaves and
 * one exact Intersection root. Every point was frozen at definition time. */
quantapdf_ir materialize_clipped_image(const admission& source,
                                       size_t expected_images)
{
    /* Native public V10 alone accepts exactly one or two original
     * Image Do paints inside the same final frozen Clip. Every staged
     * operation has an explicit clipped membership; a second draw after
     * Q restoration is not this contract. The old source signatures
     * and their legacy clipped-image cardinalities remain unchanged. */
    const bool native_bounded=source.native_unclipped_mode &&
        source.mixed_mode && source.mixed_scoped_mode &&
        expected_images>=1u && expected_images<=2u &&
        expected_images<=source.limits.max_operations &&
        source.painted_paths.empty() &&
        source.order.size()==expected_images &&
        source.scoped_clip_membership.size()==expected_images &&
        std::all_of(source.order.begin(),source.order.end(),
                    [](int kind){return kind==2;}) &&
        std::all_of(source.scoped_clip_membership.begin(),
                    source.scoped_clip_membership.end(),
                    [](unsigned char member){return member==1u;});
    const bool legacy=!source.mixed_mode &&
        (!source.restore_second_image ||
         (expected_images==2u && source.saw_q_restore));
    admit(source.clipped_image_mode && source.clip_completed &&
          (expected_images==1u || expected_images==2u) &&
          source.draws.size()==expected_images &&
          (native_bounded || legacy) &&
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
            const quantapdf_composer_graphics_state_id state_id =
                source.restore_second_image && i==1u ? 0u : 1u;
            ir.operations[i].graphics_state_id=state_id;
            ir.operations[i].value.image.options.struct_size=
                QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
            ir.operations[i].value.image.options.graphics_state_id=state_id;
        }
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}

/* Exact, immutable mixed source admission: V1 is 1 PATH + 1 IMAGE,
 * V2 is 1 PATH + 2 IMAGEs, V3 is 2 PATHs + 2 IMAGEs. Neither QPDF
 * object owners nor PDFium handles are retained in the detached IR. */
quantapdf_ir materialize_mixed(const admission& source,
                                size_t mixed_operations,
                                bool flexible=false)
{
    /* Old V1–V3 admit exact counts. V6 may independently admit 1–2
     * Images and 1–2 Paths within the same conservative four-layer cap. */
    const size_t expected_paths=flexible ? source.painted_paths.size() :
        (mixed_operations==4u ? 2u : 1u);
    const size_t expected_images=mixed_operations-expected_paths;
    admit(source.mixed_mode &&
           (mixed_operations==2u || mixed_operations==3u ||
            mixed_operations==4u) &&
           expected_paths>=1u && expected_paths<=2u &&
           expected_images>=1u && expected_images<=2u &&
           source.draws.size()==expected_images &&
           source.painted_paths.size()==expected_paths &&
           source.order.size()==mixed_operations,
           QUANTAPDF_ERROR_UNSUPPORTED);
    size_t paths=0u,images=0u;
    for(int kind:source.order) {
        admit(kind==1 || kind==2,QUANTAPDF_ERROR_UNSUPPORTED);
        if(kind==1)++paths; else ++images;
    }
    admit(paths==expected_paths && images==expected_images,
          QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_ir ir=materialize(source);
    try {
        /* Copy borrowed IMAGE operation descriptors without duplicating or
         * moving their uniquely owned image resources. */
        quantapdf_composer_operation image_ops[2]={};
        for(size_t i=0u;i<images;++i)image_ops[i]=ir.operations[i];
        quantapdf_composer_operation *expanded=
            static_cast<quantapdf_composer_operation *>(
                QP_SOURCE_C_REALLOC(ir.operations,
                    mixed_operations*sizeof(*ir.operations)));
        admit(expanded!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.operations=expanded;
        std::memset(expanded,0,mixed_operations*sizeof(*expanded));
        ir.operation_count=mixed_operations;
        size_t image_index=0u,path_index=0u;
        for(size_t i=0u;i<mixed_operations;++i) {
            if(source.order[i]==2) {
                ir.operations[i]=image_ops[image_index++];
                continue;
            }
            const staged_path& staged=source.painted_paths[path_index++];
            quantapdf_composer_operation& painted=ir.operations[i];
            painted.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
            painted.page_index=0u;
            quantapdf_composer_path_operation& path=painted.value.path;
            const size_t count=staged.commands.size();
            path.commands=static_cast<quantapdf_composer_path_command *>(
                QP_SOURCE_C_MALLOC(count*sizeof(*path.commands)));
            admit(path.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
            path.command_count=count;
            std::memcpy(path.commands,staged.commands.data(),
                        count*sizeof(*path.commands));
            path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
            path.options.fill=1;
            path.options.fill_argb=staged.fill_argb;
            path.options.fill_rule=staged.fill_rule;
        }
        admit(image_index==expected_images && path_index==expected_paths,
              QUANTAPDF_ERROR_UNSUPPORTED);
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
    admit(source.native_unclipped_mode && source.mixed_mode &&
          source.mixed_scoped_mode && source.clipped_image_mode &&
          source.clip_completed && source.two_owned_clips &&
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

    quantapdf_ir ir=images==0u ? materialize_owned_paths(source) :
        paths==0u ? materialize(source) :
                    materialize_mixed(source,count,true);
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
                op.value.image.options.struct_size=
                    QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
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

/* Native-only public unclipped mixed subset: one opaque PATH fill and
 * one qualified decoded IMAGE in EITHER source order. The existing
 * mixed materializer copies all Image bytes and PATH commands into an
 * entirely owned IR, without manufacturing Clip/GS nodes. No other
 * source profile or C11 painter is added. */
quantapdf_ir materialize_unclipped_mixed(const admission& source)
{
    admit(source.native_unclipped_mode &&
          source.mixed_mode && source.mixed_scoped_mode &&
          !source.clip_completed && source.clip.commands.empty() &&
          !source.saw_q_restore &&
          source.draws.size()==1u &&
          source.painted_paths.size()==1u &&
          source.order.size()==2u &&
          source.scoped_clip_membership.size()==2u &&
          source.scoped_clip_membership[0]==0u &&
          source.scoped_clip_membership[1]==0u &&
          source.limits.max_operations>=2u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    admit((source.order[0]==1 && source.order[1]==2) ||
          (source.order[0]==2 && source.order[1]==1),
          QUANTAPDF_ERROR_UNSUPPORTED);
    return materialize_mixed(source,2u,true);
}

/* Strict Source V5: two paints under one frozen W/W* Clip and two
 * after Q restoration, with one PATH and one Image Do in each scope.
 * The generic four-layer renderer subsequently preserves every operation's
 * immutable GS identity; no previous V1–V4 source entry is broadened. */
quantapdf_ir materialize_scoped_mixed_v5(const admission& source,
                                         bool flexible_v6=false)
{
    const size_t count=source.order.size();
    admit(source.mixed_scoped_mode && source.mixed_mode &&
          source.clipped_image_mode && source.clip_completed &&
          source.saw_q_restore && count>=2u && count<=4u &&
          source.draws.size()>=1u && source.draws.size()<=2u &&
          source.painted_paths.size()>=1u &&
          source.painted_paths.size()<=2u &&
          source.draws.size()+source.painted_paths.size()==count &&
          source.scoped_clip_membership.size()==count &&
          source.clip.commands.size()>=4u &&
          source.clip.commands.size()<=9u &&
          (flexible_v6 || (count==4u && source.draws.size()==2u &&
                           source.painted_paths.size()==2u)),
          QUANTAPDF_ERROR_UNSUPPORTED);
    unsigned inside_images=0u,inside_paths=0u;
    unsigned outside_images=0u,outside_paths=0u;
    bool saw_unclipped=false;
    for(size_t i=0u;i<count;++i) {
        const unsigned char active=source.scoped_clip_membership[i];
        admit(active<=1u,QUANTAPDF_ERROR_UNSUPPORTED);
        if(!flexible_v6)
            admit(active==(i<2u ? 1u : 0u),QUANTAPDF_ERROR_UNSUPPORTED);
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
    /* The public native-only V10 profile also admits exactly one
     * Image Do and one opaque Path Fill within the same final frozen
     * Clip, in either authored paint order. This never broadens V5–V9
     * private Source signatures, and three all-clipped paints still
     * refuse atomically rather than silently dropping an operation. */
    const bool native_two_inside=source.native_unclipped_mode &&
        flexible_v6 && count==2u &&
        source.draws.size()==1u &&
        source.painted_paths.size()==1u &&
        source.scoped_clip_membership.size()==2u &&
        source.scoped_clip_membership[0]==1u &&
        source.scoped_clip_membership[1]==1u;
    if(flexible_v6)
        admit(inside_images+inside_paths>=1u &&
              (outside_images+outside_paths>=1u || native_two_inside),
              QUANTAPDF_ERROR_UNSUPPORTED);
    else
        admit(inside_images==1u && inside_paths==1u &&
              outside_images==1u && outside_paths==1u,
              QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_ir ir=materialize_mixed(source,count,flexible_v6);
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
                op.value.image.options.struct_size=
                    QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
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
/* Source V4 is image+PATH, unlike the earlier two-IMAGE restored V3.
 * Copy the complete ordered source into an owned 1 IMAGE+1 PATH IR, then
 * attach ONLY the first Image operation to the frozen Clip Graphics State. */
quantapdf_ir materialize_restored_image_then_path(const admission& source)
{
    admit(source.mixed_mode && source.clipped_image_mode &&
          source.restore_second_image && source.clip_completed &&
          source.saw_q_restore && source.draws.size()==1u &&
          source.painted_paths.size()==1u && source.order.size()==2u &&
          source.order[0]==2 && source.order[1]==1 &&
          source.clip.commands.size()>=4u &&
          source.clip.commands.size()<=9u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_ir ir=materialize_mixed(source,2u);
    try {
        admit(ir.operation_count==2u && ir.resources.image_count==1u &&
              ir.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE &&
              ir.operations[1].kind==QUANTAPDF_COMPOSER_OPERATION_PATH,
              QUANTAPDF_ERROR_UNSUPPORTED);
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
        state.clip_id=1u;

        ir.resources.clips=
            static_cast<quantapdf_composer_clip_state *>(
                QP_SOURCE_C_CALLOC(1u,sizeof(*ir.resources.clips)));
        admit(ir.resources.clips!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.clip_count=1u;
        ir.resources.clip_capacity=1u;
        auto& clip=ir.resources.clips[0];
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.fill_rule=source.clip.rule;
        clip.command_count=source.clip.commands.size();
        clip.commands=static_cast<quantapdf_composer_path_command *>(
            QP_SOURCE_C_MALLOC(clip.command_count*sizeof(*clip.commands)));
        admit(clip.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(clip.commands,source.clip.commands.data(),
                    clip.command_count*sizeof(*clip.commands));
        clip.transform={1.0f,0.0f,0.0f,1.0f,0.0f,0.0f};

        ir.operations[0].graphics_state_id=1u;
        ir.operations[0].value.image.options.struct_size=
            QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
        ir.operations[0].value.image.options.graphics_state_id=1u;
        /* Operation #1 represents original post-Q paint with no Clip. */
        admit(ir.operations[1].graphics_state_id==0u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        return ir;
    } catch(...) {
        quantapdf_ir_dispose(&ir);
        throw;
    }
}
} // namespace

/* Source V10 only: bind the original page index to its actual Page
 * leaf under a completely checked, bounded PageTree, never merely to
 * the first leaf or an unverified /Count shortcut. Visit the entire
 * tree (not just the requested prefix) so malformed later Kids,
 * duplicate leaves, cycles and contradictory Count cannot disappear
 * behind a successful early match. This is QPDF structure admission,
 * NOT a second content parser, renderer or provider fallback. */
constexpr size_t source_v10_max_tree_nodes=128u;
size_t source_v10_walk_pages(
    Obj node,Obj expected_parent,size_t depth,size_t max_depth,
    std::set<QPDFObjGen>& seen,size_t& nodes,size_t& leaf_index,
    size_t target,Obj& selected)
{
    admit(depth<=max_depth && depth<=256u &&
          nodes<source_v10_max_tree_nodes,
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
        admit(leaf_index<source_v10_max_tree_nodes,
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
          static_cast<uint64_t>(count)<=source_v10_max_tree_nodes,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const int n=kids.getArrayNItems();
    admit(n>0 && static_cast<size_t>(n)<=source_v10_max_tree_nodes,
          QUANTAPDF_ERROR_UNSUPPORTED);
    size_t subtotal=0u;
    for(int i=0;i<n;++i) {
        const size_t children=source_v10_walk_pages(
            kids.getArrayItem(i),node,depth+1u,max_depth,
            seen,nodes,leaf_index,target,selected);
        admit(subtotal<=source_v10_max_tree_nodes-children,
              QUANTAPDF_ERROR_UNSUPPORTED);
        subtotal+=children;
    }
    admit(subtotal==static_cast<size_t>(count),
          QUANTAPDF_ERROR_FORMAT);
    return subtotal;
}

Obj source_v10_selected_page(QPDF& pdf,size_t selected_index,
                             size_t max_parent_steps)
{
    admit(selected_index<=static_cast<size_t>(INT_MAX) &&
          max_parent_steps!=0u,
          QUANTAPDF_ERROR_UNSUPPORTED);
    std::set<QPDFObjGen> seen;
    size_t visited=0u,leaves=0u;
    Obj selected=Obj::newNull();
    const Obj root=pdf.getRoot().getKey("/Pages");
    source_v10_walk_pages(root,Obj::newNull(),1u,max_parent_steps,
                          seen,visited,leaves,selected_index,selected);
    admit(!selected.isNull(),QUANTAPDF_ERROR_UNSUPPORTED);
    return selected;
}

/* Exact quarter-point M0 Source box parser. Use the original QPDF
 * NUMBER token, not double->snprintf (which can change decimal bounds).
 * This deliberately admits only the 1/4-point lattice. A fractional
 * viewport SPAN is a separate M1 gate: do not silently ceil, round,
 * stretch, or lose a partial final row/column in the current Raster. */
int64_t source_v10_quarters(Obj number)
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
    admit(seen_digit && (decimal_part*4ll)%denominator==0ll,
          QUANTAPDF_ERROR_UNSUPPORTED);
    const int64_t quarters=integer_part*4ll+
        decimal_part*4ll/denominator;
    admit(quarters<=4000000ll,QUANTAPDF_ERROR_UNSUPPORTED);
    return negative?-quarters:quarters;
}

std::array<int64_t,4> source_v10_quarter_box(Obj box)
{
    admit(box.isArray() && box.getArrayNItems()==4,
          QUANTAPDF_ERROR_FORMAT);
    std::array<int64_t,4> q={};
    for(size_t i=0u;i<4u;++i)
        q[i]=source_v10_quarters(box.getArrayItem((int)i));
    if(q[0]>q[2])std::swap(q[0],q[2]);
    if(q[1]>q[3])std::swap(q[1],q[3]);
    admit(q[2]>q[0] && q[3]>q[1],QUANTAPDF_ERROR_FORMAT);
    return q;
}

struct source_v10_exact_boxes {
    std::array<int64_t,4> media;
    std::array<int64_t,4> crop;
    std::array<int64_t,4> visible;
};

source_v10_exact_boxes source_v10_quarter_geometry(
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
    const auto m=source_v10_quarter_box(media);
    const auto c=crop.isNull()?m:source_v10_quarter_box(crop);
    std::array<int64_t,4> view={
        std::max(m[0],c[0]),std::max(m[1],c[1]),
        std::min(m[2],c[2]),std::min(m[3],c[3])
    };
    admit(view[2]>view[0] && view[3]>view[1],
          QUANTAPDF_ERROR_FORMAT);
    /* The independent, existing QPDF page geometry reader must agree
     * with the exact selected original numbers. For admitted quarters,
     * binary64 representation and comparison are exact. */
    for(size_t i=0u;i<4u;++i) {
        admit(geo.media[i]==(double)m[i]/4.0 &&
              geo.crop[i]==(double)c[i]/4.0,
              QUANTAPDF_ERROR_FORMAT);
    }
    admit(geo.left==(double)view[0]/4.0 &&
          geo.bottom==(double)view[1]/4.0 &&
          geo.right==(double)view[2]/4.0 &&
          geo.top==(double)view[3]/4.0,
          QUANTAPDF_ERROR_FORMAT);
    /* Complete exact source quarter boxes; the C11 viewport sampler
     * now owns fractional final-column/row geometric coverage. */
    return {m,c,view};
}

/* Strict V10-only original /UserUnit is a PAGE leaf number, not an
 * inheritable PageTree property. PDF box coordinates and authorial
 * CTM are admitted in their own original units before this physical
 * scale is applied. Original Quarter source * Quarter UserUnit is an
 * exact SIXTEENTH-point device-space coordinate. */
unsigned checked_source_v10_userunit(Obj page,double geometry_unit)
{
    const Obj raw=page.getKey("/UserUnit");
    const int64_t quarters=raw.isNull()?4ll:source_v10_quarters(raw);
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
size_t source_quarter_ceil(int64_t lo,int64_t hi,size_t cap)
{
    admit(hi>lo && cap!=0u,QUANTAPDF_ERROR_UNSUPPORTED);
    const uint64_t n=(uint64_t)(hi-lo);
    const uint64_t pixels=n/4u+(n%4u!=0u);
    admit(pixels>0u && pixels<=(uint64_t)cap,
          QUANTAPDF_ERROR_UNSUPPORTED);
    return (size_t)pixels;
}

static quantapdf_status source_raw_images_to_ir_impl(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir,source_profile_id profile_id,
    size_t selected_page_index=0u)
{
    if(out_ir!=nullptr) std::memset(out_ir,0,sizeof(*out_ir));
    if(out_ir==nullptr || original_pdf==nullptr || limits==nullptr ||
       size==0u || limits->max_pdf_bytes==0u ||
       limits->max_content_bytes==0u || limits->max_content_streams==0u ||
       limits->max_operations==0u || limits->max_page_pixels==0u ||
       limits->max_source_pixels==0u || limits->max_drawn_pixels==0u ||
       limits->max_parent_steps==0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(size>hard_pdf_bytes || size>limits->max_pdf_bytes ||
       limits->max_content_bytes>hard_content_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    try {
        auto pdf=QPDF::create();
        /* Match the production no-recovery source contract: a corrected
         * or warning-bearing PDF must never become a native positive. */
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile("Native Image original",
            reinterpret_cast<const char *>(original_pdf),size,"");
        const bool native_v10=
            profile_id==source_profile_id::scoped_mixed_v10_flate_predictor;
        /* Old private source profiles remain explicitly page zero.
         * Only public V10 may resolve a caller-selected original page. */
        admit(native_v10 || selected_page_index==0u,
              QUANTAPDF_ERROR_UNSUPPORTED);
        Obj selected=Obj::newNull();
        if(native_v10)
            selected=source_v10_selected_page(
                *pdf,selected_page_index,limits->max_parent_steps);
        quantapdf_qpdf_page_geometry geo={};
        const auto status=quantapdf_qpdf_load_page_geometry(
            *pdf,static_cast<int>(selected_page_index),&geo);
        if(status!=QUANTAPDF_OK)return status;
        if(native_v10)
            admit(geo.page.isSameObjectAs(selected),
                  QUANTAPDF_ERROR_FORMAT);
        /* Quarters of a source point are parsed from the ORIGINAL QPDF
         * numeric token under independent source ancestry bounds, not
         * approximated by reformatting binary64. M0 admits fractional
         * endpoints only when visible and full Media spans stay integer.
         * This protects the C11 Raster's canonical integer page grid. */
        const source_v10_exact_boxes exact=native_v10?
            source_v10_quarter_geometry(
                geo.page,limits->max_parent_steps,geo):
            source_v10_exact_boxes{};
        const unsigned user_quarters=native_v10?
            checked_source_v10_userunit(geo.page,geo.user_unit):4u;
        const bool full_media=
            geo.left==geo.media[0] &&
            geo.bottom==geo.media[1] &&
            geo.right==geo.media[2] && geo.top==geo.media[3] &&
            geo.crop[0]==geo.media[0] &&
            geo.crop[1]==geo.media[1] &&
            geo.crop[2]==geo.media[2] &&
            geo.crop[3]==geo.media[3];
        admit((geo.rotation==0 ||
               (native_v10 && (geo.rotation==90 ||
                                geo.rotation==180 ||
                                geo.rotation==270))) &&
              (native_v10 ||
               (full_media &&
                geo.left==0.0 && geo.bottom==0.0 &&
                geo.media[0]==0.0 && geo.media[1]==0.0)),
              QUANTAPDF_ERROR_UNSUPPORTED);
        if(native_v10) {
            /* Even a tiny CropBox cannot make an unbounded original
             * MediaBox an inexpensive source: budget the WHOLE media
             * before admitting bounded outside-crop paint geometry. */
            const size_t mw=source_quarter_ceil(
                exact.media[0],exact.media[2],hard_page_pixels);
            const size_t mh=source_quarter_ceil(
                exact.media[1],exact.media[3],hard_page_pixels);
            admit(mw!=0u && mh!=0u &&
                  mw<=limits->max_page_pixels/mh,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            const uint64_t mw16=(uint64_t)(
                exact.media[2]-exact.media[0])*(uint64_t)user_quarters;
            const uint64_t mh16=(uint64_t)(
                exact.media[3]-exact.media[1])*(uint64_t)user_quarters;
            const size_t device_mw=(size_t)(mw16/16u+
                (mw16%16u!=0u));
            const size_t device_mh=(size_t)(mh16/16u+
                (mh16%16u!=0u));
            admit(device_mw!=0u && device_mh!=0u &&
                  device_mw<=limits->max_page_pixels/device_mh,
                  QUANTAPDF_ERROR_UNSUPPORTED);
        }
        /* Source operators use the UNROTATED original media canvas.
         * The completely admitted staging will acquire the page-tree
         * orientation before publication, with no extra pixel producer. */
        const size_t page_w=native_v10?
            source_quarter_ceil(
                exact.visible[0],exact.visible[2],hard_page_pixels):
            whole(geo.right-geo.left,hard_page_pixels);
        const size_t page_h=native_v10?
            source_quarter_ceil(
                exact.visible[1],exact.visible[3],hard_page_pixels):
            whole(geo.top-geo.bottom,hard_page_pixels);
        if(native_v10)
            checked_original_rotation(
                geo.page,limits->max_parent_steps,geo.rotation);
        admit(page_w!=0u && page_h!=0u &&
              page_w<=hard_page_pixels/page_h &&
              page_w*page_h<=limits->max_page_pixels,
              QUANTAPDF_ERROR_UNSUPPORTED);
        admit(geo.page.getKey("/Group").isNull() &&
              geo.page.getKey("/Annots").isNull(),
              QUANTAPDF_ERROR_UNSUPPORTED);
        /* An unpainted page may omit both Contents and Resources under
         * Source V10. Earlier fixed source profile contracts stay strict.
         * Any existing inherited Resources is still type-checked. */
        Obj resources=bounded_page_resources(
            geo.page,limits->max_parent_steps,native_v10);
        /* Initialize ALL members explicitly. Some CI toolchains enable
         * -Werror=missing-field-initializers for aggregate additions. */
        admission source{resources,*limits,page_w,page_h,0u,0u,0u,
                          {},false,{},{},2u,false,false,{},{},false,
                          1u,false,false,false,{},false,false,false,
                          false,false,false};
        const source_profile_contract& profile=source_profile_for(profile_id);
        source.mixed_mode=profile.mixed;
        source.clipped_image_mode=profile.clipped;
        source.clipped_image_limit=profile.clip_image_limit;
        source.restore_second_image=profile.restore_second_image;
        source.mixed_operation_limit=profile.mixed_operation_limit;
        source.mixed_scoped_mode=profile.frozen_scoped_membership;
        source.variable_scoped_mode=profile.variable_operations;
        source.normalized_rgb_mode=profile.normalized_rgb;
        source.normalized_gray_mode=profile.normalized_gray;
        source.explicit_image_decode_mode=profile.explicit_image_decode;
        source.explicit_image_predictor_mode=
            profile.explicit_image_predictor;
        source.native_unclipped_mode=native_v10;
        source.exact_page_w=native_v10?
            (double)(exact.visible[2]-exact.visible[0])/4.0:0.0;
        source.exact_page_h=native_v10?
            (double)(exact.visible[3]-exact.visible[1])/4.0:0.0;
        source.media_left=native_v10?geo.left:0.0;
        source.media_bottom=native_v10?geo.bottom:0.0;
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
        source.crop_viewport=native_v10 &&
            (!full_media || fractional_geometry);
        Obj raw_contents=geo.page.getKey("/Contents");
        const bool empty_contents=native_v10 &&
            (raw_contents.isNull() ||
             (raw_contents.isArray() &&
              raw_contents.getArrayNItems()==0));
        auto contents=empty_contents ?
            std::vector<unsigned char>{} :
            page_streams(raw_contents,*limits);
        QPDF parser;
        parser.emptyPDF();
        parser.setSuppressWarnings(true);
        Obj parsed=parser.newStream(std::string(contents.begin(),contents.end()));
        image_content_reader reader(source,source.mixed_mode,
                                    source.clipped_image_mode);
        parsed.parseAsContents(&reader);
        admit(!parser.anyWarnings() && !pdf->anyWarnings(),
              QUANTAPDF_ERROR_FORMAT);
        const bool clip_states=native_v10 &&
            prepare_native_clip_membership(source);
        if(native_v10) {
            original_turn_staging(source,geo.rotation);
            scale_source_userunit(source,user_quarters);
        }
        if(clip_states) {
            *out_ir=materialize_clip_states(source);
            return QUANTAPDF_OK;
        }
        /* This is NOT a recovery after unsupported content: the entire
         * page stream was parsed, EOF validated q/Q and pending-path
         * ownership, and zero paint/clip operations were admitted. */
        if(native_v10 && source.draws.empty() &&
           source.painted_paths.empty() && source.order.empty() &&
           source.scoped_clip_membership.empty() &&
           !source.clip_completed && !source.saw_q_restore) {
            *out_ir=materialize_blank_page(source);
            return QUANTAPDF_OK;
        }
        /* A one-to-four PATH original without Image or W/W* uses the
         * SAME V10 Source reader and canonical C11 Raster. No silent
         * omission is possible: qpdf parsed complete source and EOF;
         * the path-only IR has no Image/GS/Clip allocations. */
        if(native_v10 && source.draws.empty() &&
           !source.painted_paths.empty() &&
           !source.clip_completed && source.clip.commands.empty() &&
           !source.saw_q_restore) {
            *out_ir=materialize_path_only(source);
            return QUANTAPDF_OK;
        }
        /* The complete native PATH-only source may use the existing
         * frozen Clip graph and an optional real Q-restored suffix.
         * This branch never supplies a fake Image to mixed profiles. */
        if(native_v10 && source.draws.empty() &&
           !source.painted_paths.empty() && source.clip_completed) {
            *out_ir=materialize_clipped_path_only(source);
            return QUANTAPDF_OK;
        }
        /* This path alone may use 1–4 homogeneous IMAGE layers,
         * bounded by the exact four-operation Source/Raster contract.
         * No Clip, PATH, Font or hidden graphics state is inferred. */
        if(native_v10 && source.painted_paths.empty() &&
           !source.draws.empty() && !source.clip_completed &&
           source.clip.commands.empty() && !source.saw_q_restore) {
            *out_ir=materialize_image_only(source);
            return QUANTAPDF_OK;
        }
        /* The full original PDF has been parsed and every q/Q, CTM,
         * image and PATH admitted. Only now publish the qualified,
         * zero-Clip/GS owned two-operation output. */
        if(native_v10 && source.draws.size()==1u &&
           source.painted_paths.size()==1u &&
           !source.clip_completed && source.clip.commands.empty() &&
           !source.saw_q_restore) {
            *out_ir=materialize_unclipped_mixed(source);
            return QUANTAPDF_OK;
        }
        /* One/two original Image Do paints under the final frozen Clip
         * are a separate homogeneous Source V10 profile. Exact
         * membership preflight is repeated inside the materializer
         * before publishing owned IR. No PATH operation is invented. */
        if(native_v10 && !source.draws.empty() &&
           source.draws.size()<=2u &&
           source.painted_paths.empty() &&
           source.clip_completed &&
           source.order.size()==source.draws.size() &&
           source.scoped_clip_membership.size()==source.draws.size()) {
            *out_ir=materialize_clipped_image(
                source,source.draws.size());
            return QUANTAPDF_OK;
        }
        const bool flexible_scoped=profile.variable_operations;
        const bool qualified_mixed=!source.mixed_mode ||
            (flexible_scoped ?
                (source.draws.size()>=1u && source.draws.size()<=2u &&
                 source.painted_paths.size()>=1u &&
                 source.painted_paths.size()<=2u &&
                 source.order.size()==source.draws.size()+
                     source.painted_paths.size() &&
                 source.order.size()>=2u && source.order.size()<=4u &&
                 source.order.size()<=source.limits.max_operations &&
                 source.scoped_clip_membership.size()==source.order.size()) :
                (source.draws.size()==(source.mixed_operation_limit==4u ?
                        2u : source.mixed_operation_limit-1u) &&
                 source.painted_paths.size()==(source.mixed_operation_limit==4u ?
                        2u : 1u) &&
                 source.order.size()==source.mixed_operation_limit));
        admit(!source.draws.empty() && qualified_mixed &&
               (!source.clipped_image_mode ||
                (source.clip_completed &&
                 (flexible_scoped ||
                  source.draws.size()==source.clipped_image_limit) &&
                 (!source.restore_second_image || source.saw_q_restore) &&
                 (!flexible_scoped || source.saw_q_restore))),
               QUANTAPDF_ERROR_UNSUPPORTED);
        /* Materialization is selected by the SAME immutable contract
         * that admitted the source operators and source-layer quota. */
        switch(profile.output) {
        case source_materialization::raw_images:
            *out_ir=materialize(source);
            break;
        case source_materialization::mixed:
            *out_ir=materialize_mixed(source,source.mixed_operation_limit);
            break;
        case source_materialization::clipped_images:
            *out_ir=materialize_clipped_image(
                source,source.clipped_image_limit);
            break;
        case source_materialization::clipped_image_then_path:
            *out_ir=materialize_restored_image_then_path(source);
            break;
        case source_materialization::scoped_exact:
            *out_ir=materialize_scoped_mixed_v5(source);
            break;
        case source_materialization::scoped_flexible:
            *out_ir=materialize_scoped_mixed_v5(source,true);
            break;
        default:
            throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
        }
        return QUANTAPDF_OK;
    } catch(rejected const& e) {return e.status;}
      catch(std::bad_alloc const&) {return QUANTAPDF_ERROR_NOMEM;}
      catch(std::length_error const&) {return QUANTAPDF_ERROR_UNSUPPORTED;}
      catch(QPDFExc const&) {return QUANTAPDF_ERROR_FORMAT;}
      catch(std::runtime_error const&) {return QUANTAPDF_ERROR_FORMAT;}
      catch(...) {return QUANTAPDF_ERROR_BACKEND;}
}

extern "C" quantapdf_status quantapdf_qpdf_source_raw_images_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::raw_images);
}

extern "C" quantapdf_status quantapdf_qpdf_source_mixed_path_image_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::mixed_path_image_v1);
}

/* The three-layer variant is independently admitted; the previously
 * qualified V1 source function remains exactly one PATH plus one IMAGE. */
extern "C" quantapdf_status quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::mixed_path_two_images_v2);
}

/* Opt-in four-paint source qualification: the earlier source signatures
 * cannot silently admit the expanded number of PATH or IMAGE operations. */
extern "C" quantapdf_status
quantapdf_qpdf_source_mixed_two_paths_two_images_to_ir_v3(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::mixed_two_paths_two_images_v3);
}

/* First original-source W/W* Clip + Image Do profile. Its C11 consumer
 * requires exactly one Clip leaf, one GS and one uniquely owned Image. */
extern "C" quantapdf_status
quantapdf_qpdf_source_clipped_raw_image_to_ir_v1(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(original_pdf,size,limits,out_ir,
        source_profile_id::clipped_image_v1);
}

/* Source V2 admits TWO IMAGE Do operations while the same frozen W/W*
 * Clip leaf remains active across q/Q. V1 remains exactly one Do. */
extern "C" quantapdf_status
quantapdf_qpdf_source_two_clipped_raw_images_to_ir_v2(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(original_pdf,size,limits,out_ir,
        source_profile_id::two_clipped_images_v2);
}

/* Source V3: an actual Q-restored second Image Do has NO Clip/GS ID.
 * This must not widen either of the previous clipped-source signatures. */
extern "C" quantapdf_status
quantapdf_qpdf_source_restored_clipped_raw_images_to_ir_v3(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::restored_clipped_images_v3);
}

/* V4 source Image under one W/W* Clip leaf followed by a Q-restored,
 * unmasked opaque PATH fill. Previous API modes keep their exact caps. */
extern "C" quantapdf_status
quantapdf_qpdf_source_clipped_image_then_path_to_ir_v4(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::clipped_image_then_path_v4);
}

/* Opt-in Source Scoped Mixed V5: one immutable W/W* Clip and exactly four
 * source-ordered paints, comprising one Image Do and one opaque PATH before
 * the real Q pop, and one of each after restoration. Each operation owns
 * its frozen GS/Clip membership. All previous V1–V4 signatures retain
 * their stricter cardinality and operator admission. */
extern "C" quantapdf_status
quantapdf_qpdf_source_scoped_mixed_v5_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::scoped_mixed_v5);
}

/* Opt-in Source Scoped Mixed V6: one frozen W/W* leaf, a REAL Q restore,
 * and 2–4 Path/Image paints. At least one of each kind, at least one
 * clipped paint, and at least one later unclipped paint. The public source
 * V5 signature remains exactly two Paths and two Images in fixed 2+2 scopes;
 * this mode reuses the same renderer and has NO provider fallback. */
extern "C" quantapdf_status
quantapdf_qpdf_source_scoped_mixed_v6_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(original_pdf,size,limits,out_ir,
        source_profile_id::scoped_mixed_v6);
}

/* Opt-in Source V7: keep V6's 2..4 bounded Path/Image and frozen W/W*
 * Clip/Q scopes, but normalize source rg channel values in [0,1] to
 * deterministic 8-bit opaque RGB. Earlier source modes keep their strict
 * exact-binary color admission and no public renderer is switched. */
extern "C" quantapdf_status
quantapdf_qpdf_source_scoped_mixed_v7_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::scoped_mixed_v7_rgb);
}

/* Opt-in Source Scoped Mixed V8: V7's bounded Path/Image, frozen W/W*
 * and real Q scope admission with additional finite DeviceGray nonstroking
 * g. Quantize gray to opaque RGB; no public ABI or C11 Raster expansion. */
extern "C" quantapdf_status
quantapdf_qpdf_source_scoped_mixed_v8_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(original_pdf,size,limits,out_ir,
        source_profile_id::scoped_mixed_v8_gray);
}

/* Private Source V9: strict V8 2..4 scoped Path/RAW Image paints with
 * independent, explicit Image /Decode identity or inversion admission.
 * Earlier source entry points, C11 Raster and public PDFium renderer
 * remain strictly unchanged. */
extern "C" quantapdf_status
quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::scoped_mixed_v9_image_decode);
}

/* Private Source V10: keep V9's exact Decode and scoped Path/Image
 * semantics; opt in to strictly validated 8-bit Flate PNG/TIFF predictors.
 * Historical signature stays fixed to original page index zero.
 * Public selection is a separate private V10-only API below. */
extern "C" quantapdf_status
quantapdf_qpdf_source_scoped_mixed_v10_flate_predictor_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::scoped_mixed_v10_flate_predictor);
}

/* This private ONLY V10 entry binds the original caller-selected index
 * to the checked page-tree leaf and publishes a ONE-PAGE local IR whose
 * operations remain index zero for the canonical V5 C11 renderer.
 * No earlier Source signature changes its selected page contract. */
extern "C" quantapdf_status
quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    size_t selected_page_index,quantapdf_ir *out_ir)
{
    return source_raw_images_to_ir_impl(
        original_pdf,size,limits,out_ir,
        source_profile_id::scoped_mixed_v10_flate_predictor,
        selected_page_index);
}
