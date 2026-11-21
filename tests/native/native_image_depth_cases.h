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
                             const std::vector<unsigned char>& expected, int components = 3) {
        int w = 0, h = 0, stride = 0, channels = 0;
        const unsigned char *pixels = nullptr;
        size_t bytes = 0u;
        return quantapdf_bitmap_dimensions(bitmap, &w, &h, &stride, &channels) == QUANTAPDF_OK &&
            w == int(width) && h == int(height) && channels == components && stride == components * w &&
            quantapdf_bitmap_data(bitmap, &pixels, &bytes) == QUANTAPDF_OK &&
            bytes == expected.size() && std::memcmp(pixels, expected.data(), bytes) == 0;
    };

    const auto execute = [&](const specimen& image, const std::string& original,
                              const std::vector<unsigned char>& reference,
                              unsigned page_width, unsigned page_height,
                              quantapdf_status wanted = QUANTAPDF_OK,
                              const std::vector<unsigned char>* image_reference = nullptr, bool indexed = false,
                              bool has_alpha = false) -> int {
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
            info.components == (indexed ? 3 : int(image.channels)) &&
            info.bits_per_component == int(image.bits) && bool(info.has_alpha) == has_alpha);
        OK(quantapdf_image_render(images.get(), 0u, &raw_bitmap));
        bitmap.reset(raw_bitmap);
        images.reset();
        CHECK(matches(bitmap.get(), image.width, image.height,
            image_reference ? *image_reference : rgb(image, false), has_alpha ? 4 : 3));
        ++extracted;
        return EXIT_SUCCESS;
    };

    for (unsigned bits : {1u, 2u, 4u, 8u, 16u})
    for (unsigned channels : {1u, 3u, 4u})
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

    specimen cmyk_wide = wide;
    cmyk_wide.channels = 4u;
    cmyk_wide.samples = {65534u, 65534u, 65534u, 1u, 65000u, 65000u, 65000u, 406u};
    CHECK(execute(cmyk_wide, create(cmyk_wide, fault::none, "[0 0 5 4]", "q 3 0 0 2 1 1 cm /I Do Q"),
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
    const indexed_palette rgb_palette{"/DeviceRGB", 2u,
        std::string("\xff\x00\x00\x00\xff\x00\x00\x00\xff", 9u)};
    for (unsigned bits : {1u, 2u, 4u, 8u, 16u}) {
        auto image = grid(bits, 1u, bits == 4u ? storage::tiff : storage::raw);
        const auto indices = bits == 1u ? std::vector{0u, 1u, 0u, 1u, 0u, 0u} :
            bits == 2u ? std::vector{0u, 2u, 1u, 1u, 2u, 1u} :
                         std::vector{0u, 2u, 2u, 1u, 2u, 2u};
        const auto reference = indexed_rgb(image, rgb_palette, indices, false);
        CHECK(execute(image, create(image, fault::none, "[0 0 3 2]", "q 3 0 0 2 0 0 cm /I Do Q", &rgb_palette),
            reference, 3u, 2u, QUANTAPDF_OK, &reference, true) == EXIT_SUCCESS);
    }

    auto gray = grid(4u, 1u, storage::png);
    gray.samples = {0u, 4u, 5u, 8u, 12u, 15u};
    gray.rotation = 90;
    const indexed_palette gray_palette{"/DeviceGray", 3u, std::string("\x10\x40\x8a\xe1", 4u),
        true, "[3.625 -0.125]"};
    const std::vector<unsigned> gray_indices{3u, 3u, 2u, 2u, 1u, 0u};
    const auto gray_reference = indexed_rgb(gray, gray_palette, gray_indices, false);
    CHECK(execute(gray, create(gray, fault::none, "[0 0 3 2]", "q 3 0 0 2 0 0 cm /I Do Q", &gray_palette),
        indexed_rgb(gray, gray_palette, gray_indices, true), 2u, 3u, QUANTAPDF_OK, &gray_reference, true) == EXIT_SUCCESS);

    specimen bilinear;
    bilinear.width = 2u;
    bilinear.height = 1u;
    bilinear.bits = 2u;
    bilinear.samples = {0u, 2u};
    bilinear.encoding = storage::flate;
    bilinear.interpolate = true;
    const auto texels = indexed_rgb(bilinear, rgb_palette, {0u, 2u}, false);
    const std::array<unsigned char, 9u> interpolated{255u, 0u, 0u, 128u, 0u, 128u, 0u, 0u, 255u};
    std::vector<unsigned char> page(5u * 4u * 3u, 0u);
    for (size_t y : {1u, 2u})
        std::copy(interpolated.begin(), interpolated.end(), page.begin() + (y * 5u + 1u) * 3u);
    CHECK(execute(bilinear, create(bilinear, fault::none, "[0 0 5 4]",
        "0 g 0 0 5 4 re f q 1 1 3 2 re W n 3 0 0 2 1 1 cm /I Do Q", &rgb_palette),
        page, 5u, 4u, QUANTAPDF_OK, &texels, true) == EXIT_SUCCESS);

    // For the CMYK palette, index 1 is green while 0 and 2 map to red and blue.
    const indexed_palette cmyk_palette{"/DeviceCMYK", 2u,
        std::string("\x00\xff\xff\x00\xff\x00\xff\x00\xff\xff\x00\x00", 12u), true};
    CHECK(execute(bilinear, create(bilinear, fault::none, "[0 0 5 4]",
        "0 g 0 0 5 4 re f q 1 1 3 2 re W n 3 0 0 2 1 1 cm /I Do Q", &cmyk_palette),
        page, 5u, 4u, QUANTAPDF_OK, &texels, true) == EXIT_SUCCESS);

    auto invalid_palette = rgb_palette;
    invalid_palette.bytes.pop_back();
    CHECK(execute(bilinear, create(bilinear, fault::none, "[0 0 5 4]", "q 3 0 0 2 0 0 cm /I Do Q", &invalid_palette),
        {}, 0u, 0u, QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
    invalid_palette = gray_palette;
    invalid_palette.high = 256u;
    CHECK(execute(gray, create(gray, fault::none, "[0 0 3 2]", "q 3 0 0 2 0 0 cm /I Do Q", &invalid_palette),
        {}, 0u, 0u, QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
    invalid_palette = rgb_palette;
    invalid_palette.base = "/Lab";
    CHECK(execute(bilinear, create(bilinear, fault::none, "[0 0 5 4]", "q 3 0 0 2 0 0 cm /I Do Q", &invalid_palette),
        {}, 0u, 0u, QUANTAPDF_ERROR_UNSUPPORTED) == EXIT_SUCCESS);

    auto cmyk = grid(8u, 4u, storage::raw);
    CHECK(execute(cmyk, create(cmyk, fault::decode_length), {}, 0u, 0u,
        QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
    CHECK(execute(cmyk, create(cmyk, fault::default_cmyk), {}, 0u, 0u,
        QUANTAPDF_ERROR_UNSUPPORTED) == EXIT_SUCCESS);
    const auto plain_gray = grid(8u, 1u, storage::raw);
    CHECK(execute(plain_gray, create(plain_gray, fault::default_cmyk),
        rgb(plain_gray, false), 3u, 2u) == EXIT_SUCCESS);

    struct jpeg_case {
        bool progressive;
        int adobe, transform;
        bool invert;
        std::array<unsigned char, 3u> color;
    };
    const jpeg_case jpeg_cases[] = {
        {false, -1, -1, false, {35u, 23u, 0u}},
        {false, -1, 1, false, {8u, 0u, 0u}},
        {false, 0, 1, true, {0u, 0u, 13u}},
        {false, 2, 0, true, {0u, 29u, 48u}},
        {true, 2, 0, true, {0u, 29u, 48u}},
    };
    constexpr const char *jpeg_bounds = "[0 0 8 8]";
    constexpr const char *jpeg_paint = "q 8 0 0 8 0 0 cm /I Do Q";
    for (const auto& item : jpeg_cases) {
        const auto image = jpeg_cmyk(item.progressive, item.adobe, item.transform, item.invert);
        std::vector<unsigned char> reference(8u * 8u * 3u);
        for (size_t i = 0u; i < reference.size(); i += 3u)
            std::copy(item.color.begin(), item.color.end(), reference.begin() + i);
        CHECK(execute(image, create(image, fault::none, jpeg_bounds, jpeg_paint),
            reference, 8u, 8u, QUANTAPDF_OK, &reference) == EXIT_SUCCESS);
    }
    auto bad_jpeg = jpeg_cmyk(false, 0, -1, false);
    --bad_jpeg.width;
    CHECK(execute(bad_jpeg, create(bad_jpeg, fault::none, jpeg_bounds, jpeg_paint),
        {}, 0u, 0u, QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);

    const std::vector<unsigned char> transparent_then_blue{0, 0, 0, 0, 0, 0, 255, 255};
    const std::array<unsigned char, 9u> masked_row{0, 255, 0, 0, 128, 128, 0, 0, 255};
    std::vector<unsigned char> masked_page(60u, 0u);
    for (size_t i = 1; i < masked_page.size(); i += 3u) masked_page[i] = 255u;
    for (size_t y : {1u, 2u})
        std::copy(masked_row.begin(), masked_row.end(), masked_page.begin() + (y * 5u + 1u) * 3u);
    for (unsigned bits : {8u, 16u}) {
        specimen masked;
        masked.width = 2u;
        masked.height = 1u;
        masked.channels = 3u;
        masked.bits = bits;
        const unsigned maximum = (1u << bits) - 1u;
        masked.samples = {maximum, 0u, 0u, 0u, 0u, maximum};
        masked.encoding = storage::flate;
        masked.interpolate = true;
        masked.mask = bits == 8u ? "[255 255 0 0 0 0]" : "[65535 65535 0 0 0 0]";
        CHECK(execute(masked, create(masked, fault::none, "[0 0 5 4]",
            "0 1 0 rg 0 0 5 4 re f q 1 1 3 2 re W n 3 0 0 2 1 1 cm /I Do Q"),
            masked_page, 5u, 4u, QUANTAPDF_OK, &transparent_then_blue, false, true) == EXIT_SUCCESS);
    }

    specimen masked_indices = bilinear;
    masked_indices.rotation = 90;
    masked_indices.interpolate = false;
    masked_indices.mask = "[0 0]";
    auto inverted_palette = rgb_palette;
    inverted_palette.decode = "[2 0]";
    masked_indices.samples = {0u, 3u};
    const std::vector<unsigned char> masked_index_page{255, 255, 255, 255, 0, 0};
    const std::vector<unsigned char> masked_index_texels{0, 0, 0, 0, 255, 0, 0, 255};
    CHECK(execute(masked_indices, create(masked_indices, fault::none, "[0 0 2 1]",
        "q 2 0 0 1 0 0 cm /I Do Q", &inverted_palette),
        masked_index_page, 1u, 2u, QUANTAPDF_OK, &masked_index_texels, true, true) == EXIT_SUCCESS);

    for (const char *mask : {"[0]", "[0 4]", "[2 1]"}) {
        masked_indices.mask = mask;
        CHECK(execute(masked_indices, create(masked_indices, fault::none, "[0 0 2 1]",
            "q 2 0 0 1 0 0 cm /I Do Q", &inverted_palette), {}, 0u, 0u,
            QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
    }

    // A 1x1 image covers both cells of an independent 2x1 alpha grid.
    specimen placed;
    placed.width = placed.height = 1u;
    placed.channels = 3u;
    placed.samples = {0u, 0u, 255u};
    bitmap_mask explicit_mask{2u, 1u, std::string(1u, '\x7f')};
    const std::vector<unsigned char> no_ink{0u, 0u, 0u, 0u};
    const std::vector<unsigned char> blue_blue_white_white{
        0u, 0u, 255u, 0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u, 255u};
    CHECK(execute(placed, create(placed, fault::none, "[0 0 4 1]",
        "q 4 0 0 1 0 0 cm /I Do Q", nullptr, &explicit_mask), blue_blue_white_white,
        4u, 1u, QUANTAPDF_OK, &no_ink, false, true) == EXIT_SUCCESS);

    explicit_mask = {1u, 2u, std::string("\x7f\xff", 2u), "/Decode [1 0] /BitsPerComponent 1", true};
    placed.rotation = 90;
    CHECK(execute(placed, create(placed, fault::none, "[0 0 1 4]",
        "q 1 0 0 4 0 0 cm /I Do Q", nullptr, &explicit_mask), blue_blue_white_white,
        4u, 1u, QUANTAPDF_OK) == EXIT_SUCCESS);

    placed.rotation = 0;
    explicit_mask = {2u, 1u, std::string(1u, '\x40'), "/Interpolate true"};
    const std::vector<unsigned char> half_blue{0u, 0u, 128u, 128u};
    const std::vector<unsigned char> blue_half_green{0u, 0u, 255u, 0u, 128u, 128u, 0u, 255u, 0u};
    constexpr const char *paint = "0 1 0 rg 0 0 3 1 re f q 0 0 3 1 re W n 3 0 0 1 0 0 cm /I Do Q";
    CHECK(execute(placed, create(placed, fault::none, "[0 0 3 1]", paint, nullptr, &explicit_mask),
        blue_half_green, 3u, 1u, QUANTAPDF_OK, &half_blue, false, true) == EXIT_SUCCESS);

    placed.width = 2u;
    placed.bits = 16u;
    placed.samples = {65535u, 0u, 0u, 0u, 0u, 65535u};
    placed.interpolate = true;
    const std::vector<unsigned char> red_mix_green{255u, 0u, 0u, 64u, 128u, 64u, 0u, 255u, 0u};
    const std::vector<unsigned char> red_then_transparent{255u, 0u, 0u, 255u, 0u, 0u, 0u, 0u};
    CHECK(execute(placed, create(placed, fault::none, "[0 0 3 1]", paint, nullptr, &explicit_mask),
        red_mix_green, 3u, 1u, QUANTAPDF_OK, &red_then_transparent, false, true) == EXIT_SUCCESS);

    for (const char *entries : {"/ImageMask false", "/Decode [0 0]", "/BitsPerComponent 8",
                                "/ColorSpace /DeviceGray", "/Mask [0 0]", "/Interpolate /Yes"}) {
        explicit_mask.entries = entries;
        CHECK(execute(placed, create(placed, fault::none, "[0 0 3 1]", paint, nullptr, &explicit_mask),
            {}, 0u, 0u, QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);
    }

    const auto render_stencil = [&](const std::string& original,
        unsigned width, unsigned height, const std::vector<unsigned char>& reference,
        quantapdf_status wanted = QUANTAPDF_OK) -> int {
        CHECK(write_pdf(filename, original));
        quantapdf_document *raw_document = nullptr;
        OK(quantapdf_open(filename.c_str(), nullptr, &raw_document));
        std::unique_ptr<quantapdf_document, decltype(&quantapdf_close)> document(raw_document, quantapdf_close);
        CHECK(std::remove(filename.c_str()) == 0);
        quantapdf_page *raw_page = nullptr;
        OK(quantapdf_load_page(document.get(), 0u, &raw_page));
        std::unique_ptr<quantapdf_page, decltype(&quantapdf_drop_page)> page(raw_page, quantapdf_drop_page);
        quantapdf_bitmap *raw_bitmap = nullptr;
        const auto status = quantapdf_render_page(page.get(), &raw_bitmap);
        bitmap_owner bitmap(raw_bitmap, quantapdf_drop_bitmap);
        CHECK(status == wanted);
        CHECK(wanted == QUANTAPDF_OK ? matches(bitmap.get(), width, height, reference) : !bitmap);
        return EXIT_SUCCESS;
    };

    specimen stencil;
    stencil.width = 2u;
    stencil.height = 1u;
    stencil.bits = 1u;
    stencil.samples = {0u, 1u};
    stencil.stencil_entries = "/BitsPerComponent null";
    CHECK(render_stencil(create(stencil, fault::none, "[0 0 2 1]", "q 2 0 0 1 0 0 cm /I Do Q"),
        2u, 1u, {0u, 0u, 0u, 255u, 255u, 255u}) == EXIT_SUCCESS);

    // Reuse the XObject with a different fill in the same q frame as a clip
    // and BT/ET. Q must restore both the red fill and the original clip.
    const std::vector<unsigned char> stencil_scene{
        255u,0u,0u, 255u,0u,0u, 0u,255u,0u, 0u,255u,0u,
        64u,64u,64u, 0u,255u,0u, 0u,255u,0u, 0u,255u,0u,
        255u,0u,0u, 255u,0u,0u, 0u,255u,0u, 0u,255u,0u};
    CHECK(render_stencil(create(stencil, fault::none, "[0 0 4 3]",
        "0 1 0 rg 0 0 4 3 re f 1 0 0 rg 0 0 1 RG q 4 0 0 1 0 2 cm /I Do Q "
        "q 0.25 g 0 0 1 2 re W n BT ET 4 0 0 1 0 1 cm /I Do Q "
        "q 4 0 0 1 0 0 cm /I Do Q"), 4u, 3u, stencil_scene) == EXIT_SUCCESS);

    stencil.encoding = storage::flate;
    stencil.stencil_entries = "/Decode [1 0] /Interpolate true";
    stencil.rotation = 90;
    const std::vector<unsigned char> green_half_blue{0u,255u,0u, 0u,128u,128u, 0u,0u,255u};
    CHECK(render_stencil(create(stencil, fault::none, "[0 0 3 1]",
        "0 1 0 rg 0 0 3 1 re f BT ET 0 0 1 rg q 3 0 0 1 0 0 cm /I Do Q"),
        1u, 3u, green_half_blue) == EXIT_SUCCESS);
    stencil.stencil_entries = "/Decode [1 1]";
    CHECK(render_stencil(create(stencil), 0u, 0u, {}, QUANTAPDF_ERROR_FORMAT) == EXIT_SUCCESS);

    CHECK(rendered == 84u && extracted == 84u && denied == 19u);
    std::printf("Image depth: %zu full-page RGB matches, %zu extracted bitmap matches, %zu atomic errors\n",
        rendered, extracted, denied);
    return EXIT_SUCCESS;
}
#endif
