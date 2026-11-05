#ifndef QUANTAPDF_NATIVE_CLIP_GRAPH_OOM_CASES_H
#define QUANTAPDF_NATIVE_CLIP_GRAPH_OOM_CASES_H
#include "native_clip_graph_fixture.h"

static int clip_graph_source_contracts(const quantapdf_qpdf_page_limits& defaults,bool wide=false)
{
    size_t clip_faults=0u,path_faults=0u,stack_faults=0u,publications=0u,raster_faults=0u,quotas=0u;
    target=0u;armed=false;
    const unsigned wide_leaves[6]={5u,9u,17u,65u,129u,257u};
    for(unsigned iteration=0u;iteration<(wide?6u:4u);++iteration) {
        const unsigned scene=wide?0u:iteration;
        native_clip_graph::options opt;opt.scene=scene;opt.order=scene;
        opt.leaves=scene==1u?3u:4u;opt.primitive=scene+1u;opt.empty=scene==1u;
        opt.prefix=scene%2u==0u;opt.depth=129u;
        opt.unused=scene==3u?257u:0u;opt.contours=scene==2u?65u:1u;opt.zeros=scene==2u?8192u:0u;
        if(wide) {opt.leaves=wide_leaves[iteration];opt.sparse=true;opt.primitive=iteration%5u;opt.order=iteration;}
        const bool encrypted=iteration%2u!=0u;auto sample=native_clip_graph::create(opt,encrypted);
        quantapdf_qpdf_document *document=nullptr;
        CHECK(quantapdf_qpdf_open_memory(reinterpret_cast<const unsigned char*>(sample.bytes.data()),sample.bytes.size(),encrypted?"reader":"",&document)==QUANTAPDF_OK);
        std::unique_ptr<quantapdf_qpdf_document,decltype(&quantapdf_qpdf_close)> owner(document,quantapdf_qpdf_close);
        auto caps=defaults;caps.max_content_bytes=sample.decoded_bytes;caps.max_content_streams=3u;
        caps.max_operations=sample.operations;caps.max_source_pixels=4u*sample.images;caps.max_drawn_pixels=3u*sample.images;
        const auto read=[&](const quantapdf_qpdf_page_limits& limits,quantapdf_ir& ir) {return quantapdf_qpdf_page_to_ir(owner.get(),1u,&limits,&ir);};
        graphics_growths=path_growths=clip_growths=0u;
        graphics_fail_growth=path_fail_growth=clip_fail_growth=0u;
        track_c_allocation=true;c_allocations=0u;c_fail_index=0u;c_faults=0u;
        quantapdf_ir ir{};const auto status=read(caps,ir);track_c_allocation=false;
        const size_t c_calls=c_allocations,clip_grows=clip_growths,path_grows=path_growths,stack_grows=graphics_growths;
        if(status!=QUANTAPDF_OK)std::fprintf(stderr,"Source Clip graph scene=%u status=%d\n",scene,int(status));
        CHECK(status==QUANTAPDF_OK && ir.operation_count==sample.operations &&
            ir.resources.clip_count==sample.nodes && ir.resources.graphics_state_count==sample.states &&
            c_calls>0u && clip_grows>0u && path_grows>0u && stack_grows>0u);
        quantapdf_ir_dispose(&ir);
        for(size_t fail=1u;fail<=clip_grows;++fail) {
            clip_growths=0u;clip_fail_growth=fail;observed=0u;faults=0u;armed=false;
            std::memset(&ir,0xa5,sizeof(ir));
            CHECK(read(caps,ir)==QUANTAPDF_ERROR_NOMEM && cleared(ir) && clip_growths==fail &&
                clip_fail_growth==0u && observed==9u && faults==1u && !armed);++clip_faults;
        }
        std::vector<size_t> positions;
        if(opt.unused!=0u || (wide && opt.leaves>17u))positions={1u,(path_grows+1u)/2u,path_grows};
        else for(size_t i=1u;i<=path_grows;++i)positions.push_back(i);
        for(size_t fail:positions) {
            path_growths=0u;path_fail_growth=fail;observed=0u;faults=0u;armed=false;
            std::memset(&ir,0xa5,sizeof(ir));
            CHECK(read(caps,ir)==QUANTAPDF_ERROR_NOMEM && cleared(ir) && path_growths==fail &&
                path_fail_growth==0u && observed==12u && faults==1u && !armed);++path_faults;
        }
        for(size_t fail=1u;fail<=stack_grows;++fail) {
            graphics_growths=0u;graphics_fail_growth=fail;observed=0u;faults=0u;armed=false;
            std::memset(&ir,0xa5,sizeof(ir));
            CHECK(read(caps,ir)==QUANTAPDF_ERROR_NOMEM && cleared(ir) && graphics_growths==fail &&
                graphics_fail_growth==0u && observed==11u && faults==1u && !armed);++stack_faults;
        }
        std::vector<size_t> c_positions;
        if(wide && opt.leaves>17u)c_positions={1u,(c_calls+1u)/2u,c_calls};
        else for(size_t i=1u;i<=c_calls;++i)c_positions.push_back(i);
        for(size_t fail:c_positions) {
            track_c_allocation=true;c_fail_index=fail;c_allocations=0u;c_faults=0u;
            std::memset(&ir,0xa5,sizeof(ir));const auto error=read(caps,ir);track_c_allocation=false;c_fail_index=0u;
            CHECK(error==QUANTAPDF_ERROR_NOMEM && cleared(ir) && c_allocations==fail && c_faults==1u);++publications;
        }
        for(unsigned quota=0u;quota<5u;++quota) {
            auto bounds=caps;
            if(quota==0u)--bounds.max_content_bytes;
            if(quota==1u)--bounds.max_content_streams;
            if(quota==2u)--bounds.max_operations;
            if(quota==3u)--bounds.max_source_pixels;
            if(quota==4u)--bounds.max_drawn_pixels;
            CHECK(read(bounds,ir)==QUANTAPDF_ERROR_UNSUPPORTED && cleared(ir));++quotas;
        }
        CHECK(read(caps,ir)==QUANTAPDF_OK);owner.reset();sample.bytes.assign(sample.bytes.size(),'x');
        const auto snapshot=text_ir_snapshot(ir),expected=native_clip_graph::rgb(sample,96u);
        if(wide && opt.leaves>=65u) {
            auto truncated=sample;
            for(auto& p:truncated.paints)if(p.clips.size()==opt.leaves)p.clips.pop_back();
            CHECK(expected!=native_clip_graph::rgb(truncated,96u));
        }
        const auto equal=[&](const quantapdf_ir_raster_image& image) {
            if(image.width!=16u || image.height!=16u || image.size!=1024u)return false;
            for(size_t i=0u;i<256u;++i)if(std::memcmp(image.pixels+4u*i,expected.data()+3u*i,3u)!=0 || image.pixels[4u*i+3u]!=255u)return false;
            return true;
        };
        const quantapdf_ir_raster_scoped_mixed_v5_limits budget={1024u,sample.operations,wide?2048u:512u,8000000u,4u*sample.images,12u*sample.images};
        quantapdf_native_page_device device{};device.dpi=96.0f;
        quantapdf_native_prepared_page_v1 metadata{};
        CHECK(quantapdf_native_prepare_page_v1(&ir,&budget,&device,&metadata)==QUANTAPDF_OK &&
            metadata.usage.glyphs==sample.glyphs && metadata.usage.clip_leaves==sample.leaves &&
            metadata.usage.clips_used==sample.nodes && metadata.usage.states_used==sample.states);
        m2a_malloc_fault_state allocations{};metadata.malloc_fault={&allocations,m2a_malloc_fault_callback};
        quantapdf_native_execution_receipt_v1 receipt{};quantapdf_ir_raster_image output{};
        const auto rendered=quantapdf_native_execute_page_v1(&metadata,&receipt,&output);
        if(rendered!=QUANTAPDF_OK || !equal(output))std::fprintf(stderr,"C11 Clip graph scene=%u status=%d RGB=%d\n",scene,int(rendered),int(rendered==QUANTAPDF_OK && equal(output)));
        CHECK(rendered==QUANTAPDF_OK && equal(output) && receipt.sample_tests_exact && allocations.attempts>0u);
        quantapdf_ir_raster_image_dispose(&output);
        for(size_t fail=1u;fail<=allocations.attempts;++fail) {
            m2a_malloc_fault_state fault{};fault.fail_on=fail;
            auto candidate=metadata;candidate.malloc_fault={&fault,m2a_malloc_fault_callback};
            quantapdf_native_execution_receipt_v1 denied{},zero{};std::memset(&output,0xa5,sizeof(output));
            CHECK(quantapdf_native_execute_page_v1(&candidate,&denied,&output)==QUANTAPDF_ERROR_NOMEM && zero_bitmap(output) &&
                std::memcmp(&denied,&zero,sizeof(zero))==0 && fault.attempts==fail && fault.failures==1u && text_ir_snapshot(ir)==snapshot);++raster_faults;
        }
        const auto trial=[&](const quantapdf_ir_raster_scoped_mixed_v5_limits& limits,size_t bytes,quantapdf_status wanted) {
            quantapdf_native_prepared_page_v1 candidate{};quantapdf_ir_raster_image image{};
            auto error=quantapdf_native_prepare_page_v1(&ir,&limits,&device,&candidate);
            if(error==QUANTAPDF_OK) {if(bytes!=0u)candidate.resource_limits.max_live_bytes=bytes;error=quantapdf_native_execute_page_v1(&candidate,nullptr,&image);}
            const bool ok=error==wanted && text_ir_snapshot(ir)==snapshot && (wanted==QUANTAPDF_OK?equal(image):zero_bitmap(image));
            quantapdf_ir_raster_image_dispose(&image);return ok;
        };
        CHECK(trial(budget,receipt.peak_live_bytes,QUANTAPDF_OK));
        CHECK(trial(budget,receipt.peak_live_bytes-1u,QUANTAPDF_ERROR_UNSUPPORTED));++quotas;
        for(unsigned quota=0u;quota<6u;++quota) {
            auto limits=budget;
            if(quota==0u)limits.max_operations=sample.operations;
            if(quota==1u)limits.max_edges=receipt.compiled_edges;
            if(quota==2u)limits.max_sample_tests=receipt.sample_tests;
            if(quota==3u)limits.max_pixels=256u;
            if(quota==4u)limits.max_source_pixels=receipt.source_pixels;
            if(quota==5u)limits.max_drawn_pixels=receipt.drawn_pixels;
            CHECK(trial(limits,0u,QUANTAPDF_OK));
            if(quota==0u)--limits.max_operations;
            if(quota==1u)--limits.max_edges;
            if(quota==2u)--limits.max_sample_tests;
            if(quota==3u)--limits.max_pixels;
            if(quota==4u)--limits.max_source_pixels;
            if(quota==5u)--limits.max_drawn_pixels;
            CHECK(trial(limits,0u,QUANTAPDF_ERROR_UNSUPPORTED));++quotas;
        }
        metadata.malloc_fault={};quantapdf_native_compiled_page_v1 compiled{};
        CHECK(quantapdf_native_compiled_page_compile_v1(&metadata,&compiled)==QUANTAPDF_OK);
        std::vector<std::vector<quantapdf_composer_path_command>> commands(ir.resources.clip_count);
        std::vector<std::vector<quantapdf_composer_clip_id>> members(ir.resources.clip_count);
        const std::vector<quantapdf_composer_operation> operations(ir.operations,ir.operations+ir.operation_count);
        const std::vector<quantapdf_composer_graphics_state> states(ir.resources.graphics_states,
            ir.resources.graphics_states+ir.resources.graphics_state_count);
        for(size_t i=0u;i<ir.resources.clip_count;++i) {
            auto& clip=ir.resources.clips[i];
            if(clip.command_count!=0u) {
                commands[i].assign(clip.commands,clip.commands+clip.command_count);
                auto *points=const_cast<quantapdf_composer_path_command *>(clip.commands);
                for(size_t j=0u;j<clip.command_count;++j) {points[j].point1={0,0};points[j].point2={0,0};points[j].point3={0,0};}
            }
            if(clip.member_count!=0u) {
                members[i].assign(clip.members,clip.members+clip.member_count);
                auto *ids=const_cast<quantapdf_composer_clip_id *>(clip.members);
                std::fill(ids,ids+clip.member_count,UINT32_MAX);
            }
        }
        for(size_t i=0u;i<ir.resources.graphics_state_count;++i)ir.resources.graphics_states[i].clip_id=SIZE_MAX;
        for(size_t i=0u;i<ir.operation_count;++i)ir.operations[i].graphics_state_id=UINT32_MAX;
        CHECK(quantapdf_native_compiled_page_execute_v1(&compiled,nullptr,&output)==QUANTAPDF_OK && equal(output));
        for(size_t i=0u;i<ir.resources.clip_count;++i) {
            auto& clip=ir.resources.clips[i];
            if(!commands[i].empty())std::copy(commands[i].begin(),commands[i].end(),const_cast<quantapdf_composer_path_command *>(clip.commands));
            if(!members[i].empty())std::copy(members[i].begin(),members[i].end(),const_cast<quantapdf_composer_clip_id *>(clip.members));
        }
        std::copy(states.begin(),states.end(),ir.resources.graphics_states);
        std::copy(operations.begin(),operations.end(),ir.operations);CHECK(text_ir_snapshot(ir)==snapshot);
        quantapdf_native_compiled_page_dispose_v1(&compiled);quantapdf_ir_dispose(&ir);CHECK(cleared(ir));
        CHECK(equal(output));quantapdf_ir_raster_image_dispose(&output);
    }
    std::printf("Clip graph Source wide=%d: %zu definition growth faults, %zu path growth faults, %zu state growth faults, %zu publication faults, %zu C11 malloc faults, %zu exact N/N-1 checks; independent RGBA and deep graph snapshots\n",int(wide),clip_faults,path_faults,stack_faults,publications,raster_faults,quotas);
    return EXIT_SUCCESS;
}
static int clip_graph_dpi_contracts(const quantapdf_qpdf_page_limits& defaults,bool fractional=false,bool cropped=false)
{
    size_t renders=0u,faults_seen=0u,boundaries=0u;
    target=0u;armed=false;
    for(bool encrypted:{false,true}) {
        native_clip_graph::options opt;opt.scene=encrypted?2u:0u;opt.primitive=4u;opt.fringe=true;
        if(fractional) {opt.width_ticks=encrypted?196:188;opt.height_ticks=encrypted?180:204;opt.glyph_fringe=true;}
        if(cropped)opt=native_clip_graph::viewport_case(opt,encrypted?3u:2u);
        auto sample=native_clip_graph::create(opt,encrypted);quantapdf_qpdf_document *document=nullptr;
        CHECK(quantapdf_qpdf_open_memory(reinterpret_cast<const unsigned char*>(sample.bytes.data()),sample.bytes.size(),
            encrypted?"reader":"",&document)==QUANTAPDF_OK);
        std::unique_ptr<quantapdf_qpdf_document,decltype(&quantapdf_qpdf_close)> owner(document,quantapdf_qpdf_close);
        auto caps=defaults;caps.max_content_bytes=sample.decoded_bytes;caps.max_content_streams=3u;
        caps.max_operations=sample.operations;
        quantapdf_ir blank{},ir{};
        CHECK(quantapdf_qpdf_page_to_ir(owner.get(),0u,&caps,&blank)==QUANTAPDF_OK && blank.operation_count==0u);
        if(fractional || cropped) {track_c_allocation=true;c_allocations=0u;c_fail_index=0u;c_faults=0u;}
        const auto staged=quantapdf_qpdf_page_to_ir(owner.get(),1u,&caps,&ir);track_c_allocation=false;
        CHECK(staged==QUANTAPDF_OK && ir.operation_count==sample.operations);
        if(fractional || cropped) {
            const size_t calls=c_allocations;
            CHECK(calls>0u && ir.pages[0].width_points==float(sample.width_ticks)/16.0f &&
                ir.pages[0].height_points==float(sample.height_ticks)/16.0f);
            for(size_t fail=1u;fail<=calls;++fail) {
                quantapdf_ir denied;std::memset(&denied,0xa5,sizeof(denied));
                track_c_allocation=true;c_fail_index=fail;c_allocations=0u;c_faults=0u;
                const auto error=quantapdf_qpdf_page_to_ir(owner.get(),1u,&caps,&denied);
                track_c_allocation=false;c_fail_index=0u;
                CHECK(error==QUANTAPDF_ERROR_NOMEM && cleared(denied) && c_faults==1u && c_allocations==fail);
            }
            if(sample.width_ticks%16!=0 || sample.height_ticks%16!=0)
                CHECK(native_clip_graph::rgb(sample,300u)!=native_clip_graph::rgb(sample,300u,true));
            if(cropped)CHECK(native_clip_graph::rgb(sample,300u)!=native_clip_graph::rgb(sample,300u,false,true));
        }
        owner.reset();sample.bytes.assign(sample.bytes.size(),'x');
        for(quantapdf_ir *source:{&blank,&ir})for(unsigned dpi:{1u,23u,73u,113u,150u,300u}) {
            const bool empty=source==&blank;
            const size_t w=empty?(5u*dpi+71u)/72u:native_clip_graph::width(sample,dpi);
            const size_t h=empty?(4u*dpi+71u)/72u:native_clip_graph::height(sample,dpi);
            const auto expected=empty?std::vector<unsigned char>(w*h*3u,255u):native_clip_graph::rgb(sample,dpi);
            const auto snapshot=text_ir_snapshot(*source);
            const auto equal=[&](const quantapdf_ir_raster_image& image) {
                if(image.width!=w || image.height!=h || image.stride!=4u*w || image.size!=4u*w*h)return false;
                for(size_t i=0u;i<w*h;++i)
                    if(std::memcmp(image.pixels+4u*i,expected.data()+3u*i,3u)!=0 || image.pixels[4u*i+3u]!=255u)return false;
                return true;
            };
            const quantapdf_ir_raster_scoped_mixed_v5_limits budget={65536u,sample.operations,512u,8000000u,1000000u,4000000u};
            quantapdf_native_page_device device{};device.dpi=float(dpi);
            quantapdf_native_prepared_page_v1 metadata{};
            CHECK(quantapdf_native_prepare_page_v1(source,&budget,&device,&metadata)==QUANTAPDF_OK &&
                metadata.route==QP_NATIVE_PREPARED_SCENE);
            m2a_malloc_fault_state allocation{};metadata.malloc_fault={&allocation,m2a_malloc_fault_callback};
            quantapdf_native_execution_receipt_v1 receipt{};quantapdf_ir_raster_image output{};
            const auto status=quantapdf_native_execute_page_v1(&metadata,&receipt,&output);
            if(status!=QUANTAPDF_OK || !equal(output))
                std::fprintf(stderr,"integer DPI empty=%d dpi=%u status=%d RGB=%d\n",int(empty),dpi,int(status),int(status==QUANTAPDF_OK && equal(output)));
            CHECK(status==QUANTAPDF_OK && equal(output) && receipt.sample_tests_exact && allocation.attempts>0u);
            quantapdf_ir_raster_image_dispose(&output);++renders;
            for(size_t fail=1u;fail<=allocation.attempts;++fail) {
                m2a_malloc_fault_state fault{};fault.fail_on=fail;
                auto candidate=metadata;candidate.malloc_fault={&fault,m2a_malloc_fault_callback};
                quantapdf_native_execution_receipt_v1 denied{},zero{};std::memset(&output,0xa5,sizeof(output));
                CHECK(quantapdf_native_execute_page_v1(&candidate,&denied,&output)==QUANTAPDF_ERROR_NOMEM &&
                    zero_bitmap(output) && std::memcmp(&denied,&zero,sizeof(zero))==0 &&
                    fault.attempts==fail && fault.failures==1u && text_ir_snapshot(*source)==snapshot);++faults_seen;
            }
            const auto trial=[&](const quantapdf_ir_raster_scoped_mixed_v5_limits& limits,size_t bytes,quantapdf_status wanted) {
                quantapdf_native_prepared_page_v1 candidate{};quantapdf_ir_raster_image image{};
                auto error=quantapdf_native_prepare_page_v1(source,&limits,&device,&candidate);
                if(error==QUANTAPDF_OK) {
                    if(bytes!=0u)candidate.resource_limits.max_live_bytes=bytes;
                    error=quantapdf_native_execute_page_v1(&candidate,nullptr,&image);
                }
                const bool ok=error==wanted && text_ir_snapshot(*source)==snapshot &&
                    (wanted==QUANTAPDF_OK?equal(image):zero_bitmap(image));
                quantapdf_ir_raster_image_dispose(&image);return ok;
            };
            CHECK(trial(budget,receipt.peak_live_bytes,QUANTAPDF_OK));
            CHECK(trial(budget,receipt.peak_live_bytes-1u,QUANTAPDF_ERROR_UNSUPPORTED));++boundaries;
            for(unsigned quota=0u;quota<6u;++quota) {
                const size_t count=quota==0u?source->operation_count:quota==1u?receipt.compiled_edges:
                    quota==2u?receipt.sample_tests:quota==3u?w*h:quota==4u?receipt.source_pixels:receipt.drawn_pixels;
                if(count==0u)continue;
                auto limits=budget;
                size_t *cap=quota==0u?&limits.max_operations:quota==1u?&limits.max_edges:
                    quota==2u?&limits.max_sample_tests:quota==3u?&limits.max_pixels:
                    quota==4u?&limits.max_source_pixels:&limits.max_drawn_pixels;
                *cap=count;CHECK(trial(limits,0u,QUANTAPDF_OK));
                --*cap;CHECK(trial(limits,0u,quota==3u && count==1u?
                    QUANTAPDF_ERROR_ARGUMENT:QUANTAPDF_ERROR_UNSUPPORTED));++boundaries;
            }
        }
        quantapdf_ir_dispose(&blank);quantapdf_ir_dispose(&ir);CHECK(cleared(blank) && cleared(ir));
    }
    CHECK(renders==24u && faults_seen>0u && boundaries>0u);
    std::printf("integer DPI Source fractional=%d crop=%d: %zu independent RGBA results, %zu actual C11 malloc faults, %zu N/N-1 boundaries; blank and Text/Fill/Image/Clip at original DPI after document destruction\n",int(fractional),int(cropped),renders,faults_seen,boundaries);
    return EXIT_SUCCESS;
}
#endif
