#include "backend/qpdf_native_image_ir.h"
#include "backend/qpdf_document_internal.h"

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
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
                                      bool image)
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
    /* First source milestone: only raw and Flate streams, no predictor,
     * crypt filters or unexpected transformation chains. */
    admit(dict.getKey("/DecodeParms").isNull(),
        QUANTAPDF_ERROR_UNSUPPORTED);
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

struct admission {
    Obj resources;
    const quantapdf_qpdf_source_raw_image_limits& limits;
    size_t page_w,page_h;
    size_t source_pixels=0,drawn_pixels=0,operators=0;
    std::vector<staged_image> draws;

    void execute(Obj image,const matrix& ctm)
    {
        admit(draws.size() < hard_operations &&
              draws.size() < limits.max_operations,
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
              dict.getKey("/Decode").isNull() &&
              dict.getKey("/Alternates").isNull() &&
              dict.getKey("/OPI").isNull() &&
              dict.getKey("/Matte").isNull(),
              QUANTAPDF_ERROR_UNSUPPORTED);
        Obj interp=dict.getKey("/Interpolate");
        admit(interp.isNull() || (interp.isBool() && !interp.getBoolValue()),
              QUANTAPDF_ERROR_UNSUPPORTED);
        Obj bits=dict.getKey("/BitsPerComponent");
        admit(bits.isInteger());
        admit(bits.getIntValue() == 8, QUANTAPDF_ERROR_UNSUPPORTED);
        Obj colorspace=dict.getKey("/ColorSpace");
        admit(colorspace.isName(),QUANTAPDF_ERROR_UNSUPPORTED);
        int channels=colorspace.getName()=="/DeviceGray" ? 1 :
            (colorspace.getName()=="/DeviceRGB" ? 3 : 0);
        admit(channels != 0,QUANTAPDF_ERROR_UNSUPPORTED);

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
        auto decoded=stream_data(image,size,true);
        admit(decoded.size()==size);
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
    }
};

class image_content_reader final : public Obj::ParserCallbacks {
public:
    explicit image_content_reader(admission& a) : target(a) {}
    void handleObject(Obj item) override
    {
        admit(++target.operators <= 512u,QUANTAPDF_ERROR_UNSUPPORTED);
        admit(!item.isInlineImage(),QUANTAPDF_ERROR_UNSUPPORTED);
        if(!item.isOperator()) {
            admit(args.size()<6u,QUANTAPDF_ERROR_UNSUPPORTED);
            args.push_back(item);
            return;
        }
        const std::string op=item.getOperatorValue();
        if(op=="q") {
            admit(args.empty());
            admit(stack.size()<16u,QUANTAPDF_ERROR_UNSUPPORTED);
            stack.push_back(current);
        } else if(op=="Q") {
            admit(args.empty() && !stack.empty());
            current=stack.back();
            stack.pop_back();
        } else if(op=="cm") {
            admit(args.size()==6u);
            matrix next;
            for(size_t i=0;i<6u;++i)next[i]=finite_number(args[i]);
            current=concat(current,next);
        } else if(op=="Do") {
            admit(args.size()==1u && args[0].isName());
            admit(target.resources.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            Obj xobjects=target.resources.getKey("/XObject");
            admit(xobjects.isDictionary(),QUANTAPDF_ERROR_FORMAT);
            Obj target_name=xobjects.getKey(args[0].getName());
            admit(!target_name.isNull(),QUANTAPDF_ERROR_FORMAT);
            target.execute(target_name,current);
        } else {
            /* Unlike the image-occurrence observer, a rendering source
             * parser must reject ALL other operators, even when a valid
             * image was drawn earlier. No silent skipped-page content. */
            throw rejected{QUANTAPDF_ERROR_UNSUPPORTED};
        }
        args.clear();
    }
    void handleEOF() override { admit(args.empty() && stack.empty()); }
private:
    admission& target;
    matrix current=identity;
    std::vector<matrix> stack;
    std::vector<Obj> args;
};

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
} // namespace

extern "C" quantapdf_status quantapdf_qpdf_source_raw_images_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir)
{
    if(out_ir!=nullptr) std::memset(out_ir,0,sizeof(*out_ir));
    if(out_ir==nullptr || original_pdf==nullptr || limits==nullptr ||
       size==0u || limits->max_pdf_bytes==0u ||
       limits->max_content_bytes==0u || limits->max_content_streams==0u ||
       limits->max_operations==0u || limits->max_page_pixels==0u ||
       limits->max_source_pixels==0u || limits->max_drawn_pixels==0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(size>hard_pdf_bytes || size>limits->max_pdf_bytes ||
       limits->max_content_bytes>hard_content_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    try {
        auto pdf=QPDF::create();
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
        Obj resources=quantapdf_qpdf_inherited_value(geo.page,"/Resources");
        admit(resources.isDictionary());
        admission source{resources,*limits,page_w,page_h};
        auto contents=page_streams(geo.page.getKey("/Contents"),*limits);
        QPDF parser;
        parser.emptyPDF();
        parser.setSuppressWarnings(true);
        Obj parsed=parser.newStream(std::string(contents.begin(),contents.end()));
        image_content_reader reader(source);
        parsed.parseAsContents(&reader);
        admit(!parser.anyWarnings());
        admit(!source.draws.empty(),QUANTAPDF_ERROR_UNSUPPORTED);
        *out_ir=materialize(source);
        return QUANTAPDF_OK;
    } catch(rejected const& e) {return e.status;}
      catch(std::bad_alloc const&) {return QUANTAPDF_ERROR_NOMEM;}
      catch(std::length_error const&) {return QUANTAPDF_ERROR_UNSUPPORTED;}
      catch(QPDFExc const&) {return QUANTAPDF_ERROR_FORMAT;}
      catch(std::runtime_error const&) {return QUANTAPDF_ERROR_FORMAT;}
      catch(...) {return QUANTAPDF_ERROR_BACKEND;}
}
