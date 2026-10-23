# Byte-pinned admitted original yWorks PDF; official W* pixel-positive report.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL 3747)
  message(FATAL_ERROR "Original yWorks clip-rule PDF length changed: ${bytes}")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT before STREQUAL EXPECTED_SHA256)
  message(FATAL_ERROR "Original yWorks clip-rule PDF SHA-256 mismatch")
endif()
execute_process(
  COMMAND "${ORACLE}" --external-wstar-positive-pdf "${SOURCE}"
    --external-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Original PDFium vs detached C11 W* pixel oracle failed: ${rc}: ${err}")
endif()
file(READ "${REPORT}" json)
foreach(field IN ITEMS result fixture_kind reference_oracle document_id
    native_expected_outcome native_observed_outcome pdfium_result
    original_source_pdfium_oracle detached_c11_native_raster native_ir
    source_sha256)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
foreach(field IN ITEMS official_corpus_admitted
    source_and_graph_released_before_raster
    source_bytes_unchanged_after_reference_and_native pdfium_fallback_used
    repeat_rgba_identical)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
foreach(field IN ITEMS page_width page_height native_painted_paths
    native_w_nonzero_clip_path_leaves native_wstar_evenodd_clip_path_leaves
    native_clip_intersection_nodes native_total_clip_nodes fixed_interior_goldens
    external_wstar_pixel_supported_count native_wstar_clip_supported_count
    native_supported_passed_count generated_fixture_count source_bytes
    gross_error_pixels reference_red_pixels native_red_pixels overlap_red_pixels
    reference_blue_pixels native_blue_pixels overlap_blue_pixels)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
string(JSON mean GET "${json}" mean_rgb_error)
if(NOT result STREQUAL "passed"
   OR NOT fixture_kind STREQUAL "external"
   OR NOT reference_oracle STREQUAL "original-pdfium"
   OR NOT document_id STREQUAL "yworks.clippath-cliprule-v1"
   OR NOT native_expected_outcome STREQUAL "supported-wstar-clip"
   OR NOT native_observed_outcome STREQUAL "supported-wstar-clip"
   OR NOT pdfium_result STREQUAL "rendered_wstar_clip"
   OR NOT original_source_pdfium_oracle STREQUAL "passed"
   OR NOT detached_c11_native_raster STREQUAL "passed"
   OR NOT native_ir STREQUAL "lowered"
   OR NOT source_sha256 STREQUAL EXPECTED_SHA256
   OR NOT official_corpus_admitted
   OR NOT source_and_graph_released_before_raster
   OR NOT source_bytes_unchanged_after_reference_and_native
   OR pdfium_fallback_used
   OR NOT repeat_rgba_identical
   OR NOT page_width EQUAL 600 OR NOT page_height EQUAL 600
   OR NOT native_painted_paths EQUAL 3
   OR NOT native_w_nonzero_clip_path_leaves EQUAL 2
   OR NOT native_wstar_evenodd_clip_path_leaves EQUAL 2
   OR NOT native_clip_intersection_nodes EQUAL 3
   OR NOT native_total_clip_nodes EQUAL 7
   OR NOT fixed_interior_goldens EQUAL 8
   OR NOT external_wstar_pixel_supported_count EQUAL 1
   OR NOT native_wstar_clip_supported_count EQUAL 1
   OR NOT native_supported_passed_count EQUAL 1
   OR NOT generated_fixture_count EQUAL 0
   OR NOT source_bytes EQUAL 3747
   OR mean GREATER 0.05 OR NOT gross_error_pixels EQUAL 0
   OR reference_red_pixels LESS 150 OR native_red_pixels LESS 150
   OR reference_blue_pixels LESS 2000 OR native_blue_pixels LESS 2000)
  message(FATAL_ERROR "Official W* report has unexpected semantics, provenance, or counts")
endif()
math(EXPR required_red_overlap "${reference_red_pixels} * 99")
math(EXPR actual_red_overlap "${overlap_red_pixels} * 100")
if(actual_red_overlap LESS required_red_overlap)
  message(FATAL_ERROR "Original W* red stroke mismatch beyond 1%")
endif()
math(EXPR required_blue_overlap "${reference_blue_pixels} * 99")
math(EXPR actual_blue_overlap "${overlap_blue_pixels} * 100")
if(actual_blue_overlap LESS required_blue_overlap)
  message(FATAL_ERROR "Original W* blue fill mismatch beyond 1%")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original yWorks clip-rule PDF mutated during pixel oracle")
endif()
message(STATUS "wstar-original-pixel: passed; sha=${after} W=${native_w_nonzero_clip_path_leaves} W*=${native_wstar_evenodd_clip_path_leaves} official_positive=1")
