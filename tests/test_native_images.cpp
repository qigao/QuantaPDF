#include <quantapdf/quantapdf.h>
#include "backend/jpeg_encoder.h"
#include <qpdf/Pl_Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using Obj = QPDFObjectHandle;
void check(bool ok, const char *expression, int line)
{
    if (!ok) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, line, expression); std::exit(EXIT_FAILURE); }
}
#define ICHECK(x) check((x), #x, __LINE__)
struct fixture {
    QPDF pdf;
    Obj page, resources, objects;
    fixture()
    {
        pdf.emptyPDF();
        page = pdf.makeIndirectObject(Obj::parse("<< /Type /Page /MediaBox [0 0 240 160] "
            "/CropBox [10 20 210 120] /UserUnit 2 >>"));
        resources = Obj::newDictionary(); objects = Obj::newDictionary();
        resources.replaceKey("/XObject", objects);
        page.replaceKey("/Resources", resources); pdf.addPage(page, false);
    }
    Obj image(std::string bytes, int width, int height, int bits, Obj color)
    {
        Obj image = pdf.newStream(bytes), d = image.getDict();
        d.replaceKey("/Type", Obj::newName("/XObject"));
        d.replaceKey("/Subtype", Obj::newName("/Image"));
        d.replaceKey("/Width", Obj::newInteger(width)); d.replaceKey("/Height", Obj::newInteger(height));
        d.replaceKey("/BitsPerComponent", Obj::newInteger(bits)); d.replaceKey("/ColorSpace", color);
        return image;
    }
    Obj rgb()
    { return image(std::string("\xff\0\0\0\xff\0", 6), 2, 1, 8, Obj::newName("/DeviceRGB")); }
    void content(std::string const& bytes) { page.replaceKey("/Contents", pdf.newStream(bytes)); }
    void write() { QPDFWriter writer(pdf, NATIVE_DOCUMENT_OUTPUT_PDF); writer.write(); }
    quantapdf_image_page *extract(quantapdf_status expected = QUANTAPDF_OK)
    {
        write();
        quantapdf_document *document = nullptr; quantapdf_page *loaded = nullptr;
        ICHECK(quantapdf_open(NATIVE_DOCUMENT_OUTPUT_PDF, nullptr, &document) == QUANTAPDF_OK);
        ICHECK(quantapdf_load_page(document, 0, &loaded) == QUANTAPDF_OK);
        auto result = reinterpret_cast<quantapdf_image_page *>(1);
        ICHECK(quantapdf_extract_images(loaded, &result) == expected);
        if (expected != QUANTAPDF_OK) ICHECK(result == nullptr);
        quantapdf_drop_page(loaded); quantapdf_close(document);
        return result;
    }
};

void pixels(quantapdf_image_page *images, size_t index,
    std::vector<unsigned char> const& expected, int width, int channels, unsigned tolerance = 0)
{
    quantapdf_bitmap *bitmap = nullptr;
    ICHECK(quantapdf_image_render(images, index, &bitmap) == QUANTAPDF_OK);
    int w = 0, h = 0, stride = 0, c = 0;
    ICHECK(quantapdf_bitmap_dimensions(bitmap, &w, &h, &stride, &c) == QUANTAPDF_OK);
    ICHECK(w == width && h == 1 && c == channels && stride == width*channels);
    const unsigned char *bytes = nullptr; size_t size = 0;
    ICHECK(quantapdf_bitmap_data(bitmap, &bytes, &size) == QUANTAPDF_OK && size == expected.size());
    for (size_t i = 0; i < size; ++i)
        ICHECK(std::abs(static_cast<int>(bytes[i])-static_cast<int>(expected[i])) <= static_cast<int>(tolerance));
    quantapdf_drop_bitmap(bitmap);
}
void point(quantapdf_point const& p, float x, float y)
{ ICHECK(std::abs(p.x-x) < 0.001f && std::abs(p.y-y) < 0.001f); }

void placements()
{
    fixture f; Obj image = f.rgb(); f.objects.replaceKey("/I", image);
    Obj form = f.pdf.newStream("q 5 0 0 6 7 8 cm /Local Do Q");
    form.getDict().replaceKey("/Subtype", Obj::newName("/Form"));
    form.getDict().replaceKey("/BBox", Obj::parse("[0 0 100 100]"));
    form.getDict().replaceKey("/Matrix", Obj::parse("[0 1 -1 0 20 10]"));
    Obj own = Obj::newDictionary(), own_objects = Obj::newDictionary();
    own_objects.replaceKey("/Local", image); own.replaceKey("/XObject", own_objects);
    form.getDict().replaceKey("/Resources", own); f.objects.replaceKey("/F", form);
    // q/cm survive the content-stream boundary; Form's local names shadow the page.
    f.page.replaceKey("/Contents", Obj::newArray({
        f.pdf.newStream("q 10 0 0 20 30 40 cm"),
        f.pdf.newStream("/I Do Q q 2 0 0 3 4 5 cm /F Do Q")}));
    auto images = f.extract(); size_t count = 0;
    ICHECK(quantapdf_image_count(images, &count) == QUANTAPDF_OK && count == 2);
    quantapdf_image_info a = {}, b = {}; a.struct_size = sizeof(a); b.struct_size = sizeof(b);
    ICHECK(quantapdf_image_get_info(images, 0, &a) == QUANTAPDF_OK);
    ICHECK(quantapdf_image_get_info(images, 1, &b) == QUANTAPDF_OK);
    point(a.quad.ul,40,120); point(a.quad.ur,60,120); point(a.quad.ll,40,160); point(a.quad.lr,60,160);
    point(b.quad.ul,12,128); point(b.quad.ur,12,98); point(b.quad.ll,36,128); point(b.quad.lr,36,98);
    pixels(images,0,{255,0,0,0,255,0},2,3); pixels(images,1,{255,0,0,0,255,0},2,3);
    quantapdf_drop_image_page(images);
    // Inherited resources, quarter rotation and a second independent extraction.
    f.page.removeKey("/Resources"); f.page.getKey("/Parent").replaceKey("/Resources", f.resources);
    f.page.replaceKey("/Rotate", Obj::newInteger(90)); f.content("q 10 0 0 20 30 40 cm /I Do Q");
    images = f.extract(); a.struct_size = sizeof(a);
    ICHECK(quantapdf_image_get_info(images,0,&a) == QUANTAPDF_OK);
    point(a.quad.ul,80,40); point(a.quad.ur,80,60); point(a.quad.ll,40,40); point(a.quad.lr,40,60);
    pixels(images,0,{255,0,0,0,255,0},2,3); quantapdf_drop_image_page(images);
}

void samples_and_masks()
{
    for (int bits : {1,2,4,8,16}) {
        fixture f;
        std::string bytes;
        if (bits < 8) bytes.push_back(static_cast<char>(((1u<<bits)-1u) << (8-2*bits)));
        else if (bits == 8) bytes = std::string("\0\xff",2);
        else bytes = std::string("\0\0\xff\xff",4);
        Obj gray = f.image(bytes,2,1,bits,Obj::newName("/DeviceGray"));
        gray.getDict().replaceKey("/Decode",Obj::parse("[1 0]"));
        f.objects.replaceKey("/I",gray); f.content("/I Do");
        auto images = f.extract(); pixels(images,0,{255,255,255,0,0,0},2,3);
        quantapdf_drop_image_page(images);
    }
    {
        fixture f;
        Obj indexed = Obj::newArray({Obj::newName("/Indexed"),Obj::newName("/DeviceRGB"),
            Obj::newInteger(3),Obj::newString(std::string("\xff\0\0\0\xff\0\0\0\xff\xff\xff\xff",12))});
        Obj spaces = Obj::newDictionary(); spaces.replaceKey("/Palette",indexed);
        f.resources.replaceKey("/ColorSpace",spaces);
        Obj image = f.image(std::string(1,'\x18'),3,1,2,Obj::newName("/Palette"));
        f.objects.replaceKey("/I",image); f.content("/I Do"); auto images = f.extract();
        pixels(images,0,{255,0,0,0,255,0,0,0,255},3,3); quantapdf_drop_image_page(images);
    }
    {
        fixture f; Obj image = f.rgb();
        image.getDict().replaceKey("/SMask", f.image(std::string(1,'\x80'),1,1,8,Obj::newName("/DeviceGray")));
        f.objects.replaceKey("/I",image); f.content("/I Do"); auto images = f.extract();
        pixels(images,0,{128,0,0,128,0,128,0,128},2,4); quantapdf_drop_image_page(images);
    }
    {
        fixture f; Obj image = f.rgb(); image.getDict().replaceKey("/Mask",Obj::parse("[255 255 0 0 0 0]"));
        f.objects.replaceKey("/I",image); f.content("/I Do"); auto images = f.extract();
        pixels(images,0,{0,0,0,0,0,255,0,255},2,4); quantapdf_drop_image_page(images);
    }
    {
        fixture f; f.objects.replaceKey("/I",f.rgb());
        f.resources.replaceKey("/ExtGState",Obj::parse("<< /Half << /ca 0.5 >> >>"));
        f.content("q /Half gs /I Do Q /I Do"); auto images = f.extract();
        pixels(images,0,{128,0,0,128,0,128,0,128},2,4);
        pixels(images,1,{255,0,0,0,255,0},2,3); quantapdf_drop_image_page(images);
    }
    {
        fixture f;
        Obj image = f.image(std::string("\xff\x7f\x7f",3),1,1,8,Obj::newName("/DeviceRGB"));
        Obj mask = f.image(std::string(1,'\x80'),1,1,8,Obj::newName("/DeviceGray"));
        mask.getDict().replaceKey("/Matte",Obj::parse("[1 1 1]"));
        image.getDict().replaceKey("/SMask",mask); f.objects.replaceKey("/I",image); f.content("/I Do");
        auto images = f.extract(); pixels(images,0,{128,0,0,128},1,4);
        quantapdf_drop_image_page(images);
    }
}

std::string jpeg(int components, std::vector<unsigned char> values = {})
{
    Pl_Buffer output("native JPEG test");
    std::unique_ptr<quantapdf::detail::jpeg_encoder> encoder;
    quantapdf::detail::jpeg_image_spec spec{2,1,components,100};
    ICHECK(quantapdf::detail::jpeg_encoder::create(spec,&output,&encoder) == QUANTAPDF_OK);
    if (values.empty()) values.assign(static_cast<size_t>(components)*2u,255);
    encoder->pipeline()->write(values.data(),values.size()); encoder->pipeline()->finish();
    auto bytes = output.getBufferSharedPointer();
    return std::string(reinterpret_cast<char const *>(bytes->getBuffer()),bytes->getSize());
}
void jpeg_images()
{
    for (int components : {1,3}) for (bool wrapped : {false,true}) {
        fixture f; std::string bytes = jpeg(components);
        Obj filter = Obj::newName("/DCTDecode");
        if (wrapped) {
            static const char hex[]="0123456789ABCDEF"; std::string encoded;
            for (unsigned char byte : bytes) { encoded += hex[byte>>4u]; encoded += hex[byte&15u]; }
            bytes = encoded + ">";
            filter = Obj::parse("[/ASCIIHexDecode /DCTDecode]");
        }
        Obj image = f.image(bytes,2,1,8,Obj::newName(components==1?"/DeviceGray":"/DeviceRGB"));
        image.getDict().replaceKey("/Filter",filter); f.objects.replaceKey("/I",image); f.content("/I Do");
        auto images = f.extract(); pixels(images,0,{255,255,255,255,255,255},2,3,1);
        quantapdf_drop_image_page(images);
        image.getDict().replaceKey("/Width",Obj::newInteger(3));
        f.extract(QUANTAPDF_ERROR_FORMAT);
    }
    for (int transform : {0,1}) {
        fixture f;
        Obj image = f.image(jpeg(3,{255,0,0,0,0,255}),2,1,8,Obj::newName("/DeviceRGB"));
        image.getDict().replaceKey("/Filter",Obj::newName("/DCTDecode"));
        Obj parms = Obj::newDictionary(); parms.replaceKey("/ColorTransform",Obj::newInteger(transform));
        image.getDict().replaceKey("/DecodeParms",parms);
        f.objects.replaceKey("/I",image); f.content("/I Do");
        auto images = f.extract();
        if (transform == 0) pixels(images,0,{255,0,0,0,0,255},2,3,2);
        else pixels(images,0,{76,255,28,178,0,0},2,3,2);
        quantapdf_drop_image_page(images);
    }
}

void rejections()
{
    for (std::string const content : {"/I Do unknown", "Q", "q /I Do", "BI /W 1 /H 1 ID x EI"}) {
        fixture f; f.objects.replaceKey("/I",f.rgb()); f.content(content);
        f.extract(content == "Q" || content == "q /I Do" ? QUANTAPDF_ERROR_FORMAT : QUANTAPDF_ERROR_UNSUPPORTED);
    }
    {
        fixture f; Obj image = f.rgb(); image.getDict().replaceKey("/Width",Obj::newInteger(INT_MAX));
        f.objects.replaceKey("/I",image); f.content("/I Do"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    {
        fixture f; Obj image = f.rgb(); image.getDict().replaceKey("/ColorSpace",Obj::newName("/DeviceCMYK"));
        f.objects.replaceKey("/I",image); f.content("/I Do"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    {
        fixture f; f.objects.replaceKey("/I",f.rgb());
        Obj form = f.pdf.newStream("/Self Do"); form.getDict().replaceKey("/Subtype",Obj::newName("/Form"));
        Obj own = Obj::newDictionary(), objects = Obj::newDictionary(); objects.replaceKey("/Self",form);
        own.replaceKey("/XObject",objects); form.getDict().replaceKey("/Resources",own);
        f.objects.replaceKey("/F",form); f.content("/I Do /F Do"); f.extract(QUANTAPDF_ERROR_FORMAT);
    }
    {
        fixture f; f.objects.replaceKey("/I",f.rgb());
        std::string content; for (size_t i=0;i<4097u;++i) content += "/I Do\n";
        f.content(content); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    {
        fixture f; f.content(std::string(8u*1024u*1024u,' ')); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    {
        fixture f; f.objects.replaceKey("/I",f.rgb());
        f.resources.replaceKey("/ExtGState",Obj::parse("<< /Bad << /SMask << /S /Alpha >> >> >>"));
        f.content("/I Do /Bad gs /I Do"); f.extract(QUANTAPDF_ERROR_UNSUPPORTED);
    }
}
}

void quantapdf_test_native_images()
{
    placements(); samples_and_masks(); jpeg_images(); rejections();
}
