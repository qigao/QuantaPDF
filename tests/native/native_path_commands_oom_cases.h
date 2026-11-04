#ifndef QUANTAPDF_NATIVE_PATH_COMMANDS_OOM_CASES_H
#define QUANTAPDF_NATIVE_PATH_COMMANDS_OOM_CASES_H
#include "native_path_commands_fixture.h"

static int path_commands_source_contracts(const quantapdf_qpdf_page_limits& defaults,bool multi=false,bool boundaries=false)
{
    size_t growth_faults=0u,publications=0u,raster_faults=0u,quotas=0u;
    struct shape {size_t repeats,zeros;unsigned primitive,clip;bool restore;unsigned layout;};
    const shape shapes[]={{3u,0u,0u,0u,false,0u},{65u,0u,1u,1u,true,1u},
        {17u,0u,2u,4u,false,2u},{17u,0u,3u,2u,true,0u},
        {3u,8192u,0u,1u,true,0u},{128u,0u,1u,0u,false,1u},{65u,0u,0u,4u,true,0u},
        {17u,0u,1u,5u,true,3u}};
    for(size_t index=boundaries?7u:0u;index<(boundaries?8u:multi?7u:6u);++index)for(bool encrypted:{false,true})
    for(unsigned rule=0u;rule<(multi?2u:1u);++rule) {
        const auto& shape=shapes[index];const bool even=rule!=0u;
        auto sample=multi?native_path_commands::create_contours(shape.repeats,shape.primitive,shape.clip,even,shape.restore,encrypted,shape.zeros,shape.layout,shape.primitive%4u):
            native_path_commands::create(shape.repeats,shape.primitive,shape.clip,false,shape.restore,encrypted,shape.zeros);
        quantapdf_qpdf_document *document=nullptr;
        CHECK(quantapdf_qpdf_open_memory(reinterpret_cast<const unsigned char*>(sample.bytes.data()),sample.bytes.size(),encrypted?"reader":"",&document)==QUANTAPDF_OK);
        std::unique_ptr<quantapdf_qpdf_document,decltype(&quantapdf_qpdf_close)> owner(document,quantapdf_qpdf_close);
        auto caps=defaults;caps.max_content_bytes=sample.decoded_bytes;caps.max_content_streams=3u;caps.max_operations=sample.operations;
        const auto read=[&](const quantapdf_qpdf_page_limits& limits,quantapdf_ir& ir) {return quantapdf_qpdf_page_to_ir(owner.get(),1u,&limits,&ir);};
        path_growths=0u;path_fail_growth=0u;
        track_c_allocation=true;c_allocations=0u;c_fail_index=0u;c_faults=0u;
        quantapdf_ir ir{};const auto status=read(caps,ir);track_c_allocation=false;
        const size_t allocations=c_allocations,growths=path_growths;
        if(status!=QUANTAPDF_OK)std::fprintf(stderr,"long Source repeats=%zu primitive=%u clip=%u status=%d\n",shape.repeats,shape.primitive,shape.clip,int(status));
        CHECK(status==QUANTAPDF_OK && growths>0u && allocations>0u && ir.operation_count==sample.operations && ir.operations[0].value.path.command_count==sample.commands);
        quantapdf_ir_dispose(&ir);
        for(size_t fail=1u;fail<=growths;++fail) {
            path_growths=0u;path_fail_growth=fail;observed=0u;faults=0u;armed=false;
            std::memset(&ir,0xa5,sizeof(ir));
            CHECK(read(caps,ir)==QUANTAPDF_ERROR_NOMEM && cleared(ir) && path_growths==fail && path_fail_growth==0u && observed==12u && faults==1u && !armed);++growth_faults;
        }
        for(size_t fail=1u;fail<=allocations;++fail) {
            track_c_allocation=true;c_fail_index=fail;c_allocations=0u;c_faults=0u;
            std::memset(&ir,0xa5,sizeof(ir));const auto error=read(caps,ir);track_c_allocation=false;c_fail_index=0u;
            CHECK(error==QUANTAPDF_ERROR_NOMEM && cleared(ir) && c_allocations==fail && c_faults==1u);++publications;
        }
        for(unsigned quota:{0u,1u,2u}) {
            auto limited=caps;
            if(quota==0u)--limited.max_content_bytes;
            if(quota==1u)--limited.max_content_streams;
            if(quota==2u)--limited.max_operations;
            CHECK(read(limited,ir)==QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir));++quotas;
        }
        CHECK(read(caps,ir)==QUANTAPDF_OK);owner.reset();sample.bytes.assign(sample.bytes.size(),'x');
        const auto snapshot=text_ir_snapshot(ir),expected=native_path_commands::rgb(sample,96u);
        const auto equal=[&](const quantapdf_ir_raster_image& image) {
            if(image.width!=16u || image.height!=16u || image.size!=1024u)return false;
            for(size_t i=0u;i<256u;++i)if(std::memcmp(image.pixels+4u*i,expected.data()+3u*i,3u)!=0 || image.pixels[4u*i+3u]!=255u)return false;
            return true;
        };
        const quantapdf_ir_raster_scoped_mixed_v5_limits budget={1024u,sample.operations,512u,8000000u,1u,1u};
        quantapdf_native_page_device device{};device.dpi=96.0f;quantapdf_native_prepared_page_v1 metadata{};
        CHECK(quantapdf_native_prepare_page_v1(&ir,&budget,&device,&metadata)==QUANTAPDF_OK && metadata.usage.commands>=sample.commands);
        m2a_malloc_fault_state baseline{};metadata.malloc_fault={&baseline,m2a_malloc_fault_callback};
        quantapdf_native_execution_receipt_v1 receipt{};quantapdf_ir_raster_image output{};
        CHECK(quantapdf_native_execute_page_v1(&metadata,&receipt,&output)==QUANTAPDF_OK && equal(output) && receipt.sample_tests_exact && baseline.attempts>0u);
        CHECK(receipt.source_pixels==0u && receipt.drawn_pixels==0u);quantapdf_ir_raster_image_dispose(&output);
        for(size_t fail=1u;fail<=baseline.attempts;++fail) {
            m2a_malloc_fault_state allocation{};allocation.fail_on=fail;
            auto candidate=metadata;candidate.malloc_fault={&allocation,m2a_malloc_fault_callback};
            quantapdf_native_execution_receipt_v1 denied{},zero{};std::memset(&output,0xa5,sizeof(output));
            CHECK(quantapdf_native_execute_page_v1(&candidate,&denied,&output)==QUANTAPDF_ERROR_NOMEM && zero_bitmap(output) && std::memcmp(&denied,&zero,sizeof(zero))==0 && allocation.attempts==fail && allocation.failures==1u && text_ir_snapshot(ir)==snapshot);++raster_faults;
        }
        const auto trial=[&](const quantapdf_ir_raster_scoped_mixed_v5_limits& bounds,size_t memory,quantapdf_status wanted) {
            quantapdf_native_prepared_page_v1 candidate{};quantapdf_ir_raster_image rendered{};
            auto error=quantapdf_native_prepare_page_v1(&ir,&bounds,&device,&candidate);
            if(error==QUANTAPDF_OK) {if(memory!=0u)candidate.resource_limits.max_live_bytes=memory;error=quantapdf_native_execute_page_v1(&candidate,nullptr,&rendered);}
            const bool valid=error==wanted && text_ir_snapshot(ir)==snapshot && (wanted==QUANTAPDF_OK?equal(rendered):zero_bitmap(rendered));
            quantapdf_ir_raster_image_dispose(&rendered);return valid;
        };
        CHECK(trial(budget,receipt.peak_live_bytes,QUANTAPDF_OK));
        CHECK(trial(budget,receipt.peak_live_bytes-1u,QUANTAPDF_ERROR_UNSUPPORTED));++quotas;
        for(unsigned quota:{0u,1u,2u,3u}) {
            auto bounds=budget;
            if(quota==0u)bounds.max_operations=sample.operations;
            if(quota==1u)bounds.max_edges=receipt.compiled_edges;
            if(quota==2u)bounds.max_sample_tests=receipt.sample_tests;
            if(quota==3u)bounds.max_pixels=256u;
            CHECK(trial(bounds,0u,QUANTAPDF_OK));
            if(quota==0u)--bounds.max_operations;
            if(quota==1u)--bounds.max_edges;
            if(quota==2u)--bounds.max_sample_tests;
            if(quota==3u)--bounds.max_pixels;
            CHECK(trial(bounds,0u,QUANTAPDF_ERROR_UNSUPPORTED));++quotas;
        }
        // Compilation must deep-copy every command, including the tail past
        // the old short-path and aggregate snapshot gates.
        metadata.malloc_fault={};quantapdf_native_compiled_page_v1 compiled{};
        CHECK(quantapdf_native_compiled_page_compile_v1(&metadata,&compiled)==QUANTAPDF_OK);
        auto& path=ir.operations[0].value.path;
        auto *commands=const_cast<quantapdf_composer_path_command*>(path.commands);
        const std::vector<quantapdf_composer_path_command> saved(commands,commands+path.command_count);
        for(size_t i=0u;i<path.command_count;++i) {commands[i].point1={0.0f,0.0f};commands[i].point2={0.0f,0.0f};commands[i].point3={0.0f,0.0f};}
        const auto state=ir.operations[0].graphics_state_id;ir.operations[0].graphics_state_id=UINT32_MAX;
        CHECK(quantapdf_native_compiled_page_execute_v1(&compiled,nullptr,&output)==QUANTAPDF_OK && equal(output));
        std::copy(saved.begin(),saved.end(),commands);ir.operations[0].graphics_state_id=state;CHECK(text_ir_snapshot(ir)==snapshot);
        quantapdf_native_compiled_page_dispose_v1(&compiled);quantapdf_ir_dispose(&ir);CHECK(cleared(ir));
        CHECK(equal(output));quantapdf_ir_raster_image_dispose(&output);CHECK(zero_bitmap(output));
    }
    std::printf("%s Fill paths: %zu vector growth faults, %zu Source publication faults, %zu C11 allocation faults, %zu N/N-1 checks; independent RGBA and deep snapshots survive owner release\n",boundaries?"Boundary":multi?"Multi-contour":"Dynamic",growth_faults,publications,raster_faults,quotas);
    return EXIT_SUCCESS;
}
#endif
