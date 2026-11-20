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

                const auto cells = colors(spec);
                std::vector<unsigned char> samples(source_pixels * 3u);
                for (size_t row = 0u; row < side; ++row) {
                    for (size_t col = 0u; col < side; ++col) {
                        const size_t cell = 2u * (row / 8u) + col / 8u;
                        std::copy_n(cells.data() + 3u * cell, 3u, samples.data() + 3u * (row * side + col));
                    }
                }
                CHECK(matches(extracted.get(), side, side, samples));
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

    // The Composer preserves the compressed progressive bytes, and the
    // reopened 1:1 page must include the samples from later AC scans.
    quantapdf_composer *raw_composer = nullptr;
    OK(quantapdf_composer_create(nullptr, &raw_composer));
    std::unique_ptr<quantapdf_composer, decltype(&quantapdf_drop_composer)>
        composer(raw_composer, quantapdf_drop_composer);
    quantapdf_composer_image_id image_id = 0u;
    OK(quantapdf_composer_add_image(composer.get(), progressive_420_jpeg,
        sizeof(progressive_420_jpeg), &image_id));
    const float side_points = static_cast<float>(subsampled_side);
    const quantapdf_composer_page_options page_options{
        sizeof(page_options), side_points, side_points, 0xffffffffu};
    size_t index = SIZE_MAX;
    OK(quantapdf_composer_add_page(composer.get(), &page_options, &index));
    const quantapdf_rect bounds{0, 0, side_points, side_points};
    const quantapdf_composer_image_options placement{QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH, 0u};
    OK(quantapdf_composer_draw_image(composer.get(), index, image_id, &bounds, &placement));
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
        auto object = pdf->getRoot().getKey("/Pages").getKey("/Kids").getArrayItem(0);
        auto image = object.getKey("/Resources").getKey("/XObject").getKey("/Im" + std::to_string(image_id));
        CHECK(image.isStream() && image.getDict().getKey("/Filter").isNameAndEquals("/DCTDecode"));
        const auto encoded = image.getRawStreamData();
        CHECK(encoded->getSize() == sizeof(progressive_420_jpeg) &&
            std::memcmp(encoded->getBuffer(), progressive_420_jpeg, encoded->getSize()) == 0);
    }
    CHECK(write_pdf(filename, std::string(reinterpret_cast<const char*>(bytes), size)));
    output.reset();
    composer.reset();
    quantapdf_document *raw_doc = nullptr;
    OK(quantapdf_open(filename.c_str(), nullptr, &raw_doc));
    doc_owner document(raw_doc, quantapdf_close);
    CHECK(std::remove(filename.c_str()) == 0);
    quantapdf_page *raw_page = nullptr;
    OK(quantapdf_load_page(document.get(), 0, &raw_page));
    page_owner page(raw_page, quantapdf_drop_page);
    document.reset();
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
    const auto expected = subsampled_rgb();
    CHECK(matches(bitmap.get(), subsampled_side, subsampled_side, expected));
    CHECK(matches(extracted.get(), subsampled_side, subsampled_side, expected));

    std::printf("PDF JPEG: %zu complete page RGB results, %zu decoded image RGB results, "
        "%zu atomic refusals and a 4:2:0 progressive Composer round trip\n",
        pages + 1u, images + 1u, refusals);
    return EXIT_SUCCESS;
}

#endif
