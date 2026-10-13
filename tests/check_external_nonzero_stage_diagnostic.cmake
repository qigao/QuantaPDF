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
string(JSON first_path GET "${json}" first_rejected_path_record_index)
string(JSON first_status GET "${json}" first_rejected_prefix_status)
string(JSON total_paths GET "${json}" parsed_path_records)
string(JSON ctm_type TYPE "${json}" first_rejected_source_ctm)
string(JSON candidate_y GET "${json}" first_rejected_conjugated_y_translation)
string(JSON bound GET "${json}" composer_transform_component_abs_bound)
string(JSON guard GET "${json}" first_rejected_numeric_guard)
string(JSON operator_start GET "${json}" first_rejected_path_first_operator_index)
string(JSON operator_end GET "${json}" first_rejected_path_terminal_operator_index)
string(JSON is_clip GET "${json}" first_rejected_introduces_clip)
string(JSON opcode GET "${json}" first_rejected_paint_opcode)
if(NOT ctm_type STREQUAL "ARRAY" OR NOT first_status EQUAL 5
   OR NOT first_path EQUAL 3 OR NOT first_path LESS total_paths
   OR NOT operator_start EQUAL 31 OR NOT operator_end EQUAL 37
   OR NOT is_clip EQUAL 1 OR NOT opcode EQUAL 19
   OR NOT bound EQUAL 65536 OR NOT candidate_y EQUAL 240100
   OR NOT guard STREQUAL "conjugated_y_translation_exceeds_bound")
  message(FATAL_ERROR "First unsupported prefix diagnostic missing/invalid")
endif()
if(NOT result STREQUAL "test_only_original_source_stage_diagnostic"
    OR failed_stage STREQUAL "none" OR NOT budget EQUAL 8
    OR NOT observed OR provenance OR NOT released
    OR NOT pages EQUAL 0 OR NOT ops EQUAL 0
    OR NOT pixel STREQUAL "not_run" OR NOT raster STREQUAL "not_run"
    OR NOT positives EQUAL 0 OR NOT generated EQUAL 0)
  message(FATAL_ERROR "Stage diagnostic contaminated support/pixel evidence")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original PDF mutated by diagnostic")
endif()
message(STATUS "original-stage-diag: stage=${failed_stage} status=unsupported source_unchanged=1 pixels=not_run")
