# Original, byte-pinned W-only page preflight (not a W* pixel-positive).
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256 EXPECTED_SIZE CASE_ID)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
if(NOT EXISTS "${SOURCE}")
  message(FATAL_ERROR "Unchanged external W candidate missing")
endif()
file(SIZE "${SOURCE}" byte_count)
if(NOT byte_count EQUAL EXPECTED_SIZE)
  message(FATAL_ERROR "Original W candidate byte count changed: ${byte_count}")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT "${before}" STREQUAL "${EXPECTED_SHA256}")
  message(FATAL_ERROR "Original W candidate SHA256 mismatch: ${before}")
endif()
# Never unlink a prior successful JSON report before the typed oracle starts.
execute_process(
  COMMAND "${ORACLE}" --preflight-original-pdf "${SOURCE}"
    --page-index 0 --expect-status observe --preflight-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Real selected-page typed observe failed: ${rc}: ${err}")
endif()
file(READ "${REPORT}" json)
foreach(field IN ITEMS native_lower_status observation_only
    original_source_provenance_verified source_document_graph_released
    detached_ir_pages detached_ir_operations detached_ir_clips
    detached_ir_graphics_states detached_ir_w_nonzero_clip_path_leaves
    detached_ir_wstar_evenodd_clip_path_leaves original_pdfium_pixel_oracle
    native_raster external_wstar_pixel_supported_count generated_fixture_count)
  string(JSON value_${field} GET "${json}" ${field})
endforeach()
if(NOT value_observation_only OR value_original_source_provenance_verified
   OR NOT value_source_document_graph_released
   OR NOT value_original_pdfium_pixel_oracle STREQUAL "not_run"
   OR NOT value_native_raster STREQUAL "not_run"
   OR NOT value_external_wstar_pixel_supported_count EQUAL 0
   OR NOT value_generated_fixture_count EQUAL 0)
  message(FATAL_ERROR "W-only candidate leaked pixel/admission evidence")
endif()
if(value_native_lower_status STREQUAL "lowered")
  if(NOT value_detached_ir_pages EQUAL 1
     OR value_detached_ir_operations LESS 1
     OR value_detached_ir_w_nonzero_clip_path_leaves LESS 1
     OR NOT value_detached_ir_wstar_evenodd_clip_path_leaves EQUAL 0)
    message(FATAL_ERROR "W-only lowered page missing owned W Clip or gained W*")
  endif()
elseif(value_native_lower_status STREQUAL "unsupported")
  if(NOT value_detached_ir_pages EQUAL 0
     OR NOT value_detached_ir_operations EQUAL 0
     OR NOT value_detached_ir_clips EQUAL 0
     OR NOT value_detached_ir_graphics_states EQUAL 0
     OR NOT value_detached_ir_w_nonzero_clip_path_leaves EQUAL 0
     OR NOT value_detached_ir_wstar_evenodd_clip_path_leaves EQUAL 0)
    message(FATAL_ERROR "Unsupported original W-only candidate leaked partial IR")
  endif()
else()
  message(FATAL_ERROR "Unexpected typed status: ${value_native_lower_status}")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT "${before}" STREQUAL "${after}")
  message(FATAL_ERROR "Original W-only source PDF mutated by typed preflight")
endif()
message(STATUS "external W-only candidate: ${CASE_ID} typed=${value_native_lower_status} W=${value_detached_ir_w_nonzero_clip_path_leaves} W*=0 pixel=not_run external_positive=0")
