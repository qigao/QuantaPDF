#ifndef QUANTAPDF_NATIVE_IMAGE_DEPTH_CASES_H
#define QUANTAPDF_NATIVE_IMAGE_DEPTH_CASES_H

#include "native_image_depth_fixture.h"
#include <memory>

static int native_image_depth_cases(const std::string& filename)
{
    using namespace native_image_depth;
    using bitmap_owner = std::unique_ptr<quantapdf_bitmap, decltype(&quantapdf_drop_bitmap)>;
    size_t rendered = 0u, extracted = 0u, denied = 0u;

    const auto matches = [](quantapdf_bitmap *bitmap, unsigned width, unsigned height,
                             const std::vector<unsigned char>& expected) {
        int w = 0, h = 0, stride = 0, channels = 0;
        const unsigned char *pixels = nullptr;
        size_t bytes = 0u;
        return quantapdf_bitmap_dimensions(bitmap, &w, &h, &stride, &channels) == QUANTAPDF_OK &&
            w == int(width) && h == int(height) && channels == 3 && stride == 3 * w &&
            quantapdf_bitmap_data(bitmap, &pixels, &bytes) == QUANTAPDF_OK &&
            bytes == expected.size() && std::memcmp(pixels, expected.data(), bytes) == 0;
    };

    const auto execute = [&](const specimen& image, const std::string& original,
                              const std::vector<unsigned char>& reference,
                              unsigned page_width, unsigned page_height,
                              quantapdf_status wanted = QUANTAPDF_OK) -> int {
        CHECK(write_pdf(filename, original));
        quantapdf_document *raw_doc = nullptr;
        OK(quantapdf_open(filename.c_str(), nullptr, &raw_doc));
        std::unique_ptr<quantapdf_document, decltype(&quantapdf_close)> document(raw_doc, quantapdf_close);
        CHECK(std::remove(filename.c_str()) == 0);
        quantapdf_page *raw_page = nullptr;
        OK(quantapdf_load_page(document.get(), 0u, &raw_page));
        std::unique_ptr<quantapdf_page, decltype(&quantapdf_drop_page)> page(raw_page, quantapdf_drop_page);
        document.reset();

        quantapdf_bitmap *raw_bitmap = nullptr;
        quantapdf_image_page *raw_images = nullptr;
        const auto page_status = quantapdf_render_page(page.get(), &raw_bitmap);
        bitmap_owner bitmap(raw_bitmap, quantapdf_drop_bitmap);
        const auto image_status = quantapdf_extract_images(page.get(), &raw_images);
        std::unique_ptr<quantapdf_image_page, decltype(&quantapdf_drop_image_page)>
            images(raw_images, quantapdf_drop_image_page);
        page.reset();
        if (page_status != wanted || image_status != wanted)
            std::fprintf(stderr, "Image depth %u channels=%u encoding=%d got render=%d extract=%d expected=%d\n",
                image.bits, image.channels, int(image.encoding), int(page_status), int(image_status), int(wanted));
        CHECK(page_status == wanted && image_status == wanted);
        if (wanted != QUANTAPDF_OK) {
            CHECK(!bitmap && !images);
            ++denied;
            return EXIT_SUCCESS;
        }

        const bool page_exact = matches(bitmap.get(), page_width, page_height, reference);
        if (!page_exact)
            std::fprintf(stderr, "Image depth %u channels=%u encoding=%d rotate=%d interpolation=%d: page pixels differ\n",
                image.bits, image.channels, int(image.encoding), image.rotation, int(image.interpolate));
        CHECK(page_exact);
        ++rendered;
        size_t count = 0u;
        OK(quantapdf_image_count(images.get(), &count));
        CHECK(count == 1u);
        quantapdf_image_info info{};
        info.struct_size = sizeof(info);
        OK(quantapdf_image_get_info(images.get(), 0u, &info));
        CHECK(info.pixel_width == int(image.width) && info.pixel_height == int(image.height) &&
            info.components == int(image.channels) && info.bits_per_component == int(image.bits) && !info.has_alpha);
        OK(quantapdf_image_render(images.get(), 0u, &raw_bitmap));
        bitmap.reset(raw_bitmap);
        images.reset();
        CHECK(matches(bitmap.get(), image.width, image.height, rgb(image, false)));
        ++extracted;
        return EXIT_SUCCESS;
    };

    for (unsigned bits : {1u, 2u, 4u, 8u, 16u})
    for (unsigned channels : {1u, 3u})
    for (auto encoding : {storage::raw, storage::flate, storage::tiff, storage::png}) {
        auto image = grid(bits, channels, encoding);
        const bool turned = image.rotation == 90;
        CHECK(execute(image, create(image), rgb(image, turned), turned ? 2u : 3u, turned ? 3u : 2u) == EXIT_SUCCESS);
    }

    specimen wide;
    wide.width = 2u;
    wide.height = 1u;
    wide.bits = 16u;
    wide.samples = {0u, 129u};
    wide.interpolate = true;
    std::vector<unsigned char> expected(5u * 4u * 3u, 255u);
    // The middle pixel is 64.5/257, which rounds to zero. Rounding both
    // input texels to RGB8 first would instead produce one.
    for (size_t y : {1u, 2u}) {
        const size_t start = (y * 5u + 1u) * 3u;
        std::fill_n(expected.begin() + start, 6u, 0u);
        std::fill_n(expected.begin() + start + 6u, 3u, 1u);
    }
    CHECK(execute(wide, create(wide, fault::none, "[0 0 5 4]", "q 3 0 0 2 1 1 cm /I Do Q"),
        expected, 5u, 4u) == EXIT_SUCCESS);

    wide.interpolate = false;
    wide.samples = {450u, 450u};
    // 450/257 mixed with half white rounds to 128. Premature rounding to
    // two followed by the same geometric coverage would yield 129.
    for (size_t y : {1u, 2u}) {
        const size_t start = (y * 5u + 1u) * 3u;
        std::fill_n(expected.begin() + start, 3u, 128u);
        std::fill_n(expected.begin() + start + 3u, 6u, 2u);
        std::fill_n(expected.begin() + start + 9u, 3u, 128u);
    }
    CHECK(execute(wide, create(wide, fault::none, "[0 0 5 4]", "q 3 0 0 2 1.5 1 cm /I Do Q"),
        expected, 5u, 4u) == EXIT_SUCCESS);

    for (auto bad : {fault::short_rows, fault::predictor_width, fault::decode_length, fault::depth}) {
        const auto image = grid(4u, 3u, bad == fault::predictor_width ? storage::tiff : storage::raw);
        const auto status = bad == fault::depth ? QUANTAPDF_ERROR_UNSUPPORTED : QUANTAPDF_ERROR_FORMAT;
        CHECK(execute(image, create(image, bad), {}, 0u, 0u, status) == EXIT_SUCCESS);
    }
    CHECK(rendered == 42u && extracted == 42u && denied == 4u);
    std::printf("Image depth: %zu full-page RGB matches, %zu extracted RGB matches, %zu atomic errors\n",
        rendered, extracted, denied);
    return EXIT_SUCCESS;
}
#endif
