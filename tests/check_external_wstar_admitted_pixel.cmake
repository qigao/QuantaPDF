# Original W* PDF independent PDFium/owned C11 whole-page conformance.
# This official report is counted ONLY through six-original reconciliation.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
if("${SOURCE}" STREQUAL "${REPORT}" OR "${SOURCE}" STREQUAL "${REPORT}.tmp")
  message(FATAL_ERROR "Report cannot alias original source")
endif()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL 3747)
  message(FATAL_ERROR "Original PDF byte length mismatch")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT "${before}" STREQUAL "${EXPECTED_SHA256}")
  message(FATAL_ERROR "Original PDF SHA-256 mismatch")
endif()
# The C++ oracle atomically publishes only after valid native pixels.
# Retain an earlier successful report if source parsing or raster fails.
file(REMOVE "${REPORT}.tmp")
execute_process(
  COMMAND "${ORACLE}" --external-wstar-original-pdf "${SOURCE}"
      --external-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Original W* PDFium/C11 pixel oracle failed: ${rc}: ${err}")
endif()
file(READ "${REPORT}" json)
string(JSON result GET "${json}" result)
string(JSON id GET "${json}" document_id)
string(JSON corpus GET "${json}" corpus_id)
string(JSON kind GET "${json}" fixture_kind)
string(JSON reference GET "${json}" reference_oracle)
string(JSON expected GET "${json}" native_expected_outcome)
string(JSON actual GET "${json}" native_observed_outcome)
string(JSON oracle GET "${json}" original_source_pdfium_oracle)
string(JSON native GET "${json}" detached_c11_native_raster)
string(JSON detached GET "${json}" source_and_graph_released_before_raster)
string(JSON repeat GET "${json}" repeat_rgba_identical)
string(JSON width GET "${json}" page_width)
string(JSON height GET "${json}" page_height)
string(JSON nonzero GET "${json}" native_w_nonzero_clip_path_leaves)
string(JSON evenodd GET "${json}" native_wstar_evenodd_clip_path_leaves)
string(JSON intersections GET "${json}" native_clip_intersection_nodes)
string(JSON goldens GET "${json}" fixed_interior_goldens)
string(JSON pathcount GET "${json}" native_path_clip_supported_count)
string(JSON clipped GET "${json}" native_clipped_path_supported_count)
string(JSON wcount GET "${json}" native_w_clip_supported_count)
string(JSON wstarcount GET "${json}" native_wstar_clip_supported_count)
string(JSON generated GET "${json}" generated_fixture_count)
string(JSON mean GET "${json}" mean_rgb_error)
string(JSON gross GET "${json}" gross_error_pixels)
string(JSON ref_red GET "${json}" reference_red_pixels)
string(JSON native_red GET "${json}" native_red_pixels)
string(JSON overlap GET "${json}" overlap_red_pixels)
if(NOT result STREQUAL "passed" OR NOT id STREQUAL "yworks.clippath-cliprule-v1"
   OR NOT corpus STREQUAL "external-source-pdf" OR NOT kind STREQUAL "external"
   OR NOT reference STREQUAL "original-pdfium"
   OR NOT expected STREQUAL "supported-path-clip" OR NOT actual STREQUAL expected
   OR NOT oracle STREQUAL "passed" OR NOT native STREQUAL "passed"
   OR NOT detached OR NOT repeat OR NOT width EQUAL 600 OR NOT height EQUAL 600
   OR NOT nonzero EQUAL 2 OR NOT evenodd EQUAL 2 OR NOT intersections EQUAL 3
   OR NOT goldens EQUAL 6 OR NOT pathcount EQUAL 1 OR NOT clipped EQUAL 1
   OR NOT wcount EQUAL 1 OR NOT wstarcount EQUAL 1 OR NOT generated EQUAL 0
   OR mean GREATER 1.0 OR gross GREATER 0
   OR ref_red LESS 25 OR native_red LESS 25 OR overlap LESS 25)
  message(FATAL_ERROR "Official original W* pixel report evidence failed strict validation")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT "${before}" STREQUAL "${after}")
  message(FATAL_ERROR "Original PDF bytes mutated by pixel test")
endif()
message(STATUS "admitted Wstar original: PDFium/C11 parity PASSED; sha=${after}; mean=${mean}; gross=${gross}; Wstar_supported_report=1")
