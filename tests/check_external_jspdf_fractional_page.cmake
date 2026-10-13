# Exact published jsPDF W-only original, negative native raster admission.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL 3344)
  message(FATAL_ERROR "Original jsPDF page byte size changed")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT before STREQUAL EXPECTED_SHA256)
  message(FATAL_ERROR "Original jsPDF SHA256 changed")
endif()
execute_process(
  COMMAND "${ORACLE}" --external-fractional-jspdf-pdf "${SOURCE}"
    --external-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Raw jsPDF fractional selected-page negative failed: ${rc}: ${stderr}")
endif()
file(READ "${REPORT}" json)
foreach(field IN ITEMS result typed_native_lower_status native_raster_status
    native_raster_rejection_reason original_pdfium_pixel_oracle)
  string(JSON got_${field} GET "${json}" ${field})
endforeach()
string(JSON width GET "${json}" page_width_points)
string(JSON height GET "${json}" page_height_points)
string(JSON paths GET "${json}" detached_ir_painted_paths)
string(JSON w GET "${json}" detached_ir_w_nonzero_clip_leaves)
string(JSON wstar GET "${json}" detached_ir_wstar_clip_leaves)
string(JSON observed GET "${json}" observation_only)
string(JSON released GET "${json}" source_and_graph_released_before_raster)
string(JSON partial GET "${json}" native_partial_pixels_published)
string(JSON positive GET "${json}" native_pixel_positive_count)
string(JSON admitted GET "${json}" official_corpus_admitted)
string(JSON generated GET "${json}" generated_fixture_count)
if(NOT got_result STREQUAL "quarantined_original_fractional_page_native_rejection"
   OR NOT got_typed_native_lower_status STREQUAL "lowered"
   OR NOT got_native_raster_status STREQUAL "unsupported"
   OR NOT got_native_raster_rejection_reason STREQUAL "fractional_page_points"
   OR NOT got_original_pdfium_pixel_oracle STREQUAL "not_run"
   OR NOT width EQUAL 595.28 OR NOT height EQUAL 841.89
   OR NOT paths EQUAL 2 OR NOT w EQUAL 1 OR NOT wstar EQUAL 0
   OR NOT observed OR NOT released OR partial OR positive OR admitted OR generated)
  message(FATAL_ERROR "Fractional page refusal was not complete and correctly classified")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original jsPDF source mutated")
endif()
message(STATUS "fractional-page-jsPDF: typed=lowered raster=UNSUPPORTED original_sha256=${after} official=0")
