#ifndef QUANTAPDF_NATIVE_FORM_XOBJECT_CASES_H
#define QUANTAPDF_NATIVE_FORM_XOBJECT_CASES_H
#include "native_graphics_state_fixture.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace native_form_xobject {
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
    pdf->processMemoryFile("form-xobject fixture", font_fixture.data(), font_fixture.size());
    auto page = pdf->getAllPages().front();
    page.replaceKey("/MediaBox", Object::parse("[0 0 12 4]"));
    page.replaceKey("/Contents", pdf->newStream(page_content));
    out_resources = page.getKey("/Resources");
    auto states = Object::parse(
        "<< /Opaque << /ca 1 >> /Half << /ca .5 >> /Multiply << /BM /Multiply >> "
        "/Knockout << /TK true >> /Separate << /TK false >> >>");
    out_resources.replaceKey("/ExtGState", states);
    return pdf;
}

inline quantapdf_status composer_overhang_form_builder(quantapdf_composer* composer, size_t page, void*)
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
    quantapdf_composer_path_options paint{};
    paint.fill = 1;
    paint.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    paint.fill_argb = UINT32_C(0xff0000ff);
    const auto left = make_rect(0.0f, 0.0f, 2.0f, 4.0f);
    auto status = quantapdf_composer_draw_path(composer, page, left.data(), left.size(), &paint);
    if(status != QUANTAPDF_OK)return status;
    paint.fill_argb = UINT32_C(0xffff0000);
    // Intentionally extends to x=6 beyond the 4x4 form BBox to verify BBox clipping.
    const auto right = make_rect(2.0f, 0.0f, 6.0f, 4.0f);
    return quantapdf_composer_draw_path(composer, page, right.data(), right.size(), &paint);
}
}

static int form_xobject_cases(const std::string& filename)
{
    using namespace native_form_xobject;
    size_t renders = 0u, refusals = 0u;

    const auto render_bytes = [&](const std::string& bytes, unsigned dpi,
                                  std::vector<unsigned char>& pixels,
                                  quantapdf_status wanted = QUANTAPDF_OK) -> int {
        CHECK(write_pdf(filename, bytes));
        quantapdf_document* document = nullptr;
        quantapdf_page* page = nullptr;
        OK(quantapdf_open(filename.c_str(), nullptr, &document));
        CHECK(std::remove(filename.c_str()) == 0);
        OK(quantapdf_load_page(document, 0, &page));
        quantapdf_close(document);
        quantapdf_bitmap* bitmap = nullptr;
        quantapdf_render_options options{};
        options.dpi = static_cast<float>(dpi);
        const auto status = quantapdf_render_page_with_options(page, &options, &bitmap);
        quantapdf_drop_page(page);
        if(status != wanted)
            std::fprintf(stderr, "Form XObject: render status %d, expected %d\n",
                int(status), int(wanted));
        CHECK(status == wanted);
        if(wanted != QUANTAPDF_OK) {
            CHECK(bitmap == nullptr);
            ++refusals;
            return EXIT_SUCCESS;
        }
        int w = 0, h = 0, stride = 0, channels = 0;
        const unsigned char* data = nullptr;
        size_t size = 0u;
        OK(quantapdf_bitmap_dimensions(bitmap, &w, &h, &stride, &channels));
        OK(quantapdf_bitmap_data(bitmap, &data, &size));
        CHECK(w == int(12u * dpi / 72u) && h == int(4u * dpi / 72u) &&
              stride == w * 3 && channels == 3 &&
              size == static_cast<size_t>(h * stride));
        pixels.assign(data, data + size);
        quantapdf_drop_bitmap(bitmap);
        ++renders;
        return EXIT_SUCCESS;
    };

    const auto check_pixel = [&](const std::vector<unsigned char>& pixels, unsigned dpi,
                                 size_t pt_x, size_t pt_y_top,
                                 unsigned r, unsigned g, unsigned b, int tol = 0) -> bool {
        const size_t scale = dpi / 72u;
        const size_t w = 12u * scale;
        const size_t px = pt_x * scale + scale / 2u;
        const size_t py = pt_y_top * scale + scale / 2u;
        const size_t idx = (py * w + px) * 3u;
        return std::abs(int(pixels[idx]) - int(r)) <= tol &&
               std::abs(int(pixels[idx + 1u]) - int(g)) <= tol &&
               std::abs(int(pixels[idx + 2u]) - int(b)) <= tol;
    };

    // 1. Basic Form XObject /Matrix placement, /BBox clipping, and q/Q state isolation.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf("0 0 1 rg 0 0 2 4 re f /Fm1 Do 6 0 4 4 re f", resources);
        auto xobjs = Object::newDictionary();
        // Form mutates color, alpha, clip, and CTM without q/Q; none may leak to page.
        xobjs.replaceKey("/Fm1", make_form(*pdf,
            "1 0 0 rg -2 0 8 4 re f /Half gs 0 0 2 4 re W n 2 0 0 2 5 0 cm",
            "<< /Type /XObject /Subtype /Form /FormType 1 /BBox [0 0 4 4] /Matrix [1 0 0 1 2 0] >>"));
        resources.replaceKey("/XObject", xobjs);
        const auto bytes = serialize_pdf(*pdf);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            CHECK(check_pixel(pixels, dpi, 1u, 2u, 0u, 0u, 255u));
            CHECK(check_pixel(pixels, dpi, 2u, 2u, 255u, 0u, 0u));
            CHECK(check_pixel(pixels, dpi, 5u, 2u, 255u, 0u, 0u));
            CHECK(check_pixel(pixels, dpi, 6u, 2u, 0u, 0u, 255u));
            CHECK(check_pixel(pixels, dpi, 9u, 2u, 0u, 0u, 255u));
            CHECK(check_pixel(pixels, dpi, 11u, 2u, 255u, 255u, 255u));
        }
    }

    // 2. Repeated shared Form with inherited color/alpha/blend + mirrored /Matrix [-1 0 0 1 4 0].
    {
        Object res_form = Object::newNull();
        auto pdf_form = make_base_pdf(
            "1 0 0 rg 0 0 12 4 re f "
            "q 0 0 1 rg /Half gs 1 0 0 1 0 0 cm /FmShared Do Q "
            "q 0 1 0 rg /Multiply gs 1 0 0 1 4 0 cm /FmShared Do Q "
            "q 1 0 0 1 8 0 cm /FmMirror Do Q", res_form);
        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmShared", make_form(*pdf_form,
            "0 0 2 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] >>"));
        xobjs.replaceKey("/FmMirror", make_form(*pdf_form,
            "0 1 0 rg 0 0 1 4 re f",
            "<< /Subtype /Form /BBox [0 0 4 4] /Matrix [-1 0 0 1 4 0] >>"));
        res_form.replaceKey("/XObject", xobjs);

        Object res_ref = Object::newNull();
        auto pdf_ref = make_base_pdf(
            "1 0 0 rg 0 0 12 4 re f "
            "q 0 0 1 rg /Half gs 0 0 2 4 re f Q "
            "q 0 1 0 rg /Multiply gs 4 0 2 4 re f Q "
            "q 0 1 0 rg 11 0 1 4 re f Q", res_ref);
        const auto form_bytes = serialize_pdf(*pdf_form);
        const auto ref_bytes = serialize_pdf(*pdf_ref);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> actual, expected;
            CHECK(render_bytes(form_bytes, dpi, actual) == EXIT_SUCCESS);
            CHECK(render_bytes(ref_bytes, dpi, expected) == EXIT_SUCCESS);
            CHECK(actual == expected);
            CHECK(check_pixel(actual, dpi, 1u, 2u, 128u, 0u, 128u, 1));
            CHECK(check_pixel(actual, dpi, 5u, 2u, 0u, 0u, 0u));
            CHECK(check_pixel(actual, dpi, 9u, 2u, 255u, 0u, 0u));
            CHECK(check_pixel(actual, dpi, 11u, 2u, 0u, 255u, 0u));
        }
    }

    // 3. Form-local /Resources override vs omitted /Resources inheritance (ExtGState + Image).
    {
        Object res_form = Object::newNull();
        auto pdf_form = make_base_pdf(
            "/FmOwn Do q 1 0 0 1 4 0 cm /FmInherit Do Q /G gs q 4 0 0 4 8 0 cm /Img Do Q", res_form);
        res_form.getKey("/ExtGState").replaceKey("/G", Object::parse("<< /ca 1 >>"));
        auto page_xobjs = Object::newDictionary();
        page_xobjs.replaceKey("/Img", make_rgb_image(*pdf_form, 255u, 0u, 0u));

        auto own_res = Object::newDictionary();
        own_res.replaceKey("/ExtGState", Object::parse("<< /G << /ca .5 >> >>"));
        auto own_xobjs = Object::newDictionary();
        own_xobjs.replaceKey("/Img", make_rgb_image(*pdf_form, 0u, 0u, 255u));
        own_res.replaceKey("/XObject", own_xobjs);

        page_xobjs.replaceKey("/FmOwn", make_form(*pdf_form,
            "/G gs q 4 0 0 4 0 0 cm /Img Do Q",
            "<< /Subtype /Form /BBox [0 0 4 4] >>", own_res));
        page_xobjs.replaceKey("/FmInherit", make_form(*pdf_form,
            "/G gs q 4 0 0 4 0 0 cm /Img Do Q",
            "<< /Subtype /Form /BBox [0 0 4 4] >>"));
        res_form.replaceKey("/XObject", page_xobjs);

        const auto bytes = serialize_pdf(*pdf_form);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            // [0,4): Form-local blue at 0.5 alpha over white -> (128,128,255)
            CHECK(check_pixel(pixels, dpi, 2u, 2u, 128u, 128u, 255u, 1));
            // [4,8): Inherited opaque red -> (255,0,0)
            CHECK(check_pixel(pixels, dpi, 6u, 2u, 255u, 0u, 0u));
            // [8,12): Page resources restored -> opaque red (255,0,0)
            CHECK(check_pixel(pixels, dpi, 10u, 2u, 255u, 0u, 0u));
        }
    }

    // 4. Nested Form XObjects (FmOuter -> FmInner) with compounded /Matrix + intersected /BBox + caller clip.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf("q 3 0 3 4 re W n /FmOuter Do Q", resources);
        auto inner = make_form(*pdf,
            "0 0 1 rg -4 0 12 4 re f",
            "<< /Subtype /Form /BBox [0 0 6 4] /Matrix [1 0 0 1 2 0] >>");
        auto outer_res = Object::newDictionary();
        auto outer_xobjs = Object::newDictionary();
        outer_xobjs.replaceKey("/FmInner", inner);
        outer_res.replaceKey("/XObject", outer_xobjs);
        auto outer = make_form(*pdf,
            "1 0 0 rg 0 0 6 4 re f /FmInner Do",
            "<< /Subtype /Form /BBox [0 0 5 4] /Matrix [1 0 0 1 2 0] >>", outer_res);
        auto page_xobjs = Object::newDictionary();
        page_xobjs.replaceKey("/FmOuter", outer);
        resources.replaceKey("/XObject", page_xobjs);

        const auto bytes = serialize_pdf(*pdf);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> pixels;
            CHECK(render_bytes(bytes, dpi, pixels) == EXIT_SUCCESS);
            CHECK(check_pixel(pixels, dpi, 2u, 2u, 255u, 255u, 255u));
            CHECK(check_pixel(pixels, dpi, 3u, 2u, 255u, 0u, 0u));
            CHECK(check_pixel(pixels, dpi, 4u, 2u, 0u, 0u, 255u));
            CHECK(check_pixel(pixels, dpi, 5u, 2u, 0u, 0u, 255u));
            CHECK(check_pixel(pixels, dpi, 6u, 2u, 255u, 255u, 255u));
        }
    }

    // 5. Mixed Form XObjects with BT..ET knockout text before, between, and after outer page BT..ET,
    //    plus inherited /TK state when Do is executed inside outline_reader.
    {
        Object res_form = Object::newNull();
        auto pdf_form = make_base_pdf(
            "/FmBefore Do "
            "q /Half gs BT /F 4 Tf 1 0 0 1 5 1 Tm 0 1 0 rg <002d> Tj 1 0 0 rg 1 0 0 1 5 1 Tm <002d> Tj ET Q "
            "/FmAfter Do "
            "BT /F 4 Tf 1 0 0 1 10 1 Tm 0 0 0 rg <002d> Tj ET", res_form);
        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmBefore", make_form(*pdf_form,
            "/Half gs BT /F 4 Tf 1 0 0 1 1 1 Tm 1 0 0 rg <002d> Tj 0 0 1 rg 1 0 0 1 1 1 Tm <002d> Tj ET",
            "<< /Subtype /Form /BBox [0 0 4 4] >>"));
        xobjs.replaceKey("/FmAfter", make_form(*pdf_form,
            "/Half gs BT /F 4 Tf 1 0 0 1 1 1 Tm 0 0 1 rg <002d> Tj 0 1 0 rg 1 0 0 1 1 1 Tm <002d> Tj ET",
            "<< /Subtype /Form /BBox [0 0 4 4] /Matrix [1 0 0 1 8 0] >>"));
        res_form.replaceKey("/XObject", xobjs);

        Object res_ref = Object::newNull();
        auto pdf_ref = make_base_pdf(
            "q 0 0 4 4 re W n /Half gs BT /F 4 Tf 1 0 0 1 1 1 Tm 1 0 0 rg <002d> Tj 0 0 1 rg 1 0 0 1 1 1 Tm <002d> Tj ET Q "
            "q /Half gs BT /F 4 Tf 1 0 0 1 5 1 Tm 0 1 0 rg <002d> Tj 1 0 0 rg 1 0 0 1 5 1 Tm <002d> Tj ET Q "
            "q 1 0 0 1 8 0 cm 0 0 4 4 re W n /Half gs BT /F 4 Tf 1 0 0 1 1 1 Tm 0 0 1 rg <002d> Tj 0 1 0 rg 1 0 0 1 1 1 Tm <002d> Tj ET Q "
            "BT /F 4 Tf 1 0 0 1 10 1 Tm 0 0 0 rg <002d> Tj ET", res_ref);
        const auto form_bytes = serialize_pdf(*pdf_form);
        const auto ref_bytes = serialize_pdf(*pdf_ref);
        for(unsigned dpi : {72u, 144u}) {
            std::vector<unsigned char> actual, expected;
            CHECK(render_bytes(form_bytes, dpi, actual) == EXIT_SUCCESS);
            CHECK(render_bytes(ref_bytes, dpi, expected) == EXIT_SUCCESS);
            CHECK(actual == expected);
        }

        // Verify /TK inheritance from outline_reader into Form XObject.
        std::vector<unsigned char> tk_true_pixels, tk_false_pixels;
        for(bool separate : {false, true}) {
            const std::string gs = separate ? "/Separate gs " : "/Knockout gs ";
            Object res_a = Object::newNull(), res_b = Object::newNull();
            auto pdf_a = make_base_pdf(
                "BT /F 4 Tf 1 0 0 1 1 1 Tm 0 g <002d> Tj ET " + gs + "/FmTK Do", res_a);
            auto xo = Object::newDictionary();
            xo.replaceKey("/FmTK", make_form(*pdf_a,
                "/Half gs BT /F 4 Tf 1 0 0 1 1 1 Tm 1 0 0 rg <002d> Tj 0 0 1 rg 1 0 0 1 1 1 Tm <002d> Tj ET",
                "<< /Subtype /Form /BBox [0 0 4 4] /Matrix [1 0 0 1 4 0] >>"));
            res_a.replaceKey("/XObject", xo);

            auto pdf_b = make_base_pdf(
                "BT /F 4 Tf 1 0 0 1 1 1 Tm 0 g <002d> Tj ET "
                "q " + gs + "1 0 0 1 4 0 cm 0 0 4 4 re W n "
                "/Half gs BT /F 4 Tf 1 0 0 1 1 1 Tm 1 0 0 rg <002d> Tj 0 0 1 rg 1 0 0 1 1 1 Tm <002d> Tj ET Q", res_b);
            std::vector<unsigned char> got, want;
            CHECK(render_bytes(serialize_pdf(*pdf_a), 144u, got) == EXIT_SUCCESS);
            CHECK(render_bytes(serialize_pdf(*pdf_b), 144u, want) == EXIT_SUCCESS);
            CHECK(got == want);
            if(separate)tk_false_pixels = std::move(got);
            else tk_true_pixels = std::move(got);
        }
        CHECK(tk_true_pixels != tk_false_pixels);
    }

    // 6. Zero-area degenerate /BBox clips cleanly to white without error.
    {
        Object resources = Object::newNull();
        auto pdf = make_base_pdf("/FmEmpty Do", resources);
        auto xobjs = Object::newDictionary();
        xobjs.replaceKey("/FmEmpty", make_form(*pdf,
            "1 0 0 rg 0 0 12 4 re f",
            "<< /Subtype /Form /BBox [2 0 2 4] >>"));
        resources.replaceKey("/XObject", xobjs);
        const auto bytes = serialize_pdf(*pdf);
        std::vector<unsigned char> pixels;
        CHECK(render_bytes(bytes, 72u, pixels) == EXIT_SUCCESS);
        for(unsigned char byte : pixels)CHECK(byte == 255u);
    }

    // 7. Composer quantapdf_composer_add_form (flags=0) + quantapdf_composer_draw_form round-trip.
    {
        quantapdf_composer* composer = nullptr;
        OK(quantapdf_composer_create(nullptr, &composer));
        const quantapdf_composer_page_options page_opt{sizeof(page_opt), 12.0f, 4.0f, UINT32_C(0xffffffff)};
        size_t page_index = SIZE_MAX;
        OK(quantapdf_composer_add_page(composer, &page_opt, &page_index));
        CHECK(page_index == 0u);

        quantapdf_composer_form_options form_opt{};
        form_opt.struct_size = sizeof(form_opt);
        form_opt.width_points = 4.0f;
        form_opt.height_points = 4.0f;
        form_opt.flags = 0u;
        quantapdf_composer_form_id form_id = 0u;
        OK(quantapdf_composer_add_form(composer, &form_opt, composer_overhang_form_builder, nullptr, &form_id));
        CHECK(form_id != 0u);

        // Paint green backdrop at [7, 11) for translucent second placement.
        std::array<quantapdf_composer_path_command, 5> backdrop{};
        backdrop[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO; backdrop[0].point1 = {7.0f, 0.0f};
        backdrop[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; backdrop[1].point1 = {11.0f, 0.0f};
        backdrop[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; backdrop[2].point1 = {11.0f, 4.0f};
        backdrop[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; backdrop[3].point1 = {7.0f, 4.0f};
        backdrop[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
        quantapdf_composer_path_options bg_paint{};
        bg_paint.fill = 1;
        bg_paint.fill_argb = UINT32_C(0xff00ff00);
        bg_paint.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
        OK(quantapdf_composer_draw_path(composer, 0u, backdrop.data(), backdrop.size(), &bg_paint));

        const quantapdf_composer_graphics_state_options half_state{
            0.5f, 1.0f, QUANTAPDF_COMPOSER_BLEND_NORMAL, 0u, 0u,
            QUANTAPDF_COMPOSER_TEXT_KNOCKOUT_INHERIT};
        quantapdf_composer_graphics_state_id half_gs = 0u;
        OK(quantapdf_composer_add_graphics_state(composer, &half_state, &half_gs));

        const quantapdf_affine_transform first_xform{1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f};
        quantapdf_composer_form_draw_options draw_opt{sizeof(draw_opt), 0u};
        OK(quantapdf_composer_draw_form(composer, 0u, form_id, &first_xform, &draw_opt));

        const quantapdf_affine_transform second_xform{1.0f, 0.0f, 0.0f, 1.0f, 7.0f, 0.0f};
        draw_opt.graphics_state_id = half_gs;
        OK(quantapdf_composer_draw_form(composer, 0u, form_id, &second_xform, &draw_opt));

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
            CHECK(check_pixel(pixels, dpi, 0u, 2u, 255u, 255u, 255u));
            CHECK(check_pixel(pixels, dpi, 1u, 2u, 0u, 0u, 255u));
            CHECK(check_pixel(pixels, dpi, 3u, 2u, 255u, 0u, 0u));
            // Overhanging [5,7) must be clipped by the 4x4 Form BBox.
            CHECK(check_pixel(pixels, dpi, 5u, 2u, 255u, 255u, 255u));
            // Second placement inherits 0.5 fill alpha over green backdrop.
            CHECK(check_pixel(pixels, dpi, 7u, 2u, 0u, 128u, 128u, 1));
            CHECK(check_pixel(pixels, dpi, 9u, 2u, 128u, 128u, 0u, 1));
            CHECK(check_pixel(pixels, dpi, 11u, 2u, 255u, 255u, 255u));
        }
    }

    // 8. Atomic refusals and security bounds.
    {
        std::vector<unsigned char> unused;

        // Direct cycle: FmA -> FmA
        {
            Object resources = Object::newNull();
            auto pdf = make_base_pdf("/FmA Do", resources);
            auto form = make_form(*pdf, "/FmA Do", "<< /Subtype /Form /BBox [0 0 4 4] >>");
            auto xobjs = Object::newDictionary();
            xobjs.replaceKey("/FmA", form);
            resources.replaceKey("/XObject", xobjs);
            CHECK(render_bytes(serialize_pdf(*pdf), 72u, unused, QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
        }

        // Indirect cycle: FmA -> FmB -> FmA
        {
            Object resources = Object::newNull();
            auto pdf = make_base_pdf("/FmA Do", resources);
            auto fa = make_form(*pdf, "/FmB Do", "<< /Subtype /Form /BBox [0 0 4 4] >>");
            auto fb = make_form(*pdf, "/FmA Do", "<< /Subtype /Form /BBox [0 0 4 4] >>");
            auto xobjs = Object::newDictionary();
            xobjs.replaceKey("/FmA", fa);
            xobjs.replaceKey("/FmB", fb);
            resources.replaceKey("/XObject", xobjs);
            CHECK(render_bytes(serialize_pdf(*pdf), 72u, unused, QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
        }

        // Excessive nesting depth (> 32)
        {
            Object resources = Object::newNull();
            auto pdf = make_base_pdf("/Fm0 Do", resources);
            auto xobjs = Object::newDictionary();
            for(unsigned i = 0u; i <= 32u; ++i) {
                const std::string body = (i < 32u) ? ("/Fm" + std::to_string(i + 1u) + " Do") : "0 0 1 1 re f";
                xobjs.replaceKey("/Fm" + std::to_string(i),
                    make_form(*pdf, body, "<< /Subtype /Form /BBox [0 0 4 4] >>"));
            }
            resources.replaceKey("/XObject", xobjs);
            CHECK(render_bytes(serialize_pdf(*pdf), 72u, unused, QUANTAPDF_ERROR_UNSUPPORTED) == EXIT_SUCCESS);
        }

        // Dictionary validation refusals
        struct bad_form_case {
            const char* page_content;
            const char* form_content;
            const char* dict_spec;
            const char* res_spec;
            quantapdf_status expected;
        };
        const bad_form_case cases[] = {
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Group << /S /Transparency >> >>", nullptr, QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form >>", nullptr, QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Matrix [1 0 0 1 0] >>", nullptr, QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /FormType 2 >>", nullptr, QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /OC << >> >>", nullptr, QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] /Ref << >> >>", nullptr, QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] >>", "<< /ColorSpace << /DefaultRGB /DeviceRGB >> >>", QUANTAPDF_ERROR_UNSUPPORTED},
            {"q /Fm Do Q", "Q", "<< /Subtype /Form /BBox [0 0 4 4] >>", nullptr, QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "q 0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] >>", nullptr, QUANTAPDF_ERROR_FORMAT},
            {"/Fm Do", "0 0 2 2 re S", "<< /Subtype /Form /BBox [0 0 4 4] >>", nullptr, QUANTAPDF_ERROR_UNSUPPORTED},
            {"/Fm Do", "", "<< /Subtype /PS >>", nullptr, QUANTAPDF_ERROR_UNSUPPORTED},
            {"BT /Fm Do ET", "0 0 2 2 re f", "<< /Subtype /Form /BBox [0 0 4 4] >>", nullptr, QUANTAPDF_ERROR_UNSUPPORTED},
        };
        for(const auto& c : cases) {
            Object resources = Object::newNull();
            auto pdf = make_base_pdf(c.page_content, resources);
            auto xobjs = Object::newDictionary();
            Object form_res = c.res_spec != nullptr ? Object::parse(c.res_spec) : Object::newNull();
            xobjs.replaceKey("/Fm", make_form(*pdf, c.form_content, c.dict_spec, form_res));
            resources.replaceKey("/XObject", xobjs);
            CHECK(render_bytes(serialize_pdf(*pdf), 72u, unused, c.expected) == EXIT_SUCCESS);
        }
    }

    std::printf("Form XObject Do: %zu full RGB renders, Composer round-trip, nested/text/isolation parity, %zu atomic refusals\n",
        renders, refusals);
    return EXIT_SUCCESS;
}
#endif
