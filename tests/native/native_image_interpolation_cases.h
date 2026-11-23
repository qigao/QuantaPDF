#ifndef QUANTAPDF_NATIVE_IMAGE_INTERPOLATION_CASES_H
#define QUANTAPDF_NATIVE_IMAGE_INTERPOLATION_CASES_H

#include "native_clip_graph_fixture.h"
#include "native_jpeg_fixture.h"
#include <memory>

static int native_image_interpolation_cases(const std::string& filename)
{
    using namespace native_clip_graph;
    using bitmap_owner = std::unique_ptr<quantapdf_bitmap, decltype(&quantapdf_drop_bitmap)>;
    const dpi_rate rates[] = {72u, 96u, {147, 2}, 144u};
    size_t renders = 0u, changed = 0u;

    const auto render = [&](const original& sample, bool encrypted, const char *label,
                            quantapdf_status expected_status = QUANTAPDF_OK) -> int {
        CHECK(write_pdf(filename, sample.bytes));
        quantapdf_document *raw_doc = nullptr;
        OK(quantapdf_open(filename.c_str(), encrypted ? "reader" : nullptr, &raw_doc));
        std::unique_ptr<quantapdf_document, decltype(&quantapdf_close)> document(raw_doc, quantapdf_close);
        CHECK(std::remove(filename.c_str()) == 0);
        quantapdf_page *raw_page = nullptr;
        OK(quantapdf_load_page(document.get(), 1u, &raw_page));
        std::unique_ptr<quantapdf_page, decltype(&quantapdf_drop_page)> page(raw_page, quantapdf_drop_page);
        document.reset();

        auto nearest = sample;
        nearest.interpolate = false;
        for (auto dpi : rates) {
            const quantapdf_render_options render_options{float(dpi), 0.0f, 0, {}, 0};
            quantapdf_bitmap *raw_bitmap = nullptr;
            const auto status = quantapdf_render_page_with_options(page.get(), &render_options, &raw_bitmap);
            bitmap_owner bitmap(raw_bitmap, quantapdf_drop_bitmap);
            if (status != expected_status)
                std::fprintf(stderr, "Image interpolation %s at %g DPI: status %d, expected %d\n",
                    label, double(float(dpi)), int(status), int(expected_status));
            CHECK(status == expected_status);
            if (status != QUANTAPDF_OK) {
                CHECK(!bitmap);
                continue;
            }
            int w = 0, h = 0, stride = 0, components = 0;
            OK(quantapdf_bitmap_dimensions(bitmap.get(), &w, &h, &stride, &components));
            CHECK(w == int(width(sample, dpi)) && h == int(height(sample, dpi)) &&
                stride == 3 * w && components == 3);
            const unsigned char *pixels = nullptr;
            size_t size = 0u;
            OK(quantapdf_bitmap_data(bitmap.get(), &pixels, &size));
            const auto expected = rgb(sample, dpi);
            CHECK(size == expected.size());
            auto difference = std::mismatch(expected.begin(), expected.end(), pixels);
            if (difference.first != expected.end())
                std::fprintf(stderr, "Image interpolation %s at %g DPI: byte %zu is %u, expected %u\n",
                    label, double(float(dpi)), size_t(difference.first - expected.begin()),
                    unsigned(*difference.second), unsigned(*difference.first));
            CHECK(difference.first == expected.end());
            ++renders;
            if (sample.interpolate && expected != rgb(nearest, dpi)) ++changed;
        }
        return EXIT_SUCCESS;
    };

    options axis;
    for (auto state : {image_interpolation::absent, image_interpolation::disabled,
                       image_interpolation::enabled}) {
        axis.interpolation = state;
        CHECK(render(create(axis, false), false, "axis RGB") == EXIT_SUCCESS);
    }
    CHECK(changed > 0u);

    for (bool gray : {false, true}) {
        auto opt = viewport_case(options{}, gray ? 3u : 1u);
        opt.interpolation = image_interpolation::enabled;
        opt.flate_image = true;
        opt.gray_image = gray;
        opt.image_affine = gray ? 6u : 4u;
        opt.rotation = gray ? 270 : 90;
        opt.user_quarters = gray ? 3u : 5u;
        opt.order = gray ? 5u : 3u;
        const auto before = changed;
        CHECK(render(create(opt, gray), gray, gray ? "affine Flate Gray" : "affine Flate RGB") == EXIT_SUCCESS);
        CHECK(changed > before);
    }

    for (const auto *spec : {&native_jpeg::cases[1], &native_jpeg::progressive_gray}) {
        const auto before = changed;
        const auto sample = native_jpeg::create(*spec, native_jpeg::container::array, true,
            native_jpeg::fault::none, true);
        CHECK(render(sample, true, spec->name) == EXIT_SUCCESS);
        CHECK(changed > before);
    }

    axis.interpolation = image_interpolation::invalid;
    CHECK(render(create(axis, true), true, "invalid flag", QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
    CHECK(renders == 28u);
    std::printf("Image interpolation: %zu complete RGB references, %zu distinguish bilinear from nearest; malformed flag refused\n",
        renders, changed);
    return EXIT_SUCCESS;
}

#endif
