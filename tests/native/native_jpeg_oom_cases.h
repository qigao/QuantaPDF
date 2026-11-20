#ifndef QUANTAPDF_NATIVE_JPEG_OOM_CASES_H
#define QUANTAPDF_NATIVE_JPEG_OOM_CASES_H

#include "backend/qpdf_composer.h"
#include "native_jpeg_fixture.h"

static int native_jpeg_source_contracts(const quantapdf_qpdf_page_limits& defaults)
{
    using namespace native_jpeg;
    size_t publication_faults = 0u, raster_faults = 0u;
    for (const auto storage : {container::name, container::array, container::wrapped}) {
        const auto& spec = cases[storage == container::name ? 0u : 5u];
        auto original = create(spec, storage, true);
        quantapdf_qpdf_document *document = nullptr;
        CHECK(quantapdf_qpdf_open_memory(reinterpret_cast<const unsigned char*>(original.bytes.data()),
            original.bytes.size(), "reader", &document) == QUANTAPDF_OK);
        std::unique_ptr<quantapdf_qpdf_document, decltype(&quantapdf_qpdf_close)> owner(document, quantapdf_qpdf_close);
        auto caps = defaults;
        caps.max_operations = original.operations;
        caps.max_source_pixels = original.images * source_pixels;
        const auto read = [&](const quantapdf_qpdf_page_limits& limits, quantapdf_ir& ir) {
            return quantapdf_qpdf_page_to_ir(owner.get(), 1u, &limits, &ir);
        };

        quantapdf_ir ir{};
        auto smaller = caps;
        --smaller.max_source_pixels;
        std::memset(&ir, 0xa5, sizeof(ir));
        CHECK(read(smaller, ir) == QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir));
        for (int phase : {1, 2}) {
            std::memset(&ir, 0xa5, sizeof(ir));
            quantapdf_jpeg_force_oom_for_testing(phase);
            const auto status = read(caps, ir);
            quantapdf_jpeg_force_oom_for_testing(0);
            CHECK(status == QUANTAPDF_ERROR_NOMEM && cleared(ir));
        }
        target = 13u;
        observed = faults = 0u;
        armed = false;
        std::memset(&ir, 0xa5, sizeof(ir));
        CHECK(read(caps, ir) == QUANTAPDF_ERROR_NOMEM && cleared(ir));
        CHECK(target == 0u && observed == 13u && faults == 1u && !armed);

        track_c_allocation = true;
        c_allocations = c_fail_index = c_faults = 0u;
        const auto loaded = read(caps, ir);
        track_c_allocation = false;
        const size_t allocations = c_allocations;
        CHECK(loaded == QUANTAPDF_OK && allocations > 0u &&
            ir.operation_count == original.operations && ir.resources.image_count == original.images);
        quantapdf_ir_dispose(&ir);
        for (size_t fail = 1u; fail <= allocations; ++fail) {
            track_c_allocation = true;
            c_fail_index = fail;
            c_allocations = c_faults = 0u;
            std::memset(&ir, 0xa5, sizeof(ir));
            const auto status = read(caps, ir);
            track_c_allocation = false;
            c_fail_index = 0u;
            CHECK(status == QUANTAPDF_ERROR_NOMEM && cleared(ir) &&
                c_allocations == fail && c_faults == 1u);
            ++publication_faults;
        }

        CHECK(read(caps, ir) == QUANTAPDF_OK);
        owner.reset();
        original.bytes.assign(original.bytes.size(), 'x');

        const native_clip_graph::dpi_rate dpi{147, 2};
        const auto rgb = native_clip_graph::rgb(original, dpi);
        const auto snapshot = text_ir_snapshot(ir);
        const size_t w = native_clip_graph::width(original, dpi);
        const size_t h = native_clip_graph::height(original, dpi);
        quantapdf_native_page_device device{};
        device.dpi = float(dpi);
        const quantapdf_ir_raster_scoped_mixed_v5_limits limits = {
            w * h, original.operations, 512u, 8000000u, caps.max_source_pixels, 4096u};
        quantapdf_native_prepared_page_v1 metadata{};
        CHECK(quantapdf_native_prepare_page_v1(&ir, &limits, &device, &metadata) == QUANTAPDF_OK);
        m2a_malloc_fault_state calls{};
        metadata.malloc_fault = {&calls, m2a_malloc_fault_callback};
        quantapdf_native_execution_receipt_v1 receipt{};
        quantapdf_ir_raster_image output{};
        CHECK(quantapdf_native_execute_page_v1(&metadata, &receipt, &output) == QUANTAPDF_OK);
        CHECK(receipt.source_pixels == caps.max_source_pixels && calls.attempts > 0u &&
            output.width == w && output.height == h && output.size == w * h * 4u);
        for (size_t i = 0u; i < w * h; ++i)
            CHECK(std::memcmp(output.pixels + 4u * i, rgb.data() + 3u * i, 3u) == 0 &&
                output.pixels[4u * i + 3u] == 255u);
        quantapdf_ir_raster_image_dispose(&output);
        for (size_t fail = 1u; fail <= calls.attempts; ++fail) {
            m2a_malloc_fault_state fault{};
            fault.fail_on = fail;
            metadata.malloc_fault = {&fault, m2a_malloc_fault_callback};
            CHECK(quantapdf_native_execute_page_v1(&metadata, nullptr, &output) ==
                QUANTAPDF_ERROR_NOMEM && zero_bitmap(output));
            CHECK(fault.attempts == fail && fault.failures == 1u && text_ir_snapshot(ir) == snapshot);
            ++raster_faults;
        }
        quantapdf_ir_dispose(&ir);
        CHECK(cleared(ir));
    }
    std::printf("JPEG Source: 3 detached RGBA results, 3 exact source budgets, "
        "6 libjpeg failures, 3 decoded buffer failures, %zu publication failures and "
        "%zu C11 allocation failures\n", publication_faults, raster_faults);
    return EXIT_SUCCESS;
}

#endif
