#include "backend/qpdf_native_image_ir.h"
#include "backend/qpdf_document_internal.h"

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>

#include <algorithm>
#include <array>
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
    size_t clipped_image_limit=1u;
    bool restore_second_image=false;
    bool saw_q_restore=false;
    /* Source V5 freezes a distinct Clip/GS identity for every paint.
     * 1 means inside the original W/W* scope, 0 means Q-restored. */
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
    /* V10 public-native only: one/two fully parsed, unclipped,
     * homogeneous opaque PATH or RAW/Flate IMAGE paints. The older
     * mixed W/Q Path+Image profiles retain their exact admission. */
    bool native_unclipped_mode=false;

    void paint_path(std::vector<quantapdf_composer_path_command> commands,
                    uint32_t color,quantapdf_composer_fill_rule rule)
    {
        /* Public V10 PATH-only pages may consume the EXISTING four
         * canonical C11 raster layers. A page with a frozen W/W* Clip
         * or any Image retains the independently qualified strict
         * one/two PATH admission: no implicit mixed/Clip expansion. */
        const size_t allowed_paths=
            native_unclipped_mode && !clip_completed && draws.empty() ?
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
        painted_paths.push_back(std::move(item));
        order.push_back(1);
    }

    void execute(Obj image,const matrix& ctm)
    {
        admit(draws.size() < hard_operations &&
              draws.size() < limits.max_operations &&
              (!mixed_mode ||
               (draws.size()<(mixed_operation_limit==4u ? 2u : mixed_operation_limit-1u) &&
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
        const size_t x0=whole(ctm[4],page_w),
                     x1=whole(ctm[4]+ctm[0],page_w);
        const size_t bottom=whole(ctm[5],page_h),
                     top=whole(ctm[5]+ctm[3],page_h);
        admit(x1>x0 && top>bottom,QUANTAPDF_ERROR_UNSUPPORTED);
        const size_t y0=page_h-top, y1=page_h-bottom;
        const size_t draw_area=(x1-x0)*(y1-y0);
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
        entry.bounds={static_cast<float>(x0),static_cast<float>(y0),
                      static_cast<float>(x1),static_cast<float>(y1)};
        draws.push_back(std::move(entry));
        if (mixed_mode) order.push_back(2);
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
        if (op=="q") {
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
                const bool was_clipped=clip_active;
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
        } else if(op=="Do") {
            admit(args.size()==1u && args[0].isName() &&
                  pending_path.empty() && !clip_pending);
            if(clipped) {
                if(target.native_unclipped_mode &&
                   !target.clip_completed) {
                    /* Public Source V10: homogeneous IMAGE-only, or
                     * exactly one PATH followed by one IMAGE, both with
                     * no W/GS/Clip. Older clipped Source profiles retain
                     * their original admission. Never admit a third
                     * layer or an unsupported late paint. */
                    const bool image_only=
                        target.painted_paths.empty() &&
                        target.draws.size()<2u &&
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
                            admit(clip_active,QUANTAPDF_ERROR_UNSUPPORTED);
                        } else {
                            admit(!clip_active && target.saw_q_restore,
                                  QUANTAPDF_ERROR_UNSUPPORTED);
                        }
                    } else {
                        admit(clip_active,QUANTAPDF_ERROR_UNSUPPORTED);
                    }
                }
            }
            admit(target.resources.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            Obj xobjects=target.resources.getKey("/XObject");
            admit(xobjects.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            Obj named=xobjects.getKey(args[0].getName());
            admit(!named.isNull(),QUANTAPDF_ERROR_FORMAT);
            target.execute(named,current);
            if(target.mixed_scoped_mode)
                target.scoped_clip_membership.push_back(clip_active ? 1u : 0u);
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
        } else if((mixed || clipped) && op=="m") {
            admit(args.size()==2u && pending_path.empty(),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            append_point(QUANTAPDF_COMPOSER_PATH_MOVE_TO,
                         finite_number(args[0]),finite_number(args[1]));
        } else if((mixed || clipped) && op=="l") {
            admit(args.size()==2u && !pending_path.empty() &&
                  !closed && pending_path.size()<8u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,
                         finite_number(args[0]),finite_number(args[1]));
        } else if((mixed || clipped) && op=="h") {
            admit(args.empty() && pending_path.size()>=3u &&
                  !closed,QUANTAPDF_ERROR_UNSUPPORTED);
            close_path();
        } else if((mixed || clipped) && op=="re") {
            admit(args.size()==4u && pending_path.empty(),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            const double x=finite_number(args[0]), y=finite_number(args[1]);
            const double w=finite_number(args[2]), h=finite_number(args[3]);
            admit(w>0.0 && h>0.0,QUANTAPDF_ERROR_UNSUPPORTED);
            append_point(QUANTAPDF_COMPOSER_PATH_MOVE_TO,x,y);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x+w,y);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x+w,y+h);
            append_point(QUANTAPDF_COMPOSER_PATH_LINE_TO,x,y+h);
            close_path();
        } else if(mixed && (op=="f" || op=="f*")) {
            admit(args.empty() && pending_path.size()>=3u,
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
            if(target.mixed_scoped_mode)
                target.scoped_clip_membership.push_back(clip_active ? 1u : 0u);
            pending_path.clear();
            closed=false;
        } else if(clipped && (op=="W" || op=="W*")) {
            admit(args.empty() && pending_path.size()>=3u &&
                  !clip_pending && !target.clip_completed &&
                  (!target.mixed_scoped_mode || target.order.empty()),
                  QUANTAPDF_ERROR_UNSUPPORTED);
            clip_rule=op=="W*" ? QUANTAPDF_COMPOSER_FILL_EVEN_ODD :
                                 QUANTAPDF_COMPOSER_FILL_NONZERO;
            clip_pending=true;
        } else if(clipped && op=="n") {
            admit(args.empty() && clip_pending &&
                  !target.clip_completed && pending_path.size()>=3u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            if(!closed)close_path();
            target.clip.commands=std::move(pending_path);
            target.clip.rule=clip_rule;
            target.clip_completed=true;
            clip_active=true;
            clip_pending=false;
            pending_path.clear();
            closed=false;
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
    void append_point(quantapdf_composer_path_command_kind kind,
                      double x,double y)
    {
        /* Source coordinates/CTM are resolved at construction time.
         * First mixed version excludes negative scale, shear and
         * fractional pixel positions. No PDF writer round-trip. */
        admit(current[0]>0.0 && current[3]>0.0 &&
              current[1]==0.0 && current[2]==0.0,
              QUANTAPDF_ERROR_UNSUPPORTED);
        const size_t ix=whole(current[0]*x+current[4],target.page_w);
        const size_t iy=whole(current[3]*y+current[5],target.page_h);
        quantapdf_composer_path_command cmd={};
        cmd.kind=kind;
        cmd.point1.x=static_cast<float>(ix);
        cmd.point1.y=static_cast<float>(target.page_h-iy);
        pending_path.push_back(cmd);
        admit(pending_path.size()<=8u,QUANTAPDF_ERROR_UNSUPPORTED);
    }
    void close_path()
    {
        admit(pending_path.size()>=3u && !closed &&
              pending_path.size()<9u,QUANTAPDF_ERROR_UNSUPPORTED);
        quantapdf_composer_path_command cmd={};
        cmd.kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
        pending_path.push_back(cmd);
        closed=true;
    }
    admission& target;
    bool mixed=false;
    bool clipped=false;
    bool clip_active=false;
    bool clip_pending=false;
    quantapdf_composer_fill_rule clip_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    matrix current=identity;
    uint32_t fill_argb=UINT32_C(0xff000000);
    std::vector<matrix> stack;
    std::vector<uint32_t> fill_stack;
    std::vector<bool> clip_stack;
    std::vector<Obj> args;
    std::vector<quantapdf_composer_path_command> pending_path;
    bool closed=false;
};

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
        std::calloc(1u,sizeof(*ir.pages)));
    admit(ir.pages!=nullptr,QUANTAPDF_ERROR_NOMEM);
    ir.page_count=1u;
    ir.pages[0].width_points=static_cast<float>(source.page_w);
    ir.pages[0].height_points=static_cast<float>(source.page_h);
    ir.pages[0].background_argb=UINT32_C(0xffffffff);
    return ir;
}

/* Public-native Source V10 bounded 1..4 PATH-only fills: never invent
 * an Image or Clip resource to satisfy the legacy mixed cardinality.
 * This stays inside the EXISTING four-layer global C11 Raster limit.
 * Exact source order, budget, EOF and complete ownership are validated
 * before publishing uniquely owned IR. */
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

    quantapdf_ir ir=materialize_blank_page(source);
    try {
        ir.operations=static_cast<quantapdf_composer_operation *>(
            std::calloc(n,sizeof(*ir.operations)));
        admit(ir.operations!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.operation_count=n;
        for(size_t i=0u;i<n;++i) {
            const staged_path& staged=source.painted_paths[i];
            admit(staged.commands.size()>=4u &&
                  staged.commands.size()<=9u,
                  QUANTAPDF_ERROR_UNSUPPORTED);
            auto& op=ir.operations[i];
            op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
            op.page_index=0u;
            auto& path=op.value.path;
            path.command_count=staged.commands.size();
            path.commands=static_cast<quantapdf_composer_path_command *>(
                std::malloc(path.command_count*sizeof(*path.commands)));
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

quantapdf_ir materialize(
    const admission& source)
{
    quantapdf_ir ir={};
    try {
        ir.pages=static_cast<quantapdf_composer_page_state *>(
            std::calloc(1u,sizeof(*ir.pages)));
        admit(ir.pages!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.page_count=1u;
        ir.pages[0].width_points=static_cast<float>(source.page_w);
        ir.pages[0].height_points=static_cast<float>(source.page_h);
        ir.pages[0].background_argb=UINT32_C(0xffffffff);

        const size_t count=source.draws.size();
        admit(count>0u && count<=hard_operations,QUANTAPDF_ERROR_UNSUPPORTED);
        ir.operations=static_cast<quantapdf_composer_operation *>(
            std::calloc(count,sizeof(*ir.operations)));
        admit(ir.operations!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.images=static_cast<quantapdf_composer_image_state *>(
            std::calloc(count,sizeof(*ir.resources.images)));
        admit(ir.resources.images!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.operation_count=count;
        ir.resources.image_count=count;
        ir.resources.image_capacity=count;
        for(size_t i=0u;i<count;++i) {
            const staged_image& staged=source.draws[i];
            auto& image=ir.resources.images[i];
            image.data=static_cast<unsigned char *>(std::malloc(staged.pixels.size()));
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
/* Public-native V10 1–2 Image-only original paints are already
 * decoded by the existing bounded Source reader. This branch only
 * confirms complete source order/ownership and reuses the canonical
 * image IR materializer; no different raster or provider is created. */
quantapdf_ir materialize_image_only(const admission& source)
{
    const size_t count=source.order.size();
    admit(source.native_unclipped_mode &&
          source.mixed_mode && source.mixed_scoped_mode &&
          count>=1u && count<=2u &&
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

/* Image Do with one frozen source W/W* Clip Path. The old image-only
 * and mixed Path/Image profiles stay deliberately narrower. Clip geometry
 * is already transformed into page-display coordinates at definition time. */
quantapdf_ir materialize_clipped_image(const admission& source,
                                       size_t expected_images)
{
    /* Native public V10 alone accepts exactly one or two original
     * Image Do paints inside the SAME frozen W/W* Clip. Every staged
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
                std::calloc(1u,sizeof(*ir.resources.graphics_states)));
        admit(ir.resources.graphics_states!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.graphics_state_count=1u;
        ir.resources.graphics_state_capacity=1u;
        auto& state=ir.resources.graphics_states[0];
        state.fill_alpha=1.0f;
        state.stroke_alpha=1.0f;
        state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        state.clip_id=1u;

        ir.resources.clips=static_cast<quantapdf_composer_clip_state *>(
            std::calloc(1u,sizeof(*ir.resources.clips)));
        admit(ir.resources.clips!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.clip_count=1u;
        ir.resources.clip_capacity=1u;
        auto& clip=ir.resources.clips[0];
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.fill_rule=source.clip.rule;
        clip.command_count=source.clip.commands.size();
        clip.commands=static_cast<quantapdf_composer_path_command *>(
            std::malloc(clip.command_count*sizeof(*clip.commands)));
        admit(clip.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(clip.commands,source.clip.commands.data(),
                    clip.command_count*sizeof(*clip.commands));
        clip.transform={1.0f,0.0f,0.0f,1.0f,0.0f,0.0f};
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
                std::realloc(ir.operations,
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
                std::malloc(count*sizeof(*path.commands)));
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
     * Image Do and one opaque Path Fill within the SAME frozen W/W*
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
            std::calloc(1u,sizeof(*ir.resources.graphics_states)));
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
            std::calloc(1u,sizeof(*ir.resources.clips)));
        admit(ir.resources.clips!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.clip_count=1u;
        ir.resources.clip_capacity=1u;
        auto& clip=ir.resources.clips[0];
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.fill_rule=source.clip.rule;
        clip.command_count=source.clip.commands.size();
        clip.commands=static_cast<quantapdf_composer_path_command *>(
            std::malloc(clip.command_count*sizeof(*clip.commands)));
        admit(clip.commands!=nullptr,QUANTAPDF_ERROR_NOMEM);
        std::memcpy(clip.commands,source.clip.commands.data(),
                    clip.command_count*sizeof(*clip.commands));
        clip.transform={1.0f,0.0f,0.0f,1.0f,0.0f,0.0f};
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
                std::calloc(1u,sizeof(*ir.resources.graphics_states)));
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
                std::calloc(1u,sizeof(*ir.resources.clips)));
        admit(ir.resources.clips!=nullptr,QUANTAPDF_ERROR_NOMEM);
        ir.resources.clip_count=1u;
        ir.resources.clip_capacity=1u;
        auto& clip=ir.resources.clips[0];
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.fill_rule=source.clip.rule;
        clip.command_count=source.clip.commands.size();
        clip.commands=static_cast<quantapdf_composer_path_command *>(
            std::malloc(clip.command_count*sizeof(*clip.commands)));
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

static quantapdf_status source_raw_images_to_ir_impl(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir,source_profile_id profile_id)
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
        quantapdf_qpdf_page_geometry geo={};
        auto status=quantapdf_qpdf_load_page_geometry(*pdf,0,&geo);
        if(status!=QUANTAPDF_OK)return status;
        admit(geo.rotation==0 && geo.user_unit==1.0 &&
              geo.left==0.0 && geo.bottom==0.0 &&
              geo.media[0]==0.0 && geo.media[1]==0.0 &&
              geo.right==geo.media[2] && geo.top==geo.media[3] &&
              geo.crop[0]==geo.media[0] &&
              geo.crop[1]==geo.media[1] &&
              geo.crop[2]==geo.media[2] &&
              geo.crop[3]==geo.media[3],
              QUANTAPDF_ERROR_UNSUPPORTED);
        const size_t page_w=whole(geo.public_width,hard_page_pixels);
        const size_t page_h=whole(geo.public_height,hard_page_pixels);
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
        const bool native_v10=
            profile_id==source_profile_id::scoped_mixed_v10_flate_predictor;
        Obj resources=bounded_page_resources(
            geo.page,limits->max_parent_steps,native_v10);
        /* Initialize ALL members explicitly. Some CI toolchains enable
         * -Werror=missing-field-initializers for aggregate additions. */
        admission source{resources,*limits,page_w,page_h,0u,0u,0u,
                          {},false,{},{},2u,false,false,{},
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
        /* A one/two PATH original without Image or W/W* now uses the
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
        /* One/two original Image Do paints under ONE frozen W/W* Clip
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
 * semantics; opt in to strictly validated 8-bit Flate PNG/TIFF predictors
 * on each Image XObject. No public backend, C11 Raster or V1–V9 change. */
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
