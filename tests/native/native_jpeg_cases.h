#ifndef QUANTAPDF_NATIVE_JPEG_CASES_H
#define QUANTAPDF_NATIVE_JPEG_CASES_H

#include "native_jpeg_fixture.h"
#include <memory>

static int native_jpeg_cases(const std::string& filename)
{
    using namespace native_jpeg;
    using bitmap_owner = std::unique_ptr<quantapdf_bitmap, decltype(&quantapdf_drop_bitmap)>;
    using doc_owner = std::unique_ptr<quantapdf_document, decltype(&quantapdf_close)>;
    using page_owner = std::unique_ptr<quantapdf_page, decltype(&quantapdf_drop_page)>;
    using image_owner = std::unique_ptr<quantapdf_image_page, decltype(&quantapdf_drop_image_page)>;
    using rate = native_clip_graph::dpi_rate;

    const auto matches = [](quantapdf_bitmap *bitmap, size_t width, size_t height,
                            const std::vector<unsigned char>& expected) {
        int w = 0, h = 0, stride = 0, components = 0;
        const unsigned char *pixels = nullptr;
        size_t size = 0u;
        return quantapdf_bitmap_dimensions(bitmap, &w, &h, &stride, &components) == QUANTAPDF_OK &&
            w == int(width) && h == int(height) && stride == 3 * w && components == 3 &&
            quantapdf_bitmap_data(bitmap, &pixels, &size) == QUANTAPDF_OK &&
            size == expected.size() && std::memcmp(pixels, expected.data(), size) == 0;
    };
    const auto flat_samples = [](const specimen& spec) {
        const auto cells = colors(spec);
        std::vector<unsigned char> samples(source_pixels * 3u);
        for (size_t row = 0u; row < side; ++row) {
            for (size_t col = 0u; col < side; ++col) {
                const size_t cell = 2u * (row / 8u) + col / 8u;
                std::copy_n(cells.data() + 3u * cell, 3u, samples.data() + 3u * (row * side + col));
            }
        }
        return samples;
    };

    size_t pages = 0u, images = 0u, refusals = 0u;
    for (const auto& spec : cases) {
        for (auto storage : {container::name, container::array, container::wrapped}) {
            for (bool encrypted : {false, true}) {
                const auto original = create(spec, storage, encrypted);
                CHECK(write_pdf(filename, original.bytes));
                quantapdf_document *raw_doc = nullptr;
                if (encrypted)
                    CHECK(quantapdf_open(filename.c_str(), "wrong", &raw_doc) ==
                        QUANTAPDF_ERROR_PASSWORD && raw_doc == nullptr);
                OK(quantapdf_open(filename.c_str(), encrypted ? "reader" : nullptr, &raw_doc));
                doc_owner document(raw_doc, quantapdf_close);
                CHECK(std::remove(filename.c_str()) == 0);
                quantapdf_page *raw_page = nullptr;
                OK(quantapdf_load_page(document.get(), 1, &raw_page));
                page_owner page(raw_page, quantapdf_drop_page);
                document.reset();

                quantapdf_image_page *raw_images = nullptr;
                OK(quantapdf_extract_images(page.get(), &raw_images));
                image_owner snapshot(raw_images, quantapdf_drop_image_page);
                size_t count = 0u;
                OK(quantapdf_image_count(snapshot.get(), &count));
                CHECK(count == original.images && count > 0u);
                quantapdf_bitmap *raw_image = nullptr;
                OK(quantapdf_image_render(snapshot.get(), 0u, &raw_image));
                bitmap_owner extracted(raw_image, quantapdf_drop_bitmap);

                std::vector<bitmap_owner> bitmaps;
                const rate rates[] = {72u, {147, 2}};
                for (auto dpi : rates) {
                    quantapdf_render_options options{};
                    options.dpi = float(dpi);
                    quantapdf_bitmap *raw_bitmap = nullptr;
                    const auto status = quantapdf_render_page_with_options(page.get(), &options, &raw_bitmap);
                    bitmap_owner bitmap(raw_bitmap, quantapdf_drop_bitmap);
                    if (status != QUANTAPDF_OK)
                        std::fprintf(stderr, "JPEG %s storage=%d dpi=%g encrypted=%d returned %d\n",
                            spec.name, int(storage), double(options.dpi), int(encrypted), int(status));
                    CHECK(status == QUANTAPDF_OK);
                    bitmaps.push_back(std::move(bitmap));
                }
                page.reset();
                snapshot.reset();

                CHECK(matches(extracted.get(), side, side, flat_samples(spec)));
                ++images;
                for (size_t i = 0u; i < bitmaps.size(); ++i) {
                    const auto dpi = rates[i];
                    const auto rgb = native_clip_graph::rgb(original, dpi);
                    const bool equal = matches(bitmaps[i].get(), native_clip_graph::width(original, dpi),
                        native_clip_graph::height(original, dpi), rgb);
                    if (!equal)
                        std::fprintf(stderr, "JPEG %s storage=%d dpi=%g encrypted=%d RGB differs\n",
                            spec.name, int(storage), double(float(dpi)), int(encrypted));
                    CHECK(equal);
                    ++pages;
                }
            }
        }
    }

    for (const auto *spec : {&cases[1], &progressive_ycbcr})
    for (auto bad : {fault::width, fault::height, fault::channels, fault::truncated,
                     fault::bad_transform, fault::nonterminal_dct}) {
        const auto original = create(*spec, container::name, true, bad);
        CHECK(write_pdf(filename, original.bytes));
        quantapdf_document *raw_doc = nullptr;
        OK(quantapdf_open(filename.c_str(), "reader", &raw_doc));
        doc_owner document(raw_doc, quantapdf_close);
        CHECK(std::remove(filename.c_str()) == 0);
        quantapdf_page *raw_page = nullptr;
        OK(quantapdf_load_page(document.get(), 1, &raw_page));
        page_owner page(raw_page, quantapdf_drop_page);
        const auto expected = bad == fault::nonterminal_dct
            ? QUANTAPDF_ERROR_UNSUPPORTED : QUANTAPDF_ERROR_FORMAT;
        quantapdf_bitmap *bitmap = reinterpret_cast<quantapdf_bitmap*>(uintptr_t(1));
        CHECK(quantapdf_render_page(page.get(), &bitmap) == expected && bitmap == nullptr);
        quantapdf_image_page *snapshot = reinterpret_cast<quantapdf_image_page*>(uintptr_t(1));
        CHECK(quantapdf_extract_images(page.get(), &snapshot) == expected && snapshot == nullptr);

        OK(quantapdf_load_page(document.get(), 0, &raw_page));
        page.reset(raw_page);
        OK(quantapdf_render_page(page.get(), &bitmap));
        bitmap_owner blank(bitmap, quantapdf_drop_bitmap);
        CHECK(matches(blank.get(), 5u, 4u, std::vector<unsigned char>(60u, 255u)));
        ++refusals;
    }

    CHECK(pages == 120u && images == 60u && refusals == 12u);

    // Reopen Composer's original JPEG streams. Unmarked RGB and YCbCr
    // need different PDF transforms even though both use DeviceRGB.
    struct composed_image {
        std::span<const unsigned char> payload;
        unsigned extent;
        int components;
        int color_transform;
        std::vector<unsigned char> pixels;
    };
    auto gray = cases[0];
    gray.invert = false;
    const auto raw_rgb = flat_samples(cases[1]);
    const std::array examples{
        composed_image{progressive_420_jpeg, subsampled_side, 3, 1, subsampled_rgb()},
        composed_image{progressive_unmarked_rgb_jpeg, side, 3, 0, raw_rgb},
        composed_image{color_jpeg, side, 3, 0, raw_rgb},
        composed_image{gray_jpeg, side, 1, 0, flat_samples(gray)},
    };
    quantapdf_composer *raw_composer = nullptr;
    OK(quantapdf_composer_create(nullptr, &raw_composer));
    std::unique_ptr<quantapdf_composer, decltype(&quantapdf_drop_composer)>
        composer(raw_composer, quantapdf_drop_composer);
    const quantapdf_composer_image_options placement{QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH, 0u};
    std::vector<quantapdf_composer_image_id> ids(examples.size());
    for (size_t i = 0u; i < examples.size(); ++i) {
        const auto& example = examples[i];
        OK(quantapdf_composer_add_image(composer.get(), example.payload.data(),
            example.payload.size(), &ids[i]));
        quantapdf_composer_image_id duplicate = 0u;
        OK(quantapdf_composer_add_image(composer.get(), example.payload.data(),
            example.payload.size(), &duplicate));
        CHECK(ids[i] != 0u && duplicate == ids[i]);
        const float extent = static_cast<float>(example.extent);
        const quantapdf_composer_page_options page_options{
            sizeof(page_options), extent, extent, 0xffffffffu};
        size_t index = SIZE_MAX;
        OK(quantapdf_composer_add_page(composer.get(), &page_options, &index));
        CHECK(index == i);
        const quantapdf_rect bounds{0, 0, extent, extent};
        OK(quantapdf_composer_draw_image(composer.get(), index, ids[i], &bounds, &placement));
    }
    quantapdf_output *raw_output = nullptr;
    OK(quantapdf_composer_finish(composer.get(), &raw_output));
    std::unique_ptr<quantapdf_output, decltype(&quantapdf_drop_output)>
        output(raw_output, quantapdf_drop_output);
    const unsigned char *bytes = nullptr;
    size_t size = 0u;
    OK(quantapdf_output_data(output.get(), &bytes, &size));
    {
        auto pdf = QPDF::create();
        pdf->processMemoryFile("progressive JPEG Composer output",
            reinterpret_cast<const char*>(bytes), size);
        auto kids = pdf->getRoot().getKey("/Pages").getKey("/Kids");
        CHECK(kids.getArrayNItems() == int(examples.size()));
        for (size_t i = 0u; i < examples.size(); ++i) {
            const auto& example = examples[i];
            auto object = kids.getArrayItem(int(i));
            auto image = object.getKey("/Resources").getKey("/XObject")
                .getKey("/Im" + std::to_string(ids[i]));
            CHECK(image.isStream());
            auto dict = image.getDict();
            CHECK(dict.getKey("/Filter").isNameAndEquals("/DCTDecode"));
            CHECK(dict.getKey("/ColorSpace").isNameAndEquals(
                example.components == 1 ? "/DeviceGray" : "/DeviceRGB"));
            auto parameters = dict.getKey("/DecodeParms");
            if (example.components == 3) {
                CHECK(parameters.isDictionary());
                auto transform = parameters.getKey("/ColorTransform");
                CHECK(transform.isInteger() && transform.getIntValue() == example.color_transform);
            } else {
                CHECK(parameters.isNull());
            }
            const auto encoded = image.getRawStreamData();
            CHECK(encoded->getSize() == example.payload.size() &&
                std::memcmp(encoded->getBuffer(), example.payload.data(), encoded->getSize()) == 0);
        }
    }
    CHECK(write_pdf(filename, std::string(reinterpret_cast<const char*>(bytes), size)));
    output.reset();
    composer.reset();
    quantapdf_document *raw_doc = nullptr;
    OK(quantapdf_open(filename.c_str(), nullptr, &raw_doc));
    doc_owner document(raw_doc, quantapdf_close);
    CHECK(std::remove(filename.c_str()) == 0);
    std::vector<page_owner> composed_pages;
    for (size_t i = 0u; i < examples.size(); ++i) {
        quantapdf_page *raw_page = nullptr;
        OK(quantapdf_load_page(document.get(), int(i), &raw_page));
        composed_pages.emplace_back(raw_page, quantapdf_drop_page);
    }
    document.reset();
    for (size_t i = 0u; i < examples.size(); ++i) {
        const auto& example = examples[i];
        auto& page = composed_pages[i];
        quantapdf_bitmap *raw_bitmap = nullptr;
        OK(quantapdf_render_page(page.get(), &raw_bitmap));
        bitmap_owner bitmap(raw_bitmap, quantapdf_drop_bitmap);
        quantapdf_image_page *raw_images = nullptr;
        OK(quantapdf_extract_images(page.get(), &raw_images));
        image_owner snapshot(raw_images, quantapdf_drop_image_page);
        OK(quantapdf_image_render(snapshot.get(), 0u, &raw_bitmap));
        bitmap_owner extracted(raw_bitmap, quantapdf_drop_bitmap);
        page.reset();
        snapshot.reset();
        CHECK(matches(bitmap.get(), example.extent, example.extent, example.pixels));
        CHECK(matches(extracted.get(), example.extent, example.extent, example.pixels));
        ++pages;
        ++images;
    }

    std::printf("PDF JPEG: %zu complete page RGB results, %zu decoded image RGB results, "
        "%zu atomic refusals and %zu Composer color round trips\n",
        pages, images, refusals, examples.size());
    return EXIT_SUCCESS;
}

#endif
