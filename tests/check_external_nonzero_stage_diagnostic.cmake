# Explicit, test-only native original-source stage attribution.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256 EXPECTED_SIZE)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
if(NOT EXISTS "${SOURCE}")
  message(FATAL_ERROR "Raw candidate PDF missing")
endif()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL EXPECTED_SIZE)
  message(FATAL_ERROR "Original source PDF length mismatch")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT before STREQUAL EXPECTED_SHA256)
  message(FATAL_ERROR "Original PDF SHA-256 mismatch")
endif()
execute_process(
  COMMAND "${ORACLE}" --diagnose-original-pdf "${SOURCE}"
    --diagnostic-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Original source stage diagnostic failed: ${rc}: ${err}")
endif()
file(READ "${REPORT}" json)
string(JSON result GET "${json}" result)
string(JSON failed_stage GET "${json}" failed_stage)
string(JSON budget GET "${json}" paint_budget)
string(JSON observed GET "${json}" observation_only)
string(JSON provenance GET "${json}" original_source_provenance_verified)
string(JSON released GET "${json}" source_document_graph_released)
string(JSON pages GET "${json}" detached_ir_pages)
string(JSON ops GET "${json}" detached_ir_operations)
string(JSON pixel GET "${json}" original_pdfium_pixel_oracle)
string(JSON raster GET "${json}" native_raster)
string(JSON positives GET "${json}" external_wstar_pixel_supported_count)
string(JSON generated GET "${json}" generated_fixture_count)
string(JSON typed GET "${json}" native_lower_status)
string(JSON total_paths GET "${json}" parsed_path_records)
string(JSON first_type TYPE "${json}" first_rejected_path_record_index)
string(JSON first_present GET "${json}" first_rejected_record_is_present)
string(JSON clip_nodes GET "${json}" detached_ir_clips)
string(JSON nonzero GET "${json}" detached_ir_w_nonzero_clip_path_leaves)
string(JSON wstar GET "${json}" detached_ir_wstar_evenodd_clip_path_leaves)
string(JSON intersections GET "${json}" detached_ir_clip_intersection_nodes)
string(JSON guard GET "${json}" first_rejected_numeric_guard)
string(JSON code GET "${json}" actual_status_code)
if(NOT result STREQUAL "test_only_original_source_stage_diagnostic"
   OR NOT budget EQUAL 8
   OR NOT observed OR provenance OR NOT released
   OR NOT pixel STREQUAL "not_run" OR NOT raster STREQUAL "not_run"
   OR NOT positives EQUAL 0 OR NOT generated EQUAL 0
   OR NOT total_paths EQUAL 11)
  message(FATAL_ERROR "Stage diagnostic contaminated source/pixel evidence")
endif()
# Successful typed lowering is *not* native C11 pixel compatibility.
# Original W-only page has five W Clip leaves and six painted operations,
# beyond the current separate four-operation/eight-node native raster gate.
if(NOT typed STREQUAL "lowered" OR NOT failed_stage STREQUAL "none"
   OR NOT code EQUAL 0 OR first_present
   OR NOT first_type STREQUAL "NULL"
   OR NOT guard STREQUAL "none"
   OR NOT pages EQUAL 1 OR NOT ops EQUAL 6
   OR NOT nonzero EQUAL 5 OR NOT wstar EQUAL 0
   OR NOT intersections GREATER 0
   OR clip_nodes LESS 5 OR clip_nodes GREATER 16)
  message(FATAL_ERROR "Original source was not fully lowered into quarantined detached IR")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original PDF mutated by diagnostic")
endif()
message(STATUS "original-stage-diag: stage=${failed_stage} status=lowered clips=${clip_nodes} W=${nonzero} pixels=not_run external_positive=0")
