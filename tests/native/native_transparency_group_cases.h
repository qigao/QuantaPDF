#ifndef QUANTAPDF_NATIVE_TRANSPARENCY_GROUP_CASES_H
#define QUANTAPDF_NATIVE_TRANSPARENCY_GROUP_CASES_H

#include <quantapdf/quantapdf.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace native_transparency_group {

using Object = QPDFObjectHandle;

inline std::string serialize_pdf(QPDF& pdf)
{
    QPDFWriter writer(pdf);
    writer.setMinimumPDFVersion("1.4");
    writer.setOutputMemory();
    writer.write();
    const auto buffer = writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

inline Object make_rgb_image(QPDF& pdf, unsigned char r, unsigned char g, unsigned char b)
{
    const std::string data = {static_cast<char>(r), static_cast<char>(g), static_cast<char>(b)};
    auto stream = pdf.newStream(data);
    auto dict = stream.getDict();
    dict.replaceKey("/Type", Object::newName("/XObject"));
    dict.replaceKey("/Subtype", Object::newName("/Image"));
    dict.replaceKey("/Width", Object::newInteger(1));
    dict.replaceKey("/Height", Object::newInteger(1));
    dict.replaceKey("/BitsPerComponent", Object::newInteger(8));
    dict.replaceKey("/ColorSpace", Object::newName("/DeviceRGB"));
    return stream;
}

inline Object make_form(QPDF& pdf, const std::string& content, const char* dict_spec,
    Object resources = Object::newNull())
{
    auto stream = pdf.newStream(content);
    auto dict = stream.getDict();
    auto entries = Object::parse(dict_spec);
    for(const auto& key : entries.getKeys())
        dict.replaceKey(key, entries.getKey(key));
    if(!resources.isNull())
        dict.replaceKey("/Resources", resources);
    return stream;
}

inline std::shared_ptr<QPDF> make_base_pdf(const std::string& page_content, Object& out_resources)
{
    auto pdf = QPDF::create();
    const auto font_fixture = native_ttf_fixture();
    pdf->processMemoryFile("transparency-group fixture", font_fixture.data(), font_fixture.size());
    auto page = pdf->getAllPages().front();
    page.replaceKey("/MediaBox", Object::parse("[0 0 12 4]"));
    page.replaceKey("/Contents", pdf->newStream(page_content));
    out_resources = page.getKey("/Resources");
    auto states = Object::parse(
        "<< /Opaque << /ca 1 /BM /Normal >> "
        "/Half << /ca .5 /BM /Normal >> "
        "/Zero << /ca 0 /BM /Normal >> "
        "/Multiply << /BM /Multiply /ca 1 >> "
        "/HalfMultiply << /BM /Multiply /ca .5 >> "
        "/Difference << /BM /Difference /ca 1 >> "
        "/Screen << /BM /Screen /ca 1 >> "
        "/Hue << /BM /Hue /ca 1 >> "
        "/Luminosity << /BM /Luminosity /ca 1 >> "
        "/Knockout << /TK true >> /Separate << /TK false >> >>");
    out_resources.replaceKey("/ExtGState", states);
    return pdf;
}

inline quantapdf_status composer_group_builder(quantapdf_composer* composer, size_t page, void*)
{
    const auto make_rect = [](float x0, float y0, float x1, float y1) {
        std::array<quantapdf_composer_path_command, 5> c{};
        c[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO; c[0].point1 = {x0, y0};
        c[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; c[1].point1 = {x1, y0};
        c[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; c[2].point1 = {x1, y1};
        c[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; c[3].point1 = {x0, y1};
        c[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
        return c;
    };
    const quantapdf_composer_graphics_state_options half_state{
        0.5f, 1.0f, QUANTAPDF_COMPOSER_BLEND_NORMAL, 0u, 0u,
        QUANTAPDF_COMPOSER_TEXT_KNOCKOUT_INHERIT};
    quantapdf_composer_graphics_state_id half_gs = 0u;
    auto status = quantapdf_composer_add_graphics_state(composer, &half_state, &half_gs);
    if(status != QUANTAPDF_OK)return status;

    quantapdf_composer_path_options paint{};
    paint.fill = 1;
    paint.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    paint.fill_argb = UINT32_C(0xffff0000);
    const auto full = make_rect(0.0f, 0.0f, 4.0f, 4.0f);
    status = quantapdf_composer_draw_path(composer, page, full.data(), full.size(), &paint);
    if(status != QUANTAPDF_OK)return status;

    paint.fill_argb = UINT32_C(0xff00ff00);
    paint.graphics_state_id = half_gs;
    return quantapdf_composer_draw_path(composer, page, full.data(), full.size(), &paint);
}

} // namespace native_transparency_group

static int transparency_group_cases(const std::string& filename)
{
    using namespace native_transparency_group;
    size_t renders = 0u, refusals = 0u;

    const auto render_bytes = [&](const std::string& bytes, unsigned dpi,
        std::vector<unsigned char>& out_pixels, quantapdf_status expected = QUANTAPDF_OK) -> int {
        CHECK(write_pdf(filename, bytes));
        quantapdf_context* ctx = nullptr;
        quantapdf_document* doc = nullptr;
        quantapdf_page* page = nullptr;
        quantapdf_bitmap* bmp = reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        OK(quantapdf_context_create(&ctx));
        OK(quantapdf_open_with_context(ctx, filename.c_str(), nullptr, &doc));
        CHECK(std::remove(filename.c_str()) == 0);
        OK(quantapdf_load_page(doc, 0, &page));
        quantapdf_render_options opt{}; opt.dpi = static_cast<float>(dpi);
        const auto st = quantapdf_render_page_with_options(page, &opt, &bmp);
        if(expected != QUANTAPDF_OK) {
            CHECK(st == expected && bmp == nullptr);
            ++refusals;
        } else {
            CHECK(st == QUANTAPDF_OK && bmp != nullptr);
            int w = 0, h = 0, stride = 0, ch = 0;
            size_t count = 0u;
            const unsigned char* data = nullptr;
            OK(quantapdf_bitmap_dimensions(bmp, &w, &h, &stride, &ch));
            OK(quantapdf_bitmap_data(bmp, &data, &count));
            const int scale = static_cast<int>(dpi / 72u);
            CHECK(w == 12 * scale && h == 4 * scale && stride == w * 3 && ch == 3 &&
                  count == static_cast<size_t>(stride) * static_cast<size_t>(h));
            out_pixels.assign(data, data + count);
            quantapdf_drop_bitmap(bmp);
            ++renders;
        }
        quantapdf_drop_page(page);
        quantapdf_close(doc);
        quantapdf_drop_context(ctx);
        return EXIT_SUCCESS;
    };

    const auto check_pixel = [](const std::vector<unsigned char>& pixels, unsigned dpi,
        size_t pt_x, size_t pt_y, unsigned r, unsigned g, unsigned b, int tol = 0) -> bool {
        const size_t scale = dpi / 72u;
        const size_t stride = 12u * scale * 3u;
        const size_t px = pt_x * scale;
        const size_t py = pt_y * scale;
        const unsigned char* p = pixels.data() + py * stride + px * 3u;
        return std::abs(static_cast<int>(p[0]) - static_cast<int>(r)) <= tol &&
               std::abs(static_cast<int>(p[1]) - static_cast<int>(g)) <= tol &&
               std::abs(static_cast<int>(p[2]) - static_cast<int>(b)) <= tol;
    };

    // 1. Isolated (/I true) vs Non-Isolated (/I false and omitted /I) over red backdrop
    //    with Multiply and Difference children + outer /ca 0.5 at 72 & 144 DPI.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf(
            "1 0 0 rg 0 0 12 4 re f "
            "q /Half gs /FmIso Do Q "
            "q /Half gs 1 0 0 1 4 0 cm /FmNonIso Do Q "
            "q 1 0 0 1 8 0 cm /FmNonIsoDiff Do Q",
            resources);
        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmIso", make_form(*pdf,
            "/Multiply gs 0 0 1 rg 0 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] /Group << /Type /Group /S /Transparency /I true /K false >> >>"));
        xobjs.replaceKey("/FmNonIso", make_form(*pdf,
            "/Multiply gs 0 0 1 rg 0 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /I false /K false >> >>"));
        xobjs.replaceKey("/FmNonIsoDiff", make_form(*pdf,
            "/Difference gs 1 1 0 rg 0 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency >> >>"));
        resources.replaceKey("/XObject", xobjs);
        const auto bytes = serialize_pdf(*pdf);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            // Isolated: Blue Multiply transparent -> Blue, then ca=0.5 over Red -> (128, 0, 128)
            CHECK(check_pixel(pixels, dpi, 1u, 2u, 128u, 0u, 128u, 1));
            // Non-Isolated: Blue Multiply Red -> Black, then ca=0.5 over Red -> (128, 0, 0)
            CHECK(check_pixel(pixels, dpi, 5u, 2u, 128u, 0u, 0u, 1));
            // Default Non-Isolated: Yellow Difference Red -> Green (0, 255, 0)
            CHECK(check_pixel(pixels, dpi, 9u, 2u, 0u, 255u, 0u));
        }
    }

    // 2. Knockout (/K true) vs Non-Knockout (/K false) for both Isolated and Non-Isolated,
    //    plus ca=0 knockout hole exposing the backdrop.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf(
            "0 0 1 rg 0 0 12 4 re f "
            "q /FmIsoKO Do Q "
            "q 1 0 0 1 3 0 cm /FmIsoNoKO Do Q "
            "q 1 0 0 1 6 0 cm /FmNonIsoKO Do Q "
            "q 1 0 0 1 9 0 cm /FmHoleKO Do Q",
            resources);
        auto xobjs = Object::newDictionary();
        const std::string two_children =
            "1 0 0 rg 0 0 3 4 re f "
            "/Half gs 0 1 0 rg 0 0 3 4 re f";
        xobjs.replaceKey("/FmIsoKO", make_form(*pdf, two_children,
            "<< /Subtype /Form /BBox [0 0 3 4] /Group << /S /Transparency /I true /K true >> >>"));
        xobjs.replaceKey("/FmIsoNoKO", make_form(*pdf, two_children,
            "<< /Subtype /Form /BBox [0 0 3 4] /Group << /S /Transparency /I true /K false >> >>"));
        xobjs.replaceKey("/FmNonIsoKO", make_form(*pdf, two_children,
            "<< /Subtype /Form /BBox [0 0 3 4] /Group << /S /Transparency /I false /K true >> >>"));
        xobjs.replaceKey("/FmHoleKO", make_form(*pdf,
            "1 0 0 rg 0 0 3 4 re f /Zero gs 0 1 0 rg 0 0 3 4 re f",
            "<< /Subtype /Form /BBox [0 0 3 4] /Group << /S /Transparency /I true /K true >> >>"));
        resources.replaceKey("/XObject", xobjs);
        const auto bytes = serialize_pdf(*pdf);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            // Isolated Knockout: Green 0.5 knocks out Red -> (0, 128, 128) over Blue
            CHECK(check_pixel(pixels, dpi, 1u, 2u, 0u, 128u, 128u, 1));
            // Isolated Non-Knockout: Green 0.5 over Red -> (128, 128, 0)
            CHECK(check_pixel(pixels, dpi, 4u, 2u, 128u, 128u, 0u, 1));
            // Non-Isolated Knockout: Green 0.5 knocks out Red back to Blue -> (0, 128, 128)
            CHECK(check_pixel(pixels, dpi, 7u, 2u, 0u, 128u, 128u, 1));
            // Isolated Knockout with ca=0 hole: knocks out Red -> Blue (0, 0, 255)
            CHECK(check_pixel(pixels, dpi, 10u, 2u, 0u, 0u, 255u));
        }
    }

    // 3. Outer /ca and /BM (separable + non-separable) applied ONCE to overlapping children.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf(
            "0 1 1 rg 6 0 6 4 re f "
            "q /Half gs /FmOverlap Do Q "
            "q /Multiply gs 1 0 0 1 6 0 cm /FmOverlap Do Q",
            resources);
        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmOverlap", make_form(*pdf,
            "1 0 0 rg 0 0 4 4 re f 0 0 1 rg 2 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 6 4] /Group << /S /Transparency /I true >> >>"));
        resources.replaceKey("/XObject", xobjs);
        const auto bytes = serialize_pdf(*pdf);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            // [0,2): Red at 0.5 over White -> (255, 128, 128)
            CHECK(check_pixel(pixels, dpi, 1u, 2u, 255u, 128u, 128u, 1));
            // [2,4): Overlap has pure Blue inside group, applied ONCE at 0.5 over White -> (128, 128, 255)
            CHECK(check_pixel(pixels, dpi, 3u, 2u, 128u, 128u, 255u, 1));
            // [4,6): Blue at 0.5 over White -> (128, 128, 255)
            CHECK(check_pixel(pixels, dpi, 5u, 2u, 128u, 128u, 255u, 1));
            // [6,8): Cyan Multiply Red -> (0, 0, 0)
            CHECK(check_pixel(pixels, dpi, 7u, 2u, 0u, 0u, 0u));
            // [8,10): Overlap pure Blue Multiply Cyan -> (0, 0, 255)
            CHECK(check_pixel(pixels, dpi, 9u, 2u, 0u, 0u, 255u));
        }

        // Non-separable outer /BM /Hue and /Luminosity on transparency group.
        Object res_ns = Object::newNull();
        auto pdf_ns = make_base_pdf(
            "0.2 0.6 0.8 rg 0 0 12 4 re f "
            "q /Hue gs /FmRed Do Q "
            "q /Luminosity gs 1 0 0 1 6 0 cm /FmRed Do Q",
            res_ns);
        auto xobjs_ns = Object::newDictionary();
        xobjs_ns.replaceKey("/FmRed", make_form(*pdf_ns,
            "1 0 0 rg 0 0 6 4 re f",
            "<< /Subtype /Form /BBox [0 0 6 4] /Group << /S /Transparency /I true >> >>"));
        res_ns.replaceKey("/XObject", xobjs_ns);
        Object res_ref = Object::newNull();
        auto pdf_ref = make_base_pdf(
            "0.2 0.6 0.8 rg 0 0 12 4 re f "
            "q /Hue gs 1 0 0 rg 0 0 6 4 re f Q "
            "q /Luminosity gs 1 0 0 rg 6 0 6 4 re f Q",
            res_ref);
        std::vector<unsigned char> actual, expected;
        CHECK(render_bytes(serialize_pdf(*pdf_ns), 72u, actual) == EXIT_SUCCESS);
        CHECK(render_bytes(serialize_pdf(*pdf_ref), 72u, expected) == EXIT_SUCCESS);
        CHECK(actual == expected);
    }

    // 4. Outer /SMask (/S /Alpha and /S /Luminosity with /TR) applied ONCE to group.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf(
            "q /MaskAlpha gs /FmOverlap Do Q "
            "q 1 0 0 1 6 0 cm /MaskLum gs /FmOverlap Do Q",
            resources);
        auto alpha_mask_form = make_form(*pdf,
            "/Half gs 0 0 0 rg 0 0 6 4 re f",
            "<< /Subtype /Form /BBox [0 0 6 4] /Group << /S /Transparency /I true >> >>",
            Object::parse("<< /ExtGState << /Half << /ca .5 >> >> >>"));
        auto lum_mask_form = make_form(*pdf,
            "0.25 g 0 0 6 4 re f",
            "<< /Subtype /Form /BBox [0 0 6 4] /Group << /S /Transparency /I true /CS /DeviceGray >> >>");
        auto invert_tr = Object::parse("<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>");

        auto gs = resources.getKey("/ExtGState");
        auto sm_alpha = Object::newDictionary();
        sm_alpha.replaceKey("/S", Object::newName("/Alpha"));
        sm_alpha.replaceKey("/G", alpha_mask_form);
        auto gs_alpha = Object::newDictionary();
        gs_alpha.replaceKey("/SMask", sm_alpha);
        gs.replaceKey("/MaskAlpha", gs_alpha);

        auto sm_lum = Object::newDictionary();
        sm_lum.replaceKey("/S", Object::newName("/Luminosity"));
        sm_lum.replaceKey("/G", lum_mask_form);
        sm_lum.replaceKey("/TR", invert_tr);
        auto gs_lum = Object::newDictionary();
        gs_lum.replaceKey("/SMask", sm_lum);
        gs.replaceKey("/MaskLum", gs_lum);

        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmOverlap", make_form(*pdf,
            "1 0 0 rg 0 0 4 4 re f 0 0 1 rg 2 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 6 4] /Group << /S /Transparency /I true >> >>"));
        resources.replaceKey("/XObject", xobjs);
        const auto bytes = serialize_pdf(*pdf);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            // Alpha mask 0.5 applied ONCE to overlap pure Blue over White -> (128, 128, 255)
            CHECK(check_pixel(pixels, dpi, 3u, 2u, 128u, 128u, 255u, 1));
            // Luminosity mask (1 - 0.25 = 0.75) applied ONCE to overlap pure Blue over White -> (64, 64, 255)
            CHECK(check_pixel(pixels, dpi, 9u, 2u, 64u, 64u, 255u, 1));
        }
    }

    // 5. /CS /DeviceGray vs /DeviceRGB and omitted /CS inheritance.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf(
            "/FmGrayParent Do "
            "q 1 0 0 1 8 0 cm /FmRgb Do Q",
            resources);
        auto child_inherit = make_form(*pdf,
            "0 1 0 rg 0 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /I true >> >>");
        auto parent_res = Object::newDictionary();
        auto parent_xobjs = Object::newDictionary();
        parent_xobjs.replaceKey("/FmChildInherit", child_inherit);
        parent_res.replaceKey("/XObject", parent_xobjs);
        auto gray_parent = make_form(*pdf,
            "1 0 0 rg 0 0 4 4 re f "
            "q 1 0 0 1 4 0 cm /FmChildInherit Do Q",
            "<< /Subtype /Form /BBox [0 0 8 4] /Group << /S /Transparency /I true /CS /DeviceGray >> >>",
            parent_res);
        auto rgb_group = make_form(*pdf,
            "1 0 0 rg 0 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /I true /CS /DeviceRGB >> >>");
        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmGrayParent", gray_parent);
        xobjs.replaceKey("/FmRgb", rgb_group);
        resources.replaceKey("/XObject", xobjs);
        const auto bytes = serialize_pdf(*pdf);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            // Red (1,0,0) in /DeviceGray -> 0.30 * 255 = 76.5 -> (77, 77, 77)
            CHECK(check_pixel(pixels, dpi, 2u, 2u, 77u, 77u, 77u, 1));
            // Green (0,1,0) in nested child inheriting /DeviceGray -> 0.59 * 255 = 150.45 -> (150, 150, 150)
            CHECK(check_pixel(pixels, dpi, 6u, 2u, 150u, 150u, 150u, 1));
            // Red in /DeviceRGB -> (255, 0, 0)
            CHECK(check_pixel(pixels, dpi, 10u, 2u, 255u, 0u, 0u));
        }
    }

    // 6. Nested Transparency Groups (inner half-alpha & ca=0 knockout hole) +
    //    ordinary Form XObject inside a Transparency Group.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf("0 0 1 rg 0 0 12 4 re f /FmOuter Do", resources);
        auto inner_grp = make_form(*pdf,
            "0 1 0 rg 0 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /I true >> >>");
        auto inner_plain = make_form(*pdf,
            "1 1 0 rg 0 0 4 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] >>");
        auto outer_res = Object::newDictionary();
        outer_res.replaceKey("/ExtGState", resources.getKey("/ExtGState"));
        auto outer_xobjs = Object::newDictionary();
        outer_xobjs.replaceKey("/FmInner", inner_grp);
        outer_xobjs.replaceKey("/FmPlain", inner_plain);
        outer_res.replaceKey("/XObject", outer_xobjs);
        auto outer_grp = make_form(*pdf,
            "1 0 0 rg 0 0 12 4 re f "
            "q /Half gs /FmInner Do Q "
            "q /Zero gs 1 0 0 1 4 0 cm /FmInner Do Q "
            "q 1 0 0 1 8 0 cm /FmPlain Do Q",
            "<< /Subtype /Form /BBox [0 0 12 4] /Group << /S /Transparency /I true /K true >> >>",
            outer_res);
        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmOuter", outer_grp);
        resources.replaceKey("/XObject", xobjs);
        const auto bytes = serialize_pdf(*pdf);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            // [0,4): Inner Green at ca=0.5 knocks out Red inside Isolated KO -> (0, 128, 128) over Blue
            CHECK(check_pixel(pixels, dpi, 2u, 2u, 0u, 128u, 128u, 1));
            // [4,8): Inner Green at ca=0 knocks out Red -> Blue backdrop shows through (0, 0, 255)
            CHECK(check_pixel(pixels, dpi, 6u, 2u, 0u, 0u, 255u));
            // [8,12): Ordinary Form paints Yellow -> (255, 255, 0)
            CHECK(check_pixel(pixels, dpi, 10u, 2u, 255u, 255u, 0u));
        }
    }

    // 7. Mixed content inside Transparency Group: RAW RGB Image + Path + BT..ET TrueType Text.
    {
        Object res_grp = Object::newNull();
        auto pdf_grp = make_base_pdf("0 0 1 rg 0 0 12 4 re f q /Half gs /FmMixed Do Q", res_grp);
        auto fm_res = Object::newDictionary();
        fm_res.replaceKey("/Font", res_grp.getKey("/Font"));
        auto fm_xobjs = Object::newDictionary();
        fm_xobjs.replaceKey("/ImgRed", make_rgb_image(*pdf_grp, 255u, 0u, 0u));
        fm_res.replaceKey("/XObject", fm_xobjs);
        auto fm_mixed = make_form(*pdf_grp,
            "q 4 0 0 4 0 0 cm /ImgRed Do Q "
            "0 1 0 rg 4 0 4 4 re f "
            "BT /F 4 Tf 1 0 0 1 9 1 Tm 1 0 0 rg <002d> Tj 1 1 0 rg 1 0 0 1 9 1 Tm <002d> Tj ET",
            "<< /Subtype /Form /BBox [0 0 12 4] /Group << /S /Transparency /I true >> >>",
            fm_res);
        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmMixed", fm_mixed);
        res_grp.replaceKey("/XObject", xobjs);

        Object res_ref = Object::newNull();
        auto pdf_ref = make_base_pdf(
            "0 0 1 rg 0 0 12 4 re f "
            "q /Half gs 1 0 0 rg 0 0 4 4 re f 0 1 0 rg 4 0 4 4 re f "
            "BT /F 4 Tf 1 0 0 1 9 1 Tm 1 1 0 rg <002d> Tj ET Q",
            res_ref);
        const auto grp_bytes = serialize_pdf(*pdf_grp);
        const auto ref_bytes = serialize_pdf(*pdf_ref);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> actual, expected;
            CHECK(render_bytes(grp_bytes, dpi, actual) == EXIT_SUCCESS);
            CHECK(render_bytes(ref_bytes, dpi, expected) == EXIT_SUCCESS);
            CHECK(check_pixel(actual, dpi, 2u, 2u, 128u, 0u, 128u, 1));
            CHECK(check_pixel(actual, dpi, 6u, 2u, 0u, 128u, 128u, 1));
            CHECK(actual.size() == expected.size());
            for(size_t i = 0u; i < actual.size(); ++i)
                CHECK(std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i])) <= 1);
            bool saw_glyph = false;
            const size_t scale = dpi / 72u;
            const size_t stride = 12u * scale * 3u;
            for(size_t py = 0u; py < 4u * scale; ++py) {
                for(size_t px = 8u * scale; px < 12u * scale; ++px) {
                    const unsigned char* p = actual.data() + py * stride + px * 3u;
                    if(p[0] > 0u || p[1] > 0u) saw_glyph = true;
                }
            }
            CHECK(saw_glyph);
        }
    }

    // 8. Composer round-trip: quantapdf_composer_add_form with
    //    QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP | ISOLATED | KNOCKOUT
    //    and non-knockout variant, write PDF, reopen, render at 72 & 144 DPI.
    {
        quantapdf_composer* composer = nullptr;
        OK(quantapdf_composer_create(nullptr, &composer));
        const quantapdf_composer_page_options page_opt{sizeof(page_opt), 12.0f, 4.0f, UINT32_C(0xffffffff)};
        size_t page_index = SIZE_MAX;
        OK(quantapdf_composer_add_page(composer, &page_opt, &page_index));
        CHECK(page_index == 0u);

        quantapdf_composer_form_options ko_opt{};
        ko_opt.struct_size = sizeof(ko_opt);
        ko_opt.width_points = 4.0f;
        ko_opt.height_points = 4.0f;
        ko_opt.flags = QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
                       QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED |
                       QUANTAPDF_COMPOSER_FORM_FLAG_KNOCKOUT;
        quantapdf_composer_form_id ko_form = 0u;
        OK(quantapdf_composer_add_form(composer, &ko_opt, composer_group_builder, nullptr, &ko_form));

        quantapdf_composer_form_options noko_opt = ko_opt;
        noko_opt.flags = QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
                         QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;
        quantapdf_composer_form_id noko_form = 0u;
        OK(quantapdf_composer_add_form(composer, &noko_opt, composer_group_builder, nullptr, &noko_form));

        std::array<quantapdf_composer_path_command, 5> backdrop{};
        backdrop[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO; backdrop[0].point1 = {0.0f, 0.0f};
        backdrop[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; backdrop[1].point1 = {12.0f, 0.0f};
        backdrop[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; backdrop[2].point1 = {12.0f, 4.0f};
        backdrop[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; backdrop[3].point1 = {0.0f, 4.0f};
        backdrop[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
        quantapdf_composer_path_options bg{};
        bg.fill = 1;
        bg.fill_argb = UINT32_C(0xff0000ff);
        bg.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
        OK(quantapdf_composer_draw_path(composer, 0u, backdrop.data(), backdrop.size(), &bg));

        const quantapdf_affine_transform xform_ko{1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f};
        quantapdf_composer_form_draw_options draw_opt{sizeof(draw_opt), 0u};
        OK(quantapdf_composer_draw_form(composer, 0u, ko_form, &xform_ko, &draw_opt));

        const quantapdf_affine_transform xform_noko{1.0f, 0.0f, 0.0f, 1.0f, 7.0f, 0.0f};
        OK(quantapdf_composer_draw_form(composer, 0u, noko_form, &xform_noko, &draw_opt));

        quantapdf_output* output = nullptr;
        const unsigned char* data = nullptr;
        size_t size = 0u;
        OK(quantapdf_composer_finish(composer, &output));
        OK(quantapdf_output_data(output, &data, &size));
        const std::string bytes(reinterpret_cast<const char*>(data), size);
        quantapdf_drop_output(output);
        quantapdf_drop_composer(composer);

        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            // Outside forms: Blue backdrop
            CHECK(check_pixel(pixels, dpi, 0u, 2u, 0u, 0u, 255u));
            // Isolated Knockout form: Green 0.5 knocks out Red -> (0, 128, 128) over Blue
            CHECK(check_pixel(pixels, dpi, 2u, 2u, 0u, 128u, 128u, 1));
            // Isolated Non-Knockout form: Green 0.5 over Red -> (128, 128, 0)
            CHECK(check_pixel(pixels, dpi, 8u, 2u, 128u, 128u, 0u, 1));
        }
    }

    // 9. Strict negative validation & atomic page refusal matrix.
    {
        std::vector<unsigned char> unused;
        struct bad_group_case {
            const char* page_content;
            const char* form_content;
            const char* dict_spec;
            quantapdf_status expected;
        };
        const bad_group_case cases[] = {
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group 123 >>", QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Other >> >>", QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S 42 >> >>", QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /Type /Other /S /Transparency >> >>", QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /Type 42 /S /Transparency >> >>", QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /I /true >> >>", QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /K 1 >> >>", QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /CS /DeviceCMYK >> >>", QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /CS [/ICCBased] >> >>", QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /CS 42 >> >>", QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /Extra 1 >> >>", QUANTAPDF_ERROR_UNSUPPORTED},
            // Atomic refusal: valid prior page paint must not survive invalid op inside group.
            {"1 0 0 rg 0 0 4 4 re f /Fm Do", "0 0 2 2 re S", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /I true >> >>", QUANTAPDF_ERROR_UNSUPPORTED},
        };
        for(const auto& c : cases) {
            Object resources = Object::newNull();
            auto pdf = make_base_pdf(c.page_content, resources);
            auto xobjs = Object::newDictionary();
            xobjs.replaceKey("/Fm", make_form(*pdf, c.form_content, c.dict_spec));
            resources.replaceKey("/XObject", xobjs);
            CHECK(render_bytes(serialize_pdf(*pdf), 72u, unused, c.expected) == EXIT_SUCCESS);
        }

        // Direct self-cycle inside transparency group.
        {
            Object resources = Object::newNull();
            auto pdf = make_base_pdf("/FmA Do", resources);
            auto form = make_form(*pdf, "/FmA Do",
                "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency /I true >> >>");
            auto xobjs = Object::newDictionary();
            xobjs.replaceKey("/FmA", form);
            resources.replaceKey("/XObject", xobjs);
            CHECK(render_bytes(serialize_pdf(*pdf), 72u, unused, QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
        }
    }

    std::printf("[native-transparency-group] %zu renders and %zu refusals passed\n", renders, refusals);
    return EXIT_SUCCESS;
}

#endif
