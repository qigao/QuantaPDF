#ifndef QUANTAPDF_NATIVE_IMAGE_DEPTH_OOM_CASES_H
#define QUANTAPDF_NATIVE_IMAGE_DEPTH_OOM_CASES_H

#include "native_image_depth_fixture.h"

static int native_image_depth_source_contract(const quantapdf_qpdf_page_limits& defaults, unsigned channels)
{
    using namespace native_image_depth;
    const bool indexed = channels == 1u;
    auto specimen = grid(4u, channels, storage::png);
    specimen.rotation = 90;
    const indexed_palette palette{"/DeviceRGB", 2u, std::string("\x11\x21\x31\x42\x52\x62\x73\x83\x93", 9u)};
    auto bytes = create(specimen, fault::none, "[0 0 3 2]", "q 3 0 0 2 0 0 cm /I Do Q",
        indexed ? &palette : nullptr);
    const auto reference = indexed ? indexed_rgb(specimen, palette, {0u, 2u, 2u, 1u, 2u, 2u}, true) :
        rgb(specimen, true);
    const size_t pixel_bytes = indexed ? 18u : 36u;
    quantapdf_qpdf_document *raw = nullptr;
    CHECK(quantapdf_qpdf_open_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
        bytes.size(), "", &raw) == QUANTAPDF_OK);
    std::unique_ptr<quantapdf_qpdf_document, decltype(&quantapdf_qpdf_close)> document(raw, quantapdf_qpdf_close);

    auto source = defaults;
    source.max_operations = 1u;
    source.max_source_pixels = source.max_drawn_pixels = 6u;
    quantapdf_ir ir{};
    const auto dispose_ir = std::unique_ptr<quantapdf_ir, decltype(&quantapdf_ir_dispose)>(&ir, quantapdf_ir_dispose);
    auto insufficient = source;
    --insufficient.max_source_pixels;
    CHECK(quantapdf_qpdf_page_to_ir(document.get(), 0u, &insufficient, &ir) == QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir));

    for (unsigned phase : {14u, 5u}) {
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

    quantapdf_native_page_device device{};
    device.dpi = 72.0f;
    const quantapdf_ir_raster_scoped_mixed_v5_limits limits{6u, 1u, 32u, 8192u, 6u, 6u};
    quantapdf_native_prepared_page_v1 plan{};
    CHECK(quantapdf_native_prepare_page_v1(&ir, &limits, &device, &plan) == QUANTAPDF_OK);
    CHECK(plan.usage.retained_bytes == sizeof(*ir.pages) + sizeof(*ir.operations) + sizeof(image) + pixel_bytes);
    quantapdf_native_execution_receipt_v1 receipt{};
    quantapdf_ir_raster_image out{};
    const auto dispose_image = std::unique_ptr<quantapdf_ir_raster_image, decltype(&quantapdf_ir_raster_image_dispose)>(
        &out, quantapdf_ir_raster_image_dispose);
    CHECK(quantapdf_native_execute_page_v1(&plan, &receipt, &out) == QUANTAPDF_OK);
    CHECK(receipt.source_pixels == 6u && receipt.drawn_pixels == 6u && out.width == 2u && out.height == 3u && out.size == 24u);
    for (size_t i = 0u; i < 6u; ++i)
        CHECK(std::memcmp(out.pixels + 4u * i, reference.data() + 3u * i, 3u) == 0 && out.pixels[4u * i + 3u] == 255u);
    quantapdf_ir_raster_image_dispose(&out);

    plan.resource_limits.max_live_bytes = receipt.peak_live_bytes;
    CHECK(quantapdf_native_execute_page_v1(&plan, nullptr, &out) == QUANTAPDF_OK);
    quantapdf_ir_raster_image_dispose(&out);
    --plan.resource_limits.max_live_bytes;
    CHECK(quantapdf_native_execute_page_v1(&plan, nullptr, &out) == QUANTAPDF_ERROR_UNSUPPORTED && zero_bitmap(out));
    std::printf("%s image Source: two vector allocation faults, %zu publication faults, pixel and byte N/N-1\n",
        indexed ? "Indexed" : channels == 4u ? "CMYK" : "Wide", allocations);
    return EXIT_SUCCESS;
}

static int native_image_depth_source_contracts(const quantapdf_qpdf_page_limits& defaults)
{
    for (unsigned channels : {3u, 1u, 4u})
        CHECK(native_image_depth_source_contract(defaults, channels) == EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
#endif
