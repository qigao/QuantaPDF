#ifndef QUANTAPDF_NATIVE_IMAGE_DEPTH_OOM_CASES_H
#define QUANTAPDF_NATIVE_IMAGE_DEPTH_OOM_CASES_H

#include "native_image_depth_fixture.h"
#include "backend/qpdf_composer.h"

static int native_image_depth_source_contract(const quantapdf_qpdf_page_limits& defaults, unsigned channels, bool masked = false,
    bool independent = false)
{
    using namespace native_image_depth;
    const bool indexed = channels == 1u;
    auto specimen = grid(4u, channels, storage::png);
    specimen.rotation = 90;
    if (masked && !independent) specimen.mask = "[0 0]";
    const image_mask mask{6u, 4u, std::string("\xc0\xc0\0\0", 4u)};
    const indexed_palette palette{"/DeviceRGB", 2u, std::string("\x11\x21\x31\x42\x52\x62\x73\x83\x93", 9u)};
    auto bytes = create(specimen, fault::none, "[0 0 3 2]", "q 3 0 0 2 0 0 cm /I Do Q",
        indexed ? &palette : nullptr, independent ? &mask : nullptr);
    auto reference = indexed ? indexed_rgb(specimen, palette, {0u, 2u, 2u, 1u, 2u, 2u}, true) :
        rgb(specimen, true);
    // After a 90-degree turn the source's index-zero texel lands at (1,0).
    if (masked) std::fill_n(reference.data() + 3u, 3u, 255u);
    const size_t pixel_bytes = indexed ? 18u : 36u;
    const size_t alpha_bytes = independent ? 24u : masked ? 6u : 0u;
    const size_t source_pixels = independent ? 30u : 6u;
    quantapdf_qpdf_document *raw = nullptr;
    CHECK(quantapdf_qpdf_open_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
        bytes.size(), "", &raw) == QUANTAPDF_OK);
    std::unique_ptr<quantapdf_qpdf_document, decltype(&quantapdf_qpdf_close)> document(raw, quantapdf_qpdf_close);

    auto source = defaults;
    source.max_operations = 1u;
    source.max_source_pixels = source_pixels;
    source.max_drawn_pixels = 6u;
    quantapdf_ir ir{};
    const auto dispose_ir = std::unique_ptr<quantapdf_ir, decltype(&quantapdf_ir_dispose)>(&ir, quantapdf_ir_dispose);
    auto insufficient = source;
    --insufficient.max_source_pixels;
    CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &insufficient, &ir) == QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir));

    const auto phases = masked ? std::vector{15u, 14u, 5u, 16u} : std::vector{14u, 5u};
    for (unsigned phase : phases) {
        target = phase;
        observed = faults = 0u;
        armed = false;
        std::memset(&ir, 0xa5, sizeof(ir));
        CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir) == QUANTAPDF_ERROR_NOMEM && cleared(ir));
        CHECK(target == 0u && observed == phase && faults == 1u && !armed);
    }

    track_c_allocation = true;
    c_allocations = c_fail_index = c_faults = 0u;
    const auto loaded = quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir);
    track_c_allocation = false;
    const size_t allocations = c_allocations;
    CHECK(loaded == QUANTAPDF_OK && allocations > 0u);
    quantapdf_ir_dispose(&ir);
    for (size_t fail = 1u; fail <= allocations; ++fail) {
        track_c_allocation = true;
        c_fail_index = fail;
        c_allocations = c_faults = 0u;
        const auto status = quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir);
        track_c_allocation = false;
        c_fail_index = 0u;
        CHECK(status == QUANTAPDF_ERROR_NOMEM && cleared(ir) && c_allocations == fail && c_faults == 1u);
    }
    CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir) == QUANTAPDF_OK);
    document.reset();
    bytes.clear();
    CHECK(ir.operation_count == 1u && ir.resources.image_count == 1u);
    const auto& image = ir.resources.images[0];
    CHECK(image.format == (indexed ? QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW : QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW16) &&
        image.width == 2u && image.height == 3u && image.components == 3 && image.size == pixel_bytes);
    CHECK(bool(image.has_alpha) == masked && image.alpha_size == alpha_bytes);
    if (independent) {
        CHECK(image.mask_width == 4u && image.mask_height == 6u && !image.mask_interpolate);
        CHECK(image.alpha_data[2] == 0u && image.alpha_data[0] == 255u);

        unsigned char *encoded = nullptr;
        size_t encoded_size = 0u;
        CHECK(quantapdf_qpdf_compose(&ir, &encoded, &encoded_size) == QUANTAPDF_OK);
        const std::unique_ptr<unsigned char, decltype(&std::free)> encoded_owner(encoded, std::free);
        quantapdf_qpdf_document *roundtrip = nullptr;
        CHECK(quantapdf_qpdf_open_memory(encoded, encoded_size, "", &roundtrip) == QUANTAPDF_OK);
        const std::unique_ptr<quantapdf_qpdf_document, decltype(&quantapdf_qpdf_close)>
            reopened(roundtrip, quantapdf_qpdf_close);
        quantapdf_ir copy{};
        const auto dispose_copy = std::unique_ptr<quantapdf_ir, decltype(&quantapdf_ir_dispose)>(
            &copy, quantapdf_ir_dispose);
        CHECK(quantapdf_qpdf_page_to_ir(reopened.get(), 0u, &source, &copy) == QUANTAPDF_OK);
        CHECK(copy.operation_count == 1u && copy.resources.image_count == 1u);
        const auto& persisted = copy.resources.images[0];
        CHECK(persisted.mask_width == image.mask_width && persisted.mask_height == image.mask_height &&
            persisted.mask_interpolate == image.mask_interpolate && persisted.alpha_size == image.alpha_size &&
            std::memcmp(persisted.alpha_data, image.alpha_data, alpha_bytes) == 0);
    } else if (masked) {
        CHECK(image.alpha_data[1] == 0u && image.alpha_data[0] == 255u);
    }

    quantapdf_native_page_device device{};
    device.dpi = 72.0f;
    const quantapdf_ir_raster_scoped_mixed_v5_limits limits{6u, 1u, 32u, 8192u, source_pixels, 6u};
    quantapdf_native_prepared_page_v1 plan{};
    CHECK(quantapdf_native_prepare_page_v1(&ir, &limits, &device, &plan) == QUANTAPDF_OK);
    CHECK(plan.usage.retained_bytes == sizeof(*ir.pages) + sizeof(*ir.operations) + sizeof(image) + pixel_bytes + alpha_bytes);
    quantapdf_native_execution_receipt_v1 receipt{};
    quantapdf_ir_raster_image out{};
    const auto dispose_image = std::unique_ptr<quantapdf_ir_raster_image, decltype(&quantapdf_ir_raster_image_dispose)>(
        &out, quantapdf_ir_raster_image_dispose);
    CHECK(quantapdf_native_execute_page_v1(&plan, &receipt, &out) == QUANTAPDF_OK);
    CHECK(receipt.source_pixels == source_pixels && receipt.drawn_pixels == 6u && out.width == 2u && out.height == 3u && out.size == 24u);
    for (size_t i = 0u; i < 6u; ++i)
        CHECK(std::memcmp(out.pixels + 4u * i, reference.data() + 3u * i, 3u) == 0 && out.pixels[4u * i + 3u] == 255u);
    quantapdf_ir_raster_image_dispose(&out);

    plan.resource_limits.max_live_bytes = receipt.peak_live_bytes;
    CHECK(quantapdf_native_execute_page_v1(&plan, nullptr, &out) == QUANTAPDF_OK);
    quantapdf_ir_raster_image_dispose(&out);
    --plan.resource_limits.max_live_bytes;
    CHECK(quantapdf_native_execute_page_v1(&plan, nullptr, &out) == QUANTAPDF_ERROR_UNSUPPORTED && zero_bitmap(out));
    std::printf("%s image Source: %zu vector allocation faults, %zu publication faults, pixel and byte N/N-1\n",
        independent ? "Explicit-mask Indexed" : masked ? "Masked Indexed" : indexed ? "Indexed" : channels == 4u ? "CMYK" : "Wide",
        phases.size(), allocations);
    return EXIT_SUCCESS;
}

static int native_stencil_source_contract(const quantapdf_qpdf_page_limits& defaults)
{
    using namespace native_image_depth;
    specimen stencil;
    stencil.width = 2u;
    stencil.height = 1u;
    stencil.bits = 1u;
    stencil.samples = {0u, 1u};
    stencil.stencil_entries = "";
    const auto bytes = create(stencil, fault::none, "[0 0 2 1]",
        "BT ET 0 1 0 rg q 2 0 0 1 0 0 cm /I Do Q");
    quantapdf_qpdf_document *raw = nullptr;
    CHECK(quantapdf_qpdf_open_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
        bytes.size(), "", &raw) == QUANTAPDF_OK);
    const std::unique_ptr<quantapdf_qpdf_document, decltype(&quantapdf_qpdf_close)> document(raw, quantapdf_qpdf_close);
    quantapdf_ir ir{};
    const auto dispose_ir = std::unique_ptr<quantapdf_ir, decltype(&quantapdf_ir_dispose)>(&ir, quantapdf_ir_dispose);
    auto source = defaults;
    source.max_source_pixels = 2u;
    CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir) == QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir));
    ++source.max_source_pixels;
    for (unsigned phase : {15u, 14u}) {
        target = phase;
        observed = faults = 0u;
        armed = false;
        CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir) == QUANTAPDF_ERROR_NOMEM && cleared(ir));
        CHECK(target == 0u && observed == phase && faults == 1u && !armed);
    }
    CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir) == QUANTAPDF_OK);
    CHECK(ir.operation_count == 1u && ir.resources.image_count == 1u);
    const auto& image = ir.resources.images[0];
    const std::array<unsigned char, 3u> green{0u, 255u, 0u};
    CHECK(image.width == 1u && image.height == 1u && image.components == 3 && image.size == green.size() &&
        std::memcmp(image.data, green.data(), green.size()) == 0);
    CHECK(image.has_alpha && image.mask_width == 2u && image.mask_height == 1u &&
        image.alpha_size == 2u && image.alpha_data[0] == 255u && image.alpha_data[1] == 0u);
    return EXIT_SUCCESS;
}

enum class matte_fixture { none, direct, indexed };

static int native_soft_mask_source_contract(const quantapdf_qpdf_page_limits& defaults, matte_fixture kind)
{
    using namespace native_image_depth;
    const bool matte = kind != matte_fixture::none;
    const bool indexed = kind == matte_fixture::indexed;
    specimen base;
    base.height = 1u;
    base.width = matte ? 2u : 1u;
    base.samples = matte ? std::vector{170u, 255u} : std::vector{255u};
    base.rotation = 90;
    const indexed_palette palette{"/DeviceGray", 2u, std::string("\x00\xaa\xff", 3u)};
    if (indexed) {
        base.bits = 2u;
        base.samples = {1u, 2u};
    }
    const char *entries = indexed ? "/Interpolate true /Matte [2]" :
        matte ? "/Interpolate true /Matte [1]" : "/Interpolate true";
    const image_mask mask{2u, 1u, matte ? std::string("\x55\x55\xaa\xaa", 4u) : std::string("\x00\x81\x80\x00", 4u),
        entries, true, 16u};
    const auto pdf = create(base, fault::none, "[0 0 2 1]", "q 2 0 0 1 0 0 cm /I Do Q", indexed ? &palette : nullptr, &mask);
    quantapdf_qpdf_document *raw = nullptr;
    CHECK(quantapdf_qpdf_open_memory(reinterpret_cast<const unsigned char*>(pdf.data()),
        pdf.size(), "", &raw) == QUANTAPDF_OK);
    const std::unique_ptr<quantapdf_qpdf_document, decltype(&quantapdf_qpdf_close)> document(raw, quantapdf_qpdf_close);
    quantapdf_ir ir{};
    const auto dispose_ir = std::unique_ptr<quantapdf_ir, decltype(&quantapdf_ir_dispose)>(&ir, quantapdf_ir_dispose);
    auto source = defaults;
    source.max_source_pixels = base.width + mask.width - 1u;
    CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir) == QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir));
    ++source.max_source_pixels;
    for (unsigned phase : matte ? std::vector{15u, 14u, 16u} : std::vector{15u, 16u}) {
        target = phase;
        observed = faults = 0u;
        armed = false;
        CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir) == QUANTAPDF_ERROR_NOMEM && cleared(ir));
        CHECK(target == 0u && observed == phase && faults == 1u && !armed);
    }
    CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &source, &ir) == QUANTAPDF_OK);
    CHECK(ir.operation_count == 1u && ir.resources.image_count == 1u);
    const auto& original = ir.resources.images[0];
    CHECK(original.mask_width == 1u && original.mask_height == 2u && original.mask_bits == 16u &&
        original.mask_interpolate && original.alpha_size == 4u &&
        std::memcmp(original.alpha_data, mask.bits.data(), mask.bits.size()) == 0);

    if (matte) {
        const std::array<unsigned char, 4u> normalized{0u, 0u, 255u, 255u};
        CHECK(original.format == QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW16 &&
            original.width == 1u && original.height == 2u && original.size == normalized.size() &&
            std::memcmp(original.data, normalized.data(), normalized.size()) == 0);
    }

    unsigned char *encoded = nullptr;
    size_t encoded_size = 0u;
    CHECK(quantapdf_qpdf_compose(&ir, &encoded, &encoded_size) == QUANTAPDF_OK);
    const std::unique_ptr<unsigned char, decltype(&std::free)> encoded_owner(encoded, std::free);
    quantapdf_qpdf_document *roundtrip = nullptr;
    CHECK(quantapdf_qpdf_open_memory(encoded, encoded_size, "", &roundtrip) == QUANTAPDF_OK);
    const std::unique_ptr<quantapdf_qpdf_document, decltype(&quantapdf_qpdf_close)> reopened(roundtrip, quantapdf_qpdf_close);
    quantapdf_ir copy{};
    const auto dispose_copy = std::unique_ptr<quantapdf_ir, decltype(&quantapdf_ir_dispose)>(&copy, quantapdf_ir_dispose);
    CHECK(quantapdf_qpdf_page_to_ir(reopened.get(), 0u, &source, &copy) == QUANTAPDF_OK);
    CHECK(copy.operation_count == 1u && copy.resources.image_count == 1u);
    const auto& persisted = copy.resources.images[0];
    CHECK(persisted.format == original.format && persisted.size == original.size &&
        std::memcmp(persisted.data, original.data, original.size) == 0);
    CHECK(persisted.mask_width == original.mask_width && persisted.mask_height == original.mask_height &&
        persisted.mask_bits == original.mask_bits && persisted.mask_interpolate == original.mask_interpolate &&
        persisted.alpha_size == original.alpha_size && std::memcmp(persisted.alpha_data, original.alpha_data, original.alpha_size) == 0);
    return EXIT_SUCCESS;
}

static int native_image_depth_source_contracts(const quantapdf_qpdf_page_limits& defaults)
{
    for (unsigned channels : {3u, 1u, 4u})
        CHECK(native_image_depth_source_contract(defaults, channels) == EXIT_SUCCESS);
    CHECK(native_image_depth_source_contract(defaults, 1u, true) == EXIT_SUCCESS);
    CHECK(native_image_depth_source_contract(defaults, 1u, true, true) == EXIT_SUCCESS);
    CHECK(native_stencil_source_contract(defaults) == EXIT_SUCCESS);
    CHECK(native_soft_mask_source_contract(defaults, matte_fixture::none) == EXIT_SUCCESS);
    CHECK(native_soft_mask_source_contract(defaults, matte_fixture::direct) == EXIT_SUCCESS);
    CHECK(native_soft_mask_source_contract(defaults, matte_fixture::indexed) == EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
#endif
